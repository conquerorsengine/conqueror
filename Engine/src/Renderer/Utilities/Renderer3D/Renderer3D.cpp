#include "Renderer3D.h"
#include "Renderer/Renderer.h"
#include "ModelLoader.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/RHI/Cubemap.h"

#include <algorithm>

#include <glad/glad.h>

namespace Conqueror
{
    Renderer3D::SceneData* Renderer3D::s_SceneData = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_CubeMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_PlaneMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_CylinderMesh = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_PBRShader = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_PBRSkinnedShader = nullptr;
    std::vector<glm::mat4> Renderer3D::s_BoneMatrixScratch;
    std::shared_ptr<Mesh> Renderer3D::s_SphereMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_ConeMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_ArrowMesh = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_UnlitShader = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_SkyboxShader = nullptr;

    void Renderer3D::Init()
    {
        s_SceneData = new SceneData();

        // Default directional light (scene'de yoksa kullanılacak)
        s_SceneData->DirectionalLight.Direction = glm::vec3(-0.5f, -1.0f, -0.3f);
        s_SceneData->DirectionalLight.Color = glm::vec3(1.0f, 1.0f, 1.0f);
        s_SceneData->DirectionalLight.Intensity = 0.5f; // Düşük ambient

        // Cube mesh oluştur
        s_CubeMesh = Mesh::CreateCube();

        // Plane mesh oluştur
        s_PlaneMesh = Mesh::CreatePlane(2.0f, 1);

        // Cylinder mesh oluştur
        s_CylinderMesh = Mesh::CreateCylinder(1.0f, 2.0f, 32);

        // Sphere mesh oluştur (light gizmo için)
        s_SphereMesh = Mesh::CreateSphere(1.0f, 64);
        
        // Cone mesh oluştur (spot light gizmo için)
        s_ConeMesh = Mesh::CreateCone(0.5f, 1.0f, 64);
        
        // Arrow mesh oluştur (directional light gizmo için)
        s_ArrowMesh = Mesh::CreateArrow(0.05f, 0.7f, 0.2f, 0.3f, 32);

        // PBR shader yükle
        s_PBRShader = Shader::Create("Engine/src/Shaders/3D/Forward/PBR.cqsh");
        s_PBRSkinnedShader = Shader::Create("Engine/src/Shaders/3D/Forward/PBRSkinned.cqsh");
        
        // Unlit shader yükle (gizmo'lar için)
        s_UnlitShader = Shader::Create("Engine/src/Shaders/3D/Forward/Unlit.cqsh");

        // Skybox shader yükle
        s_SkyboxShader = Shader::Create("Engine/src/Shaders/3D/Skybox.cqsh");
    }

    void Renderer3D::Shutdown()
    {
        delete s_SceneData;
    }

