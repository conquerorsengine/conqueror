#ifdef CQ_D3D12_ENABLED

// Direct3D 12 çeviri katmanının GL giriş noktaları: arka uçtan bağımsız ön yüz (GLEmu/GLFrontEnd.inl) + D3D12 kancaları.

#include "D12GL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace Conqueror::D12GL
{
    using DX12::Core;

#include "../../GLEmu/GLFrontEnd.inl"

    void ReleaseDrawHelpers();   // D12GLDraw.cpp

    namespace
    {
        Limits s_Limits;

        ID3D12Resource* MakeUploadBuffer(const void* data, uint64_t size)
        {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            hp.CreationNodeMask = hp.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = size;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ID3D12Resource* r = nullptr;
            if (FAILED(Core::Get().Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                     __uuidof(ID3D12Resource), (void**)&r)))
                return nullptr;
            void* mapped = nullptr;
            if (SUCCEEDED(r->Map(0, nullptr, &mapped)) && mapped)
            {
                if (data)
                    std::memcpy(mapped, data, (size_t)size);
                else
                    std::memset(mapped, 0, (size_t)size);
                r->Unmap(0, nullptr);
            }
            return r;
        }
    }

    bool Live() { return Ctx().Live; }

    const Limits& GetLimits() { return s_Limits; }

    uint32_t ClampSamples(GLsizei samples, bool depth)
    {
        auto* dev = Core::Get().Device();
        uint32_t s = 1;
        for (uint32_t n = 2; n <= (uint32_t)std::max(1, samples) && n <= 32; n *= 2)
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
            q.Format = depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_R16G16B16A16_FLOAT;
            q.SampleCount = n;
            if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) || q.NumQualityLevels == 0)
                break;
            s = n;
        }
        return s;
    }

    void WriteTimestamp(QueryObject& q)
    {
        auto& core = Core::Get();
        q.Index = core.WriteTimestamp();
        q.Serial = core.SubmitSerial();
        q.Written = q.Index != UINT32_MAX;
    }

    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns)
    {
        return Core::Get().ReadTimestamp(q.Index, q.Serial, wait, ns);
    }

    void BeginOcclusion(QueryObject& q)
    {
        q.OcclIndex = Core::Get().BeginOcclusion();
    }

    void EndOcclusion(QueryObject& q)
    {
        auto& core = Core::Get();
        core.EndOcclusion(q.OcclIndex);
        q.OcclSerial = core.SubmitSerial();
    }

    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples)
    {
        return Core::Get().ReadOcclusion(q.OcclIndex, q.OcclSerial, wait, samples);
    }

    bool Install(std::string& error)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
        {
            error = "Direct3D 12 device is not initialized";
            return false;
        }
        c.Vendor = VendorName(core.VendorId());
        c.Renderer = core.DeviceName() + " (Direct3D 12)";
        c.Version = "4.6.0 Conqueror D12GL";
        c.Extensions = { "GL_ARB_clip_control", "GL_ARB_direct_state_access", "GL_ARB_texture_storage", "GL_ARB_texture_filter_anisotropic",
                         "GL_EXT_texture_filter_anisotropic" };
        s_Limits.MaxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        s_Limits.MaxCubeMapSize = D3D12_REQ_TEXTURECUBE_DIMENSION;
        s_Limits.MaxTextureUnits = 32;
        s_Limits.MaxVertexAttribs = 16;
        s_Limits.MaxUniformBufferBindings = DX12::kRootCBVs;
        s_Limits.MaxUniformBlockSize = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        s_Limits.UniformBufferAlignment = 256;
        s_Limits.MaxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        s_Limits.MaxViewport[0] = s_Limits.MaxViewport[1] = D3D12_VIEWPORT_BOUNDS_MAX;
        s_Limits.MaxAnisotropy = (float)D3D12_REQ_MAXANISOTROPY;
        s_Limits.LineWidthRange[0] = s_Limits.LineWidthRange[1] = 1.0f;
        s_Limits.VideoMemoryKB = (int32_t)std::min<uint64_t>(core.VideoMemory() / 1024, 0x7FFFFFFF);

        // Kapalı öznitelikler: GL varsayılanı (0,0,0,1) — float ve tamsayı sürümü
        const float fdef[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const int32_t idef[4] = { 0, 0, 0, 1 };
        uint8_t defaults[32];
        std::memcpy(defaults, fdef, 16);
        std::memcpy(defaults + 16, idef, 16);
        c.NullVertexBuffer = MakeUploadBuffer(defaults, sizeof(defaults));
        c.ZeroConstants = MakeUploadBuffer(nullptr, 65536);
        if (!c.NullVertexBuffer || !c.ZeroConstants)
        {
            error = "Cannot create the default vertex attribute / constant buffers";
            return false;
        }
        auto* dev = core.Device();
        c.NullSRV = core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(nullptr, &sv, c.NullSRV.Handle);
        c.NullRTV = core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).Allocate();
        D3D12_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        dev->CreateRenderTargetView(nullptr, &rv, c.NullRTV.Handle);
        c.NullSampler = core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).Allocate();
        D3D12_SAMPLER_DESC smp{};
        smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        smp.MaxLOD = D3D12_FLOAT32_MAX;
        dev->CreateSampler(&smp, c.NullSampler.Handle);

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
        CQ_CORE_INFO("[D12GL] GL 4.6 translation layer ready on {0} ({1} unimplemented entry points stubbed)", core.DeviceName(), g_StubCount);
        return true;
    }

    void Shutdown()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!core.IsInitialized())
            return;
        core.Flush();
        c.Live = false;
        for (auto& [n, t] : c.Textures) DestroyTextureImage(*t);
        for (auto& [n, t] : c.Renderbuffers) DestroyTextureImage(*t);
        for (auto& t : c.InternalTextures) DestroyTextureImage(*t);
        for (auto& [n, s] : c.SamplerObjects) DestroyTextureImage(*s);
        DestroyTextureImage(c.BackColor);
        DestroyTextureImage(c.BackDepth);
        for (auto& [n, b] : c.Buffers) ReleaseBuffer(*b);
        for (auto& [n, p] : c.Programs) DestroyProgram(*p);
        ReleaseDrawHelpers();
        DeferRelease(c.NullVertexBuffer);
        DeferRelease(c.ZeroConstants);
        c.SamplerCache.clear();
        c.Textures.clear();
        c.Renderbuffers.clear();
        c.InternalTextures.clear();
        c.Buffers.clear();
        c.Programs.clear();
        c.ShaderObjects.clear();
        c.QueryObjects.clear();
        c.NullTexture2D = c.NullTextureCube = c.NullTextureInt = nullptr;
        c.NullSRV = c.NullRTV = c.NullSampler = CpuDescriptor{};
        core.Flush();   // ertelenen bırakmalar
    }
}

#endif
