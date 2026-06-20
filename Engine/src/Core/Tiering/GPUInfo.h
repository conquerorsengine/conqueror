#pragma once

#include "Core/Base/Base.h"
#include <string>

namespace Conqueror
{
    struct CQ_API GPUInfo
    {
        std::string Vendor;      // NVIDIA, AMD, Intel, etc.
        std::string Renderer;    // GPU model
        std::string Version;     // Driver version
        std::string GLSLVersion; // GLSL version
        
        uint64_t TotalVRAM = 0;      // MB
        uint64_t AvailableVRAM = 0;  // MB
        
        // Capabilities
        int MaxTextureSize = 0;
        int MaxTextureUnits = 0;
        int MaxVertexAttributes = 0;
        int MaxUniformBufferBindings = 0;
        int MaxComputeWorkGroupCount[3] = {0, 0, 0};
        int MaxComputeWorkGroupSize[3] = {0, 0, 0};
        
        // Extensions
        bool SupportsComputeShaders = false;
        bool SupportsGeometryShaders = false;
        bool SupportsTessellationShaders = false;
        bool SupportsBindlessTextures = false;
        bool SupportsMultiDrawIndirect = false;
        bool SupportsAnisotropicFiltering = false;
        float MaxAnisotropy = 0.0f;
        
        // Performance tier (estimated)
        enum class Tier { Low, Medium, High, Ultra };
        Tier PerformanceTier = Tier::Medium;
    };

    class CQ_API GPUDetector
    {
    public:
        static void Init();
        static const GPUInfo& GetInfo() { return s_Info; }
        
        // OpenGL/Vulkan'dan bilgi çek
        static void QueryOpenGL();
        static void QueryVulkan();
        
        // Performance tier tahmin et
        static GPUInfo::Tier EstimatePerformanceTier();
        
    private:
        static GPUInfo s_Info;
        
        static void ParseVendor(const std::string& renderer);
        static void QueryExtensions();
    };
}
