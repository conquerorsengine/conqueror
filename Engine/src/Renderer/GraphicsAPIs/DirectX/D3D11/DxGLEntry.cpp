#ifdef CQ_D3D11_ENABLED

// Direct3D 11 çeviri katmanının GL giriş noktaları: arka uçtan bağımsız ön yüz (GLEmu/GLFrontEnd.inl) + D3D11 kancaları.

#include "DxGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace Conqueror::DxGL
{
    using DX::Core;

#include "../../GLEmu/GLFrontEnd.inl"

    void ReleaseDrawHelpers();   // DxGLDraw.cpp

    namespace
    {
        Limits s_Limits;
    }

    bool Live() { return Ctx().Live; }

    const Limits& GetLimits() { return s_Limits; }

    uint32_t ClampSamples(GLsizei samples, bool depth)
    {
        auto* dev = Core::Get().Device();
        uint32_t s = 1;
        for (uint32_t n = 2; n <= (uint32_t)std::max(1, samples) && n <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; n *= 2)
        {
            UINT quality = 0;
            dev->CheckMultisampleQualityLevels(depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_R16G16B16A16_FLOAT, n, &quality);
            if (quality == 0)
                break;
            s = n;
        }
        return s;
    }

    void ReleaseBuffer(Buffer& b)
    {
        SafeRelease(b.Geometry);
        SafeRelease(b.Constant);
        b.GeometrySize = b.ConstantSize = 0;
    }

    void WriteTimestamp(QueryObject& q)
    {
        auto& core = Core::Get();
        if (!q.Query)
        {
            D3D11_QUERY_DESC qd{ D3D11_QUERY_TIMESTAMP, 0 };
            ID3D11Query* raw = nullptr;
            if (FAILED(core.Device()->CreateQuery(&qd, &raw)) || !raw)
                return;
            q.Query.reset(raw, [](ID3D11Query* p) { p->Release(); });
        }
        core.Context()->End(q.Query.get());
        q.Frame = core.TimingFrame();
        q.FrameSerial = core.FrameSerial();
        q.Written = true;
    }

    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns)
    {
        ns = 0;
        if (!q.Query)
            return true;
        auto& core = Core::Get();
        UINT64 ticks = 0;
        for (;;)
        {
            const HRESULT hr = core.Context()->GetData(q.Query.get(), &ticks, sizeof(ticks), wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_OK)
                break;
            if (!wait || FAILED(hr))
                return false;
            Sleep(0);
        }
        uint64_t frequency = 0;
        bool disjoint = true;
        if (!core.TimingFrequency(q.Frame, wait, frequency, disjoint))
            return false;
        if (!disjoint && frequency)
            ns = (uint64_t)((double)ticks * 1.0e9 / (double)frequency);
        return true;
    }

    void BeginOcclusion(QueryObject& q)
    {
        auto& core = Core::Get();
        if (!q.Occl)
        {
            D3D11_QUERY_DESC qd{ D3D11_QUERY_OCCLUSION, 0 };
            ID3D11Query* raw = nullptr;
            if (FAILED(core.Device()->CreateQuery(&qd, &raw)) || !raw)
                return;
            q.Occl.reset(raw, [](ID3D11Query* p) { p->Release(); });
        }
        core.Context()->Begin(q.Occl.get());
    }

    void EndOcclusion(QueryObject& q)
    {
        if (q.Occl)
            Core::Get().Context()->End(q.Occl.get());
    }

    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples)
    {
        samples = 0;
        if (!q.Occl)
            return true;
        UINT64 value = 0;
        for (;;)
        {
            const HRESULT hr = Core::Get().Context()->GetData(q.Occl.get(), &value, sizeof(value), wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_OK)
                break;
            if (!wait || FAILED(hr))
                return false;
            Sleep(0);
        }
        samples = value;
        return true;
    }

    bool Install(std::string& error)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
        {
            error = "Direct3D 11 device is not initialized";
            return false;
        }
        c.Vendor = VendorName(core.VendorId());
        c.Renderer = core.DeviceName() + " (Direct3D 11)";
        c.Version = "4.6.0 Conqueror DxGL";
        c.Extensions = { "GL_ARB_clip_control", "GL_ARB_direct_state_access", "GL_ARB_texture_storage", "GL_ARB_texture_filter_anisotropic",
                         "GL_EXT_texture_filter_anisotropic" };
        s_Limits.MaxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        s_Limits.MaxCubeMapSize = D3D11_REQ_TEXTURECUBE_DIMENSION;
        s_Limits.MaxTextureUnits = 32;   // 16'dan büyük örnekleyici dizileri ayrık doku + ortak örnekleyiciye çevrilir
        s_Limits.MaxVertexAttribs = 16;
        s_Limits.MaxUniformBufferBindings = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        s_Limits.MaxUniformBlockSize = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        s_Limits.UniformBufferAlignment = 256;
        s_Limits.MaxColorAttachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        s_Limits.MaxViewport[0] = s_Limits.MaxViewport[1] = D3D11_VIEWPORT_BOUNDS_MAX;
        s_Limits.MaxAnisotropy = (float)D3D11_REQ_MAXANISOTROPY;
        s_Limits.LineWidthRange[0] = s_Limits.LineWidthRange[1] = 1.0f;
        s_Limits.VideoMemoryKB = (int32_t)std::min<uint64_t>(core.VideoMemory() / 1024, 0x7FFFFFFF);

        // Kapalı öznitelikler: GL varsayılanı (0,0,0,1) — float ve tamsayı sürümü
        const float fdef[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const int32_t idef[4] = { 0, 0, 0, 1 };
        uint8_t defaults[32];
        std::memcpy(defaults, fdef, 16);
        std::memcpy(defaults + 16, idef, 16);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 32;
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init{ defaults, 0, 0 };
        if (FAILED(core.Device()->CreateBuffer(&bd, &init, &c.NullVertexBuffer)))
        {
            error = "Cannot create the default vertex attribute buffer";
            return false;
        }

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
        CQ_CORE_INFO("[DxGL] GL 4.6 translation layer ready on {0} ({1} unimplemented entry points stubbed)", core.DeviceName(), g_StubCount);
        return true;
    }

    void Shutdown()
    {
        auto& c = Ctx();
        if (!c.Live || !Core::Get().IsInitialized())
            return;
        Core::Get().Context()->ClearState();
        c.Live = false;
        for (auto& [n, t] : c.Textures) DestroyTextureImage(*t);
        for (auto& [n, t] : c.Renderbuffers) DestroyTextureImage(*t);
        for (auto& t : c.InternalTextures) DestroyTextureImage(*t);
        DestroyTextureImage(c.BackColor);
        DestroyTextureImage(c.BackDepth);
        for (auto& [n, b] : c.Buffers) ReleaseBuffer(*b);
        for (auto& [n, p] : c.Programs) DestroyProgram(*p);
        for (auto& [k, s] : c.BlendStates) if (s) s->Release();
        for (auto& [k, s] : c.DepthStates) if (s) s->Release();
        for (auto& [k, s] : c.RasterStates) if (s) s->Release();
        for (auto& [k, s] : c.SamplerCache) if (s) s->Release();
        SafeRelease(c.NullVertexBuffer);
        ReleaseDrawHelpers();
        c.Textures.clear();
        c.Renderbuffers.clear();
        c.InternalTextures.clear();
        c.Buffers.clear();
        c.Programs.clear();
        c.ShaderObjects.clear();
        c.QueryObjects.clear();
        c.BlendStates.clear();
        c.DepthStates.clear();
        c.RasterStates.clear();
        c.SamplerCache.clear();
        c.NullTexture2D = c.NullTextureCube = c.NullTextureInt = nullptr;
    }
}

#endif
