#ifdef CQ_WEBGPU_ENABLED

// WebGPU çeviri katmanı: nesne tabloları, dokular (biçim eşleme, yükleme, geri okuma, düzey büyütme), örnekleyiciler, arabellekler.

#include "WgpuGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstring>

namespace Conqueror::WgpuGL
{
    using WGPU::W;
    using GLEmu::TexelKind;

    namespace
    {
        Context s_Context;
        std::unordered_map<uint64_t, WGPUSampler> s_Samplers;

#if defined(__EMSCRIPTEN__)
        // Tarayıcıda eşzamansız geri okuma: kopya kuyruğa girer, eşleme gönderimden sonra istenir, sonuç geldiğinde saklanır;
        // glReadPixels / glGetTexImage aynı bölge için son tamamlanan sonucu verir (bir iki kare gecikmeli: otomatik pozlama vb.)
        struct AsyncRead
        {
            WGPUBuffer Buffer = nullptr;
            uint64_t Size = 0;
            uint32_t Pitch = 0;
            bool InFlight = false;
            bool HasData = false;
            std::vector<uint8_t> Data;
        };
        std::unordered_map<uint64_t, std::unique_ptr<AsyncRead>> s_Reads;

        void OnReadMapped(WGPUMapAsyncStatus status, WGPUStringView, void* user, void*)
        {
            auto* r = static_cast<AsyncRead*>(user);
            if (status == WGPUMapAsyncStatus_Success)
            {
                if (const void* p = W().BufferGetConstMappedRange(r->Buffer, 0, (size_t)r->Size))
                {
                    r->Data.assign((const uint8_t*)p, (const uint8_t*)p + r->Size);
                    r->HasData = true;
                }
                W().BufferUnmap(r->Buffer);
            }
            r->InFlight = false;
        }

        void MapAfterSubmit(void* user)
        {
            auto* r = static_cast<AsyncRead*>(user);
            WGPUBufferMapCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = &OnReadMapped;
            cb.userdata1 = r;
            W().BufferMapAsync(r->Buffer, WGPUMapMode_Read, 0, (size_t)r->Size, cb);
        }
#endif

        uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

        // WebGPU'da karşılığı olmayan texel türleri: en yakın desteklenen tür (yükleme dönüşümü bu türe göre yapılır)
        TexelKind Supported(TexelKind k)
        {
            switch (k)
            {
                case TexelKind::RGBA16: return TexelKind::RGBA16F;   // unorm16: çekirdekte yok
                case TexelKind::R16: return TexelKind::R16F;
                case TexelKind::RG16: return TexelKind::RG16F;
                case TexelKind::RG11B10F: return HasFeature(WGPUFeatureName_RG11B10UfloatRenderable) ? k : TexelKind::RGBA16F;
                case TexelKind::D24S8: return HasFeature(WGPUFeatureName_Depth32FloatStencil8) ? TexelKind::D32FS8 : k;
                case TexelKind::D32FS8: return HasFeature(WGPUFeatureName_Depth32FloatStencil8) ? k : TexelKind::D24S8;
                default: return k;
            }
        }

