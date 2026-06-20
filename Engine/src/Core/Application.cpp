#include "Application.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Audio/AudioEngine.h"
#include "Renderer/Renderer.h"
#include "Core/Time/TimeManager.h"
#include "Core/Utils/Platform.h"
#include <filesystem>

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

        m_Window = Scope<Window>(Window::Create(WindowProps(m_Specification.Name, m_Specification.Width, m_Specification.Height)));
        m_Window->SetEventCallback(CQ_BIND_EVENT_FN(Application::OnEvent));

        Renderer::Init();
        AudioEngine::Init();
        TimeManager::Init();

        std::filesystem::path exePath = Platform::GetProcessPath();
        std::filesystem::path configPath = exePath.parent_path() / "../../Engine/src/Core/Input/default.cqinput";
        Input::LoadConfig(configPath.string());

        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
    }

    Application::~Application()
    {
        AudioEngine::Shutdown();
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

    void Application::Close()
    {
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
            float time = (float)glfwGetTime();
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            TimeManager::Update(timestep);
            Input::Update();

            if (!m_Minimized)
            {
                for (Layer* layer : m_LayerStack)
                    layer->OnUpdate(timestep);

                m_ImGuiLayer->Begin();
                for (Layer* layer : m_LayerStack)
                    layer->OnImGuiRender();
                m_ImGuiLayer->End();
            }

            m_Window->OnUpdate();
        }
    }

    bool Application::OnWindowClose([[maybe_unused]] WindowCloseEvent& e)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return false;
        }

        m_Minimized = false;
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

        return false;
    }
}
