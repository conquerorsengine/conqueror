#pragma once

#include "Core/Application.h"
#include "Core/Layer.h"
#include "Scene/Scene.h"
#include <memory>
#include <filesystem>

namespace Conqueror::Runtime
{
    class RuntimeLayer : public Layer
    {
    public:
        RuntimeLayer(const std::filesystem::path& projectPath);
        virtual ~RuntimeLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(Timestep ts) override;
        virtual void OnEvent(Event& e) override;

    private:
        bool LoadProject();

    private:
        std::filesystem::path m_ProjectPath;
        std::shared_ptr<Scene> m_ActiveScene;
    };
}
