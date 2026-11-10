#include "PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Terrain/TerrainComponent.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/SpriteSheet.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>
#include <stb_image.h>
#include <yaml-cpp/yaml.h>

namespace Conqueror
{
    namespace
    {
        // Sprite'ın Custom Physics Shape'i, yoksa saydam olmayan bölgesi -> Polygon Collider 2D noktaları (Unity: bileşen eklenince)
        void FitPolygonToSprite(Entity e)
        {
            if (!e.HasComponent<SpriteRendererComponent>() || !e.HasComponent<PolygonCollider2DComponent>())
                return;
            auto& sr = e.GetComponent<SpriteRendererComponent>();
            if (!sr.Texture || sr.Texture->GetPath().empty())
                return;
            glm::vec2 mn, mx;
            if (!Renderer2D::GetSpriteLocalBounds(sr, mn, mx))
                return;
            auto sheet = SpriteSheet::Get(sr.Texture->GetPath());
            const SpriteDef* def = sheet ? sheet->FindForSprite(sr.SpriteName, glm::vec2((float)sr.Texture->GetWidth(), (float)sr.Texture->GetHeight())) : nullptr;
            std::vector<std::vector<glm::vec2>> shapes;
            if (def)
                shapes = def->PhysicsShapes;
            int w = 0, h = 0, ch = 0;
            glm::vec4 rect = def ? def->Rect : glm::vec4(0.0f);
            if (shapes.empty() || !def)
            {
                stbi_set_flip_vertically_on_load(0);
                unsigned char* rgba = stbi_load(SceneSerializer::ResolveAssetPath(sr.Texture->GetPath()).c_str(), &w, &h, &ch, 4);
                if (!rgba)
                    return;
                if (!def)
                    rect = glm::vec4(0.0f, 0.0f, (float)w, (float)h);
                if (shapes.empty())
                    shapes = SpriteSheet::GenerateOutline(rgba, w, h, rect, 0.5f);
                stbi_image_free(rgba);
            }
            if (shapes.empty())
                return;
            const glm::vec2 size(std::max(rect.z, 1.0f), std::max(rect.w, 1.0f));
            auto& pc = e.GetComponent<PolygonCollider2DComponent>();
            pc.Points.clear();
            pc.ExtraPaths.clear();
            for (size_t i = 0; i < shapes.size(); ++i)
            {
                std::vector<glm::vec2> local;
                for (const auto& p : shapes[i])
                    local.push_back(mn + (mx - mn) * (p / size));
                if (i == 0) pc.Points = std::move(local);
                else pc.ExtraPaths.push_back(std::move(local));
            }
        }
    }

    namespace
    {
        const glm::vec4 kPhysicsColor{ 0.45f, 0.85f, 0.55f, 1.0f };

        template<typename J>
        void JointCommonFields(ComponentDef<J>& def)
        {
            def.Field("ConnectedBody", "Connected Body", &J::ConnectedBody)
                   .Tooltip("Entity this joint attaches to. None = fixed to the world.")
               .Field("Anchor", "Anchor", &J::Anchor).Speed(0.01f)
                   .Tooltip("Joint position in this entity's local space.")
               .Field("BreakForce", "Break Force", &J::BreakForce).Range(0.0f, 1.0e7f, 1.0f).Header("Breaking")
                   .Tooltip("Force (N) that breaks the joint. 0 = unbreakable.")
               .Field("BreakTorque", "Break Torque", &J::BreakTorque).Range(0.0f, 1.0e7f, 1.0f)
                   .Tooltip("Torque (N·m) that breaks the joint. 0 = unbreakable.")
               .Field("EnableCollision", "Enable Collision", &J::EnableCollision)
                   .Tooltip("Allow the two connected bodies to collide with each other.")
               .Field("Broken", "Broken", &J::Broken).ReadOnly();
        }
    }

    template<> void Scene::OnComponentAdded<TerrainColliderComponent>(Entity, TerrainColliderComponent&) {}

