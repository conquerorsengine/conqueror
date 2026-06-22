#pragma once

#include "Core/Base/Base.h"
#include "ShadowMap.h"
#include "Renderer/RHI/Shader.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;
    class Entity;

    class CQ_API ShadowPass
    {
    public:
        ShadowPass();
        ~ShadowPass() = default;

        void Init();
        void Shutdown();

        void Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                     const glm::vec3& lightDirection);

        void BindShadowMapsToShader(std::shared_ptr<Shader> shader);

        std::shared_ptr<ShadowMap> GetDirectionalShadowMap() const { return m_DirectionalShadowMap; }
        const std::vector<glm::mat4>& GetLightSpaceMatrices() const { return m_LightSpaceMatrices; }

        // Cascade settings
        int CascadeCount = 4;
        float CascadeSplitLambda = 0.75f;
        float CascadeNearPlane = 0.1f;
        float CascadeFarPlane = 100.0f;
        float ShadowBias = 0.005f;
        float NormalBias = 0.02f;

    private:
        void CalculateCascadeSplits(float nearPlane, float farPlane, std::vector<float>& splits);
        glm::mat4 CalculateLightSpaceMatrix(const glm::vec3& lightDir,
                                             const glm::vec3& center, float radius,
                                             float nearZ, float farZ);
        void CalculateFrustumCorners(const glm::mat4& viewProj, glm::vec3* corners);
        float CalculateFitRadius(const glm::vec3* corners, const glm::vec3& lightDir);

        std::shared_ptr<ShadowMap> m_DirectionalShadowMap;
        std::shared_ptr<Shader> m_DepthShader;
        std::vector<glm::mat4> m_LightSpaceMatrices;
        std::vector<float> m_CascadeSplits;
    };
}
