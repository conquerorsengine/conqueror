#pragma once

#ifdef CQ_METAL_ENABLED

// Yalnızca Objective-C++ (.mm, ARC) dosyalarından eklenir.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Conqueror::MTL
{
    // Arabellek dizinleri: köşe öznitelikleri 0..15, sabit arabellekler (yazmaç b) 16 + b
    constexpr uint32_t kConstantBufferBase = 16;
    constexpr uint32_t kMaxConstantBuffers = 12;
    constexpr uint32_t kMaxTextures = 31;
    constexpr uint32_t kMaxSamplers = 16;
    constexpr uint32_t kFrames = 3;

    struct UploadAlloc
    {
        uint8_t* Cpu = nullptr;
        id<MTLBuffer> Buffer = nil;
        uint64_t Offset = 0;
    };

    // Metal cihazı, komut kuyruğu, kare halkası (yükleme arabellekleri), CAMetalLayer ve kodlayıcı yönetimi.
    // Aynı anda tek kodlayıcı açıktır: çizimler render kodlayıcısında, kopyalar blit kodlayıcısında.
    class Core
    {
    public:
        static Core& Get();

        bool Init(GLFWwindow* window, std::string& error);
        void Shutdown();
        bool IsInitialized() const { return m_Device != nil; }

        id<MTLDevice> Device() const { return m_Device; }
        const std::string& DeviceName() const { return m_DeviceName; }
        uint64_t VideoMemory() const { return m_VideoMemory; }
        uint32_t FramebufferWidth() const;
        uint32_t FramebufferHeight() const;
        void SetVSync(bool enabled);

        id<MTLCommandBuffer> CommandBuffer();
        // Render kodlayıcısı (MtlGL açar); başka kodlayıcı istenince kapatılır
        id<MTLRenderCommandEncoder> RenderEncoder() const { return m_Render; }
        id<MTLRenderCommandEncoder> BeginRender(MTLRenderPassDescriptor* pass);
        id<MTLBlitCommandEncoder> Blit();
        void EndEncoding();
        // Render kodlayıcısı her kapandığında artar (MtlGL bağlı durumu ve hedefleri yeniden kurar)
        uint64_t EncoderEpoch() const { return m_EncoderEpoch; }

        // Komutları gönder ve GPU'yu bekle (geri okuma)
        void Flush();
        uint64_t FrameSerial() const { return m_FrameSerial; }
        uint64_t CompletedFrame() const { return m_CompletedFrame; }

        UploadAlloc AllocUpload(uint64_t size, uint64_t alignment);

        // Sunum: geçerli çizilebilir (yoksa nil) ve kare sonu
        id<CAMetalDrawable> Drawable();
        void Present();

        static uint64_t NowNs();

    private:
        struct Frame
        {
            std::vector<id<MTLBuffer>> Chunks;           // ilk parça kalıcı, taşmalar kare bitince bırakılır
            size_t Chunk = 0;
            uint64_t Offset = 0;
        };
        bool NewChunk(Frame& f, uint64_t size);

        GLFWwindow* m_Window = nullptr;
        id<MTLDevice> m_Device = nil;
        id<MTLCommandQueue> m_Queue = nil;
        CAMetalLayer* m_Layer = nil;
        id<MTLCommandBuffer> m_Command = nil;
        id<MTLRenderCommandEncoder> m_Render = nil;
        id<MTLBlitCommandEncoder> m_Blit = nil;
        id<CAMetalDrawable> m_Drawable = nil;
        dispatch_semaphore_t m_Inflight = nullptr;
        Frame m_Frames[kFrames];
        uint32_t m_FrameIndex = 0;
        uint64_t m_FrameSerial = 1;
        volatile uint64_t m_CompletedFrame = 0;
        uint64_t m_EncoderEpoch = 1;
        std::string m_DeviceName;
        uint64_t m_VideoMemory = 0;
    };
}

#endif
