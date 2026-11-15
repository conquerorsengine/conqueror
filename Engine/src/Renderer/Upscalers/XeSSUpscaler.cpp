#include "XeSSUpscaler.h"

#if defined(_WIN32) && defined(CQ_VULKAN_ENABLED)

#include "VkGLInterop.h"
#include "Core/Logging/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <xess/xess_vk.h>

#include <glad/glad.h>

namespace Conqueror
{
    namespace
    {
        // libxess.dll çalışma anında: oyunun yanında yoksa XeSS kullanılamaz (Intel Simplified Software License ile dağıtılabilir)
        struct XeSSApi
        {
            HMODULE Dll = nullptr;
            decltype(&xessVKGetRequiredInstanceExtensions) GetInstanceExtensions = nullptr;
            decltype(&xessVKGetRequiredDeviceExtensions) GetDeviceExtensions = nullptr;
            decltype(&xessVKGetRequiredDeviceFeatures) GetDeviceFeatures = nullptr;
            decltype(&xessVKCreateContext) CreateContext = nullptr;
            decltype(&xessVKInit) Init = nullptr;
            decltype(&xessVKExecute) Execute = nullptr;
            decltype(&xessDestroyContext) DestroyContext = nullptr;
            decltype(&xessSetVelocityScale) SetVelocityScale = nullptr;
        };

        struct XeSSState
        {
            bool Tried = false;
            bool Available = false;
            std::string Reason;
            XeSSApi Api;
        };

        XeSSState& X()
        {
            static XeSSState s;
            return s;
        }

        template <typename T>
        bool Resolve(HMODULE dll, const char* name, T& fn)
        {
            fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
            return fn != nullptr;
        }

        xess_quality_settings_t QualityOf(int quality, bool native)
        {
            if (native)
                return XESS_QUALITY_SETTING_AA;
            switch (quality)
            {
                case 0: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
                case 1: return XESS_QUALITY_SETTING_QUALITY;
                case 2: return XESS_QUALITY_SETTING_BALANCED;
                case 3: return XESS_QUALITY_SETTING_PERFORMANCE;
                case 4: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
                default: return XESS_QUALITY_SETTING_QUALITY;
            }
        }

        bool InitXeSS()
        {
            XeSSState& x = X();
            if (x.Tried)
                return x.Available;
            x.Tried = true;
            XeSSApi& a = x.Api;
            a.Dll = LoadLibraryA("libxess.dll");
            if (!a.Dll)
            {
                x.Reason = "The Intel XeSS runtime (libxess.dll) was not found next to the game";
                return false;
            }
            if (!Resolve(a.Dll, "xessVKGetRequiredInstanceExtensions", a.GetInstanceExtensions) ||
                !Resolve(a.Dll, "xessVKGetRequiredDeviceExtensions", a.GetDeviceExtensions) ||
                !Resolve(a.Dll, "xessVKGetRequiredDeviceFeatures", a.GetDeviceFeatures) || !Resolve(a.Dll, "xessVKCreateContext", a.CreateContext) ||
                !Resolve(a.Dll, "xessVKInit", a.Init) || !Resolve(a.Dll, "xessVKExecute", a.Execute) ||
                !Resolve(a.Dll, "xessDestroyContext", a.DestroyContext) || !Resolve(a.Dll, "xessSetVelocityScale", a.SetVelocityScale))
            {
                x.Reason = "libxess.dll has no Vulkan API (XeSS 1.3 or newer is needed)";
                return false;
            }
            uint32_t instCount = 0, minApi = 0;
            const char* const* instExt = nullptr;
            if (a.GetInstanceExtensions(&instCount, &instExt, &minApi) != XESS_RESULT_SUCCESS)
            {
                x.Reason = "XeSS did not report its Vulkan requirements";
                return false;
            }
            VkGLInterop::Requirements req;
            for (uint32_t i = 0; i < instCount; ++i)
                req.InstanceExtensions.push_back(instExt[i]);
            req.ApiVersion = minApi;
            req.BeforeDevice = [&a](VkInstance instance, VkPhysicalDevice physical, VkGLInterop::Requirements& r, std::string& error) {
                uint32_t count = 0;
                const char* const* ext = nullptr;
                if (a.GetDeviceExtensions(instance, physical, &count, &ext) != XESS_RESULT_SUCCESS)
                {
                    error = "XeSS does not support this GPU's Vulkan device";
                    return false;
                }
                for (uint32_t i = 0; i < count; ++i)
                    r.DeviceExtensions.push_back(ext[i]);
                void* features = nullptr;
                if (a.GetDeviceFeatures(instance, physical, &features) != XESS_RESULT_SUCCESS)
                {
                    error = "XeSS did not report its Vulkan device features";
                    return false;
                }
                r.FeatureChain = features;
                return true;
            };
            auto& dev = VkGLInterop::Device::Get();
            dev.Shutdown();   // XeSS'in uzantı ve özellikleriyle yeniden
            std::string error;
            if (!dev.Init(req, error))
            {
                x.Reason = "XeSS needs OpenGL <-> Vulkan sharing: " + error;
                return false;
            }
            xess_context_handle_t probe = nullptr;
            if (a.CreateContext(dev.Instance(), dev.Physical(), dev.Handle(), &probe) != XESS_RESULT_SUCCESS || !probe)
            {
                x.Reason = "XeSS could not create a context on " + dev.Name();
                return false;
            }
            a.DestroyContext(probe);
            x.Available = true;
            CQ_CORE_INFO("[XeSS] Intel XeSS available on {0}", dev.Name());
            return true;
        }
    }

