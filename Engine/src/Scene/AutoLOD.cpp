#include "AutoLOD.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/MeshLOD.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Tiering/QualitySettings.h"

#include <IconsFontAwesome6.h>

#include <algorithm>

namespace Conqueror
{
    namespace
    {
        MeshLOD::Settings SettingsOf(const AutoLODComponent& c)
        {
            MeshLOD::Settings s;
            s.Levels = std::clamp(c.Levels, 1, 4);
            s.Reduction = std::clamp(c.Reduction, 0.05f, 0.95f);
            s.ScreenSizes = { c.ScreenSize1, c.ScreenSize2, c.ScreenSize3, c.ScreenSize4 };
            return s;
        }

        void FillInfo(AutoLODComponent& c, const Model& model)
        {
            int tris[5] = {};
            bool pending = false;
            for (const auto& mesh : model.Meshes)
            {
                if (!mesh)
                    continue;
                pending = pending || MeshLOD::IsPending(mesh.get());
                for (int l = 0; l < 5; ++l)
                    tris[l] += (int)(mesh->GetLOD(l)->GetIndexCount() / 3);
            }
            c.Triangles0 = tris[0];
            c.Triangles1 = c.Levels >= 1 ? tris[1] : 0;
            c.Triangles2 = c.Levels >= 2 ? tris[2] : 0;
            c.Triangles3 = c.Levels >= 3 ? tris[3] : 0;
            c.Triangles4 = c.Levels >= 4 ? tris[4] : 0;
            c.Pending = pending;
        }
    }

    namespace AutoLOD
    {
        void Update(Scene& scene)
        {
            MeshLOD::Update();
            auto view = scene.m_Registry.view<AutoLODComponent, MeshRendererComponent>();
            for (auto h : view)
            {
                auto& c = view.get<AutoLODComponent>(h);
                auto& mc = view.get<MeshRendererComponent>(h);
                if (!mc.ModelData)
                    continue;
                const MeshLOD::Settings settings = SettingsOf(c);
                const uint64_t signature = MeshLOD::Signature(settings);
                for (const auto& mesh : mc.ModelData->Meshes)
                    if (mesh && mesh->GetLODSignature() != signature)
                        MeshLOD::RequestAsync(mesh, settings);
                FillInfo(c, *mc.ModelData);
                // Görünen seviye (ilk mesh): birincil kameraya göre; kamera yoksa r.ForceLOD / son görünüm
                if (!mc.ModelData->Meshes.empty() && mc.ModelData->Meshes[0])
                {
                    const Mesh& mesh = *mc.ModelData->Meshes[0];
                    const glm::mat4& world = scene.m_Registry.get<TransformComponent>(h).WorldTransform;
                    RenderCamera camera;
                    if (Renderer3D::GetForcedLOD() < 0 && mesh.HasLODs() && scene.GetRuntimeRenderCamera(16, 9, camera))
                    {
                        const glm::vec3 center = glm::vec3(world * glm::vec4(mesh.GetBoundsCenter(), 1.0f));
                        const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                        float screen = mesh.GetBoundsRadius() * scale * std::abs(camera.Projection[1][1]);
                        if (!camera.Orthographic)
                            screen /= std::max(glm::distance(center, camera.Position), 1e-3f);
                        c.CurrentLOD = mesh.SelectLOD(screen * std::max(QualitySettings::GetPreset().LODBias, 0.01f));
                    }
                    else
                        c.CurrentLOD = Renderer3D::SelectMeshLOD(mesh, world);
                }
            }
        }

        int GenerateNow(Scene& scene, uint32_t entity)
        {
            entt::entity h = (entt::entity)entity;
            if (!scene.m_Registry.valid(h) || !scene.m_Registry.all_of<MeshRendererComponent>(h))
                return 0;
            auto& mc = scene.m_Registry.get<MeshRendererComponent>(h);
            if (!mc.ModelData)
                return 0;
            if (!scene.m_Registry.all_of<AutoLODComponent>(h))
                scene.m_Registry.emplace<AutoLODComponent>(h);
            auto& c = scene.m_Registry.get<AutoLODComponent>(h);
            const MeshLOD::Settings settings = SettingsOf(c);
            int levels = 0;
            for (const auto& mesh : mc.ModelData->Meshes)
                if (mesh)
                    levels = std::max(levels, MeshLOD::Generate(mesh, settings));
            FillInfo(c, *mc.ModelData);
            return levels;
        }
    }

    void RegisterAutoLODComponents()
    {
        using C = AutoLODComponent;
        ComponentDef<C>("Auto LOD", "AutoLODComponent", "Rendering", ICON_FA_LAYER_GROUP)
            .Describe("Generates simplified levels of detail for this model's meshes (quadric edge collapse) and switches them by screen size, "
                      "like Unreal's Static Mesh LOD settings. LODs belong to the mesh asset: every object using the model shares them. "
                      "r.ForceLOD forces a level for debugging.")
            .Field("Levels", "LOD Count", &C::Levels).Range(1.0f, 4.0f, 1.0f).Tooltip("Number of extra levels")
            .Field("Reduction", "Reduction", &C::Reduction).Range(0.05f, 0.95f, 0.01f).Tooltip("Triangles kept per level (0.5 = half of the previous level)")
            .Field("ScreenSize1", "LOD1 Screen Size", &C::ScreenSize1).Range(0.0f, 2.0f, 0.005f)
            .Field("ScreenSize2", "LOD2 Screen Size", &C::ScreenSize2).Range(0.0f, 2.0f, 0.005f)
            .Field("ScreenSize3", "LOD3 Screen Size", &C::ScreenSize3).Range(0.0f, 2.0f, 0.005f)
            .Field("ScreenSize4", "LOD4 Screen Size", &C::ScreenSize4).Range(0.0f, 2.0f, 0.005f)
            .Field("Triangles0", "LOD0 Triangles", &C::Triangles0).ReadOnly()
            .Field("Triangles1", "LOD1 Triangles", &C::Triangles1).ReadOnly()
            .Field("Triangles2", "LOD2 Triangles", &C::Triangles2).ReadOnly()
            .Field("Triangles3", "LOD3 Triangles", &C::Triangles3).ReadOnly()
            .Field("Triangles4", "LOD4 Triangles", &C::Triangles4).ReadOnly()
            .Field("CurrentLOD", "Current LOD", &C::CurrentLOD).ReadOnly()
            .Field("Pending", "Generating", &C::Pending).ReadOnly();
    }
}
