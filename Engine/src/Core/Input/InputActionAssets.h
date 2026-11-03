#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Input Actions varlığı (.cqinputactions): Unity Input System'in Input Actions dosyası, Unreal Enhanced Input'un Input
// Action + Input Mapping Context karşılığı.
//  - Eylem haritaları (Player, UI, Vehicle ...) ve içlerindeki eylemler (Move, Jump, Fire ...).
//  - Eylem türü: Value (sürekli değer: hareket, bakış), Button (bas / bırak), Pass Through (her değişiklik).
//  - Bağlamalar: tek kontrol ("<Keyboard>/space", "<Gamepad>/leftStick") ya da composite (2D Vector: WASD, 1D Axis,
//    One / Two Modifiers: Ctrl+S gibi). Her bağlama kontrol şemalarına (Keyboard&Mouse, Gamepad ...) ait olabilir.
//  - Etkileşimler (Hold, Tap, Slow Tap, Multi Tap, Press) ve işlemciler (Invert, Normalize, Scale, Clamp, Deadzone).
// Player Input bileşeni "Actions" yuvasıyla kullanır; Project Settings > Input > Project-wide Actions ile betiklerin
// genel eylem sorguları (InputGetAction ...) da bu dosyadan okur. Düzenleyici: Input Actions penceresi.
namespace Conqueror
{
    enum class InputActionKind : int { Value = 0, Button, PassThrough };
    enum class InputControlKind : int { Any = 0, Button, Axis, Vector2 };

    // Bağlama: tek kontrol, composite kökü (Composite dolu) ya da composite parçası (Part dolu; kökten hemen sonra gelir)
    struct InputBindingDef
    {
        std::string Name;            // composite kökünün adı (WASD, Arrows ...)
        std::string Path;            // "<Keyboard>/w" (kökte boş)
        std::string Composite;       // "2DVector", "1DAxis", "OneModifier", "TwoModifiers" (kökte)
        std::string CompositeParams; // "mode=DigitalNormalized", "whichSideWins=Neither", "modifiersOrder=Default"
        std::string Part;            // up/down/left/right, negative/positive, modifier/modifier1/modifier2/binding
        std::string Interactions;    // "hold(duration=0.4),tap"
        std::string Processors;      // "invert,scale(factor=2)"
        std::vector<std::string> Groups;   // kontrol şemaları (boş: hepsinde)

        bool IsComposite() const { return !Composite.empty(); }
        bool IsPart() const { return !Part.empty(); }
        bool InScheme(const std::string& scheme) const;
    };

    struct InputActionDef
    {
        std::string Name;
        InputActionKind Kind = InputActionKind::Button;
        InputControlKind Control = InputControlKind::Button;
        std::string Interactions;
        std::string Processors;
        bool InitialStateCheck = false;
        std::vector<InputBindingDef> Bindings;
    };

    struct InputActionMapDef
    {
        std::string Name;
        std::vector<InputActionDef> Actions;
        const InputActionDef* Find(const std::string& name) const;
    };

    struct InputControlSchemeDef
    {
        std::string Name;
        std::vector<std::string> Devices;   // Keyboard, Mouse, Gamepad
        std::vector<bool> Optional;         // aygıt başına: isteğe bağlı mı (false: gerekli)
    };

    struct InputActionAssetData
    {
        std::vector<InputActionMapDef> Maps;
        std::vector<InputControlSchemeDef> Schemes;
        const InputActionMapDef* FindMap(const std::string& name) const;
        // Haritayı ada göre; yoksa ilk harita (Unity Player Input > Default Map gibi)
        const InputActionMapDef* FindMapOrFirst(const std::string& name) const;
    };

    // Kontrol kataloğu: "<Keyboard>/space" ... yolları, okuma biçimi ve görünen adları
    enum class InputControlSource : int
    {
        None = 0, Key, MouseButton, MouseDelta, MouseScroll, MouseScrollY, MousePosition,
        PadButton, PadStick, PadStickAxis, PadStickDir, PadTrigger, PadDpad, PadDpadDir
    };

