#include "OpenXR.h"

#include "Core/Application.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/XRInteraction.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
    #define CQ_OPENXR_LINUX 1
    #include <dlfcn.h>
    #if __has_include(<EGL/egl.h>)
        #include <EGL/egl.h>
    #else
        // EGL başlıkları yoksa (ör. yalnız X11 paketli çapraz sistem kökü): OpenXR bağlama yapısının ve bizim kullandığımız türler
        #include <cstdint>
        typedef void* EGLDisplay;
        typedef void* EGLConfig;
        typedef void* EGLContext;
        typedef int32_t EGLint;
        typedef unsigned int EGLBoolean;
        typedef void (*__eglMustCastToProperFunctionPointerType)(void);
        typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(const char* procname);
        #define EGL_NO_DISPLAY ((EGLDisplay)0)
        #define EGL_NO_CONTEXT ((EGLContext)0)
        #define EGL_CONFIG_ID 0x3028
        #define EGL_NONE 0x3038
    #endif
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    // GLX türleri (GL/glx.h glad ile çakışır): yalnız OpenXR bağlama yapısının istedikleri
    typedef struct __GLXcontextRec* GLXContext;
    typedef struct __GLXFBConfigRec* GLXFBConfig;
    typedef XID GLXDrawable;
    #define XR_USE_PLATFORM_XLIB
    #define XR_USE_PLATFORM_EGL
#elif defined(_WIN32)
    #define CQ_OPENXR_WINDOWS 1
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #define XR_USE_PLATFORM_WIN32
#elif defined(__ANDROID__)
    // Meta Quest / Pico / Android XR: Khronos Android yükleyicisi (APK'da libopenxr_loader.so), OpenGL ES bağlamı
    #define CQ_OPENXR_ANDROID 1
    #include <dlfcn.h>
    #include <jni.h>
    #include <EGL/egl.h>
    #include <android_native_app_glue.h>
    #include "Platform/Android/AndroidPlatform.h"
    #define XR_USE_PLATFORM_ANDROID
#endif

#if defined(CQ_OPENXR_LINUX) || defined(CQ_OPENXR_WINDOWS) || defined(CQ_OPENXR_ANDROID)
    #define CQ_OPENXR_SUPPORTED 1
    #define XR_NO_PROTOTYPES
    #if defined(CQ_OPENXR_ANDROID)
        #define XR_USE_GRAPHICS_API_OPENGL_ES
    #else
        #define XR_USE_GRAPHICS_API_OPENGL
    #endif
    #include <openxr/openxr.h>
    #include <openxr/openxr_platform.h>
#endif
#if defined(CQ_OPENXR_LINUX)
    // Xlib makroları motor adlarıyla çakışıyor (OpenXR::Status -> int)
    #undef Status
    #undef Bool
    #undef None
    #undef Always
    #undef Success
#endif

#if defined(CQ_OPENXR_LINUX)
// GLFW yerel erişimi (glfw3native.h GL/glx.h ister): zayıf bağ, GLFW'nin o arka ucu yoksa boş
extern "C"
{
    __attribute__((weak)) Display* glfwGetX11Display(void);
    __attribute__((weak)) GLXContext glfwGetGLXContext(GLFWwindow* window);
    __attribute__((weak)) GLXDrawable glfwGetGLXWindow(GLFWwindow* window);
    __attribute__((weak)) EGLDisplay glfwGetEGLDisplay(void);
    __attribute__((weak)) EGLContext glfwGetEGLContext(GLFWwindow* window);
}
#elif defined(CQ_OPENXR_WINDOWS)
extern "C"
{
    HGLRC glfwGetWGLContext(GLFWwindow* window);
    HWND glfwGetWin32Window(GLFWwindow* window);
}
#endif

namespace Conqueror::OpenXR
{
    namespace
    {
        Status s_Status;
    }

    const Status& GetStatus() { return s_Status; }

    bool WantedAtStartup()
    {
        if (const char* env = std::getenv("CQ_OPENXR"))
            return env[0] == '1';
        if (!Project::GetActive())
            return false;
        const auto& xr = Project::GetActive()->GetConfig().XR;
        return xr.InitializeOnStartup && xr.OpenXR;
    }

#if !defined(CQ_OPENXR_SUPPORTED)
    bool Start()
    {
        s_Status = {};
        s_Status.Error = "OpenXR is not supported on this platform yet";
        return false;
    }
    void Stop() {}
    bool IsRunning() { return false; }
    void RenderFrame(Scene&) {}
    bool HeadPose(glm::vec3&, glm::quat&) { return false; }
    bool HandPose(int, glm::vec3&, glm::quat&) { return false; }
    HandInput Hand(int) { return {}; }
    void Haptic(int, float, float) {}
    bool ProbeEyeMean(int, glm::vec3&) { return false; }
#else
    namespace
    {
        // ---------------------------------------------------------------- yükleyici ve işlevler
        void* s_Loader = nullptr;
        PFN_xrGetInstanceProcAddr s_GetProc = nullptr;
        XrInstance s_Instance = XR_NULL_HANDLE;
        XrSystemId s_System = XR_NULL_SYSTEM_ID;
        XrSession s_Session = XR_NULL_HANDLE;
        XrSpace s_AppSpace = XR_NULL_HANDLE, s_ViewSpace = XR_NULL_HANDLE;
        XrSessionState s_State = XR_SESSION_STATE_UNKNOWN;
        bool s_Running = false;

        #define CQ_XR_FUNCTIONS(X) \
            X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrGetSystem) X(xrGetSystemProperties) X(xrPollEvent) X(xrResultToString) \
            X(xrEnumerateViewConfigurationViews) X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) \
            X(xrRequestExitSession) X(xrEnumerateReferenceSpaces) X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace) \
            X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
            X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) \
            X(xrEndFrame) X(xrLocateViews) X(xrStringToPath) X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) \
            X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrCreateActionSpace) X(xrSyncActions) \
            X(xrGetActionStateFloat) X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) X(xrGetActionStatePose) \
            X(xrApplyHapticFeedback)
        struct Functions
        {
            #define CQ_XR_MEMBER(name) PFN_##name name = nullptr;
            CQ_XR_FUNCTIONS(CQ_XR_MEMBER)
            #undef CQ_XR_MEMBER
#if defined(CQ_OPENXR_ANDROID)
            PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetGraphicsRequirements = nullptr;
#else
            PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetGraphicsRequirements = nullptr;
#endif
        } s_Fn;

#if defined(CQ_OPENXR_ANDROID)
        using SwapchainImage = XrSwapchainImageOpenGLESKHR;
        constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        constexpr bool kDesktopGL = false;   // ES: sRGB eke yazım her zaman dönüştürülür (GL_FRAMEBUFFER_SRGB yok)
        const char* const kGraphicsExtension = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
