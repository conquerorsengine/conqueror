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
#include <limits>

namespace Conqueror
{
    static const char* s_DepthVert = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        void main()
        {
            gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_DepthFrag = R"(
        #version 330 core
        void main()
        {
        }
    )";

    ShadowPass::ShadowPass() {}

    bool ShadowPass::Init()
    {
        glGenFramebuffers(1, &m_ShadowFBO);

        glGenTextures(1, &m_ShadowDepthTex);
        glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_ShadowWidth, m_ShadowHeight, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_ShadowDepthTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            CQ_CORE_ERROR("Shadow FBO incomplete!");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_Ready = false;
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_DepthShader = Shader::Create("ShadowDepth", s_DepthVert, s_DepthFrag);
        if (!m_DepthShader)
        {
            CQ_CORE_ERROR("Shadow depth shader failed!");
            m_Ready = false;
            return false;
        }

        m_Ready = true;
        CQ_CORE_INFO("Shadow pass initialized ({0}x{1})", m_ShadowWidth, m_ShadowHeight);
        return true;
    }

    void ShadowPass::Shutdown()
    {
        if (m_ShadowDepthTex) { glDeleteTextures(1, &m_ShadowDepthTex); m_ShadowDepthTex = 0; }
        if (m_ShadowFBO) { glDeleteFramebuffers(1, &m_ShadowFBO); m_ShadowFBO = 0; }
        m_DepthShader.reset();
        m_Ready = false;
    }

    static glm::vec3 GetFrustumCenter(const glm::mat4& invViewProj)
    {
        glm::vec3 corners[8] = {
            glm::vec3(-1, -1, -1), glm::vec3( 1, -1, -1),
            glm::vec3( 1,  1, -1), glm::vec3(-1,  1, -1),
            glm::vec3(-1, -1,  1), glm::vec3( 1, -1,  1),
            glm::vec3( 1,  1,  1), glm::vec3(-1,  1,  1),
        };

        glm::vec3 center(0.0f);
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 wp = invViewProj * glm::vec4(corners[i], 1.0f);
            corners[i] = glm::vec3(wp) / wp.w;
            center += corners[i];
        }
        return center / 8.0f;
    }

    static void GetFrustumSphere(const glm::mat4& invViewProj, glm::vec3& center, float& radius)
    {
        glm::vec3 corners[8] = {
            glm::vec3(-1, -1, -1), glm::vec3( 1, -1, -1),
            glm::vec3( 1,  1, -1), glm::vec3(-1,  1, -1),
            glm::vec3(-1, -1,  1), glm::vec3( 1, -1,  1),
            glm::vec3( 1,  1,  1), glm::vec3(-1,  1,  1),
        };

        center = glm::vec3(0.0f);
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 wp = invViewProj * glm::vec4(corners[i], 1.0f);
            corners[i] = glm::vec3(wp) / wp.w;
            center += corners[i];
        }
        center /= 8.0f;

        radius = 0.0f;
        for (int i = 0; i < 8; i++)
            radius = glm::max(radius, glm::length(corners[i] - center));

        // Snap radius to reduce shadow shimmer
        float unitsPerTexel = (radius * 2.0f) / 2048.0f;
        radius = std::ceil(radius / unitsPerTexel) * unitsPerTexel;
    }

    void ShadowPass::Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                              const glm::vec3& lightDirection, const glm::vec3& cameraPosition,
                              const glm::mat4& cameraViewProj)
    {
        if (!m_Ready || !scene || !m_DepthShader)
            return;

        GLint prevFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        glm::vec3 lightDir = glm::normalize(lightDirection);

        // Kamera frustumundan light space matrix hesapla
        glm::mat4 invViewProj = glm::inverse(cameraViewProj);

        glm::vec3 frustumCenter;
        float frustumRadius;
        GetFrustumSphere(invViewProj, frustumCenter, frustumRadius);

        // Shadow distance ile sinirla
        frustumRadius = glm::min(frustumRadius, ShadowDistance);

        // Light position: frustum merkezinden isik yonunde uzaklas
        glm::vec3 lightPos = frustumCenter - lightDir * frustumRadius;

        // Snap light position to texel grid (shimmer onleme)
        float orthoSize = frustumRadius;
        float unitsPerTexel = (orthoSize * 2.0f) / (float)m_ShadowWidth;
        lightPos.x = std::floor(lightPos.x / unitsPerTexel) * unitsPerTexel;
        lightPos.y = std::floor(lightPos.y / unitsPerTexel) * unitsPerTexel;

        glm::mat4 lightView = glm::lookAt(lightPos, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, orthoSize * 2.0f);
        m_LightSpaceMatrix = lightProj * lightView;

        // Shadow FBO'ya render et
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
        glViewport(0, 0, m_ShadowWidth, m_ShadowHeight);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);

        auto& registry = scene->m_Registry;

        auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            m_DepthShader->SetMat4("u_Transform", transform.GetTransform());

            switch (meshRenderer.Type)
            {
                case MeshType::Sphere:
                    RenderCommand::DrawIndexed(Renderer3D::GetSphereMesh()->GetVertexArray(), Renderer3D::GetSphereMesh()->GetIndexCount());
                    break;
                case MeshType::Plane:
                    RenderCommand::DrawIndexed(Renderer3D::GetPlaneMesh()->GetVertexArray(), Renderer3D::GetPlaneMesh()->GetIndexCount());
                    break;
                case MeshType::Cylinder:
                    RenderCommand::DrawIndexed(Renderer3D::GetCylinderMesh()->GetVertexArray(), Renderer3D::GetCylinderMesh()->GetIndexCount());
                    break;
                default:
                    RenderCommand::DrawIndexed(Renderer3D::GetCubeMesh()->GetVertexArray(), Renderer3D::GetCubeMesh()->GetIndexCount());
                    break;
            }
        }

        auto modelView = registry.view<TransformComponent, ModelComponent>();
        for (auto entity : modelView)
        {
            auto [transform, modelComp] = modelView.get<TransformComponent, ModelComponent>(entity);
            if (!modelComp.ModelData || modelComp.ModelData->Meshes.empty())
                continue;

            m_DepthShader->SetMat4("u_Transform", transform.GetTransform());
            for (size_t j = 0; j < modelComp.ModelData->Meshes.size(); j++)
            {
                auto& mesh = modelComp.ModelData->Meshes[j];
                RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
            }
        }

        glEnable(GL_CULL_FACE);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    void ShadowPass::BindShadowMapsToShader(std::shared_ptr<Shader> shader)
    {
        if (!shader || !m_Ready || !m_ShadowDepthTex)
            return;

        shader->Bind();

        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTex);
        shader->SetInt("u_ShadowMap", 8);
        shader->SetMat4("u_LightSpaceMatrices[0]", m_LightSpaceMatrix);
        shader->SetInt("u_CascadeCount", 1);
        shader->SetFloat("u_ShadowBias", ShadowBias);
        shader->SetInt("u_ShadowEnabled", 1);
    }
}
