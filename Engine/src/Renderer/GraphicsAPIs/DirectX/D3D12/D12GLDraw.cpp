#ifdef CQ_D3D12_ENABLED

#include "D12GL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstring>

namespace Conqueror::D12GL
{
    using DX12::Core;

    namespace
    {
        constexpr D3D12_RESOURCE_STATES kShaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        struct Hasher
        {
            uint64_t H = 1469598103934665603ull;
            void Add(uint64_t v) { H = (H ^ v) * 1099511628211ull; }
        };

        // Komut listesine bağlı durum (gereksiz çağrıları önler); liste yeniden açılınca (Epoch) ve yardımcı çizimlerden sonra sıfırlanır
        struct Bound
        {
            uint64_t Epoch = 0;
            ID3D12PipelineState* Pipeline = nullptr;
            D3D12_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            D3D12_GPU_VIRTUAL_ADDRESS CBV[DX12::kRootCBVs] = {};
            UINT64 SRVTable = 0, SamplerTable = 0;
            UINT StencilRef = UINT32_MAX;
            // Son SRV tablosu (aynı küme sonraki çizimde yeniden kullanılır)
            D3D12_CPU_DESCRIPTOR_HANDLE LastSRVs[DX12::kMaxSRVs] = {};
            uint32_t LastSRVCount = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE LastSRVTable{};
        };
        Bound s_Bound;

        void SyncBound()
        {
            const uint64_t epoch = Core::Get().ListEpoch();
            if (s_Bound.Epoch != epoch)
            {
                s_Bound = Bound{};
                s_Bound.Epoch = epoch;
            }
        }

        void InvalidateBound()
        {
            // Tablo önbelleği listeye bağlı değil (tanımlayıcılar yığında); yalnızca bağlı durum sıfırlanır
            const auto srvCount = s_Bound.LastSRVCount;
            D3D12_CPU_DESCRIPTOR_HANDLE srvs[DX12::kMaxSRVs];
            std::memcpy(srvs, s_Bound.LastSRVs, sizeof(srvs));
            const auto table = s_Bound.LastSRVTable;
            const uint64_t epoch = s_Bound.Epoch;
            s_Bound = Bound{};
            s_Bound.Epoch = epoch;
            std::memcpy(s_Bound.LastSRVs, srvs, sizeof(srvs));
            s_Bound.LastSRVCount = srvCount;
            s_Bound.LastSRVTable = table;
        }

        D3D12_BLEND ToBlend(GLenum f, bool alpha)
        {
            switch (f)
            {
                case GL_ZERO: return D3D12_BLEND_ZERO;
                case GL_ONE: return D3D12_BLEND_ONE;
                case GL_SRC_COLOR: return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
                case GL_ONE_MINUS_SRC_COLOR: return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
                case GL_DST_COLOR: return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
                case GL_ONE_MINUS_DST_COLOR: return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
                case GL_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
                case GL_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
                case GL_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
                case GL_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
                case GL_CONSTANT_COLOR: case GL_CONSTANT_ALPHA: return D3D12_BLEND_BLEND_FACTOR;
                case GL_ONE_MINUS_CONSTANT_COLOR: case GL_ONE_MINUS_CONSTANT_ALPHA: return D3D12_BLEND_INV_BLEND_FACTOR;
                case GL_SRC_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
                default: return D3D12_BLEND_ONE;
            }
        }

        D3D12_BLEND_OP ToBlendOp(GLenum e)
        {
            switch (e)
            {
                case GL_FUNC_SUBTRACT: return D3D12_BLEND_OP_SUBTRACT;
                case GL_FUNC_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
                case GL_MIN: return D3D12_BLEND_OP_MIN;
                case GL_MAX: return D3D12_BLEND_OP_MAX;
                default: return D3D12_BLEND_OP_ADD;
            }
        }

        D3D12_COMPARISON_FUNC ToCompare(GLenum f) { return (D3D12_COMPARISON_FUNC)DXCommon::CompareFunc(f); }

        D3D12_STENCIL_OP ToStencilOp(GLenum o)
        {
            switch (o)
            {
                case GL_ZERO: return D3D12_STENCIL_OP_ZERO;
                case GL_REPLACE: return D3D12_STENCIL_OP_REPLACE;
                case GL_INCR: return D3D12_STENCIL_OP_INCR_SAT;
                case GL_DECR: return D3D12_STENCIL_OP_DECR_SAT;
                case GL_INVERT: return D3D12_STENCIL_OP_INVERT;
                case GL_INCR_WRAP: return D3D12_STENCIL_OP_INCR;
                case GL_DECR_WRAP: return D3D12_STENCIL_OP_DECR;
                default: return D3D12_STENCIL_OP_KEEP;
            }
        }

        D3D12_PRIMITIVE_TOPOLOGY ToTopology(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                case GL_LINES: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                case GL_LINE_STRIP: case GL_LINE_LOOP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case GL_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                case GL_LINES: case GL_LINE_STRIP: case GL_LINE_LOOP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            }
        }