#else
        using SwapchainImage = XrSwapchainImageOpenGLKHR;
        constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        constexpr bool kDesktopGL = true;
        const char* const kGraphicsExtension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#endif

        bool Check(XrResult r, const char* what)
        {
            if (XR_SUCCEEDED(r))
                return true;
            char text[XR_MAX_RESULT_STRING_SIZE] = "";
            if (s_Fn.xrResultToString && s_Instance)
                s_Fn.xrResultToString(s_Instance, r, text);
            s_Status.Error = std::string(what) + ": " + (text[0] ? text : std::to_string((int)r));
            CQ_CORE_WARN("[OpenXR] {0}", s_Status.Error);
            return false;
        }

        bool LoadLoader()
        {
            if (s_GetProc)
                return true;
#if defined(CQ_OPENXR_LINUX) || defined(CQ_OPENXR_ANDROID)
            for (const char* name : { "libopenxr_loader.so.1", "libopenxr_loader.so" })
                if ((s_Loader = dlopen(name, RTLD_NOW | RTLD_LOCAL)))
                    break;
            if (s_Loader)
                s_GetProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(s_Loader, "xrGetInstanceProcAddr"));
    #if defined(CQ_OPENXR_ANDROID)
            // Android yükleyicisi uzantı sorgusundan önce Java VM + etkinlikle başlatılmalı (XR_KHR_loader_init_android)
            if (s_GetProc)
            {
                PFN_xrInitializeLoaderKHR init = nullptr;
                s_GetProc(XR_NULL_HANDLE, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction*>(&init));
                android_app* app = Android::GetApp();
                if (!init || !app || !app->activity)
                    return (s_GetProc = nullptr), false;
                XrLoaderInitInfoAndroidKHR loaderInfo{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
                loaderInfo.applicationVM = app->activity->vm;
                loaderInfo.applicationContext = app->activity->clazz;
                if (XR_FAILED(init(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo))))
                    return (s_GetProc = nullptr), false;
            }
    #endif
#else
            HMODULE lib = LoadLibraryA("openxr_loader.dll");
            s_Loader = lib;
            if (lib)
                s_GetProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(lib, "xrGetInstanceProcAddr"));
#endif
            return s_GetProc != nullptr;
        }

        template<typename F>
        F Proc(XrInstance instance, const char* name)
        {
            PFN_xrVoidFunction fn = nullptr;
            s_GetProc(instance, name, &fn);
            return reinterpret_cast<F>(fn);
        }

        // ---------------------------------------------------------------- gözler
        struct Eye
        {
            XrSwapchain Swapchain = XR_NULL_HANDLE;
            std::vector<SwapchainImage> Images;
            std::vector<GLuint> ImageFbos;
            uint32_t Width = 0, Height = 0;
            GLuint HdrFbo = 0, HdrColor = 0, HdrDepth = 0;
        };
        Eye s_Eyes[2];
        int64_t s_Format = 0;
        bool s_SrgbSwapchain = false;
        std::shared_ptr<Shader> s_Resolve;
        GLuint s_Vao = 0;

        // ---------------------------------------------------------------- eylemler (kumandalar)
        XrActionSet s_ActionSet = XR_NULL_HANDLE;
        XrAction s_GripPose = XR_NULL_HANDLE, s_AimPose = XR_NULL_HANDLE, s_Trigger = XR_NULL_HANDLE, s_Squeeze = XR_NULL_HANDLE,
                 s_PrimaryButton = XR_NULL_HANDLE, s_SecondaryButton = XR_NULL_HANDLE, s_MenuButton = XR_NULL_HANDLE,
                 s_Stick = XR_NULL_HANDLE, s_StickClick = XR_NULL_HANDLE, s_HapticAction = XR_NULL_HANDLE;
        XrPath s_HandPath[2] = { XR_NULL_PATH, XR_NULL_PATH };
        XrSpace s_AimSpace[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };

        // Son konumlanan pozlar (takip uzayında)
        struct Pose { bool Valid = false; glm::vec3 Position{ 0.0f }; glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; };
        Pose s_Head, s_Hands[2];
        HandInput s_Input[2];

        const char* kResolveVertex = R"(
            #version 450 core
            out vec2 v_UV;
            void main()
            {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                v_UV = p;
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
            }
        )";
        // HDR -> ACES ton eşleme (+ sRGB olmayan swapchain için gama); gözlüğe giden görüntü
        const char* kResolveFragment = R"(
            #version 450 core
            in vec2 v_UV;
            layout(location = 0) out vec4 o_Color;
            uniform sampler2D u_Source;
            uniform float u_Exposure;
            uniform float u_Gamma;
            vec3 ACES(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
            void main()
            {
                vec3 hdr = max(texture(u_Source, v_UV).rgb, vec3(0.0)) * u_Exposure;
                o_Color = vec4(pow(ACES(hdr), vec3(u_Gamma)), 1.0);
            }
        )";

        Pose ToPose(const XrPosef& p)
        {
            Pose r;
            r.Valid = true;
            r.Position = glm::vec3(p.position.x, p.position.y, p.position.z);
            r.Rotation = glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
            return r;
        }
        glm::mat4 PoseMatrix(const XrPosef& p)
        {
            const Pose pose = ToPose(p);
            return glm::translate(glm::mat4(1.0f), pose.Position) * glm::mat4_cast(pose.Rotation);
        }
        // OpenGL kırpma uzayı, asimetrik görüş açısı (OpenXR örneklerindeki xr_linear CreateProjectionFov)
        glm::mat4 ProjectionFov(const XrFovf& fov, float nearZ, float farZ)
        {
            const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight), u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
            const float w = r - l, h = u - d;
            glm::mat4 m(0.0f);
            m[0][0] = 2.0f / w;
            m[1][1] = 2.0f / h;
            m[2][0] = (r + l) / w;
            m[2][1] = (u + d) / h;
            m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
            m[2][3] = -1.0f;
            m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
            return m;
        }

        void DestroyEyeTargets(Eye& e)
        {
            if (!e.ImageFbos.empty()) glDeleteFramebuffers((GLsizei)e.ImageFbos.size(), e.ImageFbos.data());
            e.ImageFbos.clear();
            if (e.HdrFbo) glDeleteFramebuffers(1, &e.HdrFbo);
            if (e.HdrColor) glDeleteTextures(1, &e.HdrColor);
            if (e.HdrDepth) glDeleteRenderbuffers(1, &e.HdrDepth);
            e.HdrFbo = e.HdrColor = e.HdrDepth = 0;
        }

        bool CreateEyeTargets(Eye& e)
        {
            glGenTextures(1, &e.HdrColor);
            glBindTexture(GL_TEXTURE_2D, e.HdrColor);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)e.Width, (GLsizei)e.Height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenRenderbuffers(1, &e.HdrDepth);
            glBindRenderbuffer(GL_RENDERBUFFER, e.HdrDepth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)e.Width, (GLsizei)e.Height);
            glGenFramebuffers(1, &e.HdrFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, e.HdrFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.HdrColor, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, e.HdrDepth);
            const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            e.ImageFbos.assign(e.Images.size(), 0);
            glGenFramebuffers((GLsizei)e.ImageFbos.size(), e.ImageFbos.data());
            for (size_t i = 0; i < e.Images.size(); ++i)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, e.ImageFbos[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.Images[i].image, 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return ok;
        }

        // ---------------------------------------------------------------- oturum bağlama yapısı (etkin OpenGL bağlamı)
