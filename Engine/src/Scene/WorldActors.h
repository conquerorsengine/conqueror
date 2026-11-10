#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_set>
#include <vector>

// Unreal Place Actors karşılıkları:
// - Ortam: Exponential Height Fog, Sky Atmosphere, Volumetric Cloud, Sky Light (sahnedeyken Lighting ayarlarının yerine geçer)
// - Hacimler: Kill Z, Pain Causing, Physics, Cull Distance
// - Radial Force, Arrow, Note, Target Point
namespace Conqueror
{
    class Scene;
    struct SceneEnvironment;

    struct ExponentialHeightFogComponent
    {
        float FogDensity = 0.02f;
        float HeightFalloff = 0.2f;           // yukarı çıktıkça incelme (yükseklik = bu nesnenin Y'si)
        glm::vec3 InscatteringColor{ 0.45f, 0.55f, 0.7f };
        float StartDistance = 0.0f;
        bool VolumetricFog = false;           // ışık hüzmeleri (güneş)
        float ScatteringIntensity = 1.0f;
        glm::vec3 Albedo{ 1.0f };
        float ViewDistance = 80.0f;
    };

    struct SkyAtmosphereComponent
    {
        float RayleighScale = 1.0f;
        float MieScale = 1.0f;
        float MieAnisotropy = 0.8f;
        float OzoneScale = 1.0f;
        float SunDiskScale = 1.0f;
        float Intensity = 1.0f;
        glm::vec3 GroundAlbedo{ 0.12f };
    };

    struct VolumetricCloudComponent
    {
        float Coverage = 0.45f;
        float Density = 1.0f;
        float LayerBottomAltitude = 1500.0f;  // m
        float LayerHeight = 1800.0f;          // m
        float Scale = 1.0f;
        float WindSpeed = 12.0f;              // m/s
        float WindDirection = 30.0f;          // derece
        glm::vec3 Color{ 1.0f };
    };

    struct SkyLightComponent
    {
        float Intensity = 1.0f;               // ortam (diffuse) ışığı
        float ReflectionIntensity = 1.0f;     // yansımalar
    };

    struct KillZVolumeComponent
    {
        glm::vec3 Size{ 200.0f, 10.0f, 200.0f };   // yerel kutu (ölçekle çarpılır)
    };

    struct PainCausingVolumeComponent
    {
        glm::vec3 Size{ 4.0f };
        bool PainCausing = true;
        float DamagePerSec = 10.0f;
        float PainInterval = 1.0f;            // s
        std::string DamageType = "Pain";
        float Timer = 0.0f;                   // çalışma zamanı
    };

    struct PhysicsVolumeComponent
    {
        glm::vec3 Size{ 4.0f };
        float FluidFriction = 0.3f;           // içindeki gövdelerin hızı saniyede bu oranda azalır
        float TerminalVelocity = 40.0f;       // m/s
        bool WaterVolume = false;             // betikler IsInWaterVolume ile sorar; ayrıca kaldırma kuvveti
        float Buoyancy = 0.0f;                // m/s² yukarı (WaterVolume)
    };

    struct CullDistanceVolumeComponent
    {
        glm::vec3 Size{ 100.0f, 50.0f, 100.0f };
        // (nesne boyu m, en fazla çizim uzaklığı m); 0 uzaklık = hiç gizleme
        std::vector<glm::vec2> CullDistances{ { 1.0f, 40.0f }, { 4.0f, 120.0f }, { 10.0f, 300.0f } };
    };

    enum class RadialFalloff : int { Constant = 0, Linear };

    struct RadialForceComponent
    {
        float Radius = 5.0f;
        RadialFalloff Falloff = RadialFalloff::Constant;
        float ImpulseStrength = 10.0f;        // FireImpulse: N·s (Impulse Vel Change açıksa m/s)
        bool ImpulseVelChange = false;
        float ForceStrength = 0.0f;           // sürekli kuvvet (N); 0 = yok
        bool FireOnStart = false;
        bool FireRequested = false;           // betik / Inspector düğmesi
    };

    struct ArrowComponent
    {
        glm::vec4 Color{ 1.0f, 0.2f, 0.2f, 1.0f };
        float Size = 1.0f;
    };

    struct NoteComponent
    {
        std::string Text = "Note";
    };

    struct TargetPointComponent
    {
        float Size = 0.5f;
    };

    namespace WorldActors
    {
        // Ortam bileşenleri Lighting ayarlarının yerine geçer (her kare çizimden önce)
        CQ_API void ApplyEnvironment(const Scene& scene, SceneEnvironment& env);
        // Hacimler ve kuvvetler (oyunda, fizikten önce)
        CQ_API void BeginPlay(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        // Cull Distance Volume: kameradan uzak küçük statik nesneler
        CQ_API void ComputeCullHidden(Scene& scene, const glm::vec3& cameraPosition, std::unordered_set<uint32_t>& hidden);
        // Betikler: nokta bir Physics Volume'un su hacminde mi
        CQ_API bool IsInWaterVolume(Scene& scene, const glm::vec3& point);
        CQ_API void FireImpulse(Scene& scene, uint32_t entity);
        // Düzenleyici çizimleri (ok, hedef noktası, not, hacim kutuları)
        CQ_API void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection);
    }

    void RegisterWorldActorComponents();
}
