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
                     const glm::vec3& lightDirection,
                     const glm::mat4& cameraView = glm::mat4(1.0f),
                     const glm::mat4& cameraProj = glm::mat4(1.0f),
                     const glm::vec3& cameraPos = glm::vec3(0.0f));

        void BindShadowMapsToShader(std::shared_ptr<Shader> shader);

        std::shared_ptr<ShadowMap> GetDirectionalShadowMap() const { return m_DirectionalShadowMap; }
        const std::vector<glm::mat4>& GetLightSpaceMatrices() const { return m_LightSpaceMatrices; }

        // Cascade settings (oda ölçekli sahneler için 2 cascade:
        // 4096 map 2'ye bölününce tile başına 2048px düşer, kenarlar pürüzsüzleşir)
        int CascadeCount = 2;
        float CascadeSplitLambda = 0.75f;
        float CascadeNearPlane = 0.1f;
        float CascadeFarPlane = 80.0f;
        float ShadowBias = 0.005f;
        float NormalBias = 0.015f;
        // Cascade geçiş yumuşatma bandı (dünya birimi): kamera hareketinde
        // cascade atlaması sert patlamasın diye.
        float CascadeFade = 1.5f;

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
        // Cascade başına dünya-birimi texel boyutu (bias tabanı için shader'a gönderilir)
        std::vector<float> m_CascadeTexelWorld;
    };
}
