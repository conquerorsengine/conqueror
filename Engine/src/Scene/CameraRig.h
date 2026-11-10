#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

// Cinemachine benzeri sanal kamera sistemi:
//  - Virtual Camera: önceliği en yüksek etkin sanal kamera "canlı"dır; gövde (takip / omuz üstü / yörünge)
//    ve nişan (bakış) davranışları, sönümleme, gürültü (el kamerası titreşimi), FOV ve yatırma (dutch).
//  - Camera Brain: gerçek kamerada; canlı sanal kamerayı izler, değişince yumuşak geçiş (blend) yapar.
//  - Impulse Source / Listener: patlama, darbe gibi olaylarda kamera sarsıntısı.
namespace Conqueror
{
    class Scene;

    enum class VCamBody : int { DoNothing = 0, Follow, ThirdPerson, Orbital };
    enum class VCamAim : int { DoNothing = 0, Composer, HardLookAt, SameAsFollowTarget };
    enum class VCamBinding : int { World = 0, LockToTargetWithWorldUp, LockToTarget };
    enum class CameraBlendCurve : int { EaseInOut = 0, Linear, Cut };

    struct VirtualCameraComponent
    {
        int Priority = 10;
        uint64_t Follow = 0;              // takip edilen varlık
        uint64_t LookAt = 0;              // bakılan varlık (boşsa Follow)

        VCamBody Body = VCamBody::Follow;
        VCamBinding Binding = VCamBinding::LockToTargetWithWorldUp;
        glm::vec3 FollowOffset{ 0.0f, 2.5f, 6.0f };   // hedefin arkasında (+Z = arka)
        glm::vec3 PositionDamping{ 0.4f, 0.4f, 0.4f };  // s (0 = sert)

        // Omuz üstü (Third Person)
        glm::vec3 ShoulderOffset{ 0.6f, 1.6f, 0.0f };
        float CameraDistance = 3.5f;
        bool AvoidObstacles = true;
        float CameraRadius = 0.2f;

        // Yörünge (Orbital): fareyle döner
        float OrbitRadius = 6.0f;
        float OrbitHeight = 2.0f;
        bool OrbitInput = true;           // fareyle döndür
        float OrbitSpeed = 0.25f;         // derece / piksel
        float OrbitYaw = 0.0f;            // derece (çalışırken değişir)
        float OrbitPitch = 15.0f;

        VCamAim Aim = VCamAim::Composer;
        float AimDamping = 0.3f;          // s
        glm::vec3 LookAtOffset{ 0.0f, 1.0f, 0.0f };

        float FieldOfView = 60.0f;        // derece (dikey)
        float Dutch = 0.0f;               // derece (yatırma)

        // Gürültü (Perlin benzeri el kamerası)
        float NoiseAmplitude = 0.0f;      // derece
        float NoiseFrequency = 1.0f;

        float ImpulseGain = 1.0f;         // Impulse Listener kazancı (0 = sarsılmaz)

        // Çalışma zamanı
        bool Live = false;
        bool Initialized = false;
        glm::vec3 StatePosition{ 0.0f };
        glm::quat StateRotation{ 1, 0, 0, 0 };
        float NoiseTime = 0.0f;
        float EffectiveFov = -1.0f;       // Follow Zoom gibi eklerin bu karedeki FOV'u (< 0: FieldOfView)
        // Eklerden (Confiner, Deoccluder, Group Framing) sonraki son poz: gövdenin sönümlü durumu (State*) bozulmaz
        glm::vec3 FinalPosition{ 0.0f };
        glm::quat FinalRotation{ 1, 0, 0, 0 };
    };

    // ── Cinemachine ekleri (sanal kameranın varlığına eklenir) ──

    // Confiner 3D: sanal kamera bir hacmin içinde kalır (Bounding Volume varlığının kutusu: Box Collider'ı varsa onun kutusu, yoksa konum + ölçek)
    struct CinemachineConfiner3DComponent
    {
        uint64_t BoundingVolume = 0;
        float SlowingDistance = 0.0f;     // kenara bu mesafeden itibaren yavaşlar (0 = sert sınır)
    };

    // Confiner 2D: sanal kamera bir 2B şeklin içinde kalır (Box Collider 2D / Polygon Collider 2D; yoksa konum + ölçek kutusu)
    struct CinemachineConfiner2DComponent
    {
        uint64_t BoundingShape = 0;
    };

