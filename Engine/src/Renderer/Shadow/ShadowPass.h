#pragma once

#include "Core/Base/Base.h"
#include "ShadowMap.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RenderSettings.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Mesh;
    class Texture2D;

    class Scene;
    class Entity;

    // Güneş (yönlü ışık) için cascaded shadow map.
    // Cascade'ler tek bir atlasa yerleştirilir: 1 -> tam, 2 -> 2x1, 3-4 -> 2x2.
    class CQ_API ShadowPass
    {
    public:
        static constexpr int kMaxCascades = 4;

        ShadowPass();
        ~ShadowPass() = default;

        void Init();
        void Shutdown();

        // Kamera: view/projection + konum; ayarlar sahnenin RenderSettings.Shadows'u
        void Execute(Scene* scene, const glm::vec3& lightDirection,
                     const glm::mat4& cameraView, const glm::mat4& cameraProj,
                     const glm::vec3& cameraPos, const ShadowSettings& settings);

        // Geriye uyumluluk: varsayılan ayarlarla çalışır
        void Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                     const glm::vec3& lightDirection,
                     const glm::mat4& cameraView = glm::mat4(1.0f),
                     const glm::mat4& cameraProj = glm::mat4(1.0f),
                     const glm::vec3& cameraPos = glm::vec3(0.0f));

        uint32_t GetAtlasTextureID() const;
        std::shared_ptr<ShadowMap> GetDirectionalShadowMap() const { return m_DirectionalShadowMap; }
        const std::vector<glm::mat4>& GetLightSpaceMatrices() const { return m_LightSpaceMatrices; }
        const std::vector<float>& GetCascadeSplits() const { return m_CascadeSplits; }
        const std::vector<float>& GetCascadeTexelWorld() const { return m_CascadeTexelWorld; }
        const std::vector<glm::vec4>& GetCascadeRects() const { return m_CascadeRects; }
        int GetActiveCascadeCount() const { return m_ActiveCascades; }
        bool HasValidData() const { return m_ActiveCascades > 0; }

        // Eski API (shader uniform'larına doğrudan yazar) - özel shader'lar için
        void BindShadowMapsToShader(std::shared_ptr<Shader> shader);

    private:
        void CalculateCascadeSplits(float nearPlane, float farPlane, int count, float lambda);
        void EnsureResolution(int resolution);

        std::shared_ptr<ShadowMap> m_DirectionalShadowMap;
        std::shared_ptr<Shader> m_DepthShader;
        std::shared_ptr<Shader> m_SkinnedDepthShader;
        std::shared_ptr<Shader> m_InstancedDepthShader;
        std::shared_ptr<Shader> m_CutoutDepthShader;     // alfa kırpmalı malzemeler (yaprak, tel örgü)

        bool CutoutReady() const;
        void DrawCutout(const Mesh& mesh, const glm::mat4& transform, const glm::mat4& lightSpace, Texture2D& albedo,
                        float alphaClip, float alpha, const glm::vec2& uvScale, const glm::vec2& uvOffset, const glm::vec4& wind = glm::vec4(0.0f),
                        const glm::vec4& windParams = glm::vec4(0.0f));
        std::vector<glm::mat4> m_LightSpaceMatrices;
        std::vector<float> m_CascadeSplits;
        std::vector<float> m_CascadeTexelWorld;
        std::vector<glm::vec4> m_CascadeRects;
        int m_ActiveCascades = 0;
        ShadowSettings m_LastSettings;
    };
}
