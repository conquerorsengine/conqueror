#include <clocale>
#include <cctype>
#include "Core/Project/ProjectSettings.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Networking/Multiplayer/NetworkSession.h"
#include "Timeline/TimelineSystem.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Scene/LevelStreaming.h"
#include "Scene/Fracture.h"
#include "Scene/VirtualGeometry.h"
#include "Scene/MultiMesh.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"
#include "Scene/CameraRenderTarget.h"
#include <cstring>
#include "Application.h"
#include "Core/Input/InputGlyphs.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioMixer.h"
#include "Renderer/Renderer.h"
#include "Core/Time/TimeManager.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Utils/Platform.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Automation/Automation.h"
#include "Core/Utils/PlayerPrefs.h"
#include "Core/Debug/CrashHandler.h"
#include "Core/Audio/AudioOneShots.h"
#include <chrono>
#include <filesystem>
#include <thread>

#include <GLFW/glfw3.h>

#ifdef CQ_PLATFORM_WINDOWS
    #include <direct.h>
#else
    #include <unistd.h>
#endif

namespace Conqueror
{
    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& specification)
        : m_Specification(specification)
    {
        CQ_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;
        // Çökmelerde yığın izli rapor (+ Windows minidump)
        CrashHandler::Install(m_Specification.Name);
        // Derlenmiş oyun: --host / --server / --connect <adres> çok oyunculu oturumu başlatır
        Net::NetworkSession::SetCommandLine(m_Specification.CommandLineArgs.Count, m_Specification.CommandLineArgs.Args);

        if (!m_Specification.WorkingDirectory.empty())
        {
            #ifdef CQ_PLATFORM_WINDOWS
                int result = _chdir(m_Specification.WorkingDirectory.c_str());
            #else
                int result = chdir(m_Specification.WorkingDirectory.c_str());
            #endif
            if (result != 0)
            {
                CQ_CORE_WARN("Failed to change working directory to: {0}", m_Specification.WorkingDirectory);
            }
        }

        GLProfile::SetRequested(GLProfile::Resolve(m_Specification.CommandLineArgs.Count, m_Specification.CommandLineArgs.Args));
        RendererAPI::SetBackend(RendererAPI::ResolveBackend(m_Specification.CommandLineArgs.Count, m_Specification.CommandLineArgs.Args,
                                                            m_Specification.GraphicsBackend));
        m_Window = Scope<Window>(Window::Create(WindowProps(m_Specification.Name, m_Specification.Width, m_Specification.Height)));
        m_Window->SetEventCallback(CQ_BIND_EVENT_FN(Application::OnEvent));

        Renderer::Init();
        PhysicsWorld3D::InitGlobals();
        AudioEngine::Init();
        AudioMixer::Get().Init();
        TimeManager::Init();

        std::filesystem::path exePath = Platform::GetProcessPath();
        std::filesystem::path configPath = exePath.parent_path() / "../../Engine/src/Core/Input/default.cqinput";
        std::string resolvedInput = FileSystem::ResolveResourcePath("Engine/src/Core/Input/default.cqinput");
        if (!Input::LoadConfig(resolvedInput))
            Input::LoadConfig(configPath.string());

        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);

        Automation::Init(m_Specification.CommandLineArgs.Count, m_Specification.CommandLineArgs.Args);
    }

    Application::~Application()
    {
        // Katmanlar (editör/oyun sahneleri) ses, fizik ve render alt sistemleri hâlâ ayaktayken kapanır:
        // sahne yıkıcıları ses kaynaklarını/GPU kaynaklarını güvenle bırakabilsin
        m_LayerStack.Clear();
        PlayerPrefs::Save();
        Automation::Shutdown();
        // Zaman çizelgesi ses kaynakları ses motorundan önce
        TimelineSystem::Shutdown();
        AudioOneShots::Shutdown();
        AudioMixer::Get().Shutdown();
        AudioEngine::Shutdown();
        PhysicsWorld3D::ShutdownGlobals();
        // Önbellekli prefab / akışlı seviye şablon sahneleri GPU kaynakları tutar: GL bağlamı kapanmadan bırak
        PrefabSerializer::ClearCache();
        LevelStreaming::ClearCache();
        InputGlyphs::Shutdown();
        Fracture::ClearCache();
        VirtualGeometry::ClearCache();
        MultiMesh::ClearCache();
        CameraRenderTargets::Shutdown();
        Profiling::FrameProfiler::Shutdown();
        Renderer::Shutdown();
    }

    void Application::PushLayer(Layer* layer)
    {
        m_LayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer* overlay)
    {
        m_LayerStack.PushOverlay(overlay);
        overlay->OnAttach();
    }

    void Application::Close(bool force)
    {
        if (!force)
            for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
                if (!(*it)->OnCloseRequested())
                    return;
        m_Running = false;
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(CQ_BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(CQ_BIND_EVENT_FN(Application::OnWindowResize));

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    void Application::Run()
    {
        while (m_Running)
        {
            // Sayı biçimlemesi/ayrıştırması "C" yerel ayarına bağlıdır ("1.5"). Bazı kütüphaneler
            // (GTK dosya iletişim kutusu, JVM) setlocale(LC_ALL, "") çağırır: her karede geri al.
            if (const char* numeric = std::setlocale(LC_NUMERIC, nullptr); numeric && std::strcmp(numeric, "C") != 0)
                std::setlocale(LC_NUMERIC, "C");
            // Türkçe/Azerice LC_CTYPE'ta tolower('I') 'i' vermez: cvar/bileşen/uzantı anahtarları
            // eşleşmez. Bu durumda büyük/küçük harf eşlemesi ASCII olan C.UTF-8'e geç (UTF-8 girdi korunur).
            if (std::tolower('I') != 'i' && !std::setlocale(LC_CTYPE, "C.UTF-8"))
                std::setlocale(LC_CTYPE, "C");

            Profiling::FrameProfiler::BeginFrame();

            float time = (float)glfwGetTime();
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            TimeManager::Update(timestep);
            timestep = TimeManager::GetUnscaledDeltaTime();
            Input::Update();
            InputGlyphs::Update();
            Automation::OnFrameBegin();

            if (!m_Minimized)
            {
                {
                    CQ_PROFILE_SAMPLE("Update", Other);
                    for (Layer* layer : m_LayerStack)
                        layer->OnUpdate(timestep);
                }

                CQ_PROFILE_SAMPLE("GUI", Editor);
                m_ImGuiLayer->Begin();
                for (Layer* layer : m_LayerStack)
                    layer->OnImGuiRender();
                {
                    CQ_PROFILE_SAMPLE("GUI.Render", Editor);
                    CQ_PROFILE_GPU("GUI");
                    m_ImGuiLayer->End();
                }
            }
            else
            {
                // Simge durumunda: oyun mantığı ve simülasyon sürer (çok oyunculu sunucu, "Run In Background"),
                // arayüz çizilmez. Döngü işlemciyi boşa yakmasın diye ~60 Hz ile sınırlanır.
                {
                    CQ_PROFILE_SAMPLE("Update", Other);
                    for (Layer* layer : m_LayerStack)
                        layer->OnUpdate(timestep);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }

            {
                const auto& stats = Renderer::GetStats();
                using Profiling::Counter;
                Profiling::FrameProfiler::SetCounter(Counter::DrawCalls, stats.DrawCalls);
                Profiling::FrameProfiler::SetCounter(Counter::Triangles, stats.Triangles);
                Profiling::FrameProfiler::SetCounter(Counter::Batches2D, stats.QuadCount);
                Profiling::FrameProfiler::SetCounter(Counter::Lights, stats.LightCount);
                Profiling::FrameProfiler::SetCounter(Counter::TextureMB, stats.TextureMemory);
                Profiling::FrameProfiler::SetCounter(Counter::MeshMB, stats.MeshMemory);
            }
            {
                CQ_PROFILE_SAMPLE("Present (VSync)", VSync);
                m_Window->OnUpdate();
            }
            // Proje "Target Frame Rate" (0 = sınırsız): kalan süre kadar bekle
            if (const int target = ProjectSettings::Quality().TargetFrameRate; target > 0)
            {
                const double frameTime = 1.0 / (double)target;
                const double elapsed = glfwGetTime() - (double)time;
                if (elapsed < frameTime)
                    std::this_thread::sleep_for(std::chrono::duration<double>(frameTime - elapsed));
            }
            Profiling::FrameProfiler::EndFrame();

            if (Automation::WantsQuit())
                m_Running = false;
        }
    }

    bool Application::OnWindowClose([[maybe_unused]] WindowCloseEvent& e)
    {
        Close();
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            if (!m_Minimized)
                CQ_CORE_INFO("Window minimized (simulation continues, rendering paused)");
            m_Minimized = true;
            return false;
        }

        if (m_Minimized)
            CQ_CORE_INFO("Window restored ({0}x{1})", e.GetWidth(), e.GetHeight());
        m_Minimized = false;
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

        return false;
    }
}
