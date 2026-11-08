#ifdef CQ_METAL_ENABLED

#include "MtlGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstring>

namespace Conqueror::MtlGL
{
    using MTL::Core;

    void OcclusionBeforeDraw(id<MTLRenderCommandEncoder> enc);   // MtlGLEntry.mm

    namespace
    {
        struct Hasher
        {
            uint64_t H = 1469598103934665603ull;
            void Add(uint64_t v) { H = (H ^ v) * 1099511628211ull; }
        };

        // Kodlayıcıya bağlı durum (gereksiz çağrıları önler); her yeni geçişte sıfırlanır
        struct Bound
        {
            uint64_t Epoch = 0;
            id<MTLRenderPipelineState> Pipeline = nil;
            id<MTLDepthStencilState> DepthState = nil;
            uint32_t StencilRef = UINT32_MAX;
            int Cull = -1, Winding = -1, Fill = -1;
        };
        Bound s_Bound;

        void SyncBound()
        {
            const uint64_t epoch = Core::Get().EncoderEpoch();
            if (s_Bound.Epoch != epoch)
            {
                s_Bound = Bound{};
                s_Bound.Epoch = epoch;
            }
        }

        MTLBlendFactor ToBlend(GLenum f, bool alpha)
        {
            switch (f)
            {
                case GL_ZERO: return MTLBlendFactorZero;
                case GL_ONE: return MTLBlendFactorOne;
                case GL_SRC_COLOR: return alpha ? MTLBlendFactorSourceAlpha : MTLBlendFactorSourceColor;
                case GL_ONE_MINUS_SRC_COLOR: return alpha ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOneMinusSourceColor;
                case GL_DST_COLOR: return alpha ? MTLBlendFactorDestinationAlpha : MTLBlendFactorDestinationColor;
                case GL_ONE_MINUS_DST_COLOR: return alpha ? MTLBlendFactorOneMinusDestinationAlpha : MTLBlendFactorOneMinusDestinationColor;
                case GL_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
                case GL_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
                case GL_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
                case GL_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
                case GL_CONSTANT_COLOR: return MTLBlendFactorBlendColor;
                case GL_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
                case GL_CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
                case GL_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
                case GL_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
                default: return MTLBlendFactorOne;
            }
        }

        MTLBlendOperation ToBlendOp(GLenum e)
        {
            switch (e)
            {
                case GL_FUNC_SUBTRACT: return MTLBlendOperationSubtract;
                case GL_FUNC_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
                case GL_MIN: return MTLBlendOperationMin;
                case GL_MAX: return MTLBlendOperationMax;
                default: return MTLBlendOperationAdd;
            }
        }

        MTLCompareFunction ToCompare(GLenum f)
        {
            switch (f)
            {
                case GL_NEVER: return MTLCompareFunctionNever;
                case GL_LESS: return MTLCompareFunctionLess;
                case GL_EQUAL: return MTLCompareFunctionEqual;
                case GL_LEQUAL: return MTLCompareFunctionLessEqual;
                case GL_GREATER: return MTLCompareFunctionGreater;
                case GL_NOTEQUAL: return MTLCompareFunctionNotEqual;
                case GL_GEQUAL: return MTLCompareFunctionGreaterEqual;
                default: return MTLCompareFunctionAlways;
            }
        }

        MTLStencilOperation ToStencilOp(GLenum o)
        {
            switch (o)
            {
                case GL_ZERO: return MTLStencilOperationZero;
                case GL_REPLACE: return MTLStencilOperationReplace;
                case GL_INCR: return MTLStencilOperationIncrementClamp;
                case GL_DECR: return MTLStencilOperationDecrementClamp;
                case GL_INVERT: return MTLStencilOperationInvert;
                case GL_INCR_WRAP: return MTLStencilOperationIncrementWrap;
                case GL_DECR_WRAP: return MTLStencilOperationDecrementWrap;
                default: return MTLStencilOperationKeep;
            }
        }

        MTLPrimitiveType ToPrimitive(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return MTLPrimitiveTypePoint;
                case GL_LINES: return MTLPrimitiveTypeLine;
                case GL_LINE_STRIP: case GL_LINE_LOOP: return MTLPrimitiveTypeLineStrip;
                case GL_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
                default: return MTLPrimitiveTypeTriangle;
            }
        }

        MTLPrimitiveTopologyClass ToTopologyClass(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return MTLPrimitiveTopologyClassPoint;
                case GL_LINES: case GL_LINE_STRIP: case GL_LINE_LOOP: return MTLPrimitiveTopologyClassLine;
                default: return MTLPrimitiveTopologyClassTriangle;
            }
        }

        MTLVertexFormat AttribFormat(const Attrib& a)
        {
            const int n = std::clamp(a.Size, 1, 4);
            switch (a.Type)
            {
                case GL_FLOAT:
                {
                    const MTLVertexFormat f[] = { MTLVertexFormatFloat, MTLVertexFormatFloat2, MTLVertexFormatFloat3, MTLVertexFormatFloat4 };
                    return f[n - 1];
                }
                case GL_HALF_FLOAT:
                {
                    const MTLVertexFormat f[] = { MTLVertexFormatHalf, MTLVertexFormatHalf2, MTLVertexFormatHalf3, MTLVertexFormatHalf4 };
                    return f[n - 1];
                }
                case GL_INT:
                {
                    const MTLVertexFormat f[] = { MTLVertexFormatInt, MTLVertexFormatInt2, MTLVertexFormatInt3, MTLVertexFormatInt4 };
                    return f[n - 1];
                }
                case GL_UNSIGNED_INT:
                {
                    const MTLVertexFormat f[] = { MTLVertexFormatUInt, MTLVertexFormatUInt2, MTLVertexFormatUInt3, MTLVertexFormatUInt4 };
                    return f[n - 1];
                }
                case GL_UNSIGNED_BYTE:
                    if (a.Integer)
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatUChar, MTLVertexFormatUChar2, MTLVertexFormatUChar3, MTLVertexFormatUChar4 };
                        return f[n - 1];
                    }
                    else
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatUCharNormalized, MTLVertexFormatUChar2Normalized, MTLVertexFormatUChar3Normalized,
                                                      MTLVertexFormatUChar4Normalized };
                        return f[n - 1];
                    }
                case GL_SHORT:
                    if (a.Normalized)
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatShortNormalized, MTLVertexFormatShort2Normalized, MTLVertexFormatShort3Normalized,
                                                      MTLVertexFormatShort4Normalized };
                        return f[n - 1];
                    }
                    else
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatShort, MTLVertexFormatShort2, MTLVertexFormatShort3, MTLVertexFormatShort4 };
                        return f[n - 1];
                    }
                case GL_UNSIGNED_SHORT:
                    if (a.Normalized)
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatUShortNormalized, MTLVertexFormatUShort2Normalized, MTLVertexFormatUShort3Normalized,
                                                      MTLVertexFormatUShort4Normalized };
                        return f[n - 1];
                    }
                    else
                    {
                        const MTLVertexFormat f[] = { MTLVertexFormatUShort, MTLVertexFormatUShort2, MTLVertexFormatUShort3, MTLVertexFormatUShort4 };
                        return f[n - 1];
                    }
                default:
                    return MTLVertexFormatFloat4;
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

        // Bekleyen temizleme (bir sonraki geçişin yükleme eylemi)
        GLbitfield s_PendingClear = 0;

        // ── Yardımcı çizimler (ölçekli / çevrilmiş kopya, kısmi temizleme, sunum) ──
        const char* kHelperMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct VSOut { float4 pos [[position]]; float2 uv; };