    // Deoccluder: hedefle kamera arasında çarpıştırıcı varsa kamera hedefe doğru öne alınır (görüş açılır)
    struct CinemachineDeoccluderComponent
    {
        uint32_t CollideAgainst = 0xFFFFFFFFu;   // katmanlar
        float CameraRadius = 0.2f;
        float MinimumDistanceFromTarget = 0.3f;
        float Damping = 0.2f;                     // s: engel kalkınca geri çekilme
        // Çalışma zamanı
        float Pull = 0.0f;                        // hedefe doğru çekilen mesafe
    };

    // Group Framing: Follow / LookAt hedefi bir Target Group ise kamera grubu ekrana sığacak uzaklığa gelir (Dolly)
    struct CinemachineGroupFramingComponent
    {
        float FramingSize = 0.8f;         // grubun ekranın ne kadarını kaplayacağı (0..1)
        float Damping = 1.0f;             // s
        float MinDistance = 1.0f, MaxDistance = 100.0f;
        // Çalışma zamanı
        float Distance = -1.0f;
    };

    // Follow Zoom: FOV, hedefin çevresindeki Width metrelik alanı ekranda sabit boyda tutacak şekilde ayarlanır
    struct CinemachineFollowZoomComponent
    {
        float Width = 2.0f;               // m
        float MinFov = 3.0f, MaxFov = 60.0f;
        float Damping = 1.0f;             // s
        // Çalışma zamanı
        float Fov = -1.0f;
    };

    // Target Group: kameraların takip edebileceği grup; bu varlık her karede hedeflerin ağırlıklı merkezine konur
    struct CinemachineTargetGroupComponent
    {
        uint64_t Target1 = 0, Target2 = 0, Target3 = 0, Target4 = 0;
        float Weight1 = 1.0f, Weight2 = 1.0f, Weight3 = 1.0f, Weight4 = 1.0f;
        float Radius1 = 1.0f, Radius2 = 1.0f, Radius3 = 1.0f, Radius4 = 1.0f;
        // Çalışma zamanı: grubu kapsayan küre
        float BoundingRadius = 0.0f;
    };

    // Impulse Listener (Unity Cinemachine): bu sanal kameranın Impulse Source sarsıntılarına tepkisi. Varsa Virtual Camera'nın Impulse Gain
    // alanının yerine geçer
    struct CinemachineImpulseListenerComponent
    {
        float Gain = 1.0f;                // 0 = sarsılmaz
        bool Use2DDistance = false;       // kaynağa uzaklık Z yok sayılarak (2B oyunlar)
        bool UseCameraSpace = true;       // sarsıntı kameranın kendi eksenlerinde (sağ / yukarı / ileri)
        float FrequencyGain = 1.0f;       // titreşim hızı çarpanı
    };

    struct CameraBrainComponent
    {
        float DefaultBlend = 1.5f;        // s
        CameraBlendCurve BlendCurve = CameraBlendCurve::EaseInOut;
        bool ApplyFieldOfView = true;
        // Çalışma zamanı
        uint64_t ActiveCamera = 0;        // canlı sanal kamera (UUID)
        float BlendProgress = 1.0f;       // 0..1
        glm::vec3 BlendFromPosition{ 0.0f };
        glm::quat BlendFromRotation{ 1, 0, 0, 0 };
        float BlendFromFov = 60.0f;
        bool HasPose = false;
    };

    struct ImpulseSourceComponent
    {
        float Amplitude = 1.0f;           // metre (konum sarsıntısı) — açısal sarsıntı da bundan türetilir
        float Frequency = 12.0f;          // Hz
        float Duration = 0.4f;            // s
        float Radius = 30.0f;             // bu mesafeden sonra etkisiz
        bool GenerateOnStart = false;
        bool GenerateOnCollision = false; // çarpışmada (Rigidbody gerekir)
    };

    namespace CameraRig
    {
        // Betiklerden: ImpulseGenerate(source, strength)
        CQ_API void GenerateImpulse(Scene& scene, uint32_t sourceEntity, float strength = 1.0f);
        CQ_API void GenerateImpulseAt(Scene& scene, const glm::vec3& position, float amplitude, float frequency, float duration, float radius);
        CQ_API void LateUpdate(Scene& scene, float dt);
        CQ_API void OnCollision(Scene& scene, uint32_t entity, float impulse);
        CQ_API void Reset(Scene* scene);
    }

    void RegisterCameraRigComponents();
}
