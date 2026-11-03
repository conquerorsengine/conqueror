#include "GodotNodes.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Signals.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct TRS
        {
            glm::vec3 Position{ 0.0f };
            glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 Scale{ 1.0f };
        };

        TRS Decompose(const glm::mat4& m)
        {
            TRS t;
            glm::vec3 skew;
            glm::vec4 perspective;
            if (!glm::decompose(m, t.Scale, t.Rotation, t.Position, skew, perspective))
            {
                t = TRS{};
                t.Position = glm::vec3(m[3]);
            }
            return t;
        }

        glm::mat4 Compose(const TRS& t)
        {
            return glm::translate(glm::mat4(1.0f), t.Position) * glm::mat4_cast(t.Rotation) * glm::scale(glm::mat4(1.0f), t.Scale);
        }

        void UpdateRemoteTransforms(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, RemoteTransformComponent>();
            for (auto e : view)
            {
                auto& rt = view.get<RemoteTransformComponent>(e);
                if (!rt.Target || !scene.IsEntityActive(e))
                    continue;
                Entity target = scene.FindEntityByUUID(rt.Target);
                if (!target || (entt::entity)target == e)
                    continue;
                auto& dst = target.GetComponent<TransformComponent>();
                if (!rt.UseGlobalCoordinates)
                {
                    const auto& src = view.get<TransformComponent>(e);
                    if (rt.UpdatePosition) dst.Position = src.Position;
                    if (rt.UpdateRotation) dst.Rotation = src.Rotation;
                    if (rt.UpdateScale) dst.Scale = src.Scale;
                    continue;
                }
                // Dünya uzayında eşle, sonra hedefin atasına göre yerel dönüşüme çevir
                const TRS source = Decompose(scene.ComputeWorldMatrix(e));
                TRS desired = Decompose(scene.ComputeWorldMatrix((entt::entity)target));
                if (rt.UpdatePosition) desired.Position = source.Position;
                if (rt.UpdateRotation) desired.Rotation = source.Rotation;
                if (rt.UpdateScale) desired.Scale = source.Scale;
                glm::mat4 local = Compose(desired);
                if (Entity parent = target.GetParent())
                    local = glm::inverse(scene.ComputeWorldMatrix((entt::entity)parent)) * local;
                const TRS l = Decompose(local);
                if (rt.UpdatePosition) dst.Position = l.Position;
                if (rt.UpdateRotation) dst.Rotation = glm::eulerAngles(l.Rotation);
                if (rt.UpdateScale) dst.Scale = l.Scale;
            }
        }

        bool CastRay(Scene& scene, entt::entity e, RayCastComponent& rc)
        {
            rc.Colliding = false;
            rc.ColliderEntity = 0xFFFFFFFFu;
            rc.Collider.clear();
            const glm::mat4 world = scene.ComputeWorldMatrix(e);
            const glm::vec3 from = glm::vec3(world[3]);
            const glm::vec3 to = glm::vec3(world * glm::vec4(rc.TargetPosition, 1.0f));
            const glm::vec3 delta = to - from;
            const float length = glm::length(delta);
            if (length < 1e-6f)
                return false;
            const glm::vec3 dir = delta / length;
            uint32_t ignoreA = 0xFFFFFFFFu, ignoreB = 0xFFFFFFFFu;
            if (rc.ExcludeParent)
            {
                ignoreA = (uint32_t)e;
                if (Entity parent = Entity{ e, &scene }.GetParent())
                    ignoreB = (uint32_t)(entt::entity)parent;
            }
            bool found = false;
            float best = 1e30f;
            if (rc.Space == RayCastSpace::Physics2D)
            {
                PhysicsWorld2D* physics = scene.GetPhysicsWorld2D();
                if (!physics)
                    return false;
                static thread_local std::vector<RaycastHit2D> s_Hits;
                s_Hits.clear();
                const glm::vec2 dir2 = glm::vec2(delta);
                const float len2 = glm::length(dir2);
                if (len2 < 1e-6f)
                    return false;
                physics->RaycastAll(glm::vec2(from), dir2 / len2, len2, s_Hits, rc.CollisionMask);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (h.IsTrigger && !rc.HitTriggers) || h.Distance >= best)
                        continue;
                    best = h.Distance;
                    found = true;
                    rc.CollisionPoint = glm::vec3(h.Point, from.z);
                    rc.CollisionNormal = glm::vec3(h.Normal, 0.0f);
                    rc.Distance = h.Distance;
                    rc.ColliderEntity = h.Entity;
                }
            }
            else
            {
                PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
                if (!physics)
                    return false;
                static thread_local std::vector<Physics3D::RaycastHit> s_Hits;
                s_Hits.clear();
                physics->RaycastAll(from, dir, length, s_Hits, rc.CollisionMask);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (h.IsTrigger && !rc.HitTriggers) || h.Distance >= best)
                        continue;
                    best = h.Distance;
                    found = true;
                    rc.CollisionPoint = h.Point;
                    rc.CollisionNormal = h.Normal;
                    rc.Distance = h.Distance;
                    rc.ColliderEntity = h.Entity;
                }
            }
            rc.Colliding = found;
            if (found)
            {
                const auto hitEntity = (entt::entity)rc.ColliderEntity;
                if (scene.m_Registry.valid(hitEntity) && scene.m_Registry.all_of<TagComponent>(hitEntity))
                    rc.Collider = scene.m_Registry.get<TagComponent>(hitEntity).Tag;
            }
            return found;
        }

        // Yerel sınır küresi (merkez, yarıçap)
        bool LocalBounds(Scene& scene, entt::entity e, const VisibleOnScreenNotifierComponent& n, glm::vec3& center, float& radius)
        {
            if (n.AutoBounds)
            {
                if (auto* mr = scene.m_Registry.try_get<MeshRendererComponent>(e); mr && !mr->IsModel())
                {
                    std::shared_ptr<Mesh> mesh = mr->CustomMesh;
                    if (!mesh)
                    {
                        switch (mr->Type)
                        {
                            case MeshType::Sphere:   mesh = Renderer3D::GetSphereMesh(); break;
                            case MeshType::Plane:    mesh = Renderer3D::GetPlaneMesh(); break;
                            case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                            default:                 mesh = Renderer3D::GetCubeMesh(); break;
                        }
                    }
                    if (mesh)
                    {
                        center = mesh->GetBoundsCenter();
                        radius = mesh->GetBoundsRadius();
                        return true;
                    }
                }
                if (auto* mc = scene.m_Registry.try_get<MeshRendererComponent>(e); mc && mc->ModelData && !mc->ModelData->Meshes.empty())
                {
                    glm::vec3 mn(1e30f), mx(-1e30f);
                    for (const auto& m : mc->ModelData->Meshes)
                    {
                        if (!m) continue;
                        mn = glm::min(mn, m->GetBoundsCenter() - glm::vec3(m->GetBoundsRadius()));
                        mx = glm::max(mx, m->GetBoundsCenter() + glm::vec3(m->GetBoundsRadius()));
                    }
                    if (mn.x <= mx.x)
                    {
                        center = (mn + mx) * 0.5f;
                        radius = glm::length(mx - mn) * 0.5f;
                        return true;
                    }
                }
                if (scene.m_Registry.all_of<SpriteRendererComponent>(e))
                {
                    center = glm::vec3(0.0f);
                    radius = 0.7072f;   // birim dörtgenin köşegeni / 2
                    return true;
                }
            }
            center = n.BoundsCenter;
            radius = glm::length(n.BoundsSize) * 0.5f;
            return true;
        }

        void UpdateNotifiers(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, VisibleOnScreenNotifierComponent>();
            if (view.begin() == view.end())
                return;
            Entity cameraEntity = scene.GetPrimaryCameraEntity();
            if (!cameraEntity || !cameraEntity.HasComponent<CameraComponent>())
                return;
            const glm::mat4 projection = cameraEntity.GetComponent<CameraComponent>().Camera.GetProjection();
            const glm::mat4 viewMatrix = glm::inverse(scene.ComputeWorldMatrix((entt::entity)cameraEntity));
            glm::vec4 planes[6];
            Renderer3D::ExtractFrustum(projection * viewMatrix, planes);

            std::vector<std::pair<uint32_t, bool>> changed;
            for (auto e : view)
            {
                auto& n = view.get<VisibleOnScreenNotifierComponent>(e);
                bool visible = false;
                if (scene.IsEntityActive(e))
                {
                    glm::vec3 center;
                    float radius = 0.5f;
                    LocalBounds(scene, e, n, center, radius);
                    const glm::mat4 world = scene.ComputeWorldMatrix(e);
                    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                    const glm::vec3 c = glm::vec3(world * glm::vec4(center, 1.0f));
                    const float r = radius * scale;
                    visible = true;
                    for (int p = 0; p < 6 && visible; ++p)
                        if (glm::dot(glm::vec3(planes[p]), c) + planes[p].w < -r)
                            visible = false;
                }
                if (!n.Initialized || visible != n.OnScreen)
                {
                    // İlk karede yalnızca görünürse bildir (Godot: başlangıçta ekrandaysa screen_entered gelir)
                    if (n.Initialized || visible)
                        changed.push_back({ (uint32_t)e, visible });
                    n.Initialized = true;
                    n.OnScreen = visible;
                }
            }
            // Betikler varlık silebilir: bildirimler toplu yapılır
            for (const auto& [id, visible] : changed)
            {
                if (!scene.m_Registry.valid((entt::entity)id))
                    continue;
                Signals::Emit(scene, id, visible ? "screen_entered" : "screen_exited", nullptr, 0);
                if (scene.m_Registry.valid((entt::entity)id))
                    scene.InvokeScriptMethod(id, visible ? "OnBecameVisible" : "OnBecameInvisible", nullptr, 0);
            }
        }

        void UpdateParallax(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<TransformComponent, Parallax2DComponent>();
            if (view.begin() == view.end())
                return;
            Entity cameraEntity = scene.GetPrimaryCameraEntity();
            if (!cameraEntity)
                return;
            const glm::vec2 camera = glm::vec2(scene.ComputeWorldMatrix((entt::entity)cameraEntity)[3]);
            for (auto e : view)
            {
                auto [tc, p] = view.get<TransformComponent, Parallax2DComponent>(e);
                if (!p.HasBase)
                {
                    p.HasBase = true;
                    p.BasePosition = tc.Position;
                    p.CameraStart = camera;
                    p.AutoOffset = glm::vec2(0.0f);
                }
                p.AutoOffset += p.Autoscroll * dt;
                // Kamera hareketinin (1 - ölçek) kadarı katmana eklenir: 0.5 => kameranın yarı hızında kayar
                glm::vec2 pos = glm::vec2(p.BasePosition) + (camera - p.CameraStart) * (glm::vec2(1.0f) - p.ScrollScale) + p.ScrollOffset + p.AutoOffset;
                for (int axis = 0; axis < 2; ++axis)
                {
                    const float size = p.RepeatSize[axis];
                    if (size > 0.0f)
                    {
                        // Katmanı kameraya en yakın tekrar konumuna kaydır (kopyalar RepeatSize aralığıyla dizilir)
                        const float rel = camera[axis] - pos[axis];
                        pos[axis] += std::round(rel / size) * size;
                    }
                }
                tc.Position.x = pos.x;
                tc.Position.y = pos.y;
            }
        }
    }

    namespace GodotNodes
    {
        void Update(Scene& scene, float dt, bool runtime)
        {
            UpdateRemoteTransforms(scene);
            if (!runtime)
                return;
            auto rays = scene.m_Registry.view<RayCastComponent>();
            for (auto e : rays)
            {
                auto& rc = rays.get<RayCastComponent>(e);
                if (rc.Enabled && scene.IsEntityActive(e))
                    CastRay(scene, e, rc);
                else
                    rc.Colliding = false;
            }
            UpdateParallax(scene, dt);
            UpdateNotifiers(scene);
        }

        bool ForceRayCast(Scene& scene, uint32_t entity)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<RayCastComponent>(e))
                return false;
            return CastRay(scene, e, scene.m_Registry.get<RayCastComponent>(e));
        }
    }

    void RegisterGodotNodeComponents()
    {
        {
            using C = RemoteTransformComponent;
            ComponentDef<C>("Remote Transform", "RemoteTransformComponent", "Transform", ICON_FA_ARROWS_TO_DOT)
                .Describe("Godot RemoteTransform: pushes this object's position / rotation / scale to another object every frame without "
                          "parenting it (e.g. a camera that follows a moving platform but not its rotation).")
                .Field("Target", "Target", &C::Target)
                .Field("UseGlobalCoordinates", "Use Global Coordinates", &C::UseGlobalCoordinates)
                .Field("UpdatePosition", "Update Position", &C::UpdatePosition)
                .Field("UpdateRotation", "Update Rotation", &C::UpdateRotation)
                .Field("UpdateScale", "Update Scale", &C::UpdateScale);
        }
        {
            using C = RayCastComponent;
            ComponentDef<C>("Ray Cast", "RayCastComponent", "Physics", ICON_FA_LOCATION_CROSSHAIRS)
                .Describe("Godot RayCast3D / RayCast2D: a ray from this object to Target Position (local) re-cast every frame. Read "
                          "Colliding / Collision Point / Normal / Collider from scripts (RayCastIsColliding, RayCastGetPoint ...).")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("Space", "Space", &C::Space).Options({ "3D Physics", "2D Physics" })
                .Field("TargetPosition", "Target Position", &C::TargetPosition)
                .Field("CollisionMask", "Collision Mask", &C::CollisionMask, FieldKind::LayerMask)
                .Field("ExcludeParent", "Exclude Parent", &C::ExcludeParent).Tooltip("Ignore this object's own collider and its parent's")
                .Field("HitTriggers", "Hit Triggers", &C::HitTriggers)
                .Field("Colliding", "Colliding", &C::Colliding).ReadOnly()
                .Field("CollisionPoint", "Collision Point", &C::CollisionPoint).ReadOnly()
                .Field("CollisionNormal", "Collision Normal", &C::CollisionNormal).ReadOnly()
                .Field("Distance", "Distance", &C::Distance).ReadOnly()
                .Field("Collider", "Collider", &C::Collider).ReadOnly();
        }
        {
            using C = VisibleOnScreenNotifierComponent;
            ComponentDef<C>("Visible On Screen Notifier", "VisibleOnScreenNotifierComponent", "Rendering", ICON_FA_EYE)
                .Describe("Godot VisibleOnScreenNotifier: emits screen_entered / screen_exited signals and calls OnBecameVisible / "
                          "OnBecameInvisible on this object's scripts when its bounds enter or leave the main camera's view.")
                .Field("AutoBounds", "Auto Bounds", &C::AutoBounds).Tooltip("Use the mesh / model / sprite bounds of this object")
                .Field("BoundsCenter", "Bounds Center", &C::BoundsCenter).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("BoundsSize", "Bounds Size", &C::BoundsSize).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("OnScreen", "On Screen", &C::OnScreen).ReadOnly();
        }
        {
            using C = Parallax2DComponent;
            ComponentDef<C>("Parallax 2D", "Parallax2DComponent", "Rendering", ICON_FA_LAYER_GROUP)
                .Describe("Godot Parallax2D: the layer scrolls at Scroll Scale times the camera movement (0.5 = half speed, far away). "
                          "Autoscroll moves it on its own (clouds); Repeat Size keeps copies laid out at that spacing endlessly around the camera.")
                .Field("ScrollScale", "Scroll Scale", &C::ScrollScale)
                .Field("ScrollOffset", "Scroll Offset", &C::ScrollOffset)
                .Field("Autoscroll", "Autoscroll", &C::Autoscroll)
                .Field("RepeatSize", "Repeat Size", &C::RepeatSize);
        }
    }
}