        DXGI_FORMAT AttribFormat(const Attrib& a)
        {
            const int n = std::clamp(a.Size, 1, 4);
            switch (a.Type)
            {
                case GL_FLOAT:
                {
                    const DXGI_FORMAT f[] = { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
                    return f[n - 1];
                }
                case GL_HALF_FLOAT:
                    return n == 1 ? DXGI_FORMAT_R16_FLOAT : n == 2 ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
                case GL_INT:
                {
                    const DXGI_FORMAT f[] = { DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT };
                    return f[n - 1];
                }
                case GL_UNSIGNED_INT:
                {
                    const DXGI_FORMAT f[] = { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32A32_UINT };
                    return f[n - 1];
                }
                case GL_UNSIGNED_BYTE:
                    if (a.Integer)
                        return n == 1 ? DXGI_FORMAT_R8_UINT : n == 2 ? DXGI_FORMAT_R8G8_UINT : DXGI_FORMAT_R8G8B8A8_UINT;
                    return n == 1 ? DXGI_FORMAT_R8_UNORM : n == 2 ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
                case GL_SHORT:
                    if (a.Normalized)
                        return n == 1 ? DXGI_FORMAT_R16_SNORM : n == 2 ? DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16B16A16_SNORM;
                    return n == 1 ? DXGI_FORMAT_R16_SINT : n == 2 ? DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_R16G16B16A16_SINT;
                case GL_UNSIGNED_SHORT:
                    if (a.Normalized)
                        return n == 1 ? DXGI_FORMAT_R16_UNORM : n == 2 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
                    return n == 1 ? DXGI_FORMAT_R16_UINT : n == 2 ? DXGI_FORMAT_R16G16_UINT : DXGI_FORMAT_R16G16B16A16_UINT;
                default:
                    return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }
        }

        uint32_t AttribSize(const Attrib& a)
        {
            const uint32_t comp = a.Type == GL_UNSIGNED_BYTE || a.Type == GL_BYTE ? 1 : (a.Type == GL_SHORT || a.Type == GL_UNSIGNED_SHORT || a.Type == GL_HALF_FLOAT) ? 2 : 4;
            return comp * (uint32_t)std::clamp(a.Size, 1, 4);
        }

        struct Targets
        {
            Texture* Color[8] = {};
            int ColorLevel[8] = {}, ColorLayer[8] = {};
            int ColorCount = 0;
            Texture* Depth = nullptr;
            int DepthLevel = 0, DepthLayer = 0;
        };

        Targets CollectTargets(uint32_t fboName)
        {
            auto& c = Ctx();
            Targets t;
            if (fboName == 0)
            {
                t.Color[0] = &c.BackColor;
                t.ColorCount = 1;
                t.Depth = &c.BackDepth;
                return t;
            }
            Framebuffer* fb = GetFramebuffer(fboName);
            if (!fb)
                return t;
            for (int i = 0; i < fb->NumDrawBuffers && i < 8; ++i)
            {
                const GLenum b = fb->DrawBuffers[i];
                if (b >= GL_COLOR_ATTACHMENT0 && b < GL_COLOR_ATTACHMENT0 + 8)
                {
                    const Attachment& a = fb->Color[b - GL_COLOR_ATTACHMENT0];
                    t.Color[i] = AttachmentTexture(a);
                    t.ColorLevel[i] = a.Level;
                    t.ColorLayer[i] = a.Layer;
                }
                t.ColorCount = i + 1;
            }
            while (t.ColorCount > 0 && !(t.Color[t.ColorCount - 1] && t.Color[t.ColorCount - 1]->Image))
                --t.ColorCount;
            const Attachment& da = fb->Depth.Valid() ? fb->Depth : fb->Stencil;
            if (da.Valid())
            {
                t.Depth = AttachmentTexture(da);
                t.DepthLevel = da.Level;
                t.DepthLayer = da.Layer;
            }
            return t;
        }

        // ── Yardımcı çizimler (ölçekli / çevrilmiş kopya, mip üretimi, çoklu örnek derinlik çözümleme, sunum) ──
        const char* kHelperHLSL = R"(
cbuffer Blit : register(b0) { float4 g_UV; float4 g_Pad; };
Texture2D g_Src : register(t0);
Texture2DMS<float4> g_SrcMS : register(t1);
Texture2DMS<float> g_DepthMS : register(t2);
Texture2DArray g_SrcArr : register(t3);
SamplerState g_Smp : register(s0);
struct VSOut { float4 Pos : SV_Position; float2 UV : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    // Hedef viewport'u GL yönünde dolduran üçgen: p.y=0 -> viewport alt satırı (bellek satırı TopLeftY)
    o.Pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.Pos.y = -o.Pos.y;
    o.UV = lerp(g_UV.xy, g_UV.zw, p);
    return o;
}
float4 PSColor(VSOut i) : SV_Target { return g_Src.SampleLevel(g_Smp, i.UV, 0); }
float4 PSColorMS(VSOut i) : SV_Target { return g_SrcMS.Load(int2(i.Pos.xy), 0); }
float PSDepthMS(VSOut i) : SV_Depth { return g_DepthMS.Load(int2(i.Pos.xy), 0); }
float4 PSColorArray(VSOut i) : SV_Target { return g_SrcArr.SampleLevel(g_Smp, float3(i.UV, 0.0), 0); }
float4 PSPresent(VSOut i) : SV_Target { return float4(g_Src.SampleLevel(g_Smp, i.UV, 0).rgb, 1.0); }
)";

        enum class HelperPS : uint32_t { Color = 0, ColorMS, DepthMS, ColorArray, Present, Count };

        struct Helpers
        {
            std::vector<uint8_t> VS;
            std::vector<uint8_t> PS[(size_t)HelperPS::Count];
            std::unordered_map<uint64_t, ID3D12PipelineState*> Pipelines;
            Texture PointParams, LinearParams;
            bool Tried = false, Ok = false;
        };
        Helpers s_Helpers;

        bool EnsureHelpers()
        {
            auto& h = s_Helpers;
            if (h.Tried)
                return h.Ok;
            h.Tried = true;
            std::string err;
            if (!Core::CompileHLSL(kHelperHLSL, "VSMain", "VSMain", "vs_5_0", h.VS, err))
            {
                CQ_CORE_ERROR("[D12GL] Helper shader failed: {0}", err);
                return false;
            }
            const char* entries[] = { "PSColor", "PSColorMS", "PSDepthMS", "PSColorArray", "PSPresent" };
            for (size_t i = 0; i < (size_t)HelperPS::Count; ++i)
                if (!Core::CompileHLSL(kHelperHLSL, entries[i], entries[i], "ps_5_0", h.PS[i], err))
                    CQ_CORE_ERROR("[D12GL] Helper shader {0} failed: {1}", entries[i], err);
            h.PointParams.MinFilter = h.PointParams.MagFilter = GL_NEAREST;
            h.LinearParams.MinFilter = h.LinearParams.MagFilter = GL_LINEAR;
            for (Texture* t : { &h.PointParams, &h.LinearParams })
                t->WrapS = t->WrapT = t->WrapR = GL_CLAMP_TO_EDGE;
            h.Ok = true;
            return true;
        }

        void ReleaseHelpers()
        {
            auto& h = s_Helpers;
            for (auto& [k, p] : h.Pipelines)
                if (p) p->Release();
            h.Pipelines.clear();
            h.VS.clear();
            for (auto& ps : h.PS)
                ps.clear();
            h.Tried = h.Ok = false;
        }

