#include "VersionControl.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/JSON.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
    #include <sys/wait.h>
#endif

namespace Conqueror::Editor::VersionControl
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Cache
        {
            std::mutex Mutex;
            std::unordered_map<std::string, FileStatus> Files;   // proje göreli yol -> durum
            std::unordered_set<std::string> Tracked;             // Git: izlenen dosyalar (proje göreli)
            std::string Project;                                  // önbelleğin ait olduğu proje klasörü
            int ModeAtRefresh = 0;
            std::chrono::steady_clock::time_point Last{};
            std::atomic<bool> Running{ false };
            std::thread Worker;
        };

        Cache& C()
        {
            static Cache cache;
            return cache;
        }

        std::string Quote(const std::string& s)
        {
#if defined(_WIN32)
            return "\"" + s + "\"";
#else
            std::string out = "'";
            for (char c : s)
                out += c == '\'' ? std::string("'\\''") : std::string(1, c);
            return out + "'";
#endif
        }

        // Komutu dir klasöründe çalıştırır; çıktı (stdout + stderr) ve çıkış kodu
        int Run(const std::string& dir, const std::string& command, std::string& output)
        {
            output.clear();
#if defined(_WIN32)
            const std::string full = "cd /d " + Quote(dir) + " && " + command + " 2>&1";
            FILE* pipe = _popen(full.c_str(), "r");
#else
            const std::string full = "cd " + Quote(dir) + " && " + command + " 2>&1";
            FILE* pipe = popen(full.c_str(), "r");
#endif
            if (!pipe)
                return -1;
            char buffer[4096];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
                output.append(buffer, n);
#if defined(_WIN32)
            return _pclose(pipe);
#else
            const int status = pclose(pipe);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }

        std::string ProjectDir()
        {
            return Project::GetActive() ? Project::GetActiveProjectDirectory().lexically_normal().string() : std::string();
        }

        const VersionControlSettings* Settings()
        {
            auto project = Project::GetActive();
            return project ? &project->GetConfig().VersionControl : nullptr;
        }

        // Proje göreli, '/' ayraçlı yol (proje dışındaysa boş)
        std::string Relative(const std::string& path)
        {
            const std::string project = ProjectDir();
            if (project.empty() || path.empty())
                return {};
            fs::path p(path);
            if (p.is_relative())
                p = fs::path(project) / p;
            std::error_code ec;
            const fs::path rel = fs::relative(p.lexically_normal(), project, ec);
            if (ec || rel.empty() || rel.native().rfind(fs::path("..").native(), 0) == 0)
                return {};
            return rel.generic_string();
        }

        std::string P4Command(const VersionControlSettings& s)
        {
            std::string exe = s.P4Executable.empty() ? std::string("p4") : s.P4Executable;
            // Proje göreli yol (ör. Tools/p4) projeye göre çözülür; yalnız ad PATH'te aranır
            if (exe.find('/') != std::string::npos || exe.find('\\') != std::string::npos)
            {
                fs::path p(exe);
                if (p.is_relative())
                    p = fs::path(ProjectDir()) / p;
                exe = Quote(p.string());
            }
            std::string cmd = exe;
            if (!s.P4Port.empty()) cmd += " -p " + Quote(s.P4Port);
            if (!s.P4User.empty()) cmd += " -u " + Quote(s.P4User);
            if (!s.P4Client.empty()) cmd += " -c " + Quote(s.P4Client);
            return cmd;
        }

        // ---- Git
        void RefreshGit(const std::string& project, std::unordered_map<std::string, FileStatus>& files, std::unordered_set<std::string>& tracked)
        {
            std::string out;
            if (Run(project, "git rev-parse --show-toplevel", out) != 0)
                return;
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                out.pop_back();
            const fs::path root = fs::path(out).lexically_normal();
            auto toProject = [&](const std::string& repoPath) -> std::string {
                std::error_code ec;
                const fs::path rel = fs::relative((root / repoPath).lexically_normal(), project, ec);
                if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
                    return {};
                return rel.generic_string();
            };
            // İzlenen dosyalar (depo köküne göre)
            if (Run(root.string(), "git ls-files -z", out) == 0)
            {
                size_t start = 0;
                for (size_t i = 0; i <= out.size(); ++i)
                    if (i == out.size() || out[i] == '\0')
                    {
                        if (i > start)
                            if (std::string rel = toProject(out.substr(start, i - start)); !rel.empty())
                                tracked.insert(rel);
                        start = i + 1;
                    }
            }
            // Değişiklikler: XY yol (porcelain v1, depo köküne göre)
            if (Run(root.string(), "git status --porcelain=v1 -uall -z", out) == 0)
            {
                size_t i = 0;
                while (i + 3 < out.size())
                {
                    const char x = out[i], y = out[i + 1];
                    size_t end = out.find('\0', i + 3);
                    if (end == std::string::npos)
                        end = out.size();
                    const std::string path = out.substr(i + 3, end - (i + 3));
                    i = end + 1;
                    if (x == 'R' || x == 'C')   // yeniden adlandırma: ardından eski yol gelir
                    {
                        const size_t old = out.find('\0', i);
                        i = old == std::string::npos ? out.size() : old + 1;
                    }
                    const std::string rel = toProject(path);
                    if (rel.empty())
                        continue;
                    FileStatus st;
                    if (x == '?' && y == '?')
                        st.Status = State::NotVersioned;
                    else if (x == 'A')
                        st.Status = State::Added;
                    else
                        st.Status = State::Modified;
                    files[rel] = st;
                }
            }
            // Git LFS kilitleri (sunucu gerektirir; yoksa sessizce atlanır)
            if (Run(root.string(), "git lfs locks --verify --json", out) == 0 && !out.empty() && out[0] == '{')
            {
                auto field = [](const JSON::Value& v, const char* key) -> const JSON::Value* {
                    if (v.Type != JSON::ValueType::Map)
                        return nullptr;
                    auto it = v.MapVal.find(key);
                    return it == v.MapVal.end() ? nullptr : &it->second;
                };
                auto apply = [&](const JSON::Value* list, bool ours) {
                    if (!list || list->Type != JSON::ValueType::List)
                        return;
                    for (const auto& lock : list->ListVal)
                    {
                        const JSON::Value* path = field(lock, "path");
                        if (!path || path->Type != JSON::ValueType::String)
                            continue;
                        const std::string rel = toProject(path->StringVal);
                        if (rel.empty())
                            continue;
                        FileStatus st;
                        st.Status = ours ? State::LockedLocal : State::LockedOther;
                        if (const JSON::Value* owner = field(lock, "owner"))
                            if (const JSON::Value* name = field(*owner, "name"); name && name->Type == JSON::ValueType::String)
                                st.Owner = name->StringVal;
                        files[rel] = st;
                    }
                };
                const JSON::Value v = JSON::Parse(out);
                apply(field(v, "ours"), true);
                apply(field(v, "theirs"), false);
            }
        }

        // ---- Perforce: p4 -ztag fstat çıktısı (kayıtlar boş satırla ayrılır)
        void RefreshPerforce(const std::string& project, const VersionControlSettings& s, std::unordered_map<std::string, FileStatus>& files,
                             std::unordered_set<std::string>& tracked)
        {
            std::string out;
            Run(project, P4Command(s) + " -ztag fstat " + Quote((fs::path(project) / "...").string()), out);
            std::istringstream in(out);
            std::string line;
            std::unordered_map<std::string, std::string> record;
            auto flush = [&]() {
                auto it = record.find("clientFile");
                if (it != record.end())
                {
                    std::error_code ec;
                    const fs::path rel = fs::relative(fs::path(it->second).lexically_normal(), project, ec);
                    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0)
                    {
                        const std::string key = rel.generic_string();
                        FileStatus st;
                        const std::string action = record.count("action") ? record["action"] : std::string();
                        if (record.count("otherLock") || record.count("otherLock0"))
                        {
                            st.Status = State::LockedOther;
                            st.Owner = record.count("otherLock0") ? record["otherLock0"] : std::string();
                        }
                        else if (record.count("ourLock"))
                            st.Status = State::LockedLocal;
                        else if (action == "add")
                            st.Status = State::Added;
                        else if (!action.empty())
                            st.Status = State::CheckedOut;
                        else if (record.count("otherOpen0"))
                        {
                            st.Status = State::CheckedOutOther;
                            st.Owner = record["otherOpen0"];
                        }
                        else
                            st.Status = State::Clean;
                        if (action != "add")
                            tracked.insert(key);
                        files[key] = st;
                    }
                }
                record.clear();
            };
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                {
                    flush();
                    continue;
                }
                if (line.rfind("... ", 0) != 0)
                    continue;
                const size_t space = line.find(' ', 4);
                const std::string key = line.substr(4, space == std::string::npos ? std::string::npos : space - 4);
                record[key] = space == std::string::npos ? std::string() : line.substr(space + 1);
            }
            flush();
        }

        void StartRefresh()
        {
            Cache& c = C();
            if (c.Running.exchange(true))
                return;
            if (c.Worker.joinable())
                c.Worker.join();
            const std::string project = ProjectDir();
            const VersionControlSettings settings = Settings() ? *Settings() : VersionControlSettings{};
            c.Worker = std::thread([project, settings]() {
                std::unordered_map<std::string, FileStatus> files;
                std::unordered_set<std::string> tracked;
                if (!project.empty() && settings.Mode == 1)
                    RefreshGit(project, files, tracked);
                else if (!project.empty() && settings.Mode == 2)
                    RefreshPerforce(project, settings, files, tracked);
                Cache& cache = C();
                {
                    std::lock_guard lock(cache.Mutex);
                    cache.Files = std::move(files);
                    cache.Tracked = std::move(tracked);
                    cache.Project = project;
                    cache.ModeAtRefresh = settings.Mode;
                    cache.Last = std::chrono::steady_clock::now();
                }
                cache.Running = false;
            });
        }

        bool RunOperation(const std::string& path, const std::string& gitArgs, const std::string& p4Args, std::string* message)
        {
            const VersionControlSettings* s = Settings();
            const std::string rel = Relative(path);
            if (!s || s->Mode == 0 || rel.empty())
            {
                if (message) *message = "Version control is off (Project Settings > Version Control).";
                return false;
            }
            std::string out;
            int code = 0;
            if (s->Mode == 1)
            {
                if (gitArgs.empty())
                {
                    if (message) message->clear();
                    return true;
                }
                code = Run(ProjectDir(), "git " + gitArgs + " -- " + Quote(rel), out);
            }
            else
                code = Run(ProjectDir(), P4Command(*s) + " " + p4Args + " " + Quote((fs::path(ProjectDir()) / rel).string()), out);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                out.pop_back();
            if (message)
                *message = out;
            if (code != 0)
                CQ_CORE_WARN("[Version Control] {0}", out);
            Refresh(true);
            return code == 0;
        }
    }

    int Mode()
    {
        const VersionControlSettings* s = Settings();
        return s ? s->Mode : 0;
    }

    bool IsEnabled() { return Mode() != 0; }

    const char* ModeName()
    {
        switch (Mode())
        {
            case 1: return "Git";
            case 2: return "Perforce";
            default: return "None";
        }
    }

    FileStatus Get(const std::string& path)
    {
        if (!IsEnabled())
            return {};
        const std::string rel = Relative(path);
        if (rel.empty())
            return {};
        Cache& c = C();
        std::lock_guard lock(c.Mutex);
        if (c.Project != ProjectDir() || c.ModeAtRefresh != Mode())
            return {};
        if (auto it = c.Files.find(rel); it != c.Files.end())
            return it->second;
        FileStatus st;
        st.Status = c.Tracked.count(rel) ? State::Clean : State::NotVersioned;
        return st;
    }

    void Refresh(bool wait)
    {
        Cache& c = C();
        if (wait)
        {
            // Süren yenileme bitsin, sonra yenisi
            while (c.Running)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        StartRefresh();
        if (wait)
        {
            while (c.Running)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (c.Worker.joinable())
                c.Worker.join();
        }
    }

    void Update()
    {
        Cache& c = C();
        if (!IsEnabled() || c.Running)
            return;
        const bool stale = c.Project != ProjectDir() || c.ModeAtRefresh != Mode() ||
                           std::chrono::steady_clock::now() - c.Last > std::chrono::seconds(4);
        if (stale)
            StartRefresh();
    }

    bool CheckOut(const std::string& path, std::string* message) { return RunOperation(path, "", "edit", message); }

    bool Lock(const std::string& path, std::string* message)
    {
        // Perforce: kilit için dosya açık olmalı (Unity de önce Check Out yapar)
        if (Mode() == 2 && Get(path).Status != State::CheckedOut)
            if (!RunOperation(path, "", "edit", message))
                return false;
        return RunOperation(path, "lfs lock", "lock", message);
    }

    bool Unlock(const std::string& path, std::string* message) { return RunOperation(path, "lfs unlock", "unlock", message); }
    bool Revert(const std::string& path, std::string* message) { return RunOperation(path, "checkout", "revert", message); }
    bool Add(const std::string& path, std::string* message) { return RunOperation(path, "add", "add", message); }

    bool TestConnection(std::string* message)
    {
        const VersionControlSettings* s = Settings();
        if (!s || s->Mode == 0)
        {
            if (message) *message = "Version control is off.";
            return false;
        }
        std::string out;
        const int code = s->Mode == 1 ? Run(ProjectDir(), "git rev-parse --show-toplevel && git lfs version", out)
                                      : Run(ProjectDir(), P4Command(*s) + " info", out);
        if (message) *message = out;
        return code == 0;
    }

    bool InitGitRepository(std::string* message)
    {
        std::string out;
        const int code = Run(ProjectDir(),
                             "git init -q && git add -A && git -c user.name=Editor -c user.email=editor@localhost commit -q -m 'Initial commit'", out);
        if (message) *message = out;
        Refresh(true);
        return code == 0;
    }

    const char* StateName(State state)
    {
        switch (state)
        {
            case State::NotVersioned: return "Not versioned";
            case State::Clean: return "Up to date";
            case State::Modified: return "Modified";
            case State::Added: return "Added";
            case State::CheckedOut: return "Checked out";
            case State::LockedLocal: return "Locked by you";
            case State::LockedOther: return "Locked by someone else";
            case State::CheckedOutOther: return "Checked out by someone else";
            default: return "Unknown";
        }
    }
}
