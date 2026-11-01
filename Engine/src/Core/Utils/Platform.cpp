#include "Platform.h"
#include "Core/Logging/Log.h"

#include <thread>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(CQ_PLATFORM_LINUX) || defined(CQ_PLATFORM_MACOS)
    #define CQ_PLATFORM_POSIX
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include <limits.h>
    #include <cstdlib>
    #include <cstring>
    #ifdef CQ_PLATFORM_LINUX
        #include "Core/Utils/SelfExecutable.h"
        #if defined(CQ_PLATFORM_BSD)
            #include <sys/sysctl.h>
        #elif !defined(CQ_PLATFORM_QNX)
            #include <sys/sysinfo.h>
        #endif
    #else
        #include <mach-o/dyld.h>
        #include <sys/sysctl.h>
        #include <mach/mach.h>
    #endif
    extern char** environ;
    // BSD'lerde HOST_NAME_MAX tanımlı değil (POSIX en az 255 garanti eder)
    #ifndef HOST_NAME_MAX
        #define HOST_NAME_MAX 255
    #endif
#elif defined(CQ_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shlobj.h>
    #include <lmcons.h>
    // windows.h makroları (GetComputerName, GetUserName, ...) Conqueror::Platform üyeleriyle çakışır
    #ifdef GetComputerName
        #undef GetComputerName
    #endif
    #ifdef GetUserName
        #undef GetUserName
    #endif
    #ifdef SetEnvironmentVariable
        #undef SetEnvironmentVariable
    #endif
    #ifdef GetEnvironmentVariable
        #undef GetEnvironmentVariable
    #endif
    #ifdef GetCurrentDirectory
        #undef GetCurrentDirectory
    #endif
    #ifdef SetCurrentDirectory
        #undef SetCurrentDirectory
    #endif
    #ifdef GetTempPath
        #undef GetTempPath
    #endif
    #ifdef GetTickCount
        #undef GetTickCount
    #endif
    #ifdef Sleep
        #undef Sleep
    #endif
    #ifdef DebugBreak
        #undef DebugBreak
    #endif
    #ifdef MessageBox
        #undef MessageBox
    #endif
    #ifdef ShowMessageBox
        #undef ShowMessageBox
    #endif
#endif

namespace Conqueror
{
    namespace Platform
    {
        std::string GetOSName()
        {
#if defined(CQ_PLATFORM_ANDROID)
            return "Android";
#elif defined(CQ_PLATFORM_QNX)
            return "QNX";
#elif defined(CQ_PLATFORM_LINUX)
            return "Linux";
#elif defined(CQ_PLATFORM_MACOS)
            return "macOS";
#elif defined(CQ_PLATFORM_WINDOWS)
            return "Windows";
#else
            return "Unknown";
#endif
        }

        std::string GetOSVersion()
        {
#ifdef CQ_PLATFORM_LINUX
            std::ifstream file("/etc/os-release");
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    if (line.find("PRETTY_NAME=") == 0)
                    {
                        return line.substr(13, line.length() - 14); // Remove quotes
                    }
                }
            }
            return "Unknown Linux";
#elif defined(CQ_PLATFORM_WINDOWS)
            // Windows version detection
            return "Windows";
#else
            return "Unknown";
#endif
        }

        std::string GetArchitecture()
        {
#if defined(__x86_64__) || defined(_M_X64)
            return "x64";
#elif defined(__i386__) || defined(_M_IX86)
            return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
            return "ARM64";
#elif defined(__arm__) || defined(_M_ARM)
            return "ARM";
#else
            return "Unknown";
#endif
        }

        bool IsSteamDeck()
        {
            static const bool deck = [] {
                if (const char* forced = std::getenv("CQ_STEAM_DECK"))
                    return forced[0] == '1';
                if (const char* steam = std::getenv("SteamDeck"); steam && steam[0] == '1')
                    return true;
#if defined(CQ_PLATFORM_LINUX) && !defined(CQ_PLATFORM_ANDROID)
                for (const char* file : { "/sys/devices/virtual/dmi/id/board_name", "/sys/devices/virtual/dmi/id/product_name" })
                {
                    std::ifstream f(file);
                    std::string name;
                    if (f && std::getline(f, name) && (name.find("Jupiter") != std::string::npos || name.find("Galileo") != std::string::npos))
                        return true;
                }
#endif
                return false;
            }();
            return deck;
        }

        std::string GetComputerName()
        {
#ifdef CQ_PLATFORM_POSIX
            char hostname[HOST_NAME_MAX];
            if (gethostname(hostname, HOST_NAME_MAX) == 0)
                return std::string(hostname);
#elif defined(CQ_PLATFORM_WINDOWS)
            char computerName[MAX_COMPUTERNAME_LENGTH + 1];
            DWORD size = sizeof(computerName);
            if (::GetComputerNameA(computerName, &size))
                return std::string(computerName);
#endif
            return "Unknown";
        }

        std::string GetUserName()
        {
#ifdef CQ_PLATFORM_POSIX
            struct passwd* pw = getpwuid(getuid());
            if (pw)
                return std::string(pw->pw_name);
#elif defined(CQ_PLATFORM_WINDOWS)
            char username[UNLEN + 1];
            DWORD size = sizeof(username);
            if (::GetUserNameA(username, &size))
                return std::string(username);
#endif
            return "Unknown";
        }

        int GetCPUCoreCount()
        {
            return std::thread::hardware_concurrency();
        }

        int GetCPUThreadCount()
        {
            return std::thread::hardware_concurrency();
        }

        std::string GetCPUName()
        {
#ifdef CQ_PLATFORM_BSD
            {
                char model[256] = {};
                size_t size = sizeof(model);
                int mib[2] = { CTL_HW, HW_MODEL };
                if (sysctl(mib, 2, model, &size, nullptr, 0) == 0 && model[0])
                    return std::string(model);
            }
#endif
#ifdef CQ_PLATFORM_LINUX
            std::ifstream cpuinfo("/proc/cpuinfo");
            if (cpuinfo.is_open())
            {
                std::string line;
                while (std::getline(cpuinfo, line))
                {
                    if (line.find("model name") != std::string::npos)
                    {
                        size_t pos = line.find(':');
                        if (pos != std::string::npos)
                            return line.substr(pos + 2);
                    }
                }
            }
#endif
#ifdef CQ_PLATFORM_MACOS
            char brand[256] = {};
            size_t size = sizeof(brand);
            if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0 && brand[0])
                return std::string(brand);
