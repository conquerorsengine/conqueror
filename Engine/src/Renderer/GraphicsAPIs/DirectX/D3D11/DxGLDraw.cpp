#ifdef CQ_D3D11_ENABLED

#include "DxGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstring>

namespace Conqueror::DxGL
{
    using DX::Core;

    namespace
    {
        struct Hasher
        {
            uint64_t H = 1469598103934665603ull;
            void Add(uint64_t v) { H = (H ^ v) * 1099511628211ull; }
        };

        // Bağlı durum önbelleği (gereksiz D3D çağrılarını önler); sunum ve yardımcı çizimlerden sonra sıfırlanır
        struct Bound
        {
            ID3D11VertexShader* VS = nullptr;
            ID3D11PixelShader* PS = nullptr;
            ID3D11InputLayout* Layout = nullptr;
            ID3D11RasterizerState* Raster = nullptr;
            ID3D11DepthStencilState* Depth = nullptr;
            UINT StencilRef = 0;
            ID3D11BlendState* Blend = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        };
        Bound s_Bound;

        void InvalidateBound() { s_Bound = Bound{}; }

        D3D11_BLEND ToBlend(GLenum f, bool alpha)
        {
            switch (f)
            {
                case GL_ZERO: return D3D11_BLEND_ZERO;
                case GL_ONE: return D3D11_BLEND_ONE;
                case GL_SRC_COLOR: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
                case GL_ONE_MINUS_SRC_COLOR: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
                case GL_DST_COLOR: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
                case GL_ONE_MINUS_DST_COLOR: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
                case GL_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
                case GL_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
                case GL_DST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
                case GL_ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
                case GL_CONSTANT_COLOR: case GL_CONSTANT_ALPHA: return D3D11_BLEND_BLEND_FACTOR;
                case GL_ONE_MINUS_CONSTANT_COLOR: case GL_ONE_MINUS_CONSTANT_ALPHA: return D3D11_BLEND_INV_BLEND_FACTOR;
                case GL_SRC_ALPHA_SATURATE: return D3D11_BLEND_SRC_ALPHA_SAT;
                default: return D3D11_BLEND_ONE;
            }
        }

        D3D11_BLEND_OP ToBlendOp(GLenum e)
        {
            switch (e)
            {
                case GL_FUNC_SUBTRACT: return D3D11_BLEND_OP_SUBTRACT;
                case GL_FUNC_REVERSE_SUBTRACT: return D3D11_BLEND_OP_REV_SUBTRACT;
                case GL_MIN: return D3D11_BLEND_OP_MIN;
                case GL_MAX: return D3D11_BLEND_OP_MAX;
                default: return D3D11_BLEND_OP_ADD;
            }
        }

        D3D11_COMPARISON_FUNC ToCompare(GLenum f)
        {
            switch (f)
            {
                case GL_NEVER: return D3D11_COMPARISON_NEVER;
                case GL_LESS: return D3D11_COMPARISON_LESS;
                case GL_EQUAL: return D3D11_COMPARISON_EQUAL;
                case GL_LEQUAL: return D3D11_COMPARISON_LESS_EQUAL;
                case GL_GREATER: return D3D11_COMPARISON_GREATER;
                case GL_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
                case GL_GEQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
                default: return D3D11_COMPARISON_ALWAYS;
            }
        }

        D3D11_STENCIL_OP ToStencilOp(GLenum o)
        {
            switch (o)
            {
                case GL_ZERO: return D3D11_STENCIL_OP_ZERO;
                case GL_REPLACE: return D3D11_STENCIL_OP_REPLACE;
                case GL_INCR: return D3D11_STENCIL_OP_INCR_SAT;
                case GL_DECR: return D3D11_STENCIL_OP_DECR_SAT;
                case GL_INVERT: return D3D11_STENCIL_OP_INVERT;
                case GL_INCR_WRAP: return D3D11_STENCIL_OP_INCR;
                case GL_DECR_WRAP: return D3D11_STENCIL_OP_DECR;
                default: return D3D11_STENCIL_OP_KEEP;
            }
        }

        D3D11_PRIMITIVE_TOPOLOGY ToTopology(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
                case GL_LINES: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
                case GL_LINE_STRIP: case GL_LINE_LOOP: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case GL_TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
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

        // ── Yardımcı çizimler (ölçekli / çevrilmiş kopya, çoklu örnek derinlik çözümleme) ──
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
)";

        struct Helpers
        {
            ID3D11VertexShader* VS = nullptr;
            ID3D11PixelShader* PSColor = nullptr;
            ID3D11PixelShader* PSColorMS = nullptr;
            ID3D11PixelShader* PSDepthMS = nullptr;
            ID3D11PixelShader* PSColorArray = nullptr;
            ID3D11Buffer* CB = nullptr;
            ID3D11SamplerState* Point = nullptr;
            ID3D11SamplerState* Linear = nullptr;
            ID3D11RasterizerState* Raster = nullptr;
            ID3D11DepthStencilState* NoDepth = nullptr;
            ID3D11DepthStencilState* WriteDepth = nullptr;
            ID3D11BlendState* Opaque = nullptr;
            bool Tried = false;
        };
        Helpers s_Helpers;

