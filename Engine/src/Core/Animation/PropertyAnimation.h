#pragma once

#include "Core/Base/Base.h"
#include "Timeline/Timeline.h"

#include <string>
#include <vector>

// Animation Clip (.cqanim, Unity Create > Animation): nesnenin ve alt nesnelerinin bileşen alanlarını zamanla değiştiren
// anahtarlar (Transform konum / dönüş / ölçek, ışık şiddeti, malzeme rengi, herhangi bir sayı / vektör / renk alanı) ve
// Animation Event'ler (zamanında betik metodu çağırır).
//  - Bağlama yolu: canlandırılan kökten alt nesne adları ("" kökün kendisi, "Door/Handle").
//  - Oynatma: Animation bileşeni (Unity Animation: Play Automatically, Wrap Mode, Speed) ya da betikten AnimationPlay.
//  - Düzenleme: Animation penceresi (anahtar ekle, kayıt modu, dopesheet, eğri görünümü).
namespace Conqueror
{
    class Scene;
    class Entity;

    struct AnimCurveBinding
    {
        std::string Path;        // kökten alt nesne yolu ("" = kök)
        std::string Component;   // "Transform" ya da bileşen tür adı ("PointLightComponent")
        std::string Field;       // Transform: "Position" / "Rotation" / "Scale"; diğerleri alan adı
        TimelineChannel Channel; // anahtarlar (Rotation derece)
    };

    struct AnimEventDef
    {
        float Time = 0.0f;
        std::string Function;    // kökteki betik metodu
        std::string Argument;
    };

    enum class AnimWrapMode : int { Default = 0, Once, Loop, PingPong, ClampForever };

    struct PropertyClip
    {
        float Length = 1.0f;
        float FrameRate = 60.0f;
        bool Loop = true;
        std::vector<AnimCurveBinding> Curves;
        std::vector<AnimEventDef> Events;

        float ContentEnd() const;   // son anahtar / olay
        AnimCurveBinding* Find(const std::string& path, const std::string& component, const std::string& field);
    };

    // Unity "Animation" bileşeni: bir klibi doğrudan oynatır (Animator gerektirmez)
    struct AnimationPlayerComponent
    {
        std::string Clip;                         // .cqanim (varsayılan klip)
        std::vector<std::string> Clips;           // Unity "Animations" listesi: AnimationPlay(nesne, "Ad") ile oynatılır
        bool PlayAutomatically = true;
        AnimWrapMode WrapMode = AnimWrapMode::Default;   // Default: klibin Loop ayarı
        float Speed = 1.0f;

        // Çalışma zamanı (kaydedilmez)
        float Time = 0.0f;
        bool Playing = false;
        bool Started = false;
        std::string PlayingClip;
    };

    namespace PropertyClips
    {
        inline constexpr const char* Extension = ".cqanim";

        CQ_API bool Load(const std::string& path, PropertyClip& out, std::string* error = nullptr);
        CQ_API bool Save(const std::string& path, const PropertyClip& clip);
        // Önbellekli (dosya değişince yeniden okunur); yoksa nullptr
        CQ_API const PropertyClip* Get(const std::string& path);
        CQ_API void Invalidate(const std::string& path);

        // Kökten alt nesne (ad yolu)
        CQ_API Entity ResolvePath(Entity root, const std::string& path);
        // Nesnenin kökten yolu (Animation penceresi bağlama yolu üretir)
        CQ_API std::string PathOf(Entity root, Entity target);
        // Alan değeri (vec4) oku / yaz: Transform ya da kayıtlı bileşen alanı
        CQ_API bool ReadValue(Entity target, const std::string& component, const std::string& field, glm::vec4& out);
        CQ_API bool WriteValue(Entity target, const std::string& component, const std::string& field, const glm::vec4& value);
        // Klibi t anında köke uygula
        CQ_API void Apply(Entity root, const PropertyClip& clip, float time);
        // (from, to] aralığındaki olaylar kökün betiklerinde çağrılır
        CQ_API void FireEvents(Scene& scene, Entity root, const PropertyClip& clip, float from, float to);

        // Oyun: Animation bileşenlerini ilerlet
        CQ_API void Update(Scene& scene, float dt);
        // clip: boş = varsayılan klip; dosya yolu ya da bileşenin kliplerinden birinin adı ("DoorOpen")
        CQ_API bool Play(Entity entity, const std::string& clip = std::string());
        CQ_API std::string ResolveClip(const AnimationPlayerComponent& a, const std::string& nameOrPath);
        CQ_API void Stop(Entity entity);
        CQ_API void RegisterComponent();
    }
}
