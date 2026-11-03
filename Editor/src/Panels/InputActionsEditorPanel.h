#pragma once

#include "Core/Input/InputActionAssets.h"

#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Input Actions düzenleyicisi (Unity Input Actions penceresi): .cqinputactions dosyasına çift tıklayınca açılır.
    //  - Üç bölme: Action Maps | Actions (bağlamalar ve composite'ler ağaç halinde) | Properties.
    //  - Eylem: Action Type (Value / Button / Pass Through), Control Type, Initial State Check, etkileşimler, işlemciler.
    //  - Bağlama: kontrol seçici (Keyboard / Mouse / Gamepad ağacı, arama) ve Listen (basılan kontrolleri listeler),
    //    "Use in control scheme" işaretleri, etkileşimler, işlemciler. Composite: tür (2D Vector, 1D Axis, One / Two
    //    Modifiers) ve parametreleri.
    //  - Kontrol şemaları (Keyboard&Mouse, Gamepad ...): ekle / düzenle / çoğalt / sil; seçili şema bağlamaları süzer,
    //    yeni bağlamalar o şemaya eklenir.
    //  - Kaydet / Auto-Save, geri al / yinele (Ctrl+Z / Ctrl+Y), F2 yeniden adlandır, Del sil, Ctrl+D çoğalt,
    //    Ctrl+C / Ctrl+V kopyala / yapıştır, sürükle-bırak ile sıralama, arama. Kaydedilmemiş değişiklikle kapatınca sorar.
    class InputActionsEditorPanel
    {
    public:
        void OnImGuiRender();
        bool Open(const std::string& path);
        bool& GetIsOpen() { return m_Open; }
        const std::string& GetPath() const { return m_Path; }
        bool IsDirty() const;
        bool Save();

        // Düzenleme işlemleri (düğmeler ve otomasyon aynı yolu kullanır). Dizinler: harita, eylem, bağlama.
        InputActionAssetData& Data() { return m_Data; }
        int AddMap(const std::string& name = std::string());
        int AddAction(int map, const std::string& name = std::string());
        int AddBinding(int map, int action, const std::string& path);
        // composite: 2DVector / 1DAxis / OneModifier / TwoModifiers; parça yolları sırayla (boş olabilir)
        int AddComposite(int map, int action, const std::string& composite, const std::vector<std::string>& partPaths);
        bool SetActionKind(int map, int action, InputActionKind kind, InputControlKind control);
        bool SetInteractions(int map, int action, int binding, const std::string& interactions);
        bool SetProcessors(int map, int action, int binding, const std::string& processors);
        bool SetBindingPath(int map, int action, int binding, const std::string& path);
        bool DeleteSelection();
        bool RenameSelection(const std::string& name);
        bool DuplicateSelection();
        bool AddScheme(const std::string& name, const std::vector<std::string>& devices);
        void Select(int map, int action = -1, int binding = -1);
        void SetScheme(const std::string& scheme) { m_Scheme = scheme; }
        bool Undo();
        bool Redo();
        void SetAutoSave(bool on) { m_AutoSave = on; }

    private:
        struct Selection
        {
            int Map = -1;
            int Action = -1;
            int Binding = -1;   // -1: eylem seçili
        };
        struct Clipboard
        {
            int Kind = 0;   // 0 boş, 1 harita, 2 eylem, 3 bağlama bloğu
            InputActionMapDef Map;
            InputActionDef Action;
            std::vector<InputBindingDef> Bindings;
        };

        void DrawToolbar();
        void DrawMaps();
        void DrawActions();
        void DrawProperties();
        void DrawActionProperties(InputActionDef& action);
        void DrawBindingProperties(InputActionDef& action, int binding);
        bool DrawPathPicker(InputBindingDef& binding);
        bool DrawSchemeUsage(InputActionDef& action, int binding);
        bool DrawSpecList(const char* title, std::string& specs, bool interactions, bool vector);
        void DrawAddBindingMenu(int map, int action);
        void DrawSchemePopup();
        void DrawClosePrompt();
        void HandleShortcuts();
        void ListenForInput();

        // Değişiklik: geri al kaydı + otomatik kayıt
        void BeginEdit();
        void EndEdit();
        std::string UniqueMapName(const std::string& base) const;
        std::string UniqueActionName(const InputActionMapDef& map, const std::string& base) const;
        bool BindingVisible(const InputBindingDef& binding) const;
        bool ActionVisible(const InputActionDef& action) const;
        // Kök bağlamanın bloğu: [first, last) (composite kökü + parçaları)
        void BindingBlock(const InputActionDef& action, int binding, int& first, int& last) const;

        std::string m_Path;
        InputActionAssetData m_Data;
        std::string m_SavedText;
        bool m_Open = false;
        bool m_AutoSave = false;
        bool m_ClosePrompt = false;
        Selection m_Sel;
        std::string m_Scheme;        // seçili kontrol şeması (boş: hepsi)
        std::string m_Device;        // seçili aygıt süzgeci (boş: hepsi)
        char m_Search[96] = {};
        std::vector<InputActionAssetData> m_Undo, m_Redo;
        Clipboard m_Clipboard;

        // Yeniden adlandırma
        int m_RenameKind = 0;        // 1 harita, 2 eylem, 3 composite
        char m_RenameBuffer[128] = {};
        bool m_RenameFocus = false;

        // Kontrol seçici / Listen
        bool m_Listening = false;
        std::vector<std::string> m_ListenMatches;
        char m_PickerSearch[64] = {};
        bool m_TextPath = false;
        float m_PickerPos[2] = { 0.0f, 0.0f };
        float m_PickerWidth = 0.0f;

        // Şema penceresi
        bool m_SchemePopup = false;
        int m_SchemeEditIndex = -1;  // -1 yeni
        char m_SchemeName[64] = {};
        std::vector<std::string> m_SchemeDevices;
        std::vector<bool> m_SchemeOptional;
    };
}
