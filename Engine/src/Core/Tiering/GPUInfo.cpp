#include "GPUInfo.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <algorithm>
#include <cctype>

namespace Conqueror
{
    GPUInfo GPUDetector::s_Info = {};

    void GPUDetector::Init()
    {
        QueryOpenGL();
        s_Info.PerformanceTier = EstimatePerformanceTier();
        
        CQ_CORE_INFO("GPU Detection:");
        CQ_CORE_INFO("  Vendor: {0}", s_Info.Vendor);
        CQ_CORE_INFO("  Renderer: {0}", s_Info.Renderer);
        CQ_CORE_INFO("  Version: {0}", s_Info.Version);
        CQ_CORE_INFO("  VRAM: {0} MB (Available: {1} MB)", s_Info.TotalVRAM, s_Info.AvailableVRAM);
        CQ_CORE_INFO("  Performance Tier: {0}", (int)s_Info.PerformanceTier);
    }

    void GPUDetector::QueryOpenGL()
    {
        // Vendor ve renderer
        const char* vendor = (const char*)glGetString(GL_VENDOR);
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* version = (const char*)glGetString(GL_VERSION);
        const char* glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
        
        if (vendor) s_Info.Vendor = vendor;
        if (renderer) s_Info.Renderer = renderer;
        if (version) s_Info.Version = version;
        if (glslVersion) s_Info.GLSLVersion = glslVersion;
        
        ParseVendor(s_Info.Renderer);
        
        // VRAM (vendor-specific)
        if (s_Info.Vendor.find("NVIDIA") != std::string::npos)
        {
            // NVIDIA: GL_NVX_gpu_memory_info
            GLint totalMem = 0;
            GLint availMem = 0;
            glGetIntegerv(0x9047, &totalMem); // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
            glGetIntegerv(0x9049, &availMem); // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
            s_Info.TotalVRAM = totalMem / 1024; // KB to MB
            s_Info.AvailableVRAM = availMem / 1024;
        }
        else if (s_Info.Vendor.find("AMD") != std::string::npos || 
                 s_Info.Vendor.find("ATI") != std::string::npos)
        {
            // AMD: GL_ATI_meminfo
            GLint memInfo[4] = {0};
            glGetIntegerv(0x87FC, memInfo); // GL_VBO_FREE_MEMORY_ATI
            s_Info.TotalVRAM = memInfo[0] / 1024;
            s_Info.AvailableVRAM = memInfo[0] / 1024;
        }
        
        // Capabilities
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &s_Info.MaxTextureSize);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &s_Info.MaxTextureUnits);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &s_Info.MaxVertexAttributes);
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &s_Info.MaxUniformBufferBindings);
        
        // Compute shader support
        GLint maxComputeWorkGroupCount[3];
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeWorkGroupCount[0]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxComputeWorkGroupCount[1]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxComputeWorkGroupCount[2]);
        s_Info.MaxComputeWorkGroupCount[0] = maxComputeWorkGroupCount[0];
        s_Info.MaxComputeWorkGroupCount[1] = maxComputeWorkGroupCount[1];
        s_Info.MaxComputeWorkGroupCount[2] = maxComputeWorkGroupCount[2];
        
        QueryExtensions();
    }

    void GPUDetector::QueryVulkan()
    {
        // TODO: Vulkan implementation
    }

    void GPUDetector::ParseVendor(const std::string& renderer)
    {
        std::string lower = renderer;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        if (lower.find("nvidia") != std::string::npos || lower.find("geforce") != std::string::npos)
        {
            s_Info.Vendor = "NVIDIA";
        }
        else if (lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos || 
                 lower.find("ati") != std::string::npos)
        {
            s_Info.Vendor = "AMD";
        }
        else if (lower.find("intel") != std::string::npos)
        {
            s_Info.Vendor = "Intel";
        }
        else
        {
            s_Info.Vendor = "Unknown";
        }
    }

    void GPUDetector::QueryExtensions()
    {
        // Check extensions
        s_Info.SupportsComputeShaders = GLAD_GL_ARB_compute_shader;
        s_Info.SupportsGeometryShaders = GLAD_GL_ARB_geometry_shader4;
        s_Info.SupportsTessellationShaders = GLAD_GL_ARB_tessellation_shader;
        s_Info.SupportsBindlessTextures = GLAD_GL_ARB_bindless_texture;
        s_Info.SupportsMultiDrawIndirect = GLAD_GL_ARB_multi_draw_indirect;
        s_Info.SupportsAnisotropicFiltering = GLAD_GL_EXT_texture_filter_anisotropic;
        
        if (s_Info.SupportsAnisotropicFiltering)
        {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &s_Info.MaxAnisotropy);
        }
    }

    GPUInfo::Tier GPUDetector::EstimatePerformanceTier()
    {
        // Basit heuristic (VRAM, texture size, compute support)
        int score = 0;
        
        // VRAM
        if (s_Info.TotalVRAM >= 8000) score += 3;
        else if (s_Info.TotalVRAM >= 4000) score += 2;
        else if (s_Info.TotalVRAM >= 2000) score += 1;
        
        // Texture size
        if (s_Info.MaxTextureSize >= 16384) score += 2;
        else if (s_Info.MaxTextureSize >= 8192) score += 1;
        
        // Compute shaders
        if (s_Info.SupportsComputeShaders) score += 1;
        
        // Vendor (rough estimate)
        if (s_Info.Vendor == "NVIDIA")
        {
            if (s_Info.Renderer.find("RTX") != std::string::npos) score += 3;
            else if (s_Info.Renderer.find("GTX 16") != std::string::npos) score += 2;
            else if (s_Info.Renderer.find("GTX") != std::string::npos) score += 1;
        }
        else if (s_Info.Vendor == "AMD")
        {
            if (s_Info.Renderer.find("RX 6") != std::string::npos || 
                s_Info.Renderer.find("RX 7") != std::string::npos) score += 3;
            else if (s_Info.Renderer.find("RX 5") != std::string::npos) score += 2;
            else if (s_Info.Renderer.find("RX") != std::string::npos) score += 1;
        }
        
        // Score'a göre tier belirle
        if (score >= 8) return GPUInfo::Tier::Ultra;
        else if (score >= 5) return GPUInfo::Tier::High;
        else if (score >= 3) return GPUInfo::Tier::Medium;
        else return GPUInfo::Tier::Low;
    }
}
