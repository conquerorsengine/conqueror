#ifdef CQ_WEBGPU_ENABLED

// WebGPU cihazı, yüzey (takas zinciri), gönderim başına komut kodlayıcısı, yükleme halkası ve geri okuma.

#include "WgpuGL.h"
#include "Core/Logging/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
#else
    #if defined(_WIN32)
        #define GLFW_EXPOSE_NATIVE_WIN32
    #elif defined(__APPLE__)
        #define GLFW_EXPOSE_NATIVE_COCOA
    #else
        #if __has_include(<X11/Xlib.h>)
            #define GLFW_EXPOSE_NATIVE_X11
        #endif
        #if __has_include(<wayland-client.h>)
            #define GLFW_EXPOSE_NATIVE_WAYLAND
        #endif
    #endif
    #include <GLFW/glfw3native.h>
    #if defined(GLFW_EXPOSE_NATIVE_X11)
        #undef Status
        #undef Bool
        #undef None
        #undef Always
        #undef Success
    #endif
#endif

namespace Conqueror::WgpuGL
{
    using WGPU::W;
    using WGPU::Str;

    namespace
    {
        struct RingChunk
        {
            WGPUBuffer Buffer = nullptr;
            uint64_t Size = 0;
            uint64_t Used = 0;
            uint64_t Submit = 0;                         // son kullanan gönderim
        };

        struct Core
        {
            WGPUInstance Instance = nullptr;
            WGPUAdapter Adapter = nullptr;
            WGPUDevice Device = nullptr;
            WGPUQueue Queue = nullptr;
            WGPUSurface Surface = nullptr;
            WGPUTextureFormat SurfaceFormat = WGPUTextureFormat_BGRA8Unorm;
            WGPUPresentMode PresentMode = WGPUPresentMode_Fifo;
            uint32_t SurfaceWidth = 0, SurfaceHeight = 0;
            bool SurfaceConfigured = false;
            GLFWwindow* Window = nullptr;
            WGPUCommandEncoder Encoder = nullptr;
            WGPURenderPassEncoder Pass = nullptr;
            uint64_t Submit = 1;                         // kaydedilen gönderim
            uint64_t Completed = 0;                      // GPU'da biten son gönderim
            uint64_t Frame = 1;
            std::vector<RingChunk> Ring;
            size_t RingCurrent = 0;
            std::vector<WGPUFeatureName> Features;
            WGPULimits Limits{};
            std::string Name;
            bool VSync = true;
            bool Lost = false;
            std::vector<std::pair<void (*)(void*), void*>> AfterSubmit;
        };
        Core s_Core;

        void OnDeviceError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*)
        {
            static int s_Count = 0;
            if (++s_Count <= 50)
                CQ_CORE_ERROR("[WebGPU] {0} error: {1}", (int)type, WGPU::ToString(message));
            else if (s_Count == 51)
                CQ_CORE_ERROR("[WebGPU] too many errors, further errors are not shown");
        }

        void OnDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
        {
            if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled)
                return;
            s_Core.Lost = true;
            CQ_CORE_ERROR("[WebGPU] device lost ({0}): {1}", (int)reason, WGPU::ToString(message));
        }

