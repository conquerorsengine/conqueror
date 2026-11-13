#pragma once

#include "Core/Base/Base.h"
#include "UI/UIComponents.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Godot denetimleri (Unity UGUI'de karşılığı olmayanlar; Unity UI Toolkit'te kısmen var): Spin Box, Color Picker ve Color Picker
// Button, Menu Button, Popup Menu, Menu Bar, Tab Container, Tab Bar, Split Container, Item List, Tree, Link Button, Progress Bar,
// Accept / Confirmation Dialog, File Dialog, Text Edit, Code Edit ve Graph Edit (Graph Node). Her biri tek bileşen olarak kendini
// çizer ve girdiyi kendisi işler (UISystem çizim / girdi kancalarıyla); açılır listeler ve diyaloglar her şeyin üstünde çizilir.
// Olaylar üç yoldan gelir: UI olayı (UIGetEvent ...), Godot sinyali (Signals) ve nesnenin betik metodu (OnXxx).
namespace Conqueror
{
    class Scene;
    class Entity;
    struct UIInputState;

    // ---------------------------------------------------------------- tek satır düzenleyici (Spin Box, File Dialog, diyalog alanları)
    struct UILineEditState
    {
        std::string Text;
        int Caret = 0;          // kod noktası
        bool Active = false;
        float Blink = 0.0f;
    };

    struct UISpinBoxComponent
    {
        UISelectableState Selectable;
        float MinValue = 0.0f, MaxValue = 100.0f, Step = 1.0f, Value = 0.0f;
        bool AllowGreater = false, AllowLesser = false;
        std::string Prefix, Suffix;
        float FontSize = 24.0f;
        bool Editable = true;
        std::string OnValueChanged;
        // Çalışma zamanı
        bool ChangedThisFrame = false;
        int HeldArrow = 0;      // 1 yukarı, -1 aşağı (basılı tutunca tekrar eder)
        float RepeatTimer = 0.0f;
        UILineEditState Edit;
    };

    struct UIColorPickerComponent
    {
        UISelectableState Selectable;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool EditAlpha = true;
        bool ShowHex = true;
        std::string OnColorChanged;
        // Çalışma zamanı
        float Hue = 0.0f, Saturation = 0.0f, ValueHSV = 1.0f;
        int DragPart = 0;       // 1 doygunluk/değer karesi, 2 renk tonu, 3 saydamlık
        bool ChangedThisFrame = false;
    };

    struct UIColorPickerButtonComponent
    {
        UISelectableState Selectable;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool EditAlpha = true;
        std::string OnColorChanged;
        // Çalışma zamanı
        bool Open = false;
        float Hue = 0.0f, Saturation = 0.0f, ValueHSV = 1.0f;
        int DragPart = 0;
        bool ChangedThisFrame = false;
    };

    // Menü öğesi: "-" ayraç, "[x] Ad" işaretli, "!Ad" devre dışı (Godot PopupMenu add_item / add_check_item / set_item_disabled)
    struct UIMenuButtonComponent
    {
        UISelectableState Selectable;
        std::string Text = "Menu";
        std::vector<std::string> Items{ "New", "Open", "-", "Quit" };
        float FontSize = 24.0f;
        bool Flat = true;
        std::string OnItemPressed;
        // Çalışma zamanı
        bool Open = false;
        int Hovered = -1;
        int LastPressed = -1;
    };

    struct UIPopupMenuComponent
    {
        std::vector<std::string> Items{ "Cut", "Copy", "Paste" };
        float FontSize = 24.0f;
        bool HideOnItemSelection = true;
        std::string OnItemPressed;
        // Çalışma zamanı: betikten açılır (PopupMenuShow), konum nesnenin yerel koordinatında
        bool Visible = false;
        glm::vec2 Position{ 0.0f };
        int Hovered = -1;
        int LastPressed = -1;
    };

    struct UIMenuBarComponent
    {
        struct Menu { std::string Title; std::vector<std::string> Items; };
        std::vector<Menu> Menus{ { "File", { "New", "Open", "-", "Quit" } }, { "Edit", { "Undo", "Redo" } }, { "Help", { "About" } } };
        float FontSize = 22.0f;
        std::string OnItemPressed;
        // Çalışma zamanı
        int OpenMenu = -1;
        int Hovered = -1;
        int LastMenu = -1, LastItem = -1;
    };

