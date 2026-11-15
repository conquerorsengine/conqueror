#ifdef CQ_WEBGPU_ENABLED

// WebGPU çeviri katmanı: geçişler (hedefler, yükle / temizle), boru hattı önbelleği, bağlama grupları, çizim, temizleme, blit,
// mipmap ve sunum. Yardımcı çizimler (kısmi temizleme, ölçekli blit, mipmap, sunum çevirmesi) küçük WGSL gölgelendiricileriyle.

#include "WgpuGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Conqueror::WgpuGL
{
    using WGPU::W;
    using WGPU::Str;

    namespace
    {
        struct Hasher
        {
            uint64_t H = 1469598103934665603ull;
            void Add(uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                {
                    H ^= (v >> (i * 8)) & 0xFF;
                    H *= 1099511628211ull;
                }
            }
        };

        GLbitfield s_PendingClear = 0;
        Texture* s_ShowTexture = nullptr;                // CQ_WGPUGL_SHOW: gösterilecek ara görüntü
        uint64_t s_PassEpoch = 0;                        // her yeni geçişte artar (geçiş başına durum yeniden kurulur)
        std::unordered_map<uint64_t, WGPURenderPipeline> s_Pipelines;
        std::unordered_map<uint64_t, WGPUBindGroupLayout> s_Layouts;
        std::unordered_map<uint64_t, WGPUPipelineLayout> s_PipelineLayouts;
        WGPUBuffer s_NullVertex = nullptr;               // kapalı öznitelik: (0,0,0,1) float ve tamsayı
        WGPUBuffer s_ZeroUniform = nullptr;              // bağlanmamış blok: 64 KB sıfır

        // ── Yardımcı gölgelendiriciler ──
        const char* kHelperWgsl = R"(
struct P { color : vec4f, rect : vec4f, depth : f32, flip : f32, lod : f32, pad : f32 };
@group(0) @binding(0) var<uniform> u : P;
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var smp : sampler;
struct VO { @builtin(position) pos : vec4f, @location(0) uv : vec2f };
@vertex fn vs(@builtin(vertex_index) i : u32) -> VO {
    let q = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var o : VO;
    o.pos = vec4f(q.x * 2.0 - 1.0, q.y * 2.0 - 1.0, u.depth, 1.0);
    // NDC y = +1 görüntü alanının bellek satırı başı (GL yönünde alt): kaynak dikdörtgeni doğrusal eşlenir; flip: dikey ters
    let t = select(1.0 - q.y, q.y, u.flip > 0.5);
    o.uv = vec2f(mix(u.rect.x, u.rect.z, q.x), mix(u.rect.y, u.rect.w, t));
    return o;
}
@fragment fn fsBlit(v : VO) -> @location(0) vec4f { return textureSampleLevel(tex, smp, v.uv, u.lod); }
)";

        WGPUShaderModule s_HelperModule = nullptr;
        WGPUBindGroupLayout s_HelperLayout = nullptr;
        WGPUBindGroupLayout s_HelperLayoutUnfilt = nullptr;
        std::unordered_map<uint64_t, WGPURenderPipeline> s_HelperPipelines;
        std::unordered_map<uint32_t, WGPUShaderModule> s_ClearModules;    // hedef sayısı -> modül
        WGPUBindGroupLayout s_ClearLayout = nullptr;

        struct HelperParams { float Color[4]; float Rect[4]; float Depth; float Flip; float Lod; float Pad; };

        WGPUShaderModule Module(const std::string& code)
        {
            WGPUShaderSourceWGSL src{};
            src.chain.sType = WGPUSType_ShaderSourceWGSL;
            src.code = Str(code.c_str());
            WGPUShaderModuleDescriptor d{};
            d.nextInChain = &src.chain;
            return W().DeviceCreateShaderModule(Device(), &d);
        }

        WGPUBindGroupLayout HelperLayout(bool filtering)
        {
            WGPUBindGroupLayout& l = filtering ? s_HelperLayout : s_HelperLayoutUnfilt;
            if (l)
                return l;
            WGPUBindGroupLayoutEntry e[3]{};
            e[0].binding = 0;
            e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            e[0].buffer.type = WGPUBufferBindingType_Uniform;
            e[1].binding = 1;
            e[1].visibility = WGPUShaderStage_Fragment;
            e[1].texture.sampleType = filtering ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
            e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
            e[2].binding = 2;
            e[2].visibility = WGPUShaderStage_Fragment;
            e[2].sampler.type = filtering ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
            WGPUBindGroupLayoutDescriptor d{};
            d.entryCount = 3;
            d.entries = e;
            l = W().DeviceCreateBindGroupLayout(Device(), &d);
            return l;
        }

        WGPURenderPipeline HelperBlitPipeline(WGPUTextureFormat format, uint32_t samples, bool filtering)
        {
            Hasher h;
            h.Add((uint64_t)format);
            h.Add(samples);
            h.Add(filtering);
            if (auto it = s_HelperPipelines.find(h.H); it != s_HelperPipelines.end())
                return it->second;
            if (!s_HelperModule)
                s_HelperModule = Module(kHelperWgsl);
            WGPUBindGroupLayout bgl = HelperLayout(filtering);
            WGPUPipelineLayoutDescriptor pld{};
            pld.bindGroupLayoutCount = 1;
            pld.bindGroupLayouts = &bgl;
            WGPUPipelineLayout pl = W().DeviceCreatePipelineLayout(Device(), &pld);
            WGPUColorTargetState ct{};
            ct.format = format;
            ct.writeMask = WGPUColorWriteMask_All;
            WGPUFragmentState fs{};
            fs.module = s_HelperModule;
            fs.entryPoint = Str("fsBlit");
            fs.targetCount = 1;
            fs.targets = &ct;
            WGPURenderPipelineDescriptor d{};
            d.layout = pl;
            d.vertex.module = s_HelperModule;
            d.vertex.entryPoint = Str("vs");
            d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            d.multisample.count = samples;
            d.multisample.mask = 0xFFFFFFFF;
            d.fragment = &fs;
            WGPURenderPipeline p = W().DeviceCreateRenderPipeline(Device(), &d);
            W().PipelineLayoutRelease(pl);
            s_HelperPipelines[h.H] = p;
            return p;
        }

        // Kaynak görünümden hedef görünüme dikdörtgen çizimi (ölçekli blit, mipmap, sunum)
        void HelperBlit(WGPUTextureView srcView, bool filtering, bool linear, const float rect[4], bool flip, float lod, WGPUTextureView dstView,
                        WGPUTextureFormat dstFormat, int vx, int vy, int vw, int vh)
        {
            WGPURenderPipeline pipe = HelperBlitPipeline(dstFormat, 1, filtering);
            if (!pipe)
                return;
            HelperParams hp{};
            std::memcpy(hp.Rect, rect, sizeof(hp.Rect));
            hp.Flip = flip ? 1.0f : 0.0f;
            hp.Lod = lod;
            uint64_t uoff = 0;
            WGPUBuffer ub = RingWrite(&hp, sizeof(hp), 256, uoff);
            Texture sp;
            sp.MinFilter = sp.MagFilter = (linear && filtering) ? GL_LINEAR : GL_NEAREST;
            sp.WrapS = sp.WrapT = GL_CLAMP_TO_EDGE;
            WGPUBindGroupEntry be[3]{};
            be[0].binding = 0;
            be[0].buffer = ub;
            be[0].offset = uoff;
            be[0].size = sizeof(hp);
            be[1].binding = 1;
            be[1].textureView = srcView;
            be[2].binding = 2;
            be[2].sampler = GetSampler(sp, filtering, false);
            WGPUBindGroupDescriptor bgd{};
            bgd.layout = HelperLayout(filtering);
            bgd.entryCount = 3;
            bgd.entries = be;
            WGPUBindGroup bg = W().DeviceCreateBindGroup(Device(), &bgd);
            WGPURenderPassColorAttachment ca{};
            ca.view = dstView;
            ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load;
            ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &ca;
            WGPURenderPassEncoder pass = BeginPass(rp);
            ++s_PassEpoch;
            W().RenderPassEncoderSetPipeline(pass, pipe);
            W().RenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
            W().RenderPassEncoderSetViewport(pass, (float)vx, (float)vy, (float)vw, (float)vh, 0.0f, 1.0f);
            W().RenderPassEncoderDraw(pass, 3, 1, 0, 0);
            EndPass();
            W().BindGroupRelease(bg);
        }

        WGPUTextureView SingleView(Texture& t, int level, int layer, WGPUTextureUsage usage)
        {
            WGPUTextureViewDescriptor vd{};
            vd.format = t.Depth ? DepthAspectFormat(t) : t.Format;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.baseMipLevel = (uint32_t)level;
            vd.mipLevelCount = 1;
            vd.baseArrayLayer = (uint32_t)layer;
            vd.arrayLayerCount = 1;
            vd.aspect = t.Depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
            vd.usage = usage;
            return W().TextureCreateView(t.Image, &vd);
        }

        // ── Hedefler ──
        struct Targets
        {
            Texture* Color[8] = {};
            int ColorLevel[8] = {}, ColorLayer[8] = {};
            int ColorCount = 0;
            Texture* Depth = nullptr;
            int DepthLevel = 0, DepthLayer = 0;
        };

        Targets CollectTargets(uint32_t fbo)
        {
            auto& c = Ctx();
            Targets t;
            Framebuffer* fb = GetFramebuffer(fbo);
            if (!fb)
                return t;
            for (int i = 0; i < fb->NumDrawBuffers && i < 8; ++i)
            {
                const GLenum db = fb->DrawBuffers[i];
                if (db == GL_NONE)
                    continue;
                const int idx = fbo == 0 ? 0 : (int)(db - GL_COLOR_ATTACHMENT0);
                if (idx < 0 || idx >= 8)
                    continue;
                const Attachment& a = fb->Color[idx];
                t.Color[i] = a.Valid() ? AttachmentTexture(a) : nullptr;
                t.ColorLevel[i] = a.Level;
                t.ColorLayer[i] = a.Layer;
                t.ColorCount = i + 1;
            }
            const Attachment& d = fb->Depth.Valid() ? fb->Depth : fb->Stencil;
            if (d.Valid())
            {
                t.Depth = AttachmentTexture(d);
                t.DepthLevel = d.Level;
                t.DepthLayer = d.Layer;
            }
            (void)c;
            return t;
        }

        WGPUVertexFormat AttribFormat(const Attrib& a, uint32_t& bytes)
        {
            const int n = std::clamp((int)a.Size, 1, 4);
            auto pick = [&](WGPUVertexFormat f1, WGPUVertexFormat f2, WGPUVertexFormat f4, uint32_t comp) {
                bytes = comp * (uint32_t)(n == 3 ? 4 : n);
                return n == 1 ? f1 : n == 2 ? f2 : f4;
            };
            switch (a.Type)
            {
                case GL_UNSIGNED_BYTE:
                    if (a.Integer) return pick(WGPUVertexFormat_Uint8, WGPUVertexFormat_Uint8x2, WGPUVertexFormat_Uint8x4, 1);
                    return a.Normalized ? pick(WGPUVertexFormat_Unorm8, WGPUVertexFormat_Unorm8x2, WGPUVertexFormat_Unorm8x4, 1)
                                        : pick(WGPUVertexFormat_Uint8, WGPUVertexFormat_Uint8x2, WGPUVertexFormat_Uint8x4, 1);
                case GL_BYTE:
                    if (a.Integer) return pick(WGPUVertexFormat_Sint8, WGPUVertexFormat_Sint8x2, WGPUVertexFormat_Sint8x4, 1);
                    return a.Normalized ? pick(WGPUVertexFormat_Snorm8, WGPUVertexFormat_Snorm8x2, WGPUVertexFormat_Snorm8x4, 1)
                                        : pick(WGPUVertexFormat_Sint8, WGPUVertexFormat_Sint8x2, WGPUVertexFormat_Sint8x4, 1);
                case GL_UNSIGNED_SHORT:
                    if (a.Integer) return pick(WGPUVertexFormat_Uint16, WGPUVertexFormat_Uint16x2, WGPUVertexFormat_Uint16x4, 2);
                    return a.Normalized ? pick(WGPUVertexFormat_Unorm16, WGPUVertexFormat_Unorm16x2, WGPUVertexFormat_Unorm16x4, 2)
                                        : pick(WGPUVertexFormat_Uint16, WGPUVertexFormat_Uint16x2, WGPUVertexFormat_Uint16x4, 2);
                case GL_SHORT:
                    if (a.Integer) return pick(WGPUVertexFormat_Sint16, WGPUVertexFormat_Sint16x2, WGPUVertexFormat_Sint16x4, 2);
                    return a.Normalized ? pick(WGPUVertexFormat_Snorm16, WGPUVertexFormat_Snorm16x2, WGPUVertexFormat_Snorm16x4, 2)
                                        : pick(WGPUVertexFormat_Sint16, WGPUVertexFormat_Sint16x2, WGPUVertexFormat_Sint16x4, 2);
                case GL_HALF_FLOAT: return pick(WGPUVertexFormat_Float16, WGPUVertexFormat_Float16x2, WGPUVertexFormat_Float16x4, 2);
                case GL_INT:
                    bytes = 4u * (uint32_t)n;
                    return n == 1 ? WGPUVertexFormat_Sint32 : n == 2 ? WGPUVertexFormat_Sint32x2 : n == 3 ? WGPUVertexFormat_Sint32x3 : WGPUVertexFormat_Sint32x4;
                case GL_UNSIGNED_INT:
                    bytes = 4u * (uint32_t)n;
                    return n == 1 ? WGPUVertexFormat_Uint32 : n == 2 ? WGPUVertexFormat_Uint32x2 : n == 3 ? WGPUVertexFormat_Uint32x3 : WGPUVertexFormat_Uint32x4;
                default:
                    bytes = 4u * (uint32_t)n;
                    return n == 1 ? WGPUVertexFormat_Float32 : n == 2 ? WGPUVertexFormat_Float32x2 : n == 3 ? WGPUVertexFormat_Float32x3 : WGPUVertexFormat_Float32x4;
            }
        }

        WGPUBlendFactor ToBlend(GLenum f)
        {
            switch (f)
            {
                case GL_ZERO: return WGPUBlendFactor_Zero;
                case GL_ONE: return WGPUBlendFactor_One;
                case GL_SRC_COLOR: return WGPUBlendFactor_Src;
                case GL_ONE_MINUS_SRC_COLOR: return WGPUBlendFactor_OneMinusSrc;
                case GL_DST_COLOR: return WGPUBlendFactor_Dst;
                case GL_ONE_MINUS_DST_COLOR: return WGPUBlendFactor_OneMinusDst;
                case GL_SRC_ALPHA: return WGPUBlendFactor_SrcAlpha;
                case GL_ONE_MINUS_SRC_ALPHA: return WGPUBlendFactor_OneMinusSrcAlpha;
                case GL_DST_ALPHA: return WGPUBlendFactor_DstAlpha;
                case GL_ONE_MINUS_DST_ALPHA: return WGPUBlendFactor_OneMinusDstAlpha;
                case GL_CONSTANT_COLOR:
                case GL_CONSTANT_ALPHA: return WGPUBlendFactor_Constant;
                case GL_ONE_MINUS_CONSTANT_COLOR:
                case GL_ONE_MINUS_CONSTANT_ALPHA: return WGPUBlendFactor_OneMinusConstant;
                case GL_SRC_ALPHA_SATURATE: return WGPUBlendFactor_SrcAlphaSaturated;
                default: return WGPUBlendFactor_One;
            }
        }

        WGPUBlendOperation ToBlendOp(GLenum e)
        {
            switch (e)
            {
                case GL_FUNC_SUBTRACT: return WGPUBlendOperation_Subtract;
                case GL_FUNC_REVERSE_SUBTRACT: return WGPUBlendOperation_ReverseSubtract;
                case GL_MIN: return WGPUBlendOperation_Min;
                case GL_MAX: return WGPUBlendOperation_Max;
                default: return WGPUBlendOperation_Add;
            }
        }

        WGPUCompareFunction ToCompare(GLenum f)
        {
            switch (f)
            {
                case GL_NEVER: return WGPUCompareFunction_Never;
                case GL_LESS: return WGPUCompareFunction_Less;
                case GL_EQUAL: return WGPUCompareFunction_Equal;
                case GL_LEQUAL: return WGPUCompareFunction_LessEqual;
                case GL_GREATER: return WGPUCompareFunction_Greater;
                case GL_NOTEQUAL: return WGPUCompareFunction_NotEqual;
                case GL_GEQUAL: return WGPUCompareFunction_GreaterEqual;
                default: return WGPUCompareFunction_Always;
            }
        }

        WGPUStencilOperation ToStencilOp(GLenum o)
        {
            switch (o)
            {
                case GL_ZERO: return WGPUStencilOperation_Zero;
                case GL_REPLACE: return WGPUStencilOperation_Replace;
                case GL_INCR: return WGPUStencilOperation_IncrementClamp;
                case GL_DECR: return WGPUStencilOperation_DecrementClamp;
                case GL_INVERT: return WGPUStencilOperation_Invert;
                case GL_INCR_WRAP: return WGPUStencilOperation_IncrementWrap;
                case GL_DECR_WRAP: return WGPUStencilOperation_DecrementWrap;
                default: return WGPUStencilOperation_Keep;
            }
        }

        bool BoundAsTarget(const Texture* t)
        {
            auto& c = Ctx();
            if (t == c.RenderDepth)
                return true;
            for (uint32_t i = 0; i < c.RenderColorCount; ++i)
                if (c.RenderColor[i] == t)
                    return true;
            return false;
        }

        // GL üçgen yelpazesi / çizgi döngüsü WebGPU'da yok: liste / şerit dizinine çevrilir
        std::vector<uint32_t> ExpandIndices(GLenum mode, const std::vector<uint32_t>& src)
        {
            std::vector<uint32_t> out;
            if (mode == GL_TRIANGLE_FAN)
            {
                for (size_t i = 1; i + 1 < src.size(); ++i)
                {
                    out.push_back(src[0]);
                    out.push_back(src[i]);
                    out.push_back(src[i + 1]);
                }
            }
            else if (mode == GL_LINE_LOOP)
            {
                out = src;
                if (!src.empty())
                    out.push_back(src[0]);
            }
            return out;
        }
    }

    bool Tracing()
    {
        // "N": ilk N kare, "A-B": A..B kareleri
        static const std::pair<int, int> s_Range = [] {
            const char* v = std::getenv("CQ_WGPUGL_TRACE");
            if (!v || !*v)
                return std::pair<int, int>{ 0, -1 };
            const char* dash = std::strchr(v, '-');
            return dash ? std::pair<int, int>{ std::atoi(v), std::atoi(dash + 1) } : std::pair<int, int>{ 1, std::atoi(v) };
        }();
        const int f = (int)FrameSerial();
        return f >= s_Range.first && f <= s_Range.second;
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
        EndPass();
    }

    void FlushAndWaitGL()
    {
        if (!Device())
            return;
        EndRendering();
        WaitIdle();
    }

    bool EnsureRendering()
    {
        auto& c = Ctx();
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
        if (!s_PendingClear && c.Rendering && c.RenderingKey == h.H && RenderPass())
            return true;

        WGPURenderPassColorAttachment ca[8]{};
        WGPUTextureView views[9]{};
        uint32_t count = 0;
        for (int i = 0; i < t.ColorCount; ++i)
        {
            ca[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            if (!t.Color[i])
                continue;   // view = nullptr: boş yuva
            views[i] = TargetView(*t.Color[i], t.ColorLevel[i], t.ColorLayer[i]);
            ca[i].view = views[i];
            const bool clear = (s_PendingClear & GL_COLOR_BUFFER_BIT) != 0;
            ca[i].loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
            ca[i].storeOp = WGPUStoreOp_Store;
            ca[i].clearValue = { c.S.ClearColor[0], c.S.ClearColor[1], c.S.ClearColor[2], c.S.ClearColor[3] };
            t.Color[i]->DefinedLevels |= 1u << t.ColorLevel[i];
            count = (uint32_t)i + 1;
        }
        WGPURenderPassDepthStencilAttachment da{};
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = count;
        rp.colorAttachments = ca;
        if (t.Depth)
        {
            WGPUTextureViewDescriptor vd{};
            vd.format = t.Depth->Format;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.baseMipLevel = (uint32_t)t.DepthLevel;
            vd.mipLevelCount = 1;
            vd.baseArrayLayer = (uint32_t)t.DepthLayer;
            vd.arrayLayerCount = 1;
            vd.aspect = WGPUTextureAspect_All;
            vd.usage = WGPUTextureUsage_RenderAttachment;
            views[8] = W().TextureCreateView(t.Depth->Image, &vd);
            da.view = views[8];
            const bool clearDepth = (s_PendingClear & GL_DEPTH_BUFFER_BIT) != 0;
            da.depthLoadOp = clearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
            da.depthStoreOp = WGPUStoreOp_Store;
            da.depthClearValue = std::clamp(c.S.ClearDepth, 0.0f, 1.0f);
            if (t.Depth->Stencil)
            {
                const bool clearStencil = (s_PendingClear & GL_STENCIL_BUFFER_BIT) != 0;
                da.stencilLoadOp = clearStencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
                da.stencilStoreOp = WGPUStoreOp_Store;
                da.stencilClearValue = (uint32_t)(c.S.ClearStencil & 0xFF);
            }
            rp.depthStencilAttachment = &da;
        }
        if (Tracing())
        {
            std::string desc;
            for (int i = 0; i < t.ColorCount; ++i)
                desc += t.Color[i] ? " c" + std::to_string(i) + "=" + std::to_string((int)t.Color[i]->Format) + (ca[i].loadOp == WGPULoadOp_Clear ? "(clear)" : "") : "";
            if (t.Depth)
                desc += " d=" + std::to_string((int)t.Depth->Format) + (da.depthLoadOp == WGPULoadOp_Clear ? "(clear)" : "");
            CQ_CORE_INFO("[WgpuGL trace f{0}] pass fbo {1} {2}x{3}{4}", FrameSerial(), c.S.DrawFBO, width, height, desc);
        }
        s_PendingClear = 0;
        WGPURenderPassEncoder pass = BeginPass(rp);
        for (auto* v : views)
            if (v)
                W().TextureViewRelease(v);
        if (!pass)
            return false;
        ++s_PassEpoch;
        for (int i = 0; i < 8; ++i)
        {
            const bool live = i < t.ColorCount && t.Color[i];
            c.RenderColor[i] = live ? t.Color[i] : nullptr;
            c.RenderColorLevel[i] = t.ColorLevel[i];
            c.RenderColorLayer[i] = t.ColorLayer[i];
        }
        c.RenderColorCount = (uint32_t)t.ColorCount;
        c.RenderDepth = t.Depth;
        c.RenderDepthLevel = t.DepthLevel;
        c.RenderDepthLayer = t.DepthLayer;
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
        if (!full || (!fullMask && (mask & GL_COLOR_BUFFER_BIT)))
        {
            // Kısmi temizleme (makas / maske): WebGPU'da geçiş temizlemesi tüm hedefi kapsar. Bu sürümde yalnız tam temizleme
            // geçiş yükleme eylemiyle yapılır; kısmi durumda da tüm hedef temizlenir (nadiren kullanılır)
            static bool s_Warned = false;
            if (!s_Warned)
            {
                s_Warned = true;
                CQ_CORE_WARN("[WgpuGL] Scissored / masked clears clear the whole target");
            }
        }
        s_PendingClear = mask;
        c.Rendering = false;
        EndPass();
        EnsureRendering();
    }

    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex)
    {
        auto& c = Ctx();
        if (count <= 0 || instances <= 0)
            return;
        Program* p = GetProgram(c.S.Program);
        if (!p || !p->Linked)
            return;
        const bool triangles = mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
        if (c.S.CullFace && c.S.CullMode == GL_FRONT_AND_BACK && triangles)
            return;
        if (!EnsureRendering())
            return;
        WGPURenderPassEncoder pass = RenderPass();
        VertexArray* vao = CurrentVAO();
        const uint64_t submit = SubmitSerial();

        // ── Köşe düzeni: aynı arabellek + adım + bölen öznitelikleri tek WebGPU arabelleğinde ──
        struct VBuf { WGPUBuffer Buffer = nullptr; uint64_t Offset = 0; uint64_t Size = 0; uint32_t Stride = 0; bool Instance = false; std::vector<WGPUVertexAttribute> Attrs; uint32_t GLBuffer = 0; uint64_t Base = 0; };
        std::vector<VBuf> vbufs;
        std::vector<WGPUVertexAttribute> nullAttrs;
        Hasher layoutKey;
        for (const auto& in : p->Inputs)
        {
            if (in.Location < 0 || in.Location >= 16)
                continue;
            const Attrib& a = vao->Attribs[in.Location];
            Buffer* buf = a.Enabled ? GetBuffer(a.Buffer) : nullptr;
            if (!buf)
            {
                WGPUVertexAttribute va{};
                va.shaderLocation = (uint32_t)in.Location;
                va.format = in.Integer ? WGPUVertexFormat_Sint32x4 : WGPUVertexFormat_Float32x4;
                va.offset = in.Integer ? 16 : 0;
                nullAttrs.push_back(va);
                layoutKey.Add(0xF0000000ull | (uint64_t)in.Location);
                continue;
            }
            uint32_t bytes = 4;
            WGPUVertexAttribute va{};
            va.shaderLocation = (uint32_t)in.Location;
            va.format = AttribFormat(a, bytes);
            const uint32_t stride = a.Stride ? (uint32_t)a.Stride : bytes;
            VBuf* slot = nullptr;
            for (auto& vb : vbufs)
                if (vb.GLBuffer == a.Buffer && vb.Stride == stride && vb.Instance == (a.Divisor != 0) && a.Offset >= vb.Base &&
                    a.Offset - vb.Base + bytes <= stride)
                    slot = &vb;
            if (!slot)
            {
                vbufs.push_back({});
                slot = &vbufs.back();
                slot->GLBuffer = a.Buffer;
                slot->Stride = stride;
                slot->Instance = a.Divisor != 0;
                slot->Base = a.Offset;
                uint64_t off = 0, size = 0;
                slot->Buffer = BufferBinding(*buf, 0, off, &size);
                slot->Offset = off + std::min<uint64_t>(a.Offset, size);
                slot->Size = size;
            }
            va.offset = a.Offset - slot->Base;
            slot->Attrs.push_back(va);
        }
        if (vbufs.size() + (nullAttrs.empty() ? 0 : 1) > 8)
        {
            static bool s_Warned = false;
            if (!s_Warned)
            {
                s_Warned = true;
                CQ_CORE_WARN("[WgpuGL] A draw needs more than 8 vertex buffers (WebGPU limit); it is skipped");
            }
            return;
        }
        std::vector<WGPUVertexBufferLayout> layouts;
        for (auto& vb : vbufs)
        {
            WGPUVertexBufferLayout l{};
            l.arrayStride = vb.Stride;
            l.stepMode = vb.Instance ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
            l.attributeCount = vb.Attrs.size();
            l.attributes = vb.Attrs.data();
            layouts.push_back(l);
            layoutKey.Add(((uint64_t)vb.Stride << 32) | ((uint64_t)vb.Instance << 31) | vb.Attrs.size());
            for (const auto& va : vb.Attrs)
                layoutKey.Add(((uint64_t)va.shaderLocation << 48) | ((uint64_t)va.format << 24) | va.offset);
        }
        if (!nullAttrs.empty())
        {
            WGPUVertexBufferLayout l{};
            l.arrayStride = 0;   // her köşe aynı öğe: GL'in kapalı öznitelik değeri
            l.stepMode = WGPUVertexStepMode_Vertex;
            l.attributeCount = nullAttrs.size();
            l.attributes = nullAttrs.data();
            layouts.push_back(l);
        }

        // ── Bağlama grubu düzeni (bağlı dokuların türüne göre) + kaynaklar ──
        std::vector<WGPUBindGroupLayoutEntry> le;
        std::vector<WGPUBindGroupEntry> be;
        Hasher bgKey;
        auto addBuffer = [&](uint32_t binding, WGPUBuffer buf, uint64_t off, uint64_t size) {
            const uint32_t vis = binding < 64 && p->BlockVisibility[binding] ? p->BlockVisibility[binding] : (WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
            WGPUBindGroupLayoutEntry e{};
            e.binding = binding;
            e.visibility = (WGPUShaderStage)vis;
            e.buffer.type = WGPUBufferBindingType_Uniform;
            le.push_back(e);
            WGPUBindGroupEntry g{};
            g.binding = binding;
            g.buffer = buf;
            g.offset = off;
            g.size = size;
            be.push_back(g);
            bgKey.Add(((uint64_t)binding << 32) | vis);
        };
        for (const auto& blk : p->Blocks)
        {
            const uint32_t glb = blk.GLBinding < 64 ? blk.GLBinding : 0;
            Buffer* ub = GetBuffer(c.S.UBOBindings[glb]);
            uint64_t off = 0, size = 0;
            WGPUBuffer b = ub ? BufferBinding(*ub, blk.Size, off, &size) : nullptr;
            const uint64_t bindSize = std::max<uint64_t>(blk.Size, 16);
            if (b)
            {
                const uint64_t extra = c.S.UBOOffsets[glb] > 0 ? (uint64_t)c.S.UBOOffsets[glb] : 0;
                off += extra;
                if (off % 256 != 0 || extra + bindSize > size)
                    b = nullptr;   // hizasız / taşan aralık: sıfır blok
            }
            if (!b)
            {
                b = s_ZeroUniform;
                off = 0;
            }
            addBuffer(blk.Register, b, off, bindSize);
        }
        if (p->GlobalsRegister >= 0)
        {
            const uint64_t gsize = std::max<uint64_t>(p->Globals.size(), 16);
            if (p->GlobalsDirty || p->GlobalsFrame != submit || !p->GlobalsBuffer)
            {
                if (p->Globals.size() < gsize)
                    p->Globals.resize((size_t)gsize, 0);
                p->GlobalsBuffer = RingWrite(p->Globals.data(), gsize, 256, p->GlobalsOffset);
                p->GlobalsFrame = submit;
                p->GlobalsDirty = false;
            }
            addBuffer((uint32_t)p->GlobalsRegister, p->GlobalsBuffer, p->GlobalsOffset, gsize);
        }
        std::vector<WGPUSampler> samplers;
        for (const auto& slot : p->Textures)
        {
            const SamplerBinding& sb = p->Samplers[(size_t)slot.SamplerIndex];
            // Doku birimi çizim anında (glUniform1i bağlamadan sonra da değişebilir)
            const int unit = std::clamp((size_t)slot.ArrayElement < sb.Units.size() ? sb.Units[(size_t)slot.ArrayElement] : 0, 0, 63);
            const uint32_t texName = slot.Target == GL_TEXTURE_CUBE_MAP ? c.S.TexCube[(size_t)unit] :
                                     slot.Target == GL_TEXTURE_2D_MULTISAMPLE ? c.S.Tex2DMS[(size_t)unit] : c.S.Tex2D[(size_t)unit];
            Texture* tex = GetTexture(texName);
            const bool cube = slot.Target == GL_TEXTURE_CUBE_MAP;
            if (!tex || !tex->Image || cube != (tex->Layers == 6) || tex->Integer != slot.Integer || BoundAsTarget(tex) ||
                (slot.Shadow && !tex->Depth) || (slot.Multisampled != (tex->Samples > 1)))
                tex = cube ? c.NullTextureCube : slot.Integer ? c.NullTextureInt : slot.Shadow ? c.NullTextureDepth : c.NullTexture2D;
            if (slot.Multisampled && tex->Samples == 1)
                continue;   // çok örnekli boş doku yok: çizim düzeni geçersiz kalır (nadiren)
            tex->LastUseSubmit = submit;
            const Texture* params = tex;
            if (auto so = c.SamplerObjects.find(c.S.Samplers[(size_t)unit]); so != c.SamplerObjects.end() && c.S.Samplers[(size_t)unit])
                params = so->second.get();
            WGPUTextureSampleType st = slot.Shadow ? WGPUTextureSampleType_Depth :
                                       tex->Integer ? (tex->Kind == GLEmu::TexelKind::R32UI || tex->Kind == GLEmu::TexelKind::R8UI ? WGPUTextureSampleType_Uint
                                                                                                                                   : WGPUTextureSampleType_Sint) :
                                       (tex->Filterable ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat);
            WGPUBindGroupLayoutEntry e{};
            e.binding = slot.Binding;
            e.visibility = slot.Visibility;
            e.texture.sampleType = st;
            e.texture.viewDimension = cube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
            e.texture.multisampled = tex->Samples > 1;
            le.push_back(e);
            WGPUBindGroupEntry g{};
            g.binding = slot.Binding;
            g.textureView = SampleView(*tex);
            be.push_back(g);
            bgKey.Add(((uint64_t)slot.Binding << 32) | ((uint64_t)st << 8) | (cube ? 1u : 0u) | (tex->Samples > 1 ? 2u : 0u) | ((uint64_t)slot.Visibility << 16));
            if (slot.SamplerBinding >= 0)
            {
                const bool filtering = st == WGPUTextureSampleType_Float;
                const bool comparison = st == WGPUTextureSampleType_Depth;
                const bool alreadyAdded = std::any_of(le.begin(), le.end(), [&](const WGPUBindGroupLayoutEntry& x) { return x.binding == (uint32_t)slot.SamplerBinding; });
                if (!alreadyAdded)
                {
                    WGPUBindGroupLayoutEntry se{};
                    se.binding = (uint32_t)slot.SamplerBinding;
                    se.visibility = slot.Visibility;
                    se.sampler.type = comparison ? WGPUSamplerBindingType_Comparison : filtering ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
                    le.push_back(se);
                    WGPUBindGroupEntry sg{};
                    sg.binding = (uint32_t)slot.SamplerBinding;
                    sg.sampler = GetSampler(*params, filtering, comparison);
                    be.push_back(sg);
                    bgKey.Add(((uint64_t)slot.SamplerBinding << 32) | 0x8000 | ((uint64_t)se.sampler.type << 4) | ((uint64_t)slot.Visibility << 16));
                }
            }
        }
        WGPUBindGroupLayout& bgl = s_Layouts[bgKey.H];
        if (!bgl)
        {
            WGPUBindGroupLayoutDescriptor d{};
            d.entryCount = le.size();
            d.entries = le.data();
            bgl = W().DeviceCreateBindGroupLayout(Device(), &d);
        }
        WGPUPipelineLayout& pl = s_PipelineLayouts[bgKey.H];
        if (!pl)
        {
            WGPUPipelineLayoutDescriptor d{};
            d.bindGroupLayoutCount = 1;
            d.bindGroupLayouts = &bgl;
            pl = W().DeviceCreatePipelineLayout(Device(), &d);
        }

        // ── Dizin / topoloji ──
        WGPUPrimitiveTopology topo = WGPUPrimitiveTopology_TriangleList;
        switch (mode)
        {
            case GL_POINTS: topo = WGPUPrimitiveTopology_PointList; break;
            case GL_LINES: topo = WGPUPrimitiveTopology_LineList; break;
            case GL_LINE_STRIP: topo = WGPUPrimitiveTopology_LineStrip; break;
            case GL_LINE_LOOP: topo = WGPUPrimitiveTopology_LineStrip; break;
            case GL_TRIANGLE_STRIP: topo = WGPUPrimitiveTopology_TriangleStrip; break;
            default: topo = WGPUPrimitiveTopology_TriangleList; break;
        }
        const bool expand = mode == GL_TRIANGLE_FAN || mode == GL_LINE_LOOP;
        WGPUBuffer indexBuffer = nullptr;
        uint64_t indexOffset = 0;
        WGPUIndexFormat indexFormat = WGPUIndexFormat_Undefined;
        uint32_t drawCount = (uint32_t)count;
        bool useIndices = indexed;
        if (indexed || expand)
        {
            std::vector<uint32_t> idx;
            bool needCpu = expand || indexType == GL_UNSIGNED_BYTE;
            Buffer* eb = GetBuffer(vao->ElementBuffer ? vao->ElementBuffer : c.S.ElementBufferNoVAO);
            const uint64_t byteOffset = (uint64_t)(uintptr_t)indices;
            if (indexed && !eb)
                needCpu = true;   // istemci tarafı dizinler
            if (needCpu)
            {
                const uint8_t* srcData = indexed ? (eb ? eb->Shadow.data() + std::min<uint64_t>(byteOffset, eb->Shadow.size()) : (const uint8_t*)indices) : nullptr;
                idx.resize((size_t)count);
                for (GLsizei i = 0; i < count; ++i)
                {
                    if (!indexed)
                        idx[(size_t)i] = (uint32_t)(first + i);
                    else if (indexType == GL_UNSIGNED_BYTE)
                        idx[(size_t)i] = srcData[i];
                    else if (indexType == GL_UNSIGNED_SHORT)
                    {
                        uint16_t v;
                        std::memcpy(&v, srcData + 2 * (size_t)i, 2);
                        idx[(size_t)i] = v;
                    }
                    else
                    {
                        uint32_t v;
                        std::memcpy(&v, srcData + 4 * (size_t)i, 4);
                        idx[(size_t)i] = v;
                    }
                }
                if (expand)
                    idx = ExpandIndices(mode, idx);
                if (idx.empty())
                    return;
                indexBuffer = RingWrite(idx.data(), idx.size() * 4, 4, indexOffset);
                indexFormat = WGPUIndexFormat_Uint32;
                drawCount = (uint32_t)idx.size();
                useIndices = true;
            }
            else
            {
                uint64_t off = 0, size = 0;
                indexBuffer = BufferBinding(*eb, 0, off, &size);
                const uint32_t isz = indexType == GL_UNSIGNED_SHORT ? 2 : 4;
                indexOffset = off + byteOffset;
                if (indexOffset % isz != 0)
                    return;
                indexFormat = indexType == GL_UNSIGNED_SHORT ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
            }
        }
        const bool strip = topo == WGPUPrimitiveTopology_LineStrip || topo == WGPUPrimitiveTopology_TriangleStrip;

        // ── Boru hattı ──
        Hasher xk;
        xk.Add(layoutKey.H);
        xk.Add(p->Id);
        xk.Add(bgKey.H);
        xk.Add((uint64_t)topo | ((uint64_t)(strip && useIndices ? indexFormat : 0) << 8));
        xk.Add(c.S.Blend ? 1 : 0);
        if (c.S.Blend)
        {
            xk.Add(c.S.BlendSrcRGB); xk.Add(c.S.BlendDstRGB); xk.Add(c.S.BlendSrcA); xk.Add(c.S.BlendDstA);
            xk.Add(c.S.BlendEqRGB); xk.Add(c.S.BlendEqA);
        }
        for (uint8_t m : c.S.ColorMaskBits) xk.Add(m);
        for (uint32_t i = 0; i < 8; ++i)
            xk.Add(i < c.RenderColorCount && c.RenderColor[i] ? (uint64_t)c.RenderColor[i]->Format : 0);
        xk.Add(c.RenderDepth ? (uint64_t)c.RenderDepth->Format : 0);
        xk.Add(c.RenderingSamples);
        xk.Add(((uint64_t)c.S.DepthTest << 1) | (uint64_t)c.S.DepthMask | ((uint64_t)c.S.DepthFunc << 8));
        xk.Add(((uint64_t)c.S.CullFace << 1) | ((uint64_t)c.S.CullMode << 4) | ((uint64_t)c.S.FrontFace << 24));
        xk.Add(c.S.StencilTest ? ((uint64_t)c.S.StencilFunc | ((uint64_t)c.S.StencilFail << 16) | ((uint64_t)c.S.StencilZFail << 32) |
                                  ((uint64_t)c.S.StencilZPass << 48)) : 0);
        xk.Add(c.S.StencilTest ? ((uint64_t)(c.S.StencilValueMask & 0xFF) | ((uint64_t)(c.S.StencilWriteMask & 0xFF) << 8)) : 0);
        if (c.S.PolygonOffsetFill)
        {
            uint32_t f, u;
            std::memcpy(&f, &c.S.PolygonOffsetFactor, 4);
            std::memcpy(&u, &c.S.PolygonOffsetUnits, 4);
            xk.Add(((uint64_t)f << 32) | u);
        }
        WGPURenderPipeline& pipe = s_Pipelines[xk.H];
        if (!pipe)
        {
            WGPUColorTargetState ct[8]{};
            WGPUBlendState blend[8]{};
            uint32_t targetCount = 0;
            for (uint32_t i = 0; i < c.RenderColorCount && i < 8; ++i)
            {
                Texture* rt = c.RenderColor[i];
                if (!rt)
                {
                    ct[i].format = WGPUTextureFormat_Undefined;
                    continue;
                }
                ct[i].format = rt->Format;
                const uint8_t m = c.S.ColorMaskBits[i];
                ct[i].writeMask = (WGPUColorWriteMask)(((m & 1) ? WGPUColorWriteMask_Red : 0) | ((m & 2) ? WGPUColorWriteMask_Green : 0) |
                                                       ((m & 4) ? WGPUColorWriteMask_Blue : 0) | ((m & 8) ? WGPUColorWriteMask_Alpha : 0));
                if (!(p->ColorOutputMask & (1u << i)))
                    ct[i].writeMask = WGPUColorWriteMask_None;   // gölgelendiricinin yazmadığı hedef korunur
                // WebGPU: gölgelendiricinin vermediği bileşenler yazma maskesinde olamaz (vec2 çıktı -> yalnız R G)
                static const WGPUColorWriteMask kComp[5] = { WGPUColorWriteMask_None, WGPUColorWriteMask_Red,
                                                             (WGPUColorWriteMask)(WGPUColorWriteMask_Red | WGPUColorWriteMask_Green),
                                                             (WGPUColorWriteMask)(WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue),
                                                             WGPUColorWriteMask_All };
                ct[i].writeMask = (WGPUColorWriteMask)(ct[i].writeMask & kComp[std::min<uint32_t>(4, p->OutputComponents[i])]);
                if (c.S.Blend && !rt->Integer && ct[i].writeMask != WGPUColorWriteMask_None)
                {
                    blend[i].color = { ToBlendOp(c.S.BlendEqRGB), ToBlend(c.S.BlendSrcRGB), ToBlend(c.S.BlendDstRGB) };
                    blend[i].alpha = { ToBlendOp(c.S.BlendEqA), ToBlend(c.S.BlendSrcA), ToBlend(c.S.BlendDstA) };
                    if (c.S.BlendEqRGB == GL_MIN || c.S.BlendEqRGB == GL_MAX)
                        blend[i].color.srcFactor = blend[i].color.dstFactor = WGPUBlendFactor_One;
                    if (c.S.BlendEqA == GL_MIN || c.S.BlendEqA == GL_MAX)
                        blend[i].alpha.srcFactor = blend[i].alpha.dstFactor = WGPUBlendFactor_One;
                    ct[i].blend = &blend[i];
                }
                targetCount = i + 1;
            }
            WGPUFragmentState fs{};
            fs.module = p->FS;
            fs.entryPoint = Str(p->FSEntry.c_str());
            fs.targetCount = targetCount;
            fs.targets = ct;
            WGPUDepthStencilState ds{};
            WGPURenderPipelineDescriptor d{};
            d.layout = pl;
            d.vertex.module = p->VS;
            d.vertex.entryPoint = Str(p->VSEntry.c_str());
            d.vertex.bufferCount = layouts.size();
            d.vertex.buffers = layouts.data();
            d.primitive.topology = topo;
            d.primitive.stripIndexFormat = strip && useIndices ? indexFormat : WGPUIndexFormat_Undefined;
            d.primitive.frontFace = c.S.FrontFace == GL_CCW ? WGPUFrontFace_CW : WGPUFrontFace_CCW;   // y çevrimi sarımı tersler
            d.primitive.cullMode = !c.S.CullFace ? WGPUCullMode_None : c.S.CullMode == GL_FRONT ? WGPUCullMode_Front : WGPUCullMode_Back;
            if (c.RenderDepth)
            {
                ds.format = c.RenderDepth->Format;
                ds.depthWriteEnabled = c.S.DepthTest && c.S.DepthMask ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                ds.depthCompare = c.S.DepthTest ? ToCompare(c.S.DepthFunc) : WGPUCompareFunction_Always;
                if (c.S.StencilTest && c.RenderDepth->Stencil)
                {
                    WGPUStencilFaceState f{ ToCompare(c.S.StencilFunc), ToStencilOp(c.S.StencilFail), ToStencilOp(c.S.StencilZFail), ToStencilOp(c.S.StencilZPass) };
                    ds.stencilFront = f;
                    ds.stencilBack = f;
                    ds.stencilReadMask = c.S.StencilValueMask & 0xFF;
                    ds.stencilWriteMask = c.S.StencilWriteMask & 0xFF;
                }
                else
                {
                    WGPUStencilFaceState f{ WGPUCompareFunction_Always, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep };
                    ds.stencilFront = f;
                    ds.stencilBack = f;
                    ds.stencilReadMask = 0xFF;
                    ds.stencilWriteMask = 0;
                }
                if (c.S.PolygonOffsetFill && triangles)
                {
                    ds.depthBias = (int32_t)std::lround(c.S.PolygonOffsetUnits);
                    ds.depthBiasSlopeScale = c.S.PolygonOffsetFactor;
                }
                d.depthStencil = &ds;
            }
            d.multisample.count = c.RenderingSamples;
            d.multisample.mask = 0xFFFFFFFF;
            d.fragment = &fs;
            pipe = W().DeviceCreateRenderPipeline(Device(), &d);
            if (!pipe)
                return;
        }

        // Hata ayıklama: CQ_WGPUGL_SHOW=<gölgelendirici adı>:<renk eki> o gölgelendiricinin çizdiği hedefi sunumda gösterir
        static const char* s_Show = std::getenv("CQ_WGPUGL_SHOW");
        if (s_Show && *s_Show)
        {
            const char* colon = std::strchr(s_Show, ':');
            const std::string label = colon ? std::string(s_Show, (size_t)(colon - s_Show)) : std::string(s_Show);
            const int att = colon ? std::atoi(colon + 1) : 0;
            if (p->Label == label && att >= 0 && att < (int)c.RenderColorCount && c.RenderColor[att])
                s_ShowTexture = c.RenderColor[att];
        }
        if (Tracing())
            CQ_CORE_INFO("[WgpuGL trace f{0}]   draw '{1}' n={2} inst={3} idx={4} blend={5} depth={6}/{7} tex={8}", FrameSerial(),
                         p->Label.empty() ? std::to_string(p->Id) : p->Label, drawCount, instances, useIndices, c.S.Blend, c.S.DepthTest,
                         c.S.DepthMask, p->Textures.size());
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = bgl;
        bgd.entryCount = be.size();
        bgd.entries = be.data();
        WGPUBindGroup bg = W().DeviceCreateBindGroup(Device(), &bgd);

        W().RenderPassEncoderSetPipeline(pass, pipe);
        W().RenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        W().BindGroupRelease(bg);
        uint32_t slotIndex = 0;
        for (auto& vb : vbufs)
            W().RenderPassEncoderSetVertexBuffer(pass, slotIndex++, vb.Buffer, vb.Offset, WGPU_WHOLE_SIZE);
        if (!nullAttrs.empty())
            W().RenderPassEncoderSetVertexBuffer(pass, slotIndex++, s_NullVertex, 0, 32);
        // Görüntü alanı / makas (bellek satırları GL satırlarıyla aynı)
        const float vx = (float)c.S.Viewport[0], vy = (float)c.S.Viewport[1];
        const float vw = (float)std::max(1, c.S.Viewport[2]), vh = (float)std::max(1, c.S.Viewport[3]);
        W().RenderPassEncoderSetViewport(pass, vx, vy, vw, vh, 0.0f, 1.0f);
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
        W().RenderPassEncoderSetScissorRect(pass, (uint32_t)sx, (uint32_t)sy, (uint32_t)sw, (uint32_t)sh);
        if (c.S.Blend)
        {
            WGPUColor bc{ c.S.BlendColor[0], c.S.BlendColor[1], c.S.BlendColor[2], c.S.BlendColor[3] };
            W().RenderPassEncoderSetBlendConstant(pass, &bc);
        }
        if (c.S.StencilTest)
            W().RenderPassEncoderSetStencilReference(pass, (uint32_t)(c.S.StencilRef & 0xFF));
        if (useIndices)
        {
            W().RenderPassEncoderSetIndexBuffer(pass, indexBuffer, indexFormat, indexOffset, WGPU_WHOLE_SIZE);
            W().RenderPassEncoderDrawIndexed(pass, drawCount, (uint32_t)instances, 0, expand && !indexed ? 0 : baseVertex, 0);
        }
        else
            W().RenderPassEncoderDraw(pass, drawCount, (uint32_t)instances, (uint32_t)first, 0);
    }

    void GenerateMipmaps(Texture& t)
    {
        if (!t.Image || t.Levels < 2 || t.Samples > 1 || t.Depth)
            return;
        EndRendering();
        const float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
            for (uint32_t l = 1; l < t.Levels; ++l)
            {
                WGPUTextureView src = SingleView(t, (int)l - 1, (int)layer, WGPUTextureUsage_TextureBinding);
                WGPUTextureView dst = SingleView(t, (int)l, (int)layer, WGPUTextureUsage_RenderAttachment);
                const int w = (int)std::max(1u, t.Width >> l), h = (int)std::max(1u, t.Height >> l);
                HelperBlit(src, t.Filterable, true, rect, false, 0.0f, dst, t.Format, 0, 0, w, h);
                W().TextureViewRelease(src);
                W().TextureViewRelease(dst);
            }
        t.DefinedLevels = (t.Levels >= 32 ? 0xFFFFFFFFu : ((1u << t.Levels) - 1u));
    }

    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
    {
        auto& c = Ctx();
        EndRendering();
        Framebuffer* rfb = GetFramebuffer(c.S.ReadFBO);
        if (!rfb)
            return;
        Targets dst = CollectTargets(c.S.DrawFBO);
        if (mask & GL_COLOR_BUFFER_BIT)
        {
            const int ridx = c.S.ReadFBO == 0 ? 0 : (int)(rfb->ReadBuffer - GL_COLOR_ATTACHMENT0);
            Texture* src = (ridx >= 0 && ridx < 8) ? AttachmentTexture(rfb->Color[ridx]) : nullptr;
            const int srcLevel = (ridx >= 0 && ridx < 8) ? rfb->Color[ridx].Level : 0;
            const int srcLayer = (ridx >= 0 && ridx < 8) ? rfb->Color[ridx].Layer : 0;
            for (int i = 0; src && src->Image && i < dst.ColorCount; ++i)
            {
                Texture* d = dst.Color[i];
                if (!d || !d->Image || d == src)
                    continue;
                const bool sameSize = (sx1 - sx0) == (dx1 - dx0) && (sy1 - sy0) == (dy1 - dy0) && sx1 > sx0 && sy1 > sy0;
                if (src->Samples > 1 && d->Samples == 1 && sameSize && src->Format == d->Format && sx0 == 0 && sy0 == 0 && dx0 == 0 && dy0 == 0 &&
                    (uint32_t)sx1 == src->Width && (uint32_t)sy1 == src->Height)
                {
                    // Çok örnekli -> tek örnek çözümleme (GL'in MSAA çözüm blit'i)
                    WGPUTextureView msView = TargetView(*src, 0, 0);
                    WGPUTextureView rv = TargetView(*d, dst.ColorLevel[i], dst.ColorLayer[i]);
                    WGPURenderPassColorAttachment ca{};
                    ca.view = msView;
                    ca.resolveTarget = rv;
                    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                    ca.loadOp = WGPULoadOp_Load;
                    ca.storeOp = WGPUStoreOp_Store;
                    WGPURenderPassDescriptor rp{};
                    rp.colorAttachmentCount = 1;
                    rp.colorAttachments = &ca;
                    BeginPass(rp);
                    EndPass();
                    W().TextureViewRelease(msView);
                    W().TextureViewRelease(rv);
                }
                else if (sameSize && src->Format == d->Format && src->Samples == 1 && d->Samples == 1)
                {
                    WGPUTexelCopyTextureInfo s{};
                    s.texture = src->Image;
                    s.mipLevel = (uint32_t)srcLevel;
                    s.origin = { (uint32_t)std::max(0, sx0), (uint32_t)std::max(0, sy0), (uint32_t)srcLayer };
                    WGPUTexelCopyTextureInfo t{};
                    t.texture = d->Image;
                    t.mipLevel = (uint32_t)dst.ColorLevel[i];
                    t.origin = { (uint32_t)std::max(0, dx0), (uint32_t)std::max(0, dy0), (uint32_t)dst.ColorLayer[i] };
                    WGPUExtent3D size{ (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0), 1 };
                    W().CommandEncoderCopyTextureToTexture(Encoder(), &s, &t, &size);
                }
                else if (src->Samples == 1 && d->Samples == 1 && !src->Integer && !d->Integer)
                {
                    const float sw = (float)std::max(1u, src->Width >> srcLevel), sh = (float)std::max(1u, src->Height >> srcLevel);
                    const bool flipX = (sx1 < sx0) != (dx1 < dx0), flipY = (sy1 < sy0) != (dy1 < dy0);
                    const float rect[4] = { (flipX ? std::max(sx0, sx1) : std::min(sx0, sx1)) / sw, (float)std::min(sy0, sy1) / sh,
                                            (flipX ? std::min(sx0, sx1) : std::max(sx0, sx1)) / sw, (float)std::max(sy0, sy1) / sh };
                    WGPUTextureView sv = SingleView(*src, srcLevel, srcLayer, WGPUTextureUsage_TextureBinding);
                    WGPUTextureView dv = TargetView(*d, dst.ColorLevel[i], dst.ColorLayer[i]);
                    HelperBlit(sv, src->Filterable, filter == GL_LINEAR, rect, flipY, 0.0f, dv, d->Format, std::min(dx0, dx1), std::min(dy0, dy1),
                               std::abs(dx1 - dx0), std::abs(dy1 - dy0));
                    W().TextureViewRelease(sv);
                    W().TextureViewRelease(dv);
                }
                d->DefinedLevels |= 1u << dst.ColorLevel[i];
            }
        }
        if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))
        {
            const Attachment& ra = rfb->Depth.Valid() ? rfb->Depth : rfb->Stencil;
            Texture* src = ra.Valid() ? AttachmentTexture(ra) : nullptr;
            Texture* d = dst.Depth;
            const bool sameSize = (sx1 - sx0) == (dx1 - dx0) && (sy1 - sy0) == (dy1 - dy0) && sx0 == 0 && sy0 == 0 && dx0 == 0 && dy0 == 0;
            if (src && d && src->Image && d->Image && src != d && src->Format == d->Format && src->Samples == 1 && d->Samples == 1 && sameSize &&
                (uint32_t)sx1 == src->Width && (uint32_t)sy1 == src->Height && src->Width == d->Width && src->Height == d->Height)
            {
                WGPUTexelCopyTextureInfo s{};
                s.texture = src->Image;
                s.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyTextureInfo t{};
                t.texture = d->Image;
                t.aspect = WGPUTextureAspect_All;
                WGPUExtent3D size{ src->Width, src->Height, 1 };
                W().CommandEncoderCopyTextureToTexture(Encoder(), &s, &t, &size);
            }
            else if (src && d)
            {
                static bool s_Warned = false;
                if (!s_Warned)
                {
                    s_Warned = true;
                    CQ_CORE_WARN("[WgpuGL] Depth blits need the same size and format in WebGPU (skipped)");
                }
            }
        }
    }

    void BeginFrameResources()
    {
        auto& c = Ctx();
        uint32_t w = 0, h = 0;
        // Arka arabellek boyu pencerenin çerçeve arabelleğiyle aynı
        if (!c.BackColor.Image || c.BackColor.Width == 0)
        {
            if (!SurfaceSize(w, h))
                w = 1280, h = 720;
        }
        else
        {
            w = c.BackColor.Width;
            h = c.BackColor.Height;
        }
        if (!c.BackColor.Image || c.BackColor.Width != w || c.BackColor.Height != h)
        {
            AllocateTexture(c.BackColor, GL_TEXTURE_2D, GL_RGBA8, w, h, 1, 1);
            AllocateTexture(c.BackDepth, GL_TEXTURE_2D, GL_DEPTH24_STENCIL8, w, h, 1, 1);
            c.BackColor.DefinedLevels = c.BackDepth.DefinedLevels = 1;
        }
    }

    void SwapBuffers()
    {
        auto& c = Ctx();
        EndRendering();
        WGPUTexture surface = nullptr;
        uint32_t w = 0, h = 0;
        if (AcquireSurface(surface, w, h) && surface && c.BackColor.Image)
        {
            WGPUTextureViewDescriptor vd{};
            vd.format = SurfaceFormat();
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            vd.aspect = WGPUTextureAspect_All;
            vd.usage = WGPUTextureUsage_RenderAttachment;
            WGPUTextureView dst = W().TextureCreateView(surface, &vd);
            // Hata ayıklama: CQ_WGPUGL_SHOW=<fbo>:<renk eki> arka arabellek yerine o ara görüntüyü gösterir (HDR değerler 1'de kırpılır)
            Texture* shown = &c.BackColor;
            if (s_ShowTexture && s_ShowTexture->Image && s_ShowTexture->Samples == 1 && !s_ShowTexture->Depth && !s_ShowTexture->Integer)
                shown = s_ShowTexture;
            WGPUTextureView src = SingleView(*shown, 0, 0, WGPUTextureUsage_TextureBinding);
            // GL yönündeki arka arabellek (satır 0 = alt) yüzeye (satır 0 = üst) dikey çevrilerek
            const float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            HelperBlit(src, shown->Filterable, false, rect, true, 0.0f, dst, SurfaceFormat(), 0, 0, (int)w, (int)h);
            W().TextureViewRelease(src);
            W().TextureViewRelease(dst);
            PresentSurface();
            W().TextureRelease(surface);
        }
        else
        {
            if (surface)
                W().TextureRelease(surface);
            Submit();
        }
        // Pencere boyu değiştiyse arka arabellek bir sonraki karede yeni boyda
        if (w && h && (w != c.BackColor.Width || h != c.BackColor.Height))
        {
            AllocateTexture(c.BackColor, GL_TEXTURE_2D, GL_RGBA8, w, h, 1, 1);
            AllocateTexture(c.BackDepth, GL_TEXTURE_2D, GL_DEPTH24_STENCIL8, w, h, 1, 1);
            c.BackColor.DefinedLevels = c.BackDepth.DefinedLevels = 1;
        }
    }

    bool InitDrawHelpers()
    {
        const float fdef[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const int32_t idef[4] = { 0, 0, 0, 1 };
        uint8_t defaults[32];
        std::memcpy(defaults, fdef, 16);
        std::memcpy(defaults + 16, idef, 16);
        WGPUBufferDescriptor bd{};
        bd.size = 32;
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        s_NullVertex = W().DeviceCreateBuffer(Device(), &bd);
        bd.size = 65536;
        bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        s_ZeroUniform = W().DeviceCreateBuffer(Device(), &bd);
        if (!s_NullVertex || !s_ZeroUniform)
            return false;
        W().QueueWriteBuffer(Queue(), s_NullVertex, 0, defaults, sizeof(defaults));
        return true;
    }

    void ReleaseDrawHelpers()
    {
        for (auto& [k, p] : s_Pipelines) if (p) W().RenderPipelineRelease(p);
        for (auto& [k, p] : s_HelperPipelines) if (p) W().RenderPipelineRelease(p);
        for (auto& [k, l] : s_PipelineLayouts) if (l) W().PipelineLayoutRelease(l);
        for (auto& [k, l] : s_Layouts) if (l) W().BindGroupLayoutRelease(l);
        s_Pipelines.clear();
        s_HelperPipelines.clear();
        s_PipelineLayouts.clear();
        s_Layouts.clear();
        if (s_HelperLayout) W().BindGroupLayoutRelease(s_HelperLayout);
        if (s_HelperLayoutUnfilt) W().BindGroupLayoutRelease(s_HelperLayoutUnfilt);
        if (s_HelperModule) W().ShaderModuleRelease(s_HelperModule);
        s_HelperLayout = s_HelperLayoutUnfilt = nullptr;
        s_HelperModule = nullptr;
        if (s_NullVertex) { W().BufferDestroy(s_NullVertex); W().BufferRelease(s_NullVertex); }
        if (s_ZeroUniform) { W().BufferDestroy(s_ZeroUniform); W().BufferRelease(s_ZeroUniform); }
        s_NullVertex = s_ZeroUniform = nullptr;
        (void)s_ClearModules;
        (void)s_ClearLayout;
    }
}

#endif