    struct XeSSUpscaler::State
    {
        VkGLInterop::Image Color, Depth, Motion, Output;
        xess_context_handle_t Context = nullptr;
        uint32_t RenderW = 0, RenderH = 0, DisplayW = 0, DisplayH = 0;
        xess_quality_settings_t Quality = XESS_QUALITY_SETTING_QUALITY;
        bool Failed = false;
    };

    XeSSUpscaler::XeSSUpscaler() : m_State(std::make_unique<State>()) {}
    XeSSUpscaler::~XeSSUpscaler() { Destroy(); }

    bool XeSSUpscaler::IsAvailable(std::string& reason)
    {
        const bool ok = InitXeSS();
        if (!ok)
            reason = X().Reason;
        return ok;
    }

    bool XeSSUpscaler::HasFailed() const { return m_State->Failed; }

    void XeSSUpscaler::Destroy()
    {
        State& s = *m_State;
        auto& dev = VkGLInterop::Device::Get();
        dev.WaitIdle();
        if (s.Context && X().Api.DestroyContext)
            X().Api.DestroyContext(s.Context);
        s.Context = nullptr;
        dev.DestroyImage(s.Color);
        dev.DestroyImage(s.Depth);
        dev.DestroyImage(s.Motion);
        dev.DestroyImage(s.Output);
        s.RenderW = s.RenderH = s.DisplayW = s.DisplayH = 0;
    }

