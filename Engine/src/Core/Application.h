#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/Base.h"
#include "Core/Base/Window.h"
#include "Core/LayerStack.h"
#include "Core/Events/Event.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/ImGui/ImGuiLayer.h"

namespace Conqueror
{
    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        const char* operator[](int index) const
        {
            CQ_CORE_ASSERT(index < Count);
            return Args[index];
        }
    };

    struct ApplicationSpecification
    {
        std::string Name = "Conqueror Application";
        std::string WorkingDirectory;
        ApplicationCommandLineArgs CommandLineArgs;
        uint32_t Width = 1920;
        uint32_t Height = 1080;
    };

    class CQ_API Application
    {
    public:
        Application(const ApplicationSpecification& specification);
        virtual ~Application();

        void Run();
        void Close();

        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);

        Window& GetWindow() { return *m_Window; }

        ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

        static Application& Get() { return *s_Instance; }

        const ApplicationSpecification& GetSpecification() const { return m_Specification; }

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

    private:
        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer;
        bool m_Running = true;
        bool m_Minimized = false;
        LayerStack m_LayerStack;
        float m_LastFrameTime = 0.0f;

    private:
        static Application* s_Instance;
    };

    // To be defined in CLIENT
    Application* CreateApplication(ApplicationCommandLineArgs args);
}
