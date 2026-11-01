#include "RigConstraints.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Animation/Skeleton.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct ConstraintRef
        {
            entt::entity E;
            float Weight;      // kısıt ağırlığı x üstteki Rig ağırlıkları
        };
        std::unordered_map<uint32_t, std::vector<ConstraintRef>> s_ByOwner;
        float s_Dt = 0.0f;

        glm::mat4 Compose(const BonePose& p)
        {
            return glm::translate(glm::mat4(1.f), p.Translation) * glm::mat4_cast(glm::normalize(p.Rotation)) * glm::scale(glm::mat4(1.f), p.Scale);
        }

        glm::quat RotationOf(const glm::mat4& m)
        {
            glm::mat3 r(m);
            for (int i = 0; i < 3; ++i)
            {
                float l = glm::length(r[i]);
                if (l > 1e-8f) r[i] /= l;
            }
            return glm::normalize(glm::quat_cast(r));
        }

        glm::vec3 AxisVector(RigAxis a)
        {
            switch (a)
            {
                case RigAxis::X: return { 1, 0, 0 };
                case RigAxis::NegX: return { -1, 0, 0 };
                case RigAxis::Y: return { 0, 1, 0 };
                case RigAxis::NegY: return { 0, -1, 0 };
                case RigAxis::NegZ: return { 0, 0, -1 };
                default: return { 0, 0, 1 };
            }
        }

        // Pozun düğüm uzayı küresel dönüşümleri; bir kemik değişince kendisi ve sonrakiler yeniden hesaplanır
        struct Solver
        {
            const std::vector<FlatAnimNode>& Nodes;
            Pose& P;
            std::vector<glm::mat4> G;

            Solver(const std::vector<FlatAnimNode>& nodes, Pose& pose) : Nodes(nodes), P(pose)
            {
                G.resize(nodes.size());
                Recompute(0);
            }

            void Recompute(size_t from)
            {
                for (size_t i = from; i < Nodes.size(); ++i)
                {
                    glm::mat4 local = Compose(P[i]);
                    G[i] = Nodes[i].Parent >= 0 ? G[(size_t)Nodes[i].Parent] * local : local;
                }
            }

            glm::vec3 Pos(int i) const { return glm::vec3(G[(size_t)i][3]); }
            glm::quat Rot(int i) const { return RotationOf(G[(size_t)i]); }
            glm::quat ParentRot(int i) const { return Nodes[(size_t)i].Parent >= 0 ? Rot(Nodes[(size_t)i].Parent) : glm::quat(1, 0, 0, 0); }

            void SetGlobalRotation(int i, const glm::quat& q)
            {
                P[(size_t)i].Rotation = glm::normalize(glm::inverse(ParentRot(i)) * q);
                Recompute((size_t)i);
            }

            void SetGlobalPosition(int i, const glm::vec3& p)
            {
                const int parent = Nodes[(size_t)i].Parent;
                glm::vec3 local = parent >= 0 ? glm::vec3(glm::inverse(G[(size_t)parent]) * glm::vec4(p, 1.0f)) : p;
                P[(size_t)i].Translation = local;
                Recompute((size_t)i);
            }

            // Kemiği döndürerek "from" yönünü "to" yönüne çevir (ağırlıklı)
            void Rotate(int i, const glm::vec3& from, const glm::vec3& to, float weight, float maxAngle = 3.2f)
            {
                if (glm::length(from) < 1e-6f || glm::length(to) < 1e-6f || weight <= 0.0f)
                    return;
                glm::quat r = glm::rotation(glm::normalize(from), glm::normalize(to));
                float angle = glm::angle(r);
                if (angle > maxAngle && angle > 1e-6f)
                    r = glm::angleAxis(maxAngle, glm::axis(r));
                if (weight < 1.0f)
                    r = glm::slerp(glm::quat(1, 0, 0, 0), r, weight);
                SetGlobalRotation(i, glm::normalize(r * Rot(i)));
            }
        };

        int FindNode(const std::vector<FlatAnimNode>& nodes, const std::string& name)
        {
            if (name.empty())
                return -1;
            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i].Node && nodes[i].Node->Name == name)
                    return (int)i;
            // Önek / sonek eşleşmesi ("LeftFoot" -> "mixamorig:LeftFoot")
            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i].Node)
                {
                    const std::string& n = nodes[i].Node->Name;
                    if (n.size() > name.size() && n.compare(n.size() - name.size(), name.size(), name) == 0)
                        return (int)i;
                }
            return -1;
        }

        bool IsAncestor(const std::vector<FlatAnimNode>& nodes, int ancestor, int node)
        {
            for (int p = node; p >= 0; p = nodes[(size_t)p].Parent)
                if (p == ancestor)
                    return true;
            return false;
        }

        bool TargetInNodeSpace(Scene& scene, uint64_t uuid, const glm::mat4& nodeFromWorld, glm::vec3& outPos, glm::quat* outRot = nullptr)
        {
            if (!uuid)
                return false;
            Entity t = scene.FindEntityByUUID(uuid);
            if (!t || !t.HasComponent<TransformComponent>())
                return false;
            glm::mat4 m = nodeFromWorld * t.GetComponent<TransformComponent>().WorldTransform;
            outPos = glm::vec3(m[3]);
            if (outRot)
                *outRot = RotationOf(m);
            return true;
        }

        bool SkinnedModelOf(Entity e)
        {
            if (!e.HasComponent<ModelComponent>())
                return false;
            auto& mc = e.GetComponent<ModelComponent>();
            return mc.ModelData && mc.ModelData->IsSkinned && mc.ModelData->SkeletonData;
        }

        template<typename T>
        void Gather(Scene& scene)
        {
            auto view = scene.m_Registry.view<T>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                float w = std::clamp(view.template get<T>(e).Weight, 0.0f, 1.0f);
                Entity cur{ e, &scene };
                Entity owner{};
                while (cur)
                {
                    if (cur.HasComponent<RigComponent>())
                        w *= std::clamp(cur.GetComponent<RigComponent>().Weight, 0.0f, 1.0f);
                    if (SkinnedModelOf(cur))
                    {
                        owner = cur;
                        break;
                    }
                    cur = cur.GetParent();
                }
                if (owner && w > 0.0f)
                    s_ByOwner[(uint32_t)owner].push_back({ e, w });
            }
        }
    }

    namespace RigConstraints
    {
        void BeginFrame(Scene& scene, float dt)
        {
            s_ByOwner.clear();
            s_Dt = dt;
            Gather<TwoBoneIKConstraintComponent>(scene);
            Gather<MultiAimConstraintComponent>(scene);
            Gather<ChainIKConstraintComponent>(scene);
            Gather<DampedTransformComponent>(scene);
            // Unity gibi hiyerarşi (oluşturma) sırasıyla
            for (auto& [owner, list] : s_ByOwner)
                std::sort(list.begin(), list.end(), [](const ConstraintRef& a, const ConstraintRef& b) { return (uint32_t)a.E < (uint32_t)b.E; });
        }

        bool HasConstraints(uint32_t modelEntity)
        {
            return s_ByOwner.count(modelEntity) > 0;
        }

        void Apply(Scene& scene, uint32_t modelEntity, const Skeleton& skeleton, Pose& pose)
        {
            auto it = s_ByOwner.find(modelEntity);
            if (it == s_ByOwner.end())
                return;
            const auto& nodes = Animator::Flatten(skeleton);
            if (nodes.empty() || pose.size() < nodes.size())
                return;
            auto& reg = scene.m_Registry;
            const glm::mat4 worldFromNode = reg.get<TransformComponent>((entt::entity)modelEntity).WorldTransform * skeleton.GlobalInverseRoot;
            const glm::mat4 nodeFromWorld = glm::inverse(worldFromNode);
            Solver s(nodes, pose);

            for (const ConstraintRef& ref : it->second)
            {
                const float w = ref.Weight;
                if (auto* ik = reg.try_get<TwoBoneIKConstraintComponent>(ref.E))
                {
                    const int a = FindNode(nodes, ik->Root), b = FindNode(nodes, ik->Mid), c = FindNode(nodes, ik->Tip);
                    glm::vec3 target;
                    glm::quat targetRot;
                    if (a < 0 || b < 0 || c < 0 || !IsAncestor(nodes, a, b) || !IsAncestor(nodes, b, c) ||
                        !TargetInNodeSpace(scene, ik->Target, nodeFromWorld, target, &targetRot))
                        continue;
                    const glm::vec3 pa = s.Pos(a), pb = s.Pos(b), pc = s.Pos(c);
                    const float l1 = glm::length(pb - pa), l2 = glm::length(pc - pb);
                    if (l1 < 1e-6f || l2 < 1e-6f)
                        continue;
                    // Hedef, ağırlıkla mevcut uçtan karıştırılır; erişim sınırına kırpılır
                    glm::vec3 t = glm::mix(pc, target, std::clamp(ik->TargetPositionWeight, 0.0f, 1.0f) * w);
                    glm::vec3 toT = t - pa;
                    float d = glm::length(toT);
                    if (d < 1e-6f)
                        continue;
                    d = std::clamp(d, std::fabs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
                    const glm::vec3 dir = glm::normalize(toT);
                    // Bükülme düzlemi: ipucu varsa ona doğru, yoksa mevcut diz yönü
                    glm::vec3 pole;
                    glm::vec3 hintPos;
                    if (TargetInNodeSpace(scene, ik->Hint, nodeFromWorld, hintPos))
                        pole = hintPos - pa;
                    else
                        pole = pb - pa;
                    glm::vec3 perp = pole - dir * glm::dot(pole, dir);
                    if (glm::length(perp) < 1e-5f)
                    {
                        glm::vec3 any = std::fabs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                        perp = glm::cross(glm::cross(dir, any), dir);
                    }
                    perp = glm::normalize(perp);
                    const float cosA = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
                    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
                    const glm::vec3 knee = pa + dir * (l1 * cosA) + perp * (l1 * sinA);
                    s.Rotate(a, s.Pos(b) - pa, knee - pa, 1.0f);
                    s.Rotate(b, s.Pos(c) - s.Pos(b), pa + dir * d - s.Pos(b), 1.0f);
                    // Uç kemik hedefin dönüşünü alır (ağırlıklı)
                    const float rw = std::clamp(ik->TargetRotationWeight, 0.0f, 1.0f) * w;
                    if (rw > 0.0f)
                        s.SetGlobalRotation(c, glm::slerp(s.Rot(c), targetRot, rw));
                }
                else if (auto* aim = reg.try_get<MultiAimConstraintComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, aim->Bone);
                    glm::vec3 target;
                    if (bone < 0 || !TargetInNodeSpace(scene, aim->Target, nodeFromWorld, target))
                        continue;
                    const glm::vec3 p = s.Pos(bone);
                    const glm::vec3 aimWorld = s.Rot(bone) * AxisVector(aim->AimAxis);
                    s.Rotate(bone, aimWorld, target - p, w, glm::radians(std::clamp(aim->MaxAngle, 0.0f, 180.0f)));
                    // Yukarı ekseni dünya yukarısına en yakın olacak şekilde nişan ekseni etrafında döndür
                    const glm::vec3 worldUp = glm::normalize(glm::vec3(nodeFromWorld * glm::vec4(0, 1, 0, 0)));
                    const glm::vec3 aimNow = glm::normalize(s.Rot(bone) * AxisVector(aim->AimAxis));
                    const glm::vec3 upNow = s.Rot(bone) * AxisVector(aim->UpAxis);
                    glm::vec3 a0 = upNow - aimNow * glm::dot(upNow, aimNow), a1 = worldUp - aimNow * glm::dot(worldUp, aimNow);
                    if (glm::length(a0) > 1e-4f && glm::length(a1) > 1e-4f)
                        s.Rotate(bone, a0, a1, w);
                }
                else if (auto* chain = reg.try_get<ChainIKConstraintComponent>(ref.E))
                {
                    const int root = FindNode(nodes, chain->Root), tip = FindNode(nodes, chain->Tip);
                    glm::vec3 target;
                    if (root < 0 || tip < 0 || !IsAncestor(nodes, root, tip) || !TargetInNodeSpace(scene, chain->Target, nodeFromWorld, target))
                        continue;
                    std::vector<int> joints;
                    for (int n = tip; n >= 0; n = nodes[(size_t)n].Parent)
                    {
                        joints.push_back(n);
                        if (n == root) break;
                    }
                    std::reverse(joints.begin(), joints.end());
                    const size_t count = joints.size();
                    if (count < 2)
                        continue;
                    std::vector<glm::vec3> P(count);
                    std::vector<float> len(count - 1);
                    for (size_t k = 0; k < count; ++k) P[k] = s.Pos(joints[k]);
                    float total = 0.0f;
                    for (size_t k = 0; k + 1 < count; ++k) { len[k] = glm::length(P[k + 1] - P[k]); total += len[k]; }
                    const glm::vec3 base = P[0];
                    glm::vec3 goal = glm::mix(P[count - 1], target, w);
                    if (glm::length(goal - base) > total)
                        goal = base + glm::normalize(goal - base) * total;   // erişilemez: düz uzan
                    // FABRIK
                    for (int iter = 0; iter < std::clamp(chain->MaxIterations, 1, 100); ++iter)
                    {
                        P[count - 1] = goal;
                        for (size_t k = count - 1; k-- > 0;)
                        {
                            glm::vec3 dvec = P[k] - P[k + 1];
                            P[k] = P[k + 1] + (glm::length(dvec) > 1e-6f ? glm::normalize(dvec) : glm::vec3(0, 1, 0)) * len[k];
                        }
                        P[0] = base;
                        for (size_t k = 1; k < count; ++k)
                        {
                            glm::vec3 dvec = P[k] - P[k - 1];
                            P[k] = P[k - 1] + (glm::length(dvec) > 1e-6f ? glm::normalize(dvec) : glm::vec3(0, 1, 0)) * len[k - 1];
                        }
                        if (glm::length(P[count - 1] - goal) < std::max(chain->Tolerance, 1e-6f))
                            break;
                    }
                    // Eklemleri sırayla yeni konumlara çevir
                    for (size_t k = 0; k + 1 < count; ++k)
                        s.Rotate(joints[k], s.Pos(joints[k + 1]) - s.Pos(joints[k]), P[k + 1] - s.Pos(joints[k]), 1.0f);
                }
                else if (auto* damp = reg.try_get<DampedTransformComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, damp->Bone);
                    if (bone < 0)
                        continue;
                    const glm::vec3 animPos = s.Pos(bone);
                    const glm::quat animRot = s.Rot(bone);
                    if (!damp->Initialized)
                    {
                        damp->Position = animPos;
                        damp->Rotation = animRot;
                        damp->Initialized = true;
                    }
                    // Kare hızından bağımsız üstel yaklaşım (60 Hz referans)
                    const float frames = std::max(s_Dt, 0.0f) * 60.0f;
                    const float ap = 1.0f - std::pow(std::clamp(damp->DampPosition, 0.0f, 0.999f), frames);
                    const float ar = 1.0f - std::pow(std::clamp(damp->DampRotation, 0.0f, 0.999f), frames);
                    damp->Position = glm::mix(damp->Position, animPos, ap);
                    damp->Rotation = glm::normalize(glm::slerp(damp->Rotation, animRot, ar));
                    s.SetGlobalRotation(bone, glm::slerp(animRot, damp->Rotation, w));
                    s.SetGlobalPosition(bone, glm::mix(animPos, damp->Position, w));
                }
            }
        }

        bool GetBoneWorldTransform(Scene& scene, uint32_t modelEntity, const std::string& bone, glm::mat4& out)
        {
            entt::entity e = (entt::entity)modelEntity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<ModelComponent, TransformComponent>(e))
                return false;
            auto& mc = reg.get<ModelComponent>(e);
            if (!mc.ModelData || !mc.ModelData->SkeletonData)
                return false;
            const Skeleton& sk = *mc.ModelData->SkeletonData;
            const std::vector<glm::mat4>* palette = nullptr;
            if (auto* an = reg.try_get<AnimationComponent>(e); an && !an->BoneMatricesGPU.empty())
                palette = &an->BoneMatricesGPU;
            else if (auto* at = reg.try_get<AnimatorComponent>(e); at && !at->BoneMatricesGPU.empty())
                palette = &at->BoneMatricesGPU;
            int index = -1;
            if (auto f = sk.BoneNameToIndex.find(bone); f != sk.BoneNameToIndex.end())
                index = (int)f->second;
            else
                for (size_t i = 0; i < sk.Bones.size(); ++i)
                {
                    const std::string& n = sk.Bones[i].Name;
                    if (n.size() > bone.size() && n.compare(n.size() - bone.size(), bone.size(), bone) == 0) { index = (int)i; break; }
                }
            if (index < 0)
                return false;
            const glm::mat4& world = reg.get<TransformComponent>(e).WorldTransform;
            const glm::mat4 skin = palette && (size_t)index < palette->size() ? (*palette)[(size_t)index] : glm::mat4(1.0f);
            out = world * skin * glm::inverse(sk.Bones[(size_t)index].OffsetMatrix);
            return true;
        }
    }

    void RegisterRigComponents()
    {
        const char* axes[] = { "X", "-X", "Y", "-Y", "Z", "-Z" };
        std::vector<std::string> axisOptions(axes, axes + 6);

        ComponentDef<RigComponent>("Rig", "RigComponent", "Animation Rigging", ICON_FA_PERSON_RUNNING)
            .Describe("Groups the bone constraints below it; Weight fades all of them in or out.")
            .Field("Weight", "Weight", &RigComponent::Weight).Range(0.0f, 1.0f, 0.01f);

        using T = TwoBoneIKConstraintComponent;
        ComponentDef<T>("Two Bone IK Constraint", "TwoBoneIKConstraintComponent", "Animation Rigging", ICON_FA_SHOE_PRINTS)
            .Describe("Bends a root - mid - tip chain (arm, leg) so the tip reaches the target; the hint picks the elbow / knee direction. Put it on a child of the character.")
            .Field("Weight", "Weight", &T::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Root", "Root Bone", &T::Root)
            .Field("Mid", "Mid Bone", &T::Mid)
            .Field("Tip", "Tip Bone", &T::Tip)
            .Field("Target", "Target", &T::Target)
            .Field("Hint", "Hint", &T::Hint)
            .Field("TargetPositionWeight", "Target Position Weight", &T::TargetPositionWeight).Range(0.0f, 1.0f, 0.01f)
            .Field("TargetRotationWeight", "Target Rotation Weight", &T::TargetRotationWeight).Range(0.0f, 1.0f, 0.01f);

        using A = MultiAimConstraintComponent;
        ComponentDef<A>("Multi-Aim Constraint", "MultiAimConstraintComponent", "Animation Rigging", ICON_FA_CROSSHAIRS)
            .Describe("Turns a bone (head, eyes, turret) towards the target, keeping its up axis close to world up.")
            .Field("Weight", "Weight", &A::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &A::Bone)
            .Field("Target", "Target", &A::Target)
            .Field("AimAxis", "Aim Axis", &A::AimAxis).Options(axisOptions)
            .Field("UpAxis", "Up Axis", &A::UpAxis).Options(axisOptions)
            .Field("MaxAngle", "Max Angle", &A::MaxAngle).Range(0.0f, 180.0f, 0.5f).Format("%.1f°");

        using C = ChainIKConstraintComponent;
        ComponentDef<C>("Chain IK Constraint", "ChainIKConstraintComponent", "Animation Rigging", ICON_FA_LINK)
            .Describe("Reaches a bone chain of any length (tail, tentacle, spine) towards the target (FABRIK).")
            .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Root", "Root Bone", &C::Root)
            .Field("Tip", "Tip Bone", &C::Tip)
            .Field("Target", "Target", &C::Target)
            .Field("MaxIterations", "Max Iterations", &C::MaxIterations).Range(1.0f, 100.0f, 1.0f)
            .Field("Tolerance", "Tolerance", &C::Tolerance).Range(0.00001f, 1.0f, 0.0001f);

        using D = DampedTransformComponent;
        ComponentDef<D>("Damped Transform", "DampedTransformComponent", "Animation Rigging", ICON_FA_WIND)
            .Describe("The bone follows its animated pose with a lag: secondary motion for tails, antennas, hair, bags.")
            .Field("Weight", "Weight", &D::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &D::Bone)
            .Field("DampPosition", "Damp Position", &D::DampPosition).Range(0.0f, 1.0f, 0.01f)
            .Field("DampRotation", "Damp Rotation", &D::DampRotation).Range(0.0f, 1.0f, 0.01f);
    }
}