#endif
            return "Unknown CPU";
        }

        uint64_t GetTotalPhysicalMemory()
        {
#if defined(CQ_PLATFORM_BSD)
            const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
            if (pages > 0 && pageSize > 0)
                return (uint64_t)pages * (uint64_t)pageSize;
#elif defined(CQ_PLATFORM_LINUX) && !defined(CQ_PLATFORM_QNX)
            struct sysinfo info;
            if (sysinfo(&info) == 0)
                return info.totalram * info.mem_unit;
#elif defined(CQ_PLATFORM_MACOS)
            uint64_t memsize = 0;
            size_t size = sizeof(memsize);
            if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) == 0)
                return memsize;
#elif defined(CQ_PLATFORM_WINDOWS)
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (GlobalMemoryStatusEx(&memInfo))
                return memInfo.ullTotalPhys;
#endif
            return 0;
        }

        uint64_t GetAvailablePhysicalMemory()
        {
#if defined(CQ_PLATFORM_BSD) && defined(_SC_AVPHYS_PAGES)
            const long pages = sysconf(_SC_AVPHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
            if (pages > 0 && pageSize > 0)
                return (uint64_t)pages * (uint64_t)pageSize;
#elif defined(CQ_PLATFORM_LINUX) && !defined(CQ_PLATFORM_QNX) && !defined(CQ_PLATFORM_BSD)
            struct sysinfo info;
            if (sysinfo(&info) == 0)
                return info.freeram * info.mem_unit;
#elif defined(CQ_PLATFORM_WINDOWS)
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (GlobalMemoryStatusEx(&memInfo))
                return memInfo.ullAvailPhys;
#endif
            return 0;
        }

        uint64_t GetProcessMemoryUsage()
        {
#ifdef CQ_PLATFORM_LINUX
            std::ifstream status("/proc/self/status");
            if (status.is_open())
            {
                std::string line;
                while (std::getline(status, line))
                {
                    if (line.find("VmRSS:") == 0)
                    {
                        std::istringstream iss(line.substr(6));
                        uint64_t memory;
                        iss >> memory;
                        return memory * 1024; // KB to bytes
                    }
                }
            }
#endif
            return 0;
        }

        std::string GetEnvironmentVariable(const std::string& name)
        {
            const char* value = std::getenv(name.c_str());
            return value ? std::string(value) : "";
        }

        bool SetEnvironmentVariable(const std::string& name, const std::string& value)
        {
#ifdef CQ_PLATFORM_POSIX
            return setenv(name.c_str(), value.c_str(), 1) == 0;
#elif defined(CQ_PLATFORM_WINDOWS)
            return SetEnvironmentVariableA(name.c_str(), value.c_str()) != 0;
#else
            return false;
#endif
        }

        std::unordered_map<std::string, std::string> GetAllEnvironmentVariables()
        {
            std::unordered_map<std::string, std::string> vars;
            
#ifdef CQ_PLATFORM_POSIX
            for (char** env = ::environ; *env != nullptr; ++env)
            {
                std::string envStr(*env);
                size_t pos = envStr.find('=');
                if (pos != std::string::npos)
                {
                    vars[envStr.substr(0, pos)] = envStr.substr(pos + 1);
                }
            }
#endif
            
            return vars;
        }

        uint32_t GetProcessID()
        {
#ifdef CQ_PLATFORM_POSIX
            return getpid();
#elif defined(CQ_PLATFORM_WINDOWS)
            return GetCurrentProcessId();
#else
            return 0;
#endif
        }

        std::string GetProcessPath()
        {
#ifdef CQ_PLATFORM_LINUX
            if (std::string path = SelfExecutablePath(); !path.empty())
                return path;
#elif defined(CQ_PLATFORM_MACOS)
            char raw[PATH_MAX];
            uint32_t size = sizeof(raw);
            if (_NSGetExecutablePath(raw, &size) == 0)
            {
                char resolved[PATH_MAX];
                return realpath(raw, resolved) ? std::string(resolved) : std::string(raw);
            }
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (GetModuleFileNameA(NULL, path, MAX_PATH))
                return std::string(path);
#endif
            return "";
        }

        std::string GetWorkingDirectory()
        {
#ifdef CQ_PLATFORM_POSIX
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)))
                return std::string(cwd);
