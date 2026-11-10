#include "WorldActors.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/GameFramework.h"
#include "Scene/AbilitySystem.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scripting/ConquerorScript/CQSEngine.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        // Nokta, nesnenin yerel kutusunun (merkez 0, Size) içinde mi
        bool InsideBox(const glm::mat4& world, const glm::vec3& size, const glm::vec3& p)
        {
            const glm::vec3 l = glm::vec3(glm::inverse(world) * glm::vec4(p, 1.0f));
            return glm::all(glm::lessThanEqual(glm::abs(l), size * 0.5f));
        }

        void DrawBox(const glm::mat4& world, const glm::vec3& size, const glm::vec4& col)
        {
            DebugDraw::Box(world * glm::scale(glm::mat4(1.0f), size), col, 0.0f, false);
        }

        // Fiziği olan nesneler (Rigidbody / Character Controller)
        template<typename Fn>
        void ForEachMover(Scene& scene, Fn&& fn)
        {
            auto& reg = scene.m_Registry;
            for (auto e : reg.view<TransformComponent, RigidbodyComponent>())
                if (scene.IsEntityActive(e)) fn(e, true);
            for (auto e : reg.view<TransformComponent, CharacterControllerComponent>())
                if (scene.IsEntityActive(e) && !reg.all_of<RigidbodyComponent>(e)) fn(e, false);
        }

        void Fire(Scene& scene, entt::entity e, const RadialForceComponent& rf)
        {
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            if (!physics)
                return;
            const glm::vec3 c = glm::vec3(scene.ComputeWorldMatrix(e)[3]);
            for (auto b : scene.m_Registry.view<TransformComponent, RigidbodyComponent>())
            {
                if (b == e || !scene.IsEntityActive(b) || scene.m_Registry.get<RigidbodyComponent>(b).Type != RigidbodyComponent::BodyType::Dynamic)
                    continue;
                const glm::vec3 p = glm::vec3(scene.m_Registry.get<TransformComponent>(b).WorldTransform[3]);
                const glm::vec3 d = p - c;
                const float dist = glm::length(d);
                if (dist > rf.Radius || dist < 1e-4f)
                    continue;
                const float k = rf.Falloff == RadialFalloff::Linear ? 1.0f - dist / std::max(rf.Radius, 1e-4f) : 1.0f;
                Entity be{ b, &scene };
                if (rf.ImpulseVelChange)
                    physics->SetLinearVelocity(be, physics->GetLinearVelocity(be) + d / dist * rf.ImpulseStrength * k);
                else
                    physics->ApplyImpulse(be, d / dist * rf.ImpulseStrength * k);
            }
        }
    }

    namespace WorldActors
    {
        void ApplyEnvironment(const Scene& scene, SceneEnvironment& env)
        {
            auto& reg = const_cast<Scene&>(scene).m_Registry;
            auto active = [&](entt::entity e) { return const_cast<Scene&>(scene).IsEntityActive(e); };
            for (auto e : reg.view<TransformComponent, ExponentialHeightFogComponent>())
            {
                if (!active(e)) continue;
                const auto& f = reg.get<ExponentialHeightFogComponent>(e);
                const float y = reg.get<TransformComponent>(e).WorldTransform[3][1];
                auto& fog = env.Settings.Fog;
                fog.Enabled = true;
                fog.Mode = FogMode::Exponential;
                fog.Density = f.FogDensity;
                fog.HeightFalloff = f.HeightFalloff;
                fog.BaseHeight = y;
                fog.Color = f.InscatteringColor;
                fog.Start = f.StartDistance;
                auto& post = env.Settings.Post;
                if (f.VolumetricFog)
                {
                    post.VolumetricFog = true;
                    post.VolumetricFogDensity = f.FogDensity;
                    post.VolumetricFogHeightFalloff = f.HeightFalloff;
                    post.VolumetricFogBaseHeight = y;
                    post.VolumetricFogIntensity = f.ScatteringIntensity;
                    post.VolumetricFogAlbedo = f.Albedo;
                    post.VolumetricFogDistance = f.ViewDistance;
                }
                break;
            }
            for (auto e : reg.view<SkyAtmosphereComponent>())
            {
                if (!active(e)) continue;
                const auto& a = reg.get<SkyAtmosphereComponent>(e);
                auto& en = env.Settings.Environment;
                en.Sky = SkyMode::Atmosphere;
                en.AtmosphereRayleigh = a.RayleighScale;
                en.AtmosphereMie = a.MieScale;
                en.AtmosphereMieAnisotropy = a.MieAnisotropy;
                en.AtmosphereOzone = a.OzoneScale;
                en.AtmosphereSunDisk = a.SunDiskScale;
                en.AtmosphereIntensity = a.Intensity;
                en.AtmosphereGroundAlbedo = a.GroundAlbedo;
                break;
            }
            for (auto e : reg.view<VolumetricCloudComponent>())
            {
                if (!active(e)) continue;
                const auto& c = reg.get<VolumetricCloudComponent>(e);
                auto& post = env.Settings.Post;
                post.VolumetricClouds = true;
                post.CloudCoverage = c.Coverage;
                post.CloudDensity = c.Density;
                post.CloudBottom = c.LayerBottomAltitude;
                post.CloudThickness = c.LayerHeight;
                post.CloudScale = c.Scale;
                post.CloudWindSpeed = c.WindSpeed;
                post.CloudWindDirection = c.WindDirection;
                post.CloudColor = c.Color;
                break;
            }
            for (auto e : reg.view<SkyLightComponent>())
            {
                if (!active(e)) continue;
                const auto& s = reg.get<SkyLightComponent>(e);
                env.AmbientIntensity = s.Intensity;
                env.Settings.Environment.ReflectionIntensity = s.ReflectionIntensity;
                break;
            }
        }

        void BeginPlay(Scene& scene)
        {
            for (auto e : scene.m_Registry.view<RadialForceComponent>())
                if (scene.m_Registry.get<RadialForceComponent>(e).FireOnStart)
                    scene.m_Registry.get<RadialForceComponent>(e).FireRequested = true;
        }

        void FireImpulse(Scene& scene, uint32_t entity)
        {
            if (auto* rf = scene.m_Registry.try_get<RadialForceComponent>((entt::entity)entity))
                rf->FireRequested = true;
        }

        bool IsInWaterVolume(Scene& scene, const glm::vec3& point)
        {
            for (auto e : scene.m_Registry.view<TransformComponent, PhysicsVolumeComponent>())
            {
                const auto& v = scene.m_Registry.get<PhysicsVolumeComponent>(e);
                if (v.WaterVolume && scene.IsEntityActive(e) && InsideBox(scene.ComputeWorldMatrix(e), v.Size, point))
                    return true;
            }
            return false;
        }

        void Update(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();

            // Kill Z: oyuncu yeniden doğar (Game Mode), diğerleri silinir (Unreal FellOutOfWorld)
            std::vector<entt::entity> destroy;
            bool respawn = false;
            const uint32_t pawn = GameFramework::GetPawn(scene);
            for (auto v : reg.view<TransformComponent, KillZVolumeComponent>())
            {
                if (!scene.IsEntityActive(v)) continue;
                const glm::mat4 w = scene.ComputeWorldMatrix(v);
                const glm::vec3 size = reg.get<KillZVolumeComponent>(v).Size;
                ForEachMover(scene, [&](entt::entity e, bool) {
                    if (!InsideBox(w, size, glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3])))
                        return;
                    if ((uint32_t)e == pawn) respawn = true;
                    else if (std::find(destroy.begin(), destroy.end(), e) == destroy.end()) destroy.push_back(e);
                });
            }
            for (auto e : destroy)
            {
                scene.InvokeScriptMethod((uint32_t)e, "OnFellOutOfWorld", nullptr, 0);
                if (reg.valid(e))
                    scene.DestroyEntity(Entity{ e, &scene });
            }
            if (respawn)
                GameFramework::RestartPlayer(scene);

            // Pain Causing: aralıklı hasar (Ability System "Health" özniteliği + OnTakeDamage(miktar))
            for (auto v : reg.view<TransformComponent, PainCausingVolumeComponent>())
            {
                if (!scene.IsEntityActive(v)) continue;
                auto& pc = reg.get<PainCausingVolumeComponent>(v);
                if (!pc.PainCausing) continue;
                pc.Timer += dt;
                const float interval = std::max(pc.PainInterval, 0.01f);
                if (pc.Timer < interval) continue;
                pc.Timer -= interval;
                const float damage = pc.DamagePerSec * interval;
                const glm::mat4 w = scene.ComputeWorldMatrix(v);
                std::vector<entt::entity> hit;
                ForEachMover(scene, [&](entt::entity e, bool) {
                    if (InsideBox(w, pc.Size, glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]))) hit.push_back(e);
                });
                for (auto e : hit)
                {
                    Entity ent{ e, &scene };
                    if (AbilitySystem::FindAttribute(ent, "Health"))
                        AbilitySystem::SetAttributeBase(ent, "Health", AbilitySystem::GetAttributeBase(ent, "Health") - damage);
                    CQS::Value arg = CQS::Value::MakeFloat((double)damage);
                    scene.InvokeScriptMethod((uint32_t)e, "OnTakeDamage", &arg, 1);
                }
            }

            // Radial Force: istenen darbe (kare başına bir kez)
            if (physics)
                for (auto e : reg.view<TransformComponent, RadialForceComponent>())
                {
                    if (!scene.IsEntityActive(e)) continue;
                    auto& rf = reg.get<RadialForceComponent>(e);
                    if (rf.FireRequested)
                    {
                        rf.FireRequested = false;
                        Fire(scene, e, rf);
                    }
                }
        }

        void FixedUpdate(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            // Physics Volume: sıvı sürtünmesi, son hız, su hacminde kaldırma
            if (physics)
                for (auto v : reg.view<TransformComponent, PhysicsVolumeComponent>())
                {
                    if (!scene.IsEntityActive(v)) continue;
                    const auto& pv = reg.get<PhysicsVolumeComponent>(v);
                    const glm::mat4 w = scene.ComputeWorldMatrix(v);
                    for (auto e : reg.view<TransformComponent, RigidbodyComponent>())
                    {
                        if (!scene.IsEntityActive(e) || reg.get<RigidbodyComponent>(e).Type != RigidbodyComponent::BodyType::Dynamic) continue;
                        if (!InsideBox(w, pv.Size, glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]))) continue;
                        Entity be{ e, &scene };
                        glm::vec3 vel = physics->GetLinearVelocity(be) * std::max(0.0f, 1.0f - pv.FluidFriction * dt);
                        if (pv.WaterVolume && pv.Buoyancy != 0.0f)
                            vel.y += pv.Buoyancy * dt;
                        // Son hız adım sonundaki hıza uygulanır: bu adımda eklenecek yerçekimi hesaba katılır
                        const glm::vec3 g = reg.get<RigidbodyComponent>(e).UseGravity ? physics->GetGravity() * reg.get<RigidbodyComponent>(e).GravityScale : glm::vec3(0.0f);
                        glm::vec3 next = vel + g * dt;
                        const float sp = glm::length(next);
                        if (sp > pv.TerminalVelocity && sp > 1e-5f)
                            next *= pv.TerminalVelocity / sp;
                        physics->SetLinearVelocity(be, next - g * dt);
                    }
                }

            // Radial Force: sürekli kuvvet
            if (physics)
                for (auto e : reg.view<TransformComponent, RadialForceComponent>())
                {
                    if (!scene.IsEntityActive(e)) continue;
                    auto& rf = reg.get<RadialForceComponent>(e);
                    if (rf.ForceStrength == 0.0f) continue;
                    const glm::vec3 c = glm::vec3(scene.ComputeWorldMatrix(e)[3]);
                    for (auto b : reg.view<TransformComponent, RigidbodyComponent>())
                    {
                        if (b == e || !scene.IsEntityActive(b) || reg.get<RigidbodyComponent>(b).Type != RigidbodyComponent::BodyType::Dynamic) continue;
                        const glm::vec3 d = glm::vec3(reg.get<TransformComponent>(b).WorldTransform[3]) - c;
                        const float dist = glm::length(d);
                        if (dist > rf.Radius || dist < 1e-4f) continue;
                        const float k = rf.Falloff == RadialFalloff::Linear ? 1.0f - dist / std::max(rf.Radius, 1e-4f) : 1.0f;
                        physics->ApplyForce(Entity{ b, &scene }, d / dist * rf.ForceStrength * k);
                    }
                }
        }

        void ComputeCullHidden(Scene& scene, const glm::vec3& cam, std::unordered_set<uint32_t>& hidden)
        {
            auto& reg = scene.m_Registry;
            auto volumes = reg.view<TransformComponent, CullDistanceVolumeComponent>();
            if (volumes.begin() == volumes.end())
                return;
            for (auto e : reg.view<TransformComponent, MeshRendererComponent>())
            {
                const auto* tag = reg.try_get<TagComponent>(e);
                if (!tag || !tag->IsStatic || !scene.IsEntityActive(e))
                    continue;
                const auto& tc = reg.get<TransformComponent>(e);
                const glm::vec3 p = glm::vec3(tc.WorldTransform[3]);
                // Nesne boyu: ağ sınır yarıçapı x en büyük ölçek x 2
                const auto& mr = reg.get<MeshRendererComponent>(e);
                float radius = 1.0f;
                if (mr.IsModel())
                {
                    radius = 0.0f;
                    if (mr.ModelData)
                        for (const auto& m : mr.ModelData->Meshes)
                            if (m) radius = std::max(radius, m->GetBoundsRadius());
                }
                else if (mr.CustomMesh)
                    radius = mr.CustomMesh->GetBoundsRadius();
                const float scale = std::max({ glm::length(glm::vec3(tc.WorldTransform[0])), glm::length(glm::vec3(tc.WorldTransform[1])),
                                               glm::length(glm::vec3(tc.WorldTransform[2])) });
                const float size = 2.0f * radius * scale;
                const float dist = glm::distance(p, cam);
                for (auto v : volumes)
                {
                    if (!scene.IsEntityActive(v)) continue;
                    const auto& cv = reg.get<CullDistanceVolumeComponent>(v);
                    if (!InsideBox(scene.ComputeWorldMatrix(v), cv.Size, p)) continue;
                    // Unreal: nesne boyuna en yakın satırın uzaklığı
                    float best = -1.0f, bestDiff = 1e30f;
                    for (const auto& row : cv.CullDistances)
                    {
                        const float diff = std::abs(row.x - size);
                        if (diff < bestDiff) { bestDiff = diff; best = row.y; }
                    }
                    if (best > 0.0f && dist > best)
                        hidden.insert((uint32_t)e);
                    break;
                }
            }
        }

        void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection)
        {
            auto& reg = scene.m_Registry;
            for (auto e : reg.view<TransformComponent, ArrowComponent>())
            {
                const auto& a = reg.get<ArrowComponent>(e);
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                const glm::vec3 o = glm::vec3(w[3]), f = -glm::normalize(glm::vec3(w[2])) * a.Size;
                const glm::vec3 up = glm::normalize(glm::vec3(w[1])) * a.Size * 0.15f;
                const glm::vec3 side = glm::normalize(glm::vec3(w[0])) * a.Size * 0.15f;
                DebugDraw::Line(o, o + f, a.Color, 0.0f, false);
                DebugDraw::Line(o + f, o + f * 0.8f + up, a.Color, 0.0f, false);
                DebugDraw::Line(o + f, o + f * 0.8f - up, a.Color, 0.0f, false);
                DebugDraw::Line(o + f, o + f * 0.8f + side, a.Color, 0.0f, false);
                DebugDraw::Line(o + f, o + f * 0.8f - side, a.Color, 0.0f, false);
            }
            for (auto e : reg.view<TransformComponent, TargetPointComponent>())
                DebugDraw::Cross(glm::vec3(scene.ComputeWorldMatrix(e)[3]), reg.get<TargetPointComponent>(e).Size, glm::vec4(1.0f, 0.4f, 0.1f, 1.0f), 0.0f, false);
            for (auto e : reg.view<TransformComponent, NoteComponent>())
                DebugDraw::Text3D(glm::vec3(scene.ComputeWorldMatrix(e)[3]), reg.get<NoteComponent>(e).Text, glm::vec4(1.0f, 0.95f, 0.5f, 1.0f), 1.0f, 0.0f, false);
            for (uint32_t id : selection)
            {
                const entt::entity e = (entt::entity)id;
                if (!reg.valid(e)) continue;
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                if (const auto* k = reg.try_get<KillZVolumeComponent>(e)) DrawBox(w, k->Size, glm::vec4(1.0f, 0.25f, 0.25f, 1.0f));
                if (const auto* p = reg.try_get<PainCausingVolumeComponent>(e)) DrawBox(w, p->Size, glm::vec4(1.0f, 0.55f, 0.1f, 1.0f));
                if (const auto* p = reg.try_get<PhysicsVolumeComponent>(e)) DrawBox(w, p->Size, glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));
                if (const auto* c = reg.try_get<CullDistanceVolumeComponent>(e)) DrawBox(w, c->Size, glm::vec4(0.8f, 0.8f, 0.3f, 1.0f));
                if (const auto* r = reg.try_get<RadialForceComponent>(e)) DebugDraw::Sphere(glm::vec3(w[3]), r->Radius, glm::vec4(0.9f, 0.5f, 1.0f, 1.0f), 0.0f, false);
            }
        }
    }

    template<> void Scene::OnComponentAdded<ExponentialHeightFogComponent>(Entity, ExponentialHeightFogComponent&) {}
    template<> void Scene::OnComponentAdded<SkyAtmosphereComponent>(Entity, SkyAtmosphereComponent&) {}
    template<> void Scene::OnComponentAdded<VolumetricCloudComponent>(Entity, VolumetricCloudComponent&) {}
    template<> void Scene::OnComponentAdded<SkyLightComponent>(Entity, SkyLightComponent&) {}
    template<> void Scene::OnComponentAdded<KillZVolumeComponent>(Entity, KillZVolumeComponent&) {}
    template<> void Scene::OnComponentAdded<PainCausingVolumeComponent>(Entity, PainCausingVolumeComponent&) {}
    template<> void Scene::OnComponentAdded<PhysicsVolumeComponent>(Entity, PhysicsVolumeComponent&) {}
    template<> void Scene::OnComponentAdded<CullDistanceVolumeComponent>(Entity, CullDistanceVolumeComponent&) {}
    template<> void Scene::OnComponentAdded<RadialForceComponent>(Entity, RadialForceComponent&) {}
    template<> void Scene::OnComponentAdded<ArrowComponent>(Entity, ArrowComponent&) {}
    template<> void Scene::OnComponentAdded<NoteComponent>(Entity, NoteComponent&) {}
    template<> void Scene::OnComponentAdded<TargetPointComponent>(Entity, TargetPointComponent&) {}

    void RegisterWorldActorComponents()
    {
        const glm::vec4 kEnv{ 0.55f, 0.8f, 1.0f, 1.0f }, kVol{ 1.0f, 0.6f, 0.4f, 1.0f };
        {
            using C = ExponentialHeightFogComponent;
            ComponentDef<C>("Exponential Height Fog", "ExponentialHeightFogComponent", "Lighting", ICON_FA_SMOG).Color(kEnv)
                .Describe("Fog that thins out with height (Unreal Exponential Height Fog): the fog starts at this object's height. Replaces the Lighting window fog while in the scene.")
                .Field("FogDensity", "Fog Density", &C::FogDensity).Range(0.0f, 1.0f, 0.001f)
                .Field("HeightFalloff", "Fog Height Falloff", &C::HeightFalloff).Range(0.0f, 10.0f, 0.01f)
                .Field("InscatteringColor", "Fog Inscattering Color", &C::InscatteringColor, FieldKind::Color3)
                .Field("StartDistance", "Start Distance", &C::StartDistance).Range(0.0f, 100000.0f, 0.5f)
                .Field("VolumetricFog", "Volumetric Fog", &C::VolumetricFog).Header("Volumetric Fog")
                .Field("ScatteringIntensity", "Scattering Intensity", &C::ScatteringIntensity).Range(0.0f, 20.0f, 0.05f)
                .Field("Albedo", "Albedo", &C::Albedo, FieldKind::Color3)
                .Field("ViewDistance", "View Distance", &C::ViewDistance).Range(1.0f, 2000.0f, 1.0f);
        }
        {
            using C = SkyAtmosphereComponent;
            ComponentDef<C>("Sky Atmosphere", "SkyAtmosphereComponent", "Lighting", ICON_FA_CLOUD_SUN).Color(kEnv)
                .Describe("Physically based sky (Rayleigh + Mie + ozone) lit by the Directional Light: sunsets and twilight follow the sun angle.")
                .Field("RayleighScale", "Rayleigh Scattering Scale", &C::RayleighScale).Range(0.0f, 10.0f, 0.01f)
                .Field("MieScale", "Mie Scattering Scale", &C::MieScale).Range(0.0f, 10.0f, 0.01f)
                .Field("MieAnisotropy", "Mie Anisotropy", &C::MieAnisotropy).Range(0.0f, 0.999f, 0.01f)
                .Field("OzoneScale", "Ozone Absorption Scale", &C::OzoneScale).Range(0.0f, 10.0f, 0.01f)
                .Field("SunDiskScale", "Sun Disk Scale", &C::SunDiskScale).Range(0.0f, 10.0f, 0.01f)
                .Field("Intensity", "Sky Luminance Factor", &C::Intensity).Range(0.0f, 20.0f, 0.01f)
                .Field("GroundAlbedo", "Ground Albedo", &C::GroundAlbedo, FieldKind::Color3);
        }
        {
            using C = VolumetricCloudComponent;
            ComponentDef<C>("Volumetric Cloud", "VolumetricCloudComponent", "Lighting", ICON_FA_CLOUD).Color(kEnv)
                .Describe("Ray-marched cloud layer drifting with the wind, lit by the sun.")
                .Field("Coverage", "Coverage", &C::Coverage).Range(0.0f, 1.0f, 0.01f)
                .Field("Density", "Density", &C::Density).Range(0.0f, 10.0f, 0.01f)
                .Field("LayerBottomAltitude", "Layer Bottom Altitude", &C::LayerBottomAltitude).Range(0.0f, 20000.0f, 10.0f)
                .Field("LayerHeight", "Layer Height", &C::LayerHeight).Range(10.0f, 20000.0f, 10.0f)
                .Field("Scale", "Shape Scale", &C::Scale).Range(0.05f, 20.0f, 0.01f)
                .Field("WindSpeed", "Wind Speed", &C::WindSpeed).Range(0.0f, 200.0f, 0.1f)
                .Field("WindDirection", "Wind Direction", &C::WindDirection).Range(-360.0f, 360.0f, 1.0f)
                .Field("Color", "Albedo", &C::Color, FieldKind::Color3);
        }
        {
            using C = SkyLightComponent;
            ComponentDef<C>("Sky Light", "SkyLightComponent", "Lighting", ICON_FA_SUN).Color(kEnv)
                .Describe("Light from the sky: sets the strength of the ambient light and reflections that come from the sky.")
                .Field("Intensity", "Intensity Scale", &C::Intensity).Range(0.0f, 20.0f, 0.01f)
                .Field("ReflectionIntensity", "Reflection Intensity", &C::ReflectionIntensity).Range(0.0f, 20.0f, 0.01f);
        }
        {
            using C = KillZVolumeComponent;
            ComponentDef<C>("Kill Z Volume", "KillZVolumeComponent", "Volumes", ICON_FA_SKULL).Color(kVol)
                .Describe("Objects with physics that enter this box fall out of the world: the player respawns at a Player Start (Game Mode), "
                          "other objects are destroyed (scripts get OnFellOutOfWorld first).")
                .Field("Size", "Size", &C::Size).Speed(0.1f);
        }
        {
            using C = PainCausingVolumeComponent;
            ComponentDef<C>("Pain Causing Volume", "PainCausingVolumeComponent", "Volumes", ICON_FA_FIRE_FLAME_CURVED).Color(kVol)
                .Describe("Hurts what stands inside: every Pain Interval seconds lowers the Ability System Health attribute and calls OnTakeDamage(amount).")
                .Field("Size", "Size", &C::Size).Speed(0.05f)
                .Field("PainCausing", "Pain Causing", &C::PainCausing)
                .Field("DamagePerSec", "Damage Per Sec", &C::DamagePerSec).Range(0.0f, 100000.0f, 0.5f)
                .Field("PainInterval", "Pain Interval", &C::PainInterval).Range(0.01f, 60.0f, 0.01f)
                .Field("DamageType", "Damage Type", &C::DamageType);
        }
        {
            using C = PhysicsVolumeComponent;
            ComponentDef<C>("Physics Volume", "PhysicsVolumeComponent", "Volumes", ICON_FA_WATER).Color(kVol)
                .Describe("Changes physics inside: fluid friction slows bodies, speed is capped at Terminal Velocity; Water Volume pushes bodies up "
                          "by Buoyancy and scripts can ask IsInWaterVolume.")
                .Field("Size", "Size", &C::Size).Speed(0.05f)
                .Field("FluidFriction", "Fluid Friction", &C::FluidFriction).Range(0.0f, 100.0f, 0.01f)
                .Field("TerminalVelocity", "Terminal Velocity", &C::TerminalVelocity).Range(0.0f, 10000.0f, 0.5f)
                .Field("WaterVolume", "Water Volume", &C::WaterVolume)
                .Field("Buoyancy", "Buoyancy", &C::Buoyancy).Range(-100.0f, 100.0f, 0.1f).VisibleIf([](const C& c) { return c.WaterVolume; });
        }
        {
            using C = CullDistanceVolumeComponent;
            ComponentDef<C>("Cull Distance Volume", "CullDistanceVolumeComponent", "Volumes", ICON_FA_EYE_LOW_VISION).Color(kVol)
                .Describe("Hides small static objects inside this box when the camera is far away: each row pairs an object size with the distance "
                          "beyond which objects of about that size are not drawn (0 = always drawn).")
                .Field("Size", "Size", &C::Size).Speed(0.1f)
                .Field("CullDistances", "Cull Distances (Size, Distance)", &C::CullDistances).Speed(0.5f);
        }
        {
            using C = RadialForceComponent;
            ComponentDef<C>("Radial Force", "RadialForceComponent", "Physics", ICON_FA_BURST).Color({ 0.45f, 0.85f, 0.55f, 1.0f })
                .Describe("Pushes physics objects away from this point: a constant force, or an impulse when fired (Fire Impulse button, "
                          "Fire On Start, or from scripts).")
                .Field("Radius", "Radius", &C::Radius).Range(0.01f, 10000.0f, 0.05f)
                .Field("Falloff", "Falloff", &C::Falloff).Options({ "Constant", "Linear" })
                .Field("ImpulseStrength", "Impulse Strength", &C::ImpulseStrength).Range(-1.0e6f, 1.0e6f, 0.5f)
                .Field("ImpulseVelChange", "Impulse Vel Change", &C::ImpulseVelChange).Tooltip("Change velocity directly, ignoring mass.")
                .Field("ForceStrength", "Force Strength", &C::ForceStrength).Range(-1.0e6f, 1.0e6f, 0.5f)
                .Field("FireOnStart", "Fire On Start", &C::FireOnStart)
                .Action("Fire Impulse", [](Entity, C& c) { c.FireRequested = true; });
        }
        {
            using C = ArrowComponent;
            ComponentDef<C>("Arrow", "ArrowComponent", "Rendering", ICON_FA_ARROW_RIGHT).Color(kEnv)
                .Describe("An arrow in the editor showing this object's forward direction (spawn points, launchers).")
                .Field("Color", "Arrow Color", &C::Color, FieldKind::Color4)
                .Field("Size", "Arrow Size", &C::Size).Range(0.01f, 100.0f, 0.01f);
        }
        {
            using C = NoteComponent;
            ComponentDef<C>("Note", "NoteComponent", "Rendering", ICON_FA_NOTE_STICKY).Color(kEnv)
                .Describe("A text note shown in the editor at this object's position (for the team; not in the game).")
                .Field("Text", "Text", &C::Text);
        }
        {
            using C = TargetPointComponent;
            ComponentDef<C>("Target Point", "TargetPointComponent", "Gameplay", ICON_FA_CROSSHAIRS).Color(kEnv)
                .Describe("A marked position for scripts and AI (spawn spots, waypoints). Find it with FindEntity or by tag.")
                .Field("Size", "Gizmo Size", &C::Size).Range(0.01f, 100.0f, 0.01f);
        }
    }
}
