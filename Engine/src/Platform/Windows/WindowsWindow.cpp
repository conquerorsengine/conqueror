#include "WindowsWindow.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"
#include "Renderer/Renderer.h"

#include <GLFW/glfw3.h>
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef CQ_PLATFORM_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace Conqueror
{
    static uint8_t s_GLFWWindowCount = 0;

    static void GLFWErrorCallback(int error, const char* description)
    {
        CQ_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
        // Günlük eşzamansız: pencere oluşturma hataları çökmeden önce de görünsün
        std::fprintf(stderr, "GLFW Error (%d): %s\n", error, description ? description : "");
        std::fflush(stderr);
    }

    // Pencere / grafik bağlamı hiç oluşturulamadı: anlaşılır mesajla çık (boş gösterici çökmesi yerine)
    [[noreturn]] static void FatalWindowError(const char* what)
    {
        CQ_CORE_CRITICAL("{0}", what);
        Log::FlushAll();
        std::fprintf(stderr, "Conqueror: %s\n", what);
#ifdef CQ_PLATFORM_WINDOWS
        MessageBoxA(nullptr, what, "Conqueror Engine", MB_OK | MB_ICONERROR);
#endif
        std::exit(1);
    }

    // Wayland app_id / X11 sınıfı: masaüstü bütünleşmesi (.desktop StartupWMClass) ve AGL bileşikçisinin pencereyi
    // uygulamaya eşlemesi için. CQ_APP_ID ortam değişkeniyle değiştirilir (AGL hizmet dosyası bunu ayarlar).
    // Başlatılamayan GL'siz arka ucun yerine denenecek: Direct3D 12 -> 11, Metal -> Vulkan (MoltenVK), diğerleri -> OpenGL
    static RendererAPI::Backend FallbackBackend(RendererAPI::Backend failed)
    {
        if (failed == RendererAPI::Backend::Direct3D12 && RendererAPI::IsBackendCompiled(RendererAPI::Backend::Direct3D11))
            return RendererAPI::Backend::Direct3D11;
        if (failed == RendererAPI::Backend::Metal && RendererAPI::IsBackendCompiled(RendererAPI::Backend::Vulkan))
            return RendererAPI::Backend::Vulkan;
        return RendererAPI::Backend::OpenGL;
    }

    static void ApplyIdentityHints()
    {
        const char* id = std::getenv("CQ_APP_ID");
        const char* appId = id && *id ? id : "conqueror";
        glfwWindowHintString(GLFW_WAYLAND_APP_ID, appId);
        glfwWindowHintString(GLFW_X11_CLASS_NAME, appId);
        glfwWindowHintString(GLFW_X11_INSTANCE_NAME, appId);
    }

    Window* Window::Create(const WindowProps& props)
    {
        return new WindowsWindow(props);
    }

    WindowsWindow::WindowsWindow(const WindowProps& props)
    {
        Init(props);
    }

    WindowsWindow::~WindowsWindow()
    {
        Shutdown();
    }

    void WindowsWindow::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        CQ_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

        if (s_GLFWWindowCount == 0)
        {
#if defined(GLFW_PLATFORM) && !defined(CQ_PLATFORM_WINDOWS) && !defined(CQ_PLATFORM_MACOS) && !defined(CQ_PLATFORM_ANDROID) && !defined(CQ_PLATFORM_QNX)
            // Linux / BSD: pencere sistemi tercihi (Project Settings > Player > Linux / FreeBSD ya da CQ_DISPLAY_SERVER)
            if (const char* ds = std::getenv("CQ_DISPLAY_SERVER"); ds && *ds)
            {
                std::string want = ds;
                std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                const int platform = want == "wayland" ? GLFW_PLATFORM_WAYLAND : want == "x11" ? GLFW_PLATFORM_X11 : 0;
                if (platform && glfwPlatformSupported(platform))
                {
                    glfwInitHint(GLFW_PLATFORM, platform);
                    CQ_CORE_INFO("Display server: {0} (CQ_DISPLAY_SERVER)", want);
                }
                else if (platform)
                    CQ_CORE_WARN("Display server {0} is not available in this build; using the default", want);
            }
#endif
            [[maybe_unused]] int success = glfwInit();
#if defined(GLFW_PLATFORM) && !defined(CQ_PLATFORM_WINDOWS) && !defined(CQ_PLATFORM_MACOS) && !defined(CQ_PLATFORM_ANDROID) && !defined(CQ_PLATFORM_QNX)
            // İstenen pencere sistemi bu oturumda yoksa (ör. X11 istenmiş, yalnız Wayland var) varsayılanla yeniden dene
            if (!success)
            {
                CQ_CORE_WARN("Requested display server could not start; falling back to automatic selection");
                glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
                success = glfwInit();
            }
#endif
            CQ_CORE_ASSERT(success, "Could not initialize GLFW!");
            glfwSetErrorCallback(GLFWErrorCallback);
        }

        // Vulkan / Direct3D 11 / 12 / Metal arka ucu: GL bağlamsız pencere; başlatılamazsa Direct3D 12 -> 11, Metal -> Vulkan,
        // sonra OpenGL denenir
        while (RendererAPI::GetBackend() != RendererAPI::Backend::OpenGL && !m_Window)
        {
            glfwDefaultWindowHints();
            ApplyIdentityHints();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
            m_Context = m_Window ? GraphicsContext::Create(m_Window) : nullptr;
            if (!m_Context || !m_Context->Init())
            {
                CQ_CORE_WARN("{0} backend unavailable, falling back to {1}", RendererAPI::GetBackendName(),
                             RendererAPI::GetBackendName(FallbackBackend(RendererAPI::GetBackend())));
                delete m_Context;
                m_Context = nullptr;
                if (m_Window)
                    glfwDestroyWindow(m_Window);
                m_Window = nullptr;
                RendererAPI::SetBackend(FallbackBackend(RendererAPI::GetBackend()));
            }
        }

        if (!m_Window)
        {
            glfwDefaultWindowHints();
            ApplyIdentityHints();
            // Hata ayıklama bağlamı: Debug derlemede her zaman, diğerlerinde CQ_GL_DEBUG=1 ile (sürücü uyarılarını görmek için)
            #if defined(CQ_DEBUG)
                glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            #else
                if (const char* dbg = std::getenv("CQ_GL_DEBUG"); dbg && dbg[0] == '1')
                    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            #endif
            // İstenen OpenGL profili (--gl=3.3 / 4.1 / es3.2 ...); varsayılan: sürücünün en yükseği
            GLProfile::ApplyWindowHints();

            // MSAA (Multi-Sample Anti-Aliasing) - 4x samples
            glfwWindowHint(GLFW_SAMPLES, 4);

            m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
            if (!m_Window)
            {
                // Çoklu örnekli varsayılan çerçeve arabelleği desteklenmeyebilir (sanal makine, uzak masaüstü)
                CQ_CORE_WARN("Window creation with 4x MSAA failed, retrying without multisampling");
                glfwWindowHint(GLFW_SAMPLES, 0);
                m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
            }
            if (!m_Window)
                FatalWindowError("Could not create the window / OpenGL context. Update your graphics driver "
                                 "(OpenGL 4.1+ required) or start with --vulkan / --d3d11.");
            m_Context = GraphicsContext::Create(m_Window);
            if (!m_Context || !m_Context->Init())
                FatalWindowError("OpenGL initialization failed (OpenGL 4.1+ required).");
        }
        ++s_GLFWWindowCount;
        CQ_CORE_INFO("Graphics backend: {0}", m_Context->GetName());

        // Platform istenen boyutu vermeyebilir (Android / QNX tam ekran, otomotiv sistem çubukları, döşemeli pencere
        // yöneticisi): arayüz ve görünüm gerçek boyuttan başlasın
        {
            int actualWidth = 0, actualHeight = 0;
            glfwGetWindowSize(m_Window, &actualWidth, &actualHeight);
            if (actualWidth > 0 && actualHeight > 0 && ((uint32_t)actualWidth != m_Data.Width || (uint32_t)actualHeight != m_Data.Height))
            {
                CQ_CORE_INFO("Window size is {0}x{1} (requested {2}x{3})", actualWidth, actualHeight, m_Data.Width, m_Data.Height);
                m_Data.Width = (uint32_t)actualWidth;
                m_Data.Height = (uint32_t)actualHeight;
            }
        }

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(true);

        // Set GLFW callbacks
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event);
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char* paths[])
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowFileDropEvent event(std::vector<std::string>(paths, paths + count));
            data.EventCallback(event);
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action)
            {
                case GLFW_PRESS:
                {
                    KeyPressedEvent event(static_cast<KeyCode>(key), 0);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    KeyReleasedEvent event(static_cast<KeyCode>(key));
                    data.EventCallback(event);
                    break;
                }
                case GLFW_REPEAT:
                {
                    KeyPressedEvent event(static_cast<KeyCode>(key), 1);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            KeyTypedEvent event(static_cast<KeyCode>(keycode));
            data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, [[maybe_unused]] int mods)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action)
            {
                case GLFW_PRESS:
                {
                    MouseButtonPressedEvent event(static_cast<MouseCode>(button));
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    MouseButtonReleasedEvent event(static_cast<MouseCode>(button));
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            // Poll yoluyla da okunabilsin diye biriktir (ImGui event yutmasından bağımsız)
            Input::AddMouseScrollDelta((float)yOffset);

            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            MouseMovedEvent event((float)xPos, (float)yPos);
            data.EventCallback(event);
        });
    }

    void WindowsWindow::Shutdown()
    {
        // Vulkan yüzeyi pencereden önce bırakılmalı
        delete m_Context;
        m_Context = nullptr;
        glfwDestroyWindow(m_Window);
        --s_GLFWWindowCount;

        if (s_GLFWWindowCount == 0)
        {
            glfwTerminate();
        }
    }

    void WindowsWindow::OnUpdate()
    {
        glfwPollEvents();
        m_Context->SwapBuffers();

        if (m_Data.SoftVSync)
        {
            // Ekran yenileme hızında kare sınırlama (birikimli hedef: sapma olmaz)
            int refresh = 60;
            if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
                if (const GLFWvidmode* mode = glfwGetVideoMode(monitor); mode && mode->refreshRate > 0)
                    refresh = mode->refreshRate;
            const double interval = 1.0 / (double)std::clamp(refresh, 24, 360);
            const double now = glfwGetTime();
            if (m_Data.NextFrameTime <= 0.0 || now - m_Data.NextFrameTime > 0.25)
                m_Data.NextFrameTime = now;   // ilk kare ya da uzun duraklama: yeniden hizala
            m_Data.NextFrameTime += interval;
            const double wait = m_Data.NextFrameTime - now;
            if (wait > 0.0)
                std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        }
    }

    void WindowsWindow::SetTitle(const std::string& title)
    {
        m_Data.Title = title;
        glfwSetWindowTitle(m_Window, title.c_str());
    }

    void WindowsWindow::SetFullscreenMode(int mode)
    {
        mode = std::clamp(mode, 0, 2);
        if (mode == m_FullscreenMode)
            return;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (m_FullscreenMode == 0)
        {
            // Pencere konumunu/boyutunu geri dönüş için sakla
            glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);
        }
        if (mode == 0 || !monitor || !vm)
        {
            glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, m_WindowedW, m_WindowedH, 0);
            m_FullscreenMode = 0;
            return;
        }
        if (mode == 1)
        {
            // Kenarlıksız tam ekran pencere: masaüstü çözünürlüğü, mod değişmez (alt-tab hızlı)
            glfwSetWindowMonitor(m_Window, nullptr, 0, 0, vm->width, vm->height, 0);
            glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_FALSE);
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitor, &mx, &my);
            glfwSetWindowPos(m_Window, mx, my);
            glfwSetWindowSize(m_Window, vm->width, vm->height);
        }
        else
        {
            glfwSetWindowMonitor(m_Window, monitor, 0, 0, vm->width, vm->height, vm->refreshRate);
        }
        m_FullscreenMode = mode;
        SetVSync(m_Data.VSync);
    }

    void WindowsWindow::SetResizable(bool resizable)
    {
        glfwSetWindowAttrib(m_Window, GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    }

    bool WindowsWindow::SetIcon(const std::string& pngPath)
    {
#if defined(GLFW_PLATFORM_WAYLAND)
        // Wayland pencere simgesini desteklemez (simge uygulama kimliği + .desktop dosyasından): hata günlüğü yazılmasın
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            return false;
#endif
        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
        if (!pixels)
            return false;
        GLFWimage image{ w, h, pixels };
        glfwSetWindowIcon(m_Window, 1, &image);   // Wayland'de desteklenmez (sessizce yok sayılır)
        stbi_image_free(pixels);
        return true;
    }

    void WindowsWindow::SetVSync(bool enabled)
    {
#if defined(GLFW_PLATFORM_WAYLAND)
        const bool wayland = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
        const bool wayland = false;
#endif
        // Wayland'de görünmeyen pencere kare geri çağrısı almaz; vsync'li takas oyunu dondurur
        if (RendererAPI::GetBackend() != RendererAPI::Backend::OpenGL)
            m_Context->SetVSync(enabled && !wayland);
        else
            glfwSwapInterval(enabled && !wayland ? 1 : 0);
        m_Data.SoftVSync = enabled && wayland;
        m_Data.NextFrameTime = 0.0;
        m_Data.VSync = enabled;
    }
}