#if !defined(__EMSCRIPTEN__)
        // Masaüstü: geri çağrılar ProcessEvents ile (Dawn ve wgpu-native)
        template<typename Pred>
        bool Pump(Pred done, int timeoutMs = 10000)
        {
            const auto start = std::chrono::steady_clock::now();
            while (!done())
            {
                W().InstanceProcessEvents(s_Core.Instance);
                if (done())
                    break;
                if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
                    return false;
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            return true;
        }

        WGPUSurface CreateSurface(GLFWwindow* window)
        {
            WGPUSurfaceDescriptor desc{};
    #if defined(_WIN32)
            WGPUSurfaceSourceWindowsHWND src{};
            src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            src.hinstance = GetModuleHandleW(nullptr);
            src.hwnd = glfwGetWin32Window(window);
            desc.nextInChain = &src.chain;
            return W().InstanceCreateSurface(s_Core.Instance, &desc);
    #elif defined(__APPLE__)
            (void)window;
            return nullptr;   // macOS: Metal arka ucu (WebGPU masaüstünde Linux / Windows)
    #else
        #if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
            if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            {
                WGPUSurfaceSourceWaylandSurface src{};
                src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                src.display = glfwGetWaylandDisplay();
                src.surface = glfwGetWaylandWindow(window);
                desc.nextInChain = &src.chain;
                return W().InstanceCreateSurface(s_Core.Instance, &desc);
            }
        #endif
        #if defined(GLFW_EXPOSE_NATIVE_X11)
            if (glfwGetPlatform() == GLFW_PLATFORM_X11)
            {
                WGPUSurfaceSourceXlibWindow src{};
                src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                src.display = glfwGetX11Display();
                src.window = (uint64_t)glfwGetX11Window(window);
                desc.nextInChain = &src.chain;
                return W().InstanceCreateSurface(s_Core.Instance, &desc);
            }
        #endif
            (void)window;
            return nullptr;
    #endif
        }
#endif

        void ConfigureSurface(uint32_t w, uint32_t h)
        {
            if (!s_Core.Surface || w == 0 || h == 0)
                return;
            WGPUSurfaceConfiguration cfg{};
            cfg.device = s_Core.Device;
            cfg.format = s_Core.SurfaceFormat;
            cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
            cfg.width = w;
            cfg.height = h;
            cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
            cfg.presentMode = s_Core.VSync ? WGPUPresentMode_Fifo : s_Core.PresentMode;
            W().SurfaceConfigure(s_Core.Surface, &cfg);
            s_Core.SurfaceWidth = w;
            s_Core.SurfaceHeight = h;
            s_Core.SurfaceConfigured = true;
        }

        void OnWorkDone(WGPUQueueWorkDoneStatus, WGPUStringView, void* userdata1, void*)
        {
            const uint64_t serial = (uint64_t)(uintptr_t)userdata1;
            s_Core.Completed = std::max(s_Core.Completed, serial);
        }
    }

    bool InitDevice(GLFWwindow* window, std::string& error)
    {
        if (!WGPU::Load(error))
            return false;
        s_Core = Core{};
        s_Core.Window = window;
#if defined(__EMSCRIPTEN__)
        // Sayfanın yükleyicisi cihazı önceden ister (tarayıcıda istek eşzamansızdır): Module.preinitializedWebGPUDevice
        s_Core.Instance = W().CreateInstance(nullptr);
        s_Core.Device = emscripten_webgpu_get_device();
        if (!s_Core.Device)
        {
            error = "the browser did not provide a WebGPU device (WebGPU unavailable or disabled)";
            return false;
        }
        s_Core.Name = "Browser WebGPU";
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
        canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvas.selector = Str("#canvas");
        WGPUSurfaceDescriptor sd{};
        sd.nextInChain = &canvas.chain;
        s_Core.Surface = W().InstanceCreateSurface(s_Core.Instance, &sd);
        s_Core.SurfaceFormat = WGPUTextureFormat_BGRA8Unorm;
        {
            WGPUSurfaceCapabilities caps{};
            if (s_Core.Surface && W().SurfaceGetCapabilities(s_Core.Surface, nullptr, &caps) == WGPUStatus_Success && caps.formatCount > 0)
                s_Core.SurfaceFormat = caps.formats[0];
            W().SurfaceCapabilitiesFreeMembers(caps);
        }
#else
        WGPUInstanceDescriptor id{};
        s_Core.Instance = W().CreateInstance(&id);
        if (!s_Core.Instance)
        {
            error = "wgpuCreateInstance failed";
            return false;
        }
        s_Core.Surface = CreateSurface(window);
        if (!s_Core.Surface)
        {
            error = "cannot create a WebGPU surface for this window";
            return false;
        }
        struct AdapterResult { bool Done = false; WGPUAdapter Adapter = nullptr; std::string Message; } ar;
        WGPURequestAdapterOptions ao{};
        ao.compatibleSurface = s_Core.Surface;
        ao.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo acb{};
        acb.mode = WGPUCallbackMode_AllowProcessEvents;
        acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* ud, void*) {
            auto* r = static_cast<AdapterResult*>(ud);
            r->Done = true;
            if (status == WGPURequestAdapterStatus_Success)
                r->Adapter = adapter;
            else
                r->Message = WGPU::ToString(message);
        };
        acb.userdata1 = &ar;
        W().InstanceRequestAdapter(s_Core.Instance, &ao, acb);
        Pump([&] { return ar.Done; });
        if (!ar.Adapter)
        {
            error = "no WebGPU adapter: " + ar.Message;
            return false;
        }
        s_Core.Adapter = ar.Adapter;
        {
            WGPUAdapterInfo info{};
            if (W().AdapterGetInfo(s_Core.Adapter, &info) == WGPUStatus_Success)
                s_Core.Name = WGPU::ToString(info.device) + " (" + WGPU::ToString(info.description) + ")";
            W().AdapterInfoFreeMembers(info);
        }
        // İsteğe bağlı özellikler: 32 bit float filtreleme, RG11B10 çizim hedefi, BC sıkıştırma
        std::vector<WGPUFeatureName> wanted;
        for (WGPUFeatureName f : { WGPUFeatureName_Float32Filterable, WGPUFeatureName_RG11B10UfloatRenderable, WGPUFeatureName_TextureCompressionBC,
                                   WGPUFeatureName_Depth32FloatStencil8 })
            if (W().AdapterHasFeature(s_Core.Adapter, f))
                wanted.push_back(f);
        WGPULimits adapterLimits{};
        W().AdapterGetLimits(s_Core.Adapter, &adapterLimits);
        WGPULimits required = adapterLimits;            // adaptörün verdiği en yüksek sınırlar
        struct DeviceResult { bool Done = false; WGPUDevice Device = nullptr; std::string Message; } dr;
        WGPUDeviceDescriptor dd{};
        dd.label = Str("Conqueror");
        dd.requiredFeatureCount = wanted.size();
        dd.requiredFeatures = wanted.data();
        dd.requiredLimits = &required;
        dd.uncapturedErrorCallbackInfo.callback = &OnDeviceError;
        dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        dd.deviceLostCallbackInfo.callback = &OnDeviceLost;
        WGPURequestDeviceCallbackInfo dcb{};
        dcb.mode = WGPUCallbackMode_AllowProcessEvents;
        dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* ud, void*) {
            auto* r = static_cast<DeviceResult*>(ud);
            r->Done = true;
            if (status == WGPURequestDeviceStatus_Success)
                r->Device = device;
            else
                r->Message = WGPU::ToString(message);
        };
        dcb.userdata1 = &dr;
        W().AdapterRequestDevice(s_Core.Adapter, &dd, dcb);
        Pump([&] { return dr.Done; });
        if (!dr.Device)
        {
            error = "WebGPU device request failed: " + dr.Message;
            return false;
        }
        s_Core.Device = dr.Device;
        {
            WGPUSurfaceCapabilities caps{};
            if (W().SurfaceGetCapabilities(s_Core.Surface, s_Core.Adapter, &caps) == WGPUStatus_Success)
            {
                // Arka arabellek kendi biçiminde; yüzeyde sRGB olmayan 8 bit tercih edilir (motor ton eşlemeyi kendisi yapar)
                for (size_t i = 0; i < caps.formatCount; ++i)
                    if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
                    {
                        s_Core.SurfaceFormat = caps.formats[i];
                        break;
                    }
                for (size_t i = 0; i < caps.presentModeCount; ++i)
                    if (caps.presentModes[i] == WGPUPresentMode_Immediate || caps.presentModes[i] == WGPUPresentMode_Mailbox)
                        s_Core.PresentMode = caps.presentModes[i];
            }
            W().SurfaceCapabilitiesFreeMembers(caps);
        }