        WGPUTextureFormat FormatOf(TexelKind k)
        {
            switch (k)
            {
                case TexelKind::RGBA8: return WGPUTextureFormat_RGBA8Unorm;
                case TexelKind::RGBA8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
                case TexelKind::R8: return WGPUTextureFormat_R8Unorm;
                case TexelKind::RG8: return WGPUTextureFormat_RG8Unorm;
                case TexelKind::RGBA16F: return WGPUTextureFormat_RGBA16Float;
                case TexelKind::RGBA32F: return WGPUTextureFormat_RGBA32Float;
                case TexelKind::R16F: return WGPUTextureFormat_R16Float;
                case TexelKind::RG16F: return WGPUTextureFormat_RG16Float;
                case TexelKind::R32F: return WGPUTextureFormat_R32Float;
                case TexelKind::RG32F: return WGPUTextureFormat_RG32Float;
                case TexelKind::RG11B10F: return WGPUTextureFormat_RG11B10Ufloat;
                case TexelKind::R32I: return WGPUTextureFormat_R32Sint;
                case TexelKind::R32UI: return WGPUTextureFormat_R32Uint;
                case TexelKind::RG32I: return WGPUTextureFormat_RG32Sint;
                case TexelKind::RGBA32I: return WGPUTextureFormat_RGBA32Sint;
                case TexelKind::R8I: return WGPUTextureFormat_R8Sint;
                case TexelKind::R8UI: return WGPUTextureFormat_R8Uint;
                case TexelKind::D16: return WGPUTextureFormat_Depth16Unorm;
                case TexelKind::D32F: return WGPUTextureFormat_Depth32Float;
                case TexelKind::D24S8: return WGPUTextureFormat_Depth24PlusStencil8;
                case TexelKind::D32FS8: return WGPUTextureFormat_Depth32FloatStencil8;
                default: return WGPUTextureFormat_RGBA8Unorm;
            }
        }

        bool IsFloat32(TexelKind k) { return k == TexelKind::R32F || k == TexelKind::RG32F || k == TexelKind::RGBA32F; }
    }

    Context& Ctx() { return s_Context; }

    Texture* GetTexture(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Textures.find(name);
        return it != c.Textures.end() ? it->second.get() : nullptr;
    }

