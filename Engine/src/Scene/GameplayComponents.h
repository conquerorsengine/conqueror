#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Unity / Unreal tarzı oyun bileşenleri: çizgi ve iz çiziciler, LOD grubu, kısıtlar (constraints),
// rüzgâr bölgesi, kamera takibi, döndürücü, mermi hareketi, sıralama grubu.
namespace Conqueror
{
    enum class RibbonAlignment : int { View = 0, TransformZ };
    enum class RibbonTextureMode : int { Stretch = 0, Tile };
    enum class RibbonBlend : int { Alpha = 0, Additive };

    struct LineRendererComponent
    {
        std::vector<glm::vec3> Positions{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
        bool UseWorldSpace = false;
        bool Loop = false;
        float StartWidth = 0.1f;
        float EndWidth = 0.1f;
        glm::vec4 StartColor{ 1.0f };
        glm::vec4 EndColor{ 1.0f };
        RibbonAlignment Alignment = RibbonAlignment::View;
        RibbonTextureMode TextureMode = RibbonTextureMode::Stretch;
        RibbonBlend Blend = RibbonBlend::Alpha;
        std::string Texture;
        float Emissive = 1.0f;
    };

    struct TrailRendererComponent
    {
        float Time = 1.0f;               // noktaların ömrü (s)
        float MinVertexDistance = 0.1f;
        float StartWidth = 0.3f;
        float EndWidth = 0.0f;
        glm::vec4 StartColor{ 1.0f };
        glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
        bool Emitting = true;
        RibbonTextureMode TextureMode = RibbonTextureMode::Stretch;
        RibbonBlend Blend = RibbonBlend::Alpha;
        std::string Texture;
        float Emissive = 1.0f;
    };

    struct LODGroupComponent
    {
        // Seviye başına çocuk varlık (UUID) ve ekran yüksekliği eşiği (%): seviye i, oran >= eşik ise seçilir
        uint64_t LOD0 = 0, LOD1 = 0, LOD2 = 0, LOD3 = 0;
        float Threshold0 = 60.0f, Threshold1 = 30.0f, Threshold2 = 10.0f, Threshold3 = 1.0f;
        float Size = 2.0f;               // nesne boyutu (dünya birimi)
        bool CullBelowLast = true;
        int CurrentLOD = 0;              // çalışma zamanı (-1 = gizli)
    };

    struct ConstraintCommon
    {
        bool IsActive = true;
        float Weight = 1.0f;
        uint64_t Source = 0;             // hedef varlık UUID
    };

    struct LookAtConstraintComponent : ConstraintCommon
    {
        float Roll = 0.0f;               // derece
        glm::vec3 RotationOffset{ 0.0f };
        bool UseWorldUp = true;
    };

    struct AimConstraintComponent : ConstraintCommon
    {
        glm::vec3 AimVector{ 0.0f, 0.0f, 1.0f };
        glm::vec3 UpVector{ 0.0f, 1.0f, 0.0f };
        glm::vec3 WorldUpVector{ 0.0f, 1.0f, 0.0f };
        glm::vec3 RotationOffset{ 0.0f };
    };

    struct PositionConstraintComponent : ConstraintCommon
    {
        glm::vec3 Offset{ 0.0f };
        bool FreezeX = false, FreezeY = false, FreezeZ = false;
    };

    struct RotationConstraintComponent : ConstraintCommon
    {
        glm::vec3 Offset{ 0.0f };        // derece
        bool FreezeX = false, FreezeY = false, FreezeZ = false;
    };

    struct ScaleConstraintComponent : ConstraintCommon
    {
        glm::vec3 Offset{ 1.0f };
    };

    struct ParentConstraintComponent : ConstraintCommon
    {
        glm::vec3 PositionOffset{ 0.0f }; // hedefin yerel uzayında
        glm::vec3 RotationOffset{ 0.0f }; // derece
        bool CaptureOffsetOnStart = true;
        bool OffsetCaptured = false;      // çalışma zamanı
    };

    enum class WindZoneMode : int { Directional = 0, Spherical };

    struct WindZoneComponent
    {
        WindZoneMode Mode = WindZoneMode::Directional;
        float Radius = 20.0f;
        float Main = 1.0f;
        float Turbulence = 1.0f;
        float PulseMagnitude = 0.5f;
        float PulseFrequency = 0.01f;
        bool AffectRigidbodies = false;
        float RigidbodyForceScale = 1.0f;
    };

    // Unity ParticleSystemForceField: şekil içindeki parçacıklara yönlü kuvvet, merkeze çekim, girdap ve sürüklenme
    enum class ForceFieldShape : int { Sphere = 0, Hemisphere, Cylinder, Box };

    struct ParticleForceFieldComponent
    {
        ForceFieldShape Shape = ForceFieldShape::Sphere;
        float StartRange = 0.0f;         // iç yarıçap (bunun içinde etki yok)
        float EndRange = 5.0f;           // dış yarıçap / kutu yarı boyutu
        float Length = 5.0f;             // silindir boyu (yerel Y)
        glm::vec3 Directional{ 0.0f };   // yerel eksenlerde sabit ivme (m/s²)
        float Gravity = 0.0f;            // merkeze (+) / dışarı (-) ivme
        float GravityFocus = 0.0f;       // 0: merkez, 1: yüzeye çeker
        float RotationSpeed = 0.0f;      // yerel Y ekseni çevresinde girdap (rad/s)
        float RotationAttraction = 0.5f; // 0..1: hızın girdaba ne kadar çekileceği
        float Drag = 0.0f;               // 1/s
        bool MultiplyDragByVelocity = false;
    };

    enum class CameraFollowMode : int { Follow = 0, Orbit, SideScroller, TopDown };

    struct CameraFollowComponent
    {
        uint64_t Target = 0;
        CameraFollowMode Mode = CameraFollowMode::Follow;
        glm::vec3 Offset{ 0.0f, 3.0f, -6.0f };
        float PositionDamping = 0.15f;   // saniye (0 = anında)
        float RotationDamping = 0.1f;
        bool LookAtTarget = true;
        glm::vec3 LookOffset{ 0.0f, 1.0f, 0.0f };
        float OrbitDistance = 6.0f;
        float OrbitYaw = 0.0f;           // derece
        float OrbitPitch = 20.0f;
        bool MouseOrbit = true;          // Orbit: farenin sağ tuşu ile döndür
        float OrbitSensitivity = 0.2f;
        glm::vec2 DeadZone{ 0.0f };      // SideScroller: hareketsiz bölge
    };

    struct RotatorComponent
    {
        glm::vec3 RotationRate{ 0.0f, 90.0f, 0.0f }; // derece/s
        bool LocalSpace = true;
        bool RunInEditor = false;
    };

    struct ProjectileMovementComponent
    {
        float InitialSpeed = 20.0f;
        float MaxSpeed = 0.0f;           // 0 = sınırsız
        float GravityScale = 0.0f;
        bool RotationFollowsVelocity = true;
        bool ShouldBounce = false;
        float Bounciness = 0.6f;
        float Lifetime = 0.0f;           // 0 = sonsuz; dolunca varlık silinir
        glm::vec3 Velocity{ 0.0f };      // çalışma zamanı
        float Age = 0.0f;                // çalışma zamanı
        bool Initialized = false;        // çalışma zamanı
    };

    struct SortingGroupComponent
    {
        int SortingLayer = 0;
        int OrderInLayer = 0;
    };

    // Prefab örneğinin kökü: kaynak .cqprefab (Apply / Revert / Select Asset)
    struct PrefabInstanceComponent
    {
        std::string Prefab;
    };
}
