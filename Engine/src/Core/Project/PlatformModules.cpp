#include "PlatformModules.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Conqueror::PlatformModules
{
    namespace
    {
        std::string Env(const char* name)
        {
            const char* v = std::getenv(name);
            return v ? std::string(v) : std::string();
        }

        std::string HostName()
        {
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
            return "Windows ARM64";
#elif defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__OpenBSD__)
            return "OpenBSD";
#elif defined(__NetBSD__)
            return "NetBSD";
#elif defined(CQ_PLATFORM_BSD)
            return "FreeBSD";
#elif defined(CQ_PLATFORM_HURD)
            return "GNU";
#elif defined(__aarch64__)
            return "Linux ARM64";
#else
            return "Linux";
#endif
        }

        // Klasördeki ilk alt klasör (ör. ndk/29.0.x), sürüm sırasıyla en yenisi
        std::string NewestChild(const fs::path& dir)
        {
            std::error_code ec;
            std::string best;
            if (!fs::is_directory(dir, ec))
                return best;
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_directory(ec) && e.path().filename().string() > best)
                    best = e.path().filename().string();
            return best;
        }

        Module Make(const std::string& platform, Status state, const std::string& toolchain, const std::string& detail,
                    const std::string& install = std::string())
        {
            Module m;
            m.Platform = platform;
            m.State = state;
            m.Toolchain = toolchain;
            m.Detail = detail;
            m.Install = install;
            return m;
        }

        Module Android(const std::string& platform)
        {
            const std::string toolchain = "Android SDK + NDK + JDK (APK built and signed without Gradle)";
            const std::string install =
                "Android Studio, or the command-line tools: sdkmanager \"platform-tools\" \"build-tools;37.0.0\" \"platforms;android-36\" "
                "\"ndk;29.0.14206865\". JDK 17+ (Fedora: sudo dnf install java-21-openjdk-devel). Set ANDROID_HOME if the SDK is "
                "not in ~/Android/Sdk.";
            const std::string sdk = AndroidSdkRoot();
            if (sdk.empty())
                return Make(platform, Status::Missing, toolchain, "Android SDK not found (ANDROID_HOME, ANDROID_SDK_ROOT, ~/Android/Sdk)", install);
            std::string ndk = Env("ANDROID_NDK_HOME");
            if (ndk.empty())
            {
                const std::string v = NewestChild(fs::path(sdk) / "ndk");
                if (!v.empty())
                    ndk = (fs::path(sdk) / "ndk" / v).string();
            }
            const std::string buildTools = NewestChild(fs::path(sdk) / "build-tools");
            const std::string platforms = NewestChild(fs::path(sdk) / "platforms");
            std::string keytool = FindProgram("keytool");
            if (keytool.empty() && !Env("JAVA_HOME").empty())
            {
                std::error_code ec;
                const fs::path k = fs::path(Env("JAVA_HOME")) / "bin" / "keytool";
                if (fs::exists(k, ec))
                    keytool = k.string();
            }
            std::string missing;
            if (ndk.empty()) missing += "NDK, ";
            if (buildTools.empty()) missing += "build-tools, ";
            if (platforms.empty()) missing += "an SDK platform, ";
            if (keytool.empty()) missing += "a JDK (keytool), ";
            if (!missing.empty())
                return Make(platform, Status::Missing, toolchain, "SDK " + sdk + " is missing " + missing.substr(0, missing.size() - 2), install);
            return Make(platform, Status::Ready, toolchain,
                        "SDK " + sdk + ", NDK " + fs::path(ndk).filename().string() + ", build-tools " + buildTools + ", " + platforms);
        }
    }

    const char* StatusName(Status status)
    {
        switch (status)
        {
            case Status::Ready: return "Ready";
            case Status::Missing: return "Missing";
            case Status::NeedsHost: return "Other computer";
            case Status::CommandLine: return "Command line";
            case Status::NotAvailable: return "Not available yet";
        }
        return "";
    }

    std::string FindProgram(const std::string& name)
    {
        const std::string path = Env("PATH");
#ifdef _WIN32
        const char sep = ';';
        const std::vector<std::string> exts = { ".exe", ".cmd", ".bat", "" };
#else
        const char sep = ':';
        const std::vector<std::string> exts = { "" };
#endif
        std::stringstream ss(path);
        std::string dir;
        std::error_code ec;
        while (std::getline(ss, dir, sep))
        {
            if (dir.empty())
                continue;
            for (const auto& ext : exts)
            {
                const fs::path p = fs::path(dir) / (name + ext);
                if (fs::is_regular_file(p, ec))
                    return p.string();
            }
        }
        return {};
    }

    std::string AndroidSdkRoot()
    {
        std::error_code ec;
        std::vector<std::string> candidates = { Env("ANDROID_HOME"), Env("ANDROID_SDK_ROOT") };
        const std::string home = Env("HOME");
#if defined(_WIN32)
        if (!Env("LOCALAPPDATA").empty())
            candidates.push_back((fs::path(Env("LOCALAPPDATA")) / "Android" / "Sdk").string());
#elif defined(__APPLE__)
        if (!home.empty())
            candidates.push_back((fs::path(home) / "Library" / "Android" / "sdk").string());
#else
        if (!home.empty())
            candidates.push_back((fs::path(home) / "Android" / "Sdk").string());
#endif
        for (const auto& c : candidates)
            if (!c.empty() && fs::is_directory(c, ec))
                return c;
        return {};
    }

    namespace
    {
        Module DetectUncached(const std::string& platform);
    }

    // Build Settings her karede sorar: PATH / SDK taraması 3 saniye önbellekte (kurulum sonrası kendiliğinden yenilenir)
    Module Detect(const std::string& platform)
    {
        struct Entry { Module Value; std::chrono::steady_clock::time_point Time; };
        static std::mutex s_Mutex;
        static std::unordered_map<std::string, Entry> s_Cache;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            auto it = s_Cache.find(platform);
            if (it != s_Cache.end() && now - it->second.Time < std::chrono::seconds(3))
                return it->second.Value;
        }
        Module m = DetectUncached(platform);
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Cache[platform] = { m, now };
        return m;
    }

    namespace
    {
    Module DetectUncached(const std::string& platform)
    {
        const std::string host = HostName();
        std::error_code ec;
        if (platform == "Windows")
        {
            if (host == "Windows")
                return Make(platform, Status::Ready, "This PC (MSVC / MinGW build of the editor)", "Built with the player next to the editor");
            const std::string toolchain = "MinGW-w64 cross compiler (Tools/Windows/mingw-w64.toolchain.cmake)";
            const std::string install = "Fedora: sudo dnf install mingw64-gcc-c++ mingw64-winpthreads-static   "
                                        "Debian / Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix";
            if (host != "Linux")
                return Make(platform, Status::NeedsHost, toolchain, "Build on Windows or Linux");
            std::string cxx = FindProgram("x86_64-w64-mingw32-g++");
            if (cxx.empty())
                cxx = FindProgram("x86_64-w64-mingw32-clang++");
            if (cxx.empty() || FindProgram("cmake").empty())
                return Make(platform, Status::Missing, toolchain, cxx.empty() ? "x86_64-w64-mingw32-g++ not found" : "cmake not found", install);
            std::string detail = cxx + " (the first build compiles the Windows player)";
            if (!FindProgram("wine").empty())
                detail += "; Wine found: Build And Run starts the game with Wine";
            return Make(platform, Status::Ready, toolchain, detail);
        }
        if (platform == "macOS")
        {
            if (host == "macOS")
                return Make(platform, Status::Ready, "This Mac (Xcode command line tools)", "Built with the player next to the editor");
            return Make(platform, Status::NeedsHost, "Xcode command line tools on a Mac", "Open the project in the editor on a Mac to build it");
        }
        if (platform == "Linux")
        {
            if (host == "Linux")
                return Make(platform, Status::Ready, "This PC", "Built with the player next to the editor");
            return Make(platform, Status::NeedsHost, "GCC / Clang on Linux", "Open the project in the editor on Linux to build it");
        }
        // ARM64 Linux (Raspberry Pi 4 / 5, ARM dizüstüleri, Jetson): ARM64 Linux'ta yerel, x86_64 Linux'tan çapraz
        if (platform == "Linux ARM64")
        {
            if (host == "Linux ARM64")
                return Make(platform, Status::Ready, "This PC (ARM64)", "Built with the player next to the editor");
            const std::string toolchain = "Clang + lld + an ARM64 Linux sysroot (Tools/Linux/arm64-cross.toolchain.cmake)";
            const std::string install = "Run Tools/Linux/make-arm64-sysroot.sh <folder> (Debian arm64 packages; CQ_ARM64_SUITE=bookworm "
                                        "for Raspberry Pi OS) and set CQ_ARM64_SYSROOT";
            if (host != "Linux")
                return Make(platform, Status::NeedsHost, toolchain, "Build on ARM64 Linux or x86_64 Linux");
            const std::string sysroot = Env("CQ_ARM64_SYSROOT");
            if (sysroot.empty() || !fs::is_directory(fs::path(sysroot) / "usr" / "include", ec))
                return Make(platform, Status::Missing, toolchain, sysroot.empty() ? "CQ_ARM64_SYSROOT is not set" : "No usr/include in " + sysroot, install);
            if (FindProgram("clang++").empty() || FindProgram("cmake").empty())
                return Make(platform, Status::Missing, toolchain, "clang++ or cmake not found", install);
            if (FindProgram("ld.lld").empty() && Env("CQ_ARM64_LLD").empty())
                return Make(platform, Status::Missing, toolchain, "ld.lld not found", "Install lld or set CQ_ARM64_LLD (the Android NDK has one)");
            return Make(platform, Status::Ready, toolchain, "Sysroot " + sysroot + " (the first build compiles the ARM64 player)");
        }
        // Windows on ARM (Snapdragon X dizüstüleri, Surface Pro): ARM64 Windows'ta yerel; x64 Windows'ta Visual Studio'nun ARM64 araçlarıyla
        if (platform == "Windows ARM64")
        {
            if (host == "Windows ARM64")
                return Make(platform, Status::Ready, "This PC (ARM64)", "Built with the player next to the editor");
            const std::string toolchain = "Visual Studio 2022 with the ARM64 build tools";
            if (host == "Windows")
                return Make(platform, Status::CommandLine, toolchain,
                            "cmake -S . -B build-arm64 -G \"Visual Studio 17 2022\" -A ARM64 && cmake --build build-arm64 --config Release");
            return Make(platform, Status::NeedsHost, toolchain, "Build on Windows (CI: windows-11-arm)");
        }
        // Gömülü Linux (Unity Embedded Linux; AGL / Yocto / Buildroot): yalnız Wayland, varsayılan OpenGL ES
        if (platform == "Embedded Linux")
            return Make(platform, Status::CommandLine, "The device's Yocto / Buildroot SDK, or Tools/Linux/arm64-cross.toolchain.cmake",
                        "cmake -DCQ_RUNTIME_ONLY=ON -DCQ_DEFAULT_GL_PROFILE=es3.2 -DCQ_ENABLE_VULKAN=OFF -DCQ_LINUX_ENGINE_CORE_STATIC=ON "
                        "-DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=ON (Docs/Automotive.md)");
        if (platform == "Dedicated Server")
            return Make(platform, Status::NotAvailable, "The Windows / Linux / macOS player without graphics",
                        "A headless server player is not part of this engine version; built games already run as a server with --server");
        if (platform == "tvOS" || platform == "visionOS")
            return Make(platform, Status::NotAvailable, "Xcode on a Mac",
                        std::string("The ") + (platform == "tvOS" ? "Apple TV" : "Apple Vision Pro") + " player is not part of this engine version");
        if (platform == "Universal Windows Platform")
            return Make(platform, Status::NotAvailable, "Visual Studio with the Universal Windows Platform workload",
                        "The UWP player is not part of this engine version (the Windows player runs on Windows 10 / 11)");
        if (platform == "FreeBSD")
        {
            if (host == "FreeBSD")
                return Make(platform, Status::Ready, "This PC", "Built with the player next to the editor");
            const std::string toolchain = "Clang + a FreeBSD 14 sysroot (Tools/BSD/freebsd-cross.toolchain.cmake)";
            const std::string install = "Extract FreeBSD 14 base.txz (./usr/include ./usr/lib ./lib) into a folder and set "
                                        "CQ_FREEBSD_SYSROOT to it; clang + lld (Fedora: sudo dnf install clang lld)";
            if (host != "Linux")
                return Make(platform, Status::NeedsHost, toolchain, "Build on FreeBSD or Linux");
            const std::string sysroot = Env("CQ_FREEBSD_SYSROOT");
            if (sysroot.empty() || !fs::is_directory(fs::path(sysroot) / "usr" / "include", ec))
                return Make(platform, Status::Missing, toolchain, sysroot.empty() ? "CQ_FREEBSD_SYSROOT is not set" : "No usr/include in " + sysroot, install);
            if (FindProgram("clang++").empty() || FindProgram("cmake").empty())
                return Make(platform, Status::Missing, toolchain, "clang++ or cmake not found", install);
            return Make(platform, Status::Ready, toolchain, "Sysroot " + sysroot + " (the first build compiles the FreeBSD player)");
        }
        // OpenBSD / NetBSD / GNU/Hurd: Linux'tan clang + sistem kökü ile çapraz derleme (FreeBSD gibi); bağlayıcı lld
        if (platform == "OpenBSD" || platform == "NetBSD" || platform == "GNU Hurd")
        {
            const bool hurd = platform == "GNU Hurd";
            const std::string hostName = hurd ? "GNU" : platform;
            if (host == hostName)
                return Make(platform, Status::Ready, "This PC", "Built with the player next to the editor");
            const std::string var = platform == "OpenBSD" ? "CQ_OPENBSD_SYSROOT" : platform == "NetBSD" ? "CQ_NETBSD_SYSROOT" : "CQ_HURD_SYSROOT";
            const std::string toolchain = "Clang + lld + a " + platform + " sysroot (Tools/" + (hurd ? "Hurd/hurd" : std::string("BSD/") +
                                          (platform == "OpenBSD" ? "openbsd" : "netbsd")) + "-cross.toolchain.cmake)";
            const std::string install = platform == "OpenBSD" ? "Extract OpenBSD comp, base, xbase and xshare sets (./usr/include ./usr/lib ./usr/X11R6) and set CQ_OPENBSD_SYSROOT"
                                      : platform == "NetBSD" ? "Extract NetBSD base, comp, xbase and xcomp sets (./usr/include ./usr/lib ./lib ./usr/X11R7) and set CQ_NETBSD_SYSROOT"
                                                             : "Run Tools/Hurd/make-sysroot.sh <folder> (Debian hurd-amd64 packages) and set CQ_HURD_SYSROOT";
            if (host != "Linux")
                return Make(platform, Status::NeedsHost, toolchain, "Build on " + platform + " or Linux");
            const std::string sysroot = Env(var.c_str());
            if (sysroot.empty() || !fs::is_directory(fs::path(sysroot) / "usr" / "include", ec))
                return Make(platform, Status::Missing, toolchain, sysroot.empty() ? var + " is not set" : "No usr/include in " + sysroot, install);
            if (FindProgram("clang++").empty() || FindProgram("cmake").empty())
                return Make(platform, Status::Missing, toolchain, "clang++ or cmake not found", install);
            const std::string lldVar = hurd ? "CQ_HURD_LLD" : "CQ_BSD_LLD";
            if (FindProgram("ld.lld").empty() && Env(lldVar.c_str()).empty())
                return Make(platform, Status::Missing, toolchain, "ld.lld not found", "Install lld or set " + lldVar + " (the Android NDK has one)");
            return Make(platform, Status::Ready, toolchain, "Sysroot " + sysroot + " (the first build compiles the " + platform + " player)");
        }
        // Android TV / ChromeOS (Android uygulaması olarak) / Meta Quest: Android araçlarıyla aynı APK yolu
        if (platform == "Android" || platform == "Android Automotive" || platform == "Android TV" || platform == "ChromeOS" ||
            platform == "Meta Quest" || platform == "Android XR")
            return Android(platform);
        if (platform == "iOS")
            return Make(platform, Status::NotAvailable, "Xcode on a Mac", "The iOS player is not part of this engine version");
        if (platform == "Web")
        {
            const std::string emcc = FindProgram("emcc");
            return Make(platform, Status::NotAvailable, "Emscripten (emsdk)",
                        std::string("The web player is not part of this engine version") + (emcc.empty() ? "" : " (emcc found: " + emcc + ")"));
        }
        if (platform == "QNX")
        {
            const std::string toolchain = "QNX SDP 7.1 / 8.0 (Tools/QNX/qnx.toolchain.cmake)";
            const std::string qnxHost = Env("QNX_HOST"), qnxTarget = Env("QNX_TARGET");
            if (qnxHost.empty() || qnxTarget.empty())
                return Make(platform, Status::Missing, toolchain, "QNX_HOST / QNX_TARGET are not set",
                            "Install the QNX SDP and run: source <sdp>/qnxsdp-env.sh");
            return Make(platform, Status::CommandLine, toolchain, "SDP " + qnxTarget + ": build the player with the command in Project Settings > Player > QNX");
        }
        return Make(platform, Status::Missing, "", "Unknown platform");
    }
    }

    std::vector<Module> Detect()
    {
        std::vector<Module> list;
        for (const char* p : { "Windows", "Windows ARM64", "macOS", "Linux", "Linux ARM64", "Dedicated Server", "Embedded Linux", "FreeBSD",
                               "OpenBSD", "NetBSD", "GNU Hurd", "Android", "Android Automotive", "Android TV", "ChromeOS", "Meta Quest",
                               "Android XR", "iOS", "tvOS", "visionOS", "Web", "Universal Windows Platform", "QNX" })
            list.push_back(Detect(p));
        return list;
    }

    bool IsReady(const std::string& platform)
    {
        return Detect(platform).State == Status::Ready;
    }
}
