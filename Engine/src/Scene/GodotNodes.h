#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Godot düğüm eşdeğerleri:
//  - Remote Transform: kendi dönüşümünü başka bir nesneye iter (hiyerarşi dışı takip; RemoteTransform3D/2D)
//  - Ray Cast: her karede güncellenen kalıcı ışın (IsColliding / nokta / normal / çarpılan; RayCast3D/2D)
//  - Visible On Screen Notifier: ana kameranın görüşüne girip çıkınca sinyal ve OnBecameVisible/Invisible
//  - Parallax 2D: kamera kaydıkça katmanı ölçekli kaydırır, otomatik kayma ve sonsuz tekrar (Parallax2D)
//  - Timer: geri sayar, bitince "timeout" sinyali ve OnTimeout; tek atış değilse yeniden başlar (Timer)
//  - Shape Cast: şekli (küre / kutu / kapsül) hedefe doğru süpürür, çarpılanları yakından uzağa listeler (ShapeCast3D/2D)
//  - Visible On Screen Enabler: ekran dışındayken hedefin işlenmesini durdurur (VisibleOnScreenEnabler3D/2D)
//  - Character Body 2D: kinematik 2D karakter, move_and_slide (yüzeylerde kayma, eğim, zemine yapışma; CharacterBody2D)
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

    // Godot VisibleOnScreenEnabler3D / 2D: sınırları ana kameranın görüşünden çıkınca hedefin (boşsa üst nesnenin) işlenmesi durur —
    // betiklerin Update / FixedUpdate'i, Animator, parçacıklar, Timer (nesne çizilmeye devam eder); görüşe girince kaldığı yerden sürer
    struct VisibleOnScreenEnablerComponent
    {
        uint64_t Target = 0;                               // 0: üst nesne
        bool AutoBounds = true;
        glm::vec3 BoundsCenter{ 0.0f };
        glm::vec3 BoundsSize{ 1.0f };

        // Çalışma zamanı
        bool OnScreen = false;
        bool Initialized = false;
        uint32_t AppliedTo = 0xFFFFFFFFu;
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

    // Godot ShapeCast3D / ShapeCast2D: şekil nesnenin konumundan Target Position'a (yerel) her karede süpürülür; çarpılan nesneler
    // yakından uzağa (nesne başına bir sonuç, en çok Max Results). Safe Fraction: şeklin değmeden gidebildiği oran (0..1)
    enum class ShapeCastShape : int { Sphere = 0, Box, Capsule };

    struct ShapeCastComponent
    {
        bool Enabled = true;
        RayCastSpace Space = RayCastSpace::Physics3D;
        ShapeCastShape Shape = ShapeCastShape::Sphere;
        float Radius = 0.5f;                               // küre / kapsül (2D: daire / kapsül)
        glm::vec3 Size{ 1.0f };                            // kutu (tam boyut)
        float Height = 2.0f;                               // kapsül toplam boy (Y ekseni)
        glm::vec3 TargetPosition{ 0.0f, -1.0f, 0.0f };
        int MaxResults = 32;
        uint32_t CollisionMask = 0xFFFFFFFFu;
        bool ExcludeParent = true;
        bool HitTriggers = false;

        // Çalışma zamanı
        struct Result { glm::vec3 Point{ 0.0f }, Normal{ 0.0f, 1.0f, 0.0f }; float Distance = 0.0f; uint32_t Entity = 0xFFFFFFFFu; };
        std::vector<Result> Results;
        bool Colliding = false;
        int CollisionCount = 0;
        float SafeFraction = 1.0f;
        std::string Collider;                              // bilgi: en yakın
    };

    // Godot CharacterBody2D: kinematik 2D karakter. Betik Velocity'yi ayarlar ve CharacterBody2DMoveAndSlide çağırır: karakter iç içe
    // olduğu şekillerden çıkar, hız x süre kadar yüzeylerde kayarak ilerler, eğimlere tırmanır (Floor Max Angle), yokuş aşağı zemine
    // yapışır (Floor Snap), duvara / tavana çarpınca hızın o yöne giren kısmı silinir. Şekil nesnenin 2D çarpıştırıcısından (kapsül /
    // daire / kutu). Auto Move: betiksiz, her karede Velocity + yerçekimiyle kendi ilerler
    enum class CharacterMotionMode : int { Grounded = 0, Floating };

    struct CharacterBody2DComponent
    {
        glm::vec2 Velocity{ 0.0f };                        // birim / saniye (betik ayarlar; move_and_slide sonrası çarpışmalarla kırpılır)
        CharacterMotionMode MotionMode = CharacterMotionMode::Grounded;
        glm::vec2 UpDirection{ 0.0f, 1.0f };
        float FloorMaxAngle = 45.0f;                       // derece: bundan dik yüzey duvardır
        float FloorSnapLength = 0.1f;                      // birim: yokuş / basamak aşağı zemine yapışma
        bool FloorStopOnSlope = true;                      // durunca eğimden kaymaz
        int MaxSlides = 4;
        uint32_t CollisionMask = 0xFFFFFFFFu;
        bool AutoMove = false;                             // betiksiz: her karede Velocity ile ilerle
        float GravityScale = 1.0f;                         // Auto Move: proje 2D yerçekimi çarpanı (havadayken)

        // Çalışma zamanı
        bool OnFloor = false, OnWall = false, OnCeiling = false;
        glm::vec2 FloorNormal{ 0.0f, 1.0f };
        glm::vec2 WallNormal{ 0.0f };
        glm::vec2 RealVelocity{ 0.0f };
        int SlideCollisionCount = 0;
    };

    // Godot Timer: Wait Time saniye geri sayar; bitince "timeout" sinyalini yayar ve nesnenin betiklerinde OnTimeout çağrılır.
    // One Shot değilse hemen yeniden başlar. Process Callback: Idle her karede, Physics sabit fizik adımında sayar
    enum class TimerProcess : int { Idle = 0, Physics };

    struct TimerComponent
    {
        float WaitTime = 1.0f;
        bool OneShot = false;
        bool Autostart = false;
        bool Paused = false;
        bool IgnoreTimeScale = false;      // zaman ölçeğinden (yavaş çekim / duraklatma) etkilenmez
        TimerProcess ProcessCallback = TimerProcess::Idle;

        // Çalışma zamanı
        float TimeLeft = 0.0f;
        bool Running = false;
        bool AutostartDone = false;
        int Timeouts = 0;                  // bilgi: bu oyunda kaç kez doldu
    };

    namespace GodotNodes
    {
        // runtime = oyun; düzenleyicide yalnızca Remote Transform çalışır
        CQ_API void Update(Scene& scene, float dt, bool runtime);
        // Işını hemen yeniden at (RayCast.force_raycast_update)
        CQ_API bool ForceRayCast(Scene& scene, uint32_t entity);
        // CharacterBody2D.move_and_slide(): dt saniyelik hareket (dt <= 0: bu karenin süresi); bir yüzeye değdiyse true
        CQ_API bool MoveAndSlide2D(Scene& scene, uint32_t entity, float dt = -1.0f);
        // Şekli hemen yeniden süpür (ShapeCast.force_shapecast_update)
        CQ_API bool ForceShapeCast(Scene& scene, uint32_t entity);
        // Sabit fizik adımı (Process Callback = Physics zamanlayıcıları)
        CQ_API void FixedUpdate(Scene& scene, float dt);
        // Timer.start(time_sec = -1) / stop()
        CQ_API bool TimerStart(Scene& scene, uint32_t entity, float time = -1.0f);
        CQ_API bool TimerStop(Scene& scene, uint32_t entity);
    }

    void RegisterGodotNodeComponents();
}