    // Tab Container: çocuklar sekmelerdir (ad = sekme başlığı), yalnız seçili sekme görünür ve içerik alanını doldurur
    struct UITabContainerComponent
    {
        int CurrentTab = 0;
        bool TabsVisible = true;
        float TabHeight = 44.0f;
        float FontSize = 22.0f;
        std::string OnTabChanged;
        // Çalışma zamanı
        int TabCount = 0;
        int HoveredTab = -1;
        bool ChangedThisFrame = false;
    };

    struct UITabBarComponent
    {
        std::vector<std::string> Tabs{ "Tab 1", "Tab 2", "Tab 3" };
        int CurrentTab = 0;
        float FontSize = 22.0f;
        bool CloseButtons = false;
        std::string OnTabChanged;
        // Çalışma zamanı
        int HoveredTab = -1;
        bool ChangedThisFrame = false;
    };

    // Split Container: ilk iki çocuk iki bölmeyi doldurur, aradaki tutamaç sürüklenir
    struct UISplitContainerComponent
    {
        bool Vertical = false;            // false: yan yana (HSplitContainer), true: üst üste (VSplitContainer)
        float SplitRatio = 0.5f;
        float MinPaneSize = 40.0f;
        bool Collapsed = false;
        bool DraggerVisible = true;
        float DraggerThickness = 10.0f;
        std::string OnDragged;
        // Çalışma zamanı
        bool Dragging = false;
    };

    struct UIItemListComponent
    {
        struct ListItem { std::string Text; std::string Icon; bool Disabled = false; };
        std::vector<ListItem> Items{ { "Item 1" }, { "Item 2" }, { "Item 3" } };
        int SelectMode = 0;               // 0 Single, 1 Multi
        std::vector<int> Selected;
        float FontSize = 22.0f;
        float RowHeight = 0.0f;           // 0: yazı boyutundan
        int MaxColumns = 1;
        bool AllowReselect = false;
        std::string OnItemSelected;
        // Çalışma zamanı
        float ScrollOffset = 0.0f;
        int Hovered = -1;
        float LastClickTime = -10.0f;
        int LastClickItem = -1;
        int LastActivated = -1;
        bool ChangedThisFrame = false;
    };

    // Tree: düz liste, her öğenin derinliği; çocukları olan öğe katlanır
    struct UITreeComponent
    {
        struct TreeItem { std::string Text; int Depth = 0; bool Collapsed = false; };
        std::vector<TreeItem> Items{ { "Root", 0 }, { "Child A", 1 }, { "Child B", 1 }, { "Grandchild", 2 } };
        bool HideRoot = false;
        float FontSize = 22.0f;
        float Indent = 22.0f;
        std::string OnItemSelected;
        // Çalışma zamanı
        int Selected = -1;
        int Hovered = -1;
        float ScrollOffset = 0.0f;
        float LastClickTime = -10.0f;
        int LastActivated = -1;
        bool ChangedThisFrame = false;
    };

    struct UILinkButtonComponent
    {
        UISelectableState Selectable;
        std::string Text = "Link";
        std::string Uri;                  // boş değilse tıklayınca tarayıcıda açılır
        int Underline = 0;                // 0 Always, 1 On Hover, 2 Never
        glm::vec4 Color{ 0.26f, 0.55f, 0.98f, 1.0f };
        float FontSize = 24.0f;
        std::string OnClick;
        // Çalışma zamanı
        int Clicks = 0;
    };

    struct UIProgressBarComponent
    {
        float MinValue = 0.0f, MaxValue = 100.0f, Value = 0.0f;
        bool ShowPercentage = true;
        int FillMode = 0;                 // 0 Begin To End, 1 End To Begin, 2 Top To Bottom, 3 Bottom To Top
        bool Indeterminate = false;
        glm::vec4 FillColor{ 0.26f, 0.59f, 0.98f, 1.0f };
        glm::vec4 BackgroundColor{ 0.86f, 0.87f, 0.9f, 1.0f };
        float FontSize = 20.0f;
        // Çalışma zamanı
        float Phase = 0.0f;
    };