    Texture* GetRenderbuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Renderbuffers.find(name);
        return it != c.Renderbuffers.end() ? it->second.get() : nullptr;
    }

    Buffer* GetBuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Buffers.find(name);
        return it != c.Buffers.end() ? it->second.get() : nullptr;
    }

    Framebuffer* GetFramebuffer(uint32_t name)
    {
        auto& c = Ctx();
        if (name == 0)
            return &c.DefaultFB;
        auto it = c.Framebuffers.find(name);
        return it != c.Framebuffers.end() ? it->second.get() : nullptr;
    }

    Program* GetProgram(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Programs.find(name);
        return it != c.Programs.end() ? it->second.get() : nullptr;
    }

    Texture* AttachmentTexture(const Attachment& a)
    {
        auto& c = Ctx();
        if (a.Texture == 0xFFFFFFF0u) return &c.BackColor;
        if (a.Texture == 0xFFFFFFF1u) return &c.BackDepth;
        if (a.Texture) return GetTexture(a.Texture);
        if (a.Renderbuffer) return GetRenderbuffer(a.Renderbuffer);
        return nullptr;
    }

    void DestroyTextureImage(Texture& t)
    {
        EndRendering();
        // Kayıtlı komutlar dokuyu tutar (WebGPU başvuru sayar): bırakmak güvenli; yok etmek gönderim sonrasına kalmamalı
        if (t.View)
            W().TextureViewRelease(t.View);
        if (t.Image)
            W().TextureRelease(t.Image);
        t.Image = nullptr;
        t.View = nullptr;
        t.ViewLevels = 0;
        t.DefinedLevels = 0;
    }

    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples)
    {
        DestroyTextureImage(t);
        const GLEmu::TexelInfo ti = GLEmu::TexelInfoOf(Supported(GLEmu::TexelOf(internalFormat).Kind));
        t.Target = target;
        t.InternalFormat = internalFormat;
        t.Kind = ti.Kind;
        t.Format = FormatOf(ti.Kind);
        t.Depth = ti.Depth;
        t.Stencil = ti.Stencil;
        t.Integer = ti.Integer;
        t.Filterable = !ti.Integer && !ti.Depth && (!IsFloat32(ti.Kind) || HasFeature(WGPUFeatureName_Float32Filterable));
        t.Renderable = true;
        t.Width = std::max(1u, w);
        t.Height = std::max(1u, h);
        t.Levels = std::max(1u, std::min(levels, GLEmu::MaxLevels(t.Width, t.Height)));
        t.Layers = target == GL_TEXTURE_CUBE_MAP ? 6u : 1u;
        t.Samples = samples > 1 ? 4u : 1u;               // WebGPU: 1 ya da 4 örnek
        if (t.Samples > 1)
            t.Levels = 1;

        WGPUTextureDescriptor d{};
        d.dimension = WGPUTextureDimension_2D;
        d.format = t.Format;
        d.size = { t.Width, t.Height, t.Layers };
        d.mipLevelCount = t.Levels;
        d.sampleCount = t.Samples;
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        if (t.Samples == 1)
            d.usage |= WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        t.Image = W().DeviceCreateTexture(Device(), &d);
        if (!t.Image)
        {
            CQ_CORE_ERROR("[WgpuGL] Cannot create {0}x{1} texture (format {2}, {3} samples)", t.Width, t.Height, (int)t.Format, t.Samples);
            return false;
        }
        // WebGPU dokuları sıfırla başlar (GL'de tanımsız; diğer katmanlar da temizler)
        return true;
    }

    // Yalnız derinlik görünümü derinlik bölümünün biçimini ister (derinlik + şablon dokularında)
    WGPUTextureFormat DepthAspectFormat(const Texture& t)
    {
        if (t.Format == WGPUTextureFormat_Depth32FloatStencil8)
            return WGPUTextureFormat_Depth32Float;
        if (t.Format == WGPUTextureFormat_Depth24PlusStencil8)
            return WGPUTextureFormat_Depth24Plus;
        return t.Format;
    }

    WGPUTextureView SampleView(Texture& t)
    {
        if (!t.Image)
            return nullptr;
        int levels = (int)t.Levels;
        if (!t.Immutable && t.DefinedLevels)
        {
            levels = 0;
            while (levels < (int)t.Levels && (t.DefinedLevels & (1u << levels)))
                ++levels;
            levels = std::max(levels, 1);
        }
        levels = std::clamp(std::min(levels, t.MaxLevel + 1), 1, (int)t.Levels);
        if (t.View && t.ViewLevels == levels)
            return t.View;
        if (t.View)
            W().TextureViewRelease(t.View);
        WGPUTextureViewDescriptor vd{};
        vd.format = t.Depth ? DepthAspectFormat(t) : t.Format;
        vd.dimension = t.Layers == 6 ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
        vd.baseMipLevel = 0;
        vd.mipLevelCount = (uint32_t)levels;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = t.Layers;
        vd.aspect = t.Depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        vd.usage = WGPUTextureUsage_TextureBinding;
        t.View = W().TextureCreateView(t.Image, &vd);
        t.ViewLevels = levels;
        return t.View;
    }

    WGPUTextureView TargetView(Texture& t, int level, int layer)
    {
        WGPUTextureViewDescriptor vd{};
        vd.format = t.Format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = (uint32_t)std::clamp(level, 0, (int)t.Levels - 1);
        vd.mipLevelCount = 1;
        vd.baseArrayLayer = (uint32_t)std::clamp(layer, 0, (int)t.Layers - 1);
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        vd.usage = WGPUTextureUsage_RenderAttachment;
        return W().TextureCreateView(t.Image, &vd);
    }

    WGPUSampler GetSampler(const Texture& p, bool filtering, bool comparison)
    {
        auto linear = [](GLenum f) { return !(f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR); };
        auto mipLinear = [](GLenum f) { return f == GL_NEAREST_MIPMAP_LINEAR || f == GL_LINEAR_MIPMAP_LINEAR; };
        auto mipped = [](GLenum f) { return f != GL_NEAREST && f != GL_LINEAR; };
        auto address = [](GLenum w) {
            switch (w)
            {
                case GL_CLAMP_TO_EDGE:
                case GL_CLAMP_TO_BORDER: return WGPUAddressMode_ClampToEdge;   // kenar rengi WebGPU'da yok
                case GL_MIRRORED_REPEAT: return WGPUAddressMode_MirrorRepeat;
                default: return WGPUAddressMode_Repeat;
            }
        };
        const bool minL = filtering && linear(p.MinFilter);
        const bool magL = filtering && linear(p.MagFilter);
        const bool mipL = filtering && mipLinear(p.MinFilter);
        const float maxLod = mipped(p.MinFilter) ? std::clamp(p.MaxLod, 0.0f, 32.0f) : 0.25f;   // mipmapsiz süzgeç: düzey 0
        const float minLod = std::clamp(p.MinLod, 0.0f, maxLod);
        uint16_t aniso = (uint16_t)std::clamp((int)p.MaxAnisotropy, 1, 16);
        if (!(minL && magL && mipL))
            aniso = 1;   // WebGPU: anizotropi tüm süzgeçler doğrusalken
        const bool cmp = comparison && p.CompareMode == GL_COMPARE_REF_TO_TEXTURE;
        uint64_t key = (uint64_t)minL | ((uint64_t)magL << 1) | ((uint64_t)mipL << 2) | ((uint64_t)aniso << 3) |
                       ((uint64_t)address(p.WrapS) << 8) | ((uint64_t)address(p.WrapT) << 12) | ((uint64_t)address(p.WrapR) << 16) |
                       ((uint64_t)(cmp ? (p.CompareFunc & 0xF) + 1 : 0) << 20) | ((uint64_t)(minLod * 16.0f) << 28) | ((uint64_t)(maxLod * 16.0f) << 40);
        if (auto it = s_Samplers.find(key); it != s_Samplers.end())
            return it->second;
        WGPUSamplerDescriptor d{};
        d.addressModeU = address(p.WrapS);
        d.addressModeV = address(p.WrapT);
        d.addressModeW = address(p.WrapR);
        d.magFilter = magL ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        d.minFilter = minL ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        d.mipmapFilter = mipL ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        d.lodMinClamp = minLod;
        d.lodMaxClamp = maxLod;
        d.maxAnisotropy = aniso;
        if (cmp)
        {
            switch (p.CompareFunc)
            {
                case GL_NEVER: d.compare = WGPUCompareFunction_Never; break;
                case GL_LESS: d.compare = WGPUCompareFunction_Less; break;
                case GL_EQUAL: d.compare = WGPUCompareFunction_Equal; break;
                case GL_GREATER: d.compare = WGPUCompareFunction_Greater; break;
                case GL_NOTEQUAL: d.compare = WGPUCompareFunction_NotEqual; break;
                case GL_GEQUAL: d.compare = WGPUCompareFunction_GreaterEqual; break;
                case GL_ALWAYS: d.compare = WGPUCompareFunction_Always; break;
                default: d.compare = WGPUCompareFunction_LessEqual; break;
            }
        }
        WGPUSampler s = W().DeviceCreateSampler(Device(), &d);
        s_Samplers[key] = s;
        return s;
    }

    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || !data || w <= 0 || h <= 0)
            return;
        if (t.Depth || t.Samples > 1)
        {
            CQ_CORE_WARN("[WgpuGL] Uploads to depth / multisample textures are not supported");
            return;
        }
        auto& c = Ctx();
        EndRendering();
        // Kuyruk yazımı, henüz gönderilmemiş komutlardan önce uygulanır: doku bu gönderimde kullanıldıysa önce gönder
        if (t.LastUseSubmit == SubmitSerial())
            Submit();
        static thread_local std::vector<uint8_t> staging;
        size_t srcPitch = 0;
        const uint8_t* src = GLEmu::ConvertUpload(t.Kind, w, h, format, type, data, c.S.UnpackRowLength, c.S.UnpackAlignment, staging, srcPitch);
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = t.Image;
        dst.mipLevel = (uint32_t)level;
        dst.origin = { (uint32_t)x, (uint32_t)y, (uint32_t)layer };
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = (uint32_t)srcPitch;
        layout.rowsPerImage = (uint32_t)h;
        WGPUExtent3D size{ (uint32_t)w, (uint32_t)h, 1 };
        W().QueueWriteTexture(Queue(), &dst, src, srcPitch * (size_t)h, &layout, &size);
    }

    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        if (!t.Image || !out || w <= 0 || h <= 0)
            return;
        EndRendering();
