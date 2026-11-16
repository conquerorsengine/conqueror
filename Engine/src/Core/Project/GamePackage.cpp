#include "GamePackage.h"

#include "Core/Assets/AssetBundle.h"
#include "Core/Base/Version.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/Platform.h"
#include "Scripting/ScriptRuntime.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

#ifdef _WIN32
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace Conqueror::GamePackage
{
    namespace fs = std::filesystem;

    namespace
    {
        // Bu oyuncunun bildiği özellikler: bilinmeyen bir özellik isteyen paket daha yeni bir motorla yapılmıştır
        const char* kKnownFeatures[] = { "python", "dotnet", "java", "openxr" };

        std::string Hex64(uint64_t v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
            return buf;
        }

        // FNV-1a 64
        void Mix(uint64_t& h, const void* data, size_t size)
        {
            const auto* p = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                h ^= p[i];
                h *= 0x100000001b3ull;
            }
        }

        // Klasör adı için güvenli parça (harf, rakam, - _ .)
        std::string SafeName(const std::string& name)
        {
            std::string out;
            for (char c : name)
            {
                if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')
                    out += c;
                else if (c == ' ')
                    out += '_';
            }
            while (!out.empty() && (out.back() == '.' || out.front() == '.'))
                out.erase(out.back() == '.' ? out.size() - 1 : 0, 1);
            if (out.size() > 48)
                out.resize(48);
            return out.empty() ? "Game" : out;
        }

        bool ValidBuildId(const std::string& id)
        {
            return id.size() == 16 && std::all_of(id.begin(), id.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; });
        }

        fs::path RecentFile()
        {
            std::string base = Platform::GetUserAppDataDirectory();
            if (base.empty())
                return CacheRoot().parent_path() / "Player" / "recent_games.yaml";
            return fs::path(base) / "ConquerorEngine" / "Player" / "recent_games.yaml";
        }

        std::mutex& RequestMutex()
        {
            static std::mutex m;
            return m;
        }
        std::vector<std::string>& Requests()
        {
            static std::vector<std::string> r;
            return r;
        }

        std::vector<std::string> ReadList(const YAML::Node& node)
        {
            std::vector<std::string> out;
            if (node && node.IsSequence())
                for (const auto& v : node)
                    out.push_back(v.as<std::string>());
            return out;
        }

        long ProcessId()
        {
#ifdef _WIN32
            return (long)_getpid();
#else
            return (long)getpid();
#endif
        }
    }

    std::string SerializeManifest(const Manifest& m)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "CQSpace" << YAML::Value << m.Format;
        out << YAML::Key << "Name" << YAML::Value << m.Name;
        out << YAML::Key << "Version" << YAML::Value << m.Version;
        out << YAML::Key << "Company" << YAML::Value << m.Company;
        out << YAML::Key << "EngineVersion" << YAML::Value << m.EngineVersion;
        out << YAML::Key << "CreatedBy" << YAML::Value << m.CreatedBy;
        out << YAML::Key << "ProjectFile" << YAML::Value << m.ProjectFile;
        out << YAML::Key << "StartScene" << YAML::Value << m.StartScene;
        out << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : m.Scenes)
            out << s;
        out << YAML::EndSeq;
        out << YAML::Key << "ScriptLanguages" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const auto& s : m.ScriptLanguages)
            out << s;
        out << YAML::EndSeq;
        out << YAML::Key << "Features" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const auto& s : m.Features)
            out << s;
        out << YAML::EndSeq;
        out << YAML::Key << "BuildId" << YAML::Value << m.BuildId;
        out << YAML::Key << "Created" << YAML::Value << m.Created;
        out << YAML::Key << "Development" << YAML::Value << m.Development;
        out << YAML::Key << "Files" << YAML::Value << m.Files;
        out << YAML::Key << "Size" << YAML::Value << m.Size;
        out << YAML::EndMap;
        return std::string(out.c_str()) + "\n";
    }

    bool ParseManifest(const std::string& text, Manifest& m, std::string* error)
    {
        try
        {
            const YAML::Node root = YAML::Load(text);
            if (!root.IsMap() || !root["CQSpace"])
            {
                if (error) *error = "the package manifest (cqspace.yaml) is not valid";
                return false;
            }
            m = Manifest{};
            m.Format = root["CQSpace"].as<int>();
            auto str = [&](const char* key) { return root[key] ? root[key].as<std::string>() : std::string(); };
            m.Name = str("Name");
            m.Version = str("Version");
            m.Company = str("Company");
            m.EngineVersion = str("EngineVersion");
            m.CreatedBy = str("CreatedBy");
            m.ProjectFile = str("ProjectFile");
            m.StartScene = str("StartScene");
            m.Scenes = ReadList(root["Scenes"]);
            m.ScriptLanguages = ReadList(root["ScriptLanguages"]);
            m.Features = ReadList(root["Features"]);
            m.BuildId = str("BuildId");
            m.Created = str("Created");
            m.Development = root["Development"] && root["Development"].as<bool>();
            m.Files = root["Files"] ? root["Files"].as<uint64_t>() : 0;
            m.Size = root["Size"] ? root["Size"].as<uint64_t>() : 0;
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = std::string("the package manifest (cqspace.yaml) is not valid: ") + e.what();
            return false;
        }
    }

    bool Write(const std::string& packageFile, Manifest manifest, const std::vector<std::pair<std::string, std::string>>& files,
               const std::vector<std::pair<std::string, std::string>>& generated, std::string* error,
               const std::function<bool(size_t, size_t)>& progress, Manifest* written)
    {
        std::vector<AssetBundles::WriteEntry> entries;
        entries.reserve(files.size() + generated.size() + 1);
        for (const auto& [path, data] : generated)
            entries.push_back({ path, {}, data, {} });
        for (const auto& [path, source] : files)
            entries.push_back({ path, source, {}, {} });
        for (const auto& e : entries)
            if (fs::path(e.Path).generic_string() == ManifestName)
            {
                if (error) *error = std::string(ManifestName) + " is reserved for the package manifest";
                return false;
            }
        manifest.Format = FormatVersion;
        if (manifest.EngineVersion.empty())
            manifest.EngineVersion = CQ_ENGINE_VERSION;
        if (manifest.CreatedBy.empty())
            manifest.CreatedBy = std::string("Conqueror Engine ") + CQ_ENGINE_VERSION;
        if (manifest.Created.empty())
        {
            std::time_t t = std::time(nullptr);
            char date[64];
            std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            manifest.Created = date;
        }
        // Bildirim son girdi: içerik özeti (yol, boyut, CRC) yazılmış girdilerden hesaplanır; aynı içerik aynı BuildId'yi verir
        Manifest result;
        AssetBundles::WriteEntry last;
        last.Path = ManifestName;
        last.Generate = [&manifest, &result](const std::vector<AssetBundles::FileEntry>& previous) {
            uint64_t h = 0xcbf29ce484222325ull;
            Manifest m = manifest;
            m.Files = previous.size();
            m.Size = 0;
            for (const auto& e : previous)
            {
                Mix(h, e.Path.data(), e.Path.size());
                Mix(h, &e.Size, sizeof(e.Size));
                Mix(h, &e.Crc, sizeof(e.Crc));
                m.Size += e.Size;
            }
            Mix(h, m.EngineVersion.data(), m.EngineVersion.size());
            m.BuildId = Hex64(h);
            result = m;
            return SerializeManifest(m);
        };
        entries.push_back(std::move(last));
        // Önce geçici dosyaya: yarım kalan derleme eski paketi bozmasın
        const std::string temp = packageFile + ".partial";
        std::string err;
        if (!AssetBundles::WriteEntries(temp, entries, &err, progress))
        {
            std::error_code ec;
            fs::remove(temp, ec);
            if (error) *error = err;
            return false;
        }
        std::error_code ec;
        fs::rename(temp, packageFile, ec);
        if (ec)
        {
            // Windows: hedef açıksa yeniden adlandırma başarısız olabilir
            fs::remove(packageFile, ec);
            fs::rename(temp, packageFile, ec);
            if (ec)
            {
                if (error) *error = "cannot write " + packageFile + ": " + ec.message();
                return false;
            }
        }
        if (written)
            *written = result;
        return true;
    }

    bool ReadManifest(const std::string& packageFile, Manifest& manifest, std::string* error)
    {
        std::error_code ec;
        if (!fs::is_regular_file(packageFile, ec))
        {
            if (error) *error = "The file " + packageFile + " does not exist.";
            return false;
        }
        std::vector<AssetBundles::FileEntry> table;
        std::string err;
        if (!AssetBundles::ReadTable(packageFile, table, &err))
        {
            if (error) *error = fs::path(packageFile).filename().string() + " is not a Conqueror game package (" + err + ").";
            return false;
        }
        std::string text;
        if (!AssetBundles::ReadEntry(packageFile, ManifestName, text, &err))
        {
            if (error) *error = fs::path(packageFile).filename().string() + " is not a Conqueror game package (" + err + ").";
            return false;
        }
        if (!ParseManifest(text, manifest, &err))
        {
            if (error) *error = err;
            return false;
        }
        return true;
    }

    bool ReadFile(const std::string& packageFile, const std::string& path, std::string& data, std::string* error)
    {
        return AssetBundles::ReadEntry(packageFile, path, data, error);
    }

    int CompareVersions(const std::string& a, const std::string& b)
    {
        auto parts = [](const std::string& v) {
            std::vector<long> out;
            std::string cur;
            for (char c : v + ".")
            {
                if (std::isdigit((unsigned char)c))
                    cur += c;
                else if (c == '.' || c == '-' || c == '+')
                {
                    out.push_back(cur.empty() ? 0 : std::strtol(cur.c_str(), nullptr, 10));
                    cur.clear();
                    if (c != '.')
                        break;   // "-beta" / "+build": sayısal kısım yeter
                }
            }
            return out;
        };
        const auto pa = parts(a), pb = parts(b);
        for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i)
        {
            const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
            if (x != y)
                return x < y ? -1 : 1;
        }
        return 0;
    }

    bool CheckCompatible(const Manifest& m, std::string& reason)
    {
        const std::string name = m.Name.empty() ? std::string("This game") : "\"" + m.Name + "\"";
        if (m.Format > FormatVersion)
        {
            reason = name + " uses game package format " + std::to_string(m.Format) + ", which this Conqueror player (engine " +
                     CQ_ENGINE_VERSION + ") cannot read.\nUpdate the Conqueror player to play it.";
            return false;
        }
        if (!m.EngineVersion.empty() && CompareVersions(m.EngineVersion, CQ_ENGINE_VERSION) > 0)
        {
            reason = name + " needs Conqueror Engine " + m.EngineVersion + " or newer.\nThis player is Conqueror Engine " + CQ_ENGINE_VERSION +
                     ". Update the Conqueror player to play it.";
            return false;
        }
        for (const auto& f : m.Features)
            if (std::find_if(std::begin(kKnownFeatures), std::end(kKnownFeatures), [&](const char* k) { return f == k; }) == std::end(kKnownFeatures))
            {
                reason = name + " needs the engine feature \"" + f + "\", which this Conqueror player (engine " + CQ_ENGINE_VERSION +
                         ") does not have.\nUpdate the Conqueror player to play it.";
                return false;
            }
        if (m.ProjectFile.empty() || m.StartScene.empty())
        {
            reason = "The game package has no project file or start scene (it is damaged or was not made by the Conqueror editor).";
            return false;
        }
        return true;
    }

    bool IsNativeLanguage(const std::string& id)
    {
        return id == "cpp" || id == "c" || id == "rust" || id == "zig";
    }

    std::string FeatureForLanguage(const std::string& id)
    {
        if (id == "py") return "python";
        if (id == "cs") return "dotnet";
        if (id == "java") return "java";
        return {};
    }

    bool CheckFeaturesAvailable(const Manifest& m, std::string& reason)
    {
        const std::string name = m.Name.empty() ? std::string("This game") : "\"" + m.Name + "\"";
        std::vector<std::string> languages = m.ScriptLanguages;
        // Özellikten dile (eski / elle yazılmış bildirimler için)
        for (const auto& f : m.Features)
        {
            const char* id = f == "python" ? "py" : f == "dotnet" ? "cs" : f == "java" ? "java" : nullptr;
            if (id && std::find(languages.begin(), languages.end(), id) == languages.end())
                languages.push_back(id);
        }
        for (const auto& id : languages)
        {
            if (IsNativeLanguage(id))
            {
                reason = name + " contains native " + id + " scripts, which cannot run from a portable game package.";
                return false;
            }
            Scripting::ScriptRuntime* runtime = Scripting::ScriptRuntimes::ForLanguage(id);
            if (!runtime)
            {
                reason = name + " uses the script language \"" + id + "\", which this Conqueror player (engine " + CQ_ENGINE_VERSION +
                         ") does not support.\nUpdate the Conqueror player to play it.";
                return false;
            }
            if (!runtime->IsAvailable())
            {
                std::string why = runtime->GetUnavailableReason();
                reason = name + " uses " + runtime->GetName() + " scripts, but " + runtime->GetName() + " is not available on this computer" +
                         (why.empty() ? std::string(".") : ":\n" + why);
                return false;
            }
        }
        if (std::find(m.Features.begin(), m.Features.end(), "openxr") != m.Features.end())
            CQ_CORE_INFO("[GamePackage] {0} starts in XR (OpenXR); without an OpenXR runtime it runs on the screen", m.Name);
        return true;
    }

    bool IsPackagePath(const std::string& path)
    {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return ext == Extension;
    }

    std::vector<fs::path> GameFolders()
    {
        std::vector<fs::path> out;
        std::error_code ec;
        auto add = [&](const fs::path& p) {
            if (!p.empty() && fs::is_directory(p, ec) && std::find(out.begin(), out.end(), p) == out.end())
                out.push_back(p);
        };
        const fs::path here = fs::current_path(ec);
        add(here);
        add(here / "Games");
        std::string home;
#ifdef _WIN32
        if (const char* h = std::getenv("USERPROFILE")) home = h;
#else
        if (const char* h = std::getenv("HOME")) home = h;
#endif
        if (!home.empty())
        {
            add(fs::path(home) / "Games");
            add(fs::path(home) / "Downloads");
        }
        return out;
    }

    fs::path CacheRoot()
    {
        fs::path base;
#ifdef _WIN32
        if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
            base = local;
#else
        if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0] == '/')
            base = xdg;
        else if (const char* home = std::getenv("HOME"); home && *home)
    #if defined(__APPLE__)
            base = fs::path(home) / "Library" / "Caches";
    #else
            base = fs::path(home) / ".cache";
    #endif
