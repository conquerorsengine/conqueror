#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/GameplayComponents.h"

#include <IconsFontAwesome6.h>

namespace Conqueror
{
    namespace
    {
        const glm::vec4 kRenderColor{ 0.62f, 0.72f, 1.0f, 1.0f };
        const glm::vec4 kConstraintColor{ 0.95f, 0.55f, 0.75f, 1.0f };
        const glm::vec4 kGameplayColor{ 1.0f, 0.78f, 0.35f, 1.0f };
    }

    void RegisterGameplayComponents()
    {
        // ── Çizim ──
        ComponentDef<LineRendererComponent>("Line Renderer", "LineRendererComponent", "Effects", ICON_FA_PEN_NIB).Color(kRenderColor)
            .Describe("Draws a camera-facing ribbon through a list of points (lasers, ropes, paths).")
            .Field("Positions", "Positions", &LineRendererComponent::Positions).Speed(0.01f)
            .Field("UseWorldSpace", "Use World Space", &LineRendererComponent::UseWorldSpace)
            .Field("Loop", "Loop", &LineRendererComponent::Loop)
            .Field("StartWidth", "Start Width", &LineRendererComponent::StartWidth).Range(0.0f, 1000.0f, 0.005f).Header("Appearance")
            .Field("EndWidth", "End Width", &LineRendererComponent::EndWidth).Range(0.0f, 1000.0f, 0.005f)
            .Field("StartColor", "Start Color", &LineRendererComponent::StartColor, FieldKind::Color4)
            .Field("EndColor", "End Color", &LineRendererComponent::EndColor, FieldKind::Color4)
            .Field("Alignment", "Alignment", &LineRendererComponent::Alignment).Options({ "View", "Transform Z" })
            .Field("TextureMode", "Texture Mode", &LineRendererComponent::TextureMode).Options({ "Stretch", "Tile" })
            .Field("Blend", "Blend", &LineRendererComponent::Blend).Options({ "Alpha", "Additive" })
            .Field("Texture", "Texture", &LineRendererComponent::Texture, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga")
            .Field("Emissive", "Emission", &LineRendererComponent::Emissive).Range(0.0f, 100.0f, 0.05f);

        ComponentDef<TrailRendererComponent>("Trail Renderer", "TrailRendererComponent", "Effects", ICON_FA_WIND).Color(kRenderColor)
            .Describe("Leaves a fading ribbon behind a moving object.")
            .Field("Time", "Time", &TrailRendererComponent::Time).Range(0.01f, 60.0f, 0.01f).Format("%.2f s")
            .Field("MinVertexDistance", "Min Vertex Distance", &TrailRendererComponent::MinVertexDistance).Range(0.001f, 100.0f, 0.005f)
            .Field("Emitting", "Emitting", &TrailRendererComponent::Emitting)
            .Field("StartWidth", "Start Width", &TrailRendererComponent::StartWidth).Range(0.0f, 1000.0f, 0.005f).Header("Appearance")
            .Field("EndWidth", "End Width", &TrailRendererComponent::EndWidth).Range(0.0f, 1000.0f, 0.005f)
            .Field("StartColor", "Start Color", &TrailRendererComponent::StartColor, FieldKind::Color4)
            .Field("EndColor", "End Color", &TrailRendererComponent::EndColor, FieldKind::Color4)
            .Field("TextureMode", "Texture Mode", &TrailRendererComponent::TextureMode).Options({ "Stretch", "Tile" })
            .Field("Blend", "Blend", &TrailRendererComponent::Blend).Options({ "Alpha", "Additive" })
            .Field("Texture", "Texture", &TrailRendererComponent::Texture, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga")
            .Field("Emissive", "Emission", &TrailRendererComponent::Emissive).Range(0.0f, 100.0f, 0.05f);

        ComponentDef<LODGroupComponent>("LOD Group", "LODGroupComponent", "Rendering", ICON_FA_LAYER_GROUP).Color(kRenderColor)
            .Describe("Switches between detail levels (child entities) by on-screen size.")
            .Field("LOD0", "LOD 0", &LODGroupComponent::LOD0).Header("Levels")
            .Field("Threshold0", "LOD 0 Screen %", &LODGroupComponent::Threshold0).Range(0.0f, 100.0f, 0.1f).Format("%.1f %%")
            .Field("LOD1", "LOD 1", &LODGroupComponent::LOD1)
            .Field("Threshold1", "LOD 1 Screen %", &LODGroupComponent::Threshold1).Range(0.0f, 100.0f, 0.1f).Format("%.1f %%")
            .Field("LOD2", "LOD 2", &LODGroupComponent::LOD2)
            .Field("Threshold2", "LOD 2 Screen %", &LODGroupComponent::Threshold2).Range(0.0f, 100.0f, 0.1f).Format("%.1f %%")
            .Field("LOD3", "LOD 3", &LODGroupComponent::LOD3)
            .Field("Threshold3", "LOD 3 Screen %", &LODGroupComponent::Threshold3).Range(0.0f, 100.0f, 0.1f).Format("%.1f %%")
            .Field("Size", "Object Size", &LODGroupComponent::Size).Range(0.001f, 100000.0f, 0.05f).Header("Settings")
            .Field("CullBelowLast", "Cull Below Last", &LODGroupComponent::CullBelowLast)
            .Field("CurrentLOD", "Current LOD", &LODGroupComponent::CurrentLOD).ReadOnly();

        ComponentDef<SortingGroupComponent>("Sorting Group", "SortingGroupComponent", "Rendering", ICON_FA_ARROW_DOWN_WIDE_SHORT).Color(kRenderColor)
            .Describe("Sorts this 2D object and its children together.")
            .Field("SortingLayer", "Sorting Layer", &SortingGroupComponent::SortingLayer).Range(0.0f, 31.0f, 1.0f)
            .Field("OrderInLayer", "Order In Layer", &SortingGroupComponent::OrderInLayer).Range(-32768.0f, 32767.0f, 1.0f);

        ComponentDef<PrefabInstanceComponent>("Prefab Instance", "PrefabInstanceComponent", "Prefab", ICON_FA_CUBES)
            .Color({ 0.45f, 0.7f, 1.0f, 1.0f }).HiddenFromMenu()
            .Describe("Root of a prefab instance. Apply writes changes back to the prefab asset; Revert restores it.")
            .Field("Prefab", "Prefab Asset", &PrefabInstanceComponent::Prefab, FieldKind::Asset).Filter(".cqprefab");

        // ── Kısıtlar (Unity Animation Constraints) ──
#define CQ_CONSTRAINT_COMMON(T) \
            .Field("IsActive", "Is Active", &T::IsActive) \
            .Field("Weight", "Weight", &T::Weight).Range(0.0f, 1.0f, 0.01f) \
            .Field("Source", "Source", &T::Source).Tooltip("Entity this constraint follows.")
        ComponentDef<LookAtConstraintComponent>("Look At Constraint", "LookAtConstraintComponent", "Constraints", ICON_FA_EYE).Color(kConstraintColor)
            .Describe("Rotates to face the source (its forward axis is -Z).")
            CQ_CONSTRAINT_COMMON(LookAtConstraintComponent)
            .Field("Roll", "Roll", &LookAtConstraintComponent::Roll, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
            .Field("RotationOffset", "Rotation Offset", &LookAtConstraintComponent::RotationOffset).Speed(0.5f)
            .Field("UseWorldUp", "Use World Up", &LookAtConstraintComponent::UseWorldUp);
        ComponentDef<AimConstraintComponent>("Aim Constraint", "AimConstraintComponent", "Constraints", ICON_FA_CROSSHAIRS).Color(kConstraintColor)
            .Describe("Points a chosen local axis at the source.")
            CQ_CONSTRAINT_COMMON(AimConstraintComponent)
            .Field("AimVector", "Aim Vector", &AimConstraintComponent::AimVector).Speed(0.01f)
            .Field("UpVector", "Up Vector", &AimConstraintComponent::UpVector).Speed(0.01f)
            .Field("WorldUpVector", "World Up Vector", &AimConstraintComponent::WorldUpVector).Speed(0.01f)
            .Field("RotationOffset", "Rotation Offset", &AimConstraintComponent::RotationOffset).Speed(0.5f);
        ComponentDef<PositionConstraintComponent>("Position Constraint", "PositionConstraintComponent", "Constraints", ICON_FA_UP_DOWN_LEFT_RIGHT).Color(kConstraintColor)
            .Describe("Follows the source's position.")
            CQ_CONSTRAINT_COMMON(PositionConstraintComponent)
            .Field("Offset", "Offset", &PositionConstraintComponent::Offset).Speed(0.01f)
            .Field("FreezeX", "Freeze X", &PositionConstraintComponent::FreezeX).Header("Constraint Axes")
            .Field("FreezeY", "Freeze Y", &PositionConstraintComponent::FreezeY)
            .Field("FreezeZ", "Freeze Z", &PositionConstraintComponent::FreezeZ);
        ComponentDef<RotationConstraintComponent>("Rotation Constraint", "RotationConstraintComponent", "Constraints", ICON_FA_ROTATE).Color(kConstraintColor)
            .Describe("Copies the source's rotation.")
            CQ_CONSTRAINT_COMMON(RotationConstraintComponent)
            .Field("Offset", "Rotation Offset", &RotationConstraintComponent::Offset).Speed(0.5f)
            .Field("FreezeX", "Freeze X", &RotationConstraintComponent::FreezeX).Header("Constraint Axes")
            .Field("FreezeY", "Freeze Y", &RotationConstraintComponent::FreezeY)
            .Field("FreezeZ", "Freeze Z", &RotationConstraintComponent::FreezeZ);
        ComponentDef<ScaleConstraintComponent>("Scale Constraint", "ScaleConstraintComponent", "Constraints", ICON_FA_MAXIMIZE).Color(kConstraintColor)
            .Describe("Copies the source's scale.")
            CQ_CONSTRAINT_COMMON(ScaleConstraintComponent)
            .Field("Offset", "Scale Multiplier", &ScaleConstraintComponent::Offset).Speed(0.01f);
        ComponentDef<ParentConstraintComponent>("Parent Constraint", "ParentConstraintComponent", "Constraints", ICON_FA_SITEMAP).Color(kConstraintColor)
            .Describe("Moves and rotates as if parented to the source, without changing the hierarchy.")
            CQ_CONSTRAINT_COMMON(ParentConstraintComponent)
            .Field("CaptureOffsetOnStart", "Capture Offset On Play", &ParentConstraintComponent::CaptureOffsetOnStart)
            .Field("PositionOffset", "Position Offset", &ParentConstraintComponent::PositionOffset).Speed(0.01f)
            .Field("RotationOffset", "Rotation Offset", &ParentConstraintComponent::RotationOffset).Speed(0.5f)
            .Field("OffsetCaptured", "Offset Captured", &ParentConstraintComponent::OffsetCaptured).ReadOnly();
#undef CQ_CONSTRAINT_COMMON

        // ── Ortam / oyun ──
        ComponentDef<ParticleForceFieldComponent>("Particle System Force Field", "ParticleForceFieldComponent", "Effects", ICON_FA_TORNADO).Color(kRenderColor)
            .Describe("Pushes, pulls, swirls or slows particles inside its shape (all particle systems are affected).")
            .Field("Shape", "Shape", &ParticleForceFieldComponent::Shape).Options({ "Sphere", "Hemisphere", "Cylinder", "Box" })
            .Field("StartRange", "Start Range", &ParticleForceFieldComponent::StartRange).Range(0.0f, 100000.0f, 0.05f)
                .Tooltip("Inner radius: no effect closer than this.")
            .Field("EndRange", "End Range", &ParticleForceFieldComponent::EndRange).Range(0.0f, 100000.0f, 0.05f)
                .Tooltip("Outer radius (half size for Box).")
            .Field("Length", "Length", &ParticleForceFieldComponent::Length).Range(0.0f, 100000.0f, 0.05f)
                .VisibleIf([](const ParticleForceFieldComponent& c) { return c.Shape == ForceFieldShape::Cylinder; })
            .Field("Directional", "Directional", &ParticleForceFieldComponent::Directional).Speed(0.05f).Header("Forces")
                .Tooltip("Constant acceleration along the field's local axes.")
            .Field("Gravity", "Gravity Strength", &ParticleForceFieldComponent::Gravity).Range(-10000.0f, 10000.0f, 0.05f)
                .Tooltip("Attracts towards the centre (negative pushes away).")
            .Field("GravityFocus", "Gravity Focus", &ParticleForceFieldComponent::GravityFocus).Range(0.0f, 1.0f, 0.01f)
            .Field("RotationSpeed", "Rotation Speed", &ParticleForceFieldComponent::RotationSpeed).Range(-1000.0f, 1000.0f, 0.05f)
                .Tooltip("Vortex around the local Y axis (rad/s).")
            .Field("RotationAttraction", "Rotation Attraction", &ParticleForceFieldComponent::RotationAttraction).Range(0.0f, 1.0f, 0.01f)
            .Field("Drag", "Drag", &ParticleForceFieldComponent::Drag).Range(0.0f, 1000.0f, 0.05f)
            .Field("MultiplyDragByVelocity", "Multiply Drag By Velocity", &ParticleForceFieldComponent::MultiplyDragByVelocity);

        ComponentDef<WindZoneComponent>("Wind Zone", "WindZoneComponent", "Effects", ICON_FA_WIND).Color(kGameplayColor)
            .Describe("Blows particles and Tree Creator trees (and optionally rigidbodies). Directional wind blows along -Z.")
            .Field("Mode", "Mode", &WindZoneComponent::Mode).Options({ "Directional", "Spherical" })
            .Field("Radius", "Radius", &WindZoneComponent::Radius).Range(0.0f, 100000.0f, 0.1f)
                .VisibleIf([](const WindZoneComponent& c) { return c.Mode == WindZoneMode::Spherical; })
            .Field("Main", "Main", &WindZoneComponent::Main).Range(-1000.0f, 1000.0f, 0.05f)
            .Field("Turbulence", "Turbulence", &WindZoneComponent::Turbulence).Range(0.0f, 1000.0f, 0.05f)
            .Field("PulseMagnitude", "Pulse Magnitude", &WindZoneComponent::PulseMagnitude).Range(0.0f, 10.0f, 0.01f)
            .Field("PulseFrequency", "Pulse Frequency", &WindZoneComponent::PulseFrequency).Range(0.0f, 10.0f, 0.001f)
            .Field("AffectRigidbodies", "Affect Rigidbodies", &WindZoneComponent::AffectRigidbodies).Header("Physics")
            .Field("RigidbodyForceScale", "Force Scale", &WindZoneComponent::RigidbodyForceScale).Range(0.0f, 1000.0f, 0.05f)
                .VisibleIf([](const WindZoneComponent& c) { return c.AffectRigidbodies; });

        ComponentDef<CameraFollowComponent>("Camera Follow", "CameraFollowComponent", "Gameplay", ICON_FA_VIDEO).Color(kGameplayColor)
            .Describe("Smoothly follows a target (third person, orbit, side scroller or top down). Put it on the camera entity.")
            .Field("Target", "Target", &CameraFollowComponent::Target)
            .Field("Mode", "Mode", &CameraFollowComponent::Mode).Options({ "Follow", "Orbit", "Side Scroller", "Top Down" })
            .Field("Offset", "Offset", &CameraFollowComponent::Offset).Speed(0.05f)
            .Field("PositionDamping", "Position Damping", &CameraFollowComponent::PositionDamping).Range(0.0f, 5.0f, 0.005f).Format("%.3f s")
            .Field("RotationDamping", "Rotation Damping", &CameraFollowComponent::RotationDamping).Range(0.0f, 5.0f, 0.005f).Format("%.3f s")
            .Field("LookAtTarget", "Look At Target", &CameraFollowComponent::LookAtTarget)
            .Field("LookOffset", "Look Offset", &CameraFollowComponent::LookOffset).Speed(0.05f)
            .Field("OrbitDistance", "Orbit Distance", &CameraFollowComponent::OrbitDistance).Range(0.1f, 10000.0f, 0.05f).Header("Orbit")
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::Orbit; })
            .Field("OrbitYaw", "Yaw", &CameraFollowComponent::OrbitYaw, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::Orbit; })
            .Field("OrbitPitch", "Pitch", &CameraFollowComponent::OrbitPitch, FieldKind::Angle).Range(-85.0f, 85.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::Orbit; })
            .Field("MouseOrbit", "Mouse Orbit (RMB)", &CameraFollowComponent::MouseOrbit)
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::Orbit; })
            .Field("OrbitSensitivity", "Sensitivity", &CameraFollowComponent::OrbitSensitivity).Range(0.001f, 10.0f, 0.005f)
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::Orbit; })
            .Field("DeadZone", "Dead Zone", &CameraFollowComponent::DeadZone).Speed(0.01f)
                .VisibleIf([](const CameraFollowComponent& c) { return c.Mode == CameraFollowMode::SideScroller; });

        ComponentDef<RotatorComponent>("Rotator", "RotatorComponent", "Gameplay", ICON_FA_ARROWS_SPIN).Color(kGameplayColor)
            .Describe("Spins continuously (pickups, fans, planets).")
            .Field("RotationRate", "Rotation Rate (°/s)", &RotatorComponent::RotationRate).Speed(0.5f)
            .Field("LocalSpace", "Local Space", &RotatorComponent::LocalSpace)
            .Field("RunInEditor", "Run In Editor", &RotatorComponent::RunInEditor);

        ComponentDef<ProjectileMovementComponent>("Projectile Movement", "ProjectileMovementComponent", "Gameplay", ICON_FA_BULLSEYE).Color(kGameplayColor)
            .Describe("Moves forward (-Z) with speed and gravity; bounces or stops on hit (calls OnProjectileStop).")
            .Field("InitialSpeed", "Initial Speed", &ProjectileMovementComponent::InitialSpeed).Range(0.0f, 100000.0f, 0.1f)
            .Field("MaxSpeed", "Max Speed", &ProjectileMovementComponent::MaxSpeed).Range(0.0f, 100000.0f, 0.1f).Tooltip("0 = unlimited")
            .Field("GravityScale", "Gravity Scale", &ProjectileMovementComponent::GravityScale).Range(-100.0f, 100.0f, 0.01f)
            .Field("RotationFollowsVelocity", "Rotation Follows Velocity", &ProjectileMovementComponent::RotationFollowsVelocity)
            .Field("ShouldBounce", "Should Bounce", &ProjectileMovementComponent::ShouldBounce)
            .Field("Bounciness", "Bounciness", &ProjectileMovementComponent::Bounciness).Range(0.0f, 1.0f, 0.01f)
                .VisibleIf([](const ProjectileMovementComponent& c) { return c.ShouldBounce; })
            .Field("Lifetime", "Lifetime", &ProjectileMovementComponent::Lifetime).Range(0.0f, 3600.0f, 0.05f).Format("%.2f s").Tooltip("0 = forever")
            .Field("Velocity", "Velocity", &ProjectileMovementComponent::Velocity).NoSerialize().Header("Runtime")
                .Tooltip("Set from a script to launch in a custom direction (0 = forward x Initial Speed).");
    }
}
