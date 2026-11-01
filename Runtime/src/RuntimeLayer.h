#pragma once

#include "UI/UISystem.h"

#include "Core/Application.h"
#include "Core/Layer.h"
#include "Scene/Scene.h"
#include "Renderer/RHI/Texture.h"
#include <memory>
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>

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
        void SwitchScene(const std::filesystem::path& path);
        // Açılış ekranı (Player Settings > Splash Image): logolar belirip kaybolur, ilk sahne logo tam görünürken yüklenir
        void SetupSplash();
        void DrawSplash();

    private:
        std::filesystem::path m_ProjectPath;
        std::shared_ptr<Scene> m_ActiveScene;
        UIInputState m_UIInput;
        bool m_PrevMouseDown = false;
        bool m_WindowWasFocused = false;

        struct SplashLogo
        {
            std::shared_ptr<Texture2D> Texture;
            float Height = 0.3f;    // pencere yüksekliğine oran
        };
        std::vector<SplashLogo> m_SplashLogos;
        bool m_SplashActive = false;
        bool m_ProjectLoaded = false;
        float m_SplashTime = 0.0f;
        float m_SplashDuration = 2.0f;
        glm::vec3 m_SplashBackground{ 0.067f, 0.071f, 0.086f };
    };
}
