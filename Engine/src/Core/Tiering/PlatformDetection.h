#pragma once

#include "Core/Base/Base.h"
#include <string>

namespace Conqueror
{
    // Forward declaration
    enum class QualityLevel;
    
    struct CQ_API SystemInfo
    {
        std::string OS;
        std::string CPUName;
        int CPUCores = 0;
        int CPUThreads = 0;
        uint64_t TotalRAM = 0; // MB
        uint64_t AvailableRAM = 0; // MB
        std::string GPUName;
        uint64_t VRAM = 0; // MB
    };

    class CQ_API PlatformDetection
    {
    public:
        static void Init();
        
        static const SystemInfo& GetSystemInfo() { return s_SystemInfo; }
        
        // CPU
        static std::string GetCPUName();
        static int GetCPUCores();
        static int GetCPUThreads();
        
        // RAM
        static uint64_t GetTotalRAM(); // MB
        static uint64_t GetAvailableRAM(); // MB
        
        // GPU
        static std::string GetGPUName();
        static uint64_t GetVRAM(); // MB
        
        // OS
        static std::string GetOSName();
        
        // Otomatik quality level önerisi
        static QualityLevel RecommendQualityLevel();
        
    private:
        static SystemInfo s_SystemInfo;
    };
}
