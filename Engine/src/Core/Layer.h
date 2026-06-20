#pragma once

#include "Core/Base/Base.h"
#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"

namespace Conqueror
{
    class CQ_API Layer
    {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate([[maybe_unused]] Timestep ts) {}
        virtual void OnImGuiRender() {}
        virtual void OnEvent([[maybe_unused]] Event& event) {}

        const std::string& GetName() const { return m_DebugName; }

    protected:
        std::string m_DebugName;
    };
}