    void Renderer3D::BeginScene(EditorCamera& camera)
    {
        s_SceneData->ViewProjectionMatrix = camera.GetViewProjection();
        s_SceneData->CameraPosition = camera.GetPosition();

        // Shader'a kamera bilgilerini gönder
        s_PBRShader->Bind();
        s_PBRShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_PBRShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);
    }

    void Renderer3D::BeginScene(EditorCamera& camera, int lightingSource,
                                 const glm::vec3& ambientColor, const glm::vec3& skyColor,
                                 const glm::vec3& equatorColor, const glm::vec3& groundColor,
                                 float ambientIntensity, bool fogEnabled, const glm::vec3& fogColor, 
                                 float fogStart, float fogEnd)
    {
        BeginScene(camera);
        
        // Ambient
        s_PBRShader->Bind();
        s_PBRShader->SetInt("u_AmbientLightingSource", lightingSource);
        s_PBRShader->SetFloat3("u_AmbientColor", ambientColor);
        s_PBRShader->SetFloat3("u_AmbientSkyColor", skyColor);
        s_PBRShader->SetFloat3("u_AmbientEquatorColor", equatorColor);
        s_PBRShader->SetFloat3("u_AmbientGroundColor", groundColor);
        s_PBRShader->SetFloat("u_AmbientIntensity", ambientIntensity);
        
        // Fog
        s_PBRShader->SetInt("u_FogEnabled", fogEnabled ? 1 : 0);
        s_PBRShader->SetFloat3("u_FogColor", fogColor);
        s_PBRShader->SetFloat("u_FogStart", fogStart);
        s_PBRShader->SetFloat("u_FogEnd", fogEnd);
    }

    void Renderer3D::BeginScene(const SceneCamera& camera, const glm::mat4& transform)
    {
        s_SceneData->ViewProjectionMatrix = camera.GetProjection() * glm::inverse(transform);
        s_SceneData->CameraPosition = glm::vec3(transform[3]);

        s_PBRShader->Bind();
        s_PBRShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_PBRShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);
    }

    void Renderer3D::BeginScene(const SceneCamera& camera, const glm::mat4& transform, int lightingSource,
                                 const glm::vec3& ambientColor, const glm::vec3& skyColor,
                                 const glm::vec3& equatorColor, const glm::vec3& groundColor,
                                 float ambientIntensity, bool fogEnabled, const glm::vec3& fogColor, 
                                 float fogStart, float fogEnd)
    {
        BeginScene(camera, transform);
        
        // Ambient
        s_PBRShader->Bind();
        s_PBRShader->SetInt("u_AmbientLightingSource", lightingSource);
        s_PBRShader->SetFloat3("u_AmbientColor", ambientColor);
        s_PBRShader->SetFloat3("u_AmbientSkyColor", skyColor);
        s_PBRShader->SetFloat3("u_AmbientEquatorColor", equatorColor);
        s_PBRShader->SetFloat3("u_AmbientGroundColor", groundColor);
        s_PBRShader->SetFloat("u_AmbientIntensity", ambientIntensity);
        
        // Fog
        s_PBRShader->SetInt("u_FogEnabled", fogEnabled ? 1 : 0);
        s_PBRShader->SetFloat3("u_FogColor", fogColor);
        s_PBRShader->SetFloat("u_FogStart", fogStart);
        s_PBRShader->SetFloat("u_FogEnd", fogEnd);
    }

    void Renderer3D::EndScene()
    {
        // Light listelerini temizle
        s_SceneData->PointLights.clear();
        s_SceneData->SpotLights.clear();
    }

    void Renderer3D::SetDirectionalLight(const DirectionalLightComponent& light)
    {
        s_SceneData->DirectionalLight = light;
    }

    void Renderer3D::AddPointLight(const glm::vec3& position, const PointLightComponent& light)
    {
        s_SceneData->PointLights.push_back({ position, light });
    }

    void Renderer3D::AddSpotLight(const glm::vec3& position, const SpotLightComponent& light)
    {
        s_SceneData->SpotLights.push_back({ position, light });
    }

    void Renderer3D::ClearLights()
    {
        s_SceneData->PointLights.clear();
        s_SceneData->SpotLights.clear();
    }

    void Renderer3D::BindLightsToShader(std::shared_ptr<Shader> shader)
    {
        shader->Bind();

        // Directional light
        shader->SetFloat3("u_DirLight.Direction", s_SceneData->DirectionalLight.Direction);
        shader->SetFloat3("u_DirLight.Color", s_SceneData->DirectionalLight.Color);
        shader->SetFloat("u_DirLight.Intensity", s_SceneData->DirectionalLight.Intensity);

        // Point lights
        int numPointLights = std::min((int)s_SceneData->PointLights.size(), 16);
        shader->SetInt("u_NumPointLights", numPointLights);

        for (int i = 0; i < numPointLights; i++)
        {
            std::string base = "u_PointLights[" + std::to_string(i) + "]";
            const auto& [pos, light] = s_SceneData->PointLights[i];

            shader->SetFloat3(base + ".Position", pos);
            shader->SetFloat3(base + ".Color", light.Color);
            shader->SetFloat(base + ".Intensity", light.Intensity);
            shader->SetFloat(base + ".Range", light.Range);
            shader->SetFloat(base + ".Constant", light.Constant);
            shader->SetFloat(base + ".Linear", light.Linear);
            shader->SetFloat(base + ".Quadratic", light.Quadratic);
        }

        // Spot lights
        int numSpotLights = std::min((int)s_SceneData->SpotLights.size(), 8);
        shader->SetInt("u_NumSpotLights", numSpotLights);

        for (int i = 0; i < numSpotLights; i++)
        {
            std::string base = "u_SpotLights[" + std::to_string(i) + "]";
            const auto& [pos, light] = s_SceneData->SpotLights[i];

            shader->SetFloat3(base + ".Position", pos);
            shader->SetFloat3(base + ".Direction", light.Direction);
            shader->SetFloat3(base + ".Color", light.Color);
            shader->SetFloat(base + ".Intensity", light.Intensity);
            shader->SetFloat(base + ".Range", light.Range);
            shader->SetFloat(base + ".InnerCutoff", glm::cos(glm::radians(light.InnerConeAngle)));
            shader->SetFloat(base + ".OuterCutoff", glm::cos(glm::radians(light.OuterConeAngle)));
            shader->SetFloat(base + ".Constant", light.Constant);
            shader->SetFloat(base + ".Linear", light.Linear);
            shader->SetFloat(base + ".Quadratic", light.Quadratic);
        }
    }

    void Renderer3D::DrawCube(const glm::mat4& transform, std::shared_ptr<Material> material)
    {
        if (!material)
            material = Material::CreateDefault();

        auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRShader;
        activeShader->Bind();

        activeShader->SetMat4("u_Transform", transform);
        activeShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        activeShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);

        BindLightsToShader(activeShader);

        material->Bind(activeShader);

        if (material->MaterialShader)
            material->BindGraphUniforms(activeShader);

        RenderCommand::DrawIndexed(s_CubeMesh->GetVertexArray(), s_CubeMesh->GetIndexCount());
        
        Renderer::GetStats().DrawCalls++;
        Renderer::GetStats().MeshCount++;
        Renderer::GetStats().Vertices += s_CubeMesh->GetVertexCount();
        Renderer::GetStats().Triangles += s_CubeMesh->GetIndexCount() / 3;
    }

    void Renderer3D::DrawSphere(const glm::mat4& transform, std::shared_ptr<Material> material)
    {
        if (!material)
            material = Material::CreateDefault();

        auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRShader;
        activeShader->Bind();

        activeShader->SetMat4("u_Transform", transform);
        activeShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        activeShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);

        BindLightsToShader(activeShader);

        material->Bind(activeShader);

        if (material->MaterialShader)
            material->BindGraphUniforms(activeShader);

        RenderCommand::DrawIndexed(s_SphereMesh->GetVertexArray(), s_SphereMesh->GetIndexCount());
        
        Renderer::GetStats().DrawCalls++;
        Renderer::GetStats().MeshCount++;
        Renderer::GetStats().Vertices += s_SphereMesh->GetVertexCount();
        Renderer::GetStats().Triangles += s_SphereMesh->GetIndexCount() / 3;
    }

    void Renderer3D::DrawPlane(const glm::mat4& transform, std::shared_ptr<Material> material)
    {
        if (!material)
            material = Material::CreateDefault();

        auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRShader;
        activeShader->Bind();

        activeShader->SetMat4("u_Transform", transform);
        activeShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        activeShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);

        BindLightsToShader(activeShader);

        material->Bind(activeShader);

        if (material->MaterialShader)
            material->BindGraphUniforms(activeShader);

        RenderCommand::DrawIndexed(s_PlaneMesh->GetVertexArray(), s_PlaneMesh->GetIndexCount());
        
        Renderer::GetStats().DrawCalls++;
        Renderer::GetStats().MeshCount++;
        Renderer::GetStats().Vertices += s_PlaneMesh->GetVertexCount();
        Renderer::GetStats().Triangles += s_PlaneMesh->GetIndexCount() / 3;
    }

    void Renderer3D::DrawCylinder(const glm::mat4& transform, std::shared_ptr<Material> material)
    {
        if (!material)
            material = Material::CreateDefault();

        auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRShader;
        activeShader->Bind();

        activeShader->SetMat4("u_Transform", transform);
        activeShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        activeShader->SetFloat3("u_CameraPos", s_SceneData->CameraPosition);

        BindLightsToShader(activeShader);

        material->Bind(activeShader);

        if (material->MaterialShader)
            material->BindGraphUniforms(activeShader);

        RenderCommand::DrawIndexed(s_CylinderMesh->GetVertexArray(), s_CylinderMesh->GetIndexCount());
        
        Renderer::GetStats().DrawCalls++;
        Renderer::GetStats().MeshCount++;
        Renderer::GetStats().Vertices += s_CylinderMesh->GetVertexCount();
        Renderer::GetStats().Triangles += s_CylinderMesh->GetIndexCount() / 3;
    }

    void Renderer3D::DrawModel(const glm::mat4& transform, std::shared_ptr<Model> model)
    {
        if (!model)
            return;

        s_PBRShader->Bind();
        s_PBRShader->SetMat4("u_Transform", transform);

        // Light'ları bind et
        BindLightsToShader(s_PBRShader);

        for (size_t i = 0; i < model->Meshes.size(); i++)
        {
            auto& mesh = model->Meshes[i];
            auto material = (i < model->Materials.size()) ? model->Materials[i] : Material::CreateDefault();

            auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRShader;
            if (activeShader != s_PBRShader)
            {
                activeShader->Bind();
                activeShader->SetMat4("u_Transform", transform);
                BindLightsToShader(activeShader);
            }

            material->Bind(activeShader);
            if (material->MaterialShader)
                material->BindGraphUniforms(activeShader);

            RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
            
            // Stats güncelle
            Renderer::GetStats().DrawCalls++;
            Renderer::GetStats().MeshCount++;
            Renderer::GetStats().Vertices += mesh->GetVertexCount();
            Renderer::GetStats().Triangles += mesh->GetIndexCount() / 3;
        }
    }

    void Renderer3D::DrawSkinnedModel(const glm::mat4& transform, std::shared_ptr<Model> model,
        const std::vector<glm::mat4>& boneMatrices)
    {
        if (!model || !model->IsSkinned || !model->SkeletonData || model->SkinnedMeshes.empty())
            return;

        const uint32_t boneCount = model->SkeletonData->GetBoneCount();
        if (boneCount == 0)
            return;

        constexpr uint32_t kMaxBones = 128;
        const uint32_t palette = std::min(boneCount, kMaxBones);
        s_BoneMatrixScratch.resize(palette);

        if (boneMatrices.size() >= boneCount)
        {
            for (uint32_t i = 0; i < palette; ++i)
                s_BoneMatrixScratch[i] = boneMatrices[i];
        }
        else
        {
            for (uint32_t i = 0; i < palette; ++i)
                s_BoneMatrixScratch[i] = glm::mat4(1.f);
        }

        s_PBRSkinnedShader->Bind();
        s_PBRSkinnedShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_PBRSkinnedShader->SetMat4("u_Transform", transform);
        s_PBRSkinnedShader->SetMat4Array("u_BoneTransforms", s_BoneMatrixScratch.data(), palette);

        BindLightsToShader(s_PBRSkinnedShader);

        for (size_t i = 0; i < model->SkinnedMeshes.size(); ++i)
        {
            auto& mesh = model->SkinnedMeshes[i];
            auto material = (i < model->Materials.size()) ? model->Materials[i] : Material::CreateDefault();

            auto activeShader = material->MaterialShader ? material->MaterialShader : s_PBRSkinnedShader;
            if (activeShader != s_PBRSkinnedShader)
            {
                activeShader->Bind();
                activeShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
                activeShader->SetMat4("u_Transform", transform);
                BindLightsToShader(activeShader);
            }

            material->Bind(activeShader);
            if (material->MaterialShader)
                material->BindGraphUniforms(activeShader);

            RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());

            Renderer::GetStats().DrawCalls++;
            Renderer::GetStats().MeshCount++;
            Renderer::GetStats().Vertices += mesh->GetVertexCount();
            Renderer::GetStats().Triangles += mesh->GetIndexCount() / 3;
        }
    }

    void Renderer3D::DrawLightGizmo(const glm::vec3& position, const glm::vec3& color, float size)
    {
        // Transform oluştur
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::scale(transform, glm::vec3(size));

        // Unlit shader kullan
        s_UnlitShader->Bind();
        s_UnlitShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_UnlitShader->SetMat4("u_Transform", transform);
        s_UnlitShader->SetFloat3("u_Color", color);
        s_UnlitShader->SetInt("u_UseTexture", 0);

        // Sphere çiz
        RenderCommand::DrawIndexed(s_SphereMesh->GetVertexArray(), s_SphereMesh->GetIndexCount());
        
        // Stats güncelle
        Renderer::GetStats().DrawCalls++;
    }

    void Renderer3D::DrawSpotLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size)
    {
        // Rotation hesapla (direction vektörüne göre)
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 dir = glm::normalize(direction);
        
        glm::mat4 rotation;
        if (glm::abs(glm::dot(dir, up)) > 0.999f)
        {
            rotation = glm::mat4(1.0f);
        }
        else
        {
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            glm::vec3 newUp = glm::cross(dir, right);
            rotation = glm::mat4(
                glm::vec4(right, 0.0f),
                glm::vec4(dir, 0.0f),
                glm::vec4(newUp, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * rotation;
        transform = glm::scale(transform, glm::vec3(size));

        s_UnlitShader->Bind();
        s_UnlitShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_UnlitShader->SetMat4("u_Transform", transform);
        s_UnlitShader->SetFloat3("u_Color", color);
        s_UnlitShader->SetInt("u_UseTexture", 0);

        // Cone çiz
        RenderCommand::DrawIndexed(s_ConeMesh->GetVertexArray(), s_ConeMesh->GetIndexCount());
        
        // Stats güncelle
        Renderer::GetStats().DrawCalls++;
    }

    void Renderer3D::DrawDirectionalLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size)
    {
        // Rotation hesapla
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 dir = glm::normalize(direction);
        
        glm::mat4 rotation;
        if (glm::abs(glm::dot(dir, up)) > 0.999f)
        {
            rotation = glm::mat4(1.0f);
        }
        else
        {
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            glm::vec3 newUp = glm::cross(dir, right);
            rotation = glm::mat4(
                glm::vec4(right, 0.0f),
                glm::vec4(dir, 0.0f),
                glm::vec4(newUp, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * rotation;
        transform = glm::scale(transform, glm::vec3(size));

        s_UnlitShader->Bind();
        s_UnlitShader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        s_UnlitShader->SetMat4("u_Transform", transform);
        s_UnlitShader->SetFloat3("u_Color", color);
        s_UnlitShader->SetInt("u_UseTexture", 0);

        // Arrow çiz
        RenderCommand::DrawIndexed(s_ArrowMesh->GetVertexArray(), s_ArrowMesh->GetIndexCount());
        
        // Stats güncelle
        Renderer::GetStats().DrawCalls++;
    }

    void Renderer3D::DrawSkybox(std::shared_ptr<Cubemap> skybox, float exposure, float rotation, const glm::vec3& tint)
    {
        if (!skybox)
            return;

        // Depth test: skybox en arkada çizilsin
        glDepthFunc(GL_LEQUAL);

        s_SkyboxShader->Bind();
        
        // View matrix'ten translation'ı kaldır
        glm::mat4 view = glm::mat4(glm::mat3(s_SceneData->ViewProjectionMatrix));
        
        // Projection matrix'i ayrı hesapla
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 1000.0f);
        
        s_SkyboxShader->SetMat4("u_View", view);
        s_SkyboxShader->SetMat4("u_Projection", projection);
        s_SkyboxShader->SetInt("u_Skybox", 0);
        s_SkyboxShader->SetFloat("u_Exposure", exposure);
        s_SkyboxShader->SetFloat("u_Gamma", 2.2f);
        s_SkyboxShader->SetFloat3("u_Tint", tint);
        s_SkyboxShader->SetFloat("u_Rotation", glm::radians(rotation));

        skybox->Bind(0);
        RenderCommand::DrawIndexed(s_CubeMesh->GetVertexArray(), s_CubeMesh->GetIndexCount());

        glDepthFunc(GL_LESS);
        
        // Stats güncelle
        Renderer::GetStats().DrawCalls++;
    }
}