#endif
        s_Core.Queue = W().DeviceGetQueue(s_Core.Device);
        W().DeviceGetLimits(s_Core.Device, &s_Core.Limits);
        for (WGPUFeatureName f : { WGPUFeatureName_Float32Filterable, WGPUFeatureName_RG11B10UfloatRenderable, WGPUFeatureName_TextureCompressionBC,
                                   WGPUFeatureName_Depth32FloatStencil8 })
            if (W().DeviceHasFeature(s_Core.Device, f))
                s_Core.Features.push_back(f);
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        ConfigureSurface((uint32_t)std::max(1, fbw), (uint32_t)std::max(1, fbh));
        return true;
    }

    void ShutdownDevice()
    {
        if (!s_Core.Device)
            return;
        EndPass();
        if (s_Core.Encoder)
        {
            W().CommandEncoderRelease(s_Core.Encoder);
            s_Core.Encoder = nullptr;
        }
        for (auto& r : s_Core.Ring)
            if (r.Buffer)
            {
                W().BufferDestroy(r.Buffer);
                W().BufferRelease(r.Buffer);
            }
        s_Core.Ring.clear();
        if (s_Core.Surface)
        {
            if (s_Core.SurfaceConfigured)
                W().SurfaceUnconfigure(s_Core.Surface);
            W().SurfaceRelease(s_Core.Surface);
        }
        if (s_Core.Queue) W().QueueRelease(s_Core.Queue);
        W().DeviceRelease(s_Core.Device);
        if (s_Core.Adapter) W().AdapterRelease(s_Core.Adapter);
        if (s_Core.Instance) W().InstanceRelease(s_Core.Instance);
        s_Core = Core{};
    }

    void SetVSync(bool enabled)
    {
        if (s_Core.VSync == enabled)
            return;
        s_Core.VSync = enabled;
        if (s_Core.SurfaceConfigured)
            ConfigureSurface(s_Core.SurfaceWidth, s_Core.SurfaceHeight);
    }

    const std::string& DeviceName() { return s_Core.Name; }
    WGPUDevice Device() { return s_Core.Device; }
    WGPUQueue Queue() { return s_Core.Queue; }
    uint64_t SubmitSerial() { return s_Core.Submit; }
    uint64_t FrameSerial() { return s_Core.Frame; }
    WGPUTextureFormat SurfaceFormat() { return s_Core.SurfaceFormat; }

    const WGPULimits& DeviceLimits() { return s_Core.Limits; }

    bool HasFeature(WGPUFeatureName feature)
    {
        return std::find(s_Core.Features.begin(), s_Core.Features.end(), feature) != s_Core.Features.end();
    }

    WGPUCommandEncoder Encoder()
    {
        if (!s_Core.Encoder)
        {
            WGPUCommandEncoderDescriptor d{};
            s_Core.Encoder = W().DeviceCreateCommandEncoder(s_Core.Device, &d);
        }
        return s_Core.Encoder;
    }

    WGPURenderPassEncoder RenderPass() { return s_Core.Pass; }

    WGPURenderPassEncoder BeginPass(const WGPURenderPassDescriptor& desc)
    {
        EndPass();
        s_Core.Pass = W().CommandEncoderBeginRenderPass(Encoder(), &desc);
        return s_Core.Pass;
    }

    void EndPass()
    {
        if (!s_Core.Pass)
            return;
        W().RenderPassEncoderEnd(s_Core.Pass);
        W().RenderPassEncoderRelease(s_Core.Pass);
        s_Core.Pass = nullptr;
    }

    void Submit()
    {
        EndPass();
        if (!s_Core.Encoder)
            return;
        WGPUCommandBufferDescriptor cd{};
        WGPUCommandBuffer cb = W().CommandEncoderFinish(s_Core.Encoder, &cd);
        W().CommandEncoderRelease(s_Core.Encoder);
        s_Core.Encoder = nullptr;
        W().QueueSubmit(s_Core.Queue, 1, &cb);
        W().CommandBufferRelease(cb);
        auto after = std::move(s_Core.AfterSubmit);
        s_Core.AfterSubmit.clear();
        for (auto& [fn, user] : after)
            fn(user);
        WGPUQueueWorkDoneCallbackInfo wd{};
#if defined(__EMSCRIPTEN__)
        wd.mode = WGPUCallbackMode_AllowSpontaneous;
#else
        wd.mode = WGPUCallbackMode_AllowProcessEvents;
#endif
        wd.callback = &OnWorkDone;
        wd.userdata1 = (void*)(uintptr_t)s_Core.Submit;
        W().QueueOnSubmittedWorkDone(s_Core.Queue, wd);
        ++s_Core.Submit;
    }

    WGPUBuffer RingWrite(const void* data, uint64_t size, uint64_t alignment, uint64_t& offset)
    {
        alignment = std::max<uint64_t>(alignment, 4);
        const uint64_t padded = (size + 3) & ~3ull;     // writeBuffer boyu 4'ün katı
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (s_Core.RingCurrent < s_Core.Ring.size())
            {
                RingChunk& r = s_Core.Ring[s_Core.RingCurrent];
                const uint64_t at = (r.Used + alignment - 1) / alignment * alignment;
                if (at + padded <= r.Size)
                {
                    if (padded == size)
                        W().QueueWriteBuffer(s_Core.Queue, r.Buffer, at, data, size);
                    else
                    {
                        std::vector<uint8_t> tmp(padded, 0);
                        std::memcpy(tmp.data(), data, size);
                        W().QueueWriteBuffer(s_Core.Queue, r.Buffer, at, tmp.data(), padded);
                    }
                    r.Used = at + padded;
                    r.Submit = s_Core.Submit;
                    offset = at;
                    return r.Buffer;
                }
            }
            // Sıradaki boş parça: GPU'nun bitirdiği bir parça yeniden kullanılır, yoksa yenisi açılır
            size_t next = s_Core.Ring.size();
            for (size_t i = 0; i < s_Core.Ring.size(); ++i)
                if (i != s_Core.RingCurrent && s_Core.Ring[i].Submit <= s_Core.Completed && s_Core.Ring[i].Size >= padded)
                {
                    next = i;
                    break;
                }
            if (next == s_Core.Ring.size())
            {
                RingChunk r;
                r.Size = std::max<uint64_t>(16ull << 20, (padded + alignment) * 2);
                WGPUBufferDescriptor bd{};
                bd.size = r.Size;
                bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst |
                           WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
                r.Buffer = W().DeviceCreateBuffer(s_Core.Device, &bd);
                s_Core.Ring.push_back(r);
            }
            s_Core.RingCurrent = next;
            s_Core.Ring[next].Used = 0;
        }
        offset = 0;
        return nullptr;
    }

    void AfterSubmit(void (*fn)(void*), void* user) { s_Core.AfterSubmit.emplace_back(fn, user); }

    bool WaitIdle()
    {
#if defined(__EMSCRIPTEN__)
        return false;
#else
        Submit();
        const uint64_t target = s_Core.Submit - 1;
        return Pump([&] { return s_Core.Completed >= target; });
#endif
    }

    bool MapRead(WGPUBuffer buffer, uint64_t size, std::vector<uint8_t>& out)
    {
#if defined(__EMSCRIPTEN__)
        (void)buffer;
        (void)size;
        (void)out;
        return false;
#else
        Submit();
        struct MapResult { bool Done = false; bool Ok = false; } mr;
        WGPUBufferMapCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud, void*) {
            auto* r = static_cast<MapResult*>(ud);
            r->Done = true;
            r->Ok = status == WGPUMapAsyncStatus_Success;
        };
        cb.userdata1 = &mr;
        W().BufferMapAsync(buffer, WGPUMapMode_Read, 0, (size_t)size, cb);
        if (!Pump([&] { return mr.Done; }) || !mr.Ok)
            return false;
        const void* p = W().BufferGetConstMappedRange(buffer, 0, (size_t)size);
        if (p)
            out.assign((const uint8_t*)p, (const uint8_t*)p + size);
        W().BufferUnmap(buffer);
        return p != nullptr;
