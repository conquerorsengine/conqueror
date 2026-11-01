#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Unreal Gameplay Ability System (GAS) karşılığı, betiklerden kullanılır:
//  - Nitelikler (Attributes): Health, Mana, Stamina ... taban (Base) ve geçerli (Current) değer, alt/üst sınır
//  - Efektler (Gameplay Effects): anlık (Base'i değiştirir), süreli (Current'a geçici değiştirici), periyodik (her
//    periyotta Base'e uygulanır: zehir, iyileşme), yığılma (yenile / yığ / ayrı), süre boyunca verilen gameplay tag'ler
//  - Yetenekler (Abilities): bekleme süresi, maliyet (nitelik + miktar), gerekli / engelleyen tag'ler; etkinleşince
//    betiklere OnAbilityActivated(ad) gider, davranışı betik yazar
//  - Olaylar: OnAttributeChanged(ad, eski, yeni), OnAttributeDepleted(ad), OnEffectApplied(ad), OnEffectRemoved(ad)
namespace Conqueror
{
    class Scene;
    class Entity;

    struct GameplayAttribute
    {
        std::string Name;
        float Base = 0.0f;
        float Current = 0.0f;
        float Min = -std::numeric_limits<float>::infinity();
        float Max = std::numeric_limits<float>::infinity();
    };

    enum class ModifierOp : int { Add = 0, Multiply, Override };

    struct AttributeModifier
    {
        std::string Attribute;
        ModifierOp Op = ModifierOp::Add;
        float Value = 0.0f;
    };

    enum class EffectStacking : int { None = 0, Refresh, Stack };

    struct GameplayEffectSpec
    {
        std::string Name;
        float Duration = 0.0f;          // 0 anlık, < 0 sonsuz, > 0 saniye
        float Period = 0.0f;            // > 0: periyodik (her periyotta Base'e uygula)
        bool ExecuteOnApply = true;     // periyodik efektte ilk tik uygulanınca
        std::vector<AttributeModifier> Modifiers;
        std::vector<std::string> GrantedTags;
        std::vector<std::string> RequiredTags;    // hedefte yoksa uygulanmaz
        std::vector<std::string> BlockedTags;     // hedefte varsa uygulanmaz (ör. "Status.Immune")
        EffectStacking Stacking = EffectStacking::Refresh;
        int MaxStacks = 1;
    };

    struct ActiveGameplayEffect
    {
        uint32_t Handle = 0;
        GameplayEffectSpec Spec;
        float Remaining = 0.0f;
        float PeriodTimer = 0.0f;
        int Stacks = 1;
        uint32_t Source = 0xFFFFFFFFu;
    };

    struct GameplayAbilitySpec
    {
        std::string Name;
        float Cooldown = 0.0f;
        std::string CostAttribute;
        float CostAmount = 0.0f;
        std::vector<std::string> RequiredTags;
        std::vector<std::string> BlockedTags;
        float CooldownRemaining = 0.0f;
        int Activations = 0;
    };

    struct AbilitySystemComponent
    {
        // Inspector tanımları (satır başına bir öğe):
        //   Attributes: "Health 100 0 100"  (ad taban [min max])
        //   Abilities:  "Fireball cooldown=2 cost=Mana:20 blocked=Status.Stunned required=State.Armed"
        std::string Attributes = "Health 100 0 100\nMana 50 0 50";
        std::string Abilities;
        bool ShowDebug = false;

        // Çalışma zamanı
        bool Initialized = false;
        std::vector<GameplayAttribute> AttributeList;
        std::vector<ActiveGameplayEffect> Effects;
        std::vector<GameplayAbilitySpec> AbilityList;
        uint32_t NextHandle = 1;
        std::string LastFailure;
        std::string State;              // Inspector'da salt okunur özet (Show Debug açıkken)

        AbilitySystemComponent() = default;
        // Kopya yalnızca tanımları taşır: oyun moduna geçişte / prefab örneğinde çalışma durumu baştan kurulur
        AbilitySystemComponent(const AbilitySystemComponent& o) : Attributes(o.Attributes), Abilities(o.Abilities), ShowDebug(o.ShowDebug) {}
        AbilitySystemComponent& operator=(const AbilitySystemComponent& o)
        {
            if (this != &o)
            {
                Attributes = o.Attributes;
                Abilities = o.Abilities;
                ShowDebug = o.ShowDebug;
                Initialized = false;
                AttributeList.clear();
                Effects.clear();
                AbilityList.clear();
                State.clear();
            }
            return *this;
        }
    };

    namespace AbilitySystem
    {
        CQ_API AbilitySystemComponent* Get(Entity entity, bool create = false);
        CQ_API void EnsureInitialized(Entity entity);
        CQ_API GameplayAttribute* FindAttribute(Entity entity, const std::string& name);
        CQ_API float GetAttribute(Entity entity, const std::string& name, float fallback = 0.0f);
        CQ_API float GetAttributeBase(Entity entity, const std::string& name, float fallback = 0.0f);
        CQ_API bool SetAttributeBase(Entity entity, const std::string& name, float value);
        CQ_API bool DefineAttribute(Entity entity, const std::string& name, float base, float minValue, float maxValue);

        // 0 = uygulanmadı (gerekli / engelleyen tag), anlık efekt için 0 değil 1 döner (iz bırakmaz)
        CQ_API uint32_t ApplyEffect(Entity target, const GameplayEffectSpec& spec, uint32_t source = 0xFFFFFFFFu);
        CQ_API int RemoveEffect(Entity target, uint32_t handle);
        CQ_API int RemoveEffectsByName(Entity target, const std::string& name);
        CQ_API int GetEffectStacks(Entity target, const std::string& name);

        CQ_API bool GrantAbility(Entity entity, const GameplayAbilitySpec& spec);
        CQ_API bool TryActivateAbility(Entity entity, const std::string& name);
        CQ_API float GetCooldownRemaining(Entity entity, const std::string& name);

        // Tanım ayrıştırıcıları (Inspector metni, Data Table satırı)
        CQ_API bool ParseModifiers(const std::string& text, std::vector<AttributeModifier>& out);   // "Health:add:-10; Speed:mul:0.5"
        CQ_API std::vector<std::string> ParseTagList(const std::string& text);                     // "A.B, C" ya da "A.B|C"

        CQ_API void Update(Scene& scene, float dt);
    }

    void RegisterAbilitySystemComponents();
}