#elif defined(CQ_PLATFORM_WINDOWS)
            char cwd[MAX_PATH];
            if (GetCurrentDirectoryA(MAX_PATH, cwd))
                return std::string(cwd);
#endif
            return "";
        }

        bool SetWorkingDirectory(const std::string& path)
        {
#ifdef CQ_PLATFORM_POSIX
            return chdir(path.c_str()) == 0;
#elif defined(CQ_PLATFORM_WINDOWS)
            return SetCurrentDirectoryA(path.c_str()) != 0;
#else
            return false;
#endif
        }

        std::string GetUserHomeDirectory()
        {
#ifdef CQ_PLATFORM_POSIX
            const char* home = getenv("HOME");
            if (home)
                return std::string(home);
            
            struct passwd* pw = getpwuid(getuid());
            if (pw)
                return std::string(pw->pw_dir);
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path) == S_OK)
                return std::string(path);
#endif
            return "";
        }

        std::string GetUserDocumentsDirectory()
        {
#ifdef CQ_PLATFORM_POSIX
            return GetUserHomeDirectory() + "/Documents";
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path) == S_OK)
                return std::string(path);
#endif
            return "";
        }

        std::string GetUserAppDataDirectory()
        {
#ifdef CQ_PLATFORM_LINUX
            // XDG Base Directory: $XDG_DATA_HOME (mutlak yol ise), yoksa ~/.local/share
            if (const char* xdg = getenv("XDG_DATA_HOME"); xdg && xdg[0] == '/')
                return std::string(xdg);
            return GetUserHomeDirectory() + "/.local/share";
#elif defined(CQ_PLATFORM_MACOS)
            return GetUserHomeDirectory() + "/Library/Application Support";
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK)
                return std::string(path);
#endif
            return "";
        }

        std::string GetTempDirectory()
        {
#ifdef CQ_PLATFORM_POSIX
            const char* tmpdir = getenv("TMPDIR");
            return tmpdir ? std::string(tmpdir) : "/tmp";
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (GetTempPathA(MAX_PATH, path))
                return std::string(path);
#endif
            return "";
        }

        std::string GetClipboardText()
        {
            // Platform-specific clipboard implementation
            CQ_CORE_WARN("Platform::GetClipboardText not implemented");
            return "";
        }

        bool SetClipboardText(const std::string& text)
        {
            // Platform-specific clipboard implementation
            CQ_CORE_WARN("Platform::SetClipboardText not implemented");
            return false;
        }

        int ExecuteCommand(const std::string& command)
        {
            return system(command.c_str());
        }

        int ExecuteCommandWithOutput(const std::string& command, std::string& output)
        {
#ifdef CQ_PLATFORM_POSIX
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe)
                return -1;
            
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe))
                output += buffer;
            
            return pclose(pipe);
#else
            return ExecuteCommand(command);
#endif
        }

        void ShowMessageBox(const std::string& title, const std::string& message, bool error)
        {
            CQ_CORE_INFO("MessageBox [{}]: {}", title, message);
        }

        bool ShowConfirmDialog(const std::string& title, const std::string& message)
        {
            CQ_CORE_INFO("ConfirmDialog [{}]: {}", title, message);
            return false;
        }

        bool IsBatteryPowered()
        {
            return false; // Desktop varsayımı
        }

        float GetBatteryLevel()
        {
            return 1.0f;
        }

        bool IsCharging()
        {
            return false;
        }

        bool IsNetworkAvailable()
        {
            return true; // Basit varsayım
        }

        std::string GetLocalIPAddress()
        {
            return "127.0.0.1";
        }

        void Sleep(uint32_t milliseconds)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        }

        uint64_t GetTickCount()
        {
            auto now = std::chrono::steady_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        }

        bool IsDebuggerAttached()
        {
#ifdef CQ_PLATFORM_LINUX
            std::ifstream status("/proc/self/status");
            if (status.is_open())
            {
                std::string line;
                while (std::getline(status, line))
                {
                    if (line.find("TracerPid:") == 0)
                    {
                        return line.find("TracerPid:\t0") == std::string::npos;
                    }
                }
            }
#elif defined(CQ_PLATFORM_WINDOWS)
            return IsDebuggerPresent() != 0;
#endif
            return false;
        }

        void DebugBreak()
        {
            CQ_DEBUGBREAK();
        }
    }
}
