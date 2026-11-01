#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// Tween (Godot Tween / DOTween): bir özelliği süre boyunca hedef değere yumuşak geçişle taşır.
// Özellikler: position / rotation (derece) / scale (yerel), color / alpha (Mesh Renderer, Sprite, UI Image / Text),
// ya da "Bileşen.Alan" (ör. "PointLight.Intensity") ile kayıtlı herhangi bir sayı / vektör alanı.
namespace Conqueror
{
    class Scene;

    enum class TweenEase : int
    {
        Linear = 0,
        SineIn, SineOut, SineInOut,
        QuadIn, QuadOut, QuadInOut,
        CubicIn, CubicOut, CubicInOut,
        ExpoIn, ExpoOut, ExpoInOut,
        BackIn, BackOut, BackInOut,
        ElasticOut, BounceOut
    };

    struct TweenDesc
    {
        uint32_t Entity = 0xFFFFFFFFu;
        std::string Property;
        glm::vec4 Target{ 0.0f };
        int Components = 1;               // hedefin boyutu (1..4)
        float Duration = 1.0f;
        float Delay = 0.0f;
        TweenEase Ease = TweenEase::SineInOut;
        int Loops = 0;                    // 0 bir kez, -1 sonsuz
        bool Yoyo = false;                // döngülerde ileri-geri
        bool Relative = false;            // hedef = başlangıç + değer
        std::string OnComplete;           // bittiğinde varlığın betik metodu
    };

    namespace Tweens
    {
        // Döndürür: tween kimliği (0 = özellik bulunamadı)
        CQ_API uint32_t Start(Scene& scene, const TweenDesc& desc);
        CQ_API bool Kill(Scene& scene, uint32_t id, bool complete = false);
        CQ_API int KillAll(Scene& scene, uint32_t entity);
        CQ_API bool IsActive(Scene& scene, uint32_t id);
        CQ_API void SetPaused(Scene& scene, uint32_t id, bool paused);
        CQ_API int ActiveCount(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void ForgetScene(Scene* scene);
        CQ_API TweenEase ParseEase(const std::string& name);
        CQ_API float Evaluate(TweenEase ease, float t);
    }
}
