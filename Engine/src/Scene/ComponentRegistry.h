#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace YAML
{
    class Emitter;
    class Node;
}

// Yansıma tabanlı bileşen kaydı: alan tanımlarından genel serileştirme, kopyalama,
// denetçi arayüzü (Editor), "Add Component" menüsü ve betik erişimi üretilir.
namespace Conqueror
{
    enum class FieldKind : int
    {
        Bool = 0, Int, Float, Angle, Vec2, Vec3, Vec4, Color3, Color4, String, Enum, EntityRef, LayerMask, Asset, Vec2List, Vec3List
    };

    struct ComponentField
    {
        std::string Name;                    // serileştirme / betik anahtarı
        std::string Label;                   // arayüz etiketi
        FieldKind Kind = FieldKind::Float;
        std::function<void*(void*)> Ptr;     // bileşen -> alan
        float Min = 0.0f, Max = 0.0f, Speed = 0.05f;
        std::string Format;
        std::string Tooltip;
        std::vector<std::string> Options;    // Enum seçenekleri
        std::string AssetFilter;             // Asset: ".png;.jpg"
        std::string Header;                  // bu alandan önce çizilen bölüm başlığı
        std::function<bool(const void*)> VisibleIf;
        bool ReadOnly = false;               // çalışma zamanı bilgisi
        bool Serialize = true;
        int EnumSize = 4;                    // enum altta yatan tip boyutu
        // Sanal alan (Ptr yok): metin erişimcileri (ör. doku yolu <-> Texture2D)
        std::function<std::string(void*)> GetString;
        std::function<void(void*, const std::string&)> SetString;
    };

    struct ComponentInfo
    {
        std::string Name;                    // "Character Controller"
        std::string TypeName;                // "CharacterControllerComponent" (YAML anahtarı)
        std::string ScriptName;              // "CharacterController" (betik adı)
        std::vector<std::string> Aliases;    // eski adlar (ör. Mesh Renderer'a katılan "Model"): betikler / otomasyon çalışmaya devam eder
        std::string Category;                // "Physics"
        std::string Icon;                    // Font Awesome glifi (UTF-8)
        glm::vec4 IconColor{ 0.55f, 0.80f, 1.0f, 1.0f };
        std::string Description;
        std::vector<ComponentField> Fields;
        std::vector<std::string> Requires;   // otomatik eklenen bileşenler (TypeName / ScriptName)
        bool AllowInAddMenu = true;
        // Elle yazılmış bileşen (serileştirme/denetçi/kopya kendi kodunda): kayıt yalnızca betik erişimi için
        bool Builtin = false;

        std::function<bool(Entity)> Has;
        std::function<void*(Entity)> Get;
        std::function<void(Entity)> Add;
        std::function<void(Entity)> Remove;
        std::function<void(Entity, Entity)> Copy;   // kaynak -> hedef (hedefte yoksa eklenir)
        std::function<void(Entity)> Reset;
        std::function<void(Entity)> OnAddedInEditor; // ör. çarpıştırıcıyı ağa sığdır
        // İsteğe bağlı: denetçide alanlardan sonra özel çizim (Editor kaydeder)
        std::function<void(Entity)> ExtraInspector;
        // İsteğe bağlı: alanların ötesinde ek veri (ör. karo haritası hücreleri)
        std::function<void(YAML::Emitter&, void*)> CustomSerialize;
        std::function<void(const YAML::Node&, void*)> CustomDeserialize;
    };

    class CQ_API ComponentRegistry
    {
    public:
        static std::vector<ComponentInfo>& All();
        static ComponentInfo* Find(const std::string& typeOrName);
        static void Register(ComponentInfo info);
        static const ComponentField* FindField(const ComponentInfo& info, const std::string& name);

        static void SerializeEntity(YAML::Emitter& out, Entity entity);
        static void DeserializeEntity(const YAML::Node& entityNode, Entity entity);
        static void CopyAll(Entity source, Entity destination);
        static void AddWithRequirements(const ComponentInfo& info, Entity entity);

        // Alan değeri metin olarak (betik / otomasyon / hata ayıklama)
        static std::string FieldToString(const ComponentField& field, void* component);
        static bool FieldFromString(const ComponentField& field, void* component, const std::string& text);
    };

