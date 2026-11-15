#include "DLSSUpscaler.h"

#if defined(CQ_DLSS_ENABLED) && defined(CQ_VULKAN_ENABLED)

// This software contains source code provided by NVIDIA Corporation (NGX SDK helper functions used through its headers).
#include "VkGLInterop.h"
#include "Core/Base/Version.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/Platform.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <glad/glad.h>

#include <algorithm>
#include <filesystem>

namespace Conqueror
{
    namespace
    {
        // Motorun NGX proje kimliği (özel motor, NVSDK_NGX_ENGINE_TYPE_CUSTOM)
        constexpr const char* kProjectId = "6f1c3b2e-8a4d-4f0b-9c7e-3d2a1b5c8e90";

        struct NGXState
        {
            bool Tried = false;
            bool Available = false;
            std::string Reason;
            NVSDK_NGX_Parameter* Params = nullptr;
        };

        NGXState& NGX()
        {
            static NGXState s;
            return s;
        }

        std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

        NVSDK_NGX_PerfQuality_Value QualityOf(int quality, bool native)
        {
            if (native)
                return NVSDK_NGX_PerfQuality_Value_DLAA;
            switch (quality)
            {
                case 0: return NVSDK_NGX_PerfQuality_Value_UltraQuality;
                case 1: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
                case 2: return NVSDK_NGX_PerfQuality_Value_Balanced;
                case 3: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
                case 4: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
                default: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
            }
        }

