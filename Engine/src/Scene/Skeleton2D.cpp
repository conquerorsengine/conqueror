#include "Skeleton2D.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        float AngleOf(const glm::vec2& v) { return std::atan2(v.y, v.x); }

        float WrapAngle(float a)
        {
            while (a > kPi) a -= 2.0f * kPi;
            while (a < -kPi) a += 2.0f * kPi;
            return a;
        }

        // Yerel Z dönüşü dünyada hangi yönde döndürür (aynalanmış ata: ters)
        float RotationSign(Scene& scene, entt::entity e)
        {
            Entity parent = Entity{ e, &scene }.GetParent();
            if (!parent)
                return 1.0f;
            const glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)parent);
            return (w[0][0] * w[1][1] - w[0][1] * w[1][0]) < 0.0f ? -1.0f : 1.0f;
        }

        glm::vec2 JointOf(Scene& scene, entt::entity e)
        {
            return glm::vec2(scene.ComputeWorldMatrix(e)[3]);
        }

        // Kemik ucu (Bone 2D uzunluğu yerel +X boyunca), uzunluk yoksa eklem
        glm::vec2 BoneEnd(Scene& scene, entt::entity e)
        {
            const glm::mat4 w = scene.ComputeWorldMatrix(e);
            float length = 0.0f;
            if (auto* bone = scene.m_Registry.try_get<Bone2DComponent>(e))
                length = bone->Length;
            return glm::vec2(w * glm::vec4(length, 0.0f, 0.0f, 1.0f));
        }

        bool HasLength(Scene& scene, entt::entity e)
        {
            auto* bone = scene.m_Registry.try_get<Bone2DComponent>(e);
            return bone && bone->Length > 1e-5f;
        }

        void Rotate(Scene& scene, entt::entity e, float worldDelta)
        {
            auto& tc = scene.m_Registry.get<TransformComponent>(e);
            tc.Rotation.z = WrapAngle(tc.Rotation.z + worldDelta * RotationSign(scene, e));
        }

        entt::entity ParentOf(Scene& scene, entt::entity e)
        {
            Entity p = Entity{ e, &scene }.GetParent();
            return p ? (entt::entity)p : entt::null;
        }

        void SolveCCD(Scene& scene, entt::entity tip, const IK2DComponent& ik, const glm::vec2& target)
        {
            // Döndürülen eklemler: uç kemik uzunluklu ise kendisi, sonra atalar
            std::vector<entt::entity> chain;
            entt::entity j = HasLength(scene, tip) ? tip : ParentOf(scene, tip);
            while (j != entt::null && (int)chain.size() < std::max(ik.ChainLength, 1))
            {
                chain.push_back(j);
                j = ParentOf(scene, j);
            }
            const float weight = std::clamp(ik.Weight, 0.0f, 1.0f);
            for (int it = 0; it < std::max(ik.Iterations, 1); ++it)
            {
                for (entt::entity joint : chain)
                {
                    const glm::vec2 p = JointOf(scene, joint);
                    const glm::vec2 a = BoneEnd(scene, tip) - p, b = target - p;
                    if (glm::dot(a, a) < 1e-10f || glm::dot(b, b) < 1e-10f)
                        continue;
                    const float angle = std::atan2(a.x * b.y - a.y * b.x, glm::dot(a, b));
                    Rotate(scene, joint, angle * weight);
                }
                if (glm::length(BoneEnd(scene, tip) - target) < 1e-3f)
                    break;
            }
        }

        void SolveTwoBone(Scene& scene, entt::entity tip, const IK2DComponent& ik, const glm::vec2& target)
        {
            // alt kemik: uzunluklu uç ya da ucun atası; üst kemik onun atası
            const entt::entity lower = HasLength(scene, tip) ? tip : ParentOf(scene, tip);
            const entt::entity upper = lower != entt::null ? ParentOf(scene, lower) : entt::null;
            if (lower == entt::null || upper == entt::null)
                return;
            const glm::vec2 s = JointOf(scene, upper);
            const glm::vec2 m = JointOf(scene, lower);
            const glm::vec2 e = BoneEnd(scene, tip);
            const float l1 = glm::length(m - s), l2 = glm::length(e - m);
            if (l1 < 1e-5f || l2 < 1e-5f)
                return;
            const glm::vec2 toTarget = target - s;
            const float d = std::clamp(glm::length(toTarget), std::abs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
            const float cosA = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
            const float bend = ik.FlipBend ? -1.0f : 1.0f;
            const float upperAngle = AngleOf(toTarget) + bend * std::acos(cosA);
            const float weight = std::clamp(ik.Weight, 0.0f, 1.0f);
            Rotate(scene, upper, WrapAngle(upperAngle - AngleOf(m - s)) * weight);
            // Üst kemik döndü: dirsek yeni konumunda, alt kemiği hedefe çevir
            const glm::vec2 m2 = JointOf(scene, lower);
            const glm::vec2 e2 = BoneEnd(scene, tip);
            Rotate(scene, lower, WrapAngle(AngleOf(target - m2) - AngleOf(e2 - m2)) * weight);
        }
    }

    namespace Skeleton2D
    {
        void Update(Scene& scene, bool runtime)
        {
            auto view = scene.m_Registry.view<TransformComponent, IK2DComponent>();
            for (auto e : view)
            {
                auto& ik = view.get<IK2DComponent>(e);
                if (!ik.Enabled || ik.Target == 0 || (!runtime && !ik.RunInEditor) || !scene.IsEntityActive(e))
                    continue;
                Entity target = scene.FindEntityByUUID(ik.Target);
                if (!target)
                    continue;
                const glm::vec2 goal = glm::vec2(scene.ComputeWorldMatrix((entt::entity)target)[3]);
                if (ik.Solver == IK2DSolver::TwoBone)
                    SolveTwoBone(scene, e, ik, goal);
                else
                    SolveCCD(scene, e, ik, goal);
                ik.Error = glm::length(BoneEnd(scene, e) - goal);
            }
        }

        void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection)
        {
            auto view = scene.m_Registry.view<TransformComponent, Bone2DComponent>();
            for (auto e : view)
            {
                const auto& bone = view.get<Bone2DComponent>(e);
                if (!bone.ShowGizmo || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                const glm::vec3 a = glm::vec3(w[3]);
                const glm::vec3 b = glm::vec3(w * glm::vec4(std::max(bone.Length, 0.05f), 0.0f, 0.0f, 1.0f));
                const glm::vec3 axis = b - a;
                const float len = glm::length(axis);
                if (len < 1e-5f)
                    continue;
                const bool selected = std::find(selection.begin(), selection.end(), (uint32_t)e) != selection.end();
                const glm::vec4 color = selected ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : bone.GizmoColor;
                // Godot kemik biçimi: eklemden %20 ileride geniş, uçta sivri baklava
                const glm::vec3 dir = axis / len;
                const glm::vec3 side = glm::vec3(-dir.y, dir.x, 0.0f) * len * 0.12f;
                const glm::vec3 waist = a + axis * 0.2f;
                DebugDraw::Line(a, waist + side, color, 0.0f, false);
                DebugDraw::Line(a, waist - side, color, 0.0f, false);
                DebugDraw::Line(waist + side, b, color, 0.0f, false);
                DebugDraw::Line(waist - side, b, color, 0.0f, false);
                DebugDraw::Line(waist + side, waist - side, color * glm::vec4(1, 1, 1, 0.6f), 0.0f, false);
            }
        }
    }

    void RegisterSkeleton2DComponents()
    {
        {
            using C = Bone2DComponent;
            ComponentDef<C>("Bone 2D", "Bone2DComponent", "Animation Rigging", ICON_FA_BONE)
                .Describe("Godot Bone2D: a 2D skeleton bone pointing along local +X. Build skeletons as an entity hierarchy, parent "
                          "sprites under bones and animate bone rotations (Timeline / Animator); add IK 2D to a hand or foot bone.")
                .Field("Length", "Length", &C::Length).Range(0.0f, 1000.0f, 0.01f)
                .Field("ShowGizmo", "Show Gizmo", &C::ShowGizmo)
                .Field("GizmoColor", "Gizmo Color", &C::GizmoColor, FieldKind::Color4);
        }
        {
            using C = IK2DComponent;
            ComponentDef<C>("IK 2D", "IK2DComponent", "Animation Rigging", ICON_FA_PERSON_WALKING)
                .Describe("2D inverse kinematics on the tip bone (Godot CCDIK / TwoBoneIK, Unity 2D IK): rotates the bone chain so "
                          "the tip (bone end, or joint without Bone 2D length) reaches the Target object.")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("Solver", "Solver", &C::Solver).Options({ "CCD (any length)", "Two Bone (limb)" })
                .Field("Target", "Target", &C::Target)
                .Field("ChainLength", "Chain Length", &C::ChainLength).Range(1.0f, 32.0f, 1.0f)
                    .VisibleIf([](const C& c) { return c.Solver == IK2DSolver::CCD; })
                .Field("Iterations", "Iterations", &C::Iterations).Range(1.0f, 64.0f, 1.0f)
                    .VisibleIf([](const C& c) { return c.Solver == IK2DSolver::CCD; })
                .Field("FlipBend", "Flip Bend", &C::FlipBend).VisibleIf([](const C& c) { return c.Solver == IK2DSolver::TwoBone; })
                .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f)
                .Field("RunInEditor", "Run In Editor", &C::RunInEditor)
                .Field("Error", "Error", &C::Error).ReadOnly();
        }
    }
}