    // Kayıt yardımcı: ComponentDef<T>("Character Controller", "CharacterControllerComponent", "Physics", icon).Float(...)...
    template<typename T>
    class ComponentDef
    {
    public:
        ComponentDef(const char* name, const char* typeName, const char* category, const char* icon)
        {
            m_Info.Name = name;
            m_Info.TypeName = typeName;
            std::string script = typeName;
            if (script.size() > 9 && script.compare(script.size() - 9, 9, "Component") == 0)
                script.resize(script.size() - 9);
            m_Info.ScriptName = script;
            m_Info.Category = category;
            m_Info.Icon = icon;
            m_Info.Has = [](Entity e) { return e.HasComponent<T>(); };
            m_Info.Get = [](Entity e) -> void* { return e.HasComponent<T>() ? (void*)&e.GetComponent<T>() : nullptr; };
            m_Info.Add = [](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); };
            m_Info.Remove = [](Entity e) { if (e.HasComponent<T>()) e.RemoveComponent<T>(); };
            m_Info.Copy = [](Entity src, Entity dst) {
                if (!src.HasComponent<T>()) return;
                const T copy = src.GetComponent<T>();
                if (!dst.HasComponent<T>()) dst.AddComponent<T>();
                dst.GetComponent<T>() = copy;
            };
            m_Info.Reset = [](Entity e) { if (e.HasComponent<T>()) e.GetComponent<T>() = T{}; };
        }

        ~ComponentDef() { ComponentRegistry::Register(std::move(m_Info)); }

        ComponentDef& Color(const glm::vec4& c) { m_Info.IconColor = c; return *this; }
        ComponentDef& Describe(const char* d) { m_Info.Description = d; return *this; }
        ComponentDef& Requires(const char* typeName) { m_Info.Requires.push_back(typeName); return *this; }
        ComponentDef& HiddenFromMenu() { m_Info.AllowInAddMenu = false; return *this; }
        ComponentDef& Builtin() { m_Info.Builtin = true; m_Info.AllowInAddMenu = false; return *this; }
        ComponentDef& ScriptName(const char* name) { m_Info.ScriptName = name; return *this; }
        ComponentDef& Alias(const char* name) { m_Info.Aliases.push_back(name); return *this; }
        ComponentDef& OnAddedInEditor(std::function<void(Entity)> fn) { m_Info.OnAddedInEditor = std::move(fn); return *this; }
        ComponentDef& CustomData(std::function<void(YAML::Emitter&, T&)> save, std::function<void(const YAML::Node&, T&)> load)
        {
            m_Info.CustomSerialize = [save](YAML::Emitter& out, void* c) { save(out, *static_cast<T*>(c)); };
            m_Info.CustomDeserialize = [load](const YAML::Node& n, void* c) { load(n, *static_cast<T*>(c)); };
            return *this;
        }

        template<typename M>
        ComponentDef& Field(const char* name, const char* label, M member, FieldKind kindOverride = (FieldKind)-1)
        {
            using V = std::remove_reference_t<decltype(std::declval<T&>().*member)>;
            ComponentField f;
            f.Name = name;
            f.Label = label;
            f.Ptr = [member](void* c) -> void* { return (void*)&(static_cast<T*>(c)->*member); };
            DeduceKind<V>(f, kindOverride);
            m_Info.Fields.push_back(std::move(f));
            return *this;
        }

        // İç içe üyeler için erişimciyle tanımlanan alan (ör. [](Scrollbar& c) -> bool& { return c.Selectable.Interactable; })
        template<typename F>
        ComponentDef& FieldVia(const char* name, const char* label, F accessor, FieldKind kindOverride = (FieldKind)-1)
        {
            using V = std::remove_reference_t<decltype(accessor(std::declval<T&>()))>;
            ComponentField f;
            f.Name = name;
            f.Label = label;
            f.Ptr = [accessor](void* c) -> void* { return (void*)&accessor(*static_cast<T*>(c)); };
            DeduceKind<V>(f, kindOverride);
            m_Info.Fields.push_back(std::move(f));
            return *this;
        }