        bool EnsureHelpers()
        {
            auto& h = s_Helpers;
            if (h.Tried)
                return h.VS != nullptr;
            h.Tried = true;
            auto* dev = Core::Get().Device();
            std::vector<uint8_t> code;
            std::string err;
            auto compile = [&](const char* entry, const char* target) -> bool { return Core::CompileHLSL(kHelperHLSL, entry, entry, target, code, err); };
            if (!compile("VSMain", "vs_5_0") || FAILED(dev->CreateVertexShader(code.data(), code.size(), nullptr, &h.VS)))
            {
                CQ_CORE_ERROR("[DxGL] Helper shader failed: {0}", err);
                return false;
            }
            if (compile("PSColor", "ps_5_0"))
                dev->CreatePixelShader(code.data(), code.size(), nullptr, &h.PSColor);
            if (compile("PSColorMS", "ps_5_0"))
                dev->CreatePixelShader(code.data(), code.size(), nullptr, &h.PSColorMS);
            if (compile("PSDepthMS", "ps_5_0"))
                dev->CreatePixelShader(code.data(), code.size(), nullptr, &h.PSDepthMS);
            if (compile("PSColorArray", "ps_5_0"))
                dev->CreatePixelShader(code.data(), code.size(), nullptr, &h.PSColorArray);
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 32;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            dev->CreateBuffer(&bd, nullptr, &h.CB);
            D3D11_SAMPLER_DESC sd{};
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            dev->CreateSamplerState(&sd, &h.Point);
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            dev->CreateSamplerState(&sd, &h.Linear);
            D3D11_RASTERIZER_DESC rs{};
            rs.FillMode = D3D11_FILL_SOLID;
            rs.CullMode = D3D11_CULL_NONE;
            rs.DepthClipEnable = TRUE;
            dev->CreateRasterizerState(&rs, &h.Raster);
            D3D11_DEPTH_STENCIL_DESC ds{};
            ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dev->CreateDepthStencilState(&ds, &h.NoDepth);
            ds.DepthEnable = TRUE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dev->CreateDepthStencilState(&ds, &h.WriteDepth);
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            dev->CreateBlendState(&blend, &h.Opaque);
            return true;
        }

        void ReleaseHelpers()
        {
            auto& h = s_Helpers;
            SafeRelease(h.VS);
            SafeRelease(h.PSColor);
            SafeRelease(h.PSColorMS);
            SafeRelease(h.PSDepthMS);
            SafeRelease(h.PSColorArray);
            SafeRelease(h.CB);
            SafeRelease(h.Point);
            SafeRelease(h.Linear);
            SafeRelease(h.Raster);
            SafeRelease(h.NoDepth);
            SafeRelease(h.WriteDepth);
            SafeRelease(h.Opaque);
            h.Tried = false;
        }

