#ifdef CQ_METAL_ENABLED

#include "MtlGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Conqueror::MtlGL
{
    using MTL::Core;
    using GLEmu::TexelKind;

    namespace
    {
        MTLPixelFormat PixelFormatOf(TexelKind k)
        {
            switch (k)
            {
                case TexelKind::RGBA8: return MTLPixelFormatRGBA8Unorm;
                case TexelKind::RGBA8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
                case TexelKind::R8: return MTLPixelFormatR8Unorm;
                case TexelKind::RG8: return MTLPixelFormatRG8Unorm;
                case TexelKind::RGBA16F: return MTLPixelFormatRGBA16Float;
                case TexelKind::RGBA32F: return MTLPixelFormatRGBA32Float;
                case TexelKind::R16F: return MTLPixelFormatR16Float;
                case TexelKind::RG16F: return MTLPixelFormatRG16Float;
                case TexelKind::R32F: return MTLPixelFormatR32Float;
                case TexelKind::RG32F: return MTLPixelFormatRG32Float;
                case TexelKind::RG11B10F: return MTLPixelFormatRG11B10Float;
                case TexelKind::RGBA16: return MTLPixelFormatRGBA16Unorm;
                case TexelKind::R16: return MTLPixelFormatR16Unorm;
                case TexelKind::RG16: return MTLPixelFormatRG16Unorm;
                case TexelKind::R32I: return MTLPixelFormatR32Sint;
                case TexelKind::R32UI: return MTLPixelFormatR32Uint;
                case TexelKind::RG32I: return MTLPixelFormatRG32Sint;
                case TexelKind::RGBA32I: return MTLPixelFormatRGBA32Sint;
                case TexelKind::R8I: return MTLPixelFormatR8Sint;
                case TexelKind::R8UI: return MTLPixelFormatR8Uint;
                case TexelKind::D16: return MTLPixelFormatDepth16Unorm;
                case TexelKind::D32F: return MTLPixelFormatDepth32Float;
                // Apple GPU'larında D24S8 yok: her iki şablonlu biçim de 32F + 8
                case TexelKind::D24S8: case TexelKind::D32FS8: return MTLPixelFormatDepth32Float_Stencil8;
            }
            return MTLPixelFormatRGBA8Unorm;
        }

        uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

        // Hedefi tek geçişte temizle (boş kodlayıcı: yükleme eylemi = temizle)
        void ClearImage(Texture& t, int level, int layer, const float color[4], float depth, uint8_t stencil)
        {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            if (t.Depth)
            {
                pass.depthAttachment.texture = t.Image;
                pass.depthAttachment.level = (NSUInteger)level;
                pass.depthAttachment.slice = (NSUInteger)layer;
                pass.depthAttachment.loadAction = MTLLoadActionClear;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.depthAttachment.clearDepth = depth;
                if (t.Stencil)
                {
                    pass.stencilAttachment.texture = t.Image;
                    pass.stencilAttachment.level = (NSUInteger)level;
                    pass.stencilAttachment.slice = (NSUInteger)layer;
                    pass.stencilAttachment.loadAction = MTLLoadActionClear;
                    pass.stencilAttachment.storeAction = MTLStoreActionStore;
                    pass.stencilAttachment.clearStencil = stencil;
                }
            }
            else
            {
                pass.colorAttachments[0].texture = t.Image;
                pass.colorAttachments[0].level = (NSUInteger)level;
                pass.colorAttachments[0].slice = (NSUInteger)layer;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(color[0], color[1], color[2], color[3]);
            }
            Core::Get().BeginRender(pass);
            Core::Get().EndEncoding();
        }
    }

    Context& Ctx()
    {
        static Context c;
        return c;
    }

    Texture* GetTexture(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Textures.find(name);
        return it == c.Textures.end() ? nullptr : it->second.get();
    }

    Texture* GetRenderbuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Renderbuffers.find(name);
        return it == c.Renderbuffers.end() ? nullptr : it->second.get();
    }

    Buffer* GetBuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Buffers.find(name);
        return it == c.Buffers.end() ? nullptr : it->second.get();
    }

    Framebuffer* GetFramebuffer(uint32_t name)
    {
        auto& c = Ctx();
        if (name == 0)
            return &c.DefaultFB;
        auto it = c.Framebuffers.find(name);
        return it == c.Framebuffers.end() ? nullptr : it->second.get();
    }

    Program* GetProgram(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Programs.find(name);
        return it == c.Programs.end() ? nullptr : it->second.get();
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
        // Komut arabellekleri kullandıkları dokuları tutar: hemen bırakmak güvenli
        t.Image = nil;
        t.View = nil;
        t.ViewLevels = 0;
        t.DefinedLevels = 0;
    }

    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples)
    {
        DestroyTextureImage(t);
        GLEmu::TexelInfo ti = GLEmu::TexelOf(internalFormat);
        if (ti.Kind == TexelKind::D24S8)
            ti = GLEmu::TexelInfoOf(TexelKind::D32FS8);   // Metal'de 32F + 8 (geri okuma düzeni de float)
        t.Target = target;
        t.InternalFormat = internalFormat;
        t.Kind = ti.Kind;
        t.Format = PixelFormatOf(ti.Kind);
        t.Depth = ti.Depth;
        t.Stencil = ti.Stencil;
        t.Integer = ti.Integer;
        t.Renderable = true;
        t.Width = std::max(1u, w);
        t.Height = std::max(1u, h);
        t.Levels = std::max(1u, std::min(levels, GLEmu::MaxLevels(t.Width, t.Height)));
        t.Layers = target == GL_TEXTURE_CUBE_MAP ? 6u : 1u;
        t.Samples = std::max(1u, samples);
        if (t.Samples > 1)
            t.Levels = 1;

        MTLTextureDescriptor* d = [MTLTextureDescriptor new];
        d.textureType = t.Layers == 6 ? MTLTextureTypeCube : t.Samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
        d.pixelFormat = t.Format;
        d.width = t.Width;
        d.height = t.Height;
        d.mipmapLevelCount = t.Levels;
        d.sampleCount = t.Samples;
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        t.Image = [Core::Get().Device() newTextureWithDescriptor:d];
        if (!t.Image)
        {
            CQ_CORE_ERROR("[MtlGL] Cannot create {0}x{1} texture (format {2}, {3} samples)", t.Width, t.Height, (int)t.Format, t.Samples);
            return false;
        }
        // GL'de tanımsız; temizlemek tutarlı sonuç verir (diğer katmanlarla aynı)
        const float zero[4] = { 0, 0, 0, 0 };
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
            ClearImage(t, 0, (int)layer, zero, 1.0f, 0);
        return true;
    }

    id<MTLTexture> SampleView(Texture& t)
    {
        if (!t.Image)
            return nil;
        int levels = (int)t.Levels;
        if (!t.Immutable && t.DefinedLevels)
        {
            levels = 0;
            while (levels < (int)t.Levels && (t.DefinedLevels & (1u << levels)))
                ++levels;
            levels = std::max(levels, 1);
        }
        levels = std::clamp(std::min(levels, t.MaxLevel + 1), 1, (int)t.Levels);
        if (levels == (int)t.Levels)
            return t.Image;
        if (t.View && t.ViewLevels == levels)
            return t.View;
        t.View = [t.Image newTextureViewWithPixelFormat:t.Format textureType:t.Image.textureType levels:NSMakeRange(0, (NSUInteger)levels)
                                                 slices:NSMakeRange(0, t.Layers)];
        t.ViewLevels = levels;
        return t.View ? t.View : t.Image;
    }

    id<MTLSamplerState> GetSampler(const Texture& p, bool integer, bool comparison)
    {
        auto& c = Ctx();
        auto linear = [](GLenum f) { return !(f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR); };
        auto address = [](GLenum w) {
            switch (w)
            {
                case GL_CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
                case GL_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
                case GL_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
                default: return MTLSamplerAddressModeRepeat;
            }
        };
        const bool mip = p.MinFilter != GL_NEAREST && p.MinFilter != GL_LINEAR;
        const bool mipLinear = p.MinFilter == GL_NEAREST_MIPMAP_LINEAR || p.MinFilter == GL_LINEAR_MIPMAP_LINEAR;
        const bool minL = !integer && linear(p.MinFilter), magL = !integer && linear(p.MagFilter);
        const MTLSamplerMipFilter mipFilter = !mip ? MTLSamplerMipFilterNotMipmapped : (mipLinear && !integer) ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        const NSUInteger aniso = (NSUInteger)std::clamp(p.MaxAnisotropy, 1.0f, 16.0f);
        MTLSamplerBorderColor border = MTLSamplerBorderColorTransparentBlack;
        if (p.BorderColor[3] > 0.5f)
            border = p.BorderColor[0] > 0.5f ? MTLSamplerBorderColorOpaqueWhite : MTLSamplerBorderColorOpaqueBlack;
        MTLCompareFunction compare = MTLCompareFunctionNever;
        if (comparison)
            switch (p.CompareFunc)
            {
                case GL_NEVER: compare = MTLCompareFunctionNever; break;
                case GL_LESS: compare = MTLCompareFunctionLess; break;
                case GL_EQUAL: compare = MTLCompareFunctionEqual; break;
                case GL_GREATER: compare = MTLCompareFunctionGreater; break;
                case GL_NOTEQUAL: compare = MTLCompareFunctionNotEqual; break;
                case GL_GEQUAL: compare = MTLCompareFunctionGreaterEqual; break;
                case GL_ALWAYS: compare = MTLCompareFunctionAlways; break;
                default: compare = MTLCompareFunctionLessEqual; break;
            }
        const float minLod = mip ? std::max(0.0f, p.MinLod) : 0.0f;
        const float maxLod = mip ? std::max(minLod, std::min(p.MaxLod, 1000.0f)) : 0.0f;

        uint64_t key = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mix(minL); mix(magL); mix(mipFilter); mix(address(p.WrapS)); mix(address(p.WrapT)); mix(address(p.WrapR)); mix(aniso); mix(border);
        mix(comparison ? (uint64_t)compare + 1 : 0); mix((uint64_t)(int64_t)(minLod * 64.0f)); mix((uint64_t)(int64_t)(maxLod * 64.0f));
        if (auto it = c.SamplerCache.find(key); it != c.SamplerCache.end())
            return it->second;
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = minL ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.magFilter = magL ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.mipFilter = mipFilter;
        sd.sAddressMode = address(p.WrapS);
        sd.tAddressMode = address(p.WrapT);
        sd.rAddressMode = address(p.WrapR);
        sd.borderColor = border;
        sd.maxAnisotropy = (!integer && mip) ? aniso : 1;
        sd.lodMinClamp = minLod;
        sd.lodMaxClamp = maxLod;
        sd.normalizedCoordinates = YES;
        if (comparison)
            sd.compareFunction = compare;
        id<MTLSamplerState> s = [Core::Get().Device() newSamplerStateWithDescriptor:sd];
        c.SamplerCache[key] = s;
        return s;
    }

    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || !data || w <= 0 || h <= 0)
            return;
        if (t.Depth || t.Samples > 1)
        {
            CQ_CORE_WARN("[MtlGL] Uploads to depth / multisample textures are not supported");
            return;
        }
        auto& c = Ctx();
        auto& core = Core::Get();
        EndRendering();
        static thread_local std::vector<uint8_t> staging;
        size_t srcPitch = 0;
        const uint8_t* src = GLEmu::ConvertUpload(t.Kind, w, h, format, type, data, c.S.UnpackRowLength, c.S.UnpackAlignment, staging, srcPitch);
        const uint64_t rowBytes = (uint64_t)w * (uint64_t)GLEmu::TexelInfoOf(t.Kind).Bytes;
        const uint64_t pitch = AlignUp(rowBytes, 256);
        MTL::UploadAlloc a = core.AllocUpload(pitch * (uint64_t)h, 256);
        if (!a.Cpu)
            return;
        for (int row = 0; row < h; ++row)
            std::memcpy(a.Cpu + pitch * (uint64_t)row, src + srcPitch * (size_t)row, (size_t)rowBytes);
        [core.Blit() copyFromBuffer:a.Buffer
                       sourceOffset:a.Offset
                  sourceBytesPerRow:pitch
                sourceBytesPerImage:pitch * (uint64_t)h
                         sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
                          toTexture:t.Image
                   destinationSlice:(NSUInteger)layer
                   destinationLevel:(NSUInteger)level
                  destinationOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)];
    }

    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        if (!t.Image || !out || w <= 0 || h <= 0)
            return;
        auto& core = Core::Get();
        EndRendering();
        id<MTLTexture> source = t.Image;
        NSUInteger slice = (NSUInteger)layer, srcLevel = (NSUInteger)level;
        if (t.Samples > 1)
        {
            if (t.Depth)
            {
                CQ_CORE_WARN("[MtlGL] Reading a multisample depth texture is not supported");
                return;
            }
            // Çözümleme: boş geçiş, saklama eylemi = çoklu örnek çözümle
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.Format width:t.Width height:t.Height mipmapped:NO];
            d.storageMode = MTLStorageModePrivate;
            d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            id<MTLTexture> resolved = [core.Device() newTextureWithDescriptor:d];
            if (!resolved)
                return;
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = t.Image;
            pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
            pass.colorAttachments[0].resolveTexture = resolved;
            core.BeginRender(pass);
            core.EndEncoding();
            source = resolved;
            slice = 0;
            srcLevel = 0;
        }
        const uint32_t bytes = (uint32_t)GLEmu::TexelInfoOf(t.Kind).Bytes;
        const uint64_t pitch = AlignUp((uint64_t)w * bytes, 256);
        id<MTLBuffer> readback = [core.Device() newBufferWithLength:pitch * (uint64_t)h options:MTLResourceStorageModeShared];
        if (!readback)
            return;
        [core.Blit() copyFromTexture:source
                         sourceSlice:slice
                         sourceLevel:srcLevel
                        sourceOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)
                          sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
                            toBuffer:readback
                   destinationOffset:0
              destinationBytesPerRow:pitch
            destinationBytesPerImage:pitch * (uint64_t)h
                             options:t.Stencil ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
        core.Flush();
        GLEmu::ConvertReadback(t.Kind, (const uint8_t*)[readback contents], (size_t)pitch, w, h, format, type, out, packAlignment);
    }

    bool GrowLevels(Texture& t, uint32_t levels)
    {
        levels = std::min(levels, GLEmu::MaxLevels(t.Width, t.Height));
        if (!t.Image || levels <= t.Levels || t.Samples > 1)
            return t.Image != nil;
        id<MTLTexture> old = t.Image;
        const uint32_t oldLevels = t.Levels;
        const uint32_t defined = t.DefinedLevels;
        if (!AllocateTexture(t, t.Target, t.InternalFormat, t.Width, t.Height, levels, 1))
            return false;
        auto blit = Core::Get().Blit();
        for (uint32_t l = 0; l < oldLevels; ++l)
        {
            if (!(defined & (1u << l)))
                continue;
            const NSUInteger lw = std::max(1u, t.Width >> l), lh = std::max(1u, t.Height >> l);
            for (uint32_t layer = 0; layer < t.Layers; ++layer)
                [blit copyFromTexture:old
                          sourceSlice:layer
                          sourceLevel:l
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(lw, lh, 1)
                            toTexture:t.Image
                     destinationSlice:layer
                     destinationLevel:l
                    destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
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
            ClearImage(t, level, (int)layer, color, depth, stencil);
        t.DefinedLevels |= 1u << level;
    }

    // ── Arabellekler ──
    id<MTLBuffer> BufferBinding(Buffer& b, uint64_t minSize, uint64_t& offset, uint64_t* size)
    {
        auto& core = Core::Get();
        const uint64_t frame = core.FrameSerial();
        if (b.Dirty)
        {
            ++b.Version;
            b.Dirty = false;
            if (b.WriteFrame == frame)
                ++b.WritesThisFrame;
            else
            {
                b.WriteFrame = frame;
                b.WritesThisFrame = 1;
            }
        }
        const uint64_t need = AlignUp(std::max<uint64_t>({ (uint64_t)b.Shadow.size(), minSize, 16 }), 256);
        if (b.GpuVersion == b.Version && b.GpuSize >= need && (!b.GpuIsRing || b.RingFrame == frame))
        {
            if (!b.GpuIsRing)
                b.DedicatedLastUse = frame;
            offset = b.GpuOffset;
            if (size)
                *size = b.GpuSize;
            return b.GpuBuffer;
        }
        const size_t bytes = std::min<size_t>(b.Shadow.size(), (size_t)need);
        // Bu kare yazılan devingen veri halkadan (yalnızca bu kare geçerli); kararlı veri kalıcı arabellekte
        const bool ring = b.WriteFrame == frame && (b.WritesThisFrame > 1 || b.Usage != GL_STATIC_DRAW);
        if (ring)
        {
            MTL::UploadAlloc a = core.AllocUpload(need, 256);
            if (!a.Cpu)
                return nil;
            if (bytes)
                std::memcpy(a.Cpu, b.Shadow.data(), bytes);
            if (need > bytes)
                std::memset(a.Cpu + bytes, 0, (size_t)(need - bytes));
            b.GpuBuffer = a.Buffer;
            b.GpuOffset = a.Offset;
            b.GpuSize = need;
            b.GpuIsRing = true;
            b.RingFrame = frame;
        }
        else
        {
            // GPU hâlâ okuyor olabilir (bu kare ya da uçuştaki kareler): yeni arabellek (eskisini komut arabellekleri tutar)
            if (!b.Dedicated || [b.Dedicated length] < need || b.DedicatedLastUse > core.CompletedFrame())
                b.Dedicated = [core.Device() newBufferWithLength:AlignUp(need, 4096) options:MTLResourceStorageModeShared];
            if (!b.Dedicated)
                return nil;
            auto* dst = (uint8_t*)[b.Dedicated contents];
            if (bytes)
                std::memcpy(dst, b.Shadow.data(), bytes);
            if (need > bytes)
                std::memset(dst + bytes, 0, (size_t)(need - bytes));
            b.GpuBuffer = b.Dedicated;
            b.GpuOffset = 0;
            b.GpuSize = [b.Dedicated length];
            b.GpuIsRing = false;
            b.DedicatedLastUse = frame;
        }
        b.GpuVersion = b.Version;
        offset = b.GpuOffset;
        if (size)
            *size = b.GpuSize;
        return b.GpuBuffer;
    }

    void ReleaseBuffer(Buffer& b)
    {
        b.Dedicated = nil;
        b.GpuBuffer = nil;
        b.GpuVersion = 0;
        b.GpuOffset = b.GpuSize = 0;
        b.GpuIsRing = false;
    }
}

#endif