        template<typename V>
        static void DeduceKind(ComponentField& f, FieldKind kindOverride)
        {
            if ((int)kindOverride >= 0) f.Kind = kindOverride;
            else if constexpr (std::is_same_v<V, bool>) f.Kind = FieldKind::Bool;
            else if constexpr (std::is_enum_v<V>) { f.Kind = FieldKind::Enum; f.EnumSize = (int)sizeof(V); }
            else if constexpr (std::is_same_v<V, uint64_t>) f.Kind = FieldKind::EntityRef;
            else if constexpr (std::is_same_v<V, uint32_t>) f.Kind = FieldKind::LayerMask;
            else if constexpr (std::is_integral_v<V>) f.Kind = FieldKind::Int;
            else if constexpr (std::is_same_v<V, float>) f.Kind = FieldKind::Float;
            else if constexpr (std::is_same_v<V, glm::vec2>) f.Kind = FieldKind::Vec2;
            else if constexpr (std::is_same_v<V, glm::vec3>) f.Kind = FieldKind::Vec3;
            else if constexpr (std::is_same_v<V, glm::vec4>) f.Kind = FieldKind::Vec4;
            else if constexpr (std::is_same_v<V, std::string>) f.Kind = FieldKind::String;
            else if constexpr (std::is_same_v<V, std::vector<glm::vec2>>) f.Kind = FieldKind::Vec2List;
            else if constexpr (std::is_same_v<V, std::vector<glm::vec3>>) f.Kind = FieldKind::Vec3List;
            else static_assert(sizeof(V) == 0, "Unsupported component field type");
            if constexpr (std::is_enum_v<V>) f.EnumSize = (int)sizeof(V);
        }

        // Üye yerine erişimcilerle tanımlanan metin/varlık alanı (ör. "Texture": yol <-> Texture2D)
        ComponentDef& Property(const char* name, const char* label, FieldKind kind, std::function<std::string(const T&)> get,
                               std::function<void(T&, const std::string&)> set)
        {
            ComponentField f;
            f.Name = name;
            f.Label = label;
            f.Kind = kind;
            f.GetString = [get](void* c) { return get(*static_cast<const T*>(c)); };
            f.SetString = [set](void* c, const std::string& v) { set(*static_cast<T*>(c), v); };
            m_Info.Fields.push_back(std::move(f));
            return *this;
        }

        // Son alanın ayarları
        ComponentDef& Range(float min, float max, float speed = 0.05f) { Last().Min = min; Last().Max = max; Last().Speed = speed; return *this; }
        ComponentDef& Speed(float speed) { Last().Speed = speed; return *this; }
        ComponentDef& Format(const char* fmt) { Last().Format = fmt; return *this; }
        ComponentDef& Tooltip(const char* text) { Last().Tooltip = text; return *this; }
        ComponentDef& Options(std::vector<std::string> options) { Last().Options = std::move(options); return *this; }
        ComponentDef& Header(const char* header) { Last().Header = header; return *this; }
        ComponentDef& ReadOnly() { Last().ReadOnly = true; Last().Serialize = false; return *this; }
        ComponentDef& NoSerialize() { Last().Serialize = false; return *this; }
        ComponentDef& Filter(const char* filter) { Last().AssetFilter = filter; return *this; }
        ComponentDef& VisibleIf(std::function<bool(const T&)> pred)
        {
            Last().VisibleIf = [pred](const void* c) { return pred(*static_cast<const T*>(c)); };
            return *this;
        }

    private:
        ComponentField& Last() { return m_Info.Fields.back(); }
        ComponentInfo m_Info;
    };

    // Yerleşik kayıtlar (ComponentRegistry::All ilk çağrıldığında)
    void RegisterPhysicsComponents();
    void RegisterBuiltinComponents();
    void RegisterGameplayComponents();
    void RegisterSplineComponents();
    void RegisterCameraRigComponents();
    void RegisterPostProcessVolumeComponents();
    void RegisterDecalComponents();
    void RegisterLighting2DComponents();
    void RegisterPixelPerfectComponents();
    void RegisterBillboardComponents();
    void RegisterFullScreenPassComponents();
    void RegisterCameraSkyboxComponents();
    void RegisterAreaLightComponents();
    void RegisterPlayerInputComponents();
    void RegisterRigComponents();
    void RegisterNavigationComponents();
    void RegisterVideoComponents();
    void RegisterGameplayTagComponents();
    void RegisterAbilitySystemComponents();
    void RegisterGameFrameworkComponents();
    void RegisterLevelStreamingComponents();
    void RegisterAutoLODComponents();
    void RegisterAudioOcclusionComponents();
    void RegisterVertexPaintComponents();
    void RegisterFractureComponents();
    void RegisterRagdollComponents();
    void RegisterVirtualGeometryComponents();
    void RegisterCharacterCustomizerComponents();
    void RegisterSignalComponents();
    void RegisterCSGComponents();
    void RegisterMultiMeshComponents();
    void RegisterGodotNodeComponents();
    void RegisterRenderTargetComponents();
    void RegisterSkeleton2DComponents();
    void RegisterGridComponents();
    void RegisterNetworkComponents();
    void RegisterTimelineComponents();
}