    bool XeSSUpscaler::Dispatch(const Params& p)
    {
        State& s = *m_State;
        if (s.Failed || !InitXeSS())
            return false;
        XeSSApi& a = X().Api;
        auto& dev = VkGLInterop::Device::Get();
        std::string error;
        const xess_quality_settings_t quality = QualityOf(p.Quality, p.Native);
        if (p.RenderWidth != s.RenderW || p.RenderHeight != s.RenderH || p.DisplayWidth != s.DisplayW || p.DisplayHeight != s.DisplayH ||
            quality != s.Quality || !s.Context)
        {
            dev.WaitIdle();
            if (s.Context)
                a.DestroyContext(s.Context);
            s.Context = nullptr;
            if (!dev.CreateImage(s.Color, p.RenderWidth, p.RenderHeight, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error) ||
                !dev.CreateImage(s.Depth, p.RenderWidth, p.RenderHeight, VK_FORMAT_R32_SFLOAT, GL_R32F, error) ||
                !dev.CreateImage(s.Motion, p.RenderWidth, p.RenderHeight, VK_FORMAT_R16G16_SFLOAT, GL_RG16F, error) ||
                !dev.CreateImage(s.Output, p.DisplayWidth, p.DisplayHeight, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error))
            {
                CQ_CORE_ERROR("[XeSS] {0}", error);
                s.Failed = true;
                return false;
            }
            xess_vk_init_params_t init{};
            init.outputResolution = { p.DisplayWidth, p.DisplayHeight };
            init.qualitySetting = quality;
            init.initFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR;   // ton eşlenmiş girdi, OpenGL derinliği (ters değil), düşük çözünürlüklü hareket
            init.creationNodeMask = 1;
            init.visibleNodeMask = 1;
            if (a.CreateContext(dev.Instance(), dev.Physical(), dev.Handle(), &s.Context) != XESS_RESULT_SUCCESS ||
                a.Init(s.Context, &init) != XESS_RESULT_SUCCESS)
            {
                CQ_CORE_ERROR("[XeSS] Context initialization failed");
                s.Failed = true;
                return false;
            }
            // Hareket uv (önceki - şimdiki) -> piksel; paylaşımlı görüntüler Vulkan'da dikey ters
            a.SetVelocityScale(s.Context, (float)p.RenderWidth, -(float)p.RenderHeight);
            s.RenderW = p.RenderWidth;
            s.RenderH = p.RenderHeight;
            s.DisplayW = p.DisplayWidth;
            s.DisplayH = p.DisplayHeight;
            s.Quality = quality;
        }

        glCopyImageSubData(p.Color, GL_TEXTURE_2D, 0, 0, 0, 0, s.Color.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.RenderWidth,
                           (GLsizei)p.RenderHeight, 1);
        glCopyImageSubData(p.Motion, GL_TEXTURE_2D, 0, 0, 0, 0, s.Motion.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.RenderWidth,
                           (GLsizei)p.RenderHeight, 1);
        dev.CopyDepth(p.Depth, s.Depth);

        const bool ok = dev.Execute({ &s.Color, &s.Depth, &s.Motion, &s.Output }, [&](VkCommandBuffer cb) {
            // XeSS girdileri SHADER_READ_ONLY_OPTIMAL ister, çıktı GENERAL: girdiler geçici olarak geçirilir, sonra GENERAL'a döner
            VkImageMemoryBarrier barriers[3] = {};
            VkImage inputs[3] = { s.Color.Handle, s.Depth.Handle, s.Motion.Handle };
            auto transition = [&](VkImageLayout from, VkImageLayout to) {
                for (int i = 0; i < 3; ++i)
                {
                    barriers[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    barriers[i].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                    barriers[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                    barriers[i].oldLayout = from;
                    barriers[i].newLayout = to;
                    barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barriers[i].image = inputs[i];
                    barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                }
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 3, barriers);
            };
            transition(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            auto view = [&](const VkGLInterop::Image& img) {
                return xess_vk_image_view_info{ img.View, img.Handle, range, img.Format, img.Width, img.Height };
            };
            xess_vk_execute_params_t exec{};
            exec.colorTexture = view(s.Color);
            exec.velocityTexture = view(s.Motion);
            exec.depthTexture = view(s.Depth);
            exec.outputTexture = view(s.Output);
            exec.jitterOffsetX = p.JitterPixels.x;
            exec.jitterOffsetY = -p.JitterPixels.y;
            exec.exposureScale = 1.0f;
            exec.resetHistory = p.Reset ? 1u : 0u;
            exec.inputWidth = s.RenderW;
            exec.inputHeight = s.RenderH;
            const bool done = a.Execute(s.Context, cb, &exec) == XESS_RESULT_SUCCESS;
            transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            return done;
        }, error);
        if (!ok)
        {
            CQ_CORE_ERROR("[XeSS] Execution failed: {0}", error);
            s.Failed = true;
            return false;
        }
        glCopyImageSubData(s.Output.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, p.Output, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.DisplayWidth,
                           (GLsizei)p.DisplayHeight, 1);
        return true;
    }
}

#else

namespace Conqueror
{
    struct XeSSUpscaler::State
    {
    };

    XeSSUpscaler::XeSSUpscaler() : m_State(std::make_unique<State>()) {}
    XeSSUpscaler::~XeSSUpscaler() = default;
    bool XeSSUpscaler::Dispatch(const Params&) { return false; }
    void XeSSUpscaler::Destroy() {}
    bool XeSSUpscaler::HasFailed() const { return true; }

    bool XeSSUpscaler::IsAvailable(std::string& reason)
    {
        reason = "The Intel XeSS SDK runs only on Windows (Direct3D 11 / 12, Vulkan); there is no Linux, macOS or OpenGL version.";
        return false;
    }
}

#endif
