#include "EditorInstalls.h"

#include "Core/Base/Version.h"
#include "Core/Logging/Log.h"
#include "Core/Project/PlatformModules.h"
#include "Core/Utils/Crypto.h"
#include "Core/Utils/Platform.h"
#include "Core/Utils/ProcessRunner.h"
#include "Core/Utils/TarExtract.h"

#include <yaml-cpp/yaml.h>

#if __has_include(<mbedtls/sha256.h>)
#include <mbedtls/sha256.h>
#define CQ_INSTALLS_MBEDTLS 1
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

namespace Conqueror::Installs
{
    namespace fs = std::filesystem;

    namespace
    {
        std::mutex s_RegistryMutex;
        fs::path s_Root;

        std::string Env(const char* name)
        {
            const char* v = std::getenv(name);
            return v ? std::string(v) : std::string();
        }

        std::string NowStamp()
        {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
            return buf;
        }

        std::string ReadFile(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        bool WriteFileAtomic(const fs::path& p, const std::string& text)
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            const fs::path tmp = p.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out)
                    return false;
                out << text;
                if (!out)
                    return false;
            }
            fs::rename(tmp, p, ec);
            if (ec)
            {
                fs::remove(p, ec);
                fs::rename(tmp, p, ec);
            }
            return !ec;
        }

        // Yalnız kurulum kökünün İÇİNDEKİ bir yolu siler (kökün kendisini ya da dışını asla)
        bool RemoveInsideRoot(const fs::path& path)
        {
            std::error_code ec;
            const fs::path root = fs::weakly_canonical(InstallRoot(), ec).lexically_normal();
            const fs::path target = fs::weakly_canonical(path, ec).lexically_normal();
            if (root.empty() || target.empty() || target == root)
                return false;
            const fs::path rel = target.lexically_relative(root);
            if (rel.empty() || *rel.begin() == ".." || rel.is_absolute())
                return false;
            if (!fs::exists(target, ec))
                return true;
            fs::remove_all(target, ec);
            return !ec;
        }

        std::string Quote(const std::string& s)
        {
            std::string out = "\"";
            for (char c : s)
            {
                if (c == '"')
                    out += "\\\"";
                else
                    out += c;
            }
            return out + "\"";
        }

        std::string CurlProgram()
        {
            std::string curl = PlatformModules::FindProgram("curl");
            return curl;
        }

        std::string Str(const YAML::Node& n, const char* key)
        {
            const YAML::Node v = n[key];
            if (!v || v.IsNull() || !v.IsScalar())
                return {};
            return v.as<std::string>();
        }

        uint64_t U64(const YAML::Node& n, const char* key)
        {
            const YAML::Node v = n[key];
            if (!v || !v.IsScalar())
                return 0;
            try { return v.as<uint64_t>(); } catch (...) { return 0; }
        }

        bool Bool(const YAML::Node& n, const char* key)
        {
            const YAML::Node v = n[key];
            if (!v || !v.IsScalar())
                return false;
            try { return v.as<bool>(); } catch (...) { return false; }
        }

        // Sürüm metni ("v0.575.3", "Conqueror's Engine 0.575.3") içindeki ilk "a.b.c"
        std::string ExtractVersion(const std::string& text)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (!std::isdigit((unsigned char)text[i]))
                    continue;
                size_t j = i;
                int dots = 0;
                while (j < text.size() && (std::isdigit((unsigned char)text[j]) || text[j] == '.'))
                {
                    if (text[j] == '.')
                        ++dots;
                    ++j;
                }
                if (dots >= 1)
                {
                    std::string v = text.substr(i, j - i);
                    while (!v.empty() && v.back() == '.')
                        v.pop_back();
                    // "-rc1" gibi ön sürüm eki
                    if (j < text.size() && text[j] == '-')
                    {
                        size_t k = j + 1;
                        while (k < text.size() && std::isalnum((unsigned char)text[k]))
                            ++k;
                        v += text.substr(j, k - j);
                    }
                    return v;
                }
                i = j;
            }
            return {};
        }

        fs::path ResolveSymlinks(const fs::path& p)
        {
            std::error_code ec;
            fs::path r = fs::canonical(p, ec);
            return ec ? p : r;
        }

        // JDK kökündeki "release" dosyasından JAVA_VERSION
        std::string JdkVersion(const fs::path& home)
        {
            std::ifstream in(home / "release");
            std::string line;
            while (std::getline(in, line))
                if (line.rfind("JAVA_VERSION=", 0) == 0)
                {
                    std::string v = line.substr(13);
                    v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
                    return v;
                }
            return {};
        }

        int MajorOf(const std::string& v)
        {
            if (v.rfind("1.", 0) == 0)   // 1.8 -> 8
                return std::atoi(v.c_str() + 2);
            return std::atoi(v.c_str());
        }

        std::string Exe(const std::string& name)
        {
#ifdef _WIN32
            return name + ".exe";
#else
            return name;
#endif
        }

        std::string MB(uint64_t bytes)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f MB", (double)bytes / (1024.0 * 1024.0));
            return buf;
        }
    }

    // Kurulum düzeni: <kök>/bin (editör, Hub, oyuncu), <kök>/lib (paylaşımlı kitaplıklar), <kök>/Modules, <kök>/Tools,
    // <kök>/conqueror-install.json. Editör ve Hub kendi klasörünü (bin) verir: modüller bir üstte
    static fs::path InstallRootOf(const fs::path& dir)
    {
        std::error_code ec;
        if (dir.filename() == "bin" && (fs::exists(dir.parent_path() / "conqueror-install.json", ec) || fs::is_directory(dir.parent_path() / "Modules", ec)))
            return dir.parent_path();
        return dir;
    }

    // ------------------------------------------------------------------ kanallar
    const char* ChannelName(Channel channel)
    {
        switch (channel)
        {
            case Channel::Official: return "Official";
            case Channel::PreRelease: return "Pre-release";
            case Channel::ConquerorX: return "ConquerorX";
            case Channel::Local: return "Local";
        }
        return "Official";
    }

    Channel ChannelFromName(const std::string& name)
    {
        if (name == "Pre-release" || name == "prerelease" || name == "PreRelease")
            return Channel::PreRelease;
        if (name == "ConquerorX" || name == "conquerorx")
            return Channel::ConquerorX;
        if (name == "Local" || name == "local")
            return Channel::Local;
        return Channel::Official;
    }

    // ------------------------------------------------------------------ konum ve kayıt
    fs::path DefaultInstallRoot()
    {
#if defined(_WIN32)
        std::string base = Env("LOCALAPPDATA");
        if (base.empty())
            base = (fs::path(Platform::GetUserHomeDirectory()) / "AppData" / "Local").string();
        return fs::path(base) / "Programs" / "ConquerorEngine" / "Editors";
#elif defined(__APPLE__)
        return fs::path(Platform::GetUserHomeDirectory()) / "Library" / "Application Support" / "ConquerorEngine" / "Editors";
#else
        std::string base = Env("XDG_DATA_HOME");
        if (base.empty())
            base = (fs::path(Platform::GetUserHomeDirectory()) / ".local" / "share").string();
        return fs::path(base) / "ConquerorEngine" / "Editors";
#endif
    }

    fs::path InstallRoot()
    {
        const std::string env = Env("CQ_INSTALLS_DIR");
        if (!env.empty())
            return fs::path(env);
        if (!s_Root.empty())
            return s_Root;
        return DefaultInstallRoot();
    }

    void SetInstallRoot(const fs::path& root) { s_Root = root; }

    std::vector<Install> LoadInstalls(const fs::path& configDir)
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        std::vector<Install> out;
        const fs::path file = configDir / "installs.yaml";
        std::error_code ec;
        if (!fs::exists(file, ec))
            return out;
        try
        {
            YAML::Node root = YAML::LoadFile(file.string());
            for (const auto& n : root["Installs"])
            {
                Install i;
                i.Version = Str(n, "Version");
                i.Kind = ChannelFromName(Str(n, "Channel"));
                i.Path = fs::path(std::u8string((const char8_t*)Str(n, "Path").c_str()));
                i.Commit = Str(n, "Commit");
                i.Located = Bool(n, "Located");
                i.InstalledAt = Str(n, "InstalledAt");
                if (n["Modules"] && n["Modules"].IsSequence())
                    for (const auto& m : n["Modules"])
                        i.Modules.push_back(m.as<std::string>());
                if (!i.Path.empty())
                    out.push_back(std::move(i));
            }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("[Installs] Could not read {0}: {1}", file.string(), e.what());
        }
        return out;
    }

    bool SaveInstalls(const fs::path& configDir, const std::vector<Install>& installs)
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "Installs" << YAML::Value << YAML::BeginSeq;
        for (const auto& i : installs)
        {
            if (i.Bundled)
                continue;   // Hub'ın kendi editörü her açılışta yeniden eklenir
            out << YAML::BeginMap;
            out << YAML::Key << "Version" << YAML::Value << i.Version;
            out << YAML::Key << "Channel" << YAML::Value << ChannelName(i.Kind);
            const std::u8string p = i.Path.u8string();
            out << YAML::Key << "Path" << YAML::Value << std::string(p.begin(), p.end());
            out << YAML::Key << "Commit" << YAML::Value << i.Commit;
            out << YAML::Key << "Located" << YAML::Value << i.Located;
            out << YAML::Key << "InstalledAt" << YAML::Value << i.InstalledAt;
            out << YAML::Key << "Modules" << YAML::Value << YAML::Flow << i.Modules;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;
        return WriteFileAtomic(configDir / "installs.yaml", std::string(out.c_str()) + "\n");
    }

    bool RegisterInstall(const fs::path& configDir, const Install& install)
    {
        std::vector<Install> all = LoadInstalls(configDir);
        std::error_code ec;
        bool replaced = false;
        for (auto& i : all)
        {
            const bool samePath = fs::equivalent(i.Path, install.Path, ec) || i.Path == install.Path;
            if (samePath || (i.Kind == install.Kind && i.Version == install.Version && install.Kind != Channel::Local))
            {
                i = install;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            all.push_back(install);
        return SaveInstalls(configDir, all);
    }

    bool UnregisterInstall(const fs::path& configDir, const fs::path& path)
    {
        std::vector<Install> all = LoadInstalls(configDir);
        std::error_code ec;
        all.erase(std::remove_if(all.begin(), all.end(), [&](const Install& i) { return i.Path == path || fs::equivalent(i.Path, path, ec); }),
                  all.end());
        return SaveInstalls(configDir, all);
    }

    bool Uninstall(const fs::path& configDir, const Install& install, std::string& error)
    {
        if (install.Bundled)
        {
            error = "The editor that came with this Hub cannot be uninstalled from here.";
            return false;
        }
        if (!install.Located)
        {
            if (!RemoveInsideRoot(install.Path))
            {
                error = "Could not delete " + install.Path.string() + " (only installs inside " + InstallRoot().string() + " are deleted).";
                return false;
            }
        }
        UnregisterInstall(configDir, install.Path);
        return true;
    }

    // ------------------------------------------------------------------ bu bilgisayar
    std::string HostOS()
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#elif defined(__FreeBSD__)
        return "freebsd";
#elif defined(__OpenBSD__)
        return "openbsd";
#elif defined(__NetBSD__)
        return "netbsd";
#else
        return "linux";
#endif
    }

    std::string HostId()
    {
#if defined(__x86_64__) || defined(_M_X64)
        const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
        const char* arch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
        const char* arch = "i386";
#elif defined(__riscv)
        const char* arch = "riscv64";
#else
        const char* arch = "unknown";
#endif
        return HostOS() + "-" + arch;
    }

    fs::path EditorExecutable(const fs::path& installDir)
    {
        std::error_code ec;
        const std::string exe = Exe("ConquerorEngine");
        for (const fs::path& p : { installDir / exe, installDir / "bin" / exe, installDir / "ConquerorEngine.app" / "Contents" / "MacOS" / "ConquerorEngine" })
            if (fs::is_regular_file(p, ec))
                return p;
        return {};
    }

    std::vector<std::string> InstalledModules(const fs::path& installDir)
    {
        std::vector<std::string> out;
        std::error_code ec;
        const fs::path dir = InstallRootOf(installDir) / "Modules";
        if (fs::is_directory(dir, ec))
            for (const auto& e : fs::directory_iterator(dir, ec))
            {
                const std::string name = e.path().filename().string();
                if (e.is_directory(ec) && name.find(".partial") == std::string::npos && !name.empty() && name[0] != '.')
                    out.push_back(name);
            }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string ReadInstallVersion(const fs::path& installDir, std::string* commit, Channel* channel)
    {
        std::error_code ec;
        const fs::path info = InstallRootOf(installDir) / "conqueror-install.json";
        if (fs::exists(info, ec))
        {
            try
            {
                YAML::Node n = YAML::LoadFile(info.string());
                if (commit)
                    *commit = Str(n, "commit");
                if (channel)
                    *channel = ChannelFromName(Str(n, "channel"));
                const std::string v = Str(n, "version");
                if (!v.empty())
                    return v;
            }
            catch (...)
            {
            }
        }
        // Editörün kendisine sor (pencere açmadan sürümü yazar ve çıkar)
        const fs::path exe = EditorExecutable(installDir);
        if (exe.empty())
            return {};
        ProcessRunner runner;
        if (!runner.Start(Quote(exe.string()) + " --version", installDir))
            return {};
        std::string output;
        const auto start = std::chrono::steady_clock::now();
        while (runner.IsRunning() && std::chrono::steady_clock::now() - start < std::chrono::seconds(10))
        {
            output += runner.ReadAvailable();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (runner.IsRunning())
            runner.Terminate();
        output += runner.ReadAvailable();
        return ExtractVersion(output);
    }

    void ApplyModuleToolPaths(const fs::path& installDir)
    {
        std::error_code ec;
        std::vector<fs::path> bins;
        for (const std::string& id : InstalledModules(installDir))
        {
            const fs::path dir = InstallRootOf(installDir) / "Modules" / id;
            if (fs::is_directory(dir / "bin", ec))
                bins.push_back(dir / "bin");
            // Paketlenmiş araç zincirleri: Modules/<id>/<araç>/bin (jdk/bin, dotnet, zig, rust/bin ...)
            for (const auto& e : fs::directory_iterator(dir, ec))
            {
                if (!e.is_directory(ec))
                    continue;
                if (fs::is_directory(e.path() / "bin", ec))
                    bins.push_back(e.path() / "bin");
                if (fs::is_regular_file(e.path() / Exe("zig"), ec) || fs::is_regular_file(e.path() / Exe("dotnet"), ec))
                    bins.push_back(e.path());
            }
            if (id == "java" && Env("JAVA_HOME").empty() && fs::is_directory(dir / "jdk", ec))
                Platform::SetEnvironmentVariable("JAVA_HOME", (dir / "jdk").string());
            if (id == "csharp" && Env("DOTNET_ROOT").empty() && fs::is_directory(dir / "dotnet", ec))
                Platform::SetEnvironmentVariable("DOTNET_ROOT", (dir / "dotnet").string());
        }
        if (bins.empty())
            return;
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string path;
        for (const auto& b : bins)
            path += b.string() + sep;
        Platform::SetEnvironmentVariable("PATH", path + Env("PATH"));
        CQ_CORE_INFO("[Installs] {0} module tool folder(s) added to PATH", bins.size());
    }

    std::string PlatformModuleId(const std::string& platform)
    {
        static const std::pair<const char*, const char*> table[] = {
            { "Windows", "windows" }, { "Windows ARM64", "windows-arm64" }, { "Linux", "linux" }, { "Linux ARM64", "linux-arm64" },
            { "macOS", "macos" }, { "FreeBSD", "bsd" }, { "OpenBSD", "bsd" }, { "NetBSD", "bsd" }, { "GNU Hurd", "hurd" },
            { "Embedded Linux", "embedded-linux" }, { "Dedicated Server", "server" }, { "Web", "web" }, { "Android", "android" },
            { "Android TV", "android" }, { "Android Automotive", "android" }, { "ChromeOS", "android" }, { "Meta Quest", "meta-quest" },
            { "Android XR", "android-xr" }, { "iOS", "ios" }, { "tvOS", "tvos" }, { "visionOS", "visionos" }, { "QNX", "qnx" },
            { "HarmonyOS", "harmonyos" },
        };
        for (const auto& [name, id] : table)
            if (platform == name)
                return id;
        return "linux-extra";   // Linux i386 / ARMhf / RISC-V / PowerPC / LoongArch
    }

    fs::path PrebuiltPlayerDir(const fs::path& installDir, const std::string& platform)
    {
        // "Linux ARM (32-bit)" -> "linux-arm-32-bit", "GNU Hurd" -> "gnu-hurd"
        std::string key;
        for (char c : platform)
        {
            if (std::isalnum((unsigned char)c))
                key += (char)std::tolower((unsigned char)c);
            else if (!key.empty() && key.back() != '-')
                key += '-';
        }
        while (!key.empty() && key.back() == '-')
            key.pop_back();
        const std::string id = PlatformModuleId(platform);
        // Android ailesi (TV, Automotive, ChromeOS, Quest, Android XR) aynı ARM64 oyuncuyu kullanır: player/android
        if (id == "android" || id == "meta-quest" || id == "android-xr")
            key = "android";
        const fs::path dir = InstallRootOf(installDir) / "Modules" / id / "player" / key;
        const bool windows = platform.rfind("Windows", 0) == 0;
        std::error_code ec;
        if (id == "web")
            return fs::is_regular_file(dir / "conqueror.wasm", ec) ? dir : fs::path();
        if (key == "android")
            return fs::is_regular_file(dir / "arm64-v8a" / "libconqueror.so", ec) ? dir : fs::path();
        return fs::is_regular_file(dir / "bin" / (windows ? "ConquerorRuntime.exe" : "ConquerorRuntime"), ec) ? dir : fs::path();
    }

    // ------------------------------------------------------------------ katalog
    const std::vector<ModuleInfo>& Catalog()
    {
        static const std::vector<ModuleInfo> catalog = [] {
            std::vector<ModuleInfo> c;
            auto add = [&](const char* id, const char* name, const char* category, const char* description, std::vector<std::string> reqs,
                           std::vector<std::string> hosts = {}, bool builtIn = false, bool def = false) {
                ModuleInfo m;
                m.Id = id;
                m.Name = name;
                m.Category = category;
                m.Description = description;
                m.Requires = std::move(reqs);
                m.Hosts = std::move(hosts);
                m.BuiltIn = builtIn;
                m.Default = def;
                c.push_back(std::move(m));
            };
            // Platformlar: hazır derlenmiş oyuncu (motor kaynaktan derlenmez); yalnız işletim sistemi üreticisinin kendi araçları
            // (Xcode, QNX SDP) lisans gereği kullanıcının bilgisayarında olmalı
            add("linux", "Linux Build Support (x86_64)", "Platforms", "Prebuilt player for 64-bit Linux desktops.", {}, {}, false, true);
            add("linux-arm64", "Linux Build Support (ARM64)", "Platforms", "Prebuilt player for ARM64 Linux (Raspberry Pi 4 / 5, ARM servers, ARM laptops).", {});
            add("linux-extra", "Linux Build Support (other CPUs)", "Platforms", "Prebuilt players for i386, ARMhf, RISC-V 64, PowerPC 64 LE and LoongArch 64.", {});
            add("windows", "Windows Build Support (x64)", "Platforms", "Prebuilt player for 64-bit Windows 10 / 11.", {}, {}, false, true);
            add("windows-arm64", "Windows Build Support (ARM64)", "Platforms", "Prebuilt player for Windows on ARM.", {});
            add("macos", "Mac Build Support", "Platforms", "Prebuilt player for macOS (Apple Silicon). Signing and notarization need Xcode on a Mac.", {});
            add("web", "Web Build Support", "Platforms", "Prebuilt WebAssembly + WebGL 2 player; runs in any modern browser.", {});
            add("server", "Dedicated Server Build Support", "Platforms", "Headless player for Linux and Windows game servers.", {});
            add("android", "Android Build Support", "Platforms",
                "Prebuilt ARM64 player, APK / AAB packaging and signing. Also Android TV, ChromeOS and Android Automotive.", { "jdk", "android-sdk" });
            add("meta-quest", "Meta Quest Build Support", "Platforms", "OpenXR player for Meta Quest headsets (Android based).", { "jdk", "android-sdk" });
            add("android-xr", "Android XR Build Support", "Platforms", "OpenXR player for Android XR headsets.", { "jdk", "android-sdk" });
            add("ios", "iOS Build Support", "Platforms", "Xcode project with the prebuilt iOS / iPadOS player. Building and signing need Xcode (Apple's license allows it only on a Mac).",
                { "xcode" }, { "macos" });
            add("tvos", "tvOS Build Support", "Platforms", "Apple TV player (Xcode on a Mac).", { "xcode" }, { "macos" });
            add("visionos", "visionOS Build Support", "Platforms", "Apple Vision Pro player (Xcode on a Mac).", { "xcode" }, { "macos" });
            add("bsd", "BSD Build Support", "Platforms", "Prebuilt players for FreeBSD, OpenBSD and NetBSD (x86_64).", {});
            add("hurd", "GNU/Hurd Build Support", "Platforms", "Prebuilt player for GNU/Hurd (x86_64).", {});
            add("embedded-linux", "Embedded Linux Build Support", "Platforms", "Wayland / OpenGL ES player for Automotive Grade Linux, Yocto and other embedded boards.", {});
            add("qnx", "QNX Build Support", "Platforms", "Player sources and build scripts for QNX 8. Needs the QNX Software Development Platform (commercial license).", { "qnx-sdp" });
            add("harmonyos", "HarmonyOS Build Support", "Platforms", "OpenHarmony / HarmonyOS NEXT player (x86_64 + ARM64) and HAP packaging.", { "ohos-sdk" });
            // Diller: her betik dili ayrı modül
            add("luaq", "LuaQ", "Languages", "The engine's main scripting language. Always included.", {}, {}, true, true);
            add("conquerorscript", "ConquerorScript", "Languages", "Small built-in scripting language. Always included.", {}, {}, true, true);
            add("visual", "Visual Scripting", "Languages", "Node-based scripting. Always included.", {}, {}, true, true);
            add("cpp", "C++", "Languages", "Native C++ scripts (hot reload). Needs a C++ compiler.", { "cpp-compiler" });
            add("c", "C", "Languages", "Native C scripts (hot reload). Needs a C compiler.", { "c-compiler" });
            add("rust", "Rust", "Languages", "Native Rust scripts (hot reload). Needs the Rust toolchain.", { "rust" });
            add("zig", "Zig", "Languages", "Native Zig scripts (hot reload). The Zig compiler comes with the module.", { "zig" });
            add("csharp", "C#", "Languages", "C# scripts on .NET (hosted in the editor and the player).", { "dotnet" });
            add("java", "Java", "Languages", "Java scripts on the JVM (JNI).", { "jdk" });
            add("python", "Python", "Languages", "Python scripts (embedded CPython).", { "python" });
            add("docs", "Documentation", "Documentation", "Offline copy of the manual and the scripting API reference.", {});
            return c;
        }();
        return catalog;
    }

    const ModuleInfo* FindModule(const std::string& id)
    {
        for (const auto& m : Catalog())
            if (m.Id == id)
                return &m;
        return nullptr;
    }

    bool ModuleAvailableOnHost(const ModuleInfo& module, const std::string& hostOS)
    {
        return module.Hosts.empty() || std::find(module.Hosts.begin(), module.Hosts.end(), hostOS) != module.Hosts.end();
    }

    Requirement CheckRequirement(const std::string& id, const fs::path& installDir)
    {
        Requirement r;
        r.Id = id;
        std::error_code ec;
        const fs::path modules = installDir.empty() ? fs::path() : InstallRootOf(installDir) / "Modules";
        auto found = [&](const std::string& detail) { r.Found = true; r.Detail = detail; };

        if (id == "jdk")
        {
            r.Name = "Java Development Kit 17+";
            r.Bundleable = true;
            r.License = "OpenJDK: GPL-2.0 with Classpath Exception (may be redistributed)";
            r.HowTo = "Install a JDK 17 or newer (e.g. Eclipse Temurin) or set JAVA_HOME.";
            std::vector<fs::path> homes;
            if (!modules.empty())
                homes.push_back(modules / "java" / "jdk");
            if (!Env("JAVA_HOME").empty())
                homes.emplace_back(Env("JAVA_HOME"));
            const std::string javac = PlatformModules::FindProgram("javac");
            if (!javac.empty())
                homes.push_back(ResolveSymlinks(javac).parent_path().parent_path());
            for (const auto& h : homes)
            {
                if (!fs::is_regular_file(h / "bin" / Exe("javac"), ec))
                    continue;
                const std::string v = JdkVersion(h);
                if (!v.empty() && MajorOf(v) < 17)
                {
                    r.Detail = "JDK " + v + " at " + h.string() + " is too old (17+ needed)";
                    continue;
                }
                found("JDK " + (v.empty() ? std::string("?") : v) + " at " + h.string());
                break;
            }
            if (!r.Found && r.Detail.empty())
                r.Detail = "No JDK found (JAVA_HOME, PATH)";
        }
        else if (id == "dotnet")
        {
            r.Name = ".NET 8+ SDK";
            r.Bundleable = true;
            r.License = ".NET: MIT (may be redistributed)";
            r.HowTo = "Install the .NET 8 SDK from dot.net or set DOTNET_ROOT.";
            std::vector<fs::path> roots;
            if (!modules.empty())
                roots.push_back(modules / "csharp" / "dotnet");
            if (!Env("DOTNET_ROOT").empty())
                roots.emplace_back(Env("DOTNET_ROOT"));
            const std::string dotnet = PlatformModules::FindProgram("dotnet");
            if (!dotnet.empty())
                roots.push_back(ResolveSymlinks(dotnet).parent_path());
            for (const auto& root : roots)
            {
                const fs::path rt = root / "shared" / "Microsoft.NETCore.App";
                if (!fs::is_directory(rt, ec))
                    continue;
                std::string best;
                for (const auto& e : fs::directory_iterator(rt, ec))
                    if (CompareVersions(e.path().filename().string(), best) > 0)
                        best = e.path().filename().string();
                if (MajorOf(best) >= 8)
                {
                    found(".NET " + best + " at " + root.string());
                    break;
                }
                r.Detail = ".NET " + best + " at " + root.string() + " is too old (8+ needed)";
            }
            if (!r.Found && r.Detail.empty())
                r.Detail = "No .NET runtime found (DOTNET_ROOT, PATH)";
        }
        else if (id == "cpp-compiler" || id == "c-compiler")
        {
            const bool cpp = id == "cpp-compiler";
            r.Name = cpp ? "C++ compiler" : "C compiler";
            r.Bundleable = true;
            r.License = "LLVM / Clang: Apache-2.0 with LLVM exception; Zig (zig c++): MIT";
#if defined(_WIN32)
            r.HowTo = "Install Visual Studio (Desktop development with C++) or LLVM, or add the Zig module (zig cc / zig c++).";
#elif defined(__APPLE__)
            r.HowTo = "Run: xcode-select --install (Command Line Tools), or add the Zig module (zig cc / zig c++).";
#else
            r.HowTo = cpp ? "Install g++ or clang++ with your package manager, or add the Zig module (zig c++)."
                          : "Install gcc or clang with your package manager, or add the Zig module (zig cc).";
#endif
            const std::string envVar = Env(cpp ? "CXX" : "CC");
            if (!envVar.empty())
                found(std::string(cpp ? "CXX=" : "CC=") + envVar);
            if (!r.Found)
                for (const char* c : cpp ? std::initializer_list<const char*>{ "c++", "clang++", "g++", "cl" } : std::initializer_list<const char*>{ "cc", "clang", "gcc", "cl" })
                {
                    const std::string p = PlatformModules::FindProgram(c);
                    if (!p.empty())
                    {
                        found(p);
                        break;
                    }
                }
#if defined(_WIN32)
            if (!r.Found && fs::exists("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe", ec))
                found("Visual Studio (vswhere)");
#endif
            if (!r.Found && !modules.empty() && fs::is_regular_file(modules / "zig" / "zig" / Exe("zig"), ec))
                found("Zig module (zig " + std::string(cpp ? "c++" : "cc") + ")");
            if (!r.Found)
                r.Detail = "No compiler found (CXX / CC, PATH)";
        }
        else if (id == "rust")
        {
            r.Name = "Rust toolchain (rustc)";
            r.Bundleable = true;
            r.License = "Rust: MIT / Apache-2.0 (may be redistributed)";
            r.HowTo = "Install Rust with rustup (rustup.rs).";
            std::string p = PlatformModules::FindProgram("rustc");
            const fs::path cargoHome = Env("CARGO_HOME").empty() ? fs::path(Platform::GetUserHomeDirectory()) / ".cargo" : fs::path(Env("CARGO_HOME"));
            if (p.empty() && fs::is_regular_file(cargoHome / "bin" / Exe("rustc"), ec))
                p = (cargoHome / "bin" / Exe("rustc")).string();
            if (p.empty() && !modules.empty() && fs::is_regular_file(modules / "rust" / "bin" / Exe("rustc"), ec))
                p = (modules / "rust" / "bin" / Exe("rustc")).string();
            if (!p.empty())
                found(p);
            else
                r.Detail = "rustc not found (PATH, ~/.cargo/bin)";
        }
        else if (id == "zig")
        {
            r.Name = "Zig compiler";
            r.Bundleable = true;
            r.License = "Zig: MIT (comes with the module)";
            r.HowTo = "The Zig module includes the compiler; or install Zig from ziglang.org.";
            std::string p;
            if (!modules.empty() && fs::is_regular_file(modules / "zig" / "zig" / Exe("zig"), ec))
                p = (modules / "zig" / "zig" / Exe("zig")).string();
            if (p.empty())
                p = PlatformModules::FindProgram("zig");
            if (!p.empty())
                found(p);
            else
                r.Detail = "zig not found";
        }
        else if (id == "python")
        {
            r.Name = "Python 3 runtime";
            r.Bundleable = true;
            r.License = "Python: PSF License (may be redistributed)";
            r.HowTo = "Install Python 3 (python.org or your package manager).";
            std::string p = PlatformModules::FindProgram("python3");
            if (p.empty())
                p = PlatformModules::FindProgram("python");
            if (!p.empty())
                found(p);
            else
                r.Detail = "python3 not found";
        }
        else if (id == "android-sdk")
        {
            r.Name = "Android SDK (build-tools, platform)";
            r.Bundleable = false;
            r.License = "Android SDK License Agreement: Google does not allow redistribution, it is downloaded from Google";
            r.HowTo = "Install Android Studio, or the command-line tools and run: sdkmanager \"build-tools;35.0.0\" \"platforms;android-35\".";
            const std::string sdk = PlatformModules::AndroidSdkRoot();
            if (!sdk.empty() && fs::is_directory(fs::path(sdk) / "build-tools", ec))
                found(sdk);
            else
                r.Detail = sdk.empty() ? "No Android SDK (ANDROID_HOME, ~/Android/Sdk)" : "No build-tools in " + sdk;
        }
        else if (id == "xcode")
        {
            r.Name = "Xcode";
            r.Bundleable = false;
            r.License = "Apple: Xcode may only be used on Apple computers and is downloaded from Apple";
            r.HowTo = "Install Xcode from the Mac App Store.";
#if defined(__APPLE__)
            if (fs::is_directory("/Applications/Xcode.app", ec))
                found("/Applications/Xcode.app");
            else
                r.Detail = "Xcode is not installed";
#else
            r.Detail = "Xcode runs only on macOS";
#endif
        }
        else if (id == "qnx-sdp")
        {
            r.Name = "QNX Software Development Platform 8";
            r.Bundleable = false;
            r.License = "QNX SDP: commercial license from BlackBerry QNX";
            r.HowTo = "Install QNX SDP 8 from the QNX Software Center and source qnxsdp-env.sh.";
            if (!Env("QNX_HOST").empty() && !Env("QNX_TARGET").empty())
                found(Env("QNX_HOST"));
            else
                r.Detail = "QNX_HOST / QNX_TARGET not set";
        }
        else if (id == "ohos-sdk")
        {
            r.Name = "OpenHarmony SDK (native)";
            r.Bundleable = true;
            r.License = "OpenHarmony: Apache-2.0 (may be redistributed)";
            r.HowTo = "Download the OpenHarmony SDK (native) and set OHOS_SDK_NATIVE.";
            fs::path p = Env("OHOS_SDK_NATIVE");
            if (p.empty() && !modules.empty())
                p = modules / "harmonyos" / "sdk" / "native";
            if (!p.empty() && fs::is_directory(p, ec))
                found(p.string());
            else
                r.Detail = "OpenHarmony SDK not found (OHOS_SDK_NATIVE)";
        }
        else
        {
            r.Name = id;
            r.Detail = "unknown requirement";
        }
        return r;
    }

    // ------------------------------------------------------------------ sürümler
    std::string ReleasesApiUrl()
    {
        const std::string env = Env("CQ_RELEASES_API");
        return env.empty() ? std::string("https://api.github.com/repos/conquerorsengine/conqueror/releases?per_page=100") : env;
    }

    std::string SourceRepositoryUrl() { return "https://github.com/conquerorsengine/conqueror"; }

    int CompareVersions(const std::string& a, const std::string& b)
    {
        auto split = [](const std::string& v, std::vector<long>& nums, std::string& pre) {
            std::string s = v;
            if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
                s = s.substr(1);
            const size_t dash = s.find('-');
            if (dash != std::string::npos)
            {
                pre = s.substr(dash + 1);
                s = s.substr(0, dash);
            }
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, '.'))
                nums.push_back(std::atol(part.c_str()));
        };
        std::vector<long> na, nb;
        std::string pa, pb;
        split(a, na, pa);
        split(b, nb, pb);
        const size_t n = std::max(na.size(), nb.size());
        for (size_t i = 0; i < n; ++i)
        {
            const long x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
            if (x != y)
                return x < y ? -1 : 1;
        }
        if (pa == pb)
            return 0;
        if (pa.empty())
            return 1;    // ön eki olmayan daha yeni
        if (pb.empty())
            return -1;
        return pa < pb ? -1 : 1;
    }

    bool ParseReleases(const std::string& json, std::vector<Release>& out, std::string& error)
    {
        try
        {
            YAML::Node root = YAML::Load(json);
            if (!root.IsSequence())
            {
                error = root.IsMap() && root["message"] ? "GitHub: " + Str(root, "message") : "unexpected releases response";
                return false;
            }
            for (const auto& n : root)
            {
                if (Bool(n, "draft"))
                    continue;
                Release r;
                r.Tag = Str(n, "tag_name");
                r.Title = Str(n, "name");
                r.Date = Str(n, "published_at").substr(0, 10);
                r.Notes = Str(n, "body");
                r.Url = Str(n, "html_url");
                r.Commit = Str(n, "target_commitish");
                const bool pre = Bool(n, "prerelease");
                r.Kind = r.Tag == "conquerorx" ? Channel::ConquerorX : pre ? Channel::PreRelease : Channel::Official;
                r.Version = ExtractVersion(r.Tag);
                if (r.Version.empty())
                    r.Version = ExtractVersion(r.Title);
                for (const auto& a : n["assets"])
                {
                    const std::string name = Str(a, "name");
                    const std::string url = Str(a, "browser_download_url");
                    r.Assets.emplace_back(name, url);
                    if (name == "conqueror-release.json")
                        r.ManifestUrl = url;
                }
                // Bildirim yoksa adlandırmadan editör arşivi: ConquerorEngine-<sürüm>-<host>.tar.gz / .tar.zst
                for (const auto& [name, url] : r.Assets)
                    if (name.find("ConquerorEngine-") == 0 && name.find(HostId()) != std::string::npos)
                    {
                        r.EditorAsset = name;
                        r.EditorUrl = url;
                        for (const auto& a : n["assets"])
                            if (Str(a, "name") == name)
                                r.EditorSize = U64(a, "size");
                    }
                out.push_back(std::move(r));
            }
            return true;
        }
        catch (const std::exception& e)
        {
            error = std::string("could not read the releases list: ") + e.what();
            return false;
        }
    }

    bool ApplyManifest(Release& release, const std::string& manifestJson, std::string& error)
    {
        try
        {
            YAML::Node m = YAML::Load(manifestJson);
            if (!m.IsMap())
            {
                error = "conqueror-release.json is not an object";
                return false;
            }
            auto urlOf = [&](const std::string& asset) -> std::string {
                for (const auto& [name, url] : release.Assets)
                    if (name == asset)
                        return url;
                return {};
            };
            if (!Str(m, "version").empty())
                release.Version = Str(m, "version");
            if (!Str(m, "commit").empty())
                release.Commit = Str(m, "commit");
            release.EditorAsset.clear();
            release.EditorUrl.clear();
            release.EditorSha256.clear();
            release.EditorSize = 0;
            const YAML::Node editors = m["editors"];
            for (const std::string& key : { HostId(), HostOS() + "-universal", HostOS() })
            {
                if (!editors || !editors[key])
                    continue;
                const YAML::Node e = editors[key];
                release.EditorAsset = Str(e, "asset");
                release.EditorUrl = urlOf(release.EditorAsset);
                release.EditorSha256 = Str(e, "sha256");
                release.EditorSize = U64(e, "size");
                break;
            }
            release.Modules.clear();
            for (const auto& n : m["modules"])
            {
                const std::string id = Str(n, "id");
                ModuleInfo mod;
                if (const ModuleInfo* known = FindModule(id))
                    mod = *known;
                else
                {
                    mod.Id = id;
                    mod.Name = Str(n, "name").empty() ? id : Str(n, "name");
                    mod.Category = Str(n, "category").empty() ? "Other" : Str(n, "category");
                    mod.Description = Str(n, "description");
                }
                if (n["bundles"] && n["bundles"].IsSequence())
                    for (const auto& b : n["bundles"])
                        mod.Bundles.push_back(b.as<std::string>());
                // Modül arşivi işletim sistemine göre (paketlenmiş araç zinciri) ya da ortak ("any")
                const YAML::Node assets = n["assets"];
                for (const std::string& key : { HostId(), HostOS(), std::string("any") })
                {
                    if (!assets || !assets[key])
                        continue;
                    const YAML::Node a = assets[key];
                    mod.Asset = Str(a, "asset");
                    mod.Url = urlOf(mod.Asset);
                    mod.Sha256 = Str(a, "sha256");
                    mod.Size = U64(a, "size");
                    mod.InstalledSize = U64(a, "installed_size");
                    // Paketlenmiş araç zinciri işletim sistemine göre değişir (ör. llvm-mingw yalnız Windows'ta)
                    if (a["bundles"] && a["bundles"].IsSequence())
                    {
                        mod.Bundles.clear();
                        for (const auto& b : a["bundles"])
                            mod.Bundles.push_back(b.as<std::string>());
                    }
                    break;
                }
                release.Modules.push_back(std::move(mod));
            }
            // Editörün içinde gelen diller her sürümde var
            for (const auto& c : Catalog())
                if (c.BuiltIn && std::none_of(release.Modules.begin(), release.Modules.end(), [&](const ModuleInfo& x) { return x.Id == c.Id; }))
                    release.Modules.push_back(c);
            release.HasManifest = true;
            return true;
        }
        catch (const std::exception& e)
        {
            error = std::string("could not read conqueror-release.json: ") + e.what();
            return false;
        }
    }

    std::string BuildItYourselfText(const std::string& oldestPublished)
    {
        std::string text;
        if (oldestPublished.empty())
            text = "No version has been published as a download yet.";
        else
            text = "Versions older than " + oldestPublished + " were not published as downloads.";
        text += " Build them yourself from source:\n\n"
                "  git clone " + SourceRepositoryUrl() + ".git\n"
                "  cd conqueror\n"
                "  git log --oneline --grep \"0.5\"      (find the commit of the version)\n"
                "  git checkout <commit>\n"
                "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\n"
                "  cmake --build build --parallel\n\n"
                "The editor is then in build/bin. Use Locate to add it to the Hub.";
        return text;
    }

    // ------------------------------------------------------------------ ağ
    bool FetchText(const std::string& url, std::string& out, std::string& error)
    {
        const std::string curl = CurlProgram();
        if (curl.empty())
        {
            error = "curl was not found (it comes with Windows 10+, macOS and most Linux systems)";
            return false;
        }
        ProcessRunner runner;
        const std::string cmd = Quote(curl) + " -L -f -sS --max-time 60 -H \"Accept: application/vnd.github+json\" -H \"User-Agent: ConquerorHub\" " + Quote(url);
        if (!runner.Start(cmd, fs::temp_directory_path()))
        {
            error = "could not start curl";
            return false;
        }
        std::string output;
        while (runner.IsRunning())
        {
            output += runner.ReadAvailable();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        output += runner.ReadAvailable();
        if (runner.GetExitCode() != 0)
        {
            const size_t p = output.rfind("curl: (");
            error = p != std::string::npos ? output.substr(p) : "download failed (curl exit " + std::to_string(runner.GetExitCode()) + ")";
            while (!error.empty() && (error.back() == '\n' || error.back() == '\r'))
                error.pop_back();
            return false;
        }
        out = std::move(output);
        return true;
    }

    bool DownloadFile(const std::string& url, const fs::path& destination, std::string& error, const std::atomic<bool>* cancel,
                      const std::function<void(uint64_t)>& onBytes)
    {
        const std::string curl = CurlProgram();
        if (curl.empty())
        {
            error = "curl was not found";
            return false;
        }
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        const fs::path part = destination.string() + ".part";
        fs::remove(part, ec);
        ProcessRunner runner;
        const std::string cmd = Quote(curl) + " -L -f -sS --retry 2 -H \"User-Agent: ConquerorHub\" -o " + Quote(part.string()) + " " + Quote(url);
        if (!runner.Start(cmd, destination.parent_path()))
        {
            error = "could not start curl";
            return false;
        }
        std::string output;
        while (runner.IsRunning())
        {
            output += runner.ReadAvailable();
            if (cancel && cancel->load())
            {
                runner.Terminate();
                fs::remove(part, ec);
                error = "cancelled";
                return false;
            }
            if (onBytes)
                onBytes((uint64_t)fs::file_size(part, ec));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        output += runner.ReadAvailable();
        if (runner.GetExitCode() != 0 || !fs::exists(part, ec))
        {
            const size_t p = output.rfind("curl: (");
            error = p != std::string::npos ? output.substr(p) : "download failed (curl exit " + std::to_string(runner.GetExitCode()) + ")";
            while (!error.empty() && (error.back() == '\n' || error.back() == '\r'))
                error.pop_back();
            fs::remove(part, ec);
            return false;
        }
        if (onBytes)
            onBytes((uint64_t)fs::file_size(part, ec));
        fs::remove(destination, ec);
        fs::rename(part, destination, ec);
        if (ec)
        {
            error = "could not move the download into place: " + ec.message();
            return false;
        }
        return true;
    }

    std::string FileSha256(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return {};
#ifdef CQ_INSTALLS_MBEDTLS
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        std::vector<char> buf(1 << 20);
        while (in)
        {
            in.read(buf.data(), (std::streamsize)buf.size());
            const std::streamsize n = in.gcount();
            if (n > 0)
                mbedtls_sha256_update(&ctx, (const unsigned char*)buf.data(), (size_t)n);
        }
        unsigned char digest[32];
        mbedtls_sha256_finish(&ctx, digest);
        mbedtls_sha256_free(&ctx);
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (unsigned char b : digest)
        {
            out += hex[b >> 4];
            out += hex[b & 15];
        }
        return out;
#else
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return Crypto::SHA256Fallback(data);
#endif
    }

    // ------------------------------------------------------------------ kurulum işi
    namespace
    {
        void SetStep(Job& job, const std::string& step)
        {
            std::lock_guard<std::mutex> lock(job.Mutex);
            job.Step = step;
        }

        void Fail(Job& job, const std::string& error)
        {
            {
                std::lock_guard<std::mutex> lock(job.Mutex);
                job.Error = error;
                job.Step = "Failed";
            }
            CQ_CORE_ERROR("[Installs] {0} {1}: {2}", job.Request.Version, job.Request.AddToExisting ? "(modules)" : "", error);
            job.Succeeded = false;
            job.Finished = true;
        }

        void WriteInstallInfo(const fs::path& dir, const JobRequest& req)
        {
            std::error_code ec;
            if (fs::exists(dir / "conqueror-install.json", ec))
                return;
            std::ostringstream o;
            o << "{\n  \"version\": \"" << req.Version << "\",\n  \"commit\": \"" << req.Commit << "\",\n  \"channel\": \""
              << (req.Kind == Channel::ConquerorX ? "conquerorx" : req.Kind == Channel::PreRelease ? "prerelease" : "official") << "\"\n}\n";
            std::ofstream(dir / "conqueror-install.json") << o.str();
        }

        void RunJob(const std::shared_ptr<Job>& jobPtr)
        {
            Job& job = *jobPtr;
            const JobRequest& req = job.Request;
            std::error_code ec;
            const fs::path root = InstallRoot();
            const fs::path downloads = root / ".downloads";
            fs::create_directories(downloads, ec);

            uint64_t total = 0;
            for (const auto& f : req.Files)
                total += std::max<uint64_t>(f.Size, 1);
            total = std::max<uint64_t>(total, 1);
            uint64_t done = 0;

            // Hazırlık klasörü: yeni kurulum / güncelleme yarım kalırsa eski kurulum bozulmaz
            const fs::path staging = req.AddToExisting ? req.Target : fs::path(req.Target.string() + ".partial");
            if (!req.AddToExisting)
            {
                RemoveInsideRoot(staging);
                fs::create_directories(staging, ec);
            }
            std::vector<std::string> newModules;
            for (size_t i = 0; i < req.Files.size(); ++i)
            {
                const JobFile& f = req.Files[i];
                const std::string what = f.ModuleId.empty() ? "Conqueror's Engine " + req.Version
                                                             : (FindModule(f.ModuleId) ? FindModule(f.ModuleId)->Name : f.ModuleId);
                const fs::path archive = downloads / f.Name;
                std::string error;
                const bool ok = DownloadFile(f.Url, archive, error, &job.Cancel, [&](uint64_t bytes) {
                    job.Progress = 0.85f * (float)((double)(done + std::min<uint64_t>(bytes, std::max<uint64_t>(f.Size, 1))) / (double)total);
                    SetStep(job, "Downloading " + what + " (" + MB(bytes) + (f.Size ? " / " + MB(f.Size) : std::string()) + ")");
                });
                if (!ok)
                {
                    if (!req.AddToExisting)
                        RemoveInsideRoot(staging);
                    Fail(job, job.Cancel ? "Cancelled" : "Downloading " + what + " failed: " + error);
                    return;
                }
                if (!f.Sha256.empty())
                {
                    SetStep(job, "Verifying " + what);
                    const std::string sum = FileSha256(archive);
                    if (sum != f.Sha256)
                    {
                        fs::remove(archive, ec);
                        if (!req.AddToExisting)
                            RemoveInsideRoot(staging);
                        Fail(job, "The download of " + what + " is damaged (SHA-256 mismatch). Try again.");
                        return;
                    }
                }
                SetStep(job, "Installing " + what);
                fs::path dest = staging;
                fs::path moduleTmp;
                if (!f.ModuleId.empty())
                {
                    moduleTmp = staging / "Modules" / (f.ModuleId + ".partial");
                    RemoveInsideRoot(moduleTmp);
                    dest = moduleTmp;
                }
                const float base = 0.85f * (float)((double)done / (double)total);
                if (!TarExtract::Extract(archive, dest, error, [&](float p) { job.Progress = base + 0.15f * p * (float)((double)std::max<uint64_t>(f.Size, 1) / (double)total); }))
                {
                    fs::remove(archive, ec);
                    if (!moduleTmp.empty())
                        RemoveInsideRoot(moduleTmp);
                    if (!req.AddToExisting)
                        RemoveInsideRoot(staging);
                    Fail(job, "Installing " + what + " failed: " + error);
                    return;
                }
                fs::remove(archive, ec);
                if (!moduleTmp.empty())
                {
                    const fs::path final = staging / "Modules" / f.ModuleId;
                    RemoveInsideRoot(final);
                    fs::rename(moduleTmp, final, ec);
                    newModules.push_back(f.ModuleId);
                }
                done += std::max<uint64_t>(f.Size, 1);
                job.Progress = 0.85f * (float)((double)done / (double)total);
                if (job.Cancel)
                {
                    if (!req.AddToExisting)
                        RemoveInsideRoot(staging);
                    Fail(job, "Cancelled");
                    return;
                }
            }

            if (!req.AddToExisting)
            {
                WriteInstallInfo(staging, req);
                if (fs::exists(req.Target, ec))
                {
                    if (!req.Replace)
                    {
                        RemoveInsideRoot(staging);
                        Fail(job, "This version is already installed in " + req.Target.string());
                        return;
                    }
                    // ConquerorX güncellemesi: eski yapı kenara, yenisi yerine, eskisi silinir
                    const fs::path old = req.Target.string() + ".old";
                    RemoveInsideRoot(old);
                    fs::rename(req.Target, old, ec);
                    if (ec)
                    {
                        RemoveInsideRoot(staging);
                        Fail(job, "Could not replace " + req.Target.string() + " (is the editor still running?): " + ec.message());
                        return;
                    }
                    fs::rename(staging, req.Target, ec);
                    if (ec)
                    {
                        fs::rename(old, req.Target, ec);
                        Fail(job, "Could not move the new build into place");
                        return;
                    }
                    RemoveInsideRoot(old);
                }
                else
                {
                    fs::rename(staging, req.Target, ec);
                    if (ec)
                    {
                        RemoveInsideRoot(staging);
                        Fail(job, "Could not move the install into place: " + ec.message());
                        return;
                    }
                }
            }

            Install install;
            install.Version = req.Version;
            install.Kind = req.Kind;
            install.Path = req.Target;
            install.Commit = req.Commit;
            install.InstalledAt = NowStamp();
            std::set<std::string> mods(req.ExistingModules.begin(), req.ExistingModules.end());
            for (const auto& m : newModules)
                mods.insert(m);
            for (const auto& m : InstalledModules(req.Target))
                mods.insert(m);
            install.Modules.assign(mods.begin(), mods.end());
            if (!RegisterInstall(req.ConfigDir, install))
            {
                Fail(job, "Installed, but the Hub's install list could not be written");
                return;
            }
            SetStep(job, req.AddToExisting ? "Modules installed" : "Installed");
            CQ_CORE_INFO("[Installs] {0} {1} -> {2} ({3} module(s))", ChannelName(req.Kind), req.Version, req.Target.string(), install.Modules.size());
            job.Progress = 1.0f;
            job.Succeeded = true;
            job.Finished = true;
        }
    }

    std::shared_ptr<Job> StartJob(const JobRequest& request)
    {
        auto job = std::make_shared<Job>();
        job->Request = request;
        job->Step = "Starting";
        std::thread([job]() { RunJob(job); }).detach();
        return job;
    }

    bool MakeInstallRequest(const Release& release, const std::vector<std::string>& modules, const fs::path& configDir, JobRequest& out,
                            std::string& error)
    {
        if (release.EditorUrl.empty())
        {
            error = "This release has no editor download for " + HostId() + ".";
            return false;
        }
        out = JobRequest{};
        out.Version = release.Version;
        out.Commit = release.Commit;
        out.Kind = release.Kind;
        out.ConfigDir = configDir;
        out.Target = InstallRoot() / (release.Kind == Channel::ConquerorX ? std::string("ConquerorX") : release.Version);
        out.Files.push_back({ release.EditorAsset, release.EditorUrl, release.EditorSha256, std::string(), release.EditorSize });
        for (const std::string& id : modules)
        {
            auto it = std::find_if(release.Modules.begin(), release.Modules.end(), [&](const ModuleInfo& m) { return m.Id == id; });
            if (it == release.Modules.end())
            {
                error = "Module '" + id + "' is not part of " + release.Version + ".";
                return false;
            }
            if (it->BuiltIn)
                continue;
            if (!ModuleAvailableOnHost(*it, HostOS()))
            {
                error = it->Name + " is not available on " + HostOS() + ".";
                return false;
            }
            if (it->Url.empty())
            {
                error = it->Name + " has no download in " + release.Version + ".";
                return false;
            }
            out.Files.push_back({ it->Asset, it->Url, it->Sha256, it->Id, it->Size });
        }
        return true;
    }

    bool MakeAddModulesRequest(const Release& release, const Install& install, const std::vector<std::string>& modules, const fs::path& configDir,
                               JobRequest& out, std::string& error)
    {
        out = JobRequest{};
        out.Version = install.Version;
        out.Commit = install.Commit;
        out.Kind = install.Kind;
        out.ConfigDir = configDir;
        out.Target = install.Path;
        out.AddToExisting = true;
        out.ExistingModules = install.Modules;
        for (const std::string& id : modules)
        {
            if (std::find(install.Modules.begin(), install.Modules.end(), id) != install.Modules.end())
                continue;
            auto it = std::find_if(release.Modules.begin(), release.Modules.end(), [&](const ModuleInfo& m) { return m.Id == id; });
            if (it == release.Modules.end() || it->Url.empty())
            {
                error = "Module '" + id + "' has no download for " + install.Version + ".";
                return false;
            }
            if (it->BuiltIn)
                continue;
            out.Files.push_back({ it->Asset, it->Url, it->Sha256, it->Id, it->Size });
        }
        if (out.Files.empty())
        {
            error = "Nothing to install: the selected modules are already installed.";
            return false;
        }
        return true;
    }
}
