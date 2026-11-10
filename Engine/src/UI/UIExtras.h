#pragma once

// Unity uGUI eşdeğerleri (20. madde: Unity'de olup motorda olmayan bileşenler): Toggle Group
#include "Core/Base/Base.h"
#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Conqueror
{
    class Scene;
    class Entity;

    // Toggle Group (Unity): altındaki toggle'lardan (Group alanı 0 olanlar) aynı anda yalnız biri açık. Allow Switch Off kapalıyken açık
    // olan toggle'a tıklamak onu kapatmaz (grupta hep bir seçim kalır)
    struct ToggleGroupComponent
    {
        bool AllowSwitchOff = false;
    };

    // Physics Raycaster (Unity): bu bileşen kameradayken fare bir UI öğesinin üstünde değilse kameradan fareye ışın atılır; çarpılan 3B
    // çarpıştırıcının nesnesi (ya da Event Trigger'ı olan en yakın üst nesnesi) Pointer Enter / Exit / Down / Up / Click / Drag / Scroll alır
    struct PhysicsRaycasterComponent
    {
        uint32_t EventMask = 0xFFFFFFFFu;   // ışının çarpabileceği katmanlar
        float MaxRayDistance = 0.0f;        // 0: kameranın uzak düzlemi
    };

    // Physics 2D Raycaster (Unity): aynısı 2B çarpıştırıcılar için (farenin dünyadaki noktasındaki çarpıştırıcı)
    struct Physics2DRaycasterComponent
    {
        uint32_t EventMask = 0xFFFFFFFFu;
    };

    // On-Screen Stick (Unity Input System): bir UI görüntüsüne eklenir; basılıp sürüklenince görüntü Movement Range içinde kayar ve sanal
    // gamepad'in sol / sağ çubuğunu (-1..1) sürer; bırakınca yerine döner. Oyuncu girdisi (Player Input, eylemler) gamepad gibi okur
    enum class OnScreenStickControl : int { LeftStick = 0, RightStick };
    enum class OnScreenButtonControl : int
    {
        South = 0, East, West, North, LeftShoulder, RightShoulder, Select, Start, Guide, LeftStickPress, RightStickPress,
        DpadUp, DpadRight, DpadDown, DpadLeft
    };

    struct OnScreenStickComponent
    {
        float MovementRange = 50.0f;   // canvas birimi
        OnScreenStickControl Control = OnScreenStickControl::LeftStick;
        // Çalışma zamanı
        bool Held = false;
        glm::vec2 PressPos{ 0.0f };
        glm::vec2 StartPos{ 0.0f };
    };

    // On-Screen Button (Unity Input System): UI öğesine basılıyken sanal gamepad düğmesi basılı
    struct OnScreenButtonComponent
    {
        OnScreenButtonControl Button = OnScreenButtonControl::South;   // GLFW gamepad düğme sırası
        bool Held = false;             // çalışma zamanı
    };

    struct UIInputState;

    // Selectable (Unity): herhangi bir UI öğesini (görüntü, yazı) etkileşimli yapar: üstüne gelince / basınca renk ya da ölçek geçişi,
    // klavye / gamepad gezinmesinde seçilebilir. Olaylar aynı nesnedeki Event Trigger'a gider
    struct UISelectableComponent
    {
        UISelectableState Selectable;
    };

    namespace UIExtras
    {
        // Ekran denetimleri (On-Screen Stick / Button): işaretçinin altındaki UI öğesiyle (hovered) her kare. Sonuç: biri basılı mı
        // (basılıyken sanal gamepad UI gezinmesini / Submit'i sürmez: Unity'de denetime dokunmak UI seçimini kaldırır)
        bool UpdateOnScreenControls(Scene& scene, const UIInputState& input, entt::entity hovered);
        // Farenin altındaki dünya nesnesi (birincil kamerada Physics / Physics 2D Raycaster varsa). mouse: piksel, sol-alt köken
        entt::entity WorldPointerTarget(Scene& scene, const glm::vec2& screenSize, const glm::vec2& mouse);
        void RegisterComponents();
        // Toggle'ın bağlı olduğu grup: kendisi ya da en yakın üst nesnesi (Toggle Group bileşeni olan); yoksa entt::null
        entt::entity FindToggleGroup(Scene& scene, entt::entity toggle);
        // Toggle'a tıklandı / onaylandı: grubu varsa gruptaki diğerlerini kapatır. Sonuç: toggle değişti mi
        bool ClickToggle(Scene& scene, entt::entity toggle);
    }
}
