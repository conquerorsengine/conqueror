#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace YAML
{
    class Emitter;
    class Node;
}

namespace Conqueror
{
    // 3D fizik motoru arka ucu (Project Settings > Physics)
    enum class PhysicsBackend3D : int
    {
        Jolt = 0,   // varsayılan: oyun fiziği (karakter, eklemler, tetikleyiciler)
        MuJoCo = 1  // DeepMind MuJoCo: hassas çok gövdeli dinamik (çalışma zamanında yüklenir)
    };

    enum class MuJoCoIntegrator : int { Euler = 0, RK4, Implicit, ImplicitFast };

    // Proje genelinde fizik ayarları (.cqproj içinde "Physics" altında saklanır)
    struct CQ_API PhysicsSettings
    {
        static constexpr int MaxLayers = 32;

        PhysicsBackend3D Backend = PhysicsBackend3D::Jolt;
        glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
        glm::vec2 Gravity2D{ 0.0f, -9.81f };

        float FixedTimestep = 1.0f / 60.0f;  // saniye
        int MaxSubSteps = 8;                 // kare başına en fazla sabit adım (yavaş karelerde patlamayı önler)
        int CollisionSteps = 1;              // Jolt: adım başına çarpışma alt adımı
        int VelocityIterations = 10;
        int PositionIterations = 2;
        bool Interpolation = true;           // görsel dönüşümleri sabit adımlar arasında yumuşat

        float DefaultFriction = 0.6f;
        float DefaultRestitution = 0.0f;
        float SleepThreshold = 0.05f;        // m/s
        bool QueriesHitTriggers = true;
        bool AutoSyncTransforms = true;      // betiklerin konum değişikliklerini gövdelere aktar

        // Katman çarpışma matrisi: CollisionMatrix[i] bit j => i ve j katmanları çarpışır
        std::array<uint32_t, MaxLayers> CollisionMatrix{};

        // MuJoCo
        std::string MuJoCoLibraryPath;       // boşsa standart konumlarda aranır
        MuJoCoIntegrator Integrator = MuJoCoIntegrator::ImplicitFast;
        int MuJoCoIterations = 50;
        float MuJoCoTimestepScale = 1.0f;     // MuJoCo iç adımı = FixedTimestep * ölçek

        PhysicsSettings() { CollisionMatrix.fill(0xFFFFFFFFu); }

        bool LayersCollide(int a, int b) const
        {
            if (a < 0 || a >= MaxLayers || b < 0 || b >= MaxLayers)
                return true;
            return (CollisionMatrix[(size_t)a] >> b) & 1u;
        }
        void SetLayersCollide(int a, int b, bool collide)
        {
            if (a < 0 || a >= MaxLayers || b < 0 || b >= MaxLayers)
                return;
            if (collide)
            {
                CollisionMatrix[(size_t)a] |= 1u << b;
                CollisionMatrix[(size_t)b] |= 1u << a;
            }
            else
            {
                CollisionMatrix[(size_t)a] &= ~(1u << b);
                CollisionMatrix[(size_t)b] &= ~(1u << a);
            }
        }

        // Etkin proje ayarları (proje yoksa varsayılanlar)
        static PhysicsSettings& Get();

        void Serialize(YAML::Emitter& out) const;
        void Deserialize(const YAML::Node& node);
    };

    CQ_API const char* PhysicsBackendName(PhysicsBackend3D backend);
}