struct Params { float4 uv; float4 color; float4 depth; };
vertex VSOut cq_vs(uint vid [[vertex_id]], constant Params& p [[buffer(0)]])
{
    float2 q = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    // Hedef viewport'u GL yönünde dolduran üçgen: q.y=0 -> viewport alt satırı (bellek satırı originY)
    o.pos = float4(q * 2.0 - 1.0, p.depth.x, 1.0);
    o.pos.y = -o.pos.y;
    o.uv = mix(p.uv.xy, p.uv.zw, q);
    return o;
}
fragment float4 cq_color(VSOut i [[stage_in]], texture2d<float> src [[texture(0)]], sampler s [[sampler(0)]]) { return src.sample(s, i.uv, level(0)); }
fragment float4 cq_color_ms(VSOut i [[stage_in]], texture2d_ms<float> src [[texture(0)]]) { return src.read(uint2(i.pos.xy), 0); }
fragment float4 cq_present(VSOut i [[stage_in]], texture2d<float> src [[texture(0)]], sampler s [[sampler(0)]]) { return float4(src.sample(s, i.uv, level(0)).rgb, 1.0); }
struct ClearOut { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; float4 c3 [[color(3)]];
                  float4 c4 [[color(4)]]; float4 c5 [[color(5)]]; float4 c6 [[color(6)]]; float4 c7 [[color(7)]]; };
