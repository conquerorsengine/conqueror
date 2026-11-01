#include "BuildPipeline.h"

#include <IconsFontAwesome6.h>
#include "Core/Base/Version.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Project/PlatformModules.h"
#include "Scripting/NativeScriptCompiler.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Conqueror::Editor
{
    namespace
    {
        // Derlenmiş oyuna kopyalanan motor kaynakları (çalışma zamanı FileSystem::ResolveResourcePath yukarı doğru arar)
        const char* kEngineResourceDirs[] = { "Resources/Fonts", "Resources/Textures", "Resources/skybox", "Resources/test", "Resources/Packages", "Resources/Scripting",
                                              "Resources/Logo" };   // açılış ekranı logosu, varsayılan uygulama simgesi
        const char* kBuildMarker = "build_info.yaml";

        std::string SanitizeName(const std::string& name)
        {
            std::string out;
            for (char c : name)
            {
                if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')
                    out += c;
                else if (c == ' ')
                    out += '_';
            }
            while (!out.empty() && (out.back() == '.' || out.back() == '_'))
                out.pop_back();
            return out.empty() ? "Game" : out;
        }

        bool SkipEntry(const fs::path& p)
        {
            const std::string name = p.filename().string();
            static const char* kSkip[] = { "Library", "Builds", "Screenshots", ".git", ".vs", ".idea", "imgui.ini" };
            for (const char* s : kSkip)
                if (name == s)
                    return true;
            const std::string ext = p.extension().string();
            return ext == ".log" || ext == ".tmp";
        }

        struct CopyStats
        {
            size_t Files = 0;
            uint64_t Bytes = 0;
        };

        bool CopyTree(const fs::path& src, const fs::path& dst, CopyStats& stats, std::string& error, const std::atomic<bool>* cancel)
        {
            std::error_code ec;
            if (!fs::exists(src, ec))
                return true;
            if (fs::is_regular_file(src, ec))
            {
                fs::create_directories(dst.parent_path(), ec);
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    error = "Cannot copy " + src.string() + ": " + ec.message();
                    return false;
                }
                ++stats.Files;
                stats.Bytes += (uint64_t)fs::file_size(dst, ec);
                return true;
            }
            fs::create_directories(dst, ec);
            for (auto it = fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec)
                {
                    error = "Cannot read " + src.string() + ": " + ec.message();
                    return false;
                }
                if (cancel && cancel->load())
                {
                    error = "Cancelled";
                    return false;
                }
                const fs::path& p = it->path();
                if (SkipEntry(p))
                {
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                    continue;
                }
                const fs::path target = dst / fs::relative(p, src, ec);
                if (it->is_directory(ec))
                {
                    fs::create_directories(target, ec);
                    continue;
                }
                fs::create_directories(target.parent_path(), ec);
                fs::copy_file(p, target, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    error = "Cannot copy " + p.string() + ": " + ec.message();
                    return false;
                }
                ++stats.Files;
                stats.Bytes += (uint64_t)fs::file_size(target, ec);
            }
            return true;
        }

        // Komutu çalıştırır, çıktısını satır satır günlüğe aktarır
        int RunCommand(const std::string& command, const std::function<void(const std::string&)>& log)
        {
#ifdef _WIN32
            FILE* pipe = _popen((command + " 2>&1").c_str(), "r");
#else
            FILE* pipe = popen(("LC_ALL=C " + command + " 2>&1").c_str(), "r");
#endif
            if (!pipe)
                return -1;
            char line[1024];
            while (std::fgets(line, sizeof(line), pipe))
            {
                std::string s(line);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                    s.pop_back();
                if (log && !s.empty())
                    log(s);
            }
#ifdef _WIN32
            return _pclose(pipe);
#else
            const int status = pclose(pipe);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }

        std::string FormatBytes(uint64_t bytes)
        {
            char buf[64];
            if (bytes >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.2f GB", (double)bytes / (double)(1ull << 30));
            else if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (double)(1ull << 20));
            else std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
            return buf;
        }
    }

    BuildPipeline& BuildPipeline::Get()
    {
        static BuildPipeline s;
        return s;
    }

    BuildPipeline::~BuildPipeline()
    {
        m_Cancel = true;
        if (m_Thread.joinable())
            m_Thread.join();
    }

    std::string BuildPipeline::HostPlatform()
    {
#if defined(_WIN32)
        return "Windows";
#elif defined(__APPLE__)
        return "macOS";
#else
        return "Linux";
#endif
    }

    const std::vector<BuildPipeline::PlatformInfo>& BuildPipeline::Platforms()
    {
        static const std::vector<PlatformInfo> s_Platforms = {
            { "Windows", ICON_FA_DESKTOP, "Built on Windows (MSVC / MinGW) or cross-built from Linux with MinGW (Tools/Windows)." },
            { "macOS", ICON_FA_LAPTOP, "Built on a Mac. Bundle identifier, version, minimum macOS and privacy texts are written to Info.plist." },
            { "Linux", ICON_FA_TERMINAL, "Primary platform. Also runs in the Steam Linux Runtime (Steam Deck: 1280x800 borderless)." },
            { "FreeBSD", ICON_FA_SERVER, "Cross-built from Linux (Tools/BSD) or built on FreeBSD; the player is not yet tested on BSD." },
            { "Android", ICON_FA_MOBILE_SCREEN, "Signed APK built from this editor with the Android SDK, NDK and a JDK (OpenGL ES 3)." },
            { "Android Automotive", ICON_FA_CAR, "Android Automotive OS APK (system bars stay visible), same tools as Android." },
            { "iOS", ICON_FA_TABLET_SCREEN_BUTTON, "Not buildable yet: the iOS player is not part of this engine version (it needs a Mac with Xcode)." },
            { "Web", ICON_FA_GLOBE, "Not buildable yet: the web player (WebGL 2 / WebGPU) needs Emscripten and is not part of this engine version." },
            { "QNX", ICON_FA_GAUGE_HIGH, "Built with the QNX SDP from the command line (Tools/QNX, Docs/Automotive.md); OpenGL ES 3 on QNX Screen." },
        };
        return s_Platforms;
    }

    bool BuildPipeline::CanBuild(const std::string& platform)
    {
        // Platform modülü hazır mı (araç zinciri bu bilgisayarda): bu PC, Android SDK, MinGW (Windows), FreeBSD sistem kökü
        return PlatformModules::IsReady(platform.empty() ? HostPlatform() : platform);
    }

    std::string BuildPipeline::DefaultIdentifier(const ProjectConfig& config)
    {
        // Java paket / Apple paket kimliği parçası: harf, rakam, _ ; rakamla başlamaz
        auto part = [](const std::string& s, const char* fallback) {
            std::string out;
            for (char c : s)
            {
                if (c == ' ' || c == '-')
                    out += '_';
                else if (std::isalnum((unsigned char)c) || c == '_')
                    out += (char)std::tolower((unsigned char)c);
            }
            if (out.empty())
                out = fallback;
            if (std::isdigit((unsigned char)out[0]))
                out = "g" + out;
            return out;
        };
        return "com." + part(config.Organization, "conqueror") + "." + part(config.Name, "game");
    }

    fs::path BuildPipeline::FindEngineRoot(const fs::path& binDir)
    {
        std::error_code ec;
        fs::path probe = binDir;
        for (int i = 0; i < 8 && !probe.empty(); ++i)
        {
            if (fs::is_directory(probe / "Resources" / "Fonts", ec) && fs::is_directory(probe / "Engine" / "src" / "Shaders", ec))
                return probe;
            if (!probe.has_parent_path() || probe.parent_path() == probe)
                break;
            probe = probe.parent_path();
        }
        return {};
    }

    bool BuildPipeline::Launch(const fs::path& executable, std::string* error)
    {
#ifdef _WIN32
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + executable.wstring() + L"\"";
        std::wstring dir = executable.parent_path().wstring();
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, dir.c_str(), &si, &pi))
        {
            if (error) *error = "CreateProcess failed";
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
#else
        // fork sonrası çocukta yalnızca async-signal-safe çağrılar: dizgiler önceden hazırlanır
        const std::string exe = executable.string();
        const std::string dir = executable.parent_path().string();
        // Çapraz derlenmiş Windows oyunu: Wine ile (kuruluysa)
        const bool windowsExe = executable.extension() == ".exe";
        const std::string wine = windowsExe ? PlatformModules::FindProgram("wine") : std::string();
        if (windowsExe && wine.empty())
        {
            if (error) *error = "Wine is not installed: copy the game folder to a Windows PC to run it";
            return false;
        }
        const pid_t pid = fork();
        if (pid < 0)
        {
            if (error) *error = "fork failed";
            return false;
        }
        if (pid == 0)
        {
            // Torun süreç: editörden bağımsız (zombi bırakmaz)
            if (fork() != 0)
                _exit(0);
            setsid();
            if (chdir(dir.c_str()) != 0)
                _exit(127);
            if (windowsExe)
                execl(wine.c_str(), wine.c_str(), exe.c_str(), (char*)nullptr);
            else
                execl(exe.c_str(), exe.c_str(), (char*)nullptr);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return true;
#endif
    }

    bool BuildPipeline::Start(const BuildOptions& options)
    {
        if (m_Running.load())
            return false;
        if (m_Thread.joinable())
            m_Thread.join();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Log.clear();
            m_Step = "Starting";
            m_Result = BuildReport{};
            m_AllResults.clear();
        }
        m_Cancel = false;
        m_HasResult = false;
        m_Progress = 0.0f;
        m_Running = true;
        m_Thread = std::thread([this, options]() {
            BuildReport report = Run(
                options,
                [this](float p, const std::string& step) {
                    m_Progress = p;
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Step = step;
                },
                [this](const std::string& line) {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Log.push_back(line);
                    if (m_Log.size() > 4000)
                        m_Log.erase(m_Log.begin(), m_Log.begin() + 1000);
                },
                &m_Cancel);
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Result = report;
                m_Step = report.Success ? "Done" : "Failed";
            }
            m_Progress = 1.0f;
            m_HasResult = true;
            m_Running = false;
        });
        return true;
    }

    std::string BuildPipeline::GetStep() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Step;
    }

    std::vector<std::string> BuildPipeline::GetLog() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Log;
    }

    bool BuildPipeline::StartAll(const BuildOptions& base, const std::vector<std::string>& platforms)
    {
        if (m_Running.load() || platforms.empty())
            return false;
        if (m_Thread.joinable())
            m_Thread.join();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Log.clear();
            m_Step = "Starting";
            m_Result = BuildReport{};
            m_AllResults.clear();
        }
        m_Cancel = false;
        m_HasResult = false;
        m_Progress = 0.0f;
        m_Running = true;
        m_Thread = std::thread([this, base, platforms]() {
            const auto start = std::chrono::steady_clock::now();
            BuildReport total;
            total.Success = true;
            const float n = (float)platforms.size();
            for (size_t i = 0; i < platforms.size() && !m_Cancel.load(); ++i)
            {
                const std::string& platform = platforms[i];
                BuildOptions o = base;
                o.Config.TargetPlatform = platform;
                o.OutputDir = base.OutputDir / platform;
                o.RunAfterBuild = false;
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Log.push_back("######## " + platform + " (" + std::to_string(i + 1) + " / " + std::to_string(platforms.size()) + ")");
                }
                BuildReport r = Run(
                    o,
                    [this, i, n, platform](float p, const std::string& step) {
                        m_Progress = ((float)i + p) / n;
                        std::lock_guard<std::mutex> lock(m_Mutex);
                        m_Step = platform + ": " + step;
                    },
                    [this](const std::string& line) {
                        std::lock_guard<std::mutex> lock(m_Mutex);
                        m_Log.push_back(line);
                        if (m_Log.size() > 4000)
                            m_Log.erase(m_Log.begin(), m_Log.begin() + 1000);
                    },
                    &m_Cancel);
                total.Files += r.Files;
                total.Bytes += r.Bytes;
                if (!r.Success)
                {
                    total.Success = false;
                    total.Error += (total.Error.empty() ? "" : "; ") + platform + ": " + r.Error;
                }
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_AllResults.emplace_back(platform, r);
            }
            if (m_Cancel.load())
            {
                total.Success = false;
                total.Error = "Cancelled";
            }
            total.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Result = total;
                m_Step = total.Success ? "Done" : "Failed";
            }
            m_Progress = 1.0f;
            m_HasResult = true;
            m_Running = false;
        });
        return true;
    }

    std::vector<std::pair<std::string, BuildReport>> BuildPipeline::GetAllResults() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_AllResults;
    }

    BuildReport BuildPipeline::GetResult() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Result;
    }

    BuildReport BuildPipeline::Run(const BuildOptions& o, const std::function<void(float, const std::string&)>& progress,
                                   const std::function<void(const std::string&)>& log, const std::atomic<bool>* cancel)
    {
        const auto start = std::chrono::steady_clock::now();
        BuildReport report;
        std::error_code ec;
        auto step = [&](float p, const std::string& s) {
            if (progress) progress(p, s);
            if (log) log("== " + s);
        };
        auto fail = [&](const std::string& error) {
            report.Success = false;
            report.Error = error;
            if (log) log("ERROR: " + error);
            report.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return report;
        };
        auto cancelled = [&]() { return cancel && cancel->load(); };

        // 1) Doğrulama
        step(0.02f, "Validating project");
        const std::string platform = o.Config.TargetPlatform.empty() ? HostPlatform() : o.Config.TargetPlatform;
        // Android / Android Automotive: oyuncu NDK ile her masaüstünden derlenir, APK Tools/Android/package_apk.py ile paketlenir
        const bool android = platform == "Android" || platform == "Android Automotive";
        // Linux'tan çapraz derleme: Windows (MinGW-w64) ve FreeBSD (clang + sistem kökü); oyuncu ilk seferde derlenir
        const bool cross = !android && platform != HostPlatform() && (platform == "Windows" || platform == "FreeBSD");
        {
            const PlatformModules::Module module = PlatformModules::Detect(platform);
            if (module.State != PlatformModules::Status::Ready)
                return fail(platform + " cannot be built on this computer: " + module.Detail +
                            (module.Install.empty() ? std::string(".") : ". Install: " + module.Install));
        }
        if (!android && !cross && platform != HostPlatform())
            return fail("Target platform " + platform + " cannot be built on " + HostPlatform() + ".");
        const auto scenes = o.Config.GetBuildScenes();
        if (scenes.empty())
            return fail("No scenes in Build Settings. Add the open scene with \"Add Open Scenes\".");
        for (const auto& s : scenes)
            if (!fs::exists(o.ProjectDir / s, ec))
                return fail("Scene missing: " + s);
        if (o.OutputDir.empty())
            return fail("No output folder");
        // Proje klasörünün kendisine ya da Assets içine derleme yapılmasını engelle
        const fs::path outCanon = fs::weakly_canonical(o.OutputDir, ec);
        const fs::path projCanon = fs::weakly_canonical(o.ProjectDir, ec);
        if (outCanon == projCanon || outCanon.string().rfind((projCanon / "Assets").string(), 0) == 0)
            return fail("The output folder cannot be the project or its Assets folder");

        // 2) Oyuncu (runtime) ikilisi
        step(0.08f, "Locating the player executable");
        const bool targetWindows = platform == "Windows";
        fs::path playerBinDir = o.EngineBinDir;     // oyuncu ve DLL / dylib'leri
        fs::path playerLibDir;                      // Linux / FreeBSD paylaşımlı kitaplıkları (lib/)
        if (cross)
        {
            // Motor kaynak ağacı (araç zinciri dosyası) editör ikilisinin klasöründen yukarı doğru aranır
            const char* toolchainRel = targetWindows ? "Tools/Windows/mingw-w64.toolchain.cmake" : "Tools/BSD/freebsd-cross.toolchain.cmake";
            fs::path source;
            for (fs::path probe = o.EngineBinDir; !probe.empty(); probe = probe.parent_path())
            {
                if (fs::exists(probe / toolchainRel, ec) && fs::exists(probe / "CMakeLists.txt", ec))
                {
                    source = probe;
                    break;
                }
                if (!probe.has_parent_path() || probe.parent_path() == probe)
                    break;
            }
            if (source.empty())
                return fail(std::string("Engine source tree (") + toolchainRel + ") not found near " + o.EngineBinDir.string() +
                            ": cross builds compile the player from the engine source");
            // Oyuncu derleme klasörü: CQ_WINDOWS_BUILD_ROOT / CQ_FREEBSD_BUILD_ROOT ya da kaynak kökünde build-windows / build-freebsd
            const char* rootEnv = std::getenv(targetWindows ? "CQ_WINDOWS_BUILD_ROOT" : "CQ_FREEBSD_BUILD_ROOT");
            const fs::path buildRoot = rootEnv && *rootEnv ? fs::path(rootEnv) : source / (targetWindows ? "build-windows" : "build-freebsd");
            const fs::path crossExe = buildRoot / "bin" / (targetWindows ? "ConquerorRuntime.exe" : "ConquerorRuntime");
            const unsigned jobs = std::max(2u, std::thread::hardware_concurrency());
            if (!fs::exists(buildRoot / "CMakeCache.txt", ec))
            {
                step(0.09f, "Configuring the " + platform + " player (first build only)");
                std::string configure = "cmake -S \"" + source.string() + "\" -B \"" + buildRoot.string() + "\"";
                if (!PlatformModules::FindProgram("ninja").empty())
                    configure += " -G Ninja";
                configure += " -DCMAKE_TOOLCHAIN_FILE=\"" + (source / toolchainRel).string() + "\" -DCMAKE_BUILD_TYPE=Release -DCQ_RUNTIME_ONLY=ON";
                if (const char* sysroot = std::getenv("CQ_FREEBSD_SYSROOT"); !targetWindows && sysroot && *sysroot)
                    configure += std::string(" -DCQ_FREEBSD_SYSROOT=\"") + sysroot + "\"";
                const int rc = RunCommand(configure, log);
                if (rc != 0)
                    return fail("Configuring the " + platform + " player failed (exit code " + std::to_string(rc) + "), see the log");
            }
            // Her derlemede güncellenir (kaynak değiştiyse yalnız değişenler derlenir)
            step(0.1f, "Compiling the " + platform + " player (the first time takes several minutes)");
            const int rc = RunCommand("cmake --build \"" + buildRoot.string() + "\" --target ConquerorRuntime -j" + std::to_string(jobs), log);
            if (rc != 0 || !fs::exists(crossExe, ec))
                return fail("Compiling the " + platform + " player failed (exit code " + std::to_string(rc) + "), see the log");
            playerBinDir = buildRoot / "bin";
            playerLibDir = buildRoot / "lib";
        }
        const fs::path runtimeExe = playerBinDir / (targetWindows ? "ConquerorRuntime.exe" : "ConquerorRuntime");
        if (!android && !cross && !fs::exists(runtimeExe, ec))
        {
            const fs::path buildDir = o.EngineBinDir.parent_path();
            if (!o.BuildRuntimeIfMissing || !fs::exists(buildDir / "CMakeCache.txt", ec))
                return fail("Player executable not found: " + runtimeExe.string());
            step(0.1f, "Compiling the player (cmake --build)");
            const unsigned jobs = std::max(2u, std::thread::hardware_concurrency());
            const int rc = RunCommand("cmake --build \"" + buildDir.string() + "\" --target ConquerorRuntime -j" + std::to_string(jobs), log);
            if (rc != 0 || !fs::exists(runtimeExe, ec))
                return fail("Compiling the player failed (exit code " + std::to_string(rc) + ")");
        }
        if (cancelled())
            return fail("Cancelled");

        // 3) Çıktı klasörü: önceki derleme ise temizlenir, başka dosyalar içeriyorsa dokunulmaz
        step(0.15f, "Preparing " + o.OutputDir.string());
        if (fs::exists(o.OutputDir, ec) && !fs::is_empty(o.OutputDir, ec))
        {
            if (!fs::exists(o.OutputDir / kBuildMarker, ec))
                return fail("Output folder is not empty and is not a previous build: " + o.OutputDir.string());
            for (const auto& entry : fs::directory_iterator(o.OutputDir, ec))
                fs::remove_all(entry.path(), ec);
            if (ec)
                return fail("Cannot clean the output folder: " + ec.message());
        }
        fs::create_directories(o.OutputDir, ec);
        if (ec)
            return fail("Cannot create the output folder: " + ec.message());

        CopyStats stats;
        std::string error;
        const std::string product = SanitizeName(o.Config.Name);
        // Android: oyun dosyaları APK'ya girmeden önce burada toplanır (masaüstünde oyun klasörünün kendisi)
        const fs::path dataDir = android ? o.OutputDir / "AndroidData" : o.OutputDir;
        if (android)
            fs::create_directories(dataDir, ec);

        // 4) Çalıştırılabilir ve motor kitaplığı
        const fs::path exe = o.OutputDir / (product + (targetWindows ? ".exe" : ""));
        if (!android)
        {
        step(0.2f, "Copying the player");
        if (!CopyTree(runtimeExe, exe, stats, error, cancel))
            return fail(error);
#ifndef _WIN32
        fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
        report.Executable = exe;
        if (targetWindows)
        {
            // Windows: oyuncunun yanındaki DLL'ler (MinGW derlemesi C++ çalışma zamanını statik bağlar)
            for (const auto& entry : fs::directory_iterator(playerBinDir, ec))
                if (entry.path().extension() == ".dll")
                    if (!CopyTree(entry.path(), o.OutputDir / entry.path().filename(), stats, error, cancel))
                        return fail(error);
        }
        else if (cross)
        {
            // FreeBSD: motorun paylaşımlı kitaplıkları lib/ altında (Linux düzeni)
            for (const auto& entry : fs::directory_iterator(playerLibDir, ec))
                if (entry.path().filename().string().find(".so") != std::string::npos)
                    if (!CopyTree(fs::canonical(entry.path(), ec), o.OutputDir / "lib" / entry.path().filename(), stats, error, cancel))
                        return fail(error);
        }
        else
        {
#if defined(_WIN32)
        for (const auto& entry : fs::directory_iterator(o.EngineBinDir, ec))
            if (entry.path().extension() == ".dll")
                if (!CopyTree(entry.path(), o.OutputDir / entry.path().filename(), stats, error, cancel))
                    return fail(error);
#elif defined(__APPLE__)
        // macOS'ta motor statik; editörün yanında paketlenmiş dinamik kitaplıklar (ör. libMoltenVK.dylib) oyuna taşınır
        for (const auto& entry : fs::directory_iterator(o.EngineBinDir, ec))
            if (entry.path().extension() == ".dylib")
                if (!CopyTree(entry.path(), o.OutputDir / entry.path().filename(), stats, error, cancel))
                    return fail(error);
#else
        {
            // Motorun paylaşımlı kitaplıkları (libEngine + yerleşik zlib): oyun kendi lib/ klasöründen yükler
            fs::path libDir = o.EngineBinDir.parent_path() / "lib";
            if (!fs::exists(libDir / "libEngine.so", ec))
                libDir = o.EngineBinDir;
            if (!fs::exists(libDir / "libEngine.so", ec))
                return fail("libEngine.so not found next to the editor");
            for (const auto& entry : fs::directory_iterator(libDir, ec))
            {
                const std::string name = entry.path().filename().string();
                if (name.find(".so") == std::string::npos)
                    continue;
                // Sembolik bağlar hedef dosya olarak kopyalanır (sürüm adları korunur)
                if (!CopyTree(fs::canonical(entry.path(), ec), o.OutputDir / "lib" / name, stats, error, cancel))
                    return fail(error);
            }
        }
#endif
        }
        }

        // 5) Proje içeriği
        step(0.35f, "Copying Assets");
        if (!CopyTree(o.ProjectDir / "Assets", dataDir / "Assets", stats, error, cancel))
            return fail(error);
        step(0.55f, "Copying Packages");
        if (!CopyTree(o.ProjectDir / "Packages", dataDir / "Packages", stats, error, cancel))
            return fail(error);
        if (!android && fs::is_directory(o.ProjectDir / "Plugins", ec))
            if (!CopyTree(o.ProjectDir / "Plugins", o.OutputDir, stats, error, cancel))   // steam_api vb. oyunun yanına
                return fail(error);

        // 5b) Native Script C++ kaynakları (ScriptableEntity + CQ_NATIVE_SCRIPT): derlenip oyunun NativeScripts/ klasörüne
        //     (oyuncu derleyici aramaz). Kitaplık bu bilgisayarın mimarisi içindir: çapraz / Android derlemede atlanır.
        {
            std::vector<fs::path> nativeSources;
            for (auto it = fs::recursive_directory_iterator(o.ProjectDir / "Assets", fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec || !it->is_regular_file(ec) || !NativeScriptCompiler::IsSource(it->path().string()))
                    continue;
                std::ifstream in(it->path());
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (text.find("CQ_NATIVE_SCRIPT(") != std::string::npos)
                    nativeSources.push_back(it->path());
            }
            if (!nativeSources.empty() && (android || cross))
            {
                report.Warnings.push_back("Native C++ scripts are packaged only for " + HostPlatform() + " builds; " + platform + " skips them");
                if (log) log("WARNING: " + report.Warnings.back());
            }
            else if (!nativeSources.empty())
            {
                step(0.6f, "Compiling native scripts");
                for (const auto& src : nativeSources)
                {
                    const auto r = NativeScriptCompiler::Compile(src, false);
                    if (!r.Success)
                        return fail("Native script " + src.filename().string() + " does not compile:\n" + r.Log);
                    if (!CopyTree(r.Library, dataDir / "NativeScripts" / r.Library.filename(), stats, error, cancel))
                        return fail(error);
                }
            }
        }

        // 6) Motor kaynakları (gölgelendiriciler, yazı tipleri, dokular, gökyüzü)
        step(0.7f, "Copying engine resources");
        const fs::path root = FindEngineRoot(o.EngineBinDir);
        if (root.empty())
            return fail("Engine resources (Resources/, Engine/src/Shaders) not found near " + o.EngineBinDir.string());
        const fs::path shaders = fs::is_directory(o.EngineBinDir / "Engine" / "src" / "Shaders", ec) ? o.EngineBinDir / "Engine" / "src" / "Shaders"
                                                                                                    : root / "Engine" / "src" / "Shaders";
        if (!CopyTree(shaders, dataDir / "Engine" / "src" / "Shaders", stats, error, cancel))
            return fail(error);
        {
            // Varsayılan giriş eşlemeleri (default.cqinput) oyunda zorunlu: editörün yanında yoksa kaynak ağacında aranır
            fs::path inputDir = root / "Engine" / "src" / "Core" / "Input";
            for (fs::path probe = o.EngineBinDir; !fs::exists(inputDir / "default.cqinput", ec) && !probe.empty() && probe.has_parent_path() &&
                                                  probe.parent_path() != probe;
                 probe = probe.parent_path())
                inputDir = probe / "Engine" / "src" / "Core" / "Input";
            if (!fs::exists(inputDir / "default.cqinput", ec))
                return fail("Engine input configuration (Engine/src/Core/Input/default.cqinput) not found");
            if (!CopyTree(inputDir / "default.cqinput", dataDir / "Engine" / "src" / "Core" / "Input" / "default.cqinput", stats, error, cancel))
                return fail(error);
        }
        for (const char* dir : kEngineResourceDirs)
            if (!CopyTree(root / dir, dataDir / dir, stats, error, cancel))
                return fail(error);
        // Varsa SPIR-V önbelleği (ilk açılışta derlemeyi atlar; Android'in OpenGL ES hedefi farklı olduğundan alınmaz)
        if (!android && fs::is_directory(o.EngineBinDir / "Cache", ec))
            CopyTree(o.EngineBinDir / "Cache", dataDir / "Cache", stats, error, cancel);

        // 7) Proje dosyası: başlangıç sahnesi = ilk derleme sahnesi
        step(0.9f, "Writing project file");
        {
            auto project = std::make_shared<Project>();
            project->GetConfig() = o.Config;
            project->GetConfig().StartScene = scenes.front();
            const fs::path projFile = dataDir / (o.ProjectFile.empty() ? product + ".cqproj" : o.ProjectFile.filename().string());
            if (!ProjectSerializer(project).Serialize(projFile))
                return fail("Cannot write " + projFile.string());
            ++stats.Files;
        }

        // 8) Derleme bilgisi (bir sonraki derlemede bu klasörün güvenle temizlenebileceğini gösterir)
        {
            YAML::Emitter out;
            std::time_t t = std::time(nullptr);
            char date[64];
            std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            out << YAML::BeginMap;
            out << YAML::Key << "Product" << YAML::Value << o.Config.Name;
            out << YAML::Key << "Version" << YAML::Value << o.Config.Version;
            out << YAML::Key << "Platform" << YAML::Value << platform;
            out << YAML::Key << "Engine" << YAML::Value << CQ_ENGINE_VERSION;
            out << YAML::Key << "Date" << YAML::Value << date;
            out << YAML::Key << "Development" << YAML::Value << o.Config.Build.DevelopmentBuild;
            out << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq;
            for (const auto& s : scenes)
                out << s;
            out << YAML::EndSeq;
            out << YAML::EndMap;
            std::ofstream f(o.OutputDir / kBuildMarker);
            f << out.c_str();
        }

        // Linux / FreeBSD: oyunun simgesi <ürün>.png (masaüstü kısayolu için; Wayland uygulama kimliği ürün adıdır)
        if (!android && !targetWindows && platform != "macOS")
        {
            const fs::path icon = !o.Config.Player.Icon.empty() ? o.ProjectDir / o.Config.Player.Icon
                                                                 : root / "Resources" / "Logo" / "AppIcon" / "conqueror_256.png";
            if (fs::exists(icon, ec))
                fs::copy_file(icon, o.OutputDir / (product + ".png"), fs::copy_options::overwrite_existing, ec);
        }

        // macOS: Info.plist (Project Settings > Player > macOS). Uygulama paketi (.app) sarılırken Contents/ altına girer.
        if (platform == "macOS")
        {
            const auto& m = o.Config.Player.Mac;
            auto esc = [](const std::string& v) {
                std::string r;
                for (char c : v)
                {
                    if (c == '&') r += "&amp;";
                    else if (c == '<') r += "&lt;";
                    else if (c == '>') r += "&gt;";
                    else if (c == '"') r += "&quot;";
                    else r += c;
                }
                return r;
            };
            std::ofstream plist(o.OutputDir / "Info.plist");
            plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                     "<plist version=\"1.0\">\n<dict>\n";
            auto key = [&](const char* k, const std::string& v) { plist << "    <key>" << k << "</key>\n    <string>" << esc(v) << "</string>\n"; };
            key("CFBundleName", o.Config.Name);
            key("CFBundleDisplayName", o.Config.Name);
            key("CFBundleExecutable", product);
            key("CFBundleIdentifier", m.BundleIdentifier.empty() ? DefaultIdentifier(o.Config) : m.BundleIdentifier);
            key("CFBundleShortVersionString", o.Config.Version);
            key("CFBundleVersion", m.BuildNumber.empty() ? std::string("1") : m.BuildNumber);
            key("CFBundlePackageType", "APPL");
            key("LSMinimumSystemVersion", m.MinimumVersion.empty() ? std::string("11.0") : m.MinimumVersion);
            key("LSApplicationCategoryType", m.Category);
            if (!m.CameraUsage.empty())
                key("NSCameraUsageDescription", m.CameraUsage);
            if (!m.MicrophoneUsage.empty())
                key("NSMicrophoneUsageDescription", m.MicrophoneUsage);
            plist << "    <key>NSHighResolutionCapable</key>\n    <true/>\n</dict>\n</plist>\n";
        }

        // 9) Android: NDK ile oyuncu kitaplığı (ilk seferde derlenir, sonra önbellekte) + APK paketleme ve imzalama
        if (android)
        {
            step(0.92f, "Packaging the Android APK (first build compiles the player with the NDK, this takes a while)");
            // Paketleyici motor kaynak ağacında (Tools/Android): editör ikilisinin klasöründen yukarı doğru aranır
            fs::path script;
            for (fs::path probe = o.EngineBinDir; !probe.empty(); probe = probe.parent_path())
            {
                if (fs::exists(probe / "Tools" / "Android" / "package_apk.py", ec))
                {
                    script = probe / "Tools" / "Android" / "package_apk.py";
                    break;
                }
                if (!probe.has_parent_path() || probe.parent_path() == probe)
                    break;
            }
            if (script.empty())
                return fail("Android packager (Tools/Android/package_apk.py) not found near " + o.EngineBinDir.string());
            const fs::path engineSource = script.parent_path().parent_path().parent_path();
            const fs::path apk = o.OutputDir / (product + ".apk");
#ifdef _WIN32
            const std::string python = "python";
#else
            const std::string python = "python3";
#endif
            // NDK derleme klasörleri: CQ_ANDROID_BUILD_ROOT (CI / paylaşılan önbellek) ya da motor kökünde build-android/
            const char* buildRootEnv = std::getenv("CQ_ANDROID_BUILD_ROOT");
            const fs::path buildRoot = buildRootEnv && *buildRootEnv ? fs::path(buildRootEnv) : engineSource / "build-android";
            std::string cmd = python + " \"" + script.string() + "\" \"" + dataDir.string() + "\" --out \"" + apk.string() +
                              "\" --build --build-root \"" + buildRoot.string() + "\"";
            if (platform == "Android Automotive")
                cmd += " --automotive";
            // Project Settings > Player > Android
            {
                const auto& ap = o.Config.Player.Android;
                const std::string package = ap.PackageName.empty() ? DefaultIdentifier(o.Config) : ap.PackageName;
                const bool validPackage = package.find('.') != std::string::npos &&
                                          std::all_of(package.begin(), package.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '_'; });
                if (!validPackage)
                    return fail("Android package name \"" + package + "\" is not valid (letters, digits, _ and at least one dot, e.g. com.company.game).");
                cmd += " --package " + package + " --version-code " + std::to_string(std::max(1, ap.VersionCode));
                static const char* orientations[] = { "sensorLandscape", "landscape", "sensorPortrait", "portrait", "fullSensor" };
                cmd += std::string(" --orientation ") + orientations[std::clamp(ap.Orientation, 0, 4)];
                if (ap.ARM64)
                    cmd += " --abi arm64-v8a";
                if (ap.X86_64)
                    cmd += " --abi x86_64";
                auto quoted = [](const std::string& v) { return v.find('"') == std::string::npos; };
                if (!ap.Keystore.empty())
                {
                    fs::path ks = ap.Keystore;
                    if (ks.is_relative())
                        ks = o.ProjectDir / ks;
                    if (!quoted(ks.string()) || !fs::exists(ks, ec))
                        return fail("Android keystore not found: " + ks.string());
                    cmd += " --keystore \"" + ks.string() + "\"";
                    if (const char* pass = std::getenv("CQ_ANDROID_KEYSTORE_PASS"); pass && *pass && quoted(pass))
                        cmd += std::string(" --ks-pass \"") + pass + "\"";
                }
                if (!ap.KeyAlias.empty() && quoted(ap.KeyAlias))
                    cmd += " --key-alias \"" + ap.KeyAlias + "\"";
                // Başlatıcı simgesi: projenin simgesi ya da motor simgesi
                const fs::path icon = !o.Config.Player.Icon.empty() ? o.ProjectDir / o.Config.Player.Icon
                                                                     : root / "Resources" / "Logo" / "AppIcon" / "conqueror_256.png";
                if (fs::exists(icon, ec) && quoted(icon.string()))
                    cmd += " --icon \"" + icon.string() + "\"";
            }
            if (!o.Config.Build.DevelopmentBuild)
                cmd += " --release";
            if (o.RunAfterBuild)
                cmd += " --install --run";
            const int rc = RunCommand(cmd, log);
            if (rc != 0 || !fs::exists(apk, ec))
                return fail("Android packaging failed (exit code " + std::to_string(rc) +
                            "). The Android SDK, NDK and a JDK are required, see Docs/Automotive.md.");
            report.Executable = apk;
        }

        report.Success = true;
        report.Files = stats.Files;
        report.Bytes = stats.Bytes;
        report.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (log)
            log("Build succeeded: " + std::to_string(stats.Files) + " files, " + FormatBytes(stats.Bytes) + " in " +
                std::to_string((int)(report.Seconds * 1000.0)) + " ms -> " + report.Executable.string());
        step(1.0f, "Done");

        if (o.RunAfterBuild && !android)
        {
            std::string launchError;
            if (!Launch(exe, &launchError) && log)
                log("Could not start the game: " + launchError);
        }
        return report;
    }
}