#endif
        if (base.empty())
        {
            std::error_code ec;
            base = fs::temp_directory_path(ec);
        }
        return base / "ConquerorEngine" / "Games";
    }

    fs::path CacheDirectory(const Manifest& m)
    {
        return CacheRoot() / (SafeName(m.Name) + "-" + (ValidBuildId(m.BuildId) ? m.BuildId : std::string("0000000000000000")));
    }

    bool Open(const std::string& packageFile, Manifest& manifest, fs::path& projectDir, std::string& error, const std::function<bool(float)>& progress)
    {
        if (!ReadManifest(packageFile, manifest, &error))
            return false;
        if (!CheckCompatible(manifest, error))
            return false;
        if (!ValidBuildId(manifest.BuildId))
        {
            error = "The game package is damaged (its build id is missing).";
            return false;
        }
        std::error_code ec;
        const fs::path dir = CacheDirectory(manifest);
        const fs::path marker = dir / ".cqspace_complete";
        {
            std::ifstream in(marker);
            std::string id;
            std::getline(in, id);
            if (id == manifest.BuildId && fs::exists(dir / manifest.ProjectFile, ec))
            {
                projectDir = dir;
                if (progress)
                    progress(1.0f);
                return true;
            }
        }
        // İlk açılış (ya da yarım kalmış çözme): geçici klasöre çöz, sonra adını ver (aynı anda açılan ikinci oyuncu yarım kopya görmesin)
        CQ_CORE_INFO("[GamePackage] Unpacking {0} {1} ({2} files, {3} KB) to {4}", manifest.Name, manifest.Version, manifest.Files,
                     manifest.Size / 1024, dir.string());
        fs::create_directories(dir.parent_path(), ec);
        const fs::path temp = dir.string() + ".unpacking-" + std::to_string(ProcessId());
        fs::remove_all(temp, ec);
        std::string err;
        const bool ok = AssetBundles::Extract(packageFile, temp.string(), &err, [&](size_t done, size_t total) {
            return !progress || total == 0 || progress((float)done / (float)total);
        });
        if (!ok)
        {
            fs::remove_all(temp, ec);
            error = "The game package could not be unpacked: " + err;
            return false;
        }
        std::ofstream(temp / ".cqspace_complete") << manifest.BuildId << "\n" << packageFile << "\n";
        // Eski kopya (yarım çözme) ve aynı oyunun önceki sürümlerinin kopyaları silinir: yalnız bu önbellek kökündeki "<ad>-<16 onaltılık>"
        // klasörleri
        const std::string prefix = SafeName(manifest.Name) + "-";
        for (const auto& entry : fs::directory_iterator(dir.parent_path(), ec))
        {
            const std::string n = entry.path().filename().string();
            if (entry.path() == temp || !entry.is_directory(ec) || n.rfind(prefix, 0) != 0 || !ValidBuildId(n.substr(prefix.size(), 16)) ||
                n.size() != prefix.size() + 16)
                continue;
            fs::remove_all(entry.path(), ec);
        }
        fs::rename(temp, dir, ec);
        if (ec)
        {
            // Başka bir oyuncu aynı anda çözdüyse onunki kullanılır
            std::ifstream in(marker);
            std::string id;
            std::getline(in, id);
            fs::remove_all(temp, ec);
            if (id != manifest.BuildId)
            {
                error = "The game package could not be unpacked to " + dir.string();
                return false;
            }
        }
        projectDir = dir;
        if (progress)
            progress(1.0f);
        return true;
    }

    std::vector<RecentGame> LoadRecent()
    {
        std::vector<RecentGame> list;
        try
        {
            std::error_code ec;
            const fs::path file = RecentFile();
            if (!fs::exists(file, ec))
                return list;
            const YAML::Node root = YAML::LoadFile(file.string());
            if (root["Games"] && root["Games"].IsSequence())
                for (const auto& g : root["Games"])
                {
                    RecentGame r;
                    r.Path = g["Path"] ? g["Path"].as<std::string>() : std::string();
                    r.Name = g["Name"] ? g["Name"].as<std::string>() : std::string();
                    r.Version = g["Version"] ? g["Version"].as<std::string>() : std::string();
                    r.LastOpened = g["LastOpened"] ? g["LastOpened"].as<int64_t>() : 0;
                    if (!r.Path.empty())
                        list.push_back(r);
                }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("[GamePackage] Recent games list could not be read: {0}", e.what());
        }
        return list;
    }

    namespace
    {
        void SaveRecent(const std::vector<RecentGame>& list)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "Games" << YAML::Value << YAML::BeginSeq;
            for (const auto& r : list)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Path" << YAML::Value << r.Path;
                out << YAML::Key << "Name" << YAML::Value << r.Name;
                out << YAML::Key << "Version" << YAML::Value << r.Version;
                out << YAML::Key << "LastOpened" << YAML::Value << r.LastOpened;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq << YAML::EndMap;
            std::error_code ec;
            const fs::path file = RecentFile();
            fs::create_directories(file.parent_path(), ec);
            std::ofstream(file, std::ios::trunc) << out.c_str() << "\n";
        }
    }

    void AddRecent(const std::string& packageFile, const Manifest& manifest)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(packageFile, ec);
        const std::string path = (ec ? fs::path(packageFile) : abs.lexically_normal()).string();
        auto list = LoadRecent();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const RecentGame& r) { return r.Path == path; }), list.end());
        RecentGame r;
        r.Path = path;
        r.Name = manifest.Name;
        r.Version = manifest.Version;
        r.LastOpened = (int64_t)std::time(nullptr);
        list.insert(list.begin(), r);
        if (list.size() > 12)
            list.resize(12);
        SaveRecent(list);
    }

    void RemoveRecent(const std::string& packageFile)
    {
        auto list = LoadRecent();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const RecentGame& r) { return r.Path == packageFile; }), list.end());
        SaveRecent(list);
    }

    void RequestOpen(const std::string& packageFile)
    {
        std::lock_guard lock(RequestMutex());
        Requests().push_back(packageFile);
    }

    bool TakeOpenRequest(std::string& packageFile)
    {
        std::lock_guard lock(RequestMutex());
        if (Requests().empty())
            return false;
        packageFile = Requests().front();
        Requests().erase(Requests().begin());
        return true;
    }
}
