#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// Godot düğüm eşdeğerleri:
//  - Remote Transform: kendi dönüşümünü başka bir nesneye iter (hiyerarşi dışı takip; RemoteTransform3D/2D)
//  - Ray Cast: her karede güncellenen kalıcı ışın (IsColliding / nokta / normal / çarpılan; RayCast3D/2D)
//  - Visible On Screen Notifier: ana kameranın görüşüne girip çıkınca sinyal ve OnBecameVisible/Invisible
//  - Parallax 2D: kamera kaydıkça katmanı ölçekli kaydırır, otomatik kayma ve sonsuz tekrar (Parallax2D)
namespace Conqueror
{
    class Scene;

    struct RemoteTransformComponent
    {
        uint64_t Target = 0;
        bool UseGlobalCoordinates = true;
        bool UpdatePosition = true;
        bool UpdateRotation = true;
        bool UpdateScale = true;
    };

    enum class RayCastSpace : int { Physics3D = 0, Physics2D };

    struct RayCastComponent
    {
        bool Enabled = true;
        RayCastSpace Space = RayCastSpace::Physics3D;
        glm::vec3 TargetPosition{ 0.0f, -1.0f, 0.0f };   // yerel uç nokta (ışın kökten buraya)
        uint32_t CollisionMask = 0xFFFFFFFFu;
        bool ExcludeParent = true;                         // kendi gövdesini / atasını yok say
        bool HitTriggers = false;

        // Çalışma zamanı
        bool Colliding = false;
        glm::vec3 CollisionPoint{ 0.0f };
        glm::vec3 CollisionNormal{ 0.0f, 1.0f, 0.0f };
        float Distance = 0.0f;
        uint32_t ColliderEntity = 0xFFFFFFFFu;
        std::string Collider;                              // bilgi: çarpılan nesnenin adı
    };

    struct VisibleOnScreenNotifierComponent
    {
        bool AutoBounds = true;                            // Mesh / Model / Sprite sınırlarından
        glm::vec3 BoundsCenter{ 0.0f };
        glm::vec3 BoundsSize{ 1.0f };

        // Çalışma zamanı
        bool OnScreen = false;
        bool Initialized = false;
    };

    struct Parallax2DComponent
    {
        glm::vec2 ScrollScale{ 0.5f, 0.5f };               // 1 = dünya ile, 0 = kameraya yapışık
        glm::vec2 ScrollOffset{ 0.0f };
        glm::vec2 Autoscroll{ 0.0f };                      // birim / saniye
        glm::vec2 RepeatSize{ 0.0f };                      // > 0: katman kameranın yanında tekrarlanır (kopyalar bu aralıkla dizilmeli)

        // Çalışma zamanı
        bool HasBase = false;
        glm::vec3 BasePosition{ 0.0f };
        glm::vec2 CameraStart{ 0.0f };
        glm::vec2 AutoOffset{ 0.0f };
    };

    namespace GodotNodes
    {
        // runtime = oyun; düzenleyicide yalnızca Remote Transform çalışır
        CQ_API void Update(Scene& scene, float dt, bool runtime);
        // Işını hemen yeniden at (RayCast.force_raycast_update)
        CQ_API bool ForceRayCast(Scene& scene, uint32_t entity);
    }

    void RegisterGodotNodeComponents();
}