fragment ClearOut cq_clear(VSOut i [[stage_in]], constant Params& p [[buffer(0)]])
{
    ClearOut o;
    o.c0 = o.c1 = o.c2 = o.c3 = o.c4 = o.c5 = o.c6 = o.c7 = p.color;
    return o;
}
)";

        struct HelperParams
        {
            float UV[4];
            float Color[4];
            float Depth[4];
        };

        enum class HelperPS : uint32_t { Color = 0, ColorMS, Present, Clear, Count };

        struct Helpers
        {
            id<MTLFunction> VS = nil;
            id<MTLFunction> PS[(size_t)HelperPS::Count] = {};
            std::unordered_map<uint64_t, id<MTLRenderPipelineState>> Pipelines;
            std::unordered_map<uint64_t, id<MTLDepthStencilState>> DepthStates;
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
            NSError* err = nil;
            id<MTLLibrary> lib = [Core::Get().Device() newLibraryWithSource:[NSString stringWithUTF8String:kHelperMSL] options:nil error:&err];
            if (!lib)
            {
                CQ_CORE_ERROR("[MtlGL] Helper shader failed: {0}", err ? [[err localizedDescription] UTF8String] : "unknown");
                return false;
            }
            h.VS = [lib newFunctionWithName:@"cq_vs"];
            h.PS[(size_t)HelperPS::Color] = [lib newFunctionWithName:@"cq_color"];
            h.PS[(size_t)HelperPS::ColorMS] = [lib newFunctionWithName:@"cq_color_ms"];
            h.PS[(size_t)HelperPS::Present] = [lib newFunctionWithName:@"cq_present"];
            h.PS[(size_t)HelperPS::Clear] = [lib newFunctionWithName:@"cq_clear"];
            h.PointParams.MinFilter = h.PointParams.MagFilter = GL_NEAREST;
            h.LinearParams.MinFilter = h.LinearParams.MagFilter = GL_LINEAR;
            for (Texture* t : { &h.PointParams, &h.LinearParams })
                t->WrapS = t->WrapT = t->WrapR = GL_CLAMP_TO_EDGE;
            h.Ok = h.VS != nil;
            return h.Ok;
        }

        struct HelperTargets
        {
            MTLPixelFormat Color[8] = {};
            uint32_t ColorCount = 0;
            MTLColorWriteMask WriteMask = MTLColorWriteMaskAll;
            MTLPixelFormat Depth = MTLPixelFormatInvalid;
            bool Stencil = false;
            uint32_t Samples = 1;
        };

        id<MTLRenderPipelineState> HelperPipeline(HelperPS ps, const HelperTargets& t)
        {
            auto& h = s_Helpers;
            Hasher k;
            k.Add((uint64_t)ps);
            for (uint32_t i = 0; i < t.ColorCount; ++i)
                k.Add((uint64_t)t.Color[i]);
            k.Add(t.ColorCount);
            k.Add(t.WriteMask);
            k.Add((uint64_t)t.Depth);
            k.Add(t.Stencil);
            k.Add(t.Samples);
            if (auto it = h.Pipelines.find(k.H); it != h.Pipelines.end())
                return it->second;
            MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = h.VS;
            d.fragmentFunction = h.PS[(size_t)ps];
            for (uint32_t i = 0; i < t.ColorCount; ++i)
            {
                d.colorAttachments[i].pixelFormat = t.Color[i];
                d.colorAttachments[i].writeMask = t.Color[i] == MTLPixelFormatInvalid ? MTLColorWriteMaskNone : t.WriteMask;
            }
            d.depthAttachmentPixelFormat = t.Depth;
            if (t.Stencil)
                d.stencilAttachmentPixelFormat = t.Depth;
            d.rasterSampleCount = t.Samples;
            NSError* err = nil;
            id<MTLRenderPipelineState> pso = [Core::Get().Device() newRenderPipelineStateWithDescriptor:d error:&err];
            if (!pso)
                CQ_CORE_ERROR("[MtlGL] Helper pipeline failed: {0}", err ? [[err localizedDescription] UTF8String] : "unknown");
            h.Pipelines[k.H] = pso;
            return pso;
        }

        // Açık kodlayıcıda yardımcı üçgen çiz (hedefler ve viewport çağıranda)
        void HelperTriangle(id<MTLRenderCommandEncoder> enc, id<MTLRenderPipelineState> pso, const HelperParams& params, id<MTLTexture> src, bool linear)
        {
            [enc setRenderPipelineState:pso];
            [enc setCullMode:MTLCullModeNone];
            [enc setTriangleFillMode:MTLTriangleFillModeFill];
            [enc setVertexBytes:&params length:sizeof(params) atIndex:0];
            [enc setFragmentBytes:&params length:sizeof(params) atIndex:0];
            if (src)
            {
                [enc setFragmentTexture:src atIndex:0];
                [enc setFragmentSamplerState:GetSampler(linear ? s_Helpers.LinearParams : s_Helpers.PointParams, false, false) atIndex:0];
            }
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            s_Bound = Bound{};
        }

        // Tek hedefe tam ekran kopya (yeni geçiş, hedef yüklenir)
        void HelperBlit(Texture& dst, int level, int layer, int x0, int y0, int x1, int y1, const float uv[4], HelperPS ps, id<MTLTexture> src, bool linear)
        {
            if (!EnsureHelpers())
                return;
            HelperTargets ht;
            ht.Color[0] = dst.Format;
            ht.ColorCount = 1;
            ht.Samples = dst.Samples;
            id<MTLRenderPipelineState> pso = HelperPipeline(ps, ht);
            if (!pso)
                return;
            EndRendering();
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = dst.Image;
            pass.colorAttachments[0].level = (NSUInteger)level;
            pass.colorAttachments[0].slice = (NSUInteger)layer;
            pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> enc = Core::Get().BeginRender(pass);
            MTLViewport vp{ (double)std::min(x0, x1), (double)std::min(y0, y1), (double)std::abs(x1 - x0), (double)std::abs(y1 - y0), 0.0, 1.0 };
            [enc setViewport:vp];
            HelperParams params{};
            std::memcpy(params.UV, uv, sizeof(params.UV));
            HelperTriangle(enc, pso, params, src, linear);
            Core::Get().EndEncoding();
        }

        void BindEncoderTargets(MTLRenderPassDescriptor* pass, const Targets& t, GLbitfield clear)
        {
            auto& c = Ctx();
            for (int i = 0; i < t.ColorCount; ++i)
            {
                Texture* tex = t.Color[i];
                if (!tex || !tex->Image || tex->Depth)
                    continue;
                auto* a = pass.colorAttachments[i];
                a.texture = tex->Image;
                a.level = (NSUInteger)t.ColorLevel[i];
                a.slice = (NSUInteger)t.ColorLayer[i];
                a.storeAction = MTLStoreActionStore;
                if (clear & GL_COLOR_BUFFER_BIT)
                {
                    a.loadAction = MTLLoadActionClear;
                    a.clearColor = MTLClearColorMake(c.S.ClearColor[0], c.S.ClearColor[1], c.S.ClearColor[2], c.S.ClearColor[3]);
                }
                else
                    a.loadAction = MTLLoadActionLoad;
            }
            if (t.Depth && t.Depth->Image && t.Depth->Depth)
            {
                pass.depthAttachment.texture = t.Depth->Image;
                pass.depthAttachment.level = (NSUInteger)t.DepthLevel;
                pass.depthAttachment.slice = (NSUInteger)t.DepthLayer;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.depthAttachment.loadAction = (clear & GL_DEPTH_BUFFER_BIT) ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.depthAttachment.clearDepth = std::clamp(c.S.ClearDepth, 0.0f, 1.0f);
                if (t.Depth->Stencil)
                {
                    pass.stencilAttachment.texture = t.Depth->Image;
                    pass.stencilAttachment.level = (NSUInteger)t.DepthLevel;
                    pass.stencilAttachment.slice = (NSUInteger)t.DepthLayer;
                    pass.stencilAttachment.storeAction = MTLStoreActionStore;
                    pass.stencilAttachment.loadAction = (clear & GL_STENCIL_BUFFER_BIT) ? MTLLoadActionClear : MTLLoadActionLoad;
                    pass.stencilAttachment.clearStencil = (uint32_t)(c.S.ClearStencil & 0xFF);
                }
            }
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
        if (c.Rendering && s_PendingClear)
            EnsureRendering();   // bekleyen temizleme kaybolmasın
        c.Rendering = false;
        c.RenderingKey = 0;
        c.RenderingFBO = UINT32_MAX;
        Core::Get().EndEncoding();
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
        Hasher h;
        h.Add(c.S.DrawFBO);
        uint32_t width = UINT32_MAX, height = UINT32_MAX, samples = 1;
        for (int i = 0; i < t.ColorCount; ++i)
        {
            Texture* tex = t.Color[i];
            if (tex && tex->Image && !tex->Depth)
            {
                width = std::min(width, std::max(1u, tex->Width >> t.ColorLevel[i]));
                height = std::min(height, std::max(1u, tex->Height >> t.ColorLevel[i]));
                samples = tex->Samples;
            }
            else
                t.Color[i] = nullptr;
            h.Add((uint64_t)(uintptr_t)t.Color[i]);
            h.Add((uint64_t)t.ColorLevel[i] | ((uint64_t)t.ColorLayer[i] << 16));
        }
        if (t.Depth && t.Depth->Image && t.Depth->Depth)
        {
            width = std::min(width, std::max(1u, t.Depth->Width >> t.DepthLevel));
            height = std::min(height, std::max(1u, t.Depth->Height >> t.DepthLevel));
            samples = t.Depth->Samples;
        }
        else
            t.Depth = nullptr;
        h.Add((uint64_t)(uintptr_t)t.Depth);
        h.Add((uint64_t)t.DepthLevel | ((uint64_t)t.DepthLayer << 16));
        if (width == UINT32_MAX)
            return false;
        if (!s_PendingClear && c.Rendering && c.RenderingKey == h.H && c.RenderingEpoch == core.EncoderEpoch() && core.RenderEncoder())
            return true;
        // Sabit arabellek bağlamaları ve diğer durumlar kodlayıcıya bağlı: yeni geçiş
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        BindEncoderTargets(pass, t, s_PendingClear);
        pass.visibilityResultBuffer = c.Visibility;   // örtüşme sorguları
        s_PendingClear = 0;
        if (!core.BeginRender(pass))
            return false;
        for (int i = 0; i < 8; ++i)
        {
            const bool live = i < t.ColorCount && t.Color[i];
            c.RenderColor[i] = live ? t.Color[i] : nullptr;
            c.RenderColorLevel[i] = t.ColorLevel[i];
            c.RenderColorLayer[i] = t.ColorLayer[i];
            c.RenderColorFormats[i] = live ? t.Color[i]->Format : MTLPixelFormatInvalid;
            c.RenderColorInteger[i] = live && t.Color[i]->Integer;
        }
        c.RenderColorCount = (uint32_t)t.ColorCount;
        c.RenderDepth = t.Depth;
        c.RenderDepthLevel = t.DepthLevel;
        c.RenderDepthLayer = t.DepthLayer;
        c.RenderingDepthFormat = t.Depth ? t.Depth->Format : MTLPixelFormatInvalid;
        c.RenderingHasDepth = t.Depth != nullptr;
        c.RenderingHasStencil = t.Depth && t.Depth->Stencil;
        c.RenderingSamples = samples;
        c.RenderWidth = width;
        c.RenderHeight = height;
        c.Rendering = true;
        c.RenderingFBO = c.S.DrawFBO;
        c.RenderingKey = h.H;
        c.RenderingEpoch = core.EncoderEpoch();
        SyncBound();
        return true;
    }

    void ClearCurrent(GLbitfield mask)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (!c.S.DepthMask)
            mask &= ~GL_DEPTH_BUFFER_BIT;
        if (!mask)
            return;
        if (!EnsureRendering())
            return;
        int rx = 0, ry = 0, rw = (int)c.RenderWidth, rh = (int)c.RenderHeight;
        if (c.S.ScissorTest)
        {
            rx = std::max(0, c.S.Scissor[0]);
            ry = std::max(0, c.S.Scissor[1]);
            rw = std::min((int)c.RenderWidth, c.S.Scissor[0] + c.S.Scissor[2]) - rx;
            rh = std::min((int)c.RenderHeight, c.S.Scissor[1] + c.S.Scissor[3]) - ry;
            if (rw <= 0 || rh <= 0)
                return;
        }
        const bool full = rx == 0 && ry == 0 && rw == (int)c.RenderWidth && rh == (int)c.RenderHeight;
        const bool fullMask = c.S.ColorMask[0] && c.S.ColorMask[1] && c.S.ColorMask[2] && c.S.ColorMask[3];
        bool anyInteger = false;
        for (uint32_t i = 0; i < c.RenderColorCount; ++i)
            anyInteger = anyInteger || c.RenderColorInteger[i];
        if ((full && (fullMask || !(mask & GL_COLOR_BUFFER_BIT))) || anyInteger || !EnsureHelpers())
        {
            // Tam temizleme: geçişi yükleme eylemi = temizle ile yeniden aç
            s_PendingClear = mask;
            core.EndEncoding();
            c.Rendering = false;
            EnsureRendering();
            return;
        }
        // Kısmi (makaslı / renk maskeli) temizleme: dörtgen
        HelperTargets ht;
        for (uint32_t i = 0; i < c.RenderColorCount; ++i)
            ht.Color[i] = c.RenderColorFormats[i];
        ht.ColorCount = c.RenderColorCount;
        ht.WriteMask = (mask & GL_COLOR_BUFFER_BIT) ? (MTLColorWriteMask)((c.S.ColorMask[0] ? MTLColorWriteMaskRed : 0) | (c.S.ColorMask[1] ? MTLColorWriteMaskGreen : 0) |
                                                                          (c.S.ColorMask[2] ? MTLColorWriteMaskBlue : 0) | (c.S.ColorMask[3] ? MTLColorWriteMaskAlpha : 0))
                                                    : MTLColorWriteMaskNone;
        ht.Depth = c.RenderingDepthFormat;
        ht.Stencil = c.RenderingHasStencil;
        ht.Samples = c.RenderingSamples;
        id<MTLRenderPipelineState> pso = HelperPipeline(HelperPS::Clear, ht);
        if (!pso)
            return;
        Hasher dk;
        dk.Add(mask);
        dk.Add(c.S.StencilWriteMask & 0xFF);
        __strong id<MTLDepthStencilState>& ds = s_Helpers.DepthStates[dk.H];
        if (!ds)
        {
            MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
            d.depthCompareFunction = MTLCompareFunctionAlways;
            d.depthWriteEnabled = (mask & GL_DEPTH_BUFFER_BIT) ? YES : NO;
            if (mask & GL_STENCIL_BUFFER_BIT)
            {
                MTLStencilDescriptor* s = [MTLStencilDescriptor new];
                s.stencilCompareFunction = MTLCompareFunctionAlways;
                s.depthStencilPassOperation = MTLStencilOperationReplace;
                s.writeMask = c.S.StencilWriteMask & 0xFF;
                d.frontFaceStencil = s;
                d.backFaceStencil = s;
            }
            ds = [core.Device() newDepthStencilStateWithDescriptor:d];
        }
        id<MTLRenderCommandEncoder> enc = core.RenderEncoder();
        [enc setDepthStencilState:ds];
        [enc setStencilReferenceValue:(uint32_t)(c.S.ClearStencil & 0xFF)];
        MTLViewport vp{ 0.0, 0.0, (double)c.RenderWidth, (double)c.RenderHeight, 0.0, 1.0 };
        [enc setViewport:vp];
        [enc setScissorRect:(MTLScissorRect){ (NSUInteger)rx, (NSUInteger)ry, (NSUInteger)rw, (NSUInteger)rh }];
        HelperParams params{};
        for (int i = 0; i < 4; ++i)
            params.Color[i] = c.S.ClearColor[i];
        params.Depth[0] = std::clamp(c.S.ClearDepth, 0.0f, 1.0f);
        HelperTriangle(enc, pso, params, nil, false);
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
        if (c.S.CullFace && c.S.CullMode == GL_FRONT_AND_BACK && ToTopologyClass(mode) == MTLPrimitiveTopologyClassTriangle)
            return;
        if (!EnsureRendering())
            return;
        SyncBound();
        id<MTLRenderCommandEncoder> enc = core.RenderEncoder();
        VertexArray* vao = CurrentVAO();
        const uint64_t frame = core.FrameSerial();

        // ── Köşe düzeni + arabellekler (öznitelik başına bir arabellek yuvası) ──
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        Hasher layoutKey;
        uint32_t n = 0;
        for (const auto& in : p->Inputs)
        {
            if (in.Location < 0 || in.Location >= 16 || n >= 16)
                continue;
            const Attrib& a = vao->Attribs[in.Location];
            Buffer* buf = a.Enabled ? GetBuffer(a.Buffer) : nullptr;
            uint64_t offset = 0, size = 0;
            id<MTLBuffer> mb = buf ? BufferBinding(*buf, 0, offset, &size) : nil;
            MTLVertexAttributeDescriptor* attr = vd.attributes[(NSUInteger)in.Location];
            MTLVertexBufferLayoutDescriptor* layout = vd.layouts[n];
            attr.bufferIndex = n;
            attr.offset = 0;
            if (mb)
            {
                attr.format = AttribFormat(a);
                layout.stride = a.Stride ? (NSUInteger)a.Stride : AttribSize(a);
                layout.stepFunction = a.Divisor ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
                layout.stepRate = a.Divisor ? a.Divisor : 1;
                [enc setVertexBuffer:mb offset:offset + std::min<uint64_t>(a.Offset, size) atIndex:n];
            }
            else
            {
                // GL varsayılan özniteliği (0,0,0,1): sabit adımlı arabellek
                attr.format = in.Integer ? MTLVertexFormatInt4 : MTLVertexFormatFloat4;
                layout.stride = 16;
                layout.stepFunction = MTLVertexStepFunctionConstant;
                layout.stepRate = 0;
                [enc setVertexBuffer:c.NullVertexBuffer offset:in.Integer ? 16 : 0 atIndex:n];
            }
            layoutKey.Add(((uint64_t)in.Location << 40) | ((uint64_t)attr.format << 8) | (uint64_t)layout.stepFunction);
            layoutKey.Add(((uint64_t)layout.stride << 32) | layout.stepRate);
            ++n;
        }

        // ── Sabit arabellekler (16 + yazmaç) ──
        for (const auto& blk : p->Blocks)
        {
            if (blk.Register >= MTL::kMaxConstantBuffers)
                continue;
            const uint32_t glb = blk.GLBinding < 64 ? blk.GLBinding : 0;
            Buffer* ub = GetBuffer(c.S.UBOBindings[glb]);
            uint64_t offset = 0;
            id<MTLBuffer> mb = ub ? BufferBinding(*ub, blk.Size, offset) : nil;
            if (!mb)
            {
                mb = c.ZeroConstants;
                offset = 0;
            }
            [enc setVertexBuffer:mb offset:offset atIndex:MTL::kConstantBufferBase + blk.Register];
            [enc setFragmentBuffer:mb offset:offset atIndex:MTL::kConstantBufferBase + blk.Register];
        }
        if (p->GlobalsRegister >= 0 && p->GlobalsRegister < (int)MTL::kMaxConstantBuffers)
        {
            if (p->GlobalsDirty || p->GlobalsFrame != frame || !p->GlobalsBuffer)
            {
                MTL::UploadAlloc a = core.AllocUpload(std::max<uint64_t>(p->Globals.size(), 256), 256);
                if (a.Cpu)
                {
                    std::memcpy(a.Cpu, p->Globals.data(), p->Globals.size());
                    p->GlobalsBuffer = a.Buffer;
                    p->GlobalsOffset = a.Offset;
                    p->GlobalsFrame = frame;
                    p->GlobalsDirty = false;
                }
            }
            if (p->GlobalsBuffer)
            {
                [enc setVertexBuffer:p->GlobalsBuffer offset:p->GlobalsOffset atIndex:MTL::kConstantBufferBase + (uint32_t)p->GlobalsRegister];
                [enc setFragmentBuffer:p->GlobalsBuffer offset:p->GlobalsOffset atIndex:MTL::kConstantBufferBase + (uint32_t)p->GlobalsRegister];
            }
        }

        // ── Dokular + örnekleyiciler ──
        auto boundAsTarget = [&](const Texture* t) {
            if (t == c.RenderDepth)
                return true;
            for (uint32_t i = 0; i < c.RenderColorCount; ++i)
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
                // Bağlı hedef aynı geçişte örneklenemez (D3D katmanlarıyla aynı: boş doku)
                if (!tex || !tex->Image || (s.Target == GL_TEXTURE_CUBE_MAP) != (tex->Layers == 6) || tex->Integer != s.Integer || boundAsTarget(tex))
                    tex = s.Target == GL_TEXTURE_CUBE_MAP ? c.NullTextureCube : s.Integer ? c.NullTextureInt : c.NullTexture2D;
                const Texture* params = tex;
                if (auto so = c.SamplerObjects.find(c.S.Samplers[(size_t)unit]); so != c.SamplerObjects.end() && c.S.Samplers[(size_t)unit])
                    params = so->second.get();
                const uint32_t slot = s.Register + e;
                if (slot < MTL::kMaxTextures)
                {
                    id<MTLTexture> view = SampleView(*tex);
                    [enc setFragmentTexture:view atIndex:slot];
                    [enc setVertexTexture:view atIndex:slot];
                }
                if (s.SharedSampler)
                {
                    if (!shared && tex != c.NullTexture2D)
                        shared = params;
                }
                else if (slot < MTL::kMaxSamplers)
                {
                    id<MTLSamplerState> smp = GetSampler(*params, tex->Integer, s.Shadow);
                    [enc setFragmentSamplerState:smp atIndex:slot];
                    [enc setVertexSamplerState:smp atIndex:slot];
                }
            }
            if (s.SharedSampler && s.SamplerRegister < MTL::kMaxSamplers)
            {
                id<MTLSamplerState> smp = GetSampler(shared ? *shared : *c.NullTexture2D, s.Integer, s.Shadow);
                [enc setFragmentSamplerState:smp atIndex:s.SamplerRegister];
                [enc setVertexSamplerState:smp atIndex:s.SamplerRegister];
            }
        }

        // ── Boru hattı durumu ──
        Hasher bk;
        bk.Add(c.S.Blend ? 1 : 0);
        if (c.S.Blend)
        {
            bk.Add(c.S.BlendSrcRGB); bk.Add(c.S.BlendDstRGB); bk.Add(c.S.BlendSrcA); bk.Add(c.S.BlendDstA);
            bk.Add(c.S.BlendEqRGB); bk.Add(c.S.BlendEqA);
        }
        for (uint8_t m : c.S.ColorMaskBits) bk.Add(m);
        Hasher xk;
        xk.Add(layoutKey.H);
        xk.Add(p->Id);
        xk.Add(bk.H);
        xk.Add(ToTopologyClass(mode));
        for (uint32_t i = 0; i < 8; ++i)
            xk.Add((uint64_t)c.RenderColorFormats[i]);
        xk.Add((uint64_t)c.RenderingDepthFormat);
        xk.Add(c.RenderingHasStencil);
        xk.Add(c.RenderingSamples);
        __strong id<MTLRenderPipelineState>& pso = c.Pipelines[xk.H];
        if (!pso)
        {
            MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = p->VS;
            d.fragmentFunction = p->PS;
            d.vertexDescriptor = vd;
            d.inputPrimitiveTopology = ToTopologyClass(mode);
            d.rasterSampleCount = c.RenderingSamples;
            for (uint32_t i = 0; i < 8; ++i)
            {
                MTLRenderPipelineColorAttachmentDescriptor* ca = d.colorAttachments[i];
                ca.pixelFormat = c.RenderColorFormats[i];
                if (ca.pixelFormat == MTLPixelFormatInvalid)
                    continue;
                const bool integer = c.RenderColorInteger[i];
                ca.blendingEnabled = c.S.Blend && !integer ? YES : NO;
                ca.sourceRGBBlendFactor = ToBlend(c.S.BlendSrcRGB, false);
                ca.destinationRGBBlendFactor = ToBlend(c.S.BlendDstRGB, false);
                ca.rgbBlendOperation = ToBlendOp(c.S.BlendEqRGB);
                ca.sourceAlphaBlendFactor = ToBlend(c.S.BlendSrcA, true);
                ca.destinationAlphaBlendFactor = ToBlend(c.S.BlendDstA, true);
                ca.alphaBlendOperation = ToBlendOp(c.S.BlendEqA);
                const uint8_t m = c.S.ColorMaskBits[i];
                ca.writeMask = ((m & 1) ? MTLColorWriteMaskRed : 0) | ((m & 2) ? MTLColorWriteMaskGreen : 0) | ((m & 4) ? MTLColorWriteMaskBlue : 0) |
                               ((m & 8) ? MTLColorWriteMaskAlpha : 0);
                // Gölgelendiricinin yazmadığı hedef korunur
                if (!(p->ColorOutputMask & (1u << i)))
                    ca.writeMask = MTLColorWriteMaskNone;
            }
            d.depthAttachmentPixelFormat = c.RenderingDepthFormat;
            if (c.RenderingHasStencil)
                d.stencilAttachmentPixelFormat = c.RenderingDepthFormat;
            NSError* err = nil;
            pso = [core.Device() newRenderPipelineStateWithDescriptor:d error:&err];
            if (!pso)
                CQ_CORE_ERROR("[MtlGL] Pipeline creation failed for program {0}: {1}", p->Id, err ? [[err localizedDescription] UTF8String] : "unknown");
        }
        if (!pso)
            return;
        if (s_Bound.Pipeline != pso)
        {
            [enc setRenderPipelineState:pso];
            s_Bound.Pipeline = pso;
        }

        // Derinlik / şablon
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
        __strong id<MTLDepthStencilState>& ds = c.DepthStates[dk.H];
        if (!ds)
        {
            MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
            d.depthCompareFunction = depthTest ? ToCompare(c.S.DepthFunc) : MTLCompareFunctionAlways;
            d.depthWriteEnabled = depthTest && c.S.DepthMask ? YES : NO;
            if (stencilTest)
            {
                MTLStencilDescriptor* s = [MTLStencilDescriptor new];
                s.stencilCompareFunction = ToCompare(c.S.StencilFunc);
                s.stencilFailureOperation = ToStencilOp(c.S.StencilFail);
                s.depthFailureOperation = ToStencilOp(c.S.StencilZFail);
                s.depthStencilPassOperation = ToStencilOp(c.S.StencilZPass);
                s.readMask = c.S.StencilValueMask & 0xFF;
                s.writeMask = c.S.StencilWriteMask & 0xFF;
                d.frontFaceStencil = s;
                d.backFaceStencil = s;
            }
            ds = [core.Device() newDepthStencilStateWithDescriptor:d];
        }
        if (s_Bound.DepthState != ds)
        {
            [enc setDepthStencilState:ds];
            s_Bound.DepthState = ds;
        }
        const uint32_t ref = (uint32_t)(c.S.StencilRef & 0xFF);
        if (s_Bound.StencilRef != ref)
        {
            [enc setStencilReferenceValue:ref];
            s_Bound.StencilRef = ref;
        }

        // Tarama durumu
        const int cull = !c.S.CullFace ? (int)MTLCullModeNone : c.S.CullMode == GL_FRONT ? (int)MTLCullModeFront :
                         c.S.CullMode == GL_FRONT_AND_BACK ? (int)MTLCullModeNone : (int)MTLCullModeBack;
        if (s_Bound.Cull != cull)
        {
            [enc setCullMode:(MTLCullMode)cull];
            s_Bound.Cull = cull;
        }
        // Y çevirmesi sarımı ters çevirir: GL_CCW ön yüz -> saat yönü
        const int winding = c.S.FrontFace == GL_CW ? (int)MTLWindingCounterClockwise : (int)MTLWindingClockwise;
        if (s_Bound.Winding != winding)
        {
            [enc setFrontFacingWinding:(MTLWinding)winding];
            s_Bound.Winding = winding;
        }
        const int fill = c.S.PolygonMode == GL_LINE ? (int)MTLTriangleFillModeLines : (int)MTLTriangleFillModeFill;
        if (s_Bound.Fill != fill)
        {
            [enc setTriangleFillMode:(MTLTriangleFillMode)fill];
            s_Bound.Fill = fill;
        }
        // GL birimleri en küçük çözünürlük adımıdır; 32F derinlikte Metal sabit kaydırmayı da benzer ölçekler
        if (c.S.PolygonOffsetFill)
            [enc setDepthBias:c.S.PolygonOffsetUnits slopeScale:c.S.PolygonOffsetFactor clamp:0.0f];
        else
            [enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];

        // Görünüm alanı / makas (bellek satırları GL satırlarıyla aynı); makas hedef sınırları içinde olmalı
        MTLViewport vp{ (double)c.S.Viewport[0], (double)c.S.Viewport[1], (double)std::max(1, c.S.Viewport[2]), (double)std::max(1, c.S.Viewport[3]), 0.0, 1.0 };
        [enc setViewport:vp];
        int sx = 0, sy = 0, sw = (int)c.RenderWidth, sh = (int)c.RenderHeight;
        if (c.S.ScissorTest)
        {
            sx = std::clamp(c.S.Scissor[0], 0, (int)c.RenderWidth);
            sy = std::clamp(c.S.Scissor[1], 0, (int)c.RenderHeight);
            sw = std::clamp(c.S.Scissor[0] + c.S.Scissor[2], 0, (int)c.RenderWidth) - sx;
            sh = std::clamp(c.S.Scissor[1] + c.S.Scissor[3], 0, (int)c.RenderHeight) - sy;
            if (sw <= 0 || sh <= 0)
                return;
        }
        [enc setScissorRect:(MTLScissorRect){ (NSUInteger)sx, (NSUInteger)sy, (NSUInteger)sw, (NSUInteger)sh }];

        OcclusionBeforeDraw(enc);
        const MTLPrimitiveType prim = ToPrimitive(mode);
        if (indexed)
        {
            Buffer* eb = GetBuffer(vao->ElementBuffer);
            if (!eb)
                return;
            if (indexType == GL_UNSIGNED_BYTE)
            {
                // 8 bit indeks Metal'de yok: 16 bit'e genişletilmiş halka kopyası
                const size_t start = (size_t)(uintptr_t)indices;
                if (start + (size_t)count > eb->Shadow.size())
                    return;
                MTL::UploadAlloc a = core.AllocUpload((uint64_t)count * 2, 4);
                if (!a.Cpu)
                    return;
                auto* wide = reinterpret_cast<uint16_t*>(a.Cpu);
                for (GLsizei i = 0; i < count; ++i)
                    wide[i] = eb->Shadow[start + (size_t)i];
                [enc drawIndexedPrimitives:prim indexCount:(NSUInteger)count indexType:MTLIndexTypeUInt16 indexBuffer:a.Buffer indexBufferOffset:a.Offset
                             instanceCount:(NSUInteger)instances baseVertex:baseVertex baseInstance:0];
                return;
            }
            uint64_t offset = 0;
            id<MTLBuffer> ib = BufferBinding(*eb, 0, offset);
            if (!ib)
                return;
            [enc drawIndexedPrimitives:prim
                            indexCount:(NSUInteger)count
                             indexType:indexType == GL_UNSIGNED_SHORT ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                           indexBuffer:ib
                     indexBufferOffset:offset + (uint64_t)(uintptr_t)indices
                         instanceCount:(NSUInteger)instances
                            baseVertex:baseVertex
                          baseInstance:0];
        }
        else
            [enc drawPrimitives:prim vertexStart:(NSUInteger)first vertexCount:(NSUInteger)count instanceCount:(NSUInteger)instances];
    }

    void GenerateMipmaps(Texture& t)
    {
        if (!t.Image || t.Samples > 1)
            return;
        const uint32_t full = GLEmu::MaxLevels(t.Width, t.Height);
        const uint32_t wanted = std::min<uint32_t>(full, (uint32_t)std::max(1, t.MaxLevel + 1));
        if (!t.Immutable && t.Levels < wanted)
            GrowLevels(t, wanted);
        if (t.Levels < 2 || t.Depth || t.Integer)
            return;
        EndRendering();
        [Core::Get().Blit() generateMipmapsForTexture:t.Image];
        t.DefinedLevels = t.Levels >= 32 ? ~0u : ((1u << t.Levels) - 1);
    }

    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
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
            const int x0 = std::min(sx0, sx1), y0 = std::min(sy0, sy1), x1 = std::max(sx0, sx1), y1 = std::max(sy0, sy1);
            if (sameSize && noFlip && src->Samples > 1 && dst->Samples == 1 && src->Format == dst->Format && x0 == 0 && y0 == 0 && x1 == sw && y1 == sh &&
                (int)std::max(1u, dst->Width >> dLevel) == sw && (int)std::max(1u, dst->Height >> dLevel) == sh)
            {
                // Çoklu örnek çözümleme: boş geçiş, saklama eylemi = çözümle
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = src->Image;
                pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
                pass.colorAttachments[0].resolveTexture = dst->Image;
                pass.colorAttachments[0].resolveLevel = (NSUInteger)dLevel;
                pass.colorAttachments[0].resolveSlice = (NSUInteger)dLayer;
                core.BeginRender(pass);
                core.EndEncoding();
                return;
            }
            if (sameSize && noFlip && src->Samples == dst->Samples && src->Samples == 1 && src->Format == dst->Format)
            {
                const int cx0 = std::clamp(x0, 0, sw), cy0 = std::clamp(y0, 0, sh), cx1 = std::clamp(x1, 0, sw), cy1 = std::clamp(y1, 0, sh);
                if (cx1 <= cx0 || cy1 <= cy0)
                    return;
                [core.Blit() copyFromTexture:src->Image
                                 sourceSlice:(NSUInteger)sLayer
                                 sourceLevel:(NSUInteger)sLevel
                                sourceOrigin:MTLOriginMake((NSUInteger)cx0, (NSUInteger)cy0, 0)
                                  sourceSize:MTLSizeMake((NSUInteger)(cx1 - cx0), (NSUInteger)(cy1 - cy0), 1)
                                   toTexture:dst->Image
                            destinationSlice:(NSUInteger)dLayer
                            destinationLevel:(NSUInteger)dLevel
                           destinationOrigin:MTLOriginMake((NSUInteger)(std::min(dx0, dx1) + (cx0 - x0)), (NSUInteger)(std::min(dy0, dy1) + (cy0 - y0)), 0)];
                return;
            }
            // Ölçekli / çevrilmiş / çözümlemeli kopya: yardımcı çizim
            if (dst->Integer || dst->Depth)
                return;
            float uv[4] = { (float)sx0 / (float)sw, (float)sy0 / (float)sh, (float)sx1 / (float)sw, (float)sy1 / (float)sh };
            if (dx1 < dx0) std::swap(uv[0], uv[2]);
            if (dy1 < dy0) std::swap(uv[1], uv[3]);
            HelperBlit(*dst, dLevel, dLayer, dx0, dy0, dx1, dy1, uv, src->Samples > 1 ? HelperPS::ColorMS : HelperPS::Color,
                       src->Samples > 1 ? src->Image : SampleView(*src), filter == GL_LINEAR);
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
                    [core.Blit() copyFromTexture:src->Image
                                     sourceSlice:0
                                     sourceLevel:0
                                    sourceOrigin:MTLOriginMake(0, 0, 0)
                                      sourceSize:MTLSizeMake((NSUInteger)sw, (NSUInteger)sh, 1)
                                       toTexture:dst->Image
                                destinationSlice:0
                                destinationLevel:0
                               destinationOrigin:MTLOriginMake(0, 0, 0)];
                else if (full && src->Samples > 1 && dst->Samples == 1 && src->Format == dst->Format)
                {
                    // Çoklu örnek derinlik çözümleme: örnek 0 (şablon çözümlenmez)
                    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                    pass.depthAttachment.texture = src->Image;
                    pass.depthAttachment.loadAction = MTLLoadActionLoad;
                    pass.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
                    pass.depthAttachment.resolveTexture = dst->Image;
                    pass.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
                    core.BeginRender(pass);
                    core.EndEncoding();
                }
                else
                    CQ_CORE_WARN("[MtlGL] Unsupported depth blit (scaled or format change)");
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
        id<CAMetalDrawable> drawable = core.Drawable();
        if (drawable && EnsureHelpers())
        {
            HelperTargets ht;
            ht.Color[0] = MTLPixelFormatBGRA8Unorm;
            ht.ColorCount = 1;
            id<MTLRenderPipelineState> pso = HelperPipeline(HelperPS::Present, ht);
            if (pso)
            {
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = drawable.texture;
                pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> enc = core.BeginRender(pass);
                MTLViewport vp{ 0.0, 0.0, (double)drawable.texture.width, (double)drawable.texture.height, 0.0, 1.0 };
                [enc setViewport:vp];
                // GL yönü (satır 0 = alt) -> ekran (satır 0 = üst): UV dikey ters
                HelperParams params{};
                params.UV[0] = 0.0f;
                params.UV[1] = 1.0f;
                params.UV[2] = 1.0f;
                params.UV[3] = 0.0f;
                HelperTriangle(enc, pso, params, SampleView(c.BackColor), true);
                core.EndEncoding();
            }
        }
        core.Present();
        c.Rendering = false;
        s_Bound = Bound{};
        BeginFrameResources();
    }

    void ReleaseDrawHelpers()
    {
        auto& c = Ctx();
        s_Helpers = Helpers{};
        c.Pipelines.clear();
        c.DepthStates.clear();
        s_Bound = Bound{};
        s_PendingClear = 0;
    }
}

#endif