    struct InputControlInfo
    {
        std::string Path;       // "<Keyboard>/space"
        std::string Device;     // Keyboard / Mouse / Gamepad
        std::string Display;    // "Space"
        std::string Category;   // seçici alt menüsü (Letters, Digits, Arrows, Sticks ...)
        InputControlSource Source = InputControlSource::None;
        int Code = 0;           // tuş kodu, fare / gamepad düğmesi, çubuk (0 sol, 1 sağ), tetik (0 sol, 1 sağ)
        int Extra = 0;          // eksen (0 x, 1 y) ya da yön (0 yukarı, 1 aşağı, 2 sol, 3 sağ)
        bool Is2D = false;      // değer 2 boyutlu (çubuk, fare hareketi, d-pad)
    };

    namespace InputActionAssets
    {
        inline constexpr const char* Extension = ".cqinputactions";

        CQ_API bool Load(const std::string& path, InputActionAssetData& out);
        CQ_API bool Save(const std::string& path, const InputActionAssetData& data);
        CQ_API std::string ToString(const InputActionAssetData& data);
        CQ_API bool FromString(const std::string& text, InputActionAssetData& out);
        // Önbellekli okuma: dosya değişince (düzenleyici kaydedince) yeniden okunur. Yol proje göreli ya da mutlak.
        CQ_API const InputActionAssetData* Get(const std::string& path);
        // Dosya her yeniden okunduğunda artar (çalışma zamanı durumunu sıfırlamak için)
        CQ_API uint64_t Stamp(const std::string& path);
        CQ_API void Invalidate(const std::string& path);
        // Yeni varlık: Player (Move, Look, Attack, Interact, Crouch, Jump, Previous, Next, Sprint) ve UI haritaları,
        // Keyboard&Mouse ve Gamepad şemaları
        CQ_API InputActionAssetData MakeDefault();
        CQ_API bool CreateDefault(const std::string& path);

        CQ_API const std::vector<InputControlInfo>& Controls();
        CQ_API const InputControlInfo* FindControl(const std::string& path);
        CQ_API std::string DeviceOf(const std::string& path);
        // Bağlamanın görünen adı: "W [Keyboard]", "WASD" (composite), "Up: W [Keyboard]" (parça)
        CQ_API std::string DisplayName(const InputBindingDef& binding);
        CQ_API std::string ControlDisplay(const std::string& path);

        // Etkileşim / işlemci dizgeleri: "hold(duration=0.4),tap" <-> [{hold, {duration: 0.4}}, {tap, {}}]
        struct Spec
        {
            std::string Name;
            std::vector<std::pair<std::string, float>> Params;
            float Get(const std::string& key, float def) const;
            void Set(const std::string& key, float value);
        };
        CQ_API std::vector<Spec> ParseSpecs(const std::string& text);
        CQ_API std::string FormatSpecs(const std::vector<Spec>& specs);
        // "key=value,key2=value2" (composite parametreleri)
        CQ_API std::string Param(const std::string& params, const std::string& key, const std::string& def);
        CQ_API std::string SetParam(const std::string& params, const std::string& key, const std::string& value);

        // Composite türleri ve parçaları
        CQ_API const std::vector<std::string>& CompositeTypes();          // 2DVector, 1DAxis, OneModifier, TwoModifiers
        CQ_API std::vector<std::string> CompositeParts(const std::string& composite);
        CQ_API std::string CompositeDisplay(const std::string& composite); // "2D Vector" ...
        // Etkileşim / işlemci katalogları: ad, görünen ad, parametreler (varsayılanlarıyla)
        struct SpecInfo
        {
            const char* Name;
            const char* Display;
            std::vector<std::pair<std::string, float>> Defaults;
            const char* Tooltip;
        };
        CQ_API const std::vector<SpecInfo>& InteractionCatalog();
        CQ_API const std::vector<SpecInfo>& ProcessorCatalog();
        CQ_API const SpecInfo* FindInteraction(const std::string& name);
        CQ_API const SpecInfo* FindProcessor(const std::string& name);

        CQ_API const char* KindName(InputActionKind kind);
        CQ_API const char* ControlName(InputControlKind kind);
    }
}
