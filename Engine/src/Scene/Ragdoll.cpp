#include "Ragdoll.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Animation/Animator.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    struct RagdollRuntime
    {
        struct Bone
        {
            int Node = -1;
            uint64_t Body = 0;
            glm::mat4 Offset{ 1.0f };     // gövde -> kemik (etkinleştirme anında)
        };
        std::vector<Bone> Bones;
        std::vector<int> NodeToBone;      // düğüm -> Bones indeksi (-1: üst kemiği izler)
        std::vector<glm::mat4> LocalAct;  // etkinleştirme anındaki yerel düğüm dönüşümleri
    };

    namespace
    {
        glm::mat4 Compose(const BonePose& p)
        {
            return glm::translate(glm::mat4(1.0f), p.Translation) * glm::mat4_cast(p.Rotation) * glm::scale(glm::mat4(1.0f), p.Scale);
        }

        BonePose Decompose(const glm::mat4& m)
        {
            BonePose p;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(m, p.Scale, p.Rotation, p.Translation, skew, perspective);
            p.Rotation = glm::normalize(p.Rotation);
            return p;
        }

        const Model* SkinnedModel(Entity e)
        {
            if (!e.HasComponent<ModelComponent>())
                return nullptr;
            const auto& model = e.GetComponent<ModelComponent>().ModelData;
            return model && model->IsSkinned && model->SkeletonData ? model.get() : nullptr;
        }

        // Son animasyon pozu (yoksa bağlama pozu)
        void CurrentPose(Entity e, const Skeleton& skeleton, Pose& out)
        {
            if (e.HasComponent<AnimationComponent>() && !e.GetComponent<AnimationComponent>().FinalPose.empty())
                out = e.GetComponent<AnimationComponent>().FinalPose;
            else if (e.HasComponent<AnimatorComponent>() && !e.GetComponent<AnimatorComponent>().FinalPose.empty())
                out = e.GetComponent<AnimatorComponent>().FinalPose;
            else
                Animator::BindPose(skeleton, out);
        }

        glm::mat4 BodyWorld(Entity body)
        {
            const auto& tc = body.GetComponent<TransformComponent>();
            return glm::translate(glm::mat4(1.0f), tc.Position) * glm::toMat4(glm::quat(tc.Rotation));
        }
    }

    namespace Ragdoll
    {
        bool Activate(Scene& scene, uint32_t entityId, const glm::vec3& impulse, const glm::vec3& point, bool wholeBody)
        {
            auto& reg = scene.m_Registry;
            const entt::entity h = (entt::entity)entityId;
            if (!reg.valid(h))
                return false;
            Entity e{ h, &scene };
            const Model* model = SkinnedModel(e);
            if (!model)
            {
                CQ_CORE_WARN("[Ragdoll] '{0}' has no skinned model", e.GetComponent<TagComponent>().Tag);
                return false;
            }
            if (!e.HasComponent<RagdollComponent>())
                e.AddComponent<RagdollComponent>();
            auto& rc = e.GetComponent<RagdollComponent>();
            if (rc.Simulating)
                return true;
            const Skeleton& skeleton = *model->SkeletonData;
            const auto& nodes = Animator::Flatten(skeleton);
            if (nodes.empty())
                return false;
            Pose pose;
            CurrentPose(e, skeleton, pose);
            if (pose.size() < nodes.size())
                return false;

            // Model uzayı küresel dönüşümler ve dünya konumları
            std::vector<glm::mat4> global(nodes.size()), local(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                local[i] = Compose(pose[i]);
                global[i] = nodes[i].Parent >= 0 ? global[(size_t)nodes[i].Parent] * local[i] : local[i];
            }
            const glm::mat4 modelToWorld = e.GetComponent<TransformComponent>().WorldTransform * skeleton.GlobalInverseRoot;
            std::vector<glm::vec3> world(nodes.size());
            glm::vec3 bmin(1e30f), bmax(-1e30f);
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                world[i] = glm::vec3(modelToWorld * global[i][3]);
                if (nodes[i].BoneIndex >= 0)
                {
                    bmin = glm::min(bmin, world[i]);
                    bmax = glm::max(bmax, world[i]);
                }
            }
            const float size = std::max(glm::length(bmax - bmin), 1e-3f);
            std::vector<std::vector<int>> children(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i].Parent >= 0)
                    children[(size_t)nodes[i].Parent].push_back((int)i);

            // Aday kemikler: çocuklarının ortalamasına uzanan, yeterince uzun bölütler
            struct Candidate { int Node; glm::vec3 Start, End; float Length; };
            std::vector<Candidate> candidates;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                if (nodes[i].BoneIndex < 0 || children[i].empty())
                    continue;
                glm::vec3 end(0.0f);
                int count = 0;
                for (int c : children[i])
                    if (nodes[(size_t)c].BoneIndex >= 0) { end += world[(size_t)c]; ++count; }
                if (!count)
                    continue;
                end /= (float)count;
                const float length = glm::distance(world[i], end);
                if (length >= std::max(rc.MinBoneLength, 0.0f) * size)
                    candidates.push_back({ (int)i, world[i], end, length });
            }
            if ((int)candidates.size() > std::max(rc.MaxBodies, 2))
            {
                std::vector<Candidate> sorted = candidates;
                std::sort(sorted.begin(), sorted.end(), [](const Candidate& a, const Candidate& b) { return a.Length > b.Length; });
                sorted.resize((size_t)std::max(rc.MaxBodies, 2));
                std::sort(sorted.begin(), sorted.end(), [](const Candidate& a, const Candidate& b) { return a.Node < b.Node; });
                candidates = std::move(sorted);
            }
            if (candidates.size() < 2)
            {
                CQ_CORE_WARN("[Ragdoll] '{0}': not enough bones for a ragdoll", e.GetComponent<TagComponent>().Tag);
                return false;
            }

            auto runtime = std::make_shared<RagdollRuntime>();
            runtime->NodeToBone.assign(nodes.size(), -1);
            runtime->LocalAct = local;
            float totalWeight = 0.0f;
            for (const auto& c : candidates)
            {
                const float r = std::max(c.Length * rc.RadiusScale, size * 0.01f);
                totalWeight += c.Length * r * r;
            }
            const std::string base = e.GetComponent<TagComponent>().Tag + " Ragdoll";
            std::vector<Entity> bodies;
            for (const auto& c : candidates)
            {
                const glm::vec3 dir = glm::normalize(c.End - c.Start);
                const glm::vec3 mid = (c.Start + c.End) * 0.5f;
                const float r = std::max(c.Length * rc.RadiusScale, size * 0.01f);
                glm::quat q = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir);
                if (glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f)) < -0.9999f)
                    q = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));

                Entity body = scene.CreateEntity(base);
                auto& tc = body.GetComponent<TransformComponent>();
                tc.Position = mid;
                tc.Rotation = glm::eulerAngles(q);
                auto& rb = body.AddComponent<RigidbodyComponent>();
                rb.Type = RigidbodyComponent::BodyType::Dynamic;
                rb.Mass = std::max(rc.TotalMass * (c.Length * r * r) / std::max(totalWeight, 1e-9f), 0.05f);
                rb.LinearDrag = rc.LinearDamping;
                rb.AngularDrag = 0.5f;
                rb.RuntimeBody = nullptr;
                auto& cap = body.AddComponent<CapsuleColliderComponent>();
                cap.Radius = r;
                cap.Height = std::max(c.Length, r * 2.0f + 0.001f);
                cap.Direction = 1;

                // Üst gövde: en yakın seçili ata
                int parentBone = -1;
                for (int p = nodes[(size_t)c.Node].Parent; p >= 0; p = nodes[(size_t)p].Parent)
                    if (runtime->NodeToBone[(size_t)p] >= 0) { parentBone = runtime->NodeToBone[(size_t)p]; break; }
                if (parentBone >= 0)
                {
                    auto& j = body.AddComponent<CharacterJointComponent>();
                    j.ConnectedBody = runtime->Bones[(size_t)parentBone].Body;
                    j.Anchor = glm::vec3(0.0f, -c.Length * 0.5f, 0.0f);
                    j.Axis = glm::vec3(0.0f, 1.0f, 0.0f);
                    j.SwingAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                    j.LowTwistLimit = -std::abs(rc.TwistLimit);
                    j.HighTwistLimit = std::abs(rc.TwistLimit);
                    j.Swing1Limit = std::abs(rc.SwingLimit);
                    j.Swing2Limit = std::abs(rc.SwingLimit);
                    j.EnableCollision = false;
                }
                RagdollRuntime::Bone bone;
                bone.Node = c.Node;
                bone.Body = (uint64_t)body.GetComponent<IDComponent>().ID;
                const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), mid) * glm::toMat4(q);
                bone.Offset = glm::inverse(bodyWorld) * (modelToWorld * global[(size_t)c.Node]);
                runtime->NodeToBone[(size_t)c.Node] = (int)runtime->Bones.size();
                runtime->Bones.push_back(bone);
                bodies.push_back(body);
            }
            scene.UpdateWorldTransforms();
            if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D())
            {
                for (Entity b : bodies)
                    physics->CreateBody(b);
                for (Entity b : bodies)
                    physics->MarkDirty(b);
                if (glm::dot(impulse, impulse) > 0.0f && wholeBody)
                {
                    float total = 0.0f;
                    for (Entity b : bodies)
                        total += b.HasComponent<RigidbodyComponent>() ? std::max(b.GetComponent<RigidbodyComponent>().Mass, 0.0f) : 0.0f;
                    for (Entity b : bodies)
                    {
                        const float m = b.HasComponent<RigidbodyComponent>() ? std::max(b.GetComponent<RigidbodyComponent>().Mass, 0.0f) : 0.0f;
                        if (total > 0.0f && m > 0.0f)
                            physics->ApplyImpulse(b, impulse * (m / total));
                    }
                }
                else if (glm::dot(impulse, impulse) > 0.0f)
                {
                    Entity nearest = bodies.front();
                    float best = 1e30f;
                    for (Entity b : bodies)
                    {
                        const float d = glm::distance(b.GetComponent<TransformComponent>().Position, point);
                        if (d < best) { best = d; nearest = b; }
                    }
                    physics->ApplyImpulse(nearest, impulse);
                }
            }
            rc.Runtime = runtime;
            rc.Simulating = true;
            rc.BodyCount = (int)bodies.size();
            CQ_CORE_INFO("[Ragdoll] '{0}' activated with {1} bodies", e.GetComponent<TagComponent>().Tag, bodies.size());
            return true;
        }

        void Deactivate(Scene& scene, uint32_t entityId)
        {
            auto& reg = scene.m_Registry;
            const entt::entity h = (entt::entity)entityId;
            if (!reg.valid(h) || !reg.all_of<RagdollComponent>(h))
                return;
            auto& rc = reg.get<RagdollComponent>(h);
            if (rc.Runtime)
                for (const auto& b : rc.Runtime->Bones)
                    if (Entity body = scene.FindEntityByUUID(b.Body))
                        scene.DestroyEntity(body);
            rc.Runtime.reset();
            rc.Simulating = false;
            rc.BodyCount = 0;
        }

        bool ApplyPose(Scene& scene, uint32_t entityId, std::vector<glm::mat4>& outSkinning)
        {
            auto& reg = scene.m_Registry;
            const entt::entity h = (entt::entity)entityId;
            if (!reg.valid(h) || !reg.all_of<RagdollComponent>(h))
                return false;
            auto& rc = reg.get<RagdollComponent>(h);
            if (!rc.Simulating || !rc.Runtime)
                return false;
            Entity e{ h, &scene };
            const Model* model = SkinnedModel(e);
            if (!model)
                return false;
            const Skeleton& skeleton = *model->SkeletonData;
            const auto& nodes = Animator::Flatten(skeleton);
            const RagdollRuntime& rt = *rc.Runtime;
            if (rt.LocalAct.size() != nodes.size())
                return false;
            const glm::mat4 worldToModel = glm::inverse(e.GetComponent<TransformComponent>().WorldTransform * skeleton.GlobalInverseRoot);
            static thread_local std::vector<glm::mat4> s_Global;
            static thread_local Pose s_Pose;
            s_Global.resize(nodes.size());
            s_Pose.resize(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const int parent = nodes[i].Parent;
                const int b = rt.NodeToBone[i];
                Entity body = b >= 0 ? scene.FindEntityByUUID(rt.Bones[(size_t)b].Body) : Entity{};
                if (body)
                    s_Global[i] = worldToModel * BodyWorld(body) * rt.Bones[(size_t)b].Offset;
                else
                    s_Global[i] = parent >= 0 ? s_Global[(size_t)parent] * rt.LocalAct[i] : rt.LocalAct[i];
                const glm::mat4 localM = parent >= 0 ? glm::inverse(s_Global[(size_t)parent]) * s_Global[i] : s_Global[i];
                s_Pose[i] = body ? Decompose(localM) : Decompose(rt.LocalAct[i]);
            }
            Animator::PoseToSkinning(skeleton, s_Pose, outSkinning);
            return true;
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<RagdollComponent>();
            std::vector<entt::entity> start;
            for (auto h : view)
            {
                auto& rc = view.get<RagdollComponent>(h);
                if (rc.StartActive && !rc.Simulating && !rc.Runtime && scene.IsRuntimeScene())
                    start.push_back(h);
            }
            for (auto h : start)
            {
                Activate(scene, (uint32_t)h);
                // Başarısızsa bir daha denenmesin
                if (scene.m_Registry.valid(h) && !scene.m_Registry.get<RagdollComponent>(h).Simulating)
                    scene.m_Registry.get<RagdollComponent>(h).StartActive = false;
            }
        }
    }

    void RegisterRagdollComponents()
    {
        using C = RagdollComponent;
        ComponentDef<C>("Ragdoll", "RagdollComponent", "Physics", ICON_FA_PERSON_FALLING)
            .Describe("Builds a physics ragdoll from the skinned model's skeleton (capsule per long bone, character joints with swing/twist limits), "
                      "like Unreal's Physics Asset. RagdollActivate switches from animation to physics; RagdollDeactivate goes back.")
            .Field("StartActive", "Start Active", &C::StartActive)
            .Field("TotalMass", "Total Mass", &C::TotalMass).Range(0.1f, 10000.0f, 0.5f)
            .Field("RadiusScale", "Radius Scale", &C::RadiusScale).Range(0.02f, 1.0f, 0.01f)
            .Field("MinBoneLength", "Min Bone Length", &C::MinBoneLength).Range(0.0f, 0.5f, 0.005f).Tooltip("Relative to the skeleton size")
            .Field("MaxBodies", "Max Bodies", &C::MaxBodies).Range(2.0f, 64.0f, 1.0f)
            .Field("SwingLimit", "Swing Limit", &C::SwingLimit).Range(0.0f, 180.0f, 1.0f)
            .Field("TwistLimit", "Twist Limit", &C::TwistLimit).Range(0.0f, 180.0f, 1.0f)
            .Field("LinearDamping", "Linear Damping", &C::LinearDamping).Range(0.0f, 10.0f, 0.01f)
            .Field("Simulating", "Simulating", &C::Simulating).ReadOnly()
            .Field("BodyCount", "Bodies", &C::BodyCount).ReadOnly();
    }
}
