#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"
#include "Material.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Cubemap.h"
#include "Scene/EditorCamera.h"
#include "Scene/Components.h"
#include "Renderer/Shadow/ShadowPass.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Conqueror
{
    class CQ_API Renderer3D
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(EditorCamera& camera);
        static void BeginScene(EditorCamera& camera, int lightingSource, 
                              const glm::vec3& ambientColor, const glm::vec3& skyColor, 
                              const glm::vec3& equatorColor, const glm::vec3& groundColor,
                              float ambientIntensity, bool fogEnabled, const glm::vec3& fogColor, 
                              float fogStart, float fogEnd);

        static void BeginScene(const SceneCamera& camera, const glm::mat4& transform);
        static void BeginScene(const SceneCamera& camera, const glm::mat4& transform, int lightingSource, 
                              const glm::vec3& ambientColor, const glm::vec3& skyColor, 
                              const glm::vec3& equatorColor, const glm::vec3& groundColor,
                              float ambientIntensity, bool fogEnabled, const glm::vec3& fogColor, 
                              float fogStart, float fogEnd);
        static void EndScene();

        // Light sistemi
        static void SetDirectionalLight(const DirectionalLightComponent& light);
        static void AddPointLight(const glm::vec3& position, const PointLightComponent& light);
        static void AddSpotLight(const glm::vec3& position, const SpotLightComponent& light);
        static void ClearLights();

        // Shadow sistemi
        static void ExecuteShadowPass(Scene* scene, const DirectionalLightComponent& dirLight,
                                      const glm::vec3& lightDirection);
        static ShadowPass& GetShadowPass() { return s_ShadowPass; }

        // Reflection Probe sistemi
        static void AddReflectionProbe(const glm::vec3& position, const glm::vec3& boxOffset,
                                       const glm::vec3& boxSize, std::shared_ptr<Cubemap> cubemap);
        static void ClearReflectionProbes();

        // Light Probe sistemi
        static void AddLightProbe(const glm::vec3& position, const glm::vec3& shR,
                                  const glm::vec3& shG, const glm::vec3& shB);
        static void ClearLightProbes();
        static void SetLightProbeEnabled(bool enabled);

        // Lightmap
        static void SetLightmap(std::shared_ptr<Texture2D> lightmap);

        // Cube çizme
        static void DrawCube(const glm::mat4& transform, std::shared_ptr<Material> material);

        // Primitive çizme
        static void DrawSphere(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawPlane(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawCylinder(const glm::mat4& transform, std::shared_ptr<Material> material);

        // Model çizme
        static void DrawModel(const glm::mat4& transform, std::shared_ptr<struct Model> model);
        static void DrawSkinnedModel(const glm::mat4& transform, std::shared_ptr<struct Model> model,
            const std::vector<glm::mat4>& boneMatrices);

        // Editor gizmo'ları (unlit, depth test disabled)
        static void DrawLightGizmo(const glm::vec3& position, const glm::vec3& color, float size = 0.2f);
        static void DrawSpotLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size = 0.2f);
        static void DrawDirectionalLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size = 0.3f);

        // Skybox
        static void DrawSkybox(std::shared_ptr<Cubemap> skybox, float exposure = 1.0f, float rotation = 0.0f, const glm::vec3& tint = glm::vec3(1.0f));

        // Mesh getters (shadow pass icin)
        static std::shared_ptr<Mesh> GetCubeMesh() { return s_CubeMesh; }
        static std::shared_ptr<Mesh> GetSphereMesh() { return s_SphereMesh; }
        static std::shared_ptr<Mesh> GetPlaneMesh() { return s_PlaneMesh; }
        static std::shared_ptr<Mesh> GetCylinderMesh() { return s_CylinderMesh; }

    private:
        struct SceneData
        {
            glm::mat4 ViewProjectionMatrix;
            glm::vec3 CameraPosition;
            
            // Light data
            DirectionalLightComponent DirectionalLight;
            std::vector<std::pair<glm::vec3, PointLightComponent>> PointLights;
            std::vector<std::pair<glm::vec3, SpotLightComponent>> SpotLights;

            // Reflection Probes
            struct ReflectionProbeData
            {
                glm::vec3 Position;
                glm::vec3 BoxOffset;
                glm::vec3 BoxSize;
                std::shared_ptr<class Cubemap> ProbeCubemap;
            };
            std::vector<ReflectionProbeData> ReflectionProbes;

            // Light Probes
            struct LightProbeData
            {
                glm::vec3 Position;
                glm::vec3 SH_R, SH_G, SH_B;
            };
            std::vector<LightProbeData> LightProbes;
            bool LightProbeEnabled = false;

            // Lightmap
            std::shared_ptr<Texture2D> Lightmap;
        };

        static void BindLightsToShader(std::shared_ptr<Shader> shader);
        static void BindReflectionProbesToShader(std::shared_ptr<Shader> shader);
        static void BindLightProbesToShader(std::shared_ptr<Shader> shader);

        static SceneData* s_SceneData;
        static ShadowPass s_ShadowPass;
        static std::shared_ptr<Mesh> s_CubeMesh;
        static std::shared_ptr<Mesh> s_PlaneMesh;
        static std::shared_ptr<Mesh> s_CylinderMesh;
        static std::shared_ptr<Shader> s_PBRShader;
        static std::shared_ptr<Shader> s_PBRSkinnedShader;
        static std::vector<glm::mat4> s_BoneMatrixScratch;
        
        // Gizmo resources
        static std::shared_ptr<Mesh> s_SphereMesh;
        static std::shared_ptr<Mesh> s_ConeMesh;
        static std::shared_ptr<Mesh> s_ArrowMesh;
        static std::shared_ptr<Shader> s_UnlitShader;

        // Skybox resources
        static std::shared_ptr<Shader> s_SkyboxShader;
    };
}