#if defined(__EMSCRIPTEN__)
        // Tarayıcının ana iş parçacığı GPU'yu bekleyemez: son tamamlanan eşzamansız okuma (yoksa sıfır) + yeni okuma isteği
        {
            uint64_t key = 1469598103934665603ull;
            for (uint64_t v : { (uint64_t)(uintptr_t)&t, (uint64_t)(uintptr_t)t.Image, (uint64_t)level, (uint64_t)layer, (uint64_t)x, (uint64_t)y,
                                (uint64_t)w, (uint64_t)h })
                key = (key ^ v) * 1099511628211ull;
            auto& slot = s_Reads[key];
            if (!slot)
                slot = std::make_unique<AsyncRead>();
            AsyncRead& r = *slot;
            if (r.HasData)
                GLEmu::ConvertReadback(t.Kind, r.Data.data(), (size_t)r.Pitch, w, h, format, type, out, packAlignment);
            else
            {
                const int bpp = std::max(1, GLEmu::GLChannels(format) * GLEmu::GLTypeSize(type));
                const size_t outPitch = AlignUp((size_t)w * (size_t)bpp, (size_t)std::max(1, packAlignment));
                std::memset(out, 0, outPitch * (size_t)h);
            }
            if (r.InFlight || t.Samples > 1 || t.Kind == TexelKind::D24S8)
                return;
            const uint32_t bytes = (uint32_t)GLEmu::TexelInfoOf(t.Kind).Bytes;
            r.Pitch = (uint32_t)AlignUp((uint64_t)w * bytes, 256);
            const uint64_t size = (uint64_t)r.Pitch * (uint64_t)h;
            if (!r.Buffer || r.Size != size)
            {
                if (r.Buffer)
                    W().BufferRelease(r.Buffer);
                WGPUBufferDescriptor bd{};
                bd.size = size;
                bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
                r.Buffer = W().DeviceCreateBuffer(Device(), &bd);
                r.Size = size;
            }
            WGPUTexelCopyTextureInfo src{};
            src.texture = t.Image;
            src.mipLevel = (uint32_t)level;
            src.origin = { (uint32_t)x, (uint32_t)y, (uint32_t)layer };
            src.aspect = t.Depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst{};
            dst.buffer = r.Buffer;
            dst.layout.bytesPerRow = r.Pitch;
            dst.layout.rowsPerImage = (uint32_t)h;
            WGPUExtent3D extent{ (uint32_t)w, (uint32_t)h, 1 };
            W().CommandEncoderCopyTextureToBuffer(Encoder(), &src, &dst, &extent);
            r.InFlight = true;
            AfterSubmit(&MapAfterSubmit, &r);
        }
