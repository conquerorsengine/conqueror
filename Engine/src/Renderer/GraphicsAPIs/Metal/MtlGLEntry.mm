#ifdef CQ_METAL_ENABLED

// Metal çeviri katmanının GL giriş noktaları: arka uçtan bağımsız ön yüz (GLEmu/GLFrontEnd.inl) + Metal kancaları.

#include "MtlGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace Conqueror::MtlGL
{
    using MTL::Core;

#include "../GLEmu/GLFrontEnd.inl"

    void ReleaseDrawHelpers();   // MtlGLDraw.mm

    namespace
    {
        Limits s_Limits;
        constexpr uint32_t kVisibilitySlots = 4096;
    }

    bool Live() { return Ctx().Live; }

    const Limits& GetLimits() { return s_Limits; }

    uint32_t ClampSamples(GLsizei samples, bool depth)
    {
        (void)depth;
        id<MTLDevice> dev = Core::Get().Device();
        uint32_t s = 1;
        for (uint32_t n = 2; n <= (uint32_t)std::max(1, samples) && n <= 8; n *= 2)
        {
            if (![dev supportsTextureSampleCount:n])
                break;
            s = n;
        }
        return s;
    }

    // Metal'de komut arası GPU zaman damgası yok: CPU zamanı (profil çizelgesi CPU tarafını gösterir)
    void WriteTimestamp(QueryObject& q)
    {
        q.Ns = Core::NowNs();
        q.Written = true;
    }

    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns)
    {
        (void)wait;
        ns = q.Ns;
        return true;
    }

    void BeginOcclusion(QueryObject& q)
    {
        auto& c = Ctx();
        q.Slots.clear();
        q.Frame = Core::Get().FrameSerial();
        c.ActiveOcclusion = &q;
        c.OcclusionEpoch = 0;   // yuva ilk çizimde açılır (MtlGLDraw)
    }

    void EndOcclusion(QueryObject& q)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (c.ActiveOcclusion == &q && c.OcclusionEpoch == core.EncoderEpoch() && core.RenderEncoder())
            [core.RenderEncoder() setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
        c.ActiveOcclusion = nullptr;
        c.OcclusionEpoch = 0;
    }

    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        samples = 0;
        if (!c.Visibility || q.Slots.empty())
            return true;
        if (q.Frame > core.CompletedFrame())
        {
            if (!wait)
                return false;
            EndRendering();
            core.Flush();   // sıradaki komut arabelleği bitince öncekiler de bitmiştir
        }
        const auto* counts = (const uint64_t*)[c.Visibility contents];
        for (uint32_t slot : q.Slots)
            samples += counts[slot];
        return true;
    }

    // Draw'dan: etkin sorgunun bu kodlayıcıdaki yuvasını aç
    void OcclusionBeforeDraw(id<MTLRenderCommandEncoder> enc)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!c.ActiveOcclusion || !c.Visibility || c.OcclusionEpoch == core.EncoderEpoch())
            return;
        const uint32_t slot = c.NextVisibilitySlot;
        c.NextVisibilitySlot = (c.NextVisibilitySlot + 1) % kVisibilitySlots;
        ((uint64_t*)[c.Visibility contents])[slot] = 0;
        [enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:slot * 8];
        c.ActiveOcclusion->Slots.push_back(slot);
        c.OcclusionEpoch = core.EncoderEpoch();
    }

    bool Install(std::string& error)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
        {
            error = "Metal device is not initialized";
            return false;
        }
        c.Vendor = "Apple";
        c.Renderer = core.DeviceName() + " (Metal)";
        c.Version = "4.6.0 Conqueror MtlGL";
        c.Extensions = { "GL_ARB_clip_control", "GL_ARB_direct_state_access", "GL_ARB_texture_storage", "GL_ARB_texture_filter_anisotropic",
                         "GL_EXT_texture_filter_anisotropic" };
        s_Limits.MaxTextureSize = 16384;
        s_Limits.MaxCubeMapSize = 16384;
        s_Limits.MaxTextureUnits = 31;
        s_Limits.MaxVertexAttribs = 16;
        s_Limits.MaxUniformBufferBindings = MTL::kMaxConstantBuffers;
        s_Limits.MaxUniformBlockSize = 65536;
        s_Limits.UniformBufferAlignment = 256;
        s_Limits.MaxColorAttachments = 8;
        s_Limits.MaxViewport[0] = s_Limits.MaxViewport[1] = 16384;
        s_Limits.MaxAnisotropy = 16.0f;
        s_Limits.LineWidthRange[0] = s_Limits.LineWidthRange[1] = 1.0f;
        s_Limits.VideoMemoryKB = (int32_t)std::min<uint64_t>(core.VideoMemory() / 1024, 0x7FFFFFFF);

        // Kapalı öznitelikler: GL varsayılanı (0,0,0,1) — float ve tamsayı sürümü
        const float fdef[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const int32_t idef[4] = { 0, 0, 0, 1 };
        uint8_t defaults[32];
        std::memcpy(defaults, fdef, 16);
        std::memcpy(defaults + 16, idef, 16);
        c.NullVertexBuffer = [core.Device() newBufferWithBytes:defaults length:sizeof(defaults) options:MTLResourceStorageModeShared];
        c.ZeroConstants = [core.Device() newBufferWithLength:65536 options:MTLResourceStorageModeShared];
        if (!c.NullVertexBuffer || !c.ZeroConstants)
        {
            error = "Cannot create the default vertex attribute / constant buffers";
            return false;
        }
        std::memset([c.ZeroConstants contents], 0, 65536);
        c.Visibility = [core.Device() newBufferWithLength:kVisibilitySlots * 8 options:MTLResourceStorageModeShared];

        c.Live = true;
        const uint8_t black[4] = { 0, 0, 0, 255 };
        const int32_t zero = 0;
        c.NullTexture2D = MakeInternalTexture(GL_TEXTURE_2D, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureCube = MakeInternalTexture(GL_TEXTURE_CUBE_MAP, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureInt = MakeInternalTexture(GL_TEXTURE_2D, GL_R32I, &zero, GL_RED_INTEGER, GL_INT);

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
        CQ_CORE_INFO("[MtlGL] GL 4.6 translation layer ready on {0} ({1} unimplemented entry points stubbed)", core.DeviceName(), g_StubCount);
        return true;
    }

    void Shutdown()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
            return;
        EndRendering();
        core.Flush();
        c.Live = false;
        for (auto& [n, t] : c.Textures) DestroyTextureImage(*t);
        for (auto& [n, t] : c.Renderbuffers) DestroyTextureImage(*t);
        for (auto& t : c.InternalTextures) DestroyTextureImage(*t);
        DestroyTextureImage(c.BackColor);
        DestroyTextureImage(c.BackDepth);
        for (auto& [n, b] : c.Buffers) ReleaseBuffer(*b);
        for (auto& [n, p] : c.Programs) DestroyProgram(*p);
        ReleaseDrawHelpers();
        c.NullVertexBuffer = nil;
        c.ZeroConstants = nil;
        c.Visibility = nil;
        c.ActiveOcclusion = nullptr;
        c.SamplerCache.clear();
        c.Textures.clear();
        c.Renderbuffers.clear();
        c.InternalTextures.clear();
        c.Buffers.clear();
        c.Programs.clear();
        c.ShaderObjects.clear();
        c.QueryObjects.clear();
        c.NullTexture2D = c.NullTextureCube = c.NullTextureInt = nullptr;
    }
}

#endif
