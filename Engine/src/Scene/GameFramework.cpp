#include "GameFramework.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/RigConstraints.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Logging/Log.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace Conqueror
{
    namespace
    {
        constexpr uint32_t kNull = 0xFFFFFFFFu;
        bool s_DeferSpawnEvents = false;

        glm::mat4 WorldMatrix(Entity e)
        {
            glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
            int guard = 0;
            for (Entity p = e.GetParent(); p && guard < 256; p = p.GetParent(), ++guard)
                m = p.GetComponent<TransformComponent>().GetTransform() * m;
            return m;
        }

        Entity Root(Entity e)
        {
            Entity r = e;
            int guard = 0;
            for (Entity p = e.GetParent(); p && guard < 256; p = p.GetParent(), ++guard)
                r = p;
            return r;
        }

        Entity GameModeEntity(Scene& scene)
        {
            auto view = scene.m_Registry.view<GameModeComponent>();
            for (auto h : view)
                if (scene.IsEntityActive(h))
                    return Entity{ h, &scene };
            return {};
        }

        // Pawn'ın (kendisi ya da çocuklarından) ilk kamerasını birincil yap
        void Possess(Scene& scene, Entity pawn)
        {
            Entity cam;
            std::vector<Entity> stack{ pawn };
            while (!stack.empty() && !cam)
            {
                Entity e = stack.back();
                stack.pop_back();
                if (e.HasComponent<CameraComponent>()) { cam = e; break; }
                for (Entity c : e.GetChildren()) stack.push_back(c);
            }
            if (!cam)
                return;
            auto view = scene.m_Registry.view<CameraComponent>();
            for (auto h : view)
                view.get<CameraComponent>(h).Primary = (h == (entt::entity)cam);
        }
    }

    namespace GameFramework
    {
        uint32_t FindPlayerStart(Scene& scene, const std::string& startTag)
        {
            Entity gm = GameModeEntity(scene);
            GameModeComponent* mode = gm ? &gm.GetComponent<GameModeComponent>() : nullptr;
            std::vector<entt::entity> starts;
            auto view = scene.m_Registry.view<PlayerStartComponent>();
            for (auto h : view)
            {
                auto& ps = view.get<PlayerStartComponent>(h);
                if (!ps.Enabled || !scene.IsEntityActive(h))
                    continue;
                if (!startTag.empty() && ps.StartTag != startTag)
                    continue;
                starts.push_back(h);
            }
            if (starts.empty())
                return kNull;
            // Kararlı sıra (sahnedeki ad sırası değil, oluşturulma sırası)
            std::sort(starts.begin(), starts.end());
            size_t pick = 0;
            if (mode && mode->ChooseStart == PlayerStartChoice::Random)
            {
                static std::mt19937 rng{ 1234u };
                pick = std::uniform_int_distribution<size_t>(0, starts.size() - 1)(rng);
            }
            else if (mode && mode->ChooseStart == PlayerStartChoice::RoundRobin)
                pick = (size_t)(mode->NextStart++) % starts.size();
            return (uint32_t)starts[pick];
        }

        uint32_t GetPawn(Scene& scene)
        {
            Entity gm = GameModeEntity(scene);
            if (!gm)
                return kNull;
            uint32_t pawn = gm.GetComponent<GameModeComponent>().Pawn;
            return pawn != kNull && scene.m_Registry.valid((entt::entity)pawn) ? pawn : kNull;
        }

        void SendSpawnEvents(Scene& scene, Entity gm, uint32_t pawn)
        {
            if (pawn == kNull || !scene.m_Registry.valid((entt::entity)pawn))
                return;
            CQS::Value arg = CQS::Value::MakeInt((int64_t)pawn);
            scene.InvokeScriptMethod((uint32_t)(entt::entity)gm, "OnPlayerSpawned", &arg, 1);
            if (scene.m_Registry.valid((entt::entity)pawn))
                scene.InvokeScriptMethod(pawn, "OnPossessed", &arg, 1);
        }

        uint32_t RestartPlayer(Scene& scene, const std::string& startTag)
        {
            Entity gm = GameModeEntity(scene);
            if (!gm)
                return kNull;
            auto& mode = gm.GetComponent<GameModeComponent>();
            if (mode.DefaultPawn.empty())
                return kNull;
            if (mode.Pawn != kNull && scene.m_Registry.valid((entt::entity)mode.Pawn))
                scene.DestroyEntity(Entity{ (entt::entity)mode.Pawn, &scene });
            mode.Pawn = kNull;

            // Doğuş noktası: Player Start (etiket filtreli) ya da Game Mode'un kendi konumu
            const std::string tag = startTag.empty() ? mode.PlayerStartTag : startTag;
            uint32_t start = FindPlayerStart(scene, tag);
            Entity at = start != kNull ? Entity{ (entt::entity)start, &scene } : gm;
            glm::mat4 world = WorldMatrix(at);
            glm::vec3 position = glm::vec3(world[3]);
            glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
            float yaw = glm::degrees(std::atan2(-forward.x, -forward.z));

            Entity pawn = PrefabSerializer::Instantiate(scene, mode.DefaultPawn);
            if (!pawn)
            {
                CQ_CORE_ERROR("[GameMode] Cannot spawn default pawn {0}", mode.DefaultPawn);
                return kNull;
            }
            auto& tc = pawn.GetComponent<TransformComponent>();
            tc.Position = position;
            tc.Rotation = glm::vec3(0.0f, glm::radians(yaw), 0.0f);
            mode.Pawn = (uint32_t)(entt::entity)pawn;
            mode.Spawns++;
            mode.RespawnTimer = -1.0f;
            if (mode.PossessCamera)
                Possess(scene, pawn);
            // Oyun başında betikler henüz başlamadı: olaylar ilk karede (Update) gönderilir
            if (s_DeferSpawnEvents)
                mode.PendingSpawnEvents = true;
            else
                SendSpawnEvents(scene, gm, mode.Pawn);
            return mode.Pawn;
        }

        void ScheduleRespawn(Scene& scene, float delay)
        {
            Entity gm = GameModeEntity(scene);
            if (!gm)
                return;
            auto& mode = gm.GetComponent<GameModeComponent>();
            mode.RespawnTimer = delay >= 0.0f ? delay : mode.RespawnDelay;
        }

        void BeginPlay(Scene& scene)
        {
            Entity gm = GameModeEntity(scene);
            if (!gm)
                return;
            auto& mode = gm.GetComponent<GameModeComponent>();
            mode.Pawn = kNull;
            mode.Spawns = 0;
            mode.RespawnTimer = -1.0f;
            mode.NextStart = 0;
            for (auto h : scene.m_Registry.view<SpringArmComponent>())
                scene.m_Registry.get<SpringArmComponent>(h).HasLagged = false;
            mode.PendingSpawnEvents = false;
            if (mode.SpawnOnStart && !mode.DefaultPawn.empty())
            {
                s_DeferSpawnEvents = true;
                RestartPlayer(scene, mode.PlayerStartTag);
                s_DeferSpawnEvents = false;
            }
        }

        void Update(Scene& scene, float dt)
        {
            Entity gm = GameModeEntity(scene);
            if (!gm)
                return;
            auto& mode = gm.GetComponent<GameModeComponent>();
            if (mode.PendingSpawnEvents)
            {
                mode.PendingSpawnEvents = false;
                SendSpawnEvents(scene, gm, mode.Pawn);
                if (!scene.m_Registry.valid((entt::entity)gm))
                    return;
            }
            if (mode.RespawnTimer >= 0.0f)
            {
                mode.RespawnTimer -= dt;
                if (mode.RespawnTimer < 0.0f)
                    RestartPlayer(scene, mode.PlayerStartTag);
            }
        }

        void LateUpdate(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<SpringArmComponent>();
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            for (auto h : view)
            {
                if (!scene.IsEntityActive(h))
                    continue;
                Entity arm{ h, &scene };
                auto& sa = view.get<SpringArmComponent>(h);
                glm::mat4 world = WorldMatrix(arm);
                glm::vec3 origin = glm::vec3(world[3]) + sa.TargetOffset;
                glm::quat rot;
                if (sa.UseControlRotation)
                    rot = glm::quat(glm::vec3(glm::radians(sa.ControlPitch), glm::radians(sa.ControlYaw), 0.0f));
                else
                    rot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])), glm::normalize(glm::vec3(world[2]))));
                glm::vec3 back = rot * glm::vec3(0.0f, 0.0f, 1.0f);
                float length = std::max(0.0f, sa.TargetArmLength);
                sa.Colliding = false;
                if (sa.DoCollisionTest && physics && length > 0.0f)
                {
                    Physics3D::RaycastHit hit;
                    const uint32_t owner = (uint32_t)(entt::entity)Root(arm);
                    if (physics->SphereCast(origin, std::max(0.01f, sa.ProbeSize), back, length, hit, 0xFFFFFFFFu, owner) && !hit.IsTrigger)
                    {
                        length = std::max(0.0f, hit.Distance);
                        sa.Colliding = true;
                    }
                }
                sa.CurrentLength = length;
                glm::vec3 socket = origin + back * length + rot * sa.SocketOffset;
                if (sa.EnableCameraLag && sa.HasLagged && !sa.Colliding)
                {
                    float k = 1.0f - std::exp(-std::max(0.0f, sa.CameraLagSpeed) * dt);
                    socket = glm::mix(sa.LaggedSocket, socket, k);
                }
                sa.LaggedSocket = socket;
                sa.HasLagged = true;
                // Çocukları kolun ucuna yerleştir (yerel uzayda), kontrol dönüşünde kameraya yön ver
                glm::mat4 inv = glm::inverse(world);
                glm::vec3 local = glm::vec3(inv * glm::vec4(socket, 1.0f));
                for (Entity child : arm.GetChildren())
                {
                    auto& tc = child.GetComponent<TransformComponent>();
                    tc.Position = local;
                    if (sa.UseControlRotation)
                    {
                        // Dünya dönüşü = kontrol dönüşü: üst dönüşü telafi et
                        glm::quat parentRot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])), glm::normalize(glm::vec3(world[2]))));
                        tc.Rotation = glm::eulerAngles(glm::inverse(parentRot) * rot);
                    }
                }
            }
        }
    }

    namespace GameFramework
    {
        void UpdateBoneAttachments(Scene& scene)
        {
            auto view = scene.m_Registry.view<BoneAttachmentComponent, TransformComponent>();
            bool any = false;
            for (auto h : view)
            {
                Entity e{ h, &scene };
                const auto& ba = view.get<BoneAttachmentComponent>(h);
                // İskeletli model: en yakın ata (ya da varlığın kendisi değil)
                Entity model;
                for (Entity p = e.GetParent(); p; p = p.GetParent())
                    if (p.HasComponent<MeshRendererComponent>() && p.GetComponent<MeshRendererComponent>().IsModel())
                    {
                        model = p;
                        break;
                    }
                glm::mat4 bone;
                if (!model || !RigConstraints::GetBoneWorldTransform(scene, (uint32_t)(entt::entity)model, ba.Bone, bone))
                    continue;
                // Kemik matrisi modelin ölçeğini içerir: yönü ve konumu al, ölçeği varlığın kendisinden
                glm::vec3 pos = glm::vec3(bone[3]);
                glm::quat rot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(bone[0])), glm::normalize(glm::vec3(bone[1])), glm::normalize(glm::vec3(bone[2]))));
                rot = rot * glm::quat(glm::radians(ba.RotationOffset));
                pos += rot * ba.PositionOffset * glm::length(glm::vec3(bone[0]));
                Entity parent = e.GetParent();
                glm::mat4 parentWorld = parent ? WorldMatrix(parent) : glm::mat4(1.0f);
                auto& tc = view.get<TransformComponent>(h);
                glm::mat4 local = glm::inverse(parentWorld) * (glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot));
                glm::quat pr = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(parentWorld[0])), glm::normalize(glm::vec3(parentWorld[1])), glm::normalize(glm::vec3(parentWorld[2]))));
                tc.Position = glm::vec3(local[3]);
                tc.Rotation = glm::eulerAngles(glm::inverse(pr) * rot);
                any = true;
            }
            if (any)
                scene.UpdateWorldTransforms();
        }
    }

    void RegisterGameFrameworkComponents()
    {
        ComponentDef<BoneAttachmentComponent>("Bone Attachment", "BoneAttachmentComponent", "Animation", ICON_FA_LINK)
            .Describe("Socket: keeps this entity on a bone of the skinned model above it (weapons, props, hats). Updated after animation every frame.")
            .Field("Bone", "Bone", &BoneAttachmentComponent::Bone)
            .Field("PositionOffset", "Position Offset", &BoneAttachmentComponent::PositionOffset)
            .Field("RotationOffset", "Rotation Offset", &BoneAttachmentComponent::RotationOffset);
        ComponentDef<PlayerStartComponent>("Player Start", "PlayerStartComponent", "Gameplay", ICON_FA_PERSON_RAYS)
            .Describe("Where the Game Mode spawns the player. Facing direction = the object's forward (-Z).")
            .Field("StartTag", "Start Tag", &PlayerStartComponent::StartTag).Tooltip("Optional name so the Game Mode (or RestartPlayer) can pick this start")
            .Field("Enabled", "Enabled", &PlayerStartComponent::Enabled);
        using G = GameModeComponent;
        ComponentDef<G>("Game Mode", "GameModeComponent", "Gameplay", ICON_FA_CHESS)
            .Describe("Spawns the Default Pawn prefab at a Player Start when play begins and makes its camera primary. Scripts: GameModeRestartPlayer, GameModeRespawn, GameModeGetPawn; message OnPlayerSpawned(pawn).")
            .Field("DefaultPawn", "Default Pawn", &G::DefaultPawn, FieldKind::Asset).Filter(".cqprefab")
            .Field("PlayerStartTag", "Player Start Tag", &G::PlayerStartTag)
            .Field("ChooseStart", "Choose Start", &G::ChooseStart).Options({ "First", "Random", "Round Robin" })
            .Field("SpawnOnStart", "Spawn On Start", &G::SpawnOnStart)
            .Field("PossessCamera", "Use Pawn Camera", &G::PossessCamera)
            .Field("RespawnDelay", "Respawn Delay", &G::RespawnDelay).Range(0.0f, 60.0f, 0.05f)
            .Field("Spawns", "Spawns", &G::Spawns).ReadOnly();
        using S = SpringArmComponent;
        ComponentDef<S>("Spring Arm", "SpringArmComponent", "Camera", ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE)
            .Describe("Camera boom (Unreal Spring Arm): keeps its children (the camera) TargetArmLength behind it, pulls in when something blocks the view and can lag behind smoothly.")
            .Field("TargetArmLength", "Target Arm Length", &S::TargetArmLength).Range(0.0f, 100.0f, 0.05f)
            .Field("SocketOffset", "Socket Offset", &S::SocketOffset)
            .Field("TargetOffset", "Target Offset", &S::TargetOffset)
            .Field("DoCollisionTest", "Do Collision Test", &S::DoCollisionTest).Header("Collision")
            .Field("ProbeSize", "Probe Size", &S::ProbeSize).Range(0.0f, 2.0f, 0.01f)
            .Field("EnableCameraLag", "Enable Camera Lag", &S::EnableCameraLag).Header("Lag")
            .Field("CameraLagSpeed", "Camera Lag Speed", &S::CameraLagSpeed).Range(0.0f, 100.0f, 0.1f)
            .Field("UseControlRotation", "Use Control Rotation", &S::UseControlRotation).Header("Rotation")
            .Field("ControlYaw", "Control Yaw", &S::ControlYaw)
            .Field("ControlPitch", "Control Pitch", &S::ControlPitch)
            .Field("CurrentLength", "Current Length", &S::CurrentLength).ReadOnly()
            .Field("Colliding", "Colliding", &S::Colliding).ReadOnly();
    }
}
