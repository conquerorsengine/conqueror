// Apple MetalFX zamansal yükseltici: GL dokularının Metal karşılıkları (MtlGL) MTLFXTemporalScaler'a verilir. Çıktı MetalFX'in
// istediği kullanım bayraklarıyla ayrı bir dokuya yazılır, sonra GL çıktı dokusuna kopyalanır.
#ifdef CQ_METAL_ENABLED

#include "Renderer/Upscalers/MetalFXUpscaler.h"
#include "MtlGL.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/RendererAPI.h"

#include <TargetConditionals.h>

// iOS Simülatöründe MetalFX yok (SDK'da başlığı da yok): orada yer tutucu (aşağıda); CMake de simülatörde MetalFX'e bağlamaz
#if __has_include(<MetalFX/MetalFX.h>) && !TARGET_OS_SIMULATOR
#import <MetalFX/MetalFX.h>

namespace Conqueror
{
    struct MetalFXUpscaler::State
    {
        id<MTLFXTemporalScaler> Scaler = nil;
        id<MTLTexture> Output = nil;
        uint32_t RenderW = 0, RenderH = 0, DisplayW = 0, DisplayH = 0;
        MTLPixelFormat ColorFormat = MTLPixelFormatInvalid, DepthFormat = MTLPixelFormatInvalid, MotionFormat = MTLPixelFormatInvalid;
        bool Failed = false;
    };

    MetalFXUpscaler::MetalFXUpscaler() : m_State(std::make_unique<State>()) {}
    MetalFXUpscaler::~MetalFXUpscaler() { Destroy(); }

    bool MetalFXUpscaler::HasFailed() const { return m_State->Failed; }

    bool MetalFXUpscaler::IsAvailable(std::string& reason)
    {
        if (RendererAPI::GetBackend() != RendererAPI::Backend::Metal || !MTL::Core::Get().IsInitialized())
        {
            reason = "Runs only with the Metal graphics API (Player Settings > Graphics API > Metal).";
            return false;
        }
        if (@available(macOS 13.0, iOS 16.0, *))
        {
            if (![MTLFXTemporalScalerDescriptor supportsDevice:MTL::Core::Get().Device()])
            {
                reason = "This GPU (" + MTL::Core::Get().DeviceName() + ") does not support the MetalFX temporal scaler (an Apple silicon GPU is needed).";
                return false;
            }
            return true;
        }
        reason = "MetalFX needs macOS 13 / iOS 16 or newer.";
        return false;
    }

    void MetalFXUpscaler::Destroy()
    {
        State& s = *m_State;
        s.Scaler = nil;
        s.Output = nil;
        s.RenderW = s.RenderH = s.DisplayW = s.DisplayH = 0;
    }

