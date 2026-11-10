#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Unreal bileşen karşılıkları: Floating Pawn Movement, Interp To Movement, Cable, Cine Camera
namespace Conqueror
{
    class Scene;
    struct SceneEnvironment;
    namespace Scripting { class NativeRegistry; }

    struct FloatingPawnMovementComponent
    {
        float MaxSpeed = 12.0f;               // m/s
        float Acceleration = 40.0f;           // m/s²
        float Deceleration = 80.0f;
        float TurningBoost = 8.0f;
        bool UseDefaultInput = true;          // WASD + E / Q (nesnenin yönüne göre)
        // Çalışma zamanı
        glm::vec3 Velocity{ 0.0f };
        glm::vec3 PendingInput{ 0.0f };       // AddMovementInput (dünya uzayı)
    };

    enum class InterpBehaviour : int { OneShot = 0, OneShotReverse, LoopReset, PingPong };

    struct InterpToMovementComponent
    {
        float Duration = 2.0f;                // s, bir tur
        InterpBehaviour Behaviour = InterpBehaviour::PingPong;
        std::vector<glm::vec3> ControlPoints{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f } };   // başlangıç konumuna göre
        bool AutoActivate = true;
        // Çalışma zamanı
        bool Active = false;
        float Time = 0.0f;
        int Direction = 1;
        bool HasStart = false;
        glm::vec3 Start{ 0.0f };
    };

    struct CableComponent
    {
        uint64_t AttachEndTo = 0;             // boşsa uç bu nesneye göre End Location'da sabit
        glm::vec3 EndLocation{ 1.0f, 0.0f, 0.0f };
        float CableLength = 1.0f;             // m
        int NumSegments = 10;
        int SolverIterations = 4;
        float CableWidth = 0.03f;
        float GravityScale = 1.0f;
        glm::vec4 Color{ 0.15f, 0.15f, 0.15f, 1.0f };
        // Çalışma zamanı (Verlet parçacıkları, dünya uzayı)
        std::vector<glm::vec3> Particles, Previous;
    };

    enum class CineFocusMethod : int { DoNotOverride = 0, Manual, Tracking, Disable };

    struct CineCameraComponent
    {
        float SensorWidth = 23.76f;           // mm (Super 35 16:9)
        float SensorHeight = 13.365f;
        float FocalLength = 35.0f;            // mm
        float Aperture = 2.8f;                // f-stop
        CineFocusMethod FocusMethod = CineFocusMethod::Manual;
        float ManualFocusDistance = 10.0f;    // m
        uint64_t TrackingTarget = 0;
        // Bilgi
        float FieldOfView = 0.0f;             // dikey derece
        float CurrentFocusDistance = 0.0f;
    };

    namespace ActorComponents
    {
        // Düzenleyicide ve oyunda (Cable, Cine Camera); runtime: hareket bileşenleri
        CQ_API void Update(Scene& scene, float dt, bool runtime);
        CQ_API void ApplyEnvironment(const Scene& scene, SceneEnvironment& env);
        CQ_API void AddMovementInput(Scene& scene, uint32_t entity, const glm::vec3& worldDirection, float scale = 1.0f);
        void RegisterNatives(Scripting::NativeRegistry* vm);
    }

    void RegisterActorComponents();
}