#endif
    }

    bool AcquireSurface(WGPUTexture& texture, uint32_t& width, uint32_t& height)
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(s_Core.Window, &fbw, &fbh);
        if (fbw <= 0 || fbh <= 0)
            return false;
        if (!s_Core.SurfaceConfigured || (uint32_t)fbw != s_Core.SurfaceWidth || (uint32_t)fbh != s_Core.SurfaceHeight)
            ConfigureSurface((uint32_t)fbw, (uint32_t)fbh);
        WGPUSurfaceTexture st{};
        W().SurfaceGetCurrentTexture(s_Core.Surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        {
            if (st.texture)
                W().TextureRelease(st.texture);
            ConfigureSurface((uint32_t)fbw, (uint32_t)fbh);
            return false;
        }
        texture = st.texture;
        width = s_Core.SurfaceWidth;
        height = s_Core.SurfaceHeight;
        return true;
    }

    bool SurfaceSize(uint32_t& width, uint32_t& height)
    {
        int fbw = 0, fbh = 0;
        if (s_Core.Window)
            glfwGetFramebufferSize(s_Core.Window, &fbw, &fbh);
        width = (uint32_t)std::max(0, fbw);
        height = (uint32_t)std::max(0, fbh);
        return width > 0 && height > 0;
    }

    void PresentSurface()
    {
        Submit();
#if !defined(__EMSCRIPTEN__)
        W().SurfacePresent(s_Core.Surface);   // Web: tarayıcı karenin sonunda kendisi gösterir
        W().InstanceProcessEvents(s_Core.Instance);
#endif
        ++s_Core.Frame;
    }
}

#endif
