#pragma once

#include "Core/Base/Window.h"
#include "Renderer/RHI/GraphicsContext.h"

#include <GLFW/glfw3.h>

namespace Conqueror
{
    class WindowsWindow : public Window
    {
    public:
        WindowsWindow(const WindowProps& props);
        virtual ~WindowsWindow();

        void OnUpdate() override;

        uint32_t GetWidth() const override { return m_Data.Width; }
        uint32_t GetHeight() const override { return m_Data.Height; }

        void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return m_Data.VSync; }

        void SetTitle(const std::string& title) override;
        void SetFullscreenMode(int mode) override;
        void SetResizable(bool resizable) override;
        bool SetIcon(const std::string& pngPath) override;

        void* GetNativeWindow() const override { return m_Window; }

    private:
        virtual void Init(const WindowProps& props);
        virtual void Shutdown();

    private:
        GLFWwindow* m_Window = nullptr;
        GraphicsContext* m_Context = nullptr;

        struct WindowData
        {
            std::string Title;
            uint32_t Width = 0, Height = 0;
            bool VSync = true;
            // Wayland: gizlenen (üstü örtülen) pencerede vsync'li takas sonsuza dek bekler -> takas aralığı 0 +
            // yazılımla kare hızı sınırlama (ağ sunucusu olan oyun arka planda donmaz)
            bool SoftVSync = false;
            double NextFrameTime = 0.0;

            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
        int m_WindowedX = 100, m_WindowedY = 100, m_WindowedW = 1280, m_WindowedH = 720;
        int m_FullscreenMode = 0;
    };
}
