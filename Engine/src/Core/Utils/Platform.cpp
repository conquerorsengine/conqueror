#include "Platform.h"
#include "Core/Logging/Log.h"

#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef CQ_PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/sysinfo.h>
    #include <pwd.h>
    #include <limits.h>
    #include <cstring>
#elif defined(CQ_PLATFORM_WINDOWS)
    #include <windows.h>
    #include <shlobj.h>
#endif

namespace Conqueror
{
    namespace Platform
    {
        std::string GetOSName()
        {
#ifdef CQ_PLATFORM_LINUX
            return "Linux";
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

        std::string GetComputerName()
        {
#ifdef CQ_PLATFORM_LINUX
            char hostname[HOST_NAME_MAX];
            if (gethostname(hostname, HOST_NAME_MAX) == 0)
                return std::string(hostname);
#elif defined(CQ_PLATFORM_WINDOWS)
            char computerName[MAX_COMPUTERNAME_LENGTH + 1];
            DWORD size = sizeof(computerName);
            if (GetComputerNameA(computerName, &size))
                return std::string(computerName);
#endif
            return "Unknown";
        }

        std::string GetUserName()
        {
#ifdef CQ_PLATFORM_LINUX
            struct passwd* pw = getpwuid(getuid());
            if (pw)
                return std::string(pw->pw_name);
#elif defined(CQ_PLATFORM_WINDOWS)
            char username[UNLEN + 1];
            DWORD size = sizeof(username);
            if (GetUserNameA(username, &size))
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
            return "Unknown CPU";
        }

        uint64_t GetTotalPhysicalMemory()
        {
#ifdef CQ_PLATFORM_LINUX
            struct sysinfo info;
            if (sysinfo(&info) == 0)
                return info.totalram * info.mem_unit;
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
#ifdef CQ_PLATFORM_LINUX
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
#ifdef CQ_PLATFORM_LINUX
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
            
#ifdef CQ_PLATFORM_LINUX
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
#ifdef CQ_PLATFORM_LINUX
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
            char path[PATH_MAX];
            ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
            if (count != -1)
            {
                path[count] = '\0';
                return std::string(path);
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
#ifdef CQ_PLATFORM_LINUX
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
#ifdef CQ_PLATFORM_LINUX
            return chdir(path.c_str()) == 0;
#elif defined(CQ_PLATFORM_WINDOWS)
            return SetCurrentDirectoryA(path.c_str()) != 0;
#else
            return false;
#endif
        }

        std::string GetUserHomeDirectory()
        {
#ifdef CQ_PLATFORM_LINUX
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
#ifdef CQ_PLATFORM_LINUX
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
            return GetUserHomeDirectory() + "/.local/share";
#elif defined(CQ_PLATFORM_WINDOWS)
            char path[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK)
                return std::string(path);
#endif
            return "";
        }

        std::string GetTempDirectory()
        {
#ifdef CQ_PLATFORM_LINUX
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
#ifdef CQ_PLATFORM_LINUX
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
