#pragma once

#include "Core/Layer.h"
#include "Core/Events/Event.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

namespace Conqueror
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        virtual ~ImGuiLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& e) override;

        void Begin();
        void End();

        void BlockEvents(bool block) { m_BlockEvents = block; }
        
        void SetDarkThemeColors();

    private:
        bool m_BlockEvents = true;
        float m_Time = 0.0f;
    };
}
