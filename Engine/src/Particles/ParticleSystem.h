#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;

    // ---------------------------------------------------------------- Eğri / renk geçişi

    // 0..1 zaman ekseninde doğrusal parçalı eğri (Unity AnimationCurve basit karşılığı)
    struct FloatCurve
    {
        struct Key { float Time = 0.0f; float Value = 1.0f; };
        std::vector<Key> Keys{ { 0.0f, 1.0f }, { 1.0f, 1.0f } };

        float Evaluate(float t) const;
        void Sort();
        static FloatCurve Constant(float v) { FloatCurve c; c.Keys = { { 0.0f, v }, { 1.0f, v } }; return c; }
        static FloatCurve Linear(float a, float b) { FloatCurve c; c.Keys = { { 0.0f, a }, { 1.0f, b } }; return c; }
    };

    struct ColorGradient
    {
        struct Key { float Time = 0.0f; glm::vec4 Color{ 1.0f }; };
        std::vector<Key> Keys{ { 0.0f, glm::vec4(1.0f) }, { 1.0f, glm::vec4(1.0f) } };

        glm::vec4 Evaluate(float t) const;
        void Sort();
    };

    // ---------------------------------------------------------------- Bileşen

    enum class ParticleShape : int { Sphere = 0, Hemisphere, Cone, Box, Circle, Edge, Point };
    enum class ParticleRenderMode : int { Billboard = 0, StretchedBillboard, HorizontalBillboard, VerticalBillboard };
    enum class ParticleBlend : int { Alpha = 0, Additive, Premultiplied };
    enum class ParticleSpace : int { Local = 0, World };

    struct ParticleBurst
    {
        float Time = 0.0f;
        int Count = 30;
        int Cycles = 1;
        float Interval = 0.25f;
    };

    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        glm::vec4 StartColor{ 1.0f };
        float StartSize = 1.0f;
        float Rotation = 0.0f;         // radyan
        float AngularVelocity = 0.0f;  // radyan/s
        float Age = 0.0f;
        float Lifetime = 1.0f;
        float Random = 0.0f;           // parçacık başına sabit rastgele (0..1)
    };

    // Unity Particle System (Shuriken) modüllerinin karşılığı (kaydedilen ayarlar)
    struct ParticleSettings
    {
        // ---- Main
        float Duration = 5.0f;
        bool Looping = true;
        bool Prewarm = false;
        bool PlayOnAwake = true;
        int StopAction = 0;                          // Unity Stop Action: 0 yok, 1 devre dışı bırak, 2 yok et (oyun sırasında)
        float StartDelay = 0.0f;
        glm::vec2 StartLifetime{ 2.0f, 3.0f };      // min, max
        glm::vec2 StartSpeed{ 2.0f, 4.0f };
        glm::vec2 StartSize{ 0.3f, 0.6f };
        glm::vec2 StartRotation{ 0.0f, 0.0f };      // derece
        glm::vec4 StartColorA{ 1.0f };
        glm::vec4 StartColorB{ 1.0f };              // A..B arası rastgele
        float GravityModifier = 0.0f;
        ParticleSpace SimulationSpace = ParticleSpace::World;
        float SimulationSpeed = 1.0f;
        int MaxParticles = 1000;

        // ---- Emission
        bool EmissionEnabled = true;
        float RateOverTime = 20.0f;
        float RateOverDistance = 0.0f;
        std::vector<ParticleBurst> Bursts;

        // ---- Shape
        bool ShapeEnabled = true;
        ParticleShape Shape = ParticleShape::Cone;
        float Radius = 0.5f;
        float Angle = 25.0f;                         // koni açısı (derece)
        glm::vec3 BoxSize{ 1.0f };
        bool EmitFromShell = false;
        float RandomizeDirection = 0.0f;             // 0..1

        // ---- Velocity over Lifetime / Limit / Noise
        bool VelocityEnabled = false;
        glm::vec3 VelocityOverLifetime{ 0.0f };
        float OrbitalSpeed = 0.0f;                   // Y ekseni etrafında (rad/s)
        bool LimitVelocityEnabled = false;
        float Drag = 0.0f;
        float SpeedLimit = 100.0f;
        bool NoiseEnabled = false;
        float NoiseStrength = 1.0f;
        float NoiseFrequency = 0.5f;
        float NoiseScrollSpeed = 0.5f;

        // ---- Color / Size / Rotation over Lifetime
        bool ColorOverLifetimeEnabled = true;
        ColorGradient ColorOverLifetime;
        bool SizeOverLifetimeEnabled = false;
        FloatCurve SizeOverLifetime;
        bool RotationOverLifetimeEnabled = false;
        float AngularVelocity = 45.0f;               // derece/s

        // ---- Collision (sonsuz yer düzlemi)
        bool CollisionEnabled = false;
        float CollisionPlaneY = 0.0f;                // dünya Y
        float Bounce = 0.5f;
        float LifetimeLoss = 0.0f;

        // ---- Texture Sheet Animation
        bool TextureSheetEnabled = false;
        glm::ivec2 Tiles{ 1, 1 };
        float AnimationCycles = 1.0f;
        bool RandomStartFrame = false;

        // ---- Renderer
        ParticleRenderMode RenderMode = ParticleRenderMode::Billboard;
        ParticleBlend Blend = ParticleBlend::Alpha;
        std::string TexturePath;                     // boşsa yumuşak daire
        std::string SubUV;                           // .cqsubuv: Texture Sheet Animation ayarları
        std::string Asset;                           // .cqparticle: ayarlar bu dosyadan gelir
        float LengthScale = 2.0f;                    // Stretched
        float SpeedScale = 0.1f;
        bool SortByDistance = true;
        float EmissiveIntensity = 1.0f;              // HDR çarpanı (bloom)

    };

    struct ParticleSystemComponent : ParticleSettings
    {
        // ---- Çalışma zamanı (kaydedilmez)
        struct Runtime
        {
            std::vector<Particle> Particles;
            float Time = 0.0f;                       // sistem zamanı (döngü içinde)
            float TotalTime = 0.0f;
            float EmitAccumulator = 0.0f;
            float DistanceAccumulator = 0.0f;
            glm::vec3 LastPosition{ 0.0f };
            bool HasLastPosition = false;
            bool Playing = false;
            bool Paused = false;
            bool Started = false;
            bool EverActive = false;                 // en az bir kez parçacık üretti / oynadı (Stop Action)
            std::vector<int> BurstCyclesDone;
            std::mt19937 Rng{ 12345u };
            std::shared_ptr<Texture2D> Texture;
            std::string LoadedTexturePath;
            std::string AppliedAsset;                // uygulanan .cqparticle
        };
        std::shared_ptr<Runtime> RT = std::make_shared<Runtime>();
        bool EditorPreview = false;                   // editör seçiliyken önizleme

        ParticleSystemComponent() = default;
        // Kopya kendi çalışma zamanı durumunu alır (simülasyon paylaşılmaz)
        ParticleSystemComponent(const ParticleSystemComponent& o) : ParticleSettings(o), EditorPreview(false) {}
        ParticleSystemComponent& operator=(const ParticleSystemComponent& o)
        {
            if (this != &o)
            {
                static_cast<ParticleSettings&>(*this) = o;
                RT = std::make_shared<Runtime>();
            }
            return *this;
        }
    };

    // Particle System varlığı (.cqparticle): ayarlar dosyada, Particle System bileşeninin Asset yuvasından kullanılır
    namespace ParticleAssets
    {
        constexpr const char* Extension = ".cqparticle";
        CQ_API bool Save(const std::string& path, const ParticleSystemComponent& ps);
        CQ_API bool Load(const std::string& path, ParticleSystemComponent& ps);
        CQ_API std::string ToText(const ParticleSystemComponent& ps);
    }

    class CQ_API ParticleSystem
    {
    public:
        static void Play(ParticleSystemComponent& ps);
        static void Pause(ParticleSystemComponent& ps);
        static void Stop(ParticleSystemComponent& ps, bool clear = true);
        static void Restart(ParticleSystemComponent& ps);
        static void Emit(ParticleSystemComponent& ps, const glm::mat4& world, int count);
        // Oynatma bitti ve yaşayan parçacık kalmadı (Stop Action için)
        static bool IsFinished(const ParticleSystemComponent& ps);

        // world: varlığın dünya dönüşümü
        static void Update(ParticleSystemComponent& ps, const glm::mat4& world, float dt);
        static void Render(ParticleSystemComponent& ps, const glm::mat4& world);

        // Hazır ayarlar: "Fire", "Smoke", "Sparks", "Rain", "Snow", "Explosion", "Magic", "Dust"
        static bool ApplyPreset(ParticleSystemComponent& ps, const std::string& preset);
        static const std::vector<std::string>& GetPresetNames();

        static void Shutdown();
    };
}