        // Yardımcı tam ekran çizim: hedef görünüm + dikdörtgen (GL satırları), kaynak UV dikdörtgeni
        void HelperDraw(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, int x0, int y0, int x1, int y1, const float uv[4], ID3D11PixelShader* ps,
                        ID3D11ShaderResourceView* srv, UINT srvSlot, bool linear, bool depthWrite)
        {
            auto& h = s_Helpers;
            auto* ctx = Core::Get().Context();
            EndRendering();
            InvalidateBound();
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(ctx->Map(h.CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            {
                std::memcpy(m.pData, uv, sizeof(float) * 4);
                ctx->Unmap(h.CB, 0);
            }
            ctx->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);
            D3D11_VIEWPORT vp{ (float)std::min(x0, x1), (float)std::min(y0, y1), (float)std::abs(x1 - x0), (float)std::abs(y1 - y0), 0.0f, 1.0f };
            ctx->RSSetViewports(1, &vp);
            ctx->RSSetState(h.Raster);
            ctx->OMSetDepthStencilState(depthWrite ? h.WriteDepth : h.NoDepth, 0);
            ctx->OMSetBlendState(h.Opaque, nullptr, 0xFFFFFFFF);
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(h.VS, nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, &h.CB);
            ctx->PSSetShader(ps, nullptr, 0);
            ID3D11ShaderResourceView* srvs[4] = {};
            srvs[srvSlot] = srv;
            ctx->PSSetShaderResources(0, 4, srvs);
            ID3D11SamplerState* smp = linear ? h.Linear : h.Point;
            ctx->PSSetSamplers(0, 1, &smp);
            ctx->Draw(3, 0);
            ID3D11ShaderResourceView* none[4] = {};
            ctx->PSSetShaderResources(0, 4, none);
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
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
    }

    void FlushAndWaitGL()
    {
        // D3D11 Map/GetData beklemeyi kendisi yapar; komutları sürücüye it
        if (Core::Get().IsInitialized())
            Core::Get().Context()->Flush();
    }

    bool EnsureRendering()
    {
        auto& c = Ctx();
        Targets t = CollectTargets(c.S.DrawFBO);
        ID3D11RenderTargetView* rtvs[8] = {};
        Hasher h;
        h.Add(c.S.DrawFBO);
        uint32_t width = UINT32_MAX, height = UINT32_MAX, samples = 1;
        for (int i = 0; i < t.ColorCount; ++i)
        {
            if (t.Color[i] && t.Color[i]->Image)
            {
                rtvs[i] = GetRTV(*t.Color[i], t.ColorLevel[i], t.ColorLayer[i]);
                width = std::min(width, std::max(1u, t.Color[i]->Width >> t.ColorLevel[i]));
                height = std::min(height, std::max(1u, t.Color[i]->Height >> t.ColorLevel[i]));
                samples = t.Color[i]->Samples;
            }
            h.Add((uint64_t)(uintptr_t)rtvs[i]);
        }
        ID3D11DepthStencilView* dsv = nullptr;
        if (t.Depth && t.Depth->Image && t.Depth->Depth)
        {
            dsv = GetDSV(*t.Depth, t.DepthLevel, t.DepthLayer);
            width = std::min(width, std::max(1u, t.Depth->Width >> t.DepthLevel));
            height = std::min(height, std::max(1u, t.Depth->Height >> t.DepthLevel));
            samples = t.Depth->Samples;
        }
        h.Add((uint64_t)(uintptr_t)dsv);
        if (width == UINT32_MAX)
            return false;
        if (c.Rendering && c.RenderingKey == h.H)
            return true;
        Core::Get().Context()->OMSetRenderTargets((UINT)t.ColorCount, rtvs, dsv);
        c.RenderingColorFormats.assign((size_t)t.ColorCount, DXGI_FORMAT_UNKNOWN);
        c.RenderingColorInteger.assign((size_t)t.ColorCount, false);
        for (int i = 0; i < t.ColorCount; ++i)
            if (rtvs[i])
            {
                c.RenderingColorFormats[(size_t)i] = t.Color[i]->RTVFormat;
                c.RenderingColorInteger[(size_t)i] = t.Color[i]->Integer;
            }
        for (int i = 0; i < 8; ++i)
            c.BoundRTVs[i] = rtvs[i];
        c.BoundRTVCount = (uint32_t)t.ColorCount;
        c.BoundDSV = dsv;
        c.RenderingHasDepth = dsv != nullptr;
        c.RenderingHasStencil = dsv && t.Depth->Stencil;
        c.RenderingSamples = samples;
        c.RenderWidth = width;
        c.RenderHeight = height;
        c.Rendering = true;
        c.RenderingFBO = c.S.DrawFBO;
        c.RenderingKey = h.H;
        return true;
    }

    void ClearCurrent(GLbitfield mask)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!EnsureRendering())
            return;
        auto* ctx = core.Context();
        const bool scissor = c.S.ScissorTest;
        D3D11_RECT rect{ c.S.Scissor[0], c.S.Scissor[1], c.S.Scissor[0] + c.S.Scissor[2], c.S.Scissor[1] + c.S.Scissor[3] };
        if (scissor && (rect.right <= rect.left || rect.bottom <= rect.top))
            return;
        if (mask & GL_COLOR_BUFFER_BIT)
        {
            for (uint32_t i = 0; i < c.BoundRTVCount; ++i)
            {
                ID3D11RenderTargetView* rtv = c.BoundRTVs[i];
                if (!rtv)
                    continue;
                if (scissor && core.Context1())
                    core.Context1()->ClearView(rtv, c.S.ClearColor, &rect, 1);
                else
                    ctx->ClearRenderTargetView(rtv, c.S.ClearColor);
            }
        }
        UINT flags = 0;
        if ((mask & GL_DEPTH_BUFFER_BIT) && c.RenderingHasDepth && c.S.DepthMask)
            flags |= D3D11_CLEAR_DEPTH;
        if ((mask & GL_STENCIL_BUFFER_BIT) && c.RenderingHasStencil)
            flags |= D3D11_CLEAR_STENCIL;
        if (flags && c.BoundDSV)
            ctx->ClearDepthStencilView(c.BoundDSV, flags, std::clamp(c.S.ClearDepth, 0.0f, 1.0f), (UINT8)c.S.ClearStencil);
    }

    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        auto* dev = core.Device();
        auto* ctx = core.Context();
        if (count <= 0 || instances <= 0)
            return;
        Program* p = GetProgram(c.S.Program);
        if (!p || !p->Linked)
            return;
        if (!EnsureRendering())
            return;
        VertexArray* vao = CurrentVAO();