        // NGX: Vulkan aygıtı NGX'in istediği uzantılarla (GL ile paylaşımlı), sürücüdeki NGX çekirdeği, DLSS yeteneği
        bool InitNGX()
        {
            NGXState& n = NGX();
            if (n.Tried)
                return n.Available;
            n.Tried = true;
            unsigned int instCount = 0, devCount = 0;
            const char** instExt = nullptr;
            const char** devExt = nullptr;
            if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_RequiredExtensions(&instCount, &instExt, &devCount, &devExt)))
            {
                n.Reason = "NVIDIA NGX did not report its Vulkan requirements";
                return false;
            }
            VkGLInterop::Requirements req;
            for (unsigned int i = 0; i < instCount; ++i)
                req.InstanceExtensions.push_back(instExt[i]);
            for (unsigned int i = 0; i < devCount; ++i)
                req.DeviceExtensions.push_back(devExt[i]);
            auto& dev = VkGLInterop::Device::Get();
            dev.Shutdown();   // NGX'in uzantılarıyla yeniden (paylaşım öz-testi uzantısız açmış olabilir)
            std::string error;
            if (!dev.Init(req, error))
            {
                n.Reason = "DLSS needs OpenGL <-> Vulkan sharing: " + error;
                return false;
            }
            // NGX DLSS kitaplığını (nvngx_dlss.dll / libnvidia-ngx-dlss.so) oyunun yanında ve lib/ klasöründe arar
            const std::filesystem::path exeDir = std::filesystem::path(Platform::GetProcessPath()).parent_path();
            static std::wstring s_Paths[2] = { Wide(exeDir.string()), Wide((exeDir / "lib").string()) };
            static const wchar_t* s_PathList[2] = { s_Paths[0].c_str(), s_Paths[1].c_str() };
            NVSDK_NGX_FeatureCommonInfo info{};
            info.PathListInfo.Path = s_PathList;
            info.PathListInfo.Length = 2;
            const std::filesystem::path logs = std::filesystem::path(Platform::GetTempDirectory()) / "conqueror_ngx";
            std::error_code ec;
            std::filesystem::create_directories(logs, ec);
            const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, CQ_ENGINE_VERSION,
                                                                           Wide(logs.string()).c_str(), dev.Instance(), dev.Physical(), dev.Handle(),
                                                                           cq_vkGetInstanceProcAddr, cq_vkGetDeviceProcAddr, &info, NVSDK_NGX_Version_API);
            if (NVSDK_NGX_FAILED(r))
            {
                n.Reason = "NVIDIA NGX could not start on " + dev.Name() + " (NGX result 0x" + [&] {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%08x", (unsigned)r);
                    return std::string(buf);
                }() + "): DLSS needs an NVIDIA RTX GPU and driver";
                return false;
            }
            if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&n.Params)) || !n.Params)
            {
                n.Reason = "NVIDIA NGX returned no capability parameters";
                return false;
            }
            int available = 0, needsDriver = 0;
            NVSDK_NGX_Parameter_GetI(n.Params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
            NVSDK_NGX_Parameter_GetI(n.Params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
            if (needsDriver)
            {
                n.Reason = "DLSS needs a newer NVIDIA driver";
                return false;
            }
            if (!available)
            {
                int initResult = 0;
                NVSDK_NGX_Parameter_GetI(n.Params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
                n.Reason = "NVIDIA NGX reports DLSS is not available on this GPU (feature init result " + std::to_string(initResult) + ")";
                return false;
            }
            n.Available = true;
            CQ_CORE_INFO("[DLSS] NVIDIA DLSS available on {0}", dev.Name());
            return true;
        }

        // GL derinliği (D24S8 / D32F) -> R32F renk: NGX derinliği renk görüntüsü olarak okur, Vulkan'da D24S8 her GPU'da yok
        const char* kDepthVS = "#version 450 core\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
        const char* kDepthFS = "#version 450 core\nlayout(location = 0) out float o_Depth;\nlayout(binding = 0) uniform sampler2D u_Depth;\n"
                               "void main(){ o_Depth = texelFetch(u_Depth, ivec2(gl_FragCoord.xy), 0).r; }\n";
    }

    struct DLSSUpscaler::State
    {
        VkGLInterop::Image Color, Depth, Motion, Output;
        NVSDK_NGX_Handle* Feature = nullptr;
        uint32_t RenderW = 0, RenderH = 0, DisplayW = 0, DisplayH = 0;
        NVSDK_NGX_PerfQuality_Value Quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        GLuint DepthProgram = 0, DepthFramebuffer = 0, EmptyVAO = 0;
        bool Failed = false;
    };

    DLSSUpscaler::DLSSUpscaler() : m_State(std::make_unique<State>()) {}
    DLSSUpscaler::~DLSSUpscaler() { Destroy(); }

    bool DLSSUpscaler::IsCompiled() { return true; }

    bool DLSSUpscaler::IsAvailable(std::string& reason)
    {
        const bool ok = InitNGX();
        if (!ok)
            reason = NGX().Reason;
        return ok;
    }

    bool DLSSUpscaler::HasFailed() const { return m_State->Failed; }

    void DLSSUpscaler::Destroy()
    {
        State& s = *m_State;
        auto& dev = VkGLInterop::Device::Get();
        dev.WaitIdle();
        if (s.Feature)
            NVSDK_NGX_VULKAN_ReleaseFeature(s.Feature);
        s.Feature = nullptr;
        dev.DestroyImage(s.Color);
        dev.DestroyImage(s.Depth);
        dev.DestroyImage(s.Motion);
        dev.DestroyImage(s.Output);
        if (s.DepthProgram)
            glDeleteProgram(s.DepthProgram);
        if (s.DepthFramebuffer)
            glDeleteFramebuffers(1, &s.DepthFramebuffer);
        if (s.EmptyVAO)
            glDeleteVertexArrays(1, &s.EmptyVAO);
        s.DepthProgram = s.DepthFramebuffer = s.EmptyVAO = 0;
        s.RenderW = s.RenderH = s.DisplayW = s.DisplayH = 0;
    }

    bool DLSSUpscaler::Dispatch(const Params& p)
    {
        State& s = *m_State;
        if (s.Failed || !InitNGX())
            return false;
        auto& dev = VkGLInterop::Device::Get();
        std::string error;
        const NVSDK_NGX_PerfQuality_Value quality = QualityOf(p.Quality, p.Native);
        // Boyut ya da kalite değişti: DLSS özelliği ve paylaşımlı görüntüler yeniden
        if (p.RenderWidth != s.RenderW || p.RenderHeight != s.RenderH || p.DisplayWidth != s.DisplayW || p.DisplayHeight != s.DisplayH ||
            quality != s.Quality)
        {
            dev.WaitIdle();
            if (s.Feature)
                NVSDK_NGX_VULKAN_ReleaseFeature(s.Feature);
            s.Feature = nullptr;
            if (!dev.CreateImage(s.Color, p.RenderWidth, p.RenderHeight, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error) ||
                !dev.CreateImage(s.Depth, p.RenderWidth, p.RenderHeight, VK_FORMAT_R32_SFLOAT, GL_R32F, error) ||
                !dev.CreateImage(s.Motion, p.RenderWidth, p.RenderHeight, VK_FORMAT_R16G16_SFLOAT, GL_RG16F, error) ||
                !dev.CreateImage(s.Output, p.DisplayWidth, p.DisplayHeight, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error))
            {
                CQ_CORE_ERROR("[DLSS] {0}", error);
                s.Failed = true;
                return false;
            }
            s.RenderW = p.RenderWidth;
            s.RenderH = p.RenderHeight;
            s.DisplayW = p.DisplayWidth;
            s.DisplayH = p.DisplayHeight;
            s.Quality = quality;
        }

        // GL: girdileri paylaşımlı görüntülere kopyala (renk, hareket doğrudan; derinlik R32F'e çizilir)
        glCopyImageSubData(p.Color, GL_TEXTURE_2D, 0, 0, 0, 0, s.Color.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.RenderWidth,
                           (GLsizei)p.RenderHeight, 1);
        glCopyImageSubData(p.Motion, GL_TEXTURE_2D, 0, 0, 0, 0, s.Motion.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.RenderWidth,
                           (GLsizei)p.RenderHeight, 1);
        if (!s.DepthProgram)
        {
            auto compile = [](GLenum type, const char* src) {
                GLuint sh = glCreateShader(type);
                glShaderSource(sh, 1, &src, nullptr);
                glCompileShader(sh);
                return sh;
            };
            const GLuint vs = compile(GL_VERTEX_SHADER, kDepthVS), fs = compile(GL_FRAGMENT_SHADER, kDepthFS);
            s.DepthProgram = glCreateProgram();
            glAttachShader(s.DepthProgram, vs);
            glAttachShader(s.DepthProgram, fs);
            glLinkProgram(s.DepthProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            glCreateFramebuffers(1, &s.DepthFramebuffer);
            glCreateVertexArrays(1, &s.EmptyVAO);
        }
        {
            GLint prevProgram = 0, prevFramebuffer = 0, prevVAO = 0, prevViewport[4] = {};
            glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
            glGetIntegerv(GL_VIEWPORT, prevViewport);
            const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
            glNamedFramebufferTexture(s.DepthFramebuffer, GL_COLOR_ATTACHMENT0, s.Depth.GLTexture, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.DepthFramebuffer);
            glViewport(0, 0, (GLsizei)p.RenderWidth, (GLsizei)p.RenderHeight);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glUseProgram(s.DepthProgram);
            glBindTextureUnit(0, p.Depth);
            glBindVertexArray(s.EmptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray((GLuint)prevVAO);
            glUseProgram((GLuint)prevProgram);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevFramebuffer);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            if (depthTest) glEnable(GL_DEPTH_TEST);
            if (blend) glEnable(GL_BLEND);
        }

        // Vulkan: DLSS. Paylaşımlı görüntüler Vulkan'da GL'in satır düzeniyle (alt satır önce) görünür: dikey eksen ters, bu yüzden
        // titreşimin ve hareketin y bileşeni eksiyle verilir
        NGXState& n = NGX();
        const bool ok = dev.Execute({ &s.Color, &s.Depth, &s.Motion, &s.Output }, [&](VkCommandBuffer cb) {
            if (!s.Feature)
            {
                NVSDK_NGX_DLSS_Create_Params create{};
                create.Feature.InWidth = s.RenderW;
                create.Feature.InHeight = s.RenderH;
                create.Feature.InTargetWidth = s.DisplayW;
                create.Feature.InTargetHeight = s.DisplayH;
                create.Feature.InPerfQualityValue = s.Quality;
                create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;   // LDR girdi, OpenGL derinliği (ters değil)
                if (NVSDK_NGX_FAILED(NGX_VULKAN_CREATE_DLSS_EXT1(dev.Handle(), cb, 1, 1, &s.Feature, n.Params, &create)))
                    return false;
            }
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            NVSDK_NGX_Resource_VK color = NVSDK_NGX_Create_ImageView_Resource_VK(s.Color.View, s.Color.Handle, range, s.Color.Format, s.RenderW, s.RenderH, false);
            NVSDK_NGX_Resource_VK depth = NVSDK_NGX_Create_ImageView_Resource_VK(s.Depth.View, s.Depth.Handle, range, s.Depth.Format, s.RenderW, s.RenderH, false);
            NVSDK_NGX_Resource_VK motion = NVSDK_NGX_Create_ImageView_Resource_VK(s.Motion.View, s.Motion.Handle, range, s.Motion.Format, s.RenderW, s.RenderH, false);
            NVSDK_NGX_Resource_VK output = NVSDK_NGX_Create_ImageView_Resource_VK(s.Output.View, s.Output.Handle, range, s.Output.Format, s.DisplayW, s.DisplayH, true);
            NVSDK_NGX_VK_DLSS_Eval_Params eval{};
            eval.Feature.pInColor = &color;
            eval.Feature.pInOutput = &output;
            eval.pInDepth = &depth;
            eval.pInMotionVectors = &motion;
            eval.InJitterOffsetX = p.JitterPixels.x;
            eval.InJitterOffsetY = -p.JitterPixels.y;
            eval.InRenderSubrectDimensions = { s.RenderW, s.RenderH };
            eval.InReset = p.Reset ? 1 : 0;
            // Hareket vektörleri uv (önceki - şimdiki) -> iç çözünürlük pikseli (DLSS'in beklediği yön), y ters
            eval.InMVScaleX = (float)s.RenderW;
            eval.InMVScaleY = -(float)s.RenderH;
            eval.InFrameTimeDeltaInMsec = p.FrameTimeMs;
            return NVSDK_NGX_SUCCEED(NGX_VULKAN_EVALUATE_DLSS_EXT(cb, s.Feature, n.Params, &eval));
        }, error);
        if (!ok)
        {
            CQ_CORE_ERROR("[DLSS] Evaluation failed: {0}", error);
            s.Failed = true;
            return false;
        }
        glCopyImageSubData(s.Output.GLTexture, GL_TEXTURE_2D, 0, 0, 0, 0, p.Output, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)p.DisplayWidth,
                           (GLsizei)p.DisplayHeight, 1);
        return true;
    }
}

#else

// DLSS SDK'sız derleme (varsayılan): kullanılamaz, neden ayarlar ekranında
namespace Conqueror
{
    struct DLSSUpscaler::State
    {
    };

    DLSSUpscaler::DLSSUpscaler() : m_State(std::make_unique<State>()) {}
    DLSSUpscaler::~DLSSUpscaler() = default;
    bool DLSSUpscaler::Dispatch(const Params&) { return false; }
    void DLSSUpscaler::Destroy() {}
    bool DLSSUpscaler::HasFailed() const { return true; }
    bool DLSSUpscaler::IsCompiled() { return false; }

    bool DLSSUpscaler::IsAvailable(std::string& reason)
    {
        reason = "This engine build does not include the optional NVIDIA DLSS plugin (build it with Tools/DLSS/fetch-dlss-sdk.sh and "
                 "-DCQ_DLSS_SDK_DIR; NVIDIA's SDK license does not allow it in the open-source release)";
        return false;
    }
}

#endif