#if defined(CQ_OPENXR_LINUX)
        XrGraphicsBindingEGLMNDX s_EglBinding{ XR_TYPE_GRAPHICS_BINDING_EGL_MNDX };
        XrGraphicsBindingOpenGLXlibKHR s_GlxBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
        void* s_EglLib = nullptr;
        PFN_xrVoidFunction EglGetProc(const char* name)
        {
            static auto fn = reinterpret_cast<void* (*)(const char*)>(s_EglLib ? dlsym(s_EglLib, "eglGetProcAddress") : nullptr);
            return fn ? reinterpret_cast<PFN_xrVoidFunction>(fn(name)) : nullptr;
        }
#elif defined(CQ_OPENXR_ANDROID)
        XrGraphicsBindingOpenGLESAndroidKHR s_EsBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
#else
        XrGraphicsBindingOpenGLWin32KHR s_WglBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
#endif

        const void* MakeBinding(bool eglExtension, bool glExtension)
        {
#if defined(CQ_OPENXR_ANDROID)
            // Etkin EGL bağlamı (Android GLFW katmanı oluşturur)
            (void)eglExtension;
            if (!glExtension)
                return nullptr;
            s_EsBinding.display = eglGetCurrentDisplay();
            s_EsBinding.context = eglGetCurrentContext();
            EGLint configId = 0, count = 0;
            EGLConfig config = nullptr;
            eglQueryContext(s_EsBinding.display, s_EsBinding.context, EGL_CONFIG_ID, &configId);
            const EGLint attribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
            eglChooseConfig(s_EsBinding.display, attribs, &config, 1, &count);
            s_EsBinding.config = config;
            if (s_EsBinding.display == EGL_NO_DISPLAY || s_EsBinding.context == EGL_NO_CONTEXT)
            {
                s_Status.Error = "no EGL context";
                return nullptr;
            }
            s_Status.Binding = "OpenGL ES";
            return &s_EsBinding;
#else
            GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
#endif
#if defined(CQ_OPENXR_LINUX)
            // Wayland'da GLFW'nin "yerel" bağlamı da EGL'dir (öznitelik GLFW_NATIVE_CONTEXT_API döner)
            const bool egl = glfwGetWindowAttrib(window, GLFW_CONTEXT_CREATION_API) == GLFW_EGL_CONTEXT_API ||
                             glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
            if (egl)
            {
                if (!eglExtension)
                {
                    s_Status.Error = "the OpenGL context is EGL (Wayland) but the runtime has no XR_MNDX_egl_enable";
                    return nullptr;
                }
                if (!glfwGetEGLDisplay || !glfwGetEGLContext)
                    return nullptr;
                s_EglLib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
                auto queryContext = reinterpret_cast<EGLBoolean (*)(EGLDisplay, EGLContext, EGLint, EGLint*)>(s_EglLib ? dlsym(s_EglLib, "eglQueryContext") : nullptr);
                auto chooseConfig = reinterpret_cast<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)>(s_EglLib ? dlsym(s_EglLib, "eglChooseConfig") : nullptr);
                if (!queryContext || !chooseConfig)
                    return nullptr;
                const EGLDisplay display = glfwGetEGLDisplay();
                const EGLContext context = glfwGetEGLContext(window);
                if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT)
                {
                    s_Status.Error = "no EGL display / context";
                    return nullptr;
                }
                EGLint configId = 0, count = 0;
                EGLConfig config = nullptr;
                queryContext(display, context, EGL_CONFIG_ID, &configId);
                const EGLint attribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
                chooseConfig(display, attribs, &config, 1, &count);
                s_EglBinding.getProcAddress = &EglGetProc;
                s_EglBinding.display = display;
                s_EglBinding.config = config;
                s_EglBinding.context = context;
                s_Status.Binding = "EGL";
                return &s_EglBinding;
            }
            if (!glExtension || !glfwGetX11Display || !glfwGetGLXContext || !glfwGetGLXWindow)
            {
                s_Status.Error = "no XR_KHR_opengl_enable / GLX context";
                return nullptr;
            }
            void* glx = dlopen("libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
            if (!glx) glx = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
            auto queryContext = reinterpret_cast<int (*)(Display*, GLXContext, int, int*)>(glx ? dlsym(glx, "glXQueryContext") : nullptr);
            auto chooseConfig = reinterpret_cast<GLXFBConfig* (*)(Display*, int, const int*, int*)>(glx ? dlsym(glx, "glXChooseFBConfig") : nullptr);
            auto visualFromConfig = reinterpret_cast<XVisualInfo* (*)(Display*, GLXFBConfig)>(glx ? dlsym(glx, "glXGetVisualFromFBConfig") : nullptr);
            if (!queryContext || !chooseConfig || !visualFromConfig)
                return nullptr;
            Display* display = glfwGetX11Display();
            const GLXContext context = glfwGetGLXContext(window);
            if (!display || !context)
            {
                s_Status.Error = "no X11 display / GLX context";
                return nullptr;
            }
            constexpr int kGlxFbConfigId = 0x8013;
            int configId = 0, count = 0;
            queryContext(display, context, kGlxFbConfigId, &configId);
            const int attribs[] = { kGlxFbConfigId, configId, 0 };
            GLXFBConfig* configs = chooseConfig(display, DefaultScreen(display), attribs, &count);
            if (!configs || count < 1)
                return nullptr;
            XVisualInfo* visual = visualFromConfig(display, configs[0]);
            s_GlxBinding.xDisplay = display;
            s_GlxBinding.visualid = visual ? (uint32_t)visual->visualid : 0;
            s_GlxBinding.glxFBConfig = configs[0];
            s_GlxBinding.glxDrawable = glfwGetGLXWindow(window);
            s_GlxBinding.glxContext = context;
            s_Status.Binding = "GLX";
            return &s_GlxBinding;
#elif defined(CQ_OPENXR_WINDOWS)
            (void)eglExtension;
            if (!glExtension)
                return nullptr;
            s_WglBinding.hDC = GetDC(glfwGetWin32Window(window));
            s_WglBinding.hGLRC = glfwGetWGLContext(window);
            s_Status.Binding = "WGL";
            return &s_WglBinding;
#endif
        }

        XrPath Path(const char* text)
        {
            XrPath p = XR_NULL_PATH;
            s_Fn.xrStringToPath(s_Instance, text, &p);
            return p;
        }

        XrAction MakeAction(const char* name, const char* label, XrActionType type)
        {
            XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
            std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
            std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
            info.actionType = type;
            info.countSubactionPaths = 2;
            info.subactionPaths = s_HandPath;
            XrAction action = XR_NULL_HANDLE;
            Check(s_Fn.xrCreateAction(s_ActionSet, &info, &action), name);
            return action;
        }

        void Suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& bindings)
        {
            std::vector<XrActionSuggestedBinding> list;
            for (const auto& [action, path] : bindings)
                list.push_back({ action, Path(path) });
            XrInteractionProfileSuggestedBinding info{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
            info.interactionProfile = Path(profile);
            info.countSuggestedBindings = (uint32_t)list.size();
            info.suggestedBindings = list.data();
            const XrResult r = s_Fn.xrSuggestInteractionProfileBindings(s_Instance, &info);
            if (XR_FAILED(r))
                CQ_CORE_INFO("[OpenXR] Bindings for {0} not accepted ({1})", profile, (int)r);
        }

        void CreateActions()
        {
            XrActionSetCreateInfo setInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
            std::strcpy(setInfo.actionSetName, "gameplay");
            std::strcpy(setInfo.localizedActionSetName, "Gameplay");
            if (!Check(s_Fn.xrCreateActionSet(s_Instance, &setInfo, &s_ActionSet), "xrCreateActionSet"))
                return;
            s_HandPath[0] = Path("/user/hand/left");
            s_HandPath[1] = Path("/user/hand/right");
            s_GripPose = MakeAction("grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT);
            s_AimPose = MakeAction("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT);
            s_Trigger = MakeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
            s_Squeeze = MakeAction("squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
            s_PrimaryButton = MakeAction("primary", "Primary Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
            s_SecondaryButton = MakeAction("secondary", "Secondary Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
            s_MenuButton = MakeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
            s_Stick = MakeAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT);
            s_StickClick = MakeAction("thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT);
            s_HapticAction = MakeAction("haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT);
            // Her kumanda için önerilen bağlar (Unity OpenXR Interaction Profiles)
            Suggest("/interaction_profiles/khr/simple_controller", {
                { s_GripPose, "/user/hand/left/input/grip/pose" }, { s_GripPose, "/user/hand/right/input/grip/pose" },
                { s_AimPose, "/user/hand/left/input/aim/pose" }, { s_AimPose, "/user/hand/right/input/aim/pose" },
                { s_Trigger, "/user/hand/left/input/select/click" }, { s_Trigger, "/user/hand/right/input/select/click" },
                { s_MenuButton, "/user/hand/left/input/menu/click" }, { s_MenuButton, "/user/hand/right/input/menu/click" },
                { s_HapticAction, "/user/hand/left/output/haptic" }, { s_HapticAction, "/user/hand/right/output/haptic" } });
            Suggest("/interaction_profiles/oculus/touch_controller", {
                { s_GripPose, "/user/hand/left/input/grip/pose" }, { s_GripPose, "/user/hand/right/input/grip/pose" },
                { s_AimPose, "/user/hand/left/input/aim/pose" }, { s_AimPose, "/user/hand/right/input/aim/pose" },
                { s_Trigger, "/user/hand/left/input/trigger/value" }, { s_Trigger, "/user/hand/right/input/trigger/value" },
                { s_Squeeze, "/user/hand/left/input/squeeze/value" }, { s_Squeeze, "/user/hand/right/input/squeeze/value" },
                { s_PrimaryButton, "/user/hand/left/input/x/click" }, { s_PrimaryButton, "/user/hand/right/input/a/click" },
                { s_SecondaryButton, "/user/hand/left/input/y/click" }, { s_SecondaryButton, "/user/hand/right/input/b/click" },
                { s_MenuButton, "/user/hand/left/input/menu/click" },
                { s_Stick, "/user/hand/left/input/thumbstick" }, { s_Stick, "/user/hand/right/input/thumbstick" },
                { s_StickClick, "/user/hand/left/input/thumbstick/click" }, { s_StickClick, "/user/hand/right/input/thumbstick/click" },
                { s_HapticAction, "/user/hand/left/output/haptic" }, { s_HapticAction, "/user/hand/right/output/haptic" } });
            Suggest("/interaction_profiles/valve/index_controller", {
                { s_GripPose, "/user/hand/left/input/grip/pose" }, { s_GripPose, "/user/hand/right/input/grip/pose" },
                { s_AimPose, "/user/hand/left/input/aim/pose" }, { s_AimPose, "/user/hand/right/input/aim/pose" },
                { s_Trigger, "/user/hand/left/input/trigger/value" }, { s_Trigger, "/user/hand/right/input/trigger/value" },
                { s_Squeeze, "/user/hand/left/input/squeeze/value" }, { s_Squeeze, "/user/hand/right/input/squeeze/value" },
                { s_PrimaryButton, "/user/hand/left/input/a/click" }, { s_PrimaryButton, "/user/hand/right/input/a/click" },
                { s_SecondaryButton, "/user/hand/left/input/b/click" }, { s_SecondaryButton, "/user/hand/right/input/b/click" },
                { s_Stick, "/user/hand/left/input/thumbstick" }, { s_Stick, "/user/hand/right/input/thumbstick" },
                { s_StickClick, "/user/hand/left/input/thumbstick/click" }, { s_StickClick, "/user/hand/right/input/thumbstick/click" },
                { s_HapticAction, "/user/hand/left/output/haptic" }, { s_HapticAction, "/user/hand/right/output/haptic" } });
            Suggest("/interaction_profiles/htc/vive_controller", {
                { s_GripPose, "/user/hand/left/input/grip/pose" }, { s_GripPose, "/user/hand/right/input/grip/pose" },
                { s_AimPose, "/user/hand/left/input/aim/pose" }, { s_AimPose, "/user/hand/right/input/aim/pose" },
                { s_Trigger, "/user/hand/left/input/trigger/value" }, { s_Trigger, "/user/hand/right/input/trigger/value" },
                { s_Squeeze, "/user/hand/left/input/squeeze/click" }, { s_Squeeze, "/user/hand/right/input/squeeze/click" },
                { s_MenuButton, "/user/hand/left/input/menu/click" }, { s_MenuButton, "/user/hand/right/input/menu/click" },
                { s_Stick, "/user/hand/left/input/trackpad" }, { s_Stick, "/user/hand/right/input/trackpad" },
                { s_StickClick, "/user/hand/left/input/trackpad/click" }, { s_StickClick, "/user/hand/right/input/trackpad/click" },
                { s_HapticAction, "/user/hand/left/output/haptic" }, { s_HapticAction, "/user/hand/right/output/haptic" } });
        }

        void AttachActions()
        {
            if (!s_ActionSet)
                return;
            XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
            attach.countActionSets = 1;
            attach.actionSets = &s_ActionSet;
            Check(s_Fn.xrAttachSessionActionSets(s_Session, &attach), "xrAttachSessionActionSets");
            for (int h = 0; h < 2; ++h)
            {
                XrActionSpaceCreateInfo info{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
                info.action = s_AimPose;
                info.subactionPath = s_HandPath[h];
                info.poseInActionSpace.orientation.w = 1.0f;
                Check(s_Fn.xrCreateActionSpace(s_Session, &info, &s_AimSpace[h]), "xrCreateActionSpace");
            }
        }

        void ReadInput(XrTime time)
        {
            for (int h = 0; h < 2; ++h)
            {
                s_Input[h] = {};
                s_Hands[h].Valid = false;
            }
            if (!s_ActionSet || (s_State != XR_SESSION_STATE_FOCUSED && s_State != XR_SESSION_STATE_VISIBLE))
                return;
            XrActiveActionSet active{ s_ActionSet, XR_NULL_PATH };
            XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO };
            sync.countActiveActionSets = 1;
            sync.activeActionSets = &active;
            if (XR_FAILED(s_Fn.xrSyncActions(s_Session, &sync)))
                return;
            for (int h = 0; h < 2; ++h)
            {
                XrActionStateGetInfo get{ XR_TYPE_ACTION_STATE_GET_INFO };
                get.subactionPath = s_HandPath[h];
                auto floatState = [&](XrAction a) {
                    XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
                    get.action = a;
                    return a && XR_SUCCEEDED(s_Fn.xrGetActionStateFloat(s_Session, &get, &st)) && st.isActive ? st.currentState : 0.0f;
                };
                auto boolState = [&](XrAction a) {
                    XrActionStateBoolean st{ XR_TYPE_ACTION_STATE_BOOLEAN };
                    get.action = a;
                    return a && XR_SUCCEEDED(s_Fn.xrGetActionStateBoolean(s_Session, &get, &st)) && st.isActive && st.currentState;
                };
                HandInput& in = s_Input[h];
                in.Trigger = floatState(s_Trigger);
                in.Grip = floatState(s_Squeeze);
                in.Primary = boolState(s_PrimaryButton);
                in.Secondary = boolState(s_SecondaryButton);
                in.Menu = boolState(s_MenuButton);
                in.StickClick = boolState(s_StickClick);
                {
                    XrActionStateVector2f st{ XR_TYPE_ACTION_STATE_VECTOR2F };
                    get.action = s_Stick;
                    if (s_Stick && XR_SUCCEEDED(s_Fn.xrGetActionStateVector2f(s_Session, &get, &st)) && st.isActive)
                        in.Stick = glm::vec2(st.currentState.x, st.currentState.y);
                }
                if (s_AimSpace[h])
                {
                    XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
                    if (XR_SUCCEEDED(s_Fn.xrLocateSpace(s_AimSpace[h], s_AppSpace, time, &loc)) &&
                        (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
                    {
                        s_Hands[h] = ToPose(loc.pose);
                        in.Active = true;
                    }
                }
            }
        }

        void PollEvents()
        {
            XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
            while (s_Instance && s_Fn.xrPollEvent(s_Instance, &event) == XR_SUCCESS)
            {
                if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
                {
                    const auto& e = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                    s_State = e.state;
                    s_Status.SessionState = (int)e.state;
                    if (e.state == XR_SESSION_STATE_READY)
                    {
                        XrSessionBeginInfo begin{ XR_TYPE_SESSION_BEGIN_INFO };
                        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        s_Running = Check(s_Fn.xrBeginSession(s_Session, &begin), "xrBeginSession");
                        if (s_Running)
                            CQ_CORE_INFO("[OpenXR] Session running ({0}, {1})", s_Status.Runtime, s_Status.System);
                    }
                    else if (e.state == XR_SESSION_STATE_STOPPING)
                    {
                        s_Fn.xrEndSession(s_Session);
                        s_Running = false;
                    }
                    else if (e.state == XR_SESSION_STATE_EXITING || e.state == XR_SESSION_STATE_LOSS_PENDING)
                        s_Running = false;
                }
                else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                    s_Running = false;
                event = { XR_TYPE_EVENT_DATA_BUFFER };
            }
            s_Status.Running = s_Running;
        }

        // XR Origin (yoksa ana kamera) dünya matrisi ve yakın / uzak düzlem
        glm::mat4 OriginWorld(Scene& scene, float& nearZ, float& farZ, entt::entity& background)
        {
            nearZ = 0.05f;
            farZ = 1000.0f;
            background = entt::null;
            auto& reg = scene.m_Registry;
            for (auto e : reg.view<XROriginComponent>())
                if (scene.IsEntityActive(e))
                {
                    for (auto c : reg.view<CameraComponent>())
                    {
                        Entity cam{ c, &scene };
                        if (cam.GetParent() && (entt::entity)cam.GetParent() == e)
                        {
                            background = c;
                            const auto& sc = reg.get<CameraComponent>(c).Camera;
                            nearZ = std::max(0.01f, sc.GetPerspectiveNearClip());
                            farZ = std::max(nearZ + 1.0f, sc.GetPerspectiveFarClip());
                        }
                    }
                    return scene.ComputeWorldMatrix(e);
                }
            if (Entity cam = scene.GetPrimaryCameraEntity())
            {
                background = (entt::entity)cam;
                const auto& sc = cam.GetComponent<CameraComponent>().Camera;
                nearZ = std::max(0.01f, sc.GetPerspectiveNearClip());
                farZ = std::max(nearZ + 1.0f, sc.GetPerspectiveFarClip());
                // Unity Tracked Pose Driver gibi: kameranın ebeveyni (rig kökü, ör. betikli "XR Origin") takip uzayının kökeni,
                // kameranın yerel pozu baş pozudur
                if (Entity parent = cam.GetParent())
                    return scene.ComputeWorldMatrix((entt::entity)parent);
                // XR Origin yok: ana kameranın konumu zemin kökeni gibi (yalnız yatay dönüş)
                const glm::mat4 w = cam.GetComponent<TransformComponent>().WorldTransform;
                const glm::vec3 fwd = -glm::vec3(w[2]);
                const float yaw = std::atan2(-fwd.x, -fwd.z);
                return glm::translate(glm::mat4(1.0f), glm::vec3(w[3]) - glm::vec3(0.0f, 1.6f, 0.0f)) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
            }
            return glm::mat4(1.0f);
        }
    }

    bool Start()
    {
        Stop();
        s_Status = {};
        if (!LoadLoader())
        {
            s_Status.Error = "OpenXR loader (libopenxr_loader) not found";
            return false;
        }
        s_Status.LoaderFound = true;
        auto enumerateExtensions = Proc<PFN_xrEnumerateInstanceExtensionProperties>(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties");
        auto createInstance = Proc<PFN_xrCreateInstance>(XR_NULL_HANDLE, "xrCreateInstance");
        if (!enumerateExtensions || !createInstance)
        {
            s_Status.Error = "OpenXR loader is incomplete";
            return false;
        }
        uint32_t count = 0;
        if (XR_FAILED(enumerateExtensions(nullptr, 0, &count, nullptr)) || count == 0)
        {
            s_Status.Error = "no OpenXR runtime is active (install Monado / SteamVR or set XR_RUNTIME_JSON)";
            return false;
        }
        std::vector<XrExtensionProperties> props(count, { XR_TYPE_EXTENSION_PROPERTIES });
        enumerateExtensions(nullptr, count, &count, props.data());
        auto has = [&](const char* name) {
            return std::any_of(props.begin(), props.end(), [&](const XrExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
        };
        const bool glExt = has(kGraphicsExtension);
#if defined(CQ_OPENXR_LINUX)
        const bool eglExt = has(XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
#else
        const bool eglExt = false;
#endif
        if (!glExt && !eglExt)
        {
            s_Status.Error = "the OpenXR runtime does not support OpenGL";
            return false;
        }
        std::vector<const char*> extensions;
        if (glExt) extensions.push_back(kGraphicsExtension);
#if defined(CQ_OPENXR_LINUX)
        if (eglExt) extensions.push_back(XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
#endif
        XrInstanceCreateInfo info{ XR_TYPE_INSTANCE_CREATE_INFO };
#if defined(CQ_OPENXR_ANDROID)
        // Android: örnek Java VM ve etkinlikle oluşturulur (XR_KHR_android_create_instance)
        XrInstanceCreateInfoAndroidKHR androidInfo{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
        if (android_app* app = Android::GetApp(); app && app->activity && has(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME))
        {
            androidInfo.applicationVM = app->activity->vm;
            androidInfo.applicationActivity = app->activity->clazz;
            extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
            info.next = &androidInfo;
        }
#endif
        std::strcpy(info.applicationInfo.applicationName, "Conqueror Game");
        std::strcpy(info.applicationInfo.engineName, "Conqueror Engine");
        info.applicationInfo.applicationVersion = 1;
        info.applicationInfo.engineVersion = 1;
        info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
        info.enabledExtensionCount = (uint32_t)extensions.size();
        info.enabledExtensionNames = extensions.data();
        if (!Check(createInstance(&info, &s_Instance), "xrCreateInstance"))
            return false;
        s_Status.InstanceCreated = true;
        #define CQ_XR_LOAD(name) s_Fn.name = Proc<PFN_##name>(s_Instance, #name);
        CQ_XR_FUNCTIONS(CQ_XR_LOAD)
        #undef CQ_XR_LOAD
#if defined(CQ_OPENXR_ANDROID)
        s_Fn.xrGetGraphicsRequirements = Proc<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(s_Instance, "xrGetOpenGLESGraphicsRequirementsKHR");
#else
        s_Fn.xrGetGraphicsRequirements = Proc<PFN_xrGetOpenGLGraphicsRequirementsKHR>(s_Instance, "xrGetOpenGLGraphicsRequirementsKHR");
#endif

        XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(s_Fn.xrGetInstanceProperties(s_Instance, &ip)))
            s_Status.Runtime = std::string(ip.runtimeName) + " " + std::to_string(XR_VERSION_MAJOR(ip.runtimeVersion)) + "." +
                               std::to_string(XR_VERSION_MINOR(ip.runtimeVersion)) + "." + std::to_string(XR_VERSION_PATCH(ip.runtimeVersion));
        XrSystemGetInfo sysInfo{ XR_TYPE_SYSTEM_GET_INFO };
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (!Check(s_Fn.xrGetSystem(s_Instance, &sysInfo, &s_System), "xrGetSystem (no headset)"))
        {
            Stop();
            return false;
        }
        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
        if (XR_SUCCEEDED(s_Fn.xrGetSystemProperties(s_Instance, s_System, &sp)))
            s_Status.System = sp.systemName;

        // OpenGL gereksinimleri oturumdan önce sorulmalı (OpenXR kuralı)
#if defined(CQ_OPENXR_ANDROID)
        XrGraphicsRequirementsOpenGLESKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
#else
        XrGraphicsRequirementsOpenGLKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
#endif
        if (s_Fn.xrGetGraphicsRequirements)
            s_Fn.xrGetGraphicsRequirements(s_Instance, s_System, &req);

        uint32_t viewCount = 0;
        s_Fn.xrEnumerateViewConfigurationViews(s_Instance, s_System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        std::vector<XrViewConfigurationView> views(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        if (viewCount < 2 || !Check(s_Fn.xrEnumerateViewConfigurationViews(s_Instance, s_System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                            viewCount, &viewCount, views.data()), "views"))
        {
            Stop();
            return false;
        }

        const void* binding = MakeBinding(eglExt, glExt);
        if (!binding)
        {
            if (s_Status.Error.empty()) s_Status.Error = "cannot bind the OpenGL context";
            CQ_CORE_WARN("[OpenXR] {0}", s_Status.Error);
            Stop();
            return false;
        }
        CreateActions();
        XrSessionCreateInfo si{ XR_TYPE_SESSION_CREATE_INFO };
        si.next = binding;
        si.systemId = s_System;
        if (!Check(s_Fn.xrCreateSession(s_Instance, &si, &s_Session), "xrCreateSession"))
        {
            Stop();
            return false;
        }
        s_Status.SessionCreated = true;

        // Referans uzayı: Stage (zemin kökeni, oda ölçeği) yoksa Local
        uint32_t spaceCount = 0;
        s_Fn.xrEnumerateReferenceSpaces(s_Session, 0, &spaceCount, nullptr);
        std::vector<XrReferenceSpaceType> spaces(spaceCount);
        s_Fn.xrEnumerateReferenceSpaces(s_Session, spaceCount, &spaceCount, spaces.data());
        const bool stage = std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end();
        XrReferenceSpaceCreateInfo rs{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rs.referenceSpaceType = stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
        rs.poseInReferenceSpace.orientation.w = 1.0f;
        if (!stage)
            rs.poseInReferenceSpace.position.y = -1.6f;   // Local: baş kökende, zemin 1.6 m aşağıda
        Check(s_Fn.xrCreateReferenceSpace(s_Session, &rs, &s_AppSpace), "app space");
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        rs.poseInReferenceSpace = {};
        rs.poseInReferenceSpace.orientation.w = 1.0f;
        Check(s_Fn.xrCreateReferenceSpace(s_Session, &rs, &s_ViewSpace), "view space");

        // Swapchain biçimi: sRGB tercih (doğrusal yazılır), yoksa RGBA8 (gama gölgelendiricide)
        uint32_t formatCount = 0;
        s_Fn.xrEnumerateSwapchainFormats(s_Session, 0, &formatCount, nullptr);
        std::vector<int64_t> formats(formatCount);
        s_Fn.xrEnumerateSwapchainFormats(s_Session, formatCount, &formatCount, formats.data());
        s_Format = 0;
        for (int64_t want : { (int64_t)GL_SRGB8_ALPHA8, (int64_t)GL_RGBA8 })
            if (!s_Format && std::find(formats.begin(), formats.end(), want) != formats.end())
                s_Format = want;
        if (!s_Format && !formats.empty())
            s_Format = formats.front();
        s_SrgbSwapchain = s_Format == GL_SRGB8_ALPHA8;
        for (int i = 0; i < 2; ++i)
        {
            Eye& e = s_Eyes[i];
            e.Width = views[(size_t)i].recommendedImageRectWidth;
            e.Height = views[(size_t)i].recommendedImageRectHeight;
            XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            sc.format = s_Format;
            sc.sampleCount = 1;
            sc.width = e.Width;
            sc.height = e.Height;
            sc.faceCount = 1;
            sc.arraySize = 1;
            sc.mipCount = 1;
            if (!Check(s_Fn.xrCreateSwapchain(s_Session, &sc, &e.Swapchain), "xrCreateSwapchain"))
            {
                Stop();
                return false;
            }
            uint32_t imageCount = 0;
            s_Fn.xrEnumerateSwapchainImages(e.Swapchain, 0, &imageCount, nullptr);
            e.Images.assign(imageCount, { kSwapchainImageType });
            s_Fn.xrEnumerateSwapchainImages(e.Swapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(e.Images.data()));
            CreateEyeTargets(e);
        }
        s_Status.EyeWidth = s_Eyes[0].Width;
        s_Status.EyeHeight = s_Eyes[0].Height;
        AttachActions();
        if (!s_Vao)
            glGenVertexArrays(1, &s_Vao);
        CQ_CORE_INFO("[OpenXR] {0}: {1}, {2}x{3} per eye, {4} binding, {5} space", s_Status.Runtime, s_Status.System, s_Status.EyeWidth,
                     s_Status.EyeHeight, s_Status.Binding, stage ? "stage" : "local");
        PollEvents();
        return true;
    }

    void Stop()
    {
        if (s_Session && s_Running && s_Fn.xrRequestExitSession)
        {
            // Oturumu düzgün kapat: STOPPING olayıyla xrEndSession
            s_Fn.xrRequestExitSession(s_Session);
            for (int i = 0; i < 50 && s_Running; ++i)
                PollEvents();
            if (s_Running)
                s_Fn.xrEndSession(s_Session);
        }
        s_Running = false;
        for (Eye& e : s_Eyes)
        {
            DestroyEyeTargets(e);
            if (e.Swapchain && s_Fn.xrDestroySwapchain) s_Fn.xrDestroySwapchain(e.Swapchain);
            e = Eye{};
        }
        for (XrSpace& s : s_AimSpace)
        {
            if (s && s_Fn.xrDestroySpace) s_Fn.xrDestroySpace(s);
            s = XR_NULL_HANDLE;
        }
        if (s_ViewSpace && s_Fn.xrDestroySpace) s_Fn.xrDestroySpace(s_ViewSpace);
        if (s_AppSpace && s_Fn.xrDestroySpace) s_Fn.xrDestroySpace(s_AppSpace);
        s_ViewSpace = s_AppSpace = XR_NULL_HANDLE;
        if (s_Session && s_Fn.xrDestroySession) s_Fn.xrDestroySession(s_Session);
        s_Session = XR_NULL_HANDLE;
        if (s_ActionSet && s_Fn.xrDestroyActionSet) s_Fn.xrDestroyActionSet(s_ActionSet);
        s_ActionSet = XR_NULL_HANDLE;
        if (s_Instance && s_Fn.xrDestroyInstance) s_Fn.xrDestroyInstance(s_Instance);
        s_Instance = XR_NULL_HANDLE;
        s_System = XR_NULL_SYSTEM_ID;
        s_State = XR_SESSION_STATE_UNKNOWN;
        s_Head = {};
        s_Hands[0] = s_Hands[1] = {};
        s_Input[0] = s_Input[1] = {};
        s_Resolve.reset();
        s_Status.Running = false;
        s_Status.SessionCreated = false;
        s_Status.InstanceCreated = false;
    }

    bool IsRunning() { return s_Running; }

    bool HeadPose(glm::vec3& position, glm::quat& rotation)
    {
        if (!s_Head.Valid) return false;
        position = s_Head.Position;
        rotation = s_Head.Rotation;
        return true;
    }

    bool HandPose(int hand, glm::vec3& position, glm::quat& rotation)
    {
        if (hand < 0 || hand > 1 || !s_Hands[hand].Valid) return false;
        position = s_Hands[hand].Position;
        rotation = s_Hands[hand].Rotation;
        return true;
    }

    HandInput Hand(int hand) { return hand >= 0 && hand <= 1 ? s_Input[hand] : HandInput{}; }

    bool ProbeEyeMean(int eye, glm::vec3& mean)
    {
        if (eye < 0 || eye > 1 || !s_Eyes[eye].HdrColor || s_Status.FramesSubmitted == 0)
            return false;
        const Eye& e = s_Eyes[eye];
        std::vector<float> pixels((size_t)e.Width * e.Height * 4);
        glGetTextureImage(e.HdrColor, 0, GL_RGBA, GL_FLOAT, (GLsizei)(pixels.size() * sizeof(float)), pixels.data());
        glm::dvec3 sum(0.0);
        size_t n = 0;
        for (size_t i = 0; i < pixels.size(); i += 4 * 7)
        {
            sum += glm::dvec3(pixels[i], pixels[i + 1], pixels[i + 2]);
            ++n;
        }
        mean = n ? glm::vec3(sum / (double)n) : glm::vec3(0.0f);
        return true;
    }

    void Haptic(int hand, float amplitude, float seconds)
    {
        if (!s_Running || !s_HapticAction || hand < 0 || hand > 1)
            return;
        XrHapticActionInfo info{ XR_TYPE_HAPTIC_ACTION_INFO };
        info.action = s_HapticAction;
        info.subactionPath = s_HandPath[hand];
        XrHapticVibration v{ XR_TYPE_HAPTIC_VIBRATION };
        v.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
        v.duration = (XrDuration)(std::max(0.0f, seconds) * 1e9f);
        v.frequency = XR_FREQUENCY_UNSPECIFIED;
        s_Fn.xrApplyHapticFeedback(s_Session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&v));
    }

    void RenderFrame(Scene& scene)
    {
        if (!s_Session)
            return;
        PollEvents();
        if (!s_Running)
            return;
        XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState frame{ XR_TYPE_FRAME_STATE };
        if (!Check(s_Fn.xrWaitFrame(s_Session, &waitInfo, &frame), "xrWaitFrame"))
            return;
        XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
        if (!Check(s_Fn.xrBeginFrame(s_Session, &beginInfo), "xrBeginFrame"))
            return;

        // Baş ve eller (bu karenin gösterim zamanına göre)
        {
            XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
            if (XR_SUCCEEDED(s_Fn.xrLocateSpace(s_ViewSpace, s_AppSpace, frame.predictedDisplayTime, &loc)) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
                s_Head = ToPose(loc.pose);
        }
        ReadInput(frame.predictedDisplayTime);

        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        XrCompositionLayerProjectionView projViews[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
        bool rendered = false;
        if (frame.shouldRender && (s_State == XR_SESSION_STATE_VISIBLE || s_State == XR_SESSION_STATE_FOCUSED))
        {
            XrViewLocateInfo li{ XR_TYPE_VIEW_LOCATE_INFO };
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime = frame.predictedDisplayTime;
            li.space = s_AppSpace;
            XrViewState vs{ XR_TYPE_VIEW_STATE };
            XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
            uint32_t viewCount = 0;
            if (XR_SUCCEEDED(s_Fn.xrLocateViews(s_Session, &li, &vs, 2, &viewCount, views)) && viewCount == 2 &&
                (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
            {
                if (!s_Resolve)
                    s_Resolve = Shader::Create("OpenXRResolve", kResolveVertex, kResolveFragment);
                // GL durumunu sakla (oyun görünümünün çizimi bunun etrafında sürüyor)
                GLint prevFbo = 0, prevViewport[4];
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
                glGetIntegerv(GL_VIEWPORT, prevViewport);
                const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
                                scissor = glIsEnabled(GL_SCISSOR_TEST), srgb = kDesktopGL && glIsEnabled(GL_FRAMEBUFFER_SRGB);
                float nearZ = 0.05f, farZ = 1000.0f;
                entt::entity background = entt::null;
                const glm::mat4 origin = OriginWorld(scene, nearZ, farZ, background);
                const float exposure = std::exp2(scene.GetRenderSettings().Post.Exposure);
                for (int i = 0; i < 2; ++i)
                {
                    Eye& e = s_Eyes[i];
                    uint32_t index = 0;
                    XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                    if (XR_FAILED(s_Fn.xrAcquireSwapchainImage(e.Swapchain, &acquire, &index)))
                        continue;
                    XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                    wait.timeout = XR_INFINITE_DURATION;
                    s_Fn.xrWaitSwapchainImage(e.Swapchain, &wait);
                    // Göz kamerası: XR Origin * göz pozu
                    const glm::mat4 eyeWorld = origin * PoseMatrix(views[i].pose);
                    RenderCamera cam;
                    cam.View = glm::inverse(eyeWorld);
                    cam.Projection = ProjectionFov(views[i].fov, nearZ, farZ);
                    cam.Position = glm::vec3(eyeWorld[3]);
                    cam.NearClip = nearZ;
                    cam.FarClip = farZ;
                    cam.ViewportWidth = e.Width;
                    cam.ViewportHeight = e.Height;
                    glBindFramebuffer(GL_FRAMEBUFFER, e.HdrFbo);
                    glViewport(0, 0, (GLsizei)e.Width, (GLsizei)e.Height);
                    glDisable(GL_SCISSOR_TEST);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                    glEnable(GL_DEPTH_TEST);
                    scene.RenderSceneSecondary(cam, background);
                    // Ton eşleme -> swapchain görüntüsü
                    glBindFramebuffer(GL_FRAMEBUFFER, e.ImageFbos[index]);
                    glViewport(0, 0, (GLsizei)e.Width, (GLsizei)e.Height);
                    glDisable(GL_DEPTH_TEST);
                    glDisable(GL_BLEND);
                    glDisable(GL_CULL_FACE);
                    if (kDesktopGL) { if (s_SrgbSwapchain) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB); }
                    if (s_Resolve && s_Resolve->IsValid())
                    {
                        s_Resolve->Bind();
                        glBindTextureUnit(0, e.HdrColor);
                        s_Resolve->SetInt("u_Source", 0);
                        s_Resolve->SetFloat("u_Exposure", exposure);
                        s_Resolve->SetFloat("u_Gamma", s_SrgbSwapchain ? 1.0f : 1.0f / 2.2f);
                        glBindVertexArray(s_Vao);
                        glDrawArrays(GL_TRIANGLES, 0, 3);
                        glBindVertexArray(0);
                    }
                    XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                    s_Fn.xrReleaseSwapchainImage(e.Swapchain, &release);
                    projViews[i].pose = views[i].pose;
                    projViews[i].fov = views[i].fov;
                    projViews[i].subImage.swapchain = e.Swapchain;
                    projViews[i].subImage.imageRect.offset = { 0, 0 };
                    projViews[i].subImage.imageRect.extent = { (int32_t)e.Width, (int32_t)e.Height };
                    projViews[i].subImage.imageArrayIndex = 0;
                }
                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
                glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
                if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
                if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
                if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
                if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
                if (kDesktopGL) { if (srgb) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB); }
                layer.space = s_AppSpace;
                layer.viewCount = 2;
                layer.views = projViews;
                rendered = true;
            }
        }
        const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer) };
        XrFrameEndInfo end{ XR_TYPE_FRAME_END_INFO };
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = rendered ? 1u : 0u;
        end.layers = rendered ? layers : nullptr;
        if (Check(s_Fn.xrEndFrame(s_Session, &end), "xrEndFrame") && rendered)
            ++s_Status.FramesSubmitted;
    }
#endif
}