        ID3D12PipelineState* HelperPipeline(HelperPS ps, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32_t samples, bool depthWrite)
        {
            auto& h = s_Helpers;
            const uint64_t key = (uint64_t)ps | ((uint64_t)rtvFormat << 8) | ((uint64_t)dsvFormat << 20) | ((uint64_t)samples << 32) | ((uint64_t)depthWrite << 40);
            if (auto it = h.Pipelines.find(key); it != h.Pipelines.end())
                return it->second;
            const auto& code = h.PS[(size_t)ps];
            ID3D12PipelineState* pso = nullptr;
            if (!code.empty())
            {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
                d.pRootSignature = Core::Get().RootSignature();
                d.VS = { h.VS.data(), h.VS.size() };
                d.PS = { code.data(), code.size() };
                d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                d.SampleMask = UINT32_MAX;
                d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                d.RasterizerState.DepthClipEnable = TRUE;
                d.DepthStencilState.DepthEnable = depthWrite ? TRUE : FALSE;
                d.DepthStencilState.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
                d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                d.NumRenderTargets = rtvFormat != DXGI_FORMAT_UNKNOWN ? 1 : 0;
                d.RTVFormats[0] = rtvFormat;
                d.DSVFormat = dsvFormat;
                d.SampleDesc.Count = samples;
                if (FAILED(Core::Get().Device()->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&pso)))
                {
                    CQ_CORE_ERROR("[D12GL] Helper pipeline creation failed (ps {0}, format {1})", (int)ps, (int)rtvFormat);
                    pso = nullptr;
                }
            }
            h.Pipelines[key] = pso;
            return pso;
        }

        // Yardımcı tam ekran çizim. Çağıran durumları hazırlar (hedef RENDER_TARGET / DEPTH_WRITE, kaynak okunur).
        void HelperDraw(const D3D12_CPU_DESCRIPTOR_HANDLE* rtv, DXGI_FORMAT rtvFormat, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv, DXGI_FORMAT dsvFormat, uint32_t samples,
                        int x0, int y0, int x1, int y1, const float uv[4], HelperPS ps, D3D12_CPU_DESCRIPTOR_HANDLE srv, uint32_t srvSlot, bool linear, bool depthWrite)
        {
            auto& h = s_Helpers;
            auto& c = Ctx();
            auto& core = Core::Get();
            ID3D12PipelineState* pso = HelperPipeline(ps, rtvFormat, dsvFormat, samples, depthWrite);
            if (!pso)
                return;
            EndRendering();
            D3D12_CPU_DESCRIPTOR_HANDLE smp = GetSampler(linear ? h.LinearParams : h.PointParams, false, false);
            const D3D12_GPU_DESCRIPTOR_HANDLE smpTable = core.SamplerTable(&smp, 1, 0x48454C50ull ^ (uint64_t)smp.ptr);
            D3D12_CPU_DESCRIPTOR_HANDLE srvs[4] = { c.NullSRV.Handle, c.NullSRV.Handle, c.NullSRV.Handle, c.NullSRV.Handle };
            srvs[std::min(srvSlot, 3u)] = srv;
            const D3D12_GPU_DESCRIPTOR_HANDLE srvTable = core.CopyResourceTable(srvs, 4);
            DX12::UploadAlloc cb = core.AllocUpload(256, 256);
            if (!cb.Cpu)
                return;
            std::memcpy(cb.Cpu, uv, sizeof(float) * 4);
            auto* list = core.List();
            list->SetPipelineState(pso);
            list->SetGraphicsRootConstantBufferView(0, cb.Gpu);
            list->SetGraphicsRootDescriptorTable(DX12::kRootSRVTable, srvTable);
            list->SetGraphicsRootDescriptorTable(DX12::kRootSamplerTable, smpTable);
            list->OMSetRenderTargets(rtv ? 1 : 0, rtv, FALSE, dsv);
            D3D12_VIEWPORT vp{ (float)std::min(x0, x1), (float)std::min(y0, y1), (float)std::abs(x1 - x0), (float)std::abs(y1 - y0), 0.0f, 1.0f };
            list->RSSetViewports(1, &vp);
            D3D12_RECT sc{ 0, 0, 16384, 16384 };
            list->RSSetScissorRects(1, &sc);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
            InvalidateBound();
        }

        uint64_t PipelineKey(const Hasher& layout, uint64_t program, uint64_t raster, uint64_t depth, uint64_t blend, uint64_t extra)
        {
            Hasher h;
            h.Add(layout.H);
            h.Add(program);
            h.Add(raster);
            h.Add(depth);
            h.Add(blend);
            h.Add(extra);
            return h.H;
        }
    }

    VertexArray* CurrentVAO()
    {
        auto& c = Ctx();
        if (c.S.VAO)
            if (auto it = c.VertexArrays.find(c.S.VAO); it != c.VertexArrays.end())
                return it->second.get();
        return &c.DefaultVAO;
    }

    void EndRendering()
    {
        auto& c = Ctx();
        c.Rendering = false;
        c.RenderingKey = 0;
        c.RenderingFBO = UINT32_MAX;
        for (auto& t : c.RenderColor)
            t = nullptr;
        c.RenderDepth = nullptr;
    }

    void FlushAndWaitGL()
    {
        if (Core::Get().IsInitialized())
        {
            EndRendering();
            Core::Get().Flush();
        }
    }

    bool EnsureRendering()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        Targets t = CollectTargets(c.S.DrawFBO);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8] = {};
        Hasher h;
        h.Add(c.S.DrawFBO);
        uint32_t width = UINT32_MAX, height = UINT32_MAX, samples = 1;
        for (int i = 0; i < t.ColorCount; ++i)
        {
            Texture* tex = t.Color[i];
            if (tex && tex->Image && tex->Renderable)
            {
                rtvs[i] = GetRTV(*tex, t.ColorLevel[i], t.ColorLayer[i]);
                width = std::min(width, std::max(1u, tex->Width >> t.ColorLevel[i]));
                height = std::min(height, std::max(1u, tex->Height >> t.ColorLevel[i]));
                samples = tex->Samples;
                Transition(*tex, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            else
            {
                rtvs[i] = c.NullRTV.Handle;
                t.Color[i] = nullptr;
            }
            h.Add((uint64_t)rtvs[i].ptr);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        if (t.Depth && t.Depth->Image && t.Depth->Depth)
        {
            dsv = GetDSV(*t.Depth, t.DepthLevel, t.DepthLayer);
            width = std::min(width, std::max(1u, t.Depth->Width >> t.DepthLevel));
            height = std::min(height, std::max(1u, t.Depth->Height >> t.DepthLevel));
            samples = t.Depth->Samples;
            Transition(*t.Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        else
            t.Depth = nullptr;
        h.Add((uint64_t)dsv.ptr);
        if (width == UINT32_MAX)
            return false;
        if (c.Rendering && c.RenderingKey == h.H && c.RenderingEpoch == core.ListEpoch())
            return true;
        core.List()->OMSetRenderTargets((UINT)t.ColorCount, t.ColorCount ? rtvs : nullptr, FALSE, dsv.ptr ? &dsv : nullptr);
        for (int i = 0; i < 8; ++i)
        {
            const bool live = i < t.ColorCount && t.Color[i];
            c.RenderColor[i] = live ? t.Color[i] : nullptr;
            c.RenderColorFormats[i] = i < t.ColorCount ? (live ? t.Color[i]->RTVFormat : DXGI_FORMAT_R8G8B8A8_UNORM) : DXGI_FORMAT_UNKNOWN;
            c.RenderColorInteger[i] = live && t.Color[i]->Integer;
            c.BoundRTVs[i] = rtvs[i];
        }
        c.BoundRTVCount = (uint32_t)t.ColorCount;
        c.RenderDepth = t.Depth;
        c.BoundDSV = dsv;
        c.RenderingDepthFormat = t.Depth ? t.Depth->DSVFormat : DXGI_FORMAT_UNKNOWN;
        c.RenderingHasDepth = t.Depth != nullptr;
        c.RenderingHasStencil = t.Depth && t.Depth->Stencil;
        c.RenderingSamples = samples;
        c.RenderWidth = width;
        c.RenderHeight = height;
        c.Rendering = true;
        c.RenderingFBO = c.S.DrawFBO;
        c.RenderingKey = h.H;
        c.RenderingEpoch = core.ListEpoch();
        return true;
    }

    void ClearCurrent(GLbitfield mask)
    {
        auto& c = Ctx();
        if (!EnsureRendering())
            return;
        auto* list = Core::Get().List();
        const bool scissor = c.S.ScissorTest;
        D3D12_RECT rect{ c.S.Scissor[0], c.S.Scissor[1], c.S.Scissor[0] + c.S.Scissor[2], c.S.Scissor[1] + c.S.Scissor[3] };
        if (scissor && (rect.right <= rect.left || rect.bottom <= rect.top))
            return;
        if (mask & GL_COLOR_BUFFER_BIT)
            for (uint32_t i = 0; i < c.BoundRTVCount; ++i)
                if (c.RenderColor[i])
                    list->ClearRenderTargetView(c.BoundRTVs[i], c.S.ClearColor, scissor ? 1 : 0, scissor ? &rect : nullptr);
        D3D12_CLEAR_FLAGS flags = (D3D12_CLEAR_FLAGS)0;
        if ((mask & GL_DEPTH_BUFFER_BIT) && c.RenderingHasDepth && c.S.DepthMask)
            flags |= D3D12_CLEAR_FLAG_DEPTH;
        if ((mask & GL_STENCIL_BUFFER_BIT) && c.RenderingHasStencil)
            flags |= D3D12_CLEAR_FLAG_STENCIL;
        if (flags && c.BoundDSV.ptr)
            list->ClearDepthStencilView(c.BoundDSV, flags, std::clamp(c.S.ClearDepth, 0.0f, 1.0f), (UINT8)c.S.ClearStencil, scissor ? 1 : 0, scissor ? &rect : nullptr);
    }

    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (count <= 0 || instances <= 0)
            return;
        Program* p = GetProgram(c.S.Program);
        if (!p || !p->Linked)
            return;
        // GL_FRONT_AND_BACK ayıklama: hiçbir üçgen çizilmez
        if (c.S.CullFace && c.S.CullMode == GL_FRONT_AND_BACK && ToTopologyType(mode) == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            return;
        if (!EnsureRendering())
            return;
        VertexArray* vao = CurrentVAO();
        const uint64_t frame = core.FrameSerial();

        // ── Giriş düzeni + köşe arabellekleri (öznitelik başına bir yuva) ──
        D3D12_VERTEX_BUFFER_VIEW views[16] = {};
        D3D12_INPUT_ELEMENT_DESC elems[16] = {};
        UINT n = 0;
        Hasher layoutKey;
        const D3D12_GPU_VIRTUAL_ADDRESS nullVB = c.NullVertexBuffer->GetGPUVirtualAddress();
        for (const auto& in : p->Inputs)
        {
            if (in.Location < 0 || in.Location >= 16 || n >= 16)
                continue;
            const Attrib& a = vao->Attribs[in.Location];
            Buffer* buf = a.Enabled ? GetBuffer(a.Buffer) : nullptr;
            D3D12_INPUT_ELEMENT_DESC& e = elems[n];
            e.SemanticName = "TEXCOORD";
            e.SemanticIndex = (UINT)in.Location;
            e.InputSlot = n;
            e.AlignedByteOffset = 0;
            uint64_t size = 0;
            const D3D12_GPU_VIRTUAL_ADDRESS addr = buf ? BufferAddress(*buf, 0, &size) : 0;
            if (addr)
            {
                e.Format = AttribFormat(a);
                e.InputSlotClass = a.Divisor ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                e.InstanceDataStepRate = a.Divisor;
                const uint64_t offset = std::min<uint64_t>(a.Offset, size);
                views[n].BufferLocation = addr + offset;
                views[n].SizeInBytes = (UINT)(size - offset);
                views[n].StrideInBytes = a.Stride ? (UINT)a.Stride : AttribSize(a);
            }
            else
            {
                // GL varsayılan özniteliği (0,0,0,1): sıfır adımlı sabit arabellek
                e.Format = in.Integer ? DXGI_FORMAT_R32G32B32A32_SINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
                e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                views[n].BufferLocation = nullVB + (in.Integer ? 16 : 0);
                views[n].SizeInBytes = 16;
                views[n].StrideInBytes = 0;
            }
            layoutKey.Add(((uint64_t)in.Location << 40) | ((uint64_t)e.Format << 8) | (uint64_t)e.InputSlotClass);
            layoutKey.Add(e.InstanceDataStepRate);
            ++n;
        }

        // ── İndeksler ──
        D3D12_INDEX_BUFFER_VIEW ibv{};
        UINT startIndex = 0;
        if (indexed)
        {
            Buffer* eb = GetBuffer(vao->ElementBuffer);
            if (!eb)
                return;
            if (indexType == GL_UNSIGNED_BYTE)
            {
                // 8 bit indeks D3D12'de yok: 16 bit'e genişletilmiş halka kopyası
                const size_t start = (size_t)(uintptr_t)indices;
                if (start + (size_t)count > eb->Shadow.size())
                    return;
                DX12::UploadAlloc a = core.AllocUpload((uint64_t)count * 2, 4);
                if (!a.Cpu)
                    return;
                auto* wide = reinterpret_cast<uint16_t*>(a.Cpu);
                for (GLsizei i = 0; i < count; ++i)
                    wide[i] = eb->Shadow[start + (size_t)i];
                ibv = { a.Gpu, (UINT)(count * 2), DXGI_FORMAT_R16_UINT };
            }
            else
            {
                uint64_t size = 0;
                const D3D12_GPU_VIRTUAL_ADDRESS addr = BufferAddress(*eb, 0, &size);
                if (!addr)
                    return;
                const UINT indexSize = indexType == GL_UNSIGNED_SHORT ? 2 : 4;
                const uint64_t offset = (uint64_t)(uintptr_t)indices;
                ibv = { addr, (UINT)size, indexType == GL_UNSIGNED_SHORT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
                startIndex = (UINT)(offset / indexSize);
            }
        }

        // ── Sabit arabellekler (kök CBV) ──
        D3D12_GPU_VIRTUAL_ADDRESS cbvs[DX12::kRootCBVs];
        const D3D12_GPU_VIRTUAL_ADDRESS zero = c.ZeroConstants->GetGPUVirtualAddress();
        for (auto& a : cbvs)
            a = zero;
        for (const auto& blk : p->Blocks)
        {
            if (blk.Register >= DX12::kRootCBVs)
                continue;
            const uint32_t glb = blk.GLBinding < 64 ? blk.GLBinding : 0;
            Buffer* ub = GetBuffer(c.S.UBOBindings[glb]);
            const D3D12_GPU_VIRTUAL_ADDRESS addr = ub ? BufferAddress(*ub, blk.Size) : 0;
            cbvs[blk.Register] = addr ? addr : zero;
        }
        if (p->GlobalsRegister >= 0 && p->GlobalsRegister < (int)DX12::kRootCBVs)
        {
            if (p->GlobalsDirty || p->GlobalsFrame != frame || !p->GlobalsAddress)
            {
                DX12::UploadAlloc a = core.AllocUpload(std::max<uint64_t>(p->Globals.size(), 256), 256);
                if (a.Cpu)
                {
                    std::memcpy(a.Cpu, p->Globals.data(), p->Globals.size());
                    p->GlobalsAddress = a.Gpu;
                    p->GlobalsFrame = frame;
                    p->GlobalsDirty = false;
                }
            }
            if (p->GlobalsAddress)
                cbvs[p->GlobalsRegister] = p->GlobalsAddress;
        }

        // ── Dokular + örnekleyiciler ──
        Texture* srvTex[DX12::kMaxSRVs] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE samplers[DX12::kMaxSamplers];
        for (auto& s : samplers)
            s = c.NullSampler.Handle;
        uint32_t srvCount = 0, smpCount = 0;
        auto boundAsTarget = [&](const Texture* t) {
            if (t == c.RenderDepth)
                return true;
            for (uint32_t i = 0; i < c.BoundRTVCount; ++i)
                if (c.RenderColor[i] == t)
                    return true;
            return false;
        };
        for (const auto& s : p->Samplers)
        {
            const Texture* shared = nullptr;
            for (uint32_t e = 0; e < s.Count; ++e)
            {
                const int unit = std::clamp(e < s.Units.size() ? s.Units[e] : 0, 0, 63);
                const uint32_t texName = s.Target == GL_TEXTURE_CUBE_MAP ? c.S.TexCube[(size_t)unit] :
                                         s.Target == GL_TEXTURE_2D_MULTISAMPLE ? c.S.Tex2DMS[(size_t)unit] : c.S.Tex2D[(size_t)unit];
                Texture* tex = GetTexture(texName);
                // Bağlı hedef aynı anda örneklenemez (D3D11 bu durumda SRV'yi boşaltır; aynı davranış)
                if (!tex || !tex->Image || (s.Target == GL_TEXTURE_CUBE_MAP) != (tex->Layers == 6) || tex->Integer != s.Integer || boundAsTarget(tex))
                    tex = s.Target == GL_TEXTURE_CUBE_MAP ? c.NullTextureCube : s.Integer ? c.NullTextureInt : c.NullTexture2D;
                const Texture* params = tex;
                if (auto so = c.SamplerObjects.find(c.S.Samplers[(size_t)unit]); so != c.SamplerObjects.end() && c.S.Samplers[(size_t)unit])
                    params = so->second.get();
                const uint32_t slot = s.Register + e;
                if (slot < DX12::kMaxSRVs)
                {
                    srvTex[slot] = tex;
                    srvCount = std::max(srvCount, slot + 1);
                }
                if (s.SharedSampler)
                {
                    if (!shared && tex != c.NullTexture2D)
                        shared = params;
                }
                else if (slot < DX12::kMaxSamplers)
                {
                    samplers[slot] = GetSampler(*params, tex->Integer, s.Shadow);
                    smpCount = std::max(smpCount, slot + 1);
                }
            }
            if (s.SharedSampler && s.SamplerRegister < DX12::kMaxSamplers)
            {
                samplers[s.SamplerRegister] = GetSampler(shared ? *shared : *c.NullTexture2D, s.Integer, s.Shadow);
                smpCount = std::max(smpCount, s.SamplerRegister + 1);
            }
        }
        // Örnekleyici tablosu önce (dolarsa GPU beklenir ve tanımlayıcı bölümü baştan başlar)
        D3D12_GPU_DESCRIPTOR_HANDLE smpTable{};
        if (smpCount)
        {
            Hasher k;
            for (uint32_t i = 0; i < smpCount; ++i)
                k.Add(samplers[i].ptr);
            smpTable = core.SamplerTable(samplers, smpCount, k.H);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE srvTable{};
        if (srvCount)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE srvs[DX12::kMaxSRVs];
            for (uint32_t i = 0; i < srvCount; ++i)
            {
                if (srvTex[i])
                {
                    Transition(*srvTex[i], kShaderRead);
                    srvs[i] = GetSRV(*srvTex[i]);
                }
                else
                    srvs[i] = c.NullSRV.Handle;
            }
            SyncBound();
            if (s_Bound.LastSRVCount == srvCount && s_Bound.LastSRVTable.ptr && std::memcmp(s_Bound.LastSRVs, srvs, sizeof(srvs[0]) * srvCount) == 0)
                srvTable = s_Bound.LastSRVTable;
            else
            {
                srvTable = core.CopyResourceTable(srvs, srvCount);
                SyncBound();
                std::memcpy(s_Bound.LastSRVs, srvs, sizeof(srvs[0]) * srvCount);
                s_Bound.LastSRVCount = srvCount;
                s_Bound.LastSRVTable = srvTable;
            }
        }
        // Tablo ayırma GPU'yu bekletmiş olabilir (yeni komut listesi): hedefler yeniden bağlanır
        if (c.RenderingEpoch != core.ListEpoch() && !EnsureRendering())
            return;
        SyncBound();

        // ── Boru hattı durumu ──
        const D3D12_PRIMITIVE_TOPOLOGY topology = ToTopology(mode);
        const D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = ToTopologyType(mode);
        const bool strip = mode == GL_TRIANGLE_STRIP || mode == GL_LINE_STRIP;
        const D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut = indexed && strip ? (ibv.Format == DXGI_FORMAT_R16_UINT ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF
                                                                                                           : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF)
                                                                   : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        const int32_t units = (int32_t)c.S.PolygonOffsetUnits;
        const float slope = c.S.PolygonOffsetFactor;
        uint32_t slopeBits;
        std::memcpy(&slopeBits, &slope, 4);
        Hasher rk;
        rk.Add(c.S.PolygonMode);
        rk.Add(c.S.CullFace ? c.S.CullMode : 0);
        rk.Add(c.S.FrontFace);
        rk.Add(c.S.PolygonOffsetFill ? 1 : 0);
        if (c.S.PolygonOffsetFill)
        {
            rk.Add((uint64_t)(uint32_t)units);
            rk.Add(slopeBits);
        }
        const bool depthTest = c.S.DepthTest && c.RenderingHasDepth;
        const bool stencilTest = c.S.StencilTest && c.RenderingHasStencil;
        Hasher dk;
        dk.Add(depthTest ? 1 : 0);
        dk.Add(depthTest && c.S.DepthMask ? 1 : 0);
        dk.Add(c.S.DepthFunc);
        dk.Add(stencilTest ? 1 : 0);
        if (stencilTest)
        {
            dk.Add(c.S.StencilFunc);
            dk.Add(c.S.StencilValueMask & 0xFF);
            dk.Add(c.S.StencilWriteMask & 0xFF);
            dk.Add(c.S.StencilFail);
            dk.Add(c.S.StencilZFail);
            dk.Add(c.S.StencilZPass);
        }
        Hasher bk;
        bk.Add(c.S.Blend ? 1 : 0);
        if (c.S.Blend)
        {
            bk.Add(c.S.BlendSrcRGB); bk.Add(c.S.BlendDstRGB); bk.Add(c.S.BlendSrcA); bk.Add(c.S.BlendDstA);
            bk.Add(c.S.BlendEqRGB); bk.Add(c.S.BlendEqA);
        }
        for (uint8_t m : c.S.ColorMaskBits) bk.Add(m);
        Hasher xk;
        xk.Add(topologyType);
        xk.Add(cut);
        xk.Add(c.BoundRTVCount);
        for (uint32_t i = 0; i < c.BoundRTVCount; ++i)
        {
            xk.Add(c.RenderColorFormats[i]);
            xk.Add(c.RenderColorInteger[i] ? 1 : 0);
        }
        xk.Add(c.RenderingDepthFormat);
        xk.Add(c.RenderingSamples);
        const uint64_t psoKey = PipelineKey(layoutKey, p->Id, rk.H, dk.H, bk.H, xk.H);
        ID3D12PipelineState*& pso = c.Pipelines[psoKey];
        if (!pso)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
            d.pRootSignature = core.RootSignature();
            d.VS = { p->VS.data(), p->VS.size() };
            d.PS = { p->PS.data(), p->PS.size() };
            d.BlendState.IndependentBlendEnable = TRUE;
            for (uint32_t i = 0; i < 8; ++i)
            {
                auto& rt = d.BlendState.RenderTarget[i];
                const bool integer = c.RenderColorInteger[i];
                rt.BlendEnable = c.S.Blend && !integer ? TRUE : FALSE;
                rt.SrcBlend = ToBlend(c.S.BlendSrcRGB, false);
                rt.DestBlend = ToBlend(c.S.BlendDstRGB, false);
                rt.BlendOp = ToBlendOp(c.S.BlendEqRGB);
                rt.SrcBlendAlpha = ToBlend(c.S.BlendSrcA, true);
                rt.DestBlendAlpha = ToBlend(c.S.BlendDstA, true);
                rt.BlendOpAlpha = ToBlendOp(c.S.BlendEqA);
                rt.LogicOp = D3D12_LOGIC_OP_NOOP;
                rt.RenderTargetWriteMask = c.S.ColorMaskBits[i];   // R G B A bitleri D3D ile aynı
                // Gölgelendiricinin yazmadığı hedef korunur
                if (!(p->ColorOutputMask & (1u << i)))
                    rt.RenderTargetWriteMask = 0;
            }
            d.SampleMask = UINT32_MAX;
            auto& rs = d.RasterizerState;
            rs.FillMode = c.S.PolygonMode == GL_LINE ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            rs.CullMode = !c.S.CullFace ? D3D12_CULL_MODE_NONE : c.S.CullMode == GL_FRONT ? D3D12_CULL_MODE_FRONT :
                          c.S.CullMode == GL_FRONT_AND_BACK ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            // Y çevirmesi sarımı ters çevirir (bkz. D12GL.h)
            rs.FrontCounterClockwise = c.S.FrontFace == GL_CW ? TRUE : FALSE;
            if (c.S.PolygonOffsetFill)
            {
                rs.DepthBias = units;
                rs.SlopeScaledDepthBias = slope;
            }
            rs.DepthClipEnable = TRUE;
            rs.MultisampleEnable = c.RenderingSamples > 1 ? TRUE : FALSE;
            auto& ds = d.DepthStencilState;
            ds.DepthEnable = depthTest ? TRUE : FALSE;
            ds.DepthWriteMask = depthTest && c.S.DepthMask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = ToCompare(c.S.DepthFunc);
            ds.StencilEnable = stencilTest ? TRUE : FALSE;
            ds.StencilReadMask = (UINT8)(c.S.StencilValueMask & 0xFF);
            ds.StencilWriteMask = (UINT8)(c.S.StencilWriteMask & 0xFF);
            ds.FrontFace = { ToStencilOp(c.S.StencilFail), ToStencilOp(c.S.StencilZFail), ToStencilOp(c.S.StencilZPass), ToCompare(c.S.StencilFunc) };
            ds.BackFace = ds.FrontFace;
            d.InputLayout = { n ? elems : nullptr, n };
            d.IBStripCutValue = cut;
            d.PrimitiveTopologyType = topologyType;
            d.NumRenderTargets = c.BoundRTVCount;
            for (uint32_t i = 0; i < c.BoundRTVCount; ++i)
                d.RTVFormats[i] = c.RenderColorFormats[i];
            d.DSVFormat = c.RenderingDepthFormat;
            d.SampleDesc.Count = c.RenderingSamples;
            if (FAILED(core.Device()->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&pso)))
            {
                CQ_CORE_ERROR("[D12GL] Pipeline creation failed for program {0}", p->Id);
                pso = nullptr;
            }
        }
        if (!pso)
            return;

        // ── Bağlama ──
        auto* list = core.List();
        if (s_Bound.Pipeline != pso)
        {
            list->SetPipelineState(pso);
            s_Bound.Pipeline = pso;
        }
        if (s_Bound.Topology != topology)
        {
            list->IASetPrimitiveTopology(topology);
            s_Bound.Topology = topology;
        }
        for (uint32_t i = 0; i < DX12::kRootCBVs; ++i)
            if (s_Bound.CBV[i] != cbvs[i])
            {
                list->SetGraphicsRootConstantBufferView(i, cbvs[i]);
                s_Bound.CBV[i] = cbvs[i];
            }
        if (srvCount && s_Bound.SRVTable != srvTable.ptr)
        {
            list->SetGraphicsRootDescriptorTable(DX12::kRootSRVTable, srvTable);
            s_Bound.SRVTable = srvTable.ptr;
        }
        if (smpCount && s_Bound.SamplerTable != smpTable.ptr)
        {
            list->SetGraphicsRootDescriptorTable(DX12::kRootSamplerTable, smpTable);
            s_Bound.SamplerTable = smpTable.ptr;
        }
        if (n)
            list->IASetVertexBuffers(0, n, views);
        const UINT ref = (UINT)(c.S.StencilRef & 0xFF);
        if (s_Bound.StencilRef != ref)
        {
            list->OMSetStencilRef(ref);
            s_Bound.StencilRef = ref;
        }

        // ── Görünüm alanı / makas (bellek satırları GL satırlarıyla aynı) ──
        D3D12_VIEWPORT vp{ (float)c.S.Viewport[0], (float)c.S.Viewport[1], (float)std::max(1, c.S.Viewport[2]), (float)std::max(1, c.S.Viewport[3]), 0.0f, 1.0f };
        list->RSSetViewports(1, &vp);
        D3D12_RECT r{ 0, 0, (LONG)c.RenderWidth, (LONG)c.RenderHeight };
        if (c.S.ScissorTest)
        {
            r = { c.S.Scissor[0], c.S.Scissor[1], c.S.Scissor[0] + std::max(0, c.S.Scissor[2]), c.S.Scissor[1] + std::max(0, c.S.Scissor[3]) };
            if (r.right <= r.left || r.bottom <= r.top)
                return;
        }
        list->RSSetScissorRects(1, &r);

        if (indexed)
        {
            list->IASetIndexBuffer(&ibv);
            list->DrawIndexedInstanced((UINT)count, (UINT)instances, startIndex, baseVertex, 0);
        }
        else
            list->DrawInstanced((UINT)count, (UINT)instances, (UINT)first, 0);
    }

    void GenerateMipmaps(Texture& t)
    {
        if (!t.Image || t.Samples > 1)
            return;
        const uint32_t full = DXCommon::MaxLevels(t.Width, t.Height);
        const uint32_t wanted = std::min<uint32_t>(full, (uint32_t)std::max(1, t.MaxLevel + 1));
        if (!t.Immutable && t.Levels < wanted)
            GrowLevels(t, wanted);
        if (t.Levels < 2 || t.Depth || !t.Renderable || !EnsureHelpers())
            return;
        auto& core = Core::Get();
        auto& pool = core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        EndRendering();
        const bool layered = t.Layers > 1;
        Transition(t, kShaderRead);
        const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
        {
            for (uint32_t l = 1; l < t.Levels; ++l)
            {
                const UINT sub = l + layer * t.Levels;
                CpuDescriptor src = CreateSRV(t, (int)l - 1, 1, layered ? (int)layer : -1);
                if (!src.Valid())
                    continue;
                core.Barrier(t.Image, kShaderRead, D3D12_RESOURCE_STATE_RENDER_TARGET, sub);
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetRTV(t, (int)l, (int)layer);
                HelperDraw(&rtv, t.RTVFormat, nullptr, DXGI_FORMAT_UNKNOWN, 1, 0, 0, (int)std::max(1u, t.Width >> l), (int)std::max(1u, t.Height >> l), uv,
                           layered ? HelperPS::ColorArray : HelperPS::Color, src.Handle, layered ? 3u : 0u, !t.Integer, false);
                core.Barrier(t.Image, D3D12_RESOURCE_STATE_RENDER_TARGET, kShaderRead, sub);
                pool.Free(src);   // tabloya kopyalandı
            }
        }
        EndRendering();
        t.DefinedLevels = t.Levels >= 32 ? ~0u : ((1u << t.Levels) - 1);
    }

    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        auto* list = core.List();
        Framebuffer* rf = GetFramebuffer(c.S.ReadFBO);
        Framebuffer* df = GetFramebuffer(c.S.DrawFBO);
        if (!rf || !df)
            return;
        EndRendering();
        const bool sameSize = std::abs(sx1 - sx0) == std::abs(dx1 - dx0) && std::abs(sy1 - sy0) == std::abs(dy1 - dy0);
        const bool noFlip = (sx1 > sx0) == (dx1 > dx0) && (sy1 > sy0) == (dy1 > dy0);

        auto colorBlit = [&](Texture* src, int sLevel, int sLayer, Texture* dst, int dLevel, int dLayer) {
            if (!src || !dst || !src->Image || !dst->Image || src == dst)
                return;
            const int sw = (int)std::max(1u, src->Width >> sLevel), sh = (int)std::max(1u, src->Height >> sLevel);
            const UINT dstSub = (UINT)dLevel + (UINT)dLayer * dst->Levels;
            const UINT srcSub = (UINT)sLevel + (UINT)sLayer * src->Levels;
            const int x0 = std::min(sx0, sx1), y0 = std::min(sy0, sy1), x1 = std::max(sx0, sx1), y1 = std::max(sy0, sy1);
            if (sameSize && noFlip && src->Samples > 1 && dst->Samples == 1 && src->RTVFormat == dst->RTVFormat && x0 == 0 && y0 == 0 && x1 == sw && y1 == sh &&
                (int)std::max(1u, dst->Width >> dLevel) == sw && (int)std::max(1u, dst->Height >> dLevel) == sh)
            {
                Transition(*src, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                Transition(*dst, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                list->ResolveSubresource(dst->Image, dstSub, src->Image, srcSub, src->RTVFormat);
                return;
            }
            if (sameSize && noFlip && src->Samples == dst->Samples && src->Samples == 1 && src->Format == dst->Format)
            {
                const int cx0 = std::clamp(x0, 0, sw), cy0 = std::clamp(y0, 0, sh), cx1 = std::clamp(x1, 0, sw), cy1 = std::clamp(y1, 0, sh);
                if (cx1 <= cx0 || cy1 <= cy0)
                    return;
                Transition(*src, D3D12_RESOURCE_STATE_COPY_SOURCE);
                Transition(*dst, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION d{};
                d.pResource = dst->Image;
                d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                d.SubresourceIndex = dstSub;
                D3D12_TEXTURE_COPY_LOCATION s{};
                s.pResource = src->Image;
                s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                s.SubresourceIndex = srcSub;
                D3D12_BOX box{ (UINT)cx0, (UINT)cy0, 0, (UINT)cx1, (UINT)cy1, 1 };
                list->CopyTextureRegion(&d, (UINT)(std::min(dx0, dx1) + (cx0 - x0)), (UINT)(std::min(dy0, dy1) + (cy0 - y0)), 0, &s, &box);
                return;
            }
            // Ölçekli / çevrilmiş / çözümlemeli kopya: yardımcı çizim
            if (!EnsureHelpers() || !dst->Renderable)
                return;
            float uv[4] = { (float)sx0 / (float)sw, (float)sy0 / (float)sh, (float)sx1 / (float)sw, (float)sy1 / (float)sh };
            // Hedef dikdörtgen yönü UV ile birleştirilir (dx1 < dx0 ise UV ters)
            if (dx1 < dx0) std::swap(uv[0], uv[2]);
            if (dy1 < dy0) std::swap(uv[1], uv[3]);
            Transition(*src, kShaderRead);
            Transition(*dst, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetRTV(*dst, dLevel, dLayer);
            if (src->Samples > 1)
                HelperDraw(&rtv, dst->RTVFormat, nullptr, DXGI_FORMAT_UNKNOWN, dst->Samples, dx0, dy0, dx1, dy1, uv, HelperPS::ColorMS, GetSRV(*src), 1, false, false);
            else
                HelperDraw(&rtv, dst->RTVFormat, nullptr, DXGI_FORMAT_UNKNOWN, dst->Samples, dx0, dy0, dx1, dy1, uv, HelperPS::Color, GetSRV(*src), 0, filter == GL_LINEAR,
                           false);
        };

        if (mask & GL_COLOR_BUFFER_BIT)
        {
            Texture* src = nullptr;
            int sLevel = 0, sLayer = 0;
            if (c.S.ReadFBO == 0)
                src = &c.BackColor;
            else if (rf->ReadBuffer >= GL_COLOR_ATTACHMENT0 && rf->ReadBuffer < GL_COLOR_ATTACHMENT0 + 8)
            {
                const Attachment& a = rf->Color[rf->ReadBuffer - GL_COLOR_ATTACHMENT0];
                src = AttachmentTexture(a);
                sLevel = a.Level;
                sLayer = a.Layer;
            }
            if (c.S.DrawFBO == 0)
                colorBlit(src, sLevel, sLayer, &c.BackColor, 0, 0);
            else
                for (int i = 0; i < df->NumDrawBuffers && i < 8; ++i)
                {
                    const GLenum b = df->DrawBuffers[i];
                    if (b < GL_COLOR_ATTACHMENT0 || b >= GL_COLOR_ATTACHMENT0 + 8)
                        continue;
                    const Attachment& a = df->Color[b - GL_COLOR_ATTACHMENT0];
                    colorBlit(src, sLevel, sLayer, AttachmentTexture(a), a.Level, a.Layer);
                }
        }
        if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))
        {
            Texture* src = c.S.ReadFBO == 0 ? &c.BackDepth : AttachmentTexture(rf->Depth.Valid() ? rf->Depth : rf->Stencil);
            Texture* dst = c.S.DrawFBO == 0 ? &c.BackDepth : AttachmentTexture(df->Depth.Valid() ? df->Depth : df->Stencil);
            if (src && dst && src->Image && dst->Image && src != dst && src->Depth && dst->Depth)
            {
                const int sw = (int)src->Width, sh = (int)src->Height;
                const bool full = std::min(sx0, sx1) == 0 && std::min(sy0, sy1) == 0 && std::max(sx0, sx1) == sw && std::max(sy0, sy1) == sh &&
                                  (int)dst->Width == sw && (int)dst->Height == sh;
                if (full && sameSize && noFlip && src->Samples == dst->Samples && src->Format == dst->Format && src->Levels == dst->Levels)
                {
                    // Derinlik kaynakları yalnızca bütün olarak kopyalanır
                    Transition(*src, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Transition(*dst, D3D12_RESOURCE_STATE_COPY_DEST);
                    list->CopyResource(dst->Image, src->Image);
                }
                else if (src->Samples > 1 && dst->Samples == 1 && sameSize && noFlip && EnsureHelpers())
                {
                    // Çoklu örnek derinlik çözümleme: örnek 0, SV_Depth ile yazılır (şablon çözümlenmez)
                    const float uv[4] = { 0, 0, 1, 1 };
                    Transition(*src, kShaderRead);
                    Transition(*dst, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetDSV(*dst, 0, 0);
                    HelperDraw(nullptr, DXGI_FORMAT_UNKNOWN, &dsv, dst->DSVFormat, 1, dx0, dy0, dx1, dy1, uv, HelperPS::DepthMS, GetSRV(*src), 2, false, true);
                }
                else
                    CQ_CORE_WARN("[D12GL] Unsupported depth blit (scaled or format change)");
            }
        }
        EndRendering();
    }

    void BeginFrameResources()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        const uint32_t w = core.FramebufferWidth(), h = core.FramebufferHeight();
        if (c.BackColor.Width != w || c.BackColor.Height != h || !c.BackColor.Image)
        {
            AllocateTexture(c.BackColor, GL_TEXTURE_2D, GL_RGBA8, w, h, 1, 1);
            c.BackColor.DefinedLevels = 1;
            AllocateTexture(c.BackDepth, GL_TEXTURE_2D, GL_DEPTH24_STENCIL8, w, h, 1, 1);
            c.BackDepth.DefinedLevels = 1;
        }
    }

    void SwapBuffers()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        EndRendering();
        if (EnsureHelpers())
        {
            Transition(c.BackColor, kShaderRead);
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            if (core.BeginBackbuffer(rtv))
            {
                // GL yönü (satır 0 = alt) -> ekran (satır 0 = üst): UV dikey ters
                const float uv[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
                HelperDraw(&rtv, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, DXGI_FORMAT_UNKNOWN, 1, 0, 0, (int)core.FramebufferWidth(), (int)core.FramebufferHeight(), uv,
                           HelperPS::Present, GetSRV(c.BackColor), 0, true, false);
            }
        }
        core.Present();
        EndRendering();
        InvalidateBound();
        BeginFrameResources();
    }

    void ReleaseDrawHelpers()
    {
        auto& c = Ctx();
        ReleaseHelpers();
        for (auto& [k, p] : c.Pipelines)
            if (p) p->Release();
        c.Pipelines.clear();
        s_Bound = Bound{};
    }
}

#endif
