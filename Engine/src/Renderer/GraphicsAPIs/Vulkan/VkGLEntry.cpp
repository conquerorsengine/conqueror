#ifdef CQ_VULKAN_ENABLED

// Vulkan çeviri katmanının GL giriş noktaları: arka uçtan bağımsız ön yüz (GLEmu/GLFrontEnd.inl) + Vulkan kancaları.
// glad işlev göstericileri Install'da bu katmana yönlendirilir.

#include "VkGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace Conqueror::VkGL
{
    using VK::Core;

#include "../GLEmu/GLFrontEnd.inl"

    // ════════════════════════════════════════════════════════════════════════════════════════════
    //  Vulkan kancaları
    // ════════════════════════════════════════════════════════════════════════════════════════════
    namespace
    {
        Limits s_Limits;

        void DestroyTextureNow(Texture& t)
        {
            VkDevice dev = Core::Get().Device();
            if (t.SampledView) vkDestroyImageView(dev, t.SampledView, nullptr);
            for (auto& [k, v] : t.AttachmentViews) vkDestroyImageView(dev, v, nullptr);
            if (t.Image) vkDestroyImage(dev, t.Image, nullptr);
            if (t.Memory) vkFreeMemory(dev, t.Memory, nullptr);
            t.SampledView = VK_NULL_HANDLE;
            t.AttachmentViews.clear();
            t.Image = VK_NULL_HANDLE;
            t.Memory = VK_NULL_HANDLE;
        }
    }

    bool Live() { return Ctx().Live; }

    const Limits& GetLimits() { return s_Limits; }

    uint32_t ClampSamples(GLsizei samples, bool depth)
    {
        const auto& limits = Core::Get().Properties().limits;
        const VkSampleCountFlags supported = (depth ? limits.framebufferDepthSampleCounts : limits.framebufferColorSampleCounts) &
                                             limits.sampledImageColorSampleCounts;
        uint32_t s = 1;
        while (s * 2 <= (uint32_t)std::max(1, samples) && (supported & (s * 2)))
            s *= 2;
        return s;
    }

    void ReleaseBuffer(Buffer& b)
    {
        if (!b.Dedicated)
            return;
        auto& core = Core::Get();
        VkDevice dev = core.Device();
        VkBuffer buf = b.Dedicated;
        VkDeviceMemory mem = b.Memory;
        core.DeferDelete([dev, buf, mem]() { vkDestroyBuffer(dev, buf, nullptr); vkFreeMemory(dev, mem, nullptr); });
        b.Dedicated = VK_NULL_HANDLE;
        b.Memory = VK_NULL_HANDLE;
    }

    void WriteTimestamp(QueryObject& q)
    {
        auto& c = Ctx();
        if (!c.QueryPool || !c.TimestampsSupported)
            return;
        auto& core = Core::Get();
        VkCommandBuffer cmd = core.Cmd();
        // Döngüsel dizin: 4096 yazım önce kullanılmış dizin çoktan tamamlanmıştır
        const uint32_t index = c.NextQueryIndex;
        c.NextQueryIndex = (c.NextQueryIndex + 1) % Context::kQueryCount;
        vkResetQueryPool(core.Device(), c.QueryPool, index, 1);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c.QueryPool, index);
        q.Index = index;
        q.Serial = core.FrameSerial();
        q.Written = true;
    }

    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& out)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        out = 0;
        if (!c.QueryPool)
            return true;
        if (q.Serial > core.CompletedSerial() && q.Serial == core.FrameSerial() && core.InFrame())
        {
            // Henüz gönderilmedi
            if (!wait)
                return false;
            FlushAndWaitGL();
        }
        uint64_t value = 0;
        const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0);
        const VkResult r = vkGetQueryPoolResults(core.Device(), c.QueryPool, q.Index, 1, sizeof(value), &value, sizeof(value), flags);
        if (r != VK_SUCCESS)
            return false;
        out = (uint64_t)((double)value * (double)c.TimestampPeriod);
        return true;
    }

    void StartOcclusionSegment()
    {
        auto& c = Ctx();
        if (!c.ActiveOcclusion || c.OcclusionRunning || !c.OcclusionPool || !c.Rendering)
            return;
        auto& core = Core::Get();
        const uint32_t index = c.NextOcclusionIndex;
        c.NextOcclusionIndex = (c.NextOcclusionIndex + 1) % Context::kQueryCount;
        vkResetQueryPool(core.Device(), c.OcclusionPool, index, 1);
        vkCmdBeginQuery(core.Cmd(), c.OcclusionPool, index, 0);
        c.ActiveOcclusion->OcclIndices.push_back(index);
        c.ActiveOcclusion->OcclSerial = core.FrameSerial();
        c.OcclusionRunning = true;
    }

    void StopOcclusionSegment()
    {
        auto& c = Ctx();
        if (!c.OcclusionRunning)
            return;
        auto& core = Core::Get();
        vkCmdEndQuery(core.Cmd(), c.OcclusionPool, c.ActiveOcclusion ? c.ActiveOcclusion->OcclIndices.back() : 0);
        c.OcclusionRunning = false;
    }

    void BeginOcclusion(QueryObject& q)
    {
        auto& c = Ctx();
        q.OcclIndices.clear();
        q.OcclSerial = Core::Get().FrameSerial();
        c.ActiveOcclusion = &q;
        StartOcclusionSegment();   // açık kapsam yoksa ilk çizimde başlar
    }

    void EndOcclusion(QueryObject& q)
    {
        auto& c = Ctx();
        if (c.ActiveOcclusion == &q)
            StopOcclusionSegment();
        c.ActiveOcclusion = nullptr;
    }

    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        samples = 0;
        if (!c.OcclusionPool || q.OcclIndices.empty())
            return true;
        if (q.OcclSerial > core.CompletedSerial() && q.OcclSerial == core.FrameSerial() && core.InFrame())
        {
            if (!wait)
                return false;
            FlushAndWaitGL();
        }
        for (uint32_t index : q.OcclIndices)
        {
            uint64_t value = 0;
            const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0);
            if (vkGetQueryPoolResults(core.Device(), c.OcclusionPool, index, 1, sizeof(value), &value, sizeof(value), flags) != VK_SUCCESS)
                return false;
            samples += value;
        }
        return true;
    }

    bool Install(std::string& error)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
        {
            error = "Vulkan core is not initialized";
            return false;
        }
        const auto& props = core.Properties();
        c.Vendor = VendorName(props.vendorID);
        c.Renderer = core.DeviceName() + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                     std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + "." + std::to_string(VK_API_VERSION_PATCH(props.apiVersion)) + ")";
        c.Version = "4.6.0 Conqueror VkGL";
        c.Extensions = { "GL_ARB_clip_control", "GL_ARB_direct_state_access", "GL_ARB_texture_storage" };
        if (core.Features().samplerAnisotropy)
        {
            c.Extensions.push_back("GL_ARB_texture_filter_anisotropic");
            c.Extensions.push_back("GL_EXT_texture_filter_anisotropic");
        }
        {
            const auto& l = props.limits;
            s_Limits.MaxTextureSize = (int32_t)l.maxImageDimension2D;
            s_Limits.MaxCubeMapSize = (int32_t)l.maxImageDimensionCube;
            s_Limits.MaxTextureUnits = (int32_t)std::min<uint32_t>(32, l.maxPerStageDescriptorSamplers);
            s_Limits.MaxVertexAttribs = (int32_t)std::min<uint32_t>(16, l.maxVertexInputAttributes);
            s_Limits.MaxUniformBufferBindings = (int32_t)std::min<uint32_t>(64, l.maxDescriptorSetUniformBuffers);
            s_Limits.MaxUniformBlockSize = (int32_t)std::min<uint32_t>(l.maxUniformBufferRange, 0x7FFFFFFF);
            s_Limits.UniformBufferAlignment = (int32_t)l.minUniformBufferOffsetAlignment;
            s_Limits.MaxColorAttachments = (int32_t)std::min<uint32_t>(8, l.maxColorAttachments);
            s_Limits.MaxViewport[0] = (int32_t)l.maxViewportDimensions[0];
            s_Limits.MaxViewport[1] = (int32_t)l.maxViewportDimensions[1];
            s_Limits.MaxAnisotropy = core.Features().samplerAnisotropy ? l.maxSamplerAnisotropy : 1.0f;
            s_Limits.LineWidthRange[0] = l.lineWidthRange[0];
            s_Limits.LineWidthRange[1] = core.Features().wideLines ? l.lineWidthRange[1] : 1.0f;
            VkPhysicalDeviceMemoryProperties mp;
            vkGetPhysicalDeviceMemoryProperties(core.Physical(), &mp);
            VkDeviceSize best = 0;
            for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
                if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    best = std::max(best, mp.memoryHeaps[i].size);
            s_Limits.VideoMemoryKB = (int32_t)std::min<VkDeviceSize>(best / 1024, 0x7FFFFFFF);
        }
        VkDevice dev = core.Device();

        // Zaman damgası havuzu
        c.TimestampPeriod = props.limits.timestampPeriod;
        c.TimestampsSupported = props.limits.timestampComputeAndGraphics == VK_TRUE;
        VkQueryPoolCreateInfo qi{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = Context::kQueryCount;
        if (vkCreateQueryPool(dev, &qi, nullptr, &c.QueryPool) == VK_SUCCESS)
            vkResetQueryPool(dev, c.QueryPool, 0, Context::kQueryCount);
        else
            c.QueryPool = VK_NULL_HANDLE;

        // Örtüşme sorgusu havuzu
        VkQueryPoolCreateInfo oi{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        oi.queryType = VK_QUERY_TYPE_OCCLUSION;
        oi.queryCount = Context::kQueryCount;
        if (vkCreateQueryPool(dev, &oi, nullptr, &c.OcclusionPool) == VK_SUCCESS)
            vkResetQueryPool(dev, c.OcclusionPool, 0, Context::kQueryCount);
        else
            c.OcclusionPool = VK_NULL_HANDLE;

        // Kapalı öznitelikler için sıfır adımlı arabellek: GL varsayılanı (0,0,0,1)
        void* mapped = nullptr;
        if (!core.CreateBuffer(32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               c.NullVertexBuffer, c.NullVertexMemory, &mapped))
        {
            error = "Cannot allocate the default vertex attribute buffer";
            return false;
        }
        const float fdef[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const int32_t idef[4] = { 0, 0, 0, 1 };
        std::memcpy(mapped, fdef, sizeof(fdef));
        std::memcpy(static_cast<uint8_t*>(mapped) + 16, idef, sizeof(idef));

        c.Live = true;

        // Eksik dokular GL gibi (0,0,0,1) döndürür
        const uint8_t black[4] = { 0, 0, 0, 255 };
        const int32_t zero = 0;
        c.NullTexture2D = MakeInternalTexture(GL_TEXTURE_2D, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureCube = MakeInternalTexture(GL_TEXTURE_CUBE_MAP, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureInt = MakeInternalTexture(GL_TEXTURE_2D, GL_R32I, &zero, GL_RED_INTEGER, GL_INT);

        // Varsayılan çerçeve arabelleği
        c.DefaultFB.Color[0].Texture = 0xFFFFFFF0u;
        c.DefaultFB.Depth.Texture = 0xFFFFFFF1u;
        c.DefaultFB.Stencil.Texture = 0xFFFFFFF1u;
        c.DefaultFB.ReadBuffer = GL_COLOR_ATTACHMENT0;
        BeginFrameResources();
        const GLint w = (GLint)c.BackColor.Width, h = (GLint)c.BackColor.Height;
        c.S.Viewport[2] = c.S.Scissor[2] = w;
        c.S.Viewport[3] = c.S.Scissor[3] = h;

        if (!gladLoadGLLoader((GLADloadproc)&GetProc))
        {
            error = "glad could not load the GL translation layer";
            return false;
        }
        CQ_CORE_INFO("[VkGL] GL 4.6 translation layer ready on {0} ({1} unimplemented entry points stubbed)", core.DeviceName(), g_StubCount);
        return true;
    }

    void Shutdown()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!c.Live || !core.IsInitialized())
            return;
        EndRendering();
        core.FlushAndWait();
        vkDeviceWaitIdle(core.Device());
        c.Live = false;
        VkDevice dev = core.Device();
        for (auto& [n, t] : c.Textures) DestroyTextureNow(*t);
        for (auto& [n, t] : c.Renderbuffers) DestroyTextureNow(*t);
        for (auto& t : c.InternalTextures) DestroyTextureNow(*t);
        DestroyTextureNow(c.BackColor);
        DestroyTextureNow(c.BackDepth);
        for (auto& [n, b] : c.Buffers)
        {
            if (b->Dedicated) vkDestroyBuffer(dev, b->Dedicated, nullptr);
            if (b->Memory) vkFreeMemory(dev, b->Memory, nullptr);
        }
        for (auto& [n, p] : c.Programs)
        {
            if (p->Vert) vkDestroyShaderModule(dev, p->Vert, nullptr);
            if (p->Frag) vkDestroyShaderModule(dev, p->Frag, nullptr);
            if (p->Layout) vkDestroyPipelineLayout(dev, p->Layout, nullptr);
            if (p->SetLayout) vkDestroyDescriptorSetLayout(dev, p->SetLayout, nullptr);
        }
        for (auto& [k, p] : c.Pipelines)
            if (p) vkDestroyPipeline(dev, p, nullptr);
        for (auto& [k, s] : c.SamplerCache)
            vkDestroySampler(dev, s, nullptr);
        for (auto& pools : c.Pools)
            for (auto pool : pools)
                vkDestroyDescriptorPool(dev, pool, nullptr);
        if (c.QueryPool) vkDestroyQueryPool(dev, c.QueryPool, nullptr);
        if (c.OcclusionPool) vkDestroyQueryPool(dev, c.OcclusionPool, nullptr);
        c.OcclusionPool = VK_NULL_HANDLE;
        c.ActiveOcclusion = nullptr;
        c.OcclusionRunning = false;
        if (c.NullVertexBuffer) vkDestroyBuffer(dev, c.NullVertexBuffer, nullptr);
        if (c.NullVertexMemory) vkFreeMemory(dev, c.NullVertexMemory, nullptr);
        c.Textures.clear();
        c.Renderbuffers.clear();
        c.InternalTextures.clear();
        c.Buffers.clear();
        c.Programs.clear();
        c.ShaderObjects.clear();
        c.Pipelines.clear();
        c.SamplerCache.clear();
        for (auto& pools : c.Pools)
            pools.clear();
        c.QueryPool = VK_NULL_HANDLE;
        c.NullVertexBuffer = VK_NULL_HANDLE;
        c.NullVertexMemory = VK_NULL_HANDLE;
        c.NullTexture2D = c.NullTextureCube = c.NullTextureInt = nullptr;
    }
}

#endif
