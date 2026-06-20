#include "PlatformDetection.h"
#include "QualitySettings.h"
#include "Core/Logging/Log.h"

#include <thread>
#include <fstream>
#include <sstream>

#ifdef __linux__
    #include <sys/sysinfo.h>
    #include <unistd.h>
#elif _WIN32
    #include <windows.h>
#endif

namespace Conqueror
{
    SystemInfo PlatformDetection::s_SystemInfo = {};

    void PlatformDetection::Init()
    {
        s_SystemInfo.OS = GetOSName();
        s_SystemInfo.CPUName = GetCPUName();
        s_SystemInfo.CPUCores = GetCPUCores();
        s_SystemInfo.CPUThreads = GetCPUThreads();
        s_SystemInfo.TotalRAM = GetTotalRAM();
        s_SystemInfo.AvailableRAM = GetAvailableRAM();
        s_SystemInfo.GPUName = GetGPUName();
        s_SystemInfo.VRAM = GetVRAM();
        
        CQ_CORE_INFO("Platform Detection:");
        CQ_CORE_INFO("  OS: {0}", s_SystemInfo.OS);
        CQ_CORE_INFO("  CPU: {0} ({1} cores, {2} threads)", s_SystemInfo.CPUName, s_SystemInfo.CPUCores, s_SystemInfo.CPUThreads);
        CQ_CORE_INFO("  RAM: {0} MB (Available: {1} MB)", s_SystemInfo.TotalRAM, s_SystemInfo.AvailableRAM);
        CQ_CORE_INFO("  GPU: {0} (VRAM: {1} MB)", s_SystemInfo.GPUName, s_SystemInfo.VRAM);
    }

    std::string PlatformDetection::GetCPUName()
    {
#ifdef __linux__
        std::ifstream cpuinfo("/proc/cpuinfo");
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
        return "Unknown CPU";
#elif _WIN32
        // Windows CPU detection (WMI veya registry)
        return "Windows CPU";
#else
        return "Unknown CPU";
#endif
    }

    int PlatformDetection::GetCPUCores()
    {
        return std::thread::hardware_concurrency();
    }

    int PlatformDetection::GetCPUThreads()
    {
        return std::thread::hardware_concurrency();
    }

    uint64_t PlatformDetection::GetTotalRAM()
    {
#ifdef __linux__
        struct sysinfo info;
        if (sysinfo(&info) == 0)
            return (info.totalram * info.mem_unit) / (1024 * 1024); // MB
        return 0;
#elif _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        return memInfo.ullTotalPhys / (1024 * 1024); // MB
#else
        return 0;
#endif
    }

    uint64_t PlatformDetection::GetAvailableRAM()
    {
#ifdef __linux__
        struct sysinfo info;
        if (sysinfo(&info) == 0)
            return (info.freeram * info.mem_unit) / (1024 * 1024); // MB
        return 0;
#elif _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        return memInfo.ullAvailPhys / (1024 * 1024); // MB
#else
        return 0;
#endif
    }

    std::string PlatformDetection::GetGPUName()
    {
        // OpenGL renderer string'den alınabilir
        // Şimdilik placeholder
        return "Unknown GPU";
    }

    uint64_t PlatformDetection::GetVRAM()
    {
        // GPU VRAM detection (platform-specific)
        // Şimdilik placeholder
        return 0;
    }

    std::string PlatformDetection::GetOSName()
    {
#ifdef __linux__
        return "Linux";
#elif _WIN32
        return "Windows";
#elif __APPLE__
        return "macOS";
#else
        return "Unknown OS";
#endif
    }

    QualityLevel PlatformDetection::RecommendQualityLevel()
    {
        uint64_t ram = s_SystemInfo.TotalRAM;
        int cores = s_SystemInfo.CPUCores;
        
        // Basit heuristic
        if (ram >= 16000 && cores >= 8)
            return QualityLevel::Ultra;
        else if (ram >= 8000 && cores >= 4)
            return QualityLevel::High;
        else if (ram >= 4000 && cores >= 2)
            return QualityLevel::Medium;
        else
            return QualityLevel::Low;
    }
}