        // ── Giriş düzeni + köşe arabellekleri (öznitelik başına bir yuva) ──
        ID3D11Buffer* vbufs[16] = {};
        UINT strides[16] = {}, offsets[16] = {};
        D3D11_INPUT_ELEMENT_DESC elems[16] = {};
        UINT n = 0;
        Hasher layoutKey;
        for (const auto& in : p->Inputs)
        {
            if (in.Location < 0 || in.Location >= 16 || n >= 16)
                continue;
            const Attrib& a = vao->Attribs[in.Location];
            Buffer* buf = a.Enabled ? GetBuffer(a.Buffer) : nullptr;
            D3D11_INPUT_ELEMENT_DESC& e = elems[n];
            e.SemanticName = "TEXCOORD";
            e.SemanticIndex = (UINT)in.Location;
            e.InputSlot = n;
            e.AlignedByteOffset = 0;
            if (buf)
            {
                e.Format = AttribFormat(a);
                e.InputSlotClass = a.Divisor ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                e.InstanceDataStepRate = a.Divisor;
                vbufs[n] = GeometryBuffer(*buf);
                strides[n] = a.Stride ? (UINT)a.Stride : AttribSize(a);
                offsets[n] = (UINT)a.Offset;
            }
            else
            {
                // GL varsayılan özniteliği (0,0,0,1): sıfır adımlı sabit arabellek
                e.Format = in.Integer ? DXGI_FORMAT_R32G32B32A32_SINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
                e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                vbufs[n] = c.NullVertexBuffer;
                strides[n] = 0;
                offsets[n] = in.Integer ? 16 : 0;
            }
            layoutKey.Add(((uint64_t)in.Location << 40) | ((uint64_t)e.Format << 8) | (uint64_t)e.InputSlotClass);
            layoutKey.Add(e.InstanceDataStepRate);
            ++n;
        }
        ID3D11InputLayout* layout = nullptr;
        if (n)
        {
            auto it = p->InputLayouts.find(layoutKey.H);
            if (it != p->InputLayouts.end())
                layout = it->second;
            else
            {
                if (FAILED(dev->CreateInputLayout(elems, n, p->VSBytecode.data(), p->VSBytecode.size(), &layout)))
                {
                    CQ_CORE_ERROR("[DxGL] CreateInputLayout failed for program {0}", p->Id);
                    layout = nullptr;
                }
                p->InputLayouts[layoutKey.H] = layout;
            }
            if (!layout)
                return;
        }
        if (s_Bound.Layout != layout)
        {
            ctx->IASetInputLayout(layout);
            s_Bound.Layout = layout;
        }
        if (n)
            ctx->IASetVertexBuffers(0, n, vbufs, strides, offsets);
        const D3D11_PRIMITIVE_TOPOLOGY topology = ToTopology(mode);
        if (s_Bound.Topology != topology)
        {
            ctx->IASetPrimitiveTopology(topology);
            s_Bound.Topology = topology;
        }
        if (s_Bound.VS != p->VS)
        {
            ctx->VSSetShader(p->VS, nullptr, 0);
            s_Bound.VS = p->VS;
        }
        if (s_Bound.PS != p->PS)
        {
            ctx->PSSetShader(p->PS, nullptr, 0);
            s_Bound.PS = p->PS;
        }