    // Accept / Confirmation Dialog: modal panel (arka plan karartılır, diğer UI'ya tıklanmaz)
    struct UIDialogComponent
    {
        int Kind = 0;                     // 0 Accept, 1 Confirmation
        std::string Title = "Alert!";
        std::string Text = "Are you sure?";
        std::string OkText = "OK";
        std::string CancelText = "Cancel";
        bool Visible = false;
        glm::vec2 Size{ 520.0f, 240.0f };
        float FontSize = 24.0f;
        bool HideOnOk = true;
        std::string OnConfirmed;
        // Çalışma zamanı
        int Hovered = -1;                 // 0 OK, 1 Cancel, 2 kapat
        int Confirmed = 0, Canceled = 0;
    };

    struct UIFileDialogComponent
    {
        int FileMode = 0;                 // 0 Open File, 1 Open Files, 2 Open Dir, 3 Save File
        int Access = 0;                   // 0 User Data, 1 Project / Game Data, 2 File System
        std::string Title = "Open a File";
        std::string RootSubfolder;
        std::string Filters;              // "*.png, *.jpg" (boş: hepsi)
        bool Visible = false;
        glm::vec2 Size{ 720.0f, 520.0f };
        float FontSize = 22.0f;
        std::string OnFileSelected;
        // Çalışma zamanı
        std::string CurrentDir;           // erişim köküne göreli
        std::vector<std::string> Entries; // "/ad" klasör, "ad" dosya
        std::vector<int> Selected;
        float ScrollOffset = 0.0f;
        int Hovered = -1;
        int HoveredButton = -1;
        UILineEditState FileName;
        std::string LastSelected;         // son seçilen tam yol(lar), ";" ile
        bool NeedsRefresh = true;
        float LastClickTime = -10.0f;
    };

    // Text Edit / Code Edit (Godot): çok satırlı düzenleyici. Code Edit ek olarak satır numaraları, sözdizimi renkleri, otomatik girinti
    // ve parantez kapatma
    struct UITextEditComponent
    {
        UISelectableState Selectable;
        std::string Text;
        bool Editable = true;
        bool CodeEditor = false;          // Code Edit
        bool LineNumbers = true;
        bool HighlightSyntax = true;
        bool AutoBraceCompletion = true;
        bool AutoIndent = true;
        std::string Keywords = "if else for while return var func class using true false null break continue";
        float FontSize = 22.0f;
        std::string Placeholder;
        std::string OnTextChanged;
        // Çalışma zamanı
        int CaretLine = 0, CaretColumn = 0;
        bool Focused = false;
        float Blink = 0.0f;
        float ScrollY = 0.0f, ScrollX = 0.0f;
        bool ChangedThisFrame = false;
    };

    // Graph Edit: Graph Node çocuklarını kaydırma / yakınlaştırmayla yerleştirir, bağlantıları çizer; çıkış ucundan giriş ucuna
    // sürükleyince connection_request (Auto Connect açıksa bağlantı eklenir)
    struct UIGraphEditComponent
    {
        struct Connection { uint64_t From = 0; int FromPort = 0; uint64_t To = 0; int ToPort = 0; };
        std::vector<Connection> Connections;
        glm::vec2 ScrollOffset{ 0.0f };
        float Zoom = 1.0f;
        bool ShowGrid = true;
        bool AutoConnect = true;
        std::string OnConnectionRequest;
        // Çalışma zamanı
        bool Panning = false;
        glm::vec2 PanStart{ 0.0f }, PanOrigin{ 0.0f };
        uint64_t DragFrom = 0;
        int DragFromPort = -1;
        glm::vec2 DragPoint{ 0.0f };
        uint64_t MovingNode = 0;
        glm::vec2 MoveGrab{ 0.0f };
    };