    bool MetalFXUpscaler::Dispatch(const Params& p)
    {
        State& s = *m_State;
        std::string reason;
        if (s.Failed || !IsAvailable(reason))
            return false;
        if (@available(macOS 13.0, iOS 16.0, *))
        {
            MtlGL::Texture* color = MtlGL::GetTexture(p.Color);
            MtlGL::Texture* depth = MtlGL::GetTexture(p.Depth);
            MtlGL::Texture* motion = MtlGL::GetTexture(p.Motion);
            MtlGL::Texture* output = MtlGL::GetTexture(p.Output);
            if (!color || !depth || !motion || !output || !color->Image || !depth->Image || !motion->Image || !output->Image)
            {
                CQ_CORE_ERROR("[MetalFX] Input textures are missing");
                s.Failed = true;
                return false;
            }
            id<MTLDevice> device = MTL::Core::Get().Device();
            if (!s.Scaler || p.RenderWidth != s.RenderW || p.RenderHeight != s.RenderH || p.DisplayWidth != s.DisplayW || p.DisplayHeight != s.DisplayH ||
                color->Image.pixelFormat != s.ColorFormat || depth->Image.pixelFormat != s.DepthFormat || motion->Image.pixelFormat != s.MotionFormat)
            {
                MTLFXTemporalScalerDescriptor* d = [MTLFXTemporalScalerDescriptor new];
                d.colorTextureFormat = color->Image.pixelFormat;
                d.depthTextureFormat = depth->Image.pixelFormat;
                d.motionTextureFormat = motion->Image.pixelFormat;
                d.outputTextureFormat = output->Image.pixelFormat;
                d.inputWidth = p.RenderWidth;
                d.inputHeight = p.RenderHeight;
                d.outputWidth = p.DisplayWidth;
                d.outputHeight = p.DisplayHeight;
                d.autoExposureEnabled = NO;
                s.Scaler = [d newTemporalScalerWithDevice:device];
                if (!s.Scaler)
                {
                    CQ_CORE_ERROR("[MetalFX] Cannot create the temporal scaler ({0}x{1} -> {2}x{3})", p.RenderWidth, p.RenderHeight, p.DisplayWidth,
                                  p.DisplayHeight);
                    s.Failed = true;
                    return false;
                }
                MTLTextureDescriptor* od = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:output->Image.pixelFormat width:p.DisplayWidth
                                                                                            height:p.DisplayHeight mipmapped:NO];
                od.usage = s.Scaler.outputTextureUsage | MTLTextureUsageShaderRead;
                od.storageMode = MTLStorageModePrivate;
                s.Output = [device newTextureWithDescriptor:od];
                s.RenderW = p.RenderWidth;
                s.RenderH = p.RenderHeight;
                s.DisplayW = p.DisplayWidth;
                s.DisplayH = p.DisplayHeight;
                s.ColorFormat = color->Image.pixelFormat;
                s.DepthFormat = depth->Image.pixelFormat;
                s.MotionFormat = motion->Image.pixelFormat;
            }
            // MtlGL dokuları GL'in satır düzeninde (alt satır önce): MetalFX'in gördüğü dikey eksen ters, hareketin ve titreşimin y'si eksi
            MtlGL::EndRendering();
            id<MTLCommandBuffer> cb = MTL::Core::Get().CommandBuffer();
            s.Scaler.colorTexture = color->Image;
            s.Scaler.depthTexture = depth->Image;
            s.Scaler.motionTexture = motion->Image;
            s.Scaler.outputTexture = s.Output;
            s.Scaler.inputContentWidth = p.RenderWidth;
            s.Scaler.inputContentHeight = p.RenderHeight;
            s.Scaler.jitterOffsetX = p.JitterPixels.x;
            s.Scaler.jitterOffsetY = -p.JitterPixels.y;
            // Hareket uv (önceki - şimdiki) -> iç çözünürlük pikseli
            s.Scaler.motionVectorScaleX = (float)p.RenderWidth;
            s.Scaler.motionVectorScaleY = -(float)p.RenderHeight;
            s.Scaler.depthReversed = NO;
            s.Scaler.reset = p.Reset ? YES : NO;
            [s.Scaler encodeToCommandBuffer:cb];
            id<MTLBlitCommandEncoder> blit = MTL::Core::Get().Blit();
            [blit copyFromTexture:s.Output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(p.DisplayWidth, p.DisplayHeight, 1) toTexture:output->Image destinationSlice:0
                 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            MTL::Core::Get().EndEncoding();
            return true;
        }
        return false;
    }
}

#else

namespace Conqueror
{
    struct MetalFXUpscaler::State
    {
    };

    MetalFXUpscaler::MetalFXUpscaler() : m_State(std::make_unique<State>()) {}
    MetalFXUpscaler::~MetalFXUpscaler() = default;
    bool MetalFXUpscaler::Dispatch(const Params&) { return false; }
    void MetalFXUpscaler::Destroy() {}
    bool MetalFXUpscaler::HasFailed() const { return true; }

    bool MetalFXUpscaler::IsAvailable(std::string& reason)
    {
        reason = "MetalFX is not part of this SDK (the iOS Simulator has no MetalFX).";
        return false;
    }
}

#endif // MetalFX

#endif
