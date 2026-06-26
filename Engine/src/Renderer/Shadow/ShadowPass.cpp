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
                              const glm::vec3& lightDirection)
    {
        if (!scene || !m_DirectionalShadowMap || !m_DepthShader)
            return;

        // Camera bilgisini al
        auto& registry = scene->m_Registry;

        // View proj matrix hesapla (editor veya runtime camera'dan)
        // Su an icin scene'in camera bilgisini kullaniyoruz
        glm::mat4 viewProj = glm::mat4(1.0f);

        // Cascade split'leri hesapla
        CalculateCascadeSplits(CascadeNearPlane, CascadeFarPlane, m_CascadeSplits);

        // Her cascade icin shadow map render et
        m_DirectionalShadowMap->Bind();
        glViewport(0, 0, m_DirectionalShadowMap->GetWidth(), m_DirectionalShadowMap->GetHeight());
        glClear(GL_DEPTH_BUFFER_BIT);

        glm::vec3 lightDir = glm::normalize(lightDirection);

        for (int i = 0; i < CascadeCount; i++)
        {
            float nearZ = (i == 0) ? CascadeNearPlane : m_CascadeSplits[i - 1];
            float farZ = m_CascadeSplits[i];

            // Bu cascade'in view-proj matrix'ini hesapla
            glm::mat4 lightView = glm::lookAt(
                glm::vec3(0.0f),
                lightDir,
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            // Frustum corners hesapla (simplified)
            float tanHalfFov = glm::radians(45.0f);
            float aspect = 1.0f;

            float nearHeight = nearZ * tanHalfFov;
            float nearWidth = nearHeight * aspect;
            float farHeight = farZ * tanHalfFov;
            float farWidth = farHeight * aspect;

            glm::vec3 corners[8] = {
                glm::vec3(-nearWidth, -nearHeight, -nearZ),
                glm::vec3( nearWidth, -nearHeight, -nearZ),
                glm::vec3( nearWidth,  nearHeight, -nearZ),
                glm::vec3(-nearWidth,  nearHeight, -nearZ),
                glm::vec3(-farWidth,  -farHeight,  -farZ),
                glm::vec3( farWidth,  -farHeight,  -farZ),
                glm::vec3( farWidth,   farHeight,  -farZ),
                glm::vec3(-farWidth,   farHeight,  -farZ),
            };

            // Center ve radius hesapla
            glm::vec3 center(0.0f);
            for (int j = 0; j < 8; j++)
                center += corners[j];
            center /= 8.0f;

            float radius = 0.0f;
            for (int j = 0; j < 8; j++)
                radius = glm::max(radius, glm::length(corners[j] - center));
            radius = glm::ceil(radius * 16.0f) / 16.0f;

            // Light space matrix
            glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 2.0f);
            glm::vec3 shadowCenter = center - lightDir * radius;
            glm::mat4 shadowView = glm::lookAt(shadowCenter, center, glm::vec3(0.0f, 1.0f, 0.0f));

            m_LightSpaceMatrices[i] = lightProjection * shadowView;

            // Viewport ayarla (cascade'e gore)
            uint32_t cascadeWidth = m_DirectionalShadowMap->GetWidth() / CascadeCount;
            uint32_t cascadeHeight = m_DirectionalShadowMap->GetHeight();
            glViewport(i * cascadeWidth, 0, cascadeWidth, cascadeHeight);

            // Shadow pass shader'i bind et
            m_DepthShader->Bind();
            m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);

            // Tum mesh renderer'lari render et (depth-only)
            auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
            for (auto entity : meshView)
            {
                auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);

                m_DepthShader->SetMat4("u_Transform", transform.GetTransform());

                // MeshType'e gore render
                switch (meshRenderer.Type)
                {
                    case MeshType::Sphere:   RenderCommand::DrawIndexed(Renderer3D::GetSphereMesh()->GetVertexArray(), Renderer3D::GetSphereMesh()->GetIndexCount()); break;
                    case MeshType::Plane:    RenderCommand::DrawIndexed(Renderer3D::GetPlaneMesh()->GetVertexArray(), Renderer3D::GetPlaneMesh()->GetIndexCount()); break;
                    case MeshType::Cylinder: RenderCommand::DrawIndexed(Renderer3D::GetCylinderMesh()->GetVertexArray(), Renderer3D::GetCylinderMesh()->GetIndexCount()); break;
                    default:                 RenderCommand::DrawIndexed(Renderer3D::GetCubeMesh()->GetVertexArray(), Renderer3D::GetCubeMesh()->GetIndexCount()); break;
                }
            }

            // Model renderer'lari da render et
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

        // Viewport'u geri yukle
        // (Editor veya window size'a gore)
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
