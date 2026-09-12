#include "ShadowPass.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Conqueror
{
    static const char* s_DepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        void main()
        {
            gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_DepthFragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 FragColor;
        void main()
        {
            FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
        }
    )";

    ShadowPass::ShadowPass()
    {
    }

    void ShadowPass::Init()
    {
        m_DirectionalShadowMap = ShadowMap::Create(2048, 2048);
        m_DepthShader = Shader::Create("ShadowDepthShader", s_DepthVertexSrc, s_DepthFragmentSrc);
        m_LightSpaceMatrices.resize(CascadeCount);
        m_CascadeSplits.resize(CascadeCount);
    }

    void ShadowPass::Shutdown()
    {
        m_DirectionalShadowMap.reset();
        m_DepthShader.reset();
        m_LightSpaceMatrices.clear();
        m_CascadeSplits.clear();
    }

    void ShadowPass::Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                              const glm::vec3& lightDirection,
                              const glm::mat4& cameraView,
                              const glm::mat4& cameraProj,
                              const glm::vec3& cameraPos)
    {
        if (!scene || !m_DirectionalShadowMap || !m_DepthShader)
            return;

        auto& registry = scene->m_Registry;

        // Cascade splits hesapla
        CalculateCascadeSplits(CascadeNearPlane, CascadeFarPlane, m_CascadeSplits);

        // Her cascade icin shadow map render et
        m_DirectionalShadowMap->Bind();
        glViewport(0, 0, m_DirectionalShadowMap->GetWidth(), m_DirectionalShadowMap->GetHeight());
        glClear(GL_DEPTH_BUFFER_BIT);

        glm::vec3 lightDir = glm::normalize(lightDirection);

        // Unproject camera frustum NDC corners
        glm::mat4 invCamVP = glm::inverse(cameraProj * cameraView);
        glm::vec3 ndcNear[4] = {
            glm::vec3(-1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f,  1.0f, -1.0f),
            glm::vec3(-1.0f,  1.0f, -1.0f)
        };
        glm::vec3 ndcFar[4] = {
            glm::vec3(-1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f)
        };

        glm::vec3 worldNear[4], worldFar[4];
        for (int k = 0; k < 4; k++)
        {
            glm::vec4 wN = invCamVP * glm::vec4(ndcNear[k], 1.0f);
            worldNear[k] = glm::vec3(wN) / wN.w;
            glm::vec4 wF = invCamVP * glm::vec4(ndcFar[k], 1.0f);
            worldFar[k] = glm::vec3(wF) / wF.w;
        }

        for (int i = 0; i < CascadeCount; i++)
        {
            float prevSplit = (i == 0) ? CascadeNearPlane : m_CascadeSplits[i - 1];
            float split = m_CascadeSplits[i];

            float tn = (prevSplit - CascadeNearPlane) / (CascadeFarPlane - CascadeNearPlane);
            float tf = (split - CascadeNearPlane) / (CascadeFarPlane - CascadeNearPlane);
            tn = glm::clamp(tn, 0.0f, 1.0f);
            tf = glm::clamp(tf, 0.0f, 1.0f);

            glm::vec3 corners[8];
            for (int k = 0; k < 4; k++)
            {
                corners[k]     = glm::mix(worldNear[k], worldFar[k], tn);
                corners[k + 4] = glm::mix(worldNear[k], worldFar[k], tf);
            }

            // Center ve radius
            glm::vec3 center(0.0f);
            for (int j = 0; j < 8; j++)
                center += corners[j];
            center /= 8.0f;

            float radius = 0.0f;
            for (int j = 0; j < 8; j++)
                radius = glm::max(radius, glm::length(corners[j] - center));
            radius = glm::max(radius, 5.0f);

            // Texel snapping to stabilize shadow map
            float cascadeTexWidth = (float)(m_DirectionalShadowMap->GetWidth() / CascadeCount);
            float texelSize = (radius * 2.0f) / cascadeTexWidth;

            glm::mat4 lightView = glm::lookAt(center - lightDir * radius * 2.0f, center, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec4 centerLS = lightView * glm::vec4(center, 1.0f);
            centerLS.x = std::floor(centerLS.x / texelSize) * texelSize;
            centerLS.y = std::floor(centerLS.y / texelSize) * texelSize;
            center = glm::vec3(glm::inverse(lightView) * centerLS);

            lightView = glm::lookAt(center - lightDir * radius * 2.0f, center, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);

            m_LightSpaceMatrices[i] = lightProjection * lightView;

            // Viewport ayarla (cascade'e gore)
            uint32_t cascadeWidth = m_DirectionalShadowMap->GetWidth() / CascadeCount;
            uint32_t cascadeHeight = m_DirectionalShadowMap->GetHeight();
            glViewport(i * cascadeWidth, 0, cascadeWidth, cascadeHeight);

            // Depth shader bind
            m_DepthShader->Bind();
            m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);

            // Mesh renderer'lari render et
            auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
            for (auto entity : meshView)
            {
                auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);

                m_DepthShader->SetMat4("u_Transform", transform.GetTransform());

                switch (meshRenderer.Type)
                {
                    case MeshType::Sphere:   RenderCommand::DrawIndexed(Renderer3D::GetSphereMesh()->GetVertexArray(), Renderer3D::GetSphereMesh()->GetIndexCount()); break;
                    case MeshType::Plane:    RenderCommand::DrawIndexed(Renderer3D::GetPlaneMesh()->GetVertexArray(), Renderer3D::GetPlaneMesh()->GetIndexCount()); break;
                    case MeshType::Cylinder: RenderCommand::DrawIndexed(Renderer3D::GetCylinderMesh()->GetVertexArray(), Renderer3D::GetCylinderMesh()->GetIndexCount()); break;
                    default:                 RenderCommand::DrawIndexed(Renderer3D::GetCubeMesh()->GetVertexArray(), Renderer3D::GetCubeMesh()->GetIndexCount()); break;
                }
            }

            // Model renderer'lari render et
            auto modelView = registry.view<TransformComponent, ModelComponent>();
            for (auto entity : modelView)
            {
                auto [transform, modelComponent] = modelView.get<TransformComponent, ModelComponent>(entity);

                if (!modelComponent.ModelData || modelComponent.ModelData->Meshes.empty())
                    continue;

                m_DepthShader->SetMat4("u_Transform", transform.GetTransform());

                for (size_t j = 0; j < modelComponent.ModelData->Meshes.size(); j++)
                {
                    auto& mesh = modelComponent.ModelData->Meshes[j];
                    RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                }
            }
        }

        m_DirectionalShadowMap->Unbind();
    }

    void ShadowPass::BindShadowMapsToShader(std::shared_ptr<Shader> shader)
    {
        if (!shader || !m_DirectionalShadowMap)
            return;

        shader->Bind();

        // Shadow map'i texture slot 8'e bind et
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, m_DirectionalShadowMap->GetDepthTextureID());
        shader->SetInt("u_ShadowMap", 8);

        // Light space matrix'leri gonder
        for (int i = 0; i < CascadeCount; i++)
        {
            std::string name = "u_LightSpaceMatrices[" + std::to_string(i) + "]";
            shader->SetMat4(name, m_LightSpaceMatrices[i]);
        }

        // Cascade split'leri gonder
        for (int i = 0; i < CascadeCount; i++)
        {
            std::string name = "u_CascadeSplits[" + std::to_string(i) + "]";
            shader->SetFloat(name, m_CascadeSplits[i]);
        }

        shader->SetInt("u_CascadeCount", CascadeCount);
        shader->SetFloat("u_ShadowBias", ShadowBias);
        shader->SetFloat("u_NormalBias", NormalBias);
    }

    void ShadowPass::CalculateCascadeSplits(float nearPlane, float farPlane, std::vector<float>& splits)
    {
        float range = farPlane - nearPlane;
        float ratio = farPlane / nearPlane;

        for (int i = 0; i < CascadeCount; i++)
        {
            float p = (float)(i + 1) / (float)CascadeCount;
            float logSplit = nearPlane * std::pow(ratio, p);
            float uniformSplit = nearPlane + range * p;
            splits[i] = CascadeSplitLambda * logSplit + (1.0f - CascadeSplitLambda) * uniformSplit;
        }
    }

    glm::mat4 ShadowPass::CalculateLightSpaceMatrix(const glm::vec3& lightDir,
                                                      const glm::vec3& center, float radius,
                                                      float nearZ, float farZ)
    {
        glm::mat4 lightView = glm::lookAt(center - lightDir * radius, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 2.0f);
        return lightProj * lightView;
    }

    void ShadowPass::CalculateFrustumCorners(const glm::mat4& viewProj, glm::vec3* corners)
    {
        // View-projection matrix'in inverse'ini al
        glm::mat4 invVP = glm::inverse(viewProj);

        int index = 0;
        for (int x = 0; x < 2; x++)
        {
            for (int y = 0; y < 2; y++)
            {
                for (int z = 0; z < 2; z++)
                {
                    glm::vec4 clip(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 2.0f - 1.0f, 1.0f);
                    glm::vec4 worldPos = invVP * clip;
                    corners[index++] = glm::vec3(worldPos) / worldPos.w;
                }
            }
        }
    }

    float ShadowPass::CalculateFitRadius(const glm::vec3* corners, const glm::vec3& lightDir)
    {
        glm::vec3 center(0.0f);
        for (int i = 0; i < 8; i++)
            center += corners[i];
        center /= 8.0f;

        float radius = 0.0f;
        for (int i = 0; i < 8; i++)
            radius = glm::max(radius, glm::length(corners[i] - center));

        return radius;
    }
}