    void RegisterPhysicsComponents()
    {
        ComponentDef<TerrainColliderComponent>("Terrain Collider", "TerrainColliderComponent", "Physics", ICON_FA_MOUNTAIN).Color(kPhysicsColor)
            .Describe("Makes the Terrain on this object solid (height field collider). Without it, objects fall through the terrain.")
            .Field("PhysicsMaterial", "Material", &TerrainColliderComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat")
                .Tooltip("Friction and bounciness of the ground. Empty = project defaults.");
        {
            ComponentDef<CharacterControllerComponent> def("Character Controller", "CharacterControllerComponent", "Physics", ICON_FA_PERSON_WALKING);
            def.Color(kPhysicsColor)
               .Describe("Capsule-based controller moved with CharacterMove(dx, dy, dz): slides along walls, climbs steps and slopes, never tunnels.")
               .Field("SlopeLimit", "Slope Limit", &CharacterControllerComponent::SlopeLimit, FieldKind::Angle).Range(0.0f, 89.0f, 0.5f).Format("%.1f°")
                   .Tooltip("Steepest walkable slope in degrees.")
               .Field("StepOffset", "Step Offset", &CharacterControllerComponent::StepOffset).Range(0.0f, 5.0f, 0.01f)
                   .Tooltip("Highest step the controller climbs automatically.")
               .Field("SkinWidth", "Skin Width", &CharacterControllerComponent::SkinWidth).Range(0.001f, 0.5f, 0.001f)
                   .Tooltip("Small gap kept from colliders to avoid getting stuck.")
               .Field("MinMoveDistance", "Min Move Distance", &CharacterControllerComponent::MinMoveDistance).Range(0.0f, 1.0f, 0.001f)
               .Field("Center", "Center", &CharacterControllerComponent::Center).Speed(0.01f)
               .Field("Radius", "Radius", &CharacterControllerComponent::Radius).Range(0.01f, 50.0f, 0.01f)
               .Field("Height", "Height", &CharacterControllerComponent::Height).Range(0.02f, 100.0f, 0.01f)
               .Field("Mass", "Push Mass", &CharacterControllerComponent::Mass).Range(1.0f, 10000.0f, 1.0f).Header("Interaction")
                   .Tooltip("How strongly the controller pushes dynamic rigidbodies.")
               .Field("PushRigidbodies", "Push Rigidbodies", &CharacterControllerComponent::PushRigidbodies)
               .Field("IsGrounded", "Is Grounded", &CharacterControllerComponent::IsGrounded).ReadOnly().Header("Runtime")
               .Field("Velocity", "Velocity", &CharacterControllerComponent::Velocity).ReadOnly()
               .Field("CollisionFlags", "Collision Flags", &CharacterControllerComponent::CollisionFlags).ReadOnly()
                   .Tooltip("1 = sides, 2 = above, 4 = below");
        }
        {
            ComponentDef<FixedJointComponent> def("Fixed Joint", "FixedJointComponent", "Physics", ICON_FA_LINK);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Locks this body to another body (or the world) so they move as one.");
            JointCommonFields(def);
        }
        {
            ComponentDef<HingeJointComponent> def("Hinge Joint", "HingeJointComponent", "Physics", ICON_FA_DOOR_OPEN);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Rotates around one axis like a door or wheel, with optional limits, spring and motor.");
            JointCommonFields(def);
            def.Field("Axis", "Axis", &HingeJointComponent::Axis).Speed(0.01f).Header("Hinge")
               .Field("UseLimits", "Use Limits", &HingeJointComponent::UseLimits)
               .Field("MinAngle", "Min Angle", &HingeJointComponent::MinAngle, FieldKind::Angle).Range(-180.0f, 0.0f, 0.5f).Format("%.1f°")
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseLimits; })
               .Field("MaxAngle", "Max Angle", &HingeJointComponent::MaxAngle, FieldKind::Angle).Range(0.0f, 180.0f, 0.5f).Format("%.1f°")
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseLimits; })
               .Field("UseSpring", "Use Spring", &HingeJointComponent::UseSpring)
               .Field("Spring", "Spring", &HingeJointComponent::Spring).Range(0.0f, 1.0e6f, 0.5f)
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseSpring; })
               .Field("Damper", "Damper", &HingeJointComponent::Damper).Range(0.0f, 1.0e6f, 0.1f)
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseSpring; })
               .Field("TargetAngle", "Target Angle", &HingeJointComponent::TargetAngle, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseSpring; })
               .Field("UseMotor", "Use Motor", &HingeJointComponent::UseMotor)
               .Field("MotorTargetVelocity", "Target Velocity", &HingeJointComponent::MotorTargetVelocity).Range(-10000.0f, 10000.0f, 1.0f).Format("%.1f °/s")
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseMotor; })
               .Field("MotorForce", "Motor Force", &HingeJointComponent::MotorForce).Range(0.0f, 1.0e7f, 1.0f)
                   .VisibleIf([](const HingeJointComponent& c) { return c.UseMotor; })
               .Field("CurrentAngle", "Current Angle", &HingeJointComponent::CurrentAngle).ReadOnly().Header("Runtime");
        }
        {
            ComponentDef<SliderJointComponent> def("Slider Joint", "SliderJointComponent", "Physics", ICON_FA_ARROWS_LEFT_RIGHT);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Slides along one axis (pistons, elevators, drawers) with optional limits and motor.");
            JointCommonFields(def);
            def.Field("Axis", "Axis", &SliderJointComponent::Axis).Speed(0.01f).Header("Slider")
               .Field("UseLimits", "Use Limits", &SliderJointComponent::UseLimits)
               .Field("MinDistance", "Min Distance", &SliderJointComponent::MinDistance).Range(-1000.0f, 0.0f, 0.01f)
                   .VisibleIf([](const SliderJointComponent& c) { return c.UseLimits; })
               .Field("MaxDistance", "Max Distance", &SliderJointComponent::MaxDistance).Range(0.0f, 1000.0f, 0.01f)
                   .VisibleIf([](const SliderJointComponent& c) { return c.UseLimits; })
               .Field("UseMotor", "Use Motor", &SliderJointComponent::UseMotor)
               .Field("MotorTargetSpeed", "Target Speed", &SliderJointComponent::MotorTargetSpeed).Range(-1000.0f, 1000.0f, 0.05f)
                   .VisibleIf([](const SliderJointComponent& c) { return c.UseMotor; })
               .Field("MotorForce", "Motor Force", &SliderJointComponent::MotorForce).Range(0.0f, 1.0e7f, 1.0f)
                   .VisibleIf([](const SliderJointComponent& c) { return c.UseMotor; })
               .Field("CurrentPosition", "Current Position", &SliderJointComponent::CurrentPosition).ReadOnly().Header("Runtime");
        }
        {
            ComponentDef<SpringJointComponent> def("Spring Joint", "SpringJointComponent", "Physics", ICON_FA_WAVE_SQUARE);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Keeps two points at a distance with a spring (ropes, bungees, suspension).");
            JointCommonFields(def);
            def.Field("AutoConfigureConnectedAnchor", "Auto Configure Connected Anchor", &SpringJointComponent::AutoConfigureConnectedAnchor).Header("Spring")
               .Field("ConnectedAnchor", "Connected Anchor", &SpringJointComponent::ConnectedAnchor).Speed(0.01f)
                   .VisibleIf([](const SpringJointComponent& c) { return !c.AutoConfigureConnectedAnchor; })
               .Field("Spring", "Spring", &SpringJointComponent::Spring).Range(0.0f, 1.0e6f, 0.5f)
               .Field("Damper", "Damper", &SpringJointComponent::Damper).Range(0.0f, 1.0e6f, 0.05f)
               .Field("MinDistance", "Min Distance", &SpringJointComponent::MinDistance).Range(0.0f, 1000.0f, 0.01f)
               .Field("MaxDistance", "Max Distance", &SpringJointComponent::MaxDistance).Range(0.0f, 1000.0f, 0.01f)
                   .Tooltip("0 = rest at the starting distance.");
        }
        {
            ComponentDef<CharacterJointComponent> def("Character Joint", "CharacterJointComponent", "Physics", ICON_FA_PERSON);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Ragdoll joint: twist around an axis plus a swing cone.");
            JointCommonFields(def);
            def.Field("Axis", "Twist Axis", &CharacterJointComponent::Axis).Speed(0.01f).Header("Limits")
               .Field("SwingAxis", "Swing Axis", &CharacterJointComponent::SwingAxis).Speed(0.01f)
               .Field("LowTwistLimit", "Low Twist Limit", &CharacterJointComponent::LowTwistLimit, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
               .Field("HighTwistLimit", "High Twist Limit", &CharacterJointComponent::HighTwistLimit, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
               .Field("Swing1Limit", "Swing 1 Limit", &CharacterJointComponent::Swing1Limit, FieldKind::Angle).Range(0.0f, 180.0f, 0.5f).Format("%.1f°")
               .Field("Swing2Limit", "Swing 2 Limit", &CharacterJointComponent::Swing2Limit, FieldKind::Angle).Range(0.0f, 180.0f, 0.5f).Format("%.1f°");
        }
        {
            ComponentDef<BallJointComponent> def("Ball Joint", "BallJointComponent", "Physics", ICON_FA_CIRCLE_DOT);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Free rotation around a shared point (chains, pendulums).");
            JointCommonFields(def);
        }
        // ── 2D ──
        const glm::vec4 k2DColor{ 0.40f, 0.85f, 0.95f, 1.0f };
        {
            ComponentDef<CapsuleCollider2DComponent> def("Capsule Collider 2D", "CapsuleCollider2DComponent", "Physics 2D", ICON_FA_CAPSULES);
            def.Color(k2DColor)
               .Field("IsTrigger", "Is Trigger", &CapsuleCollider2DComponent::IsTrigger)
               .Field("Offset", "Offset", &CapsuleCollider2DComponent::Offset).Speed(0.01f)
               .Field("Size", "Size", &CapsuleCollider2DComponent::Size).Speed(0.01f)
               .Field("Direction", "Direction", &CapsuleCollider2DComponent::Direction, FieldKind::Enum).Options({ "Vertical", "Horizontal" })
               .Field("PhysicsMaterial", "Material", &CapsuleCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d").Header("Material")
               .Field("Density", "Density", &CapsuleCollider2DComponent::Density).Range(0.0f, 1000.0f, 0.01f)
               .Field("Friction", "Friction", &CapsuleCollider2DComponent::Friction).Range(0.0f, 1.0f, 0.01f)
               .Field("Restitution", "Bounciness", &CapsuleCollider2DComponent::Restitution).Range(0.0f, 1.0f, 0.01f);
        }
        {
            ComponentDef<PolygonCollider2DComponent> def("Polygon Collider 2D", "PolygonCollider2DComponent", "Physics 2D", ICON_FA_DRAW_POLYGON);
            def.Color(k2DColor).Describe("Polygon collider. Added to a sprite it takes the sprite's Custom Physics Shape, or its opaque outline.")
               .OnAddedInEditor([](Entity e) { FitPolygonToSprite(e); })
               .Action("Reset From Sprite", [](Entity e, PolygonCollider2DComponent&) { FitPolygonToSprite(e); })
               .CustomData(
                   [](YAML::Emitter& out, PolygonCollider2DComponent& c) {
                       if (c.ExtraPaths.empty())
                           return;
                       out << YAML::Key << "ExtraPaths" << YAML::Value << YAML::BeginSeq;
                       for (const auto& path : c.ExtraPaths)
                       {
                           std::vector<float> flat;
                           for (const auto& p : path) { flat.push_back(p.x); flat.push_back(p.y); }
                           out << YAML::Flow << flat;
                       }
                       out << YAML::EndSeq;
                   },
                   [](const YAML::Node& n, PolygonCollider2DComponent& c) {
                       c.ExtraPaths.clear();
                       if (auto list = n["ExtraPaths"]; list && list.IsSequence())
                           for (const auto& path : list)
                           {
                               std::vector<glm::vec2> pts;
                               for (std::size_t i = 0; i + 1 < path.size(); i += 2)
                                   pts.push_back({ path[i].as<float>(), path[i + 1].as<float>() });
                               c.ExtraPaths.push_back(std::move(pts));
                           }
                   })
               .Field("IsTrigger", "Is Trigger", &PolygonCollider2DComponent::IsTrigger)
               .Field("Offset", "Offset", &PolygonCollider2DComponent::Offset).Speed(0.01f)
               .Field("Points", "Points", &PolygonCollider2DComponent::Points).Speed(0.01f)
               .Field("PhysicsMaterial", "Material", &PolygonCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d").Header("Material")
               .Field("Density", "Density", &PolygonCollider2DComponent::Density).Range(0.0f, 1000.0f, 0.01f)
               .Field("Friction", "Friction", &PolygonCollider2DComponent::Friction).Range(0.0f, 1.0f, 0.01f)
               .Field("Restitution", "Bounciness", &PolygonCollider2DComponent::Restitution).Range(0.0f, 1.0f, 0.01f);
        }
        {
            ComponentDef<EdgeCollider2DComponent> def("Edge Collider 2D", "EdgeCollider2DComponent", "Physics 2D", ICON_FA_BEZIER_CURVE);
            def.Color(k2DColor).Describe("Line strip (ground, slopes, terrain outlines). Always static-like; one-sided chains.")
               .Field("Offset", "Offset", &EdgeCollider2DComponent::Offset).Speed(0.01f)
               .Field("Points", "Points", &EdgeCollider2DComponent::Points).Speed(0.01f)
               .Field("Loop", "Loop", &EdgeCollider2DComponent::Loop)
               .Field("EdgeRadius", "Edge Radius", &EdgeCollider2DComponent::EdgeRadius).Range(0.0f, 10.0f, 0.005f)
               .Field("PhysicsMaterial", "Material", &EdgeCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d").Header("Material")
               .Field("Friction", "Friction", &EdgeCollider2DComponent::Friction).Range(0.0f, 1.0f, 0.01f)
               .Field("Restitution", "Bounciness", &EdgeCollider2DComponent::Restitution).Range(0.0f, 1.0f, 0.01f);
        }
#define CQ_JOINT2D_COMMON(T) \
            .Field("ConnectedBody", "Connected Rigid Body", &T::ConnectedBody) \
            .Field("EnableCollision", "Enable Collision", &T::EnableCollision) \
            .Field("AutoConfigureConnectedAnchor", "Auto Configure Connected Anchor", &T::AutoConfigureConnectedAnchor) \
            .Field("Anchor", "Anchor", &T::Anchor).Speed(0.01f) \
            .Field("ConnectedAnchor", "Connected Anchor", &T::ConnectedAnchor).Speed(0.01f) \
                .VisibleIf([](const T& c) { return !c.AutoConfigureConnectedAnchor; }) \
            .Field("BreakForce", "Break Force", &T::BreakForce).Range(0.0f, 1.0e7f, 1.0f).Header("Breaking") \
            .Field("BreakTorque", "Break Torque", &T::BreakTorque).Range(0.0f, 1.0e7f, 1.0f) \
            .Field("Broken", "Broken", &T::Broken).ReadOnly()
        ComponentDef<DistanceJoint2DComponent>("Distance Joint 2D", "DistanceJoint2DComponent", "Physics 2D", ICON_FA_RULER).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Keeps two points at a fixed distance (rods, ropes with Max Distance Only).")
            CQ_JOINT2D_COMMON(DistanceJoint2DComponent)
            .Field("AutoConfigureDistance", "Auto Configure Distance", &DistanceJoint2DComponent::AutoConfigureDistance).Header("Distance")
            .Field("Distance", "Distance", &DistanceJoint2DComponent::Distance).Range(0.0f, 1000.0f, 0.01f)
                .VisibleIf([](const DistanceJoint2DComponent& c) { return !c.AutoConfigureDistance; })
            .Field("MaxDistanceOnly", "Max Distance Only", &DistanceJoint2DComponent::MaxDistanceOnly);
        ComponentDef<SpringJoint2DComponent>("Spring Joint 2D", "SpringJoint2DComponent", "Physics 2D", ICON_FA_WAVE_SQUARE).Color(k2DColor).Requires("RigidBody2DComponent")
            CQ_JOINT2D_COMMON(SpringJoint2DComponent)
            .Field("AutoConfigureDistance", "Auto Configure Distance", &SpringJoint2DComponent::AutoConfigureDistance).Header("Spring")
            .Field("Distance", "Distance", &SpringJoint2DComponent::Distance).Range(0.0f, 1000.0f, 0.01f)
                .VisibleIf([](const SpringJoint2DComponent& c) { return !c.AutoConfigureDistance; })
            .Field("Frequency", "Frequency", &SpringJoint2DComponent::Frequency).Range(0.0f, 100.0f, 0.05f).Format("%.2f Hz")
            .Field("DampingRatio", "Damping Ratio", &SpringJoint2DComponent::DampingRatio).Range(0.0f, 1.0f, 0.01f);
        ComponentDef<HingeJoint2DComponent>("Hinge Joint 2D", "HingeJoint2DComponent", "Physics 2D", ICON_FA_DOOR_OPEN).Color(k2DColor).Requires("RigidBody2DComponent")
            CQ_JOINT2D_COMMON(HingeJoint2DComponent)
            .Field("UseMotor", "Use Motor", &HingeJoint2DComponent::UseMotor).Header("Motor")
            .Field("MotorSpeed", "Motor Speed", &HingeJoint2DComponent::MotorSpeed).Range(-100000.0f, 100000.0f, 1.0f).Format("%.1f °/s")
                .VisibleIf([](const HingeJoint2DComponent& c) { return c.UseMotor; })
            .Field("MaxMotorTorque", "Maximum Motor Force", &HingeJoint2DComponent::MaxMotorTorque).Range(0.0f, 1.0e7f, 1.0f)
                .VisibleIf([](const HingeJoint2DComponent& c) { return c.UseMotor; })
            .Field("UseLimits", "Use Limits", &HingeJoint2DComponent::UseLimits).Header("Angle Limits")
            .Field("LowerAngle", "Lower Angle", &HingeJoint2DComponent::LowerAngle, FieldKind::Angle).Range(-359.0f, 359.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const HingeJoint2DComponent& c) { return c.UseLimits; })
            .Field("UpperAngle", "Upper Angle", &HingeJoint2DComponent::UpperAngle, FieldKind::Angle).Range(-359.0f, 359.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const HingeJoint2DComponent& c) { return c.UseLimits; })
            .Field("CurrentAngle", "Joint Angle", &HingeJoint2DComponent::CurrentAngle).ReadOnly().Header("Runtime");
        ComponentDef<SliderJoint2DComponent>("Slider Joint 2D", "SliderJoint2DComponent", "Physics 2D", ICON_FA_ARROWS_LEFT_RIGHT).Color(k2DColor).Requires("RigidBody2DComponent")
            CQ_JOINT2D_COMMON(SliderJoint2DComponent)
            .Field("Angle", "Angle", &SliderJoint2DComponent::Angle, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°").Header("Slider")
            .Field("UseMotor", "Use Motor", &SliderJoint2DComponent::UseMotor)
            .Field("MotorSpeed", "Motor Speed", &SliderJoint2DComponent::MotorSpeed).Range(-10000.0f, 10000.0f, 0.05f)
                .VisibleIf([](const SliderJoint2DComponent& c) { return c.UseMotor; })
            .Field("MaxMotorForce", "Maximum Motor Force", &SliderJoint2DComponent::MaxMotorForce).Range(0.0f, 1.0e7f, 1.0f)
                .VisibleIf([](const SliderJoint2DComponent& c) { return c.UseMotor; })
            .Field("UseLimits", "Use Limits", &SliderJoint2DComponent::UseLimits)
            .Field("LowerTranslation", "Lower Translation", &SliderJoint2DComponent::LowerTranslation).Range(-1000.0f, 1000.0f, 0.01f)
                .VisibleIf([](const SliderJoint2DComponent& c) { return c.UseLimits; })
            .Field("UpperTranslation", "Upper Translation", &SliderJoint2DComponent::UpperTranslation).Range(-1000.0f, 1000.0f, 0.01f)
                .VisibleIf([](const SliderJoint2DComponent& c) { return c.UseLimits; })
            .Field("CurrentTranslation", "Joint Translation", &SliderJoint2DComponent::CurrentTranslation).ReadOnly().Header("Runtime");
        ComponentDef<WheelJoint2DComponent>("Wheel Joint 2D", "WheelJoint2DComponent", "Physics 2D", ICON_FA_CIRCLE_NOTCH).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Vehicle wheel: suspension spring along an axis plus a motor.")
            CQ_JOINT2D_COMMON(WheelJoint2DComponent)
            .Field("Angle", "Suspension Angle", &WheelJoint2DComponent::Angle, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°").Header("Suspension")
            .Field("SuspensionFrequency", "Frequency", &WheelJoint2DComponent::SuspensionFrequency).Range(0.0f, 100.0f, 0.05f).Format("%.2f Hz")
            .Field("SuspensionDamping", "Damping Ratio", &WheelJoint2DComponent::SuspensionDamping).Range(0.0f, 1.0f, 0.01f)
            .Field("UseMotor", "Use Motor", &WheelJoint2DComponent::UseMotor).Header("Motor")
            .Field("MotorSpeed", "Motor Speed", &WheelJoint2DComponent::MotorSpeed).Range(-100000.0f, 100000.0f, 1.0f).Format("%.1f °/s")
                .VisibleIf([](const WheelJoint2DComponent& c) { return c.UseMotor; })
            .Field("MaxMotorTorque", "Maximum Motor Force", &WheelJoint2DComponent::MaxMotorTorque).Range(0.0f, 1.0e7f, 1.0f)
                .VisibleIf([](const WheelJoint2DComponent& c) { return c.UseMotor; });
        ComponentDef<FixedJoint2DComponent>("Fixed Joint 2D", "FixedJoint2DComponent", "Physics 2D", ICON_FA_LINK).Color(k2DColor).Requires("RigidBody2DComponent")
            CQ_JOINT2D_COMMON(FixedJoint2DComponent)
            .Field("Frequency", "Frequency", &FixedJoint2DComponent::Frequency).Range(0.0f, 100.0f, 0.05f).Format("%.2f Hz").Tooltip("0 = rigid weld").Header("Softness")
            .Field("DampingRatio", "Damping Ratio", &FixedJoint2DComponent::DampingRatio).Range(0.0f, 1.0f, 0.01f);
        ComponentDef<FrictionJoint2DComponent>("Friction Joint 2D", "FrictionJoint2DComponent", "Physics 2D", ICON_FA_HAND).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Slows relative motion between two bodies (or a body and the world) up to a force/torque limit: top-down friction.")
            CQ_JOINT2D_COMMON(FrictionJoint2DComponent)
            .Field("MaxForce", "Max Force", &FrictionJoint2DComponent::MaxForce).Range(0.0f, 1.0e7f, 0.1f).Header("Friction")
            .Field("MaxTorque", "Max Torque", &FrictionJoint2DComponent::MaxTorque).Range(0.0f, 1.0e7f, 0.1f);

        ComponentDef<RelativeJoint2DComponent>("Relative Joint 2D", "RelativeJoint2DComponent", "Physics 2D", ICON_FA_ARROWS_TO_CIRCLE).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Keeps a body at a linear and angular offset from another body using a limited force (followers, carried items).")
            CQ_JOINT2D_COMMON(RelativeJoint2DComponent)
            .Field("MaxForce", "Max Force", &RelativeJoint2DComponent::MaxForce).Range(0.0f, 1.0e7f, 1.0f).Header("Offset")
            .Field("MaxTorque", "Max Torque", &RelativeJoint2DComponent::MaxTorque).Range(0.0f, 1.0e7f, 1.0f)
            .Field("CorrectionScale", "Correction Scale", &RelativeJoint2DComponent::CorrectionScale).Range(0.0f, 1.0f, 0.01f)
            .Field("AutoConfigureOffset", "Auto Configure Offset", &RelativeJoint2DComponent::AutoConfigureOffset)
            .Field("LinearOffset", "Linear Offset", &RelativeJoint2DComponent::LinearOffset).Speed(0.01f)
                .VisibleIf([](const RelativeJoint2DComponent& c) { return !c.AutoConfigureOffset; })
            .Field("AngularOffset", "Angular Offset", &RelativeJoint2DComponent::AngularOffset, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const RelativeJoint2DComponent& c) { return !c.AutoConfigureOffset; });

        ComponentDef<TargetJoint2DComponent>("Target Joint 2D", "TargetJoint2DComponent", "Physics 2D", ICON_FA_CROSSHAIRS).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Pulls a point of the body towards a world-space target with a spring (mouse dragging, grappling).")
            .Field("Anchor", "Anchor", &TargetJoint2DComponent::Anchor).Speed(0.01f)
            .Field("AutoConfigureTarget", "Auto Configure Target", &TargetJoint2DComponent::AutoConfigureTarget)
            .Field("Target", "Target", &TargetJoint2DComponent::Target).Speed(0.01f)
                .Tooltip("World-space point the anchor is pulled towards. Can be changed every frame from scripts.")
            .Field("MaxForce", "Max Force", &TargetJoint2DComponent::MaxForce).Range(0.0f, 1.0e7f, 1.0f).Header("Spring")
            .Field("DampingRatio", "Damping Ratio", &TargetJoint2DComponent::DampingRatio).Range(0.0f, 1.0f, 0.01f)
            .Field("Frequency", "Frequency", &TargetJoint2DComponent::Frequency).Range(0.0f, 100.0f, 0.05f).Format("%.2f Hz")
            .Field("BreakForce", "Break Force", &TargetJoint2DComponent::BreakForce).Range(0.0f, 1.0e7f, 1.0f).Header("Breaking")
            .Field("Broken", "Broken", &TargetJoint2DComponent::Broken).ReadOnly();

        ComponentDef<ConstantForce2DComponent>("Constant Force 2D", "ConstantForce2DComponent", "Physics 2D", ICON_FA_ROCKET).Color(k2DColor).Requires("RigidBody2DComponent")
            .Describe("Applies a continuous 2D force and torque every physics step.")
            .Field("Force", "Force", &ConstantForce2DComponent::Force).Speed(0.1f).Tooltip("World-space force (N).")
            .Field("RelativeForce", "Relative Force", &ConstantForce2DComponent::RelativeForce).Speed(0.1f).Tooltip("Force in the body's local space.")
            .Field("Torque", "Torque", &ConstantForce2DComponent::Torque).Speed(0.1f);
#undef CQ_JOINT2D_COMMON

        ComponentDef<ClothComponent>("Cloth", "ClothComponent", "Physics", ICON_FA_FLAG)
            .Describe("Simulated cloth (flags, curtains, capes, table cloths): a grid that collides with the physics world, hangs from its pinned edge "
                      "(which follows this object) and flutters in Wind Zones. Replaces the Mesh Renderer's mesh.")
            .Requires("MeshRendererComponent")
            .Field("Size", "Size", &ClothComponent::Size).Range(0.01f, 1000.0f, 0.01f)
            .Field("Resolution", "Resolution", &ClothComponent::Resolution).Range(2.0f, 128.0f, 1.0f)
            .Field("Orientation", "Orientation", &ClothComponent::Orientation).Options({ "Vertical (XY)", "Horizontal (XZ)" })
            .Field("Pin", "Pinned Vertices", &ClothComponent::Pin).Options({ "None", "Top Edge", "Top Corners", "Left Edge", "All Corners" })
            .Field("StretchingStiffness", "Stretching Stiffness", &ClothComponent::StretchingStiffness).Range(0.0f, 1.0f, 0.01f).Header("Material")
            .Field("BendingStiffness", "Bending Stiffness", &ClothComponent::BendingStiffness).Range(0.0f, 1.0f, 0.01f)
            .Field("Damping", "Damping", &ClothComponent::Damping).Range(0.0f, 10.0f, 0.01f)
            .Field("Friction", "Friction", &ClothComponent::Friction).Range(0.0f, 1.0f, 0.01f)
            .Field("Thickness", "Thickness", &ClothComponent::Thickness).Range(0.0f, 1.0f, 0.001f)
            .Field("GravityScale", "Gravity Scale", &ClothComponent::GravityScale).Range(-10.0f, 10.0f, 0.01f)
            .Field("SolverIterations", "Solver Iterations", &ClothComponent::SolverIterations).Range(1.0f, 64.0f, 1.0f)
            .Field("WindInfluence", "Wind Influence", &ClothComponent::WindInfluence).Range(0.0f, 10.0f, 0.01f);

        ComponentDef<CompositeCollider2DComponent>("Composite Collider 2D", "CompositeCollider2DComponent", "Physics 2D", ICON_FA_OBJECT_GROUP).Color(k2DColor)
            .Describe("Merges the 2D colliders on this object and its children (box, circle, capsule, polygon, tilemap) into one shape. "
                      "Outlines: smooth one-sided chains, no snagging on tile seams. Polygons: solid boxes for triggers and dynamic bodies.")
            .Field("GeometryType", "Geometry Type", &CompositeCollider2DComponent::GeometryType).Options({ "Outlines", "Polygons" })
            .Field("VertexDistance", "Vertex Distance", &CompositeCollider2DComponent::VertexDistance).Range(0.005f, 1.0f, 0.001f)
                .Tooltip("Sampling step for non-rectangular shapes. Axis-aligned boxes and tiles are merged exactly.")
            .Field("PhysicsMaterial", "Material", &CompositeCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d")
            .Field("Density", "Density", &CompositeCollider2DComponent::Density).Range(0.0f, 1000.0f, 0.01f)
            .Field("Friction", "Friction", &CompositeCollider2DComponent::Friction).Range(0.0f, 10.0f, 0.01f)
            .Field("Restitution", "Bounciness", &CompositeCollider2DComponent::Restitution).Range(0.0f, 1.0f, 0.01f)
            .Field("IsTrigger", "Is Trigger", &CompositeCollider2DComponent::IsTrigger)
            .Field("OutlineCount", "Outlines", &CompositeCollider2DComponent::OutlineCount).ReadOnly()
            .Field("ShapeCount", "Shapes", &CompositeCollider2DComponent::ShapeCount).ReadOnly();

        ComponentDef<PlatformEffector2DComponent>("Platform Effector 2D", "PlatformEffector2DComponent", "Physics 2D", ICON_FA_GRIP_LINES).Color(k2DColor)
            .Describe("One-way platform: bodies pass through from below and land on top.")
            .Field("UseOneWay", "Use One Way", &PlatformEffector2DComponent::UseOneWay)
            .Field("SurfaceArc", "Surface Arc", &PlatformEffector2DComponent::SurfaceArc, FieldKind::Angle).Range(1.0f, 360.0f, 1.0f).Format("%.0f°")
            .Field("RotationalOffset", "Rotational Offset", &PlatformEffector2DComponent::RotationalOffset, FieldKind::Angle).Range(-360.0f, 360.0f, 1.0f).Format("%.0f°");
        ComponentDef<AreaEffector2DComponent>("Area Effector 2D", "AreaEffector2DComponent", "Physics 2D", ICON_FA_WIND).Color(k2DColor)
            .Describe("Applies a force to bodies inside this entity's trigger collider (wind, currents).")
            .Field("UseGlobalAngle", "Use Global Angle", &AreaEffector2DComponent::UseGlobalAngle)
            .Field("ForceAngle", "Force Angle", &AreaEffector2DComponent::ForceAngle, FieldKind::Angle).Range(-360.0f, 360.0f, 1.0f).Format("%.0f°")
            .Field("ForceMagnitude", "Force Magnitude", &AreaEffector2DComponent::ForceMagnitude).Range(-100000.0f, 100000.0f, 0.1f)
            .Field("Drag", "Drag", &AreaEffector2DComponent::Drag).Range(0.0f, 1000.0f, 0.01f).Header("Damping")
            .Field("AngularDrag", "Angular Drag", &AreaEffector2DComponent::AngularDrag).Range(0.0f, 1000.0f, 0.01f);
        ComponentDef<PointEffector2DComponent>("Point Effector 2D", "PointEffector2DComponent", "Physics 2D", ICON_FA_MAGNET).Color(k2DColor)
            .Describe("Attracts (negative) or repels (positive) bodies inside this entity's trigger collider.")
            .Field("ForceMagnitude", "Force Magnitude", &PointEffector2DComponent::ForceMagnitude).Range(-100000.0f, 100000.0f, 0.1f)
            .Field("DistanceScale", "Distance Scale", &PointEffector2DComponent::DistanceScale).Range(0.001f, 1000.0f, 0.01f)
            .Field("ForceMode", "Force Mode", &PointEffector2DComponent::ForceMode, FieldKind::Enum).Options({ "Constant", "Inverse Linear", "Inverse Squared" })
            .Field("Drag", "Drag", &PointEffector2DComponent::Drag).Range(0.0f, 1000.0f, 0.01f).Header("Damping")
            .Field("AngularDrag", "Angular Drag", &PointEffector2DComponent::AngularDrag).Range(0.0f, 1000.0f, 0.01f);
        ComponentDef<SurfaceEffector2DComponent>("Surface Effector 2D", "SurfaceEffector2DComponent", "Physics 2D", ICON_FA_ARROWS_TURN_RIGHT).Color(k2DColor)
            .Describe("Conveyor belt: moves bodies along the collider's surface.")
            .Field("Speed", "Speed", &SurfaceEffector2DComponent::Speed).Range(-1000.0f, 1000.0f, 0.05f);
        ComponentDef<BuoyancyEffector2DComponent>("Buoyancy Effector 2D", "BuoyancyEffector2DComponent", "Physics 2D", ICON_FA_WATER).Color(k2DColor)
            .Describe("Fluid volume: floats bodies inside this entity's trigger collider.")
            .Field("SurfaceLevel", "Surface Level", &BuoyancyEffector2DComponent::SurfaceLevel).Speed(0.01f)
            .Field("Density", "Density", &BuoyancyEffector2DComponent::Density).Range(0.0f, 1000.0f, 0.01f)
            .Field("LinearDrag", "Linear Drag", &BuoyancyEffector2DComponent::LinearDrag).Range(0.0f, 1000.0f, 0.01f).Header("Damping")
            .Field("AngularDrag", "Angular Drag", &BuoyancyEffector2DComponent::AngularDrag).Range(0.0f, 1000.0f, 0.01f)
            .Field("FlowAngle", "Flow Angle", &BuoyancyEffector2DComponent::FlowAngle, FieldKind::Angle).Range(-360.0f, 360.0f, 1.0f).Format("%.0f°").Header("Flow")
            .Field("FlowMagnitude", "Flow Magnitude", &BuoyancyEffector2DComponent::FlowMagnitude).Range(-10000.0f, 10000.0f, 0.1f);

        {
            ComponentDef<ConstantForceComponent> def("Constant Force", "ConstantForceComponent", "Physics", ICON_FA_ROCKET);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Applies a continuous force and torque every physics step.")
               .Field("Force", "Force", &ConstantForceComponent::Force).Speed(0.1f).Tooltip("World-space force (N).")
               .Field("RelativeForce", "Relative Force", &ConstantForceComponent::RelativeForce).Speed(0.1f).Tooltip("Force in the body's local space.")
               .Field("Torque", "Torque", &ConstantForceComponent::Torque).Speed(0.1f)
               .Field("RelativeTorque", "Relative Torque", &ConstantForceComponent::RelativeTorque).Speed(0.1f);
        }
        {
            using C = ConfigurableJointComponent;
            ComponentDef<C> def("Configurable Joint", "ConfigurableJointComponent", "Physics", ICON_FA_SLIDERS);
            def.Color(kPhysicsColor).Requires("RigidbodyComponent")
               .Describe("Fully configurable joint: lock, limit or free each of the 3 linear and 3 angular axes; drives pull towards a target position and rotation.");
            JointCommonFields(def);
            auto limited = [](JointMotion m) { return m == JointMotion::Limited; };
            const std::vector<std::string> motions{ "Locked", "Limited", "Free" };
            def.Field("Axis", "Axis", &C::Axis).Speed(0.01f).Header("Axes")
                   .Tooltip("Primary axis (X of the joint frame), in this entity's local space.")
               .Field("SecondaryAxis", "Secondary Axis", &C::SecondaryAxis).Speed(0.01f)
                   .Tooltip("Y of the joint frame; Z = Axis × Secondary Axis.")
               .Field("XMotion", "X Motion", &C::XMotion).Options(motions).Header("Linear Motion")
               .Field("YMotion", "Y Motion", &C::YMotion).Options(motions)
               .Field("ZMotion", "Z Motion", &C::ZMotion).Options(motions)
               .Field("LinearLimit", "Limit", &C::LinearLimit).Range(0.0f, 1000.0f, 0.01f)
                   .VisibleIf([=](const C& c) { return limited(c.XMotion) || limited(c.YMotion) || limited(c.ZMotion); })
               .Field("LinearLimitSpring", "Limit Spring", &C::LinearLimitSpring).Range(0.0f, 1.0e7f, 1.0f)
                   .Tooltip("0 = hard limit; otherwise the limit is soft (spring).")
                   .VisibleIf([=](const C& c) { return limited(c.XMotion) || limited(c.YMotion) || limited(c.ZMotion); })
               .Field("LinearLimitDamper", "Limit Damper", &C::LinearLimitDamper).Range(0.0f, 1.0e6f, 0.1f)
                   .VisibleIf([=](const C& c) { return c.LinearLimitSpring > 0.0f; })
               .Field("AngularXMotion", "Angular X Motion", &C::AngularXMotion).Options(motions).Header("Angular Motion")
               .Field("AngularYMotion", "Angular Y Motion", &C::AngularYMotion).Options(motions)
               .Field("AngularZMotion", "Angular Z Motion", &C::AngularZMotion).Options(motions)
               .Field("LowAngularXLimit", "Low Angular X Limit", &C::LowAngularXLimit, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
                   .VisibleIf([=](const C& c) { return limited(c.AngularXMotion); })
               .Field("HighAngularXLimit", "High Angular X Limit", &C::HighAngularXLimit, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
                   .VisibleIf([=](const C& c) { return limited(c.AngularXMotion); })
               .Field("AngularYLimit", "Angular Y Limit", &C::AngularYLimit, FieldKind::Angle).Range(0.0f, 90.0f, 0.5f).Format("±%.1f°")
                   .VisibleIf([=](const C& c) { return limited(c.AngularYMotion); })
               .Field("AngularZLimit", "Angular Z Limit", &C::AngularZLimit, FieldKind::Angle).Range(0.0f, 90.0f, 0.5f).Format("±%.1f°")
                   .VisibleIf([=](const C& c) { return limited(c.AngularZMotion); })
               .Field("TargetPosition", "Target Position", &C::TargetPosition).Speed(0.01f).Header("Linear Drive")
               .Field("TargetVelocity", "Target Velocity", &C::TargetVelocity).Speed(0.01f)
               .Field("XDriveSpring", "X Spring", &C::XDriveSpring).Range(0.0f, 1.0e7f, 1.0f)
               .Field("XDriveDamper", "X Damper", &C::XDriveDamper).Range(0.0f, 1.0e6f, 0.1f)
               .Field("XDriveMaxForce", "X Max Force", &C::XDriveMaxForce).Range(0.0f, 1.0e9f, 10.0f)
               .Field("YDriveSpring", "Y Spring", &C::YDriveSpring).Range(0.0f, 1.0e7f, 1.0f)
               .Field("YDriveDamper", "Y Damper", &C::YDriveDamper).Range(0.0f, 1.0e6f, 0.1f)
               .Field("YDriveMaxForce", "Y Max Force", &C::YDriveMaxForce).Range(0.0f, 1.0e9f, 10.0f)
               .Field("ZDriveSpring", "Z Spring", &C::ZDriveSpring).Range(0.0f, 1.0e7f, 1.0f)
               .Field("ZDriveDamper", "Z Damper", &C::ZDriveDamper).Range(0.0f, 1.0e6f, 0.1f)
               .Field("ZDriveMaxForce", "Z Max Force", &C::ZDriveMaxForce).Range(0.0f, 1.0e9f, 10.0f)
               .Field("TargetRotation", "Target Rotation", &C::TargetRotation).Speed(0.5f).Header("Angular Drive")
                   .Tooltip("Target rotation (degrees) relative to the starting pose.")
               .Field("TargetAngularVelocity", "Target Angular Velocity", &C::TargetAngularVelocity).Speed(0.5f)
               .Field("AngularXDriveSpring", "Angular X Spring", &C::AngularXDriveSpring).Range(0.0f, 1.0e7f, 1.0f)
               .Field("AngularXDriveDamper", "Angular X Damper", &C::AngularXDriveDamper).Range(0.0f, 1.0e6f, 0.1f)
               .Field("AngularXDriveMaxForce", "Angular X Max Torque", &C::AngularXDriveMaxForce).Range(0.0f, 1.0e9f, 10.0f)
               .Field("AngularYZDriveSpring", "Angular YZ Spring", &C::AngularYZDriveSpring).Range(0.0f, 1.0e7f, 1.0f)
               .Field("AngularYZDriveDamper", "Angular YZ Damper", &C::AngularYZDriveDamper).Range(0.0f, 1.0e6f, 0.1f)
               .Field("AngularYZDriveMaxForce", "Angular YZ Max Torque", &C::AngularYZDriveMaxForce).Range(0.0f, 1.0e9f, 10.0f)
               .Field("CurrentPosition", "Current Position", &C::CurrentPosition).ReadOnly().Header("Runtime")
               .Field("CurrentRotation", "Current Rotation", &C::CurrentRotation).ReadOnly();
        }
        {
            using W = WheelColliderComponent;
            ComponentDef<W> def("Wheel Collider", "WheelColliderComponent", "Physics", ICON_FA_CIRCLE_NOTCH);
            def.Color(kPhysicsColor)
               .Describe("Vehicle wheel: raycast suspension and tyre friction. Put it on a child of a Rigidbody; drive it with Motor Torque, Brake Torque and Steer Angle.")
               .Field("Mass", "Mass", &W::Mass).Range(0.1f, 10000.0f, 0.5f).Tooltip("Wheel mass (kg); sets its spin inertia.")
               .Field("Radius", "Radius", &W::Radius).Range(0.01f, 20.0f, 0.005f)
               .Field("WheelDampingRate", "Wheel Damping Rate", &W::WheelDampingRate).Range(0.0f, 100.0f, 0.01f)
               .Field("SuspensionDistance", "Suspension Distance", &W::SuspensionDistance).Range(0.0f, 10.0f, 0.005f)
               .Field("ForceAppPointDistance", "Force App Point Distance", &W::ForceAppPointDistance).Range(0.0f, 10.0f, 0.005f)
                   .Tooltip("Height above the wheel centre where forces are applied (lower = less body roll).")
               .Field("Center", "Center", &W::Center).Speed(0.01f)
               .Field("Spring", "Spring", &W::Spring).Range(0.0f, 1.0e7f, 50.0f).Header("Suspension Spring")
               .Field("Damper", "Damper", &W::Damper).Range(0.0f, 1.0e6f, 10.0f)
               .Field("TargetPosition", "Target Position", &W::TargetPosition).Range(0.0f, 1.0f, 0.01f)
                   .Tooltip("Rest point along the travel: 0 = fully extended, 1 = fully compressed.")
               .Field("FwdExtremumSlip", "Extremum Slip", &W::FwdExtremumSlip).Range(0.001f, 10.0f, 0.01f).Header("Forward Friction")
               .Field("FwdExtremumValue", "Extremum Value", &W::FwdExtremumValue).Range(0.0f, 10.0f, 0.01f)
               .Field("FwdAsymptoteSlip", "Asymptote Slip", &W::FwdAsymptoteSlip).Range(0.001f, 20.0f, 0.01f)
               .Field("FwdAsymptoteValue", "Asymptote Value", &W::FwdAsymptoteValue).Range(0.0f, 10.0f, 0.01f)
               .Field("FwdStiffness", "Stiffness", &W::FwdStiffness).Range(0.0f, 10.0f, 0.01f)
               .Field("SideExtremumSlip", "Extremum Slip", &W::SideExtremumSlip).Range(0.001f, 10.0f, 0.01f).Header("Sideways Friction")
               .Field("SideExtremumValue", "Extremum Value", &W::SideExtremumValue).Range(0.0f, 10.0f, 0.01f)
               .Field("SideAsymptoteSlip", "Asymptote Slip", &W::SideAsymptoteSlip).Range(0.001f, 20.0f, 0.01f)
               .Field("SideAsymptoteValue", "Asymptote Value", &W::SideAsymptoteValue).Range(0.0f, 10.0f, 0.01f)
               .Field("SideStiffness", "Stiffness", &W::SideStiffness).Range(0.0f, 10.0f, 0.01f)
               .Field("MotorTorque", "Motor Torque", &W::MotorTorque).Range(-1.0e6f, 1.0e6f, 1.0f).Header("Drive")
               .Field("BrakeTorque", "Brake Torque", &W::BrakeTorque).Range(0.0f, 1.0e6f, 1.0f)
               .Field("SteerAngle", "Steer Angle", &W::SteerAngle, FieldKind::Angle).Range(-90.0f, 90.0f, 0.5f).Format("%.1f°")
               .Field("WheelModel", "Wheel Model", &W::WheelModel)
                   .Tooltip("Visual wheel mesh posed every frame (suspension travel, steering and spin).")
               .Field("IsGrounded", "Is Grounded", &W::IsGrounded).ReadOnly().Header("Runtime")
               .Field("RPM", "RPM", &W::RPM).ReadOnly()
               .Field("Compression", "Compression", &W::Compression).ReadOnly()
               .Field("ForwardSlip", "Forward Slip", &W::ForwardSlip).ReadOnly()
               .Field("SidewaysSlip", "Sideways Slip", &W::SidewaysSlip).ReadOnly()
               .Field("SuspensionForce", "Suspension Force", &W::SuspensionForce).ReadOnly()
               .Field("HitEntity", "Hit Entity", &W::HitEntity).ReadOnly();
        }
    }
}
