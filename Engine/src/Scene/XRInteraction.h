#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>

// Unity XR Interaction Toolkit karşılıkları: XR Origin (oyuncunun kökü, kamera = baş), XR Controller (el), XR Ray Interactor
// (elden ışın / ışınlanma yayı), XR Grab Interactable (tutulup fırlatılan nesne), Teleportation Area / Anchor, Snap Turn ve
// Continuous Move sağlayıcıları. Gözlük bağlı değilken XR Device Simulator çalışır (Unity'deki gibi): fare baş, sağ tık sağ el
// tutma, sol tık tetik, F / G sol el tutma / tetik, Space basılı ışınlanma yayı, Q / E adımlı dönüş, WASD yürüyüş.
namespace Conqueror
{
    class Scene;

    struct XROriginComponent
    {
        float CameraYOffset = 1.36f;          // gözlük yokken göz yüksekliği (m)
        float LookSensitivity = 0.12f;        // simülatör: fare -> derece
        // Çalışma zamanı
        float HeadYaw = 0.0f, HeadPitch = 0.0f;
        bool CursorLocked = false;
    };

    enum class XRHand : int { Left = 0, Right };

    struct XRControllerComponent
    {
        XRHand Hand = XRHand::Right;
        // Çalışma zamanı
        bool Grip = false, Trigger = false, Teleport = false;
        glm::vec3 Velocity{ 0.0f };           // fırlatma için (son kısa sürenin tepe hızı)
        std::deque<std::pair<float, glm::vec3>> History;
    };

    enum class XRLineType : int { Straight = 0, Projectile };

    struct XRRayInteractorComponent
    {
        XRLineType LineType = XRLineType::Straight;   // Projectile: ışınlanma yayı
        float MaxDistance = 30.0f;
        float Velocity = 8.5f;                // yay çıkış hızı (m/s)
        float LineWidth = 0.01f;
        glm::vec4 ValidColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 InvalidColor{ 1.0f, 0.3f, 0.3f, 1.0f };
        // Çalışma zamanı
        uint64_t Hovered = 0;
        uint64_t Selected = 0;
        glm::mat4 AttachOffset{ 1.0f };       // tutulan nesne, ele göre
        bool HasTarget = false;
        glm::vec3 TargetPoint{ 0.0f };
        uint64_t TargetAnchor = 0;
        bool PrevGrip = false, PrevTrigger = false, PrevTeleport = false;   // kenar algılama
    };

    enum class XRMovementType : int { VelocityTracking = 0, Kinematic, Instantaneous };

    struct XRGrabInteractableComponent
    {
        XRMovementType MovementType = XRMovementType::VelocityTracking;
        bool TrackRotation = true;
        bool ThrowOnDetach = true;
        float ThrowVelocityScale = 1.5f;
        // Çalışma zamanı
        bool IsSelected = false;
        bool IsHovered = false;
    };

    struct TeleportationAreaComponent
    {
        bool MatchOrientation = false;
    };

    struct TeleportationAnchorComponent
    {
        bool MatchOrientation = true;         // ışınlanınca bu nesnenin baktığı yöne dön
    };

    struct SnapTurnProviderComponent
    {
        float TurnAmount = 45.0f;
    };

    struct ContinuousMoveProviderComponent
    {
        float MoveSpeed = 2.0f;
        bool EnableStrafe = true;
    };

    namespace XRInteraction
    {
        // Oyunda her kare (betiklerden önce): simülatör girişi, baş / el pozları, ışınlar, tutma, ışınlanma
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void Stop(Scene& scene);
        // Otomasyon / testler: simülatör düğmelerini zorla (-1 serbest)
        CQ_API void SetSimulatedButtons(int hand, int grip, int trigger, int teleport);
    }

    void RegisterXRComponents();
}