#else
        if (t.Kind == TexelKind::D24S8)
        {
            CQ_CORE_WARN("[WgpuGL] depth24plus cannot be read back in WebGPU");
            return;
        }
        WGPUTexture source = t.Image;
        WGPUTexture resolved = nullptr;
        uint32_t srcLevel = (uint32_t)level, srcLayer = (uint32_t)layer;
        if (t.Samples > 1)
        {
            if (t.Depth)
            {
                CQ_CORE_WARN("[WgpuGL] Reading a multisample depth texture is not supported");
                return;
            }
            WGPUTextureDescriptor d{};
            d.dimension = WGPUTextureDimension_2D;
            d.format = t.Format;
            d.size = { t.Width, t.Height, 1 };
            d.mipLevelCount = 1;
            d.sampleCount = 1;
            d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            resolved = W().DeviceCreateTexture(Device(), &d);
            WGPUTextureView msView = TargetView(t, 0, 0);
            WGPUTextureViewDescriptor rvd{};
            rvd.format = t.Format;
            rvd.dimension = WGPUTextureViewDimension_2D;
            rvd.mipLevelCount = 1;
            rvd.arrayLayerCount = 1;
            rvd.aspect = WGPUTextureAspect_All;
            rvd.usage = WGPUTextureUsage_RenderAttachment;
            WGPUTextureView rView = W().TextureCreateView(resolved, &rvd);
            WGPURenderPassColorAttachment ca{};
            ca.view = msView;
            ca.resolveTarget = rView;
            ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load;
            ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &ca;
            BeginPass(rp);
            EndPass();
            W().TextureViewRelease(msView);
            W().TextureViewRelease(rView);
            source = resolved;
            srcLevel = 0;
            srcLayer = 0;
        }
        const uint32_t bytes = (uint32_t)GLEmu::TexelInfoOf(t.Kind).Bytes;
        const uint64_t pitch = AlignUp((uint64_t)w * bytes, 256);
        WGPUBufferDescriptor bd{};
        bd.size = pitch * (uint64_t)h;
        bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = W().DeviceCreateBuffer(Device(), &bd);
        WGPUTexelCopyTextureInfo src{};
        src.texture = source;
        src.mipLevel = srcLevel;
        src.origin = { (uint32_t)x, (uint32_t)y, srcLayer };
        src.aspect = t.Depth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo dst{};
        dst.buffer = readback;
        dst.layout.bytesPerRow = (uint32_t)pitch;
        dst.layout.rowsPerImage = (uint32_t)h;
        WGPUExtent3D size{ (uint32_t)w, (uint32_t)h, 1 };
        W().CommandEncoderCopyTextureToBuffer(Encoder(), &src, &dst, &size);
        std::vector<uint8_t> data;
        if (MapRead(readback, bd.size, data))
            GLEmu::ConvertReadback(t.Kind, data.data(), (size_t)pitch, w, h, format, type, out, packAlignment);
        W().BufferDestroy(readback);
        W().BufferRelease(readback);
        if (resolved)
        {
            W().TextureDestroy(resolved);
            W().TextureRelease(resolved);
        }
