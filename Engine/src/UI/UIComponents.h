#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;

    // Unity RectTransform: üst dikdörtgene göre çapa (anchor) + pivot + konum/boyut.
    // Birim: canvas pikseli (referans çözünürlük ölçeğinde).
    struct RectTransformComponent
    {
        glm::vec2 AnchorMin{ 0.5f, 0.5f };
        glm::vec2 AnchorMax{ 0.5f, 0.5f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 AnchoredPosition{ 0.0f, 0.0f };
        glm::vec2 SizeDelta{ 160.0f, 40.0f };
        float Rotation = 0.0f;                  // derece (Z)
        glm::vec2 Scale{ 1.0f, 1.0f };

        // Çalışma zamanı: canvas uzayında hesaplanan dikdörtgen (sol-alt köken, piksel)
        glm::vec4 ComputedRect{ 0.0f };         // x, y, w, h
        glm::mat4 CanvasMatrix{ 1.0f };         // yerel (pivot merkezli) -> canvas pikseli
        bool Visible = true;
    };

    enum class UIImageType : int { Simple = 0, Sliced, Tiled, Filled };
    enum class UIFillMethod : int { Horizontal = 0, Vertical, Radial360 };

    struct UIImageComponent
    {
        glm::vec4 Color{ 1.0f };
        std::string TexturePath;               // boşsa düz renk
        std::string SpriteName;                // sprite sayfası dilimi
        UIImageType Type = UIImageType::Simple;
        glm::vec4 Border{ 0.0f };              // Sliced için (piksel; sayfa diliminde Border kullanılır)
        float PixelsPerUnitMultiplier = 1.0f;  // Sliced köşe ölçeği
        UIFillMethod FillMethod = UIFillMethod::Horizontal;
        float FillAmount = 1.0f;
        bool FillClockwise = true;
        bool PreserveAspect = false;
        bool RaycastTarget = true;
        float CornerRadius = 0.0f;             // doku yoksa yuvarlatılmış dikdörtgen (piksel)

        std::shared_ptr<Texture2D> RuntimeTexture;
        std::string LoadedPath;
    };

    enum class UITextAlignH : int { Left = 0, Center, Right };
    enum class UITextAlignV : int { Top = 0, Middle, Bottom };

    struct UITextComponent
    {
        std::string Text = "New Text";
        std::string FontPath;                  // boş: varsayılan yazı tipi
        float FontSize = 32.0f;
        glm::vec4 Color{ 0.12f, 0.12f, 0.14f, 1.0f };
        UITextAlignH AlignH = UITextAlignH::Left;
        UITextAlignV AlignV = UITextAlignV::Middle;
        bool WordWrap = true;
        bool BestFit = false;
        float MinSize = 10.0f, MaxSize = 72.0f;
        float LineSpacing = 1.0f;
        bool Outline = false;
        glm::vec4 OutlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        bool Shadow = false;
        glm::vec2 ShadowOffset{ 2.0f, -2.0f };
        glm::vec4 ShadowColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        bool RaycastTarget = false;
        bool RichText = false;                 // BBCode / TMP etiketleri ([b] [color=red] [wave] ...)
        std::string ColorGradient;             // .cqcolorgradient: harf başına renk geçişi
    };

    enum class UITransition : int { None = 0, ColorTint, Scale };

    // Seçilebilir (Button/Toggle/Slider/...) ortak durum
    struct UISelectableState
    {
        bool Interactable = true;
        UITransition Transition = UITransition::ColorTint;
        glm::vec4 NormalColor{ 1.0f };
        glm::vec4 HighlightedColor{ 0.92f, 0.92f, 0.92f, 1.0f };
        glm::vec4 PressedColor{ 0.75f, 0.75f, 0.75f, 1.0f };
        glm::vec4 DisabledColor{ 0.78f, 0.78f, 0.78f, 0.5f };
        float FadeDuration = 0.1f;
        // Çalışma zamanı
        bool GroupInteractable = true;          // üstteki Canvas Group etkileşimi kapatabilir
        bool Selected = false;                  // klavye/gamepad gezinmesiyle seçili
        bool Hovered = false;
        bool Pressed = false;
        glm::vec4 CurrentTint{ 1.0f };
        float CurrentScale = 1.0f;
    };

    struct UIButtonComponent
    {
        UISelectableState Selectable;
        std::string OnClick;                   // olay adı (scriptler UI.WasClicked/Event ile okur)
        // Çalışma zamanı
        uint32_t ClickCount = 0;
        bool ClickedThisFrame = false;
    };

    struct UIToggleComponent
    {
        UISelectableState Selectable;
        bool IsOn = true;
        int Group = 0;                         // 0: grup yok; aynı gruptan yalnızca biri açık
        std::string OnValueChanged;
        bool ChangedThisFrame = false;
    };

    enum class UISliderDirection : int { LeftToRight = 0, RightToLeft, BottomToTop, TopToBottom };

    struct UISliderComponent
    {
        UISelectableState Selectable;
        float MinValue = 0.0f, MaxValue = 1.0f, Value = 0.5f;
        bool WholeNumbers = false;
        UISliderDirection Direction = UISliderDirection::LeftToRight;
        glm::vec4 BackgroundColor{ 0.8f, 0.8f, 0.82f, 1.0f };
        glm::vec4 FillColor{ 0.26f, 0.59f, 0.98f, 1.0f };
        glm::vec4 HandleColor{ 1.0f };
        float HandleSize = 22.0f;
        std::string OnValueChanged;
        bool Dragging = false;
        bool ChangedThisFrame = false;
    };

    enum class UIContentType : int { Standard = 0, IntegerNumber, DecimalNumber, Alphanumeric, Password };

    struct UIInputFieldComponent
    {
        UISelectableState Selectable;
        std::string Text;
        std::string Placeholder = "Enter text...";
        int CharacterLimit = 0;                // 0 sınırsız
        UIContentType ContentType = UIContentType::Standard;
        float FontSize = 26.0f;
        glm::vec4 TextColor{ 0.1f, 0.1f, 0.12f, 1.0f };
        glm::vec4 PlaceholderColor{ 0.5f, 0.5f, 0.55f, 1.0f };
        glm::vec4 BackgroundColor{ 1.0f };
        std::string OnSubmit;
        // Çalışma zamanı
        bool Focused = false;
        int Caret = 0;                          // kod noktası indeksi
        float CaretBlink = 0.0f;
        bool SubmittedThisFrame = false;
    };

    struct UIDropdownComponent
    {
        UISelectableState Selectable;
        std::vector<std::string> Options{ "Option A", "Option B", "Option C" };
        int Value = 0;
        float FontSize = 26.0f;
        std::string OnValueChanged;
        bool Expanded = false;
        bool ChangedThisFrame = false;
        int HoveredOption = -1;
    };

    struct UIScrollRectComponent
    {
        bool Horizontal = false;
        bool Vertical = true;
        float ScrollSensitivity = 40.0f;
        bool Mask = true;                      // içeriği dikdörtgene kırp
        glm::vec2 NormalizedPosition{ 0.0f, 1.0f };  // 0..1 (1 = üst)
        bool ShowScrollbar = true;
        // Çalışma zamanı
        glm::vec2 ContentOffset{ 0.0f };       // piksel
        glm::vec2 ContentSize{ 0.0f };
        bool Dragging = false;
        // Bağlı Scrollbar varlıkları (iki yönlü eşitlenir)
        uint64_t HorizontalScrollbar = 0;
        uint64_t VerticalScrollbar = 0;
    };

    // Unity CanvasGroup: alt ağacın saydamlığı, etkileşimi ve tıklamayı engellemesi
    struct UICanvasGroupComponent
    {
        float Alpha = 1.0f;
        bool Interactable = true;
        bool BlocksRaycasts = true;
        bool IgnoreParentGroups = false;
    };

    // Unity RectMask2D: çocukları kendi dikdörtgenine kırpar (dolgu ile daraltılabilir)
    struct UIRectMask2DComponent
    {
        glm::vec4 Padding{ 0.0f };             // sol, sağ, üst, alt
    };

    // Unity Mask: çocukları kendi dikdörtgenine kırpar; maske görseli gizlenebilir
    struct UIMaskComponent
    {
        bool ShowMaskGraphic = true;
    };

    // Unity RawImage: herhangi bir doku (video, render texture, dosya) + UV dikdörtgeni
    struct UIRawImageComponent
    {
        std::string TexturePath;
        glm::vec4 Color{ 1.0f };
        glm::vec4 UVRect{ 0.0f, 0.0f, 1.0f, 1.0f };   // x, y, genişlik, yükseklik
        bool RaycastTarget = true;
        std::shared_ptr<Texture2D> RuntimeTexture;
        std::string LoadedPath;
        std::shared_ptr<Texture2D> ExternalTexture;   // Video Player / betik atar (dosyanın önüne geçer)
    };

    enum class UIFitMode : int { Unconstrained = 0, MinSize, PreferredSize };
    // Unity ContentSizeFitter: boyutu içeriğe (metin / yerleşim grubu) göre ayarlar
    struct UIContentSizeFitterComponent
    {
        UIFitMode HorizontalFit = UIFitMode::Unconstrained;
        UIFitMode VerticalFit = UIFitMode::PreferredSize;
    };

    enum class UIAspectMode : int { None = 0, WidthControlsHeight, HeightControlsWidth, FitInParent, EnvelopeParent };
    struct UIAspectRatioFitterComponent
    {
        UIAspectMode Mode = UIAspectMode::WidthControlsHeight;
        float AspectRatio = 1.0f;              // genişlik / yükseklik
    };

    // Unity LayoutElement: yerleşim gruplarına en küçük / tercih edilen / esnek boyut bildirir (-1 = yok)
    struct UILayoutElementComponent
    {
        bool IgnoreLayout = false;
        float MinWidth = -1.0f, MinHeight = -1.0f;
        float PreferredWidth = -1.0f, PreferredHeight = -1.0f;
        float FlexibleWidth = -1.0f, FlexibleHeight = -1.0f;
    };

    enum class UIScrollbarDirection : int { LeftToRight = 0, RightToLeft, BottomToTop, TopToBottom };
    struct UIScrollbarComponent
    {
        UISelectableState Selectable;
        UIScrollbarDirection Direction = UIScrollbarDirection::BottomToTop;
        float Value = 0.0f;                    // 0..1
        float Size = 0.2f;                     // tutamacın oranı 0..1
        int NumberOfSteps = 0;                 // 0 = sürekli
        glm::vec4 BackgroundColor{ 0.85f, 0.85f, 0.88f, 1.0f };
        glm::vec4 HandleColor{ 0.55f, 0.56f, 0.62f, 1.0f };
        std::string OnValueChanged;
        // Çalışma zamanı
        bool Dragging = false;
        float DragGrab = 0.0f;                 // tutamaç içindeki tutma noktası (0..1)
        bool ChangedThisFrame = false;
    };

    // Unity Shadow / Outline (BaseMeshEffect): görsel ya da metin efekt rengiyle kaydırılarak arkada tekrar çizilir
    struct UIShadowComponent
    {
        glm::vec4 EffectColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        glm::vec2 EffectDistance{ 1.0f, -1.0f };   // piksel
        bool UseGraphicAlpha = true;
    };

    struct UIOutlineComponent
    {
        glm::vec4 EffectColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        glm::vec2 EffectDistance{ 1.0f, -1.0f };   // dört yöne
        bool UseGraphicAlpha = true;
    };

    // Unity EventTrigger: işaretçi / gezinme olaylarında aynı varlıktaki betik yöntemlerini çağırır
    struct UIEventTriggerComponent
    {
        std::string PointerEnter, PointerExit, PointerDown, PointerUp, PointerClick, Drag, Scroll;
        std::string Select, Deselect, Submit, Cancel;
    };

    // Unity EventSystem: klavye / gamepad gezinme ayarları (yoksa varsayılanlarla yine çalışır)
    struct UIEventSystemComponent
    {
        uint64_t FirstSelected = 0;
        bool SendNavigationEvents = true;
        float RepeatDelay = 0.4f;              // basılı tutunca ilk tekrar (s)
        float RepeatRate = 0.12f;              // sonraki tekrarlar (s)
    };

    enum class UILayoutType : int { Horizontal = 0, Vertical, Grid };
    enum class UIChildAlignment : int { UpperLeft = 0, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight };

    struct UILayoutGroupComponent
    {
        UILayoutType Type = UILayoutType::Vertical;
        glm::vec4 Padding{ 10.0f, 10.0f, 10.0f, 10.0f };   // sol, sağ, üst, alt
        float Spacing = 8.0f;
        glm::vec2 CellSize{ 100.0f, 100.0f };               // Grid
        glm::vec2 GridSpacing{ 8.0f, 8.0f };
        int Constraint = 0;                                 // Grid: 0 esnek, >0 sabit sütun
        UIChildAlignment ChildAlignment = UIChildAlignment::UpperLeft;
        bool ControlChildWidth = true;
        bool ControlChildHeight = false;
        bool ChildForceExpandWidth = true;
        bool ChildForceExpandHeight = false;
        bool FitContent = false;                            // içerik boyutuna büyü (ScrollRect içeriği)
    };
}