    struct UIGraphNodeComponent
    {
        std::string Title = "Node";
        glm::vec2 PositionOffset{ 40.0f, 40.0f };   // Graph Edit içeriğinde (sol üstten, aşağı +)
        int InputPorts = 1, OutputPorts = 1;
        glm::vec4 TitleColor{ 0.30f, 0.45f, 0.75f, 1.0f };
        bool Draggable = true;
        bool Selected = false;
    };

    namespace UIDraw
    {
        // UISystem'in çizim yardımcıları (öğe yereli: sol alt köken, y yukarı; canvas birimi)
        CQ_API void Rect(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const glm::vec4& color);
        CQ_API void RoundRect(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, float radius, const glm::vec4& color);
        CQ_API void Triangle(const glm::mat4& m, glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color);
        CQ_API void Line(const glm::mat4& m, glm::vec2 a, glm::vec2 b, float thickness, const glm::vec4& color);
        CQ_API void Image(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const std::string& texturePath, const glm::vec4& color);
        // alignH: 0 sol, 1 orta, 2 sağ; alignV: 0 üst, 1 orta, 2 alt
        CQ_API void Text(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const std::string& text, float fontSize, const glm::vec4& color,
                         int alignH = 0, int alignV = 1, bool wrap = false);
        CQ_API float TextWidth(const std::string& text, float fontSize);
        CQ_API float LineHeight(float fontSize);
        // Kod noktası dizini -> x (tek satır)
        CQ_API float CaretX(const std::string& text, int codepoint, float fontSize);
        CQ_API int CodepointAt(const std::string& text, float x, float fontSize);
        // Öğe yerel dikdörtgenine kırp (öğenin kendi kırpmasıyla kesişir); PopClip öğenin kırpmasına döner
        CQ_API void PushClip(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx);
        CQ_API void PopClip();
    }

    namespace UIControls
    {
        UISelectableState* GetSelectable(Entity e);
        bool IsRaycastTarget(Entity e);
        void Draw(Entity e, const glm::mat4& m, glm::vec2 size, const glm::vec4& tint);
        // Açılır listeler, renk seçici, diyaloglar (her şeyin üstünde)
        void DrawOverlay(Entity e, const glm::mat4& m, glm::vec2 size);
        bool OverlayHit(Entity e, const glm::mat4& m, glm::vec2 size, glm::vec2 canvasPoint, glm::vec2& local);
        bool HasOverlay(Entity e);
        void OverlayPress(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool& wantsFocus);
        void OverlayDrag(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size);
        void OverlayRelease(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size);
        void OverlayHover(Entity e, glm::vec2 local, glm::vec2 size, bool inside);
        void CloseOverlays(Scene& scene, Entity except);
        // Normal girdi (true: işlendi). wantsFocus: klavye bu denetime gelsin
        bool OnPress(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool& wantsFocus);
        void OnDrag(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size);
        void OnRelease(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool inside);
        void OnHover(Entity e, glm::vec2 local, glm::vec2 size, bool inside);
        bool OnScroll(Scene& scene, Entity e, float delta);
        bool WantsKeyboard(Entity e);
        void OnKeyboard(Scene& scene, Entity e, const UIInputState& input, float dt);
        void Unfocus(Entity e);
        void Update(Scene& scene, float dt);
        // Yerleşim: çocuk dikdörtgenleri (x, y, w, h; z < 0 serbest), gizlenen çocuklar, çocukların taban matrisi
        void LayoutChildren(Entity parent, glm::vec2 parentSize, const std::vector<Entity>& children, std::vector<glm::vec4>& forced,
                            std::vector<bool>& hidden);
        bool ChildTransform(Entity parent, glm::vec2 parentSize, glm::mat4& childBase);
        // Etkin denetim (kendisi ya da üstlerinden ilki)
        Entity FindControl(Entity e);
        // Ertelenmiş sinyaller ve betik metotları (UI güncellemesinin sonunda)
        void FlushEvents(Scene& scene);
        void RegisterComponents();
        // GameObject > UI > Controls: denetimi (ve örnek çocuklarını) oluştur; bilinmeyen tür: boş varlık
        Entity CreateElement(Scene& scene, const std::string& type, Entity parent);
    }
}
