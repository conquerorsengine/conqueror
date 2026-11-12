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

        // Kaynak nesnenin dönüşümü modelin düğüm uzayında
        bool SourceMatrix(Scene& scene, uint64_t uuid, const glm::mat4& nodeFromWorld, glm::mat4& out)
        {
            if (!uuid)
                return false;
            Entity t = scene.FindEntityByUUID(uuid);
            if (!t || !t.HasComponent<TransformComponent>())
                return false;
            out = nodeFromWorld * t.GetComponent<TransformComponent>().WorldTransform;
            return true;
        }

        // Kaynakların ağırlıklı konum ve dönüş ortalaması (kuaterniyonlar ilkiyle aynı yarıküreye çevrilip toplanır)
        struct WeightedAverage
        {
            glm::vec3 Pos{ 0.0f };
            glm::quat Rot{ 0.0f, 0.0f, 0.0f, 0.0f };
            float Total = 0.0f;
            bool HasFirst = false;
            glm::quat First{ 1, 0, 0, 0 };
            void Add(const glm::mat4& m, float w)
            {
                if (w <= 0.0f)
                    return;
                glm::quat q = RotationOf(m);
                if (!HasFirst) { First = q; HasFirst = true; }
                if (glm::dot(q, First) < 0.0f)
                    q = -q;
                Pos += glm::vec3(m[3]) * w;
                Rot = Rot + q * w;
                Total += w;
            }
            bool Valid() const { return Total > 0.0f; }
            glm::vec3 Position() const { return Pos / Total; }
            glm::quat Rotation() const { return glm::normalize(Rot); }
        };

        bool SkinnedModelOf(Entity e)
        {
            if (!e.HasComponent<MeshRendererComponent>())
                return false;
            auto& mc = e.GetComponent<MeshRendererComponent>();
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
            Gather<MultiParentConstraintComponent>(scene);
            Gather<MultiPositionConstraintComponent>(scene);
            Gather<MultiRotationConstraintComponent>(scene);
            Gather<OverrideTransformComponent>(scene);
            Gather<TwistCorrectionComponent>(scene);
            Gather<BlendConstraintComponent>(scene);
            Gather<TwistChainConstraintComponent>(scene);
            Gather<SpringBoneComponent>(scene);
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
                else if (auto* sb = reg.try_get<SpringBoneComponent>(ref.E))
                {
                    const int root = FindNode(nodes, sb->Root), tip = FindNode(nodes, sb->Tip);
                    if (root < 0 || tip < 0 || !IsAncestor(nodes, root, tip))
                        continue;
                    std::vector<int> joints;
                    for (int n = tip; n >= 0; n = nodes[(size_t)n].Parent)
                    {
                        joints.push_back(n);
                        if (n == root) break;
                    }
                    std::reverse(joints.begin(), joints.end());
                    const size_t count = joints.size();
                    // Uçlar: her eklemin çocuğu (son eklem için sanal uzantı); dünya uzayında
                    auto worldOf = [&](const glm::vec3& p) { return glm::vec3(worldFromNode * glm::vec4(p, 1.0f)); };
                    auto nodeOf = [&](const glm::vec3& p) { return glm::vec3(nodeFromWorld * glm::vec4(p, 1.0f)); };
                    auto restTail = [&](size_t k) -> glm::vec3 {
                        if (k + 1 < count)
                            return worldOf(s.Pos(joints[k + 1]));
                        // Uç kemik: üst kemikten gelen yönde TipLength kadar
                        const glm::vec3 head = s.Pos(joints[k]);
                        glm::vec3 dir = count > 1 ? head - s.Pos(joints[k - 1]) : glm::vec3(0.0f, 1.0f, 0.0f);
                        if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.0f, 1.0f, 0.0f);
                        const glm::vec3 nodeTail = head + glm::normalize(dir) * (std::max(sb->TipLength, 0.0f) / std::max(glm::length(glm::vec3(worldFromNode[0])), 1e-6f));
                        return worldOf(nodeTail);
                    };
                    const size_t segments = sb->TipLength > 0.0f ? count : count - 1;
                    if (segments == 0)
                        continue;
                    if (!sb->Initialized || sb->Current.size() != segments)
                    {
                        sb->Current.resize(segments);
                        sb->Prev.resize(segments);
                        for (size_t k = 0; k < segments; ++k)
                            sb->Current[k] = sb->Prev[k] = restTail(k);
                        sb->Initialized = true;
                    }
                    // Çarpıştırıcılar (dünya): küre ya da kapsül (segment + yarıçap)
                    struct Col { glm::vec3 A, B; float R; };
                    std::vector<Col> cols;
                    for (uint64_t id : { sb->Collider1, sb->Collider2, sb->Collider3, sb->Collider4 })
                    {
                        if (!id) continue;
                        Entity ce = scene.FindEntityByUUID(id);
                        if (!ce) continue;
                        const glm::mat4 cw = ce.GetComponent<TransformComponent>().WorldTransform;
                        const float sc = std::max({ glm::length(glm::vec3(cw[0])), glm::length(glm::vec3(cw[1])), glm::length(glm::vec3(cw[2])) });
                        if (ce.HasComponent<CapsuleColliderComponent>())
                        {
                            const auto& c = ce.GetComponent<CapsuleColliderComponent>();
                            glm::vec3 axis(0.0f);
                            axis[std::clamp(c.Direction, 0, 2)] = 1.0f;
                            const float half = std::max(c.Height * 0.5f - c.Radius, 0.0f);
                            cols.push_back({ glm::vec3(cw * glm::vec4(c.Offset - axis * half, 1.0f)), glm::vec3(cw * glm::vec4(c.Offset + axis * half, 1.0f)), c.Radius * sc });
                        }
                        else if (ce.HasComponent<SphereColliderComponent>())
                        {
                            const auto& c = ce.GetComponent<SphereColliderComponent>();
                            const glm::vec3 p = glm::vec3(cw * glm::vec4(c.Offset, 1.0f));
                            cols.push_back({ p, p, c.Radius * sc });
                        }
                        else
                            cols.push_back({ glm::vec3(cw[3]), glm::vec3(cw[3]), 0.5f * sc });
                    }
                    const float dt = std::min(std::max(s_Dt, 0.0f), 0.05f);
                    const float drag = std::clamp(sb->Drag, 0.0f, 1.0f);
                    const glm::vec3 gravity = (glm::length(sb->GravityDirection) > 1e-6f ? glm::normalize(sb->GravityDirection) : glm::vec3(0.0f, -1.0f, 0.0f))
                                            * sb->GravityPower;
                    sb->Deflection = 0.0f;
                    for (size_t k = 0; k < segments; ++k)
                    {
                        const glm::vec3 head = worldOf(s.Pos(joints[k]));   // üst eklemler döndükçe güncel
                        const glm::vec3 rest = restTail(k);
                        const float len = glm::length(rest - head);
                        if (len < 1e-6f)
                            continue;
                        const glm::vec3 restDir = (rest - head) / len;
                        // Işınlanma (karakter uzağa taşındı): uç dinlenme pozundan başlar (savrulmaz)
                        if (glm::length(sb->Current[k] - rest) > len * 5.0f)
                            sb->Current[k] = sb->Prev[k] = rest;
                        // VRM: eylemsizlik + dinlenme yönüne yay + dış kuvvet (Verlet)
                        const glm::vec3 inertia = (sb->Current[k] - sb->Prev[k]) * (1.0f - drag);
                        glm::vec3 next = sb->Current[k] + inertia + restDir * (std::max(sb->Stiffness, 0.0f) * dt * len * 10.0f) + gravity * dt * dt;
                        next = head + (glm::length(next - head) > 1e-6f ? glm::normalize(next - head) : restDir) * len;
                        for (const Col& c : cols)
                        {
                            const glm::vec3 ab = c.B - c.A;
                            const float t = glm::dot(ab, ab) > 1e-12f ? std::clamp(glm::dot(next - c.A, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
                            const glm::vec3 closest = c.A + ab * t;
                            const glm::vec3 d = next - closest;
                            const float minDist = c.R + std::max(sb->Radius, 0.0f);
                            if (glm::dot(d, d) < minDist * minDist)
                            {
                                next = closest + (glm::length(d) > 1e-6f ? glm::normalize(d) : restDir) * minDist;
                                next = head + glm::normalize(next - head) * len;
                            }
                        }
                        sb->Prev[k] = sb->Current[k];
                        sb->Current[k] = next;
                        sb->Deflection = std::max(sb->Deflection, glm::length(next - rest));
                        // Kemiği yeni uca çevir (ağırlıkla; düğüm uzayında)
                        const glm::vec3 hn = s.Pos(joints[k]);
                        s.Rotate(joints[k], nodeOf(rest) - hn, nodeOf(next) - hn, w);
                    }
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
                else if (auto* mp = reg.try_get<MultiParentConstraintComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, mp->Bone);
                    if (bone < 0)
                        continue;
                    const uint64_t src[3] = { mp->Source1, mp->Source2, mp->Source3 };
                    const float sw[3] = { mp->SourceWeight1, mp->SourceWeight2, mp->SourceWeight3 };
                    glm::mat4 m[3];
                    bool ok[3];
                    for (int k = 0; k < 3; ++k)
                        ok[k] = SourceMatrix(scene, src[k], nodeFromWorld, m[k]);
                    if (mp->MaintainOffset && !mp->OffsetsCaptured)
                    {
                        // İlk karede kemiğin kaynaklara göre duruşu (ebeveyne bağlarken kalan ofset gibi)
                        for (int k = 0; k < 3; ++k)
                            mp->Offsets[k] = ok[k] ? glm::inverse(m[k]) * s.G[(size_t)bone] : glm::mat4(1.0f);
                        mp->OffsetsCaptured = true;
                    }
                    WeightedAverage avg;
                    for (int k = 0; k < 3; ++k)
                        if (ok[k])
                            avg.Add(mp->MaintainOffset ? m[k] * mp->Offsets[k] : m[k], sw[k]);
                    if (!avg.Valid())
                        continue;
                    if (mp->ConstrainRotation)
                        s.SetGlobalRotation(bone, glm::slerp(s.Rot(bone), avg.Rotation(), w));
                    if (mp->ConstrainPosition)
                        s.SetGlobalPosition(bone, glm::mix(s.Pos(bone), avg.Position(), w));
                }
                else if (auto* mpos = reg.try_get<MultiPositionConstraintComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, mpos->Bone);
                    if (bone < 0)
                        continue;
                    const uint64_t src[3] = { mpos->Source1, mpos->Source2, mpos->Source3 };
                    const float sw[3] = { mpos->SourceWeight1, mpos->SourceWeight2, mpos->SourceWeight3 };
                    WeightedAverage avg;
                    for (int k = 0; k < 3; ++k)
                    {
                        glm::mat4 m;
                        if (SourceMatrix(scene, src[k], nodeFromWorld, m))
                            avg.Add(m, sw[k]);
                    }
                    if (!avg.Valid())
                        continue;
                    const glm::vec3 cur = s.Pos(bone);
                    if (mpos->MaintainOffset && !mpos->OffsetCaptured)
                    {
                        mpos->CapturedOffset = cur - avg.Position();
                        mpos->OffsetCaptured = true;
                    }
                    const glm::vec3 target = avg.Position() + (mpos->MaintainOffset ? mpos->CapturedOffset : glm::vec3(0.0f)) + mpos->Offset;
                    glm::vec3 next = cur;
                    if (mpos->ConstrainX) next.x = target.x;
                    if (mpos->ConstrainY) next.y = target.y;
                    if (mpos->ConstrainZ) next.z = target.z;
                    s.SetGlobalPosition(bone, glm::mix(cur, next, w));
                }
                else if (auto* mrot = reg.try_get<MultiRotationConstraintComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, mrot->Bone);
                    if (bone < 0)
                        continue;
                    const uint64_t src[3] = { mrot->Source1, mrot->Source2, mrot->Source3 };
                    const float sw[3] = { mrot->SourceWeight1, mrot->SourceWeight2, mrot->SourceWeight3 };
                    WeightedAverage avg;
                    for (int k = 0; k < 3; ++k)
                    {
                        glm::mat4 m;
                        if (SourceMatrix(scene, src[k], nodeFromWorld, m))
                            avg.Add(m, sw[k]);
                    }
                    if (!avg.Valid())
                        continue;
                    const glm::quat cur = s.Rot(bone);
                    if (mrot->MaintainOffset && !mrot->OffsetCaptured)
                    {
                        mrot->CapturedOffset = glm::normalize(glm::inverse(avg.Rotation()) * cur);
                        mrot->OffsetCaptured = true;
                    }
                    glm::quat target = avg.Rotation() * (mrot->MaintainOffset ? mrot->CapturedOffset : glm::quat(1, 0, 0, 0)) *
                                       glm::quat(glm::radians(mrot->Offset));
                    if (!(mrot->ConstrainX && mrot->ConstrainY && mrot->ConstrainZ))
                    {
                        // Eksen eksen: yalnız seçili Euler eksenleri hedefe gider
                        glm::vec3 ce = glm::eulerAngles(cur), te = glm::eulerAngles(target);
                        if (!mrot->ConstrainX) te.x = ce.x;
                        if (!mrot->ConstrainY) te.y = ce.y;
                        if (!mrot->ConstrainZ) te.z = ce.z;
                        target = glm::quat(te);
                    }
                    s.SetGlobalRotation(bone, glm::slerp(cur, glm::normalize(target), w));
                }
                else if (auto* ov = reg.try_get<OverrideTransformComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, ov->Bone);
                    if (bone < 0)
                        continue;
                    glm::mat4 target;
                    if (!SourceMatrix(scene, ov->Source, nodeFromWorld, target))
                    {
                        const glm::mat4 local = glm::translate(glm::mat4(1.0f), ov->Position) * glm::mat4_cast(glm::quat(glm::radians(ov->Rotation)));
                        const int parent = nodes[(size_t)bone].Parent;
                        switch (ov->Space)
                        {
                            case RigOverrideSpace::World: target = nodeFromWorld * local; break;
                            case RigOverrideSpace::Local: target = (parent >= 0 ? s.G[(size_t)parent] : glm::mat4(1.0f)) * local; break;
                            default: target = s.G[(size_t)bone] * local; break;   // Pivot: animasyonlu duruşa göre
                        }
                    }
                    const float pw = std::clamp(ov->PositionWeight, 0.0f, 1.0f) * w, rw = std::clamp(ov->RotationWeight, 0.0f, 1.0f) * w;
                    if (rw > 0.0f)
                        s.SetGlobalRotation(bone, glm::slerp(s.Rot(bone), RotationOf(target), rw));
                    if (pw > 0.0f)
                        s.SetGlobalPosition(bone, glm::mix(s.Pos(bone), glm::vec3(target[3]), pw));
                }
                else if (auto* tw = reg.try_get<TwistCorrectionComponent>(ref.E))
                {
                    const int source = FindNode(nodes, tw->Source);
                    if (source < 0)
                        continue;
                    // Kaynağın yerel dönüşünün bükülme ekseni etrafındaki kısmı (swing-twist ayrıştırması)
                    const glm::vec3 axis = AxisVector(tw->TwistAxis);
                    const glm::quat lq = glm::normalize(s.P[(size_t)source].Rotation);
                    const float angle = 2.0f * std::atan2(glm::dot(glm::vec3(lq.x, lq.y, lq.z), axis), lq.w);
                    const std::string* names[3] = { &tw->Twist1, &tw->Twist2, &tw->Twist3 };
                    const float tws[3] = { tw->TwistWeight1, tw->TwistWeight2, tw->TwistWeight3 };
                    for (int k = 0; k < 3; ++k)
                    {
                        const int node = FindNode(nodes, *names[k]);
                        if (node < 0 || std::fabs(tws[k]) < 1e-5f)
                            continue;
                        s.P[(size_t)node].Rotation = glm::normalize(s.P[(size_t)node].Rotation * glm::angleAxis(angle * tws[k] * w, axis));
                        s.Recompute((size_t)node);
                    }
                }
                else if (auto* bl = reg.try_get<BlendConstraintComponent>(ref.E))
                {
                    const int bone = FindNode(nodes, bl->Bone);
                    glm::mat4 a, b;
                    if (bone < 0 || !SourceMatrix(scene, bl->SourceA, nodeFromWorld, a) || !SourceMatrix(scene, bl->SourceB, nodeFromWorld, b))
                        continue;
                    if (bl->BlendRotation)
                    {
                        const glm::quat q = glm::slerp(RotationOf(a), RotationOf(b), std::clamp(bl->RotationWeight, 0.0f, 1.0f));
                        s.SetGlobalRotation(bone, glm::slerp(s.Rot(bone), q, w));
                    }
                    if (bl->BlendPosition)
                    {
                        const glm::vec3 p = glm::mix(glm::vec3(a[3]), glm::vec3(b[3]), std::clamp(bl->PositionWeight, 0.0f, 1.0f));
                        s.SetGlobalPosition(bone, glm::mix(s.Pos(bone), p, w));
                    }
                }
                else if (auto* tc = reg.try_get<TwistChainConstraintComponent>(ref.E))
                {
                    const int root = FindNode(nodes, tc->Root), tip = FindNode(nodes, tc->Tip);
                    glm::mat4 rt, tt;
                    if (root < 0 || tip < 0 || !IsAncestor(nodes, root, tip) || !SourceMatrix(scene, tc->RootTarget, nodeFromWorld, rt) ||
                        !SourceMatrix(scene, tc->TipTarget, nodeFromWorld, tt))
                        continue;
                    std::vector<int> joints;
                    for (int n = tip; n >= 0; n = nodes[(size_t)n].Parent)
                    {
                        joints.push_back(n);
                        if (n == root) break;
                    }
                    std::reverse(joints.begin(), joints.end());
                    const glm::quat qr = RotationOf(rt), qt = RotationOf(tt);
                    const size_t count = joints.size();
                    for (size_t k = 0; k < count; ++k)
                    {
                        const float f = count > 1 ? (float)k / (float)(count - 1) : 1.0f;
                        s.SetGlobalRotation(joints[k], glm::slerp(s.Rot(joints[k]), glm::slerp(qr, qt, f), w));
                    }
                }
            }
        }

        bool GetBoneWorldTransform(Scene& scene, uint32_t modelEntity, const std::string& bone, glm::mat4& out)
        {
            if (!GetBoneModelTransform(scene, modelEntity, bone, out))
                return false;
            out = scene.m_Registry.get<TransformComponent>((entt::entity)modelEntity).WorldTransform * out;
            return true;
        }

        bool GetBoneModelTransform(Scene& scene, uint32_t modelEntity, const std::string& bone, glm::mat4& out)
        {
            entt::entity e = (entt::entity)modelEntity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<MeshRendererComponent, TransformComponent>(e))
                return false;
            auto& mc = reg.get<MeshRendererComponent>(e);
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
            const glm::mat4 skin = palette && (size_t)index < palette->size() ? (*palette)[(size_t)index] : glm::mat4(1.0f);
            out = skin * glm::inverse(sk.Bones[(size_t)index].OffsetMatrix);
            return true;
        }
    }

    template<> void Scene::OnComponentAdded<MultiParentConstraintComponent>(Entity, MultiParentConstraintComponent&) {}
    template<> void Scene::OnComponentAdded<MultiPositionConstraintComponent>(Entity, MultiPositionConstraintComponent&) {}
    template<> void Scene::OnComponentAdded<MultiRotationConstraintComponent>(Entity, MultiRotationConstraintComponent&) {}
    template<> void Scene::OnComponentAdded<OverrideTransformComponent>(Entity, OverrideTransformComponent&) {}
    template<> void Scene::OnComponentAdded<TwistCorrectionComponent>(Entity, TwistCorrectionComponent&) {}
    template<> void Scene::OnComponentAdded<BlendConstraintComponent>(Entity, BlendConstraintComponent&) {}
    template<> void Scene::OnComponentAdded<TwistChainConstraintComponent>(Entity, TwistChainConstraintComponent&) {}
    template<> void Scene::OnComponentAdded<SpringBoneComponent>(Entity, SpringBoneComponent&) {}

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

        using SB = SpringBoneComponent;
        ComponentDef<SB>("Spring Bone", "SpringBoneComponent", "Animation Rigging", ICON_FA_WAVE_SQUARE)
            .Describe("Godot SpringBoneSimulator3D: the bone chain from Root to Tip swings like a spring - hair, tails, ears, skirts, "
                      "antennas. It lags behind when the character moves (Drag damps it), springs back to the animated pose (Stiffness), "
                      "falls with Gravity and keeps the bone lengths. Colliders (objects with a Sphere or Capsule Collider: head, shoulders) "
                      "push it out. Tip Length extends the last bone so the tip swings too.")
            .Field("Weight", "Weight", &SB::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Root", "Root Bone", &SB::Root)
            .Field("Tip", "Tip Bone", &SB::Tip)
            .Field("Stiffness", "Stiffness", &SB::Stiffness).Range(0.0f, 10.0f, 0.01f)
            .Field("Drag", "Drag", &SB::Drag).Range(0.0f, 1.0f, 0.01f)
            .Field("GravityPower", "Gravity Power", &SB::GravityPower).Range(0.0f, 100.0f, 0.1f)
            .Field("GravityDirection", "Gravity Direction", &SB::GravityDirection)
            .Field("Radius", "Radius", &SB::Radius).Range(0.0f, 1.0f, 0.001f)
            .Field("TipLength", "Tip Length", &SB::TipLength).Range(0.0f, 10.0f, 0.01f)
            .Field("Collider1", "Collider 1", &SB::Collider1)
            .Field("Collider2", "Collider 2", &SB::Collider2)
            .Field("Collider3", "Collider 3", &SB::Collider3)
            .Field("Collider4", "Collider 4", &SB::Collider4)
            .Field("Deflection", "Deflection", &SB::Deflection).ReadOnly();

        using D = DampedTransformComponent;
        ComponentDef<D>("Damped Transform", "DampedTransformComponent", "Animation Rigging", ICON_FA_WIND)
            .Describe("The bone follows its animated pose with a lag: secondary motion for tails, antennas, hair, bags.")
            .Field("Weight", "Weight", &D::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &D::Bone)
            .Field("DampPosition", "Damp Position", &D::DampPosition).Range(0.0f, 1.0f, 0.01f)
            .Field("DampRotation", "Damp Rotation", &D::DampRotation).Range(0.0f, 1.0f, 0.01f);

        using MP = MultiParentConstraintComponent;
        ComponentDef<MP>("Multi-Parent Constraint", "MultiParentConstraintComponent", "Animation Rigging", ICON_FA_SITEMAP)
            .Describe("The bone follows the weighted source objects as if it were their child (a weapon held in both hands, a rider). "
                      "Maintain Offset keeps the pose it had on the first frame relative to them.")
            .Field("Weight", "Weight", &MP::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &MP::Bone)
            .Field("Source1", "Source 1", &MP::Source1).Field("SourceWeight1", "Source 1 Weight", &MP::SourceWeight1).Range(0.0f, 1.0f, 0.01f)
            .Field("Source2", "Source 2", &MP::Source2).Field("SourceWeight2", "Source 2 Weight", &MP::SourceWeight2).Range(0.0f, 1.0f, 0.01f)
            .Field("Source3", "Source 3", &MP::Source3).Field("SourceWeight3", "Source 3 Weight", &MP::SourceWeight3).Range(0.0f, 1.0f, 0.01f)
            .Field("MaintainOffset", "Maintain Offset", &MP::MaintainOffset)
            .Field("ConstrainPosition", "Constrain Position", &MP::ConstrainPosition)
            .Field("ConstrainRotation", "Constrain Rotation", &MP::ConstrainRotation);

        using MPO = MultiPositionConstraintComponent;
        ComponentDef<MPO>("Multi-Position Constraint", "MultiPositionConstraintComponent", "Animation Rigging", ICON_FA_UP_DOWN_LEFT_RIGHT)
            .Describe("Moves the bone to the weighted average position of the sources, on the chosen axes.")
            .Field("Weight", "Weight", &MPO::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &MPO::Bone)
            .Field("Source1", "Source 1", &MPO::Source1).Field("SourceWeight1", "Source 1 Weight", &MPO::SourceWeight1).Range(0.0f, 1.0f, 0.01f)
            .Field("Source2", "Source 2", &MPO::Source2).Field("SourceWeight2", "Source 2 Weight", &MPO::SourceWeight2).Range(0.0f, 1.0f, 0.01f)
            .Field("Source3", "Source 3", &MPO::Source3).Field("SourceWeight3", "Source 3 Weight", &MPO::SourceWeight3).Range(0.0f, 1.0f, 0.01f)
            .Field("MaintainOffset", "Maintain Offset", &MPO::MaintainOffset)
            .Field("Offset", "Offset", &MPO::Offset)
            .Field("ConstrainX", "X", &MPO::ConstrainX).Field("ConstrainY", "Y", &MPO::ConstrainY).Field("ConstrainZ", "Z", &MPO::ConstrainZ);

        using MR = MultiRotationConstraintComponent;
        ComponentDef<MR>("Multi-Rotation Constraint", "MultiRotationConstraintComponent", "Animation Rigging", ICON_FA_ROTATE)
            .Describe("Turns the bone to the weighted average rotation of the sources, on the chosen axes.")
            .Field("Weight", "Weight", &MR::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &MR::Bone)
            .Field("Source1", "Source 1", &MR::Source1).Field("SourceWeight1", "Source 1 Weight", &MR::SourceWeight1).Range(0.0f, 1.0f, 0.01f)
            .Field("Source2", "Source 2", &MR::Source2).Field("SourceWeight2", "Source 2 Weight", &MR::SourceWeight2).Range(0.0f, 1.0f, 0.01f)
            .Field("Source3", "Source 3", &MR::Source3).Field("SourceWeight3", "Source 3 Weight", &MR::SourceWeight3).Range(0.0f, 1.0f, 0.01f)
            .Field("MaintainOffset", "Maintain Offset", &MR::MaintainOffset)
            .Field("Offset", "Offset", &MR::Offset)
            .Field("ConstrainX", "X", &MR::ConstrainX).Field("ConstrainY", "Y", &MR::ConstrainY).Field("ConstrainZ", "Z", &MR::ConstrainZ);

        using OT = OverrideTransformComponent;
        ComponentDef<OT>("Override Transform", "OverrideTransformComponent", "Animation Rigging", ICON_FA_ARROWS_TO_DOT)
            .Describe("Replaces the bone's animated transform with the source object's, or with the Position / Rotation below in World, Local "
                      "(parent) or Pivot (relative to the animated pose) space.")
            .Field("Weight", "Weight", &OT::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &OT::Bone)
            .Field("Source", "Source", &OT::Source)
            .Field("Space", "Space", &OT::Space).Options({ "World", "Local", "Pivot" })
            .Field("Position", "Position", &OT::Position)
            .Field("Rotation", "Rotation", &OT::Rotation)
            .Field("PositionWeight", "Position Weight", &OT::PositionWeight).Range(0.0f, 1.0f, 0.01f)
            .Field("RotationWeight", "Rotation Weight", &OT::RotationWeight).Range(0.0f, 1.0f, 0.01f);

        using TW = TwistCorrectionComponent;
        ComponentDef<TW>("Twist Correction", "TwistCorrectionComponent", "Animation Rigging", ICON_FA_ARROWS_SPIN)
            .Describe("Spreads the source bone's twist around the twist axis to the twist bones (forearm twist bones follow the wrist), "
                      "so the skin does not collapse.")
            .Field("Weight", "Weight", &TW::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Source", "Source Bone", &TW::Source)
            .Field("TwistAxis", "Twist Axis", &TW::TwistAxis).Options(axisOptions)
            .Field("Twist1", "Twist Bone 1", &TW::Twist1).Field("TwistWeight1", "Twist Bone 1 Weight", &TW::TwistWeight1).Range(-1.0f, 1.0f, 0.01f)
            .Field("Twist2", "Twist Bone 2", &TW::Twist2).Field("TwistWeight2", "Twist Bone 2 Weight", &TW::TwistWeight2).Range(-1.0f, 1.0f, 0.01f)
            .Field("Twist3", "Twist Bone 3", &TW::Twist3).Field("TwistWeight3", "Twist Bone 3 Weight", &TW::TwistWeight3).Range(-1.0f, 1.0f, 0.01f);

        using BL = BlendConstraintComponent;
        ComponentDef<BL>("Blend Constraint", "BlendConstraintComponent", "Animation Rigging", ICON_FA_CODE_MERGE)
            .Describe("Puts the bone between two source objects: 0 is Source A, 1 is Source B, separately for position and rotation.")
            .Field("Weight", "Weight", &BL::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Bone", "Constrained Bone", &BL::Bone)
            .Field("SourceA", "Source A", &BL::SourceA)
            .Field("SourceB", "Source B", &BL::SourceB)
            .Field("BlendPosition", "Blend Position", &BL::BlendPosition)
            .Field("PositionWeight", "Position A -> B", &BL::PositionWeight).Range(0.0f, 1.0f, 0.01f)
            .Field("BlendRotation", "Blend Rotation", &BL::BlendRotation)
            .Field("RotationWeight", "Rotation A -> B", &BL::RotationWeight).Range(0.0f, 1.0f, 0.01f);

        using TC = TwistChainConstraintComponent;
        ComponentDef<TC>("Twist Chain Constraint", "TwistChainConstraintComponent", "Animation Rigging", ICON_FA_LINK)
            .Describe("Turns every bone from Root to Tip smoothly between the rotation of the Root Target and the Tip Target (spine, tail).")
            .Field("Weight", "Weight", &TC::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Root", "Root Bone", &TC::Root)
            .Field("Tip", "Tip Bone", &TC::Tip)
            .Field("RootTarget", "Root Target", &TC::RootTarget)
            .Field("TipTarget", "Tip Target", &TC::TipTarget);
    }
}