        // ── Sabit arabellekler ──
        ID3D11Buffer* cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        UINT cbCount = 0;
        for (const auto& blk : p->Blocks)
        {
            const uint32_t glb = blk.GLBinding < 64 ? blk.GLBinding : 0;
            Buffer* ub = GetBuffer(c.S.UBOBindings[glb]);
            cbs[blk.Register] = ub ? ConstantBuffer(*ub) : nullptr;
            cbCount = std::max(cbCount, blk.Register + 1);
        }
        if (p->GlobalsRegister >= 0 && p->GlobalsBuffer)
        {
            if (p->GlobalsDirty)
            {
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(p->GlobalsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                {
                    std::memcpy(m.pData, p->Globals.data(), p->Globals.size());
                    ctx->Unmap(p->GlobalsBuffer, 0);
                }
                p->GlobalsDirty = false;
            }
            cbs[p->GlobalsRegister] = p->GlobalsBuffer;
            cbCount = std::max(cbCount, (UINT)p->GlobalsRegister + 1);
        }
        if (cbCount)
        {
            ctx->VSSetConstantBuffers(0, cbCount, cbs);
            ctx->PSSetConstantBuffers(0, cbCount, cbs);
        }

        // ── Dokular + örnekleyiciler ──
        ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        UINT srvCount = 0, smpCount = 0;
        for (const auto& s : p->Samplers)
        {
            const Texture* shared = nullptr;
            for (uint32_t e = 0; e < s.Count; ++e)
            {
                const int unit = std::clamp(e < s.Units.size() ? s.Units[e] : 0, 0, 63);
                const uint32_t texName = s.Target == GL_TEXTURE_CUBE_MAP ? c.S.TexCube[(size_t)unit] :
                                         s.Target == GL_TEXTURE_2D_MULTISAMPLE ? c.S.Tex2DMS[(size_t)unit] : c.S.Tex2D[(size_t)unit];
                Texture* tex = GetTexture(texName);
                if (!tex || !tex->Image || (s.Target == GL_TEXTURE_CUBE_MAP) != (tex->Layers == 6) || tex->Integer != s.Integer)
                    tex = s.Target == GL_TEXTURE_CUBE_MAP ? c.NullTextureCube : s.Integer ? c.NullTextureInt : c.NullTexture2D;
                const Texture* params = tex;
                if (auto so = c.SamplerObjects.find(c.S.Samplers[(size_t)unit]); so != c.SamplerObjects.end() && c.S.Samplers[(size_t)unit])
                    params = so->second.get();
                const UINT slot = s.Register + e;
                if (slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
                {
                    srvs[slot] = GetSRV(*tex);
                    srvCount = std::max(srvCount, slot + 1);
                }
                if (s.SharedSampler)
                {
                    if (!shared && tex != c.NullTexture2D)
                        shared = params;
                }
                else if (slot < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
                {
                    samplers[slot] = GetSampler(*params, tex->Integer, s.Shadow);
                    smpCount = std::max(smpCount, slot + 1);
                }
            }
            if (s.SharedSampler && s.SamplerRegister < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
            {
                samplers[s.SamplerRegister] = GetSampler(shared ? *shared : *c.NullTexture2D, s.Integer, s.Shadow);
                smpCount = std::max(smpCount, s.SamplerRegister + 1);
            }
        }
        if (srvCount)
        {
            ctx->PSSetShaderResources(0, srvCount, srvs);
            ctx->VSSetShaderResources(0, srvCount, srvs);
        }
        if (smpCount)
        {
            ctx->PSSetSamplers(0, smpCount, samplers);
            ctx->VSSetSamplers(0, smpCount, samplers);
        }

        // ── Sabit işlev durumları ──
        {
            Hasher k;
            k.Add(c.S.PolygonMode);
            k.Add(c.S.CullFace ? c.S.CullMode : 0);
            k.Add(c.S.FrontFace);
            k.Add(c.S.PolygonOffsetFill ? 1 : 0);
            int32_t units = (int32_t)c.S.PolygonOffsetUnits;
            float slope = c.S.PolygonOffsetFactor;
            uint32_t slopeBits;
            std::memcpy(&slopeBits, &slope, 4);
            k.Add((uint64_t)(uint32_t)units);
            k.Add(slopeBits);
            k.Add(c.S.ScissorTest ? 1 : 0);
            k.Add(c.RenderingSamples > 1 ? 1 : 0);
            ID3D11RasterizerState*& rs = c.RasterStates[k.H];
            if (!rs)
            {
                D3D11_RASTERIZER_DESC d{};
                d.FillMode = c.S.PolygonMode == GL_LINE ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
                d.CullMode = !c.S.CullFace ? D3D11_CULL_NONE : c.S.CullMode == GL_FRONT ? D3D11_CULL_FRONT :
                             c.S.CullMode == GL_FRONT_AND_BACK ? D3D11_CULL_NONE : D3D11_CULL_BACK;
                // Y çevirmesi sarımı ters çevirir (bkz. DxGL.h)
                d.FrontCounterClockwise = c.S.FrontFace == GL_CW ? TRUE : FALSE;
                if (c.S.PolygonOffsetFill)
                {
                    d.DepthBias = units;
                    d.SlopeScaledDepthBias = slope;
                }
                d.DepthClipEnable = TRUE;
                d.ScissorEnable = c.S.ScissorTest ? TRUE : FALSE;
                d.MultisampleEnable = c.RenderingSamples > 1 ? TRUE : FALSE;
                dev->CreateRasterizerState(&d, &rs);
            }
            if (s_Bound.Raster != rs)
            {
                ctx->RSSetState(rs);
                s_Bound.Raster = rs;
            }
            // GL_FRONT_AND_BACK ayıklama: hiçbir üçgen çizilmez
            if (c.S.CullFace && c.S.CullMode == GL_FRONT_AND_BACK && topology >= D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
                return;
        }
        {
            Hasher k;
            const bool depthTest = c.S.DepthTest && c.RenderingHasDepth;
            const bool stencilTest = c.S.StencilTest && c.RenderingHasStencil;
            k.Add(depthTest ? 1 : 0);
            k.Add(depthTest && c.S.DepthMask ? 1 : 0);
            k.Add(c.S.DepthFunc);
            k.Add(stencilTest ? 1 : 0);
            if (stencilTest)
            {
                k.Add(c.S.StencilFunc);
                k.Add(c.S.StencilValueMask & 0xFF);
                k.Add(c.S.StencilWriteMask & 0xFF);
                k.Add(c.S.StencilFail);
                k.Add(c.S.StencilZFail);
                k.Add(c.S.StencilZPass);
            }
            ID3D11DepthStencilState*& ds = c.DepthStates[k.H];
            if (!ds)
            {
                D3D11_DEPTH_STENCIL_DESC d{};
                d.DepthEnable = depthTest ? TRUE : FALSE;
                d.DepthWriteMask = depthTest && c.S.DepthMask ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                d.DepthFunc = ToCompare(c.S.DepthFunc);
                d.StencilEnable = stencilTest ? TRUE : FALSE;
                d.StencilReadMask = (UINT8)(c.S.StencilValueMask & 0xFF);
                d.StencilWriteMask = (UINT8)(c.S.StencilWriteMask & 0xFF);
                d.FrontFace = { ToStencilOp(c.S.StencilFail), ToStencilOp(c.S.StencilZFail), ToStencilOp(c.S.StencilZPass), ToCompare(c.S.StencilFunc) };
                d.BackFace = d.FrontFace;
                dev->CreateDepthStencilState(&d, &ds);
            }
            const UINT ref = (UINT)(c.S.StencilRef & 0xFF);
            if (s_Bound.Depth != ds || s_Bound.StencilRef != ref)
            {
                ctx->OMSetDepthStencilState(ds, ref);
                s_Bound.Depth = ds;
                s_Bound.StencilRef = ref;
            }
        }
        {
            Hasher k;
            k.Add(c.S.Blend ? 1 : 0);
            k.Add(c.S.BlendSrcRGB); k.Add(c.S.BlendDstRGB); k.Add(c.S.BlendSrcA); k.Add(c.S.BlendDstA);
            k.Add(c.S.BlendEqRGB); k.Add(c.S.BlendEqA);
            for (uint8_t m : c.S.ColorMaskBits) k.Add(m);
            k.Add(p->ColorOutputMask);
            for (size_t i = 0; i < c.RenderingColorInteger.size(); ++i)
                k.Add(c.RenderingColorInteger[i] ? 1 : 0);
            ID3D11BlendState*& bs = c.BlendStates[k.H];
            if (!bs)
            {
                D3D11_BLEND_DESC d{};
                d.IndependentBlendEnable = TRUE;
                for (size_t i = 0; i < 8; ++i)
                {
                    auto& rt = d.RenderTarget[i];
                    const bool integer = i < c.RenderingColorInteger.size() && c.RenderingColorInteger[i];
                    rt.BlendEnable = c.S.Blend && !integer ? TRUE : FALSE;
                    rt.SrcBlend = ToBlend(c.S.BlendSrcRGB, false);
                    rt.DestBlend = ToBlend(c.S.BlendDstRGB, false);
                    rt.BlendOp = ToBlendOp(c.S.BlendEqRGB);
                    rt.SrcBlendAlpha = ToBlend(c.S.BlendSrcA, true);
                    rt.DestBlendAlpha = ToBlend(c.S.BlendDstA, true);
                    rt.BlendOpAlpha = ToBlendOp(c.S.BlendEqA);
                    rt.RenderTargetWriteMask = c.S.ColorMaskBits[i];   // R G B A bitleri D3D ile aynı
                    // Gölgelendiricinin yazmadığı hedef korunur
                    if (!(p->ColorOutputMask & (1u << i)))
                        rt.RenderTargetWriteMask = 0;
                }
                dev->CreateBlendState(&d, &bs);
            }
            if (s_Bound.Blend != bs)
            {
                ctx->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);
                s_Bound.Blend = bs;
            }
        }

        // ── Görünüm alanı / makas (bellek satırları GL satırlarıyla aynı) ──
        D3D11_VIEWPORT vp{ (float)c.S.Viewport[0], (float)c.S.Viewport[1], (float)std::max(1, c.S.Viewport[2]), (float)std::max(1, c.S.Viewport[3]), 0.0f, 1.0f };
        ctx->RSSetViewports(1, &vp);
        if (c.S.ScissorTest)
        {
            D3D11_RECT r{ c.S.Scissor[0], c.S.Scissor[1], c.S.Scissor[0] + std::max(0, c.S.Scissor[2]), c.S.Scissor[1] + std::max(0, c.S.Scissor[3]) };
            if (r.right <= r.left || r.bottom <= r.top)
                return;
            ctx->RSSetScissorRects(1, &r);
        }

        if (indexed)
        {
            Buffer* eb = GetBuffer(vao->ElementBuffer);
            if (!eb)
                return;
            if (indexType == GL_UNSIGNED_BYTE)
            {
                // 8 bit indeks D3D11'de yok: 16 bit'e genişletilmiş geçici arabellek
                const size_t start = (size_t)(uintptr_t)indices;
                if (start + (size_t)count > eb->Shadow.size())
                    return;
                std::vector<uint16_t> wide((size_t)count);
                for (GLsizei i = 0; i < count; ++i)
                    wide[(size_t)i] = eb->Shadow[start + (size_t)i];
                D3D11_BUFFER_DESC bd{};
                bd.ByteWidth = (UINT)(wide.size() * 2);
                bd.Usage = D3D11_USAGE_IMMUTABLE;
                bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
                D3D11_SUBRESOURCE_DATA init{ wide.data(), 0, 0 };
                ID3D11Buffer* tmp = nullptr;
                if (FAILED(dev->CreateBuffer(&bd, &init, &tmp)))
                    return;
                ctx->IASetIndexBuffer(tmp, DXGI_FORMAT_R16_UINT, 0);
                ctx->DrawIndexedInstanced((UINT)count, (UINT)instances, 0, baseVertex, 0);
                tmp->Release();
                return;
            }
            ID3D11Buffer* ib = GeometryBuffer(*eb);
            ctx->IASetIndexBuffer(ib, indexType == GL_UNSIGNED_SHORT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, (UINT)(uintptr_t)indices);
            if (instances > 1)
                ctx->DrawIndexedInstanced((UINT)count, (UINT)instances, 0, baseVertex, 0);
            else
                ctx->DrawIndexed((UINT)count, 0, baseVertex);
        }
        else
        {
            if (instances > 1)
                ctx->DrawInstanced((UINT)count, (UINT)instances, (UINT)first, 0);
            else
                ctx->Draw((UINT)count, (UINT)first);
        }
    }

    void GenerateMipmaps(Texture& t)
    {
        if (!t.Image || t.Samples > 1)
            return;
        uint32_t full = 1;
        for (uint32_t m = std::max(t.Width, t.Height); m > 1; m >>= 1)
            ++full;
        const uint32_t wanted = std::min<uint32_t>(full, (uint32_t)std::max(1, t.MaxLevel + 1));
        if (!t.Immutable && t.Levels < wanted)
            GrowLevels(t, wanted);
        if (t.Levels < 2)
            return;
        // Kendi küçültme geçişimiz (düzey ve katman başına): ID3D11DeviceContext::GenerateMips sürücüye bağlıdır,
        // yalnızca MIP_AUTOGEN biçimlerinde çalışır ve bazı uygulamalarda (Wine) küp dokularda düzeyleri doldurmaz
        if (t.Depth || !EnsureHelpers())
            return;
        auto* dev = Core::Get().Device();
        const bool layered = t.Layers > 1;
        ID3D11PixelShader* ps = layered ? s_Helpers.PSColorArray : s_Helpers.PSColor;
        if (!ps)
            return;
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
        {
            for (uint32_t l = 1; l < t.Levels; ++l)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
                vd.Format = t.SRVFormat;
                if (layered)
                {
                    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    vd.Texture2DArray.MostDetailedMip = l - 1;
                    vd.Texture2DArray.MipLevels = 1;
                    vd.Texture2DArray.FirstArraySlice = layer;
                    vd.Texture2DArray.ArraySize = 1;
                }
                else
                {
                    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    vd.Texture2D.MostDetailedMip = l - 1;
                    vd.Texture2D.MipLevels = 1;
                }
                ID3D11ShaderResourceView* src = nullptr;
                if (FAILED(dev->CreateShaderResourceView(t.Image, &vd, &src)) || !src)
                    continue;
                ID3D11RenderTargetView* rtv = GetRTV(t, (int)l, (int)layer);
                const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
                if (rtv)
                    HelperDraw(rtv, nullptr, 0, 0, (int)std::max(1u, t.Width >> l), (int)std::max(1u, t.Height >> l), uv, ps, src, layered ? 3u : 0u,
                               !t.Integer, false);
                src->Release();
            }
        }
        EndRendering();
        InvalidateBound();
        t.DefinedLevels = t.Levels >= 32 ? ~0u : ((1u << t.Levels) - 1);
    }

    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
    {
        auto& c = Ctx();
        auto* ctx = Core::Get().Context();
        Framebuffer* rf = GetFramebuffer(c.S.ReadFBO);
        Framebuffer* df = GetFramebuffer(c.S.DrawFBO);
        if (!rf || !df)
            return;
        const bool sameSize = std::abs(sx1 - sx0) == std::abs(dx1 - dx0) && std::abs(sy1 - sy0) == std::abs(dy1 - dy0);
        const bool noFlip = (sx1 > sx0) == (dx1 > dx0) && (sy1 > sy0) == (dy1 > dy0);

        auto colorBlit = [&](Texture* src, int sLevel, int sLayer, Texture* dst, int dLevel, int dLayer) {
            if (!src || !dst || !src->Image || !dst->Image || src == dst)
                return;
            const int sw = (int)std::max(1u, src->Width >> sLevel), sh = (int)std::max(1u, src->Height >> sLevel);
            const UINT dstSub = D3D11CalcSubresource((UINT)dLevel, (UINT)dLayer, dst->Levels);
            const UINT srcSub = D3D11CalcSubresource((UINT)sLevel, (UINT)sLayer, src->Levels);
            const int x0 = std::min(sx0, sx1), y0 = std::min(sy0, sy1), x1 = std::max(sx0, sx1), y1 = std::max(sy0, sy1);
            if (sameSize && noFlip && src->Samples > 1 && dst->Samples == 1 && src->RTVFormat == dst->RTVFormat && x0 == 0 && y0 == 0 &&
                x1 == sw && y1 == sh && (int)std::max(1u, dst->Width >> dLevel) == sw && (int)std::max(1u, dst->Height >> dLevel) == sh)
            {
                ctx->ResolveSubresource(dst->Image, dstSub, src->Image, srcSub, src->RTVFormat);
                return;
            }
            if (sameSize && noFlip && src->Samples == dst->Samples && src->Samples == 1 && src->Format == dst->Format)
            {
                const int cx0 = std::clamp(x0, 0, sw), cy0 = std::clamp(y0, 0, sh), cx1 = std::clamp(x1, 0, sw), cy1 = std::clamp(y1, 0, sh);
                if (cx1 <= cx0 || cy1 <= cy0)
                    return;
                D3D11_BOX box{ (UINT)cx0, (UINT)cy0, 0, (UINT)cx1, (UINT)cy1, 1 };
                ctx->CopySubresourceRegion(dst->Image, dstSub, (UINT)(std::min(dx0, dx1) + (cx0 - x0)), (UINT)(std::min(dy0, dy1) + (cy0 - y0)), 0, src->Image, srcSub,
                                           &box);
                return;
            }
            // Ölçekli / çevrilmiş / çözümlemeli kopya: yardımcı çizim
            if (!EnsureHelpers())
                return;
            ID3D11RenderTargetView* rtv = GetRTV(*dst, dLevel, dLayer);
            if (!rtv)
                return;
            const float uv[4] = { (float)sx0 / (float)sw, (float)sy0 / (float)sh, (float)sx1 / (float)sw, (float)sy1 / (float)sh };
            // Hedef dikdörtgen yönü UV ile birleştirilir (dx1 < dx0 ise UV ters)
            float uvAdj[4] = { uv[0], uv[1], uv[2], uv[3] };
            if (dx1 < dx0) std::swap(uvAdj[0], uvAdj[2]);
            if (dy1 < dy0) std::swap(uvAdj[1], uvAdj[3]);
            if (src->Samples > 1)
                HelperDraw(rtv, nullptr, dx0, dy0, dx1, dy1, uvAdj, s_Helpers.PSColorMS, GetSRV(*src), 1, false, false);
            else
                HelperDraw(rtv, nullptr, dx0, dy0, dx1, dy1, uvAdj, s_Helpers.PSColor, GetSRV(*src), 0, filter == GL_LINEAR, false);
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
                if (full && sameSize && noFlip && src->Samples == dst->Samples && src->Format == dst->Format)
                    ctx->CopyResource(dst->Image, src->Image);   // derinlik kaynakları yalnızca bütün olarak kopyalanır
                else if (src->Samples > 1 && dst->Samples == 1 && sameSize && noFlip && EnsureHelpers())
                {
                    // Çoklu örnek derinlik çözümleme: örnek 0, SV_Depth ile yazılır (şablon çözümlenmez)
                    const float uv[4] = { 0, 0, 1, 1 };
                    HelperDraw(nullptr, GetDSV(*dst, 0, 0), dx0, dy0, dx1, dy1, uv, s_Helpers.PSDepthMS, GetSRV(*src), 2, false, true);
                }
                else
                    CQ_CORE_WARN("[DxGL] Unsupported depth blit (scaled or format change)");
            }
        }
        EndRendering();
        InvalidateBound();
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
        // Sunum kopyası kaynak görünümü bağladığı için hedefler önce çözülür
        core.Context()->OMSetRenderTargets(0, nullptr, nullptr);
        EndRendering();
        core.Present(GetSRV(c.BackColor), c.BackColor.Width, c.BackColor.Height);
        InvalidateBound();
        BeginFrameResources();
    }

    void ReleaseDrawHelpers()
    {
        ReleaseHelpers();
        InvalidateBound();
    }
}

#endif
