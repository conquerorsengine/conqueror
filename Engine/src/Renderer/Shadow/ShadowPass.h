#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Shader.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    class CQ_API ShadowPass
    {
    public:
        ShadowPass();
        ~ShadowPass() = default;

        bool Init();
        void Shutdown();

        void Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                     const glm::vec3& lightDirection, const glm::vec3& cameraPosition,
                     const glm::mat4& cameraViewProj);

        void BindShadowMapsToShader(std::shared_ptr<Shader> shader);

        bool IsReady() const { return m_Ready; }

        float ShadowBias = 0.005f;
        float NormalBias = 0.02f;
        float ShadowDistance = 60.0f;

    private:
        bool m_Ready = false;
        uint32_t m_ShadowFBO = 0;
        uint32_t m_ShadowDepthTex = 0;
        uint32_t m_ShadowWidth = 2048;
        uint32_t m_ShadowHeight = 2048;
        std::shared_ptr<Shader> m_DepthShader;
        glm::mat4 m_LightSpaceMatrix = glm::mat4(1.0f);
    };
}
