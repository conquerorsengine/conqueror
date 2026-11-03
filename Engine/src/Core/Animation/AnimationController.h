#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Conqueror
{
    // Özel düğüm adları (geçişlerde From/To olarak kullanılır)
    namespace AnimNodeNames
    {
        inline constexpr const char* Entry = "Entry";
        inline constexpr const char* AnyState = "Any State";
        inline constexpr const char* Exit = "Exit";
    }

    enum class AnimParameterType
    {
        Float, Bool, Int, Trigger
    };

    enum class AnimConditionMode
    {
        Greater, Less, Equals, NotEquals,
        If,      // Bool/Trigger true
        IfNot    // Bool false
    };

    struct AnimCondition
    {
        std::string ParameterName;
        AnimConditionMode Mode = AnimConditionMode::Greater;
        float Threshold = 0.f;
    };

    enum class AnimInterruptionSource
    {
        None = 0, Source, Destination, SourceThenDestination, DestinationThenSource
    };

    struct AnimTransition
    {
        std::string FromState;           // durum / sub-state / "Any State" / "Entry"
        std::string ToState;             // durum / sub-state / "Exit"
        float Duration = 0.25f;          // FixedDuration ise saniye, değilse normalize
        float ExitTime = 0.8f;           // normalize
        bool HasExitTime = true;
        bool FixedDuration = true;
        float Offset = 0.f;              // hedef durumun başlangıç normalize zamanı
        AnimInterruptionSource Interruption = AnimInterruptionSource::None;
        bool OrderedInterruption = true;
        bool CanTransitionToSelf = false; // yalnızca Any State geçişlerinde anlamlı
        bool Mute = false;
        bool Solo = false;
        std::string Name;
        std::vector<AnimCondition> Conditions;
    };

    enum class AnimStateType
    {
        State, BlendTree
    };

    // Unreal Anim Notify / Unity Animation Event: durumun (ya da montajın) normalize zamanı geçilince betiklere
    // OnAnimNotify(ad) gelir; Duration > 0 ise Notify State: OnAnimNotifyBegin(ad) ... OnAnimNotifyEnd(ad)
    // (durumdan erken çıkılsa da End gelir). "Sound:Assets/x.wav" adlı notify sesi varlığın konumunda çalar.
    struct AnimNotify
    {
        std::string Name;
        float Time = 0.0f;               // normalize (0..1)
        float Duration = 0.0f;           // normalize; > 0 = Notify State
    };

    enum class AnimNotifyEventKind : int
    {
        Notify = 0, Begin, End,
        MontageBlendingOut, MontageEnded
    };

    // Çalışma zamanında üretilen olay (Scene betiklere dağıtır)
    struct AnimNotifyEvent
    {
        std::string Name;
        AnimNotifyEventKind Kind = AnimNotifyEventKind::Notify;
        bool Interrupted = false;        // montaj olayları
    };

    // Montaj bölümü: Next boş = zaman çizelgesinde sonraki bölüme devam, "Stop" = bitir, ad = o bölüme atla
    struct AnimMontageSection
    {
        std::string Name;
        float Start = 0.0f;              // normalize (klip uzunluğu)
        std::string Next;
    };

    // Unreal Anim Montage: durum makinesinin üstünde oynatılan tek seferlik klip (saldırı, etkileşim, kombo);
    // bölümler (sections) ile atlama, karışma girişi/çıkışı ve kemik filtresi (FromBone: yalnızca o kemik ve
    // altı — "Layered Blend per Bone", ör. koşarken üst gövdeyle saldırı).
    struct CQ_API AnimMontage
    {
        std::string Name;
        std::string ClipName;
        int ClipIndex = -1;
        float Rate = 1.0f;
        float BlendIn = 0.15f;
        float BlendOut = 0.2f;
        std::string FromBone;
        std::vector<AnimMontageSection> Sections;
        std::vector<AnimNotify> Notifies;

        bool Serialize(const std::string& filepath) const;
        static std::shared_ptr<AnimMontage> Load(const std::string& filepath);
        // Bölüm adı -> bölüm (yoksa nullptr)
        const AnimMontageSection* FindSection(const std::string& name) const;
    };

    // Oynayan montajın çalışma zamanı durumu (AnimationComponent içinde)
    struct AnimMontageInstance
    {
        std::shared_ptr<AnimMontage> Montage;
        bool Active = false;
        bool BlendingOut = false;
        bool Interrupted = false;
        bool Fresh = true;                // ilk karede başlangıçtaki notify'lar da ateşlenir
        float Time = 0.0f;                // saniye
        float Weight = 0.0f;
        float Rate = 1.0f;
        float BlendOutTime = 0.2f;
        std::string Section;
        std::vector<std::pair<std::string, std::string>> NextSectionOverrides;   // (bölüm, sonraki)
        std::vector<std::pair<std::string, std::string>> ActiveNotifyStates;
    };

    enum class AnimBlendTreeType
    {
        Simple1D = 0,
        SimpleDirectional2D,
        FreeformDirectional2D,
        FreeformCartesian2D,
        Direct
    };

    struct AnimBlendChild
    {
        int ClipIndex = 0;
        std::string ClipName;
        float Threshold = 0.f;            // 1D
        glm::vec2 Position{ 0.f };        // 2D
        float TimeScale = 1.f;
        std::string DirectParameter;      // Direct
        bool Mirror = false;
    };

    struct AnimState
    {
        std::string Name;
        AnimStateType Type = AnimStateType::State;
        std::string ClipName;
        int ClipIndex = 0;
        float Speed = 1.f;
        bool Loop = true;
        float CycleOffset = 0.f;
        bool Mirror = false;
        bool FootIK = false;
        bool WriteDefaults = true;
        std::string MotionTimeParameter;  // doluysa normalize zamanı bu parametre sürer
        std::string SpeedParameter;       // doluysa hız bu parametreyle çarpılır
        std::string Tag;
        glm::vec2 EditorPosition{ 0.f };
        std::vector<std::string> Behaviours;
        std::vector<AnimNotify> Notifies;

        // Blend tree
        // Doluysa örnekler bu Blend Space varlığından (.cqblendspace) gelir: tür (1D / 2D) ve çocuklar yüklenirken kurulur
        std::string BlendSpace;
        AnimBlendTreeType BlendType = AnimBlendTreeType::Simple1D;
        std::string BlendParameter;
        std::string BlendParameterY;
        std::vector<AnimBlendChild> BlendChildren;
    };

    // Bir durum makinesi: katmanın kökü ya da iç içe bir Sub-State Machine
    struct AnimStateMachine
    {
        std::string Name;
        std::string DefaultState;
        std::vector<AnimState> States;
        std::vector<AnimTransition> Transitions;
        std::vector<AnimStateMachine> SubStates;   // iç içe sub-state machine'ler

        // Editör yerleşimi
        glm::vec2 EditorPosition{ 0.f };            // üst makinede bu düğümün konumu
        glm::vec2 EntryPosition{ 50.f, 200.f };
        glm::vec2 AnyStatePosition{ 50.f, 330.f };
        glm::vec2 ExitPosition{ 760.f, 200.f };
        glm::vec2 ParentPosition{ 420.f, 60.f };    // "(Up)" düğümü (sub-state içinde)
    };

    // Eski isim (geriye uyumluluk)
    using AnimSubStateData = AnimStateMachine;

    enum class AnimLayerBlending
    {
        Override = 0, Additive
    };

    struct AnimLayer : AnimStateMachine
    {
        float Weight = 1.f;
        bool Solo = false;
        bool Mute = false;
        AnimLayerBlending Blending = AnimLayerBlending::Override;
        // Avatar Mask / Layered blend per bone: doluysa katman yalnızca bu kemiği ve altını etkiler (ör. "spine_01")
        std::string FromBone;
        // Avatar Mask varlığı (.cqmask, Unity Layer > Mask): doluysa katman yalnız maskedeki kemikleri karıştırır
        std::string AvatarMask;
        // Doluysa katman ağırlığı bu float parametreden okunur (0..1; ör. nişan alırken üst gövde)
        std::string WeightParameter;

        AnimLayer() { Name = "Base Layer"; }
    };

    struct AnimParameter
    {
        std::string Name;
        AnimParameterType Type = AnimParameterType::Float;
        float DefaultValue = 0.f;
    };

    // Bir katmanın çalışma zamanı durumu (AnimationComponent içinde tutulur)
    struct AnimLayerRuntime
    {
        std::string CurrentState;
        float StateTime = 0.f;            // saniye (durum hızı uygulanmış)
        float PreviousNormalized = 0.f;
        float CurrentNormalized = 0.f;    // editör gösterimi
        float CurrentDuration = 0.f;
        // Blend tree süresi karışım ağırlıklarıyla değişir (yürüme 1.3 s, koşu 0.9 s, depar 0.7 s): birikmiş saniye hangi
        // süreyle hesaplandıysa o tutulur; süre değişince aynı normalize faza ölçeklenir (Unity / Unreal senkron karışım).
        // Aksi halde hız değişince adım fazı sıçrar (Shift'e dokununca, ani ters yöne dönünce bacaklar öne-arkaya titrer)
        float TimeDuration = 0.f;
        std::string TimeDurationState;
        float NextTimeDuration = 0.f;
        std::string NextDurationState;

        bool InTransition = false;
        std::string NextState;
        float NextTime = 0.f;
        float NextNormalized = 0.f;
        float TransitionElapsed = 0.f;
        float TransitionDuration = 0.f;
        AnimTransition ActiveTransition;

        float EffectiveWeight = 1.f;

        // Açık Notify State'ler: (durum, notify adı)
        std::vector<std::pair<std::string, std::string>> ActiveNotifyStates;
    };

    class CQ_API AnimationController
    {
    public:
        std::vector<AnimLayer> Layers;
        std::vector<AnimParameter> Parameters;
        int NextNodeID = 1;

        bool Serialize(const std::string& filepath) const;
        // .cqcont ya da .cqoverride (Animator Override Controller: temel controller + klip değiştirme listesi)
        static std::shared_ptr<AnimationController> Deserialize(const std::string& filepath);

        // Animator Override Controller (Unity): klip adı -> yeni klip adı. Override yüklenince durumların klip adları
        // değişir, dizinleri modelden ada göre bulunur (ilk güncellemede ResolveClips)
        bool PendingClipResolve = false;
        void ResolveClips(const struct Model& model);
        // Temel controller'daki tüm klip adları (durumlar + blend tree çocukları, sırayla, tekrarsız)
        std::vector<std::string> CollectClipNames() const;
        // Blend Space varlığını duruma uygula: tür + çocuklar (klip dizinleri modelden ada göre, ResolveClips)
        static bool ApplyBlendSpace(AnimState& state);
        // Varsayılan "Base Layer" içeren yeni controller
        static std::shared_ptr<AnimationController> CreateDefault();

        AnimLayer* GetLayer(const std::string& name);
        // Katman kökünde arar (eski API)
        AnimState* GetState(AnimLayer& layer, const std::string& name);
        AnimState* GetDefaultState(AnimLayer& layer);

        // Makine ağacında (iç içe dahil) durum arama; owner: durumu içeren makine
        static AnimState* FindState(AnimStateMachine& machine, const std::string& name, AnimStateMachine** owner = nullptr);
        static const AnimState* FindState(const AnimStateMachine& machine, const std::string& name, const AnimStateMachine** owner = nullptr);
        static AnimStateMachine* FindSubMachine(AnimStateMachine& machine, const std::string& name, AnimStateMachine** parent = nullptr);
        static const AnimStateMachine* FindSubMachine(const AnimStateMachine& machine, const std::string& name, const AnimStateMachine** parent = nullptr);
        // Bir makinedeki (ve alt makinelerdeki) tüm durum/sub-state adları benzersiz mi
        static bool IsNameUsed(const AnimStateMachine& root, const std::string& name);
        static std::string MakeUniqueName(const AnimStateMachine& root, const std::string& base);
        // Adı tüm geçişlerde ve varsayılan durum referanslarında değiştirir
        static void RenameNode(AnimStateMachine& root, const std::string& oldName, const std::string& newName);
        // Bir parametrenin adını tüm koşullarda/blend tree'lerde değiştirir
        void RenameParameter(const std::string& oldName, const std::string& newName);
        AnimParameter* FindParameter(const std::string& name);
    };
}