#endif
    }

    bool GrowLevels(Texture& t, uint32_t levels)
    {
        levels = std::min(levels, GLEmu::MaxLevels(t.Width, t.Height));
        if (!t.Image || levels <= t.Levels || t.Samples > 1)
            return t.Image != nullptr;
        WGPUTexture old = t.Image;
        t.Image = nullptr;   // AllocateTexture eskisini bırakmasın
        if (t.View)
        {
            W().TextureViewRelease(t.View);
            t.View = nullptr;
        }
        const uint32_t oldLevels = t.Levels;
        const uint32_t defined = t.DefinedLevels;
        if (!AllocateTexture(t, t.Target, t.InternalFormat, t.Width, t.Height, levels, 1))
        {
            t.Image = old;
            return false;
        }
        for (uint32_t l = 0; l < oldLevels; ++l)
        {
            if (!(defined & (1u << l)))
                continue;
            WGPUTexelCopyTextureInfo src{};
            src.texture = old;
            src.mipLevel = l;
            src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyTextureInfo dst{};
            dst.texture = t.Image;
            dst.mipLevel = l;
            dst.aspect = WGPUTextureAspect_All;
            WGPUExtent3D size{ std::max(1u, t.Width >> l), std::max(1u, t.Height >> l), t.Layers };
            W().CommandEncoderCopyTextureToTexture(Encoder(), &src, &dst, &size);
        }
        W().TextureRelease(old);
        t.DefinedLevels = defined;
        return true;
    }

    void ClearTexture(Texture& t, int level, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || level < 0 || level >= (int)t.Levels)
            return;
        EndRendering();
        float color[4], depth = 1.0f;
        uint8_t stencil = 0;
        GLEmu::ParseClearValue(t.Depth, t.Integer, format, type, data, color, depth, stencil);
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
        {
            WGPUTextureView view = TargetView(t, level, (int)layer);
            WGPURenderPassDescriptor rp{};
            WGPURenderPassColorAttachment ca{};
            WGPURenderPassDepthStencilAttachment da{};
            if (t.Depth)
            {
                da.view = view;
                da.depthLoadOp = WGPULoadOp_Clear;
                da.depthStoreOp = WGPUStoreOp_Store;
                da.depthClearValue = depth;
                if (t.Stencil)
                {
                    da.stencilLoadOp = WGPULoadOp_Clear;
                    da.stencilStoreOp = WGPUStoreOp_Store;
                    da.stencilClearValue = stencil;
                }
                rp.depthStencilAttachment = &da;
            }
            else
            {
                ca.view = view;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = { color[0], color[1], color[2], color[3] };
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
            }
            BeginPass(rp);
            EndPass();
            W().TextureViewRelease(view);
        }
        t.DefinedLevels |= 1u << level;
    }

    // ── Arabellekler ──
    WGPUBuffer BufferBinding(Buffer& b, uint64_t minSize, uint64_t& offset, uint64_t* size)
    {
        const uint64_t submit = SubmitSerial();
        if (b.Dirty)
        {
            ++b.Version;
            b.Dirty = false;
            if (b.WriteFrame == submit)
                ++b.WritesThisFrame;
            else
            {
                b.WriteFrame = submit;
                b.WritesThisFrame = 1;
            }
        }
        const uint64_t need = AlignUp(std::max<uint64_t>({ (uint64_t)b.Shadow.size(), minSize, 16 }), 16);
        if (b.GpuVersion == b.Version && b.GpuSize >= need && (!b.GpuIsRing || b.RingFrame == submit))
        {
            if (!b.GpuIsRing)
                b.DedicatedLastUse = submit;
            offset = b.GpuOffset;
            if (size)
                *size = b.GpuSize;
            return b.GpuBuffer;
        }
        static thread_local std::vector<uint8_t> padded;
        const size_t bytes = std::min<size_t>(b.Shadow.size(), (size_t)need);
        padded.assign((size_t)need, 0);
        if (bytes)
            std::memcpy(padded.data(), b.Shadow.data(), bytes);
        // Bu gönderimde kullanılmış kalıcı arabelleğe yazmak önceki çizimleri bozardı: o durumda ve devingen veride halka
        const bool ring = (b.Dedicated && b.DedicatedLastUse == submit) || (b.WriteFrame == submit && b.WritesThisFrame > 1) ||
                          (b.Usage == GL_STREAM_DRAW);
        if (ring)
        {
            uint64_t at = 0;
            WGPUBuffer rb = RingWrite(padded.data(), need, 256, at);
            if (!rb)
                return nullptr;
            b.GpuBuffer = rb;
            b.GpuOffset = at;
            b.GpuSize = need;
            b.GpuIsRing = true;
            b.RingFrame = submit;
        }
        else
        {
            if (!b.Dedicated || b.DedicatedSize < need)
            {
                if (b.Dedicated)
                    W().BufferRelease(b.Dedicated);
                WGPUBufferDescriptor bd{};
                bd.size = AlignUp(need, 256);
                bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst |
                           WGPUBufferUsage_CopySrc;
                b.Dedicated = W().DeviceCreateBuffer(Device(), &bd);
                b.DedicatedSize = bd.size;
            }
            if (!b.Dedicated)
                return nullptr;
            W().QueueWriteBuffer(Queue(), b.Dedicated, 0, padded.data(), (size_t)need);
            b.GpuBuffer = b.Dedicated;
            b.GpuOffset = 0;
            b.GpuSize = b.DedicatedSize;
            b.GpuIsRing = false;
            b.DedicatedLastUse = submit;
        }
        b.GpuVersion = b.Version;
        offset = b.GpuOffset;
        if (size)
            *size = b.GpuSize;
        return b.GpuBuffer;
    }

    void ReleaseBuffer(Buffer& b)
    {
        if (b.Dedicated)
            W().BufferRelease(b.Dedicated);
        b.Dedicated = nullptr;
        b.DedicatedSize = 0;
        b.GpuBuffer = nullptr;
        b.GpuVersion = 0;
        b.GpuOffset = b.GpuSize = 0;
        b.GpuIsRing = false;
    }

    void ReleaseSamplers()
    {
        for (auto& [k, s] : s_Samplers)
            W().SamplerRelease(s);
        s_Samplers.clear();
    }
}

#endif
