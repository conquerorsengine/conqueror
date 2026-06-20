#pragma once

#include "Core/Base/Base.h"
#include "Core/Events/Event.h"

#include <string>
#include <functional>

namespace Conqueror
{
    struct WindowProps
    {
        std::string Title;
        uint32_t Width;
        uint32_t Height;

        WindowProps(const std::string& title = "Conqueror Engine",
                   uint32_t width = 1920,
                   uint32_t height = 1080)
            : Title(title), Width(width), Height(height)
        {
        }
    };

    class CQ_API Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        virtual ~Window() = default;

        virtual void OnUpdate() = 0;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;

        virtual void* GetNativeWindow() const = 0;

        static Window* Create(const WindowProps& props = WindowProps());
    };
}
