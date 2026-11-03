#include "InputActionsEditorPanel.h"

#include "Core/Input/GameInput.h"
#include "Core/Input/Input.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        bool Contains(const std::string& hay, const char* needle)
        {
            if (!needle || !needle[0])
                return true;
            return Lower(hay).find(Lower(needle)) != std::string::npos;
        }

        const char* KindIcon(InputActionKind k)
        {
            switch (k)
            {
            case InputActionKind::Value: return ICON_FA_WAVE_SQUARE;
            case InputActionKind::PassThrough: return ICON_FA_ARROW_RIGHT_ARROW_LEFT;
            default: return ICON_FA_CIRCLE_DOT;
            }
        }

        // "pressPoint" -> "Press Point"
        std::string Pretty(const std::string& key)
        {
            std::string out;
            for (size_t i = 0; i < key.size(); ++i)
            {
                const char c = key[i];
                if (i == 0)
                    out += (char)std::toupper((unsigned char)c);
                else if (std::isupper((unsigned char)c))
                {
                    out += ' ';
                    out += c;
                }
                else
                    out += c;
            }
            return out;
        }

        void SetBuffer(char* buf, size_t size, const std::string& text)
        {
            std::strncpy(buf, text.c_str(), size - 1);
            buf[size - 1] = 0;
        }

        // Bağlama ekle menüsünün öğeleri: görünen ad, composite türü (boş: tek bağlama)
        struct AddItem
        {
            const char* Label;
            const char* Composite;
        };
        const AddItem kAddItems[] = {
            { "Add Binding", "" },
            { "Add Up\\Down\\Left\\Right Composite", "2DVector" },
            { "Add Positive\\Negative Binding", "1DAxis" },
            { "Add Binding With One Modifier", "OneModifier" },
            { "Add Binding With Two Modifiers", "TwoModifiers" },
        };
    }

    // ================================================================== açma / kaydetme / değişiklik

    bool InputActionsEditorPanel::Open(const std::string& path)
    {
        InputActionAssetData data;
        if (!InputActionAssets::Load(path, data))
            return false;
        m_Path = path;
        m_Data = std::move(data);
        m_SavedText = InputActionAssets::ToString(m_Data);
        m_Undo.clear();
        m_Redo.clear();
        m_Sel = {};
        if (!m_Data.Maps.empty())
            m_Sel.Map = 0;
        m_Scheme.clear();
        m_Device.clear();
        m_RenameKind = 0;
        m_Listening = false;
        m_ClosePrompt = false;
        m_Open = true;
        return true;
    }

    bool InputActionsEditorPanel::IsDirty() const
    {
        return !m_Path.empty() && InputActionAssets::ToString(m_Data) != m_SavedText;
    }

    bool InputActionsEditorPanel::Save()
    {
        if (m_Path.empty())
            return false;
        if (!InputActionAssets::Save(m_Path, m_Data))
            return false;
        m_SavedText = InputActionAssets::ToString(m_Data);
        return true;
    }

    void InputActionsEditorPanel::BeginEdit()
    {
        // Sürüklenen bir alan (ör. Duration) her karede değişir: tek geri alma adımı
        static bool s_Continuous = false;
        if (!ImGui::GetCurrentContext() || !ImGui::IsAnyItemActive())
            s_Continuous = false;
        else if (s_Continuous)
            return;
        else
            s_Continuous = true;
        m_Undo.push_back(m_Data);
        if (m_Undo.size() > 200)
            m_Undo.erase(m_Undo.begin());
        m_Redo.clear();
    }

    void InputActionsEditorPanel::EndEdit()
    {
        if (m_AutoSave)
            Save();
    }

    bool InputActionsEditorPanel::Undo()
    {
        if (m_Undo.empty())
            return false;
        m_Redo.push_back(m_Data);
        m_Data = m_Undo.back();
        m_Undo.pop_back();
        Select(m_Sel.Map, m_Sel.Action, m_Sel.Binding);
        if (m_AutoSave)
            Save();
        return true;
    }

    bool InputActionsEditorPanel::Redo()
    {
        if (m_Redo.empty())
            return false;
        m_Undo.push_back(m_Data);
        m_Data = m_Redo.back();
        m_Redo.pop_back();
        Select(m_Sel.Map, m_Sel.Action, m_Sel.Binding);
        if (m_AutoSave)
            Save();
        return true;
    }

    void InputActionsEditorPanel::Select(int map, int action, int binding)
    {
        m_Sel = {};
        if (map < 0 || map >= (int)m_Data.Maps.size())
            return;
        m_Sel.Map = map;
        const auto& m = m_Data.Maps[(size_t)map];
        if (action < 0 || action >= (int)m.Actions.size())
            return;
        m_Sel.Action = action;
        const auto& a = m.Actions[(size_t)action];
        if (binding >= 0 && binding < (int)a.Bindings.size())
            m_Sel.Binding = binding;
    }

    std::string InputActionsEditorPanel::UniqueMapName(const std::string& base) const
    {
        std::string name = base;
        for (int i = 1; m_Data.FindMap(name); ++i)
            name = base + " " + std::to_string(i);
        return name;
    }

    std::string InputActionsEditorPanel::UniqueActionName(const InputActionMapDef& map, const std::string& base) const
    {
        std::string name = base;
        for (int i = 1; map.Find(name); ++i)
            name = base + " " + std::to_string(i);
        return name;
    }

    void InputActionsEditorPanel::BindingBlock(const InputActionDef& action, int binding, int& first, int& last) const
    {
        first = binding;
        // Parça seçiliyse kökü bul
        while (first > 0 && action.Bindings[(size_t)first].IsPart())
            --first;
        last = first + 1;
        if (action.Bindings[(size_t)first].IsComposite())
            while (last < (int)action.Bindings.size() && action.Bindings[(size_t)last].IsPart())
                ++last;
    }

    bool InputActionsEditorPanel::BindingVisible(const InputBindingDef& b) const
    {
        if (!m_Scheme.empty() && std::find(b.Groups.begin(), b.Groups.end(), m_Scheme) == b.Groups.end())
            return false;
        if (!m_Device.empty() && InputActionAssets::DeviceOf(b.Path) != m_Device)
            return false;
        return true;
    }

    bool InputActionsEditorPanel::ActionVisible(const InputActionDef& a) const
    {
        if (!m_Search[0] || Contains(a.Name, m_Search))
            return true;
        for (const auto& b : a.Bindings)
            if (Contains(InputActionAssets::DisplayName(b), m_Search) || Contains(b.Path, m_Search))
                return true;
        return false;
    }

    // ================================================================== düzenleme işlemleri

    int InputActionsEditorPanel::AddMap(const std::string& name)
    {
        BeginEdit();
        InputActionMapDef map;
        map.Name = UniqueMapName(name.empty() ? std::string("New action map") : name);
        // Unity: yeni haritada boş bir eylem ve bağlaması
        InputActionDef action;
        action.Name = "New action";
        InputBindingDef b;
        if (!m_Scheme.empty())
            b.Groups.push_back(m_Scheme);
        action.Bindings.push_back(b);
        map.Actions.push_back(action);
        m_Data.Maps.push_back(map);
        EndEdit();
        const int index = (int)m_Data.Maps.size() - 1;
        Select(index);
        return index;
    }

    int InputActionsEditorPanel::AddAction(int mapIndex, const std::string& name)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size())
            return -1;
        BeginEdit();
        auto& map = m_Data.Maps[(size_t)mapIndex];
        InputActionDef action;
        action.Name = UniqueActionName(map, name.empty() ? std::string("New action") : name);
        InputBindingDef b;
        if (!m_Scheme.empty())
            b.Groups.push_back(m_Scheme);
        action.Bindings.push_back(b);
        map.Actions.push_back(action);
        EndEdit();
        const int index = (int)map.Actions.size() - 1;
        Select(mapIndex, index);
        return index;
    }

    int InputActionsEditorPanel::AddBinding(int mapIndex, int actionIndex, const std::string& path)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size())
            return -1;
        auto& map = m_Data.Maps[(size_t)mapIndex];
        if (actionIndex < 0 || actionIndex >= (int)map.Actions.size())
            return -1;
        BeginEdit();
        InputBindingDef b;
        b.Path = path;
        if (!m_Scheme.empty())
            b.Groups.push_back(m_Scheme);
        auto& action = map.Actions[(size_t)actionIndex];
        action.Bindings.push_back(b);
        EndEdit();
        const int index = (int)action.Bindings.size() - 1;
        Select(mapIndex, actionIndex, index);
        return index;
    }

    int InputActionsEditorPanel::AddComposite(int mapIndex, int actionIndex, const std::string& composite, const std::vector<std::string>& partPaths)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size())
            return -1;
        auto& map = m_Data.Maps[(size_t)mapIndex];
        if (actionIndex < 0 || actionIndex >= (int)map.Actions.size())
            return -1;
        const auto parts = InputActionAssets::CompositeParts(composite);
        if (parts.empty())
            return -1;
        BeginEdit();
        auto& action = map.Actions[(size_t)actionIndex];
        InputBindingDef root;
        root.Composite = composite;
        root.Name = InputActionAssets::CompositeDisplay(composite);
        if (composite == "2DVector")
            root.CompositeParams = "mode=DigitalNormalized";
        else if (composite == "1DAxis")
            root.CompositeParams = "whichSideWins=Neither";
        const int index = (int)action.Bindings.size();
        action.Bindings.push_back(root);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            InputBindingDef part;
            part.Part = parts[i];
            part.Path = i < partPaths.size() ? partPaths[i] : std::string();
            if (!m_Scheme.empty())
                part.Groups.push_back(m_Scheme);
            action.Bindings.push_back(part);
        }
        EndEdit();
        Select(mapIndex, actionIndex, index);
        return index;
    }

    bool InputActionsEditorPanel::SetActionKind(int mapIndex, int actionIndex, InputActionKind kind, InputControlKind control)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size() || actionIndex < 0 || actionIndex >= (int)m_Data.Maps[(size_t)mapIndex].Actions.size())
            return false;
        BeginEdit();
        auto& a = m_Data.Maps[(size_t)mapIndex].Actions[(size_t)actionIndex];
        a.Kind = kind;
        a.Control = kind == InputActionKind::Button ? InputControlKind::Button : control;
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::SetInteractions(int mapIndex, int actionIndex, int binding, const std::string& interactions)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size() || actionIndex < 0 || actionIndex >= (int)m_Data.Maps[(size_t)mapIndex].Actions.size())
            return false;
        auto& a = m_Data.Maps[(size_t)mapIndex].Actions[(size_t)actionIndex];
        if (binding >= (int)a.Bindings.size())
            return false;
        BeginEdit();
        (binding < 0 ? a.Interactions : a.Bindings[(size_t)binding].Interactions) = interactions;
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::SetProcessors(int mapIndex, int actionIndex, int binding, const std::string& processors)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size() || actionIndex < 0 || actionIndex >= (int)m_Data.Maps[(size_t)mapIndex].Actions.size())
            return false;
        auto& a = m_Data.Maps[(size_t)mapIndex].Actions[(size_t)actionIndex];
        if (binding >= (int)a.Bindings.size())
            return false;
        BeginEdit();
        (binding < 0 ? a.Processors : a.Bindings[(size_t)binding].Processors) = processors;
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::SetBindingPath(int mapIndex, int actionIndex, int binding, const std::string& path)
    {
        if (mapIndex < 0 || mapIndex >= (int)m_Data.Maps.size() || actionIndex < 0 || actionIndex >= (int)m_Data.Maps[(size_t)mapIndex].Actions.size())
            return false;
        auto& a = m_Data.Maps[(size_t)mapIndex].Actions[(size_t)actionIndex];
        if (binding < 0 || binding >= (int)a.Bindings.size() || a.Bindings[(size_t)binding].IsComposite())
            return false;
        BeginEdit();
        auto& b = a.Bindings[(size_t)binding];
        b.Path = path;
        if (!m_Scheme.empty() && b.Groups.empty())
            b.Groups.push_back(m_Scheme);
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::DeleteSelection()
    {
        if (m_Sel.Map < 0)
            return false;
        BeginEdit();
        auto& map = m_Data.Maps[(size_t)m_Sel.Map];
        if (m_Sel.Action < 0)
        {
            m_Data.Maps.erase(m_Data.Maps.begin() + m_Sel.Map);
            Select(std::min(m_Sel.Map, (int)m_Data.Maps.size() - 1));
        }
        else if (m_Sel.Binding < 0)
        {
            map.Actions.erase(map.Actions.begin() + m_Sel.Action);
            Select(m_Sel.Map, std::min(m_Sel.Action, (int)map.Actions.size() - 1));
        }
        else
        {
            auto& action = map.Actions[(size_t)m_Sel.Action];
            int first = 0, last = 0;
            BindingBlock(action, m_Sel.Binding, first, last);
            // Parça seçiliyse yalnız o parça, değilse bağlama / bütün composite
            if (action.Bindings[(size_t)m_Sel.Binding].IsPart())
            {
                first = m_Sel.Binding;
                last = first + 1;
            }
            action.Bindings.erase(action.Bindings.begin() + first, action.Bindings.begin() + last);
            Select(m_Sel.Map, m_Sel.Action, action.Bindings.empty() ? -1 : std::min(first, (int)action.Bindings.size() - 1));
            // Seçim bir parçaya düşerse köke çık
            if (m_Sel.Binding >= 0)
            {
                int f = 0, l = 0;
                BindingBlock(action, m_Sel.Binding, f, l);
                m_Sel.Binding = f;
            }
        }
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::RenameSelection(const std::string& nameIn)
    {
        std::string name = nameIn;
        while (!name.empty() && std::isspace((unsigned char)name.back()))
            name.pop_back();
        if (m_Sel.Map < 0 || name.empty())
            return false;
        auto& map = m_Data.Maps[(size_t)m_Sel.Map];
        if (m_Sel.Action < 0)
        {
            if (name == map.Name)
                return true;
            BeginEdit();
            map.Name = UniqueMapName(name);
            EndEdit();
            return true;
        }
        auto& action = map.Actions[(size_t)m_Sel.Action];
        if (m_Sel.Binding < 0)
        {
            if (name == action.Name)
                return true;
            BeginEdit();
            action.Name = UniqueActionName(map, name);
            EndEdit();
            return true;
        }
        auto& b = action.Bindings[(size_t)m_Sel.Binding];
        if (!b.IsComposite())
            return false;
        BeginEdit();
        b.Name = name;
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::DuplicateSelection()
    {
        if (m_Sel.Map < 0)
            return false;
        BeginEdit();
        auto& maps = m_Data.Maps;
        if (m_Sel.Action < 0)
        {
            InputActionMapDef copy = maps[(size_t)m_Sel.Map];
            copy.Name = UniqueMapName(copy.Name);
            maps.insert(maps.begin() + m_Sel.Map + 1, copy);
            Select(m_Sel.Map + 1);
        }
        else
        {
            auto& map = maps[(size_t)m_Sel.Map];
            if (m_Sel.Binding < 0)
            {
                InputActionDef copy = map.Actions[(size_t)m_Sel.Action];
                copy.Name = UniqueActionName(map, copy.Name);
                map.Actions.insert(map.Actions.begin() + m_Sel.Action + 1, copy);
                Select(m_Sel.Map, m_Sel.Action + 1);
            }
            else
            {
                auto& action = map.Actions[(size_t)m_Sel.Action];
                int first = 0, last = 0;
                BindingBlock(action, m_Sel.Binding, first, last);
                std::vector<InputBindingDef> block(action.Bindings.begin() + first, action.Bindings.begin() + last);
                action.Bindings.insert(action.Bindings.begin() + last, block.begin(), block.end());
                Select(m_Sel.Map, m_Sel.Action, last);
            }
        }
        EndEdit();
        return true;
    }

    bool InputActionsEditorPanel::AddScheme(const std::string& name, const std::vector<std::string>& devices)
    {
        if (name.empty())
            return false;
        for (const auto& s : m_Data.Schemes)
            if (s.Name == name)
                return false;
        BeginEdit();
        InputControlSchemeDef s;
        s.Name = name;
        s.Devices = devices;
        s.Optional.assign(devices.size(), false);
        m_Data.Schemes.push_back(s);
        EndEdit();
        return true;
    }

    // ================================================================== çizim

    void InputActionsEditorPanel::OnImGuiRender()
    {
        if (!m_Open)
            return;
        const bool dirty = IsDirty();
        const std::string title = "Input Actions - " + fs::path(m_Path).stem().string() + (dirty ? " *" : "") + "###InputActionsEditor";
        ImGui::SetNextWindowSize(ImVec2(1150.0f, 620.0f), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            HandleShortcuts();
            DrawToolbar();
            ImGui::Separator();
            const float height = ImGui::GetContentRegionAvail().y;
            if (ImGui::BeginTable("##iaTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, height)))
            {
                ImGui::TableSetupColumn("Action Maps", ImGuiTableColumnFlags_WidthFixed, 210.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 1.15f);
                ImGui::TableNextRow();
                const float childHeight = height - 6.0f;
                ImGui::TableSetColumnIndex(0);
                if (ImGui::BeginChild("##iaMaps", ImVec2(0.0f, childHeight)))
                    DrawMaps();
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                if (ImGui::BeginChild("##iaActions", ImVec2(0.0f, childHeight)))
                    DrawActions();
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(2);
                if (ImGui::BeginChild("##iaProps", ImVec2(0.0f, childHeight)))
                    DrawProperties();
                ImGui::EndChild();
                ImGui::EndTable();
            }
            DrawSchemePopup();
            DrawClosePrompt();
        }
        ImGui::End();
        if (!open)
        {
            if (IsDirty() && !m_AutoSave)
                m_ClosePrompt = true;
            else
                m_Open = false;
        }
    }

    void InputActionsEditorPanel::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
            return;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (shift) Redo();
            else Undo();
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
            Redo();
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            Save();
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            DuplicateSelection();
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            DeleteSelection();
        else if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_Sel.Map >= 0)
        {
            const auto& map = m_Data.Maps[(size_t)m_Sel.Map];
            if (m_Sel.Action < 0)
            {
                m_RenameKind = 1;
                SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), map.Name);
            }
            else if (m_Sel.Binding < 0)
            {
                m_RenameKind = 2;
                SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), map.Actions[(size_t)m_Sel.Action].Name);
            }
            else if (map.Actions[(size_t)m_Sel.Action].Bindings[(size_t)m_Sel.Binding].IsComposite())
            {
                m_RenameKind = 3;
                SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), map.Actions[(size_t)m_Sel.Action].Bindings[(size_t)m_Sel.Binding].Name);
            }
            m_RenameFocus = true;
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && m_Sel.Map >= 0)
        {
            const auto& map = m_Data.Maps[(size_t)m_Sel.Map];
            m_Clipboard = {};
            if (m_Sel.Action < 0)
            {
                m_Clipboard.Kind = 1;
                m_Clipboard.Map = map;
            }
            else if (m_Sel.Binding < 0)
            {
                m_Clipboard.Kind = 2;
                m_Clipboard.Action = map.Actions[(size_t)m_Sel.Action];
            }
            else
            {
                const auto& action = map.Actions[(size_t)m_Sel.Action];
                int first = 0, last = 0;
                BindingBlock(action, m_Sel.Binding, first, last);
                m_Clipboard.Kind = 3;
                m_Clipboard.Bindings.assign(action.Bindings.begin() + first, action.Bindings.begin() + last);
            }
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && m_Clipboard.Kind != 0)
        {
            BeginEdit();
            if (m_Clipboard.Kind == 1)
            {
                InputActionMapDef copy = m_Clipboard.Map;
                copy.Name = UniqueMapName(copy.Name);
                const int at = m_Sel.Map >= 0 ? m_Sel.Map + 1 : (int)m_Data.Maps.size();
                m_Data.Maps.insert(m_Data.Maps.begin() + at, copy);
                Select(at);
            }
            else if (m_Clipboard.Kind == 2 && m_Sel.Map >= 0)
            {
                auto& map = m_Data.Maps[(size_t)m_Sel.Map];
                InputActionDef copy = m_Clipboard.Action;
                copy.Name = UniqueActionName(map, copy.Name);
                const int at = m_Sel.Action >= 0 ? m_Sel.Action + 1 : (int)map.Actions.size();
                map.Actions.insert(map.Actions.begin() + at, copy);
                Select(m_Sel.Map, at);
            }
            else if (m_Clipboard.Kind == 3 && m_Sel.Map >= 0 && m_Sel.Action >= 0)
            {
                auto& action = m_Data.Maps[(size_t)m_Sel.Map].Actions[(size_t)m_Sel.Action];
                const int at = (int)action.Bindings.size();
                action.Bindings.insert(action.Bindings.end(), m_Clipboard.Bindings.begin(), m_Clipboard.Bindings.end());
                Select(m_Sel.Map, m_Sel.Action, at);
            }
            EndEdit();
        }
    }

    void InputActionsEditorPanel::DrawToolbar()
    {
        // Kontrol şeması
        ImGui::SetNextItemWidth(210.0f);
        const std::string schemePreview = m_Scheme.empty() ? std::string("All Control Schemes") : m_Scheme;
        if (ImGui::BeginCombo("##iaScheme", schemePreview.c_str()))
        {
            if (ImGui::Selectable("All Control Schemes", m_Scheme.empty()))
            {
                m_Scheme.clear();
                m_Device.clear();
            }
            for (const auto& s : m_Data.Schemes)
                if (ImGui::Selectable(s.Name.c_str(), s.Name == m_Scheme))
                {
                    m_Scheme = s.Name;
                    m_Device.clear();
                }
            ImGui::Separator();
            if (ImGui::Selectable("Add Control Scheme..."))
            {
                m_SchemeEditIndex = -1;
                SetBuffer(m_SchemeName, sizeof(m_SchemeName), "New Control Scheme");
                m_SchemeDevices = { "Keyboard" };
                m_SchemeOptional = { false };
                m_SchemePopup = true;
            }
            int current = -1;
            for (size_t i = 0; i < m_Data.Schemes.size(); ++i)
                if (m_Data.Schemes[i].Name == m_Scheme)
                    current = (int)i;
            if (current >= 0)
            {
                if (ImGui::Selectable("Edit Control Scheme..."))
                {
                    m_SchemeEditIndex = current;
                    SetBuffer(m_SchemeName, sizeof(m_SchemeName), m_Data.Schemes[(size_t)current].Name);
                    m_SchemeDevices = m_Data.Schemes[(size_t)current].Devices;
                    m_SchemeOptional = m_Data.Schemes[(size_t)current].Optional;
                    m_SchemeOptional.resize(m_SchemeDevices.size(), false);
                    m_SchemePopup = true;
                }
                if (ImGui::Selectable("Duplicate Control Scheme..."))
                {
                    m_SchemeEditIndex = -1;
                    SetBuffer(m_SchemeName, sizeof(m_SchemeName), m_Scheme + " Copy");
                    m_SchemeDevices = m_Data.Schemes[(size_t)current].Devices;
                    m_SchemeOptional = m_Data.Schemes[(size_t)current].Optional;
                    m_SchemeOptional.resize(m_SchemeDevices.size(), false);
                    m_SchemePopup = true;
                }
                if (ImGui::Selectable("Delete Control Scheme"))
                {
                    BeginEdit();
                    const std::string gone = m_Scheme;
                    m_Data.Schemes.erase(m_Data.Schemes.begin() + current);
                    for (auto& map : m_Data.Maps)
                        for (auto& action : map.Actions)
                            for (auto& b : action.Bindings)
                                b.Groups.erase(std::remove(b.Groups.begin(), b.Groups.end(), gone), b.Groups.end());
                    m_Scheme.clear();
                    m_Device.clear();
                    EndEdit();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Control schemes group bindings by device (Keyboard&Mouse, Gamepad ...).\nSelecting one shows only its bindings; new bindings are added to it.");

        // Aygıt süzgeci (seçili şemanın aygıtları)
        if (!m_Scheme.empty())
            if (const InputControlSchemeDef* scheme = [&]() -> const InputControlSchemeDef* {
                    for (const auto& s : m_Data.Schemes)
                        if (s.Name == m_Scheme)
                            return &s;
                    return nullptr;
                }())
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::BeginCombo("##iaDevice", m_Device.empty() ? "All Devices" : m_Device.c_str()))
                {
                    if (ImGui::Selectable("All Devices", m_Device.empty()))
                        m_Device.clear();
                    for (const auto& d : scheme->Devices)
                        if (ImGui::Selectable(d.c_str(), d == m_Device))
                            m_Device = d;
                    ImGui::EndCombo();
                }
            }

        ImGui::SameLine(0.0f, 18.0f);
        ImGui::BeginDisabled(m_AutoSave || !IsDirty());
        if (UI::AccentButton(ICON_FA_FLOPPY_DISK "  Save Asset"))
            Save();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Save", &m_AutoSave);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Write every change to the asset immediately.");
        if (m_AutoSave && IsDirty())
            Save();

        // Arama (sağda)
        const float searchWidth = 230.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - searchWidth));
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##iaSearch", ICON_FA_MAGNIFYING_GLASS "  Search actions / bindings", m_Search, sizeof(m_Search));
    }

    void InputActionsEditorPanel::DrawMaps()
    {
        ImGui::TextDisabled("Action Maps");
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::SmallButton(ICON_FA_PLUS "##addMap"))
        {
            const int index = AddMap();
            m_RenameKind = 1;
            SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), m_Data.Maps[(size_t)index].Name);
            m_RenameFocus = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Add Action Map");
        ImGui::Separator();

        int moveFrom = -1, moveTo = -1;
        for (int i = 0; i < (int)m_Data.Maps.size(); ++i)
        {
            ImGui::PushID(i);
            auto& map = m_Data.Maps[(size_t)i];
            const bool selected = m_Sel.Map == i && m_Sel.Action < 0;
            if (m_RenameKind == 1 && m_Sel.Map == i && m_Sel.Action < 0)
            {
                if (m_RenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    m_RenameFocus = false;
                }
                ImGui::SetNextItemWidth(-1.0f);
                const bool enter = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (enter || ImGui::IsItemDeactivated())
                {
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                        RenameSelection(m_RenameBuffer);
                    m_RenameKind = 0;
                }
            }
            else
            {
                if (ImGui::Selectable((std::string(ICON_FA_LAYER_GROUP "  ") + map.Name).c_str(), m_Sel.Map == i, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    Select(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        m_RenameKind = 1;
                        SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), map.Name);
                        m_RenameFocus = true;
                    }
                }
                (void)selected;
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("IA_MAP", &i, sizeof(int));
                    ImGui::TextUnformatted(map.Name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IA_MAP"))
                    {
                        moveFrom = *(const int*)p->Data;
                        moveTo = i;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("##mapContext"))
                {
                    Select(i);
                    if (ImGui::MenuItem("Rename", "F2"))
                    {
                        m_RenameKind = 1;
                        SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), map.Name);
                        m_RenameFocus = true;
                    }
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                        DuplicateSelection();
                    if (ImGui::MenuItem("Delete", "Del"))
                        DeleteSelection();
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
        {
            BeginEdit();
            InputActionMapDef map = m_Data.Maps[(size_t)moveFrom];
            m_Data.Maps.erase(m_Data.Maps.begin() + moveFrom);
            m_Data.Maps.insert(m_Data.Maps.begin() + moveTo, map);
            Select(moveTo);
            EndEdit();
        }
        if (m_Data.Maps.empty())
            ImGui::TextDisabled("No action maps.\nPress + to add one.");
    }

    void InputActionsEditorPanel::DrawAddBindingMenu(int mapIndex, int actionIndex)
    {
        const InputControlKind control = m_Data.Maps[(size_t)mapIndex].Actions[(size_t)actionIndex].Control;
        for (const auto& item : kAddItems)
        {
            // Vector 2: 2D Vector; Axis / Button: 1D Axis; Any: hepsi. Modifier composite'leri her türe uyar.
            const std::string composite = item.Composite;
            if (composite == "2DVector" && control != InputControlKind::Vector2 && control != InputControlKind::Any)
                continue;
            if (composite == "1DAxis" && control == InputControlKind::Vector2)
                continue;
            if (ImGui::MenuItem(item.Label))
            {
                if (!item.Composite[0])
                    AddBinding(mapIndex, actionIndex, std::string());
                else
                    AddComposite(mapIndex, actionIndex, item.Composite, {});
            }
        }
    }

    void InputActionsEditorPanel::DrawActions()
    {
        ImGui::TextDisabled("Actions");
        if (m_Sel.Map < 0 || m_Sel.Map >= (int)m_Data.Maps.size())
        {
            ImGui::Separator();
            ImGui::TextDisabled("Select or add an action map.");
            return;
        }
        const int mapIndex = m_Sel.Map;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::SmallButton(ICON_FA_PLUS "##addAction"))
        {
            const int index = AddAction(mapIndex);
            m_RenameKind = 2;
            SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), m_Data.Maps[(size_t)mapIndex].Actions[(size_t)index].Name);
            m_RenameFocus = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Add Action");
        ImGui::Separator();

        auto& map = m_Data.Maps[(size_t)mapIndex];
        int moveActionFrom = -1, moveActionTo = -1;
        struct BlockMove
        {
            int Action = -1, First = -1, Last = -1, Target = -1;
        } blockMove;
        int addMenuAction = -1;

        for (int ai = 0; ai < (int)map.Actions.size(); ++ai)
        {
            auto& action = map.Actions[(size_t)ai];
            if (!ActionVisible(action))
                continue;
            ImGui::PushID(ai);
            const bool actionSelected = m_Sel.Action == ai && m_Sel.Binding < 0;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
            if (actionSelected)
                flags |= ImGuiTreeNodeFlags_Selected;
            bool open = false;
            if (m_RenameKind == 2 && m_Sel.Action == ai && m_Sel.Binding < 0)
            {
                open = ImGui::TreeNodeEx("##action", flags, "%s", KindIcon(action.Kind));
                ImGui::SameLine();
                if (m_RenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    m_RenameFocus = false;
                }
                ImGui::SetNextItemWidth(-40.0f);
                const bool enter = ImGui::InputText("##renameAction", m_RenameBuffer, sizeof(m_RenameBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (enter || ImGui::IsItemDeactivated())
                {
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                        RenameSelection(m_RenameBuffer);
                    m_RenameKind = 0;
                }
            }
            else
            {
                open = ImGui::TreeNodeEx("##action", flags, "%s  %s", KindIcon(action.Kind), action.Name.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                {
                    Select(mapIndex, ai);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        m_RenameKind = 2;
                        SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), action.Name);
                        m_RenameFocus = true;
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s (%s)", InputActionAssets::KindName(action.Kind), InputActionAssets::ControlName(action.Control));
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("IA_ACTION", &ai, sizeof(int));
                    ImGui::TextUnformatted(action.Name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IA_ACTION"))
                    {
                        moveActionFrom = *(const int*)p->Data;
                        moveActionTo = ai;
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            if (ImGui::BeginPopupContextItem("##actionContext"))
            {
                Select(mapIndex, ai);
                if (ImGui::MenuItem("Rename", "F2"))
                {
                    m_RenameKind = 2;
                    SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), action.Name);
                    m_RenameFocus = true;
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    DuplicateSelection();
                if (ImGui::MenuItem("Delete", "Del"))
                    DeleteSelection();
                ImGui::Separator();
                DrawAddBindingMenu(mapIndex, ai);
                ImGui::EndPopup();
            }
            // "+" bağlama ekle
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
            if (ImGui::SmallButton(ICON_FA_PLUS "##addBinding"))
                addMenuAction = ai;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Add Binding / Composite");
            if (addMenuAction == ai)
                ImGui::OpenPopup("##addBindingMenu");
            if (ImGui::BeginPopup("##addBindingMenu"))
            {
                DrawAddBindingMenu(mapIndex, ai);
                ImGui::EndPopup();
            }

            if (open)
            {
                for (int bi = 0; bi < (int)action.Bindings.size();)
                {
                    auto& b = action.Bindings[(size_t)bi];
                    int first = 0, last = 0;
                    BindingBlock(action, bi, first, last);
                    if (b.IsPart())
                    {
                        bi = last;   // köksüz parça (bozuk dosya): atla
                        continue;
                    }
                    bool visible = BindingVisible(b);
                    if (b.IsComposite())
                    {
                        visible = false;
                        for (int k = first + 1; k < last; ++k)
                            visible |= BindingVisible(action.Bindings[(size_t)k]);
                        if (!m_Scheme.empty() && last == first + 1)
                            visible = BindingVisible(b);
                    }
                    if (!visible)
                    {
                        bi = last;
                        continue;
                    }
                    ImGui::PushID(bi);
                    const bool sel = m_Sel.Action == ai && m_Sel.Binding == bi;
                    if (b.IsComposite())
                    {
                        ImGuiTreeNodeFlags cflags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                        if (sel)
                            cflags |= ImGuiTreeNodeFlags_Selected;
                        bool copen = false;
                        if (m_RenameKind == 3 && sel)
                        {
                            copen = ImGui::TreeNodeEx("##composite", cflags, "%s", ICON_FA_DIAGRAM_PROJECT);
                            ImGui::SameLine();
                            if (m_RenameFocus)
                            {
                                ImGui::SetKeyboardFocusHere();
                                m_RenameFocus = false;
                            }
                            ImGui::SetNextItemWidth(-1.0f);
                            const bool enter = ImGui::InputText("##renameComposite", m_RenameBuffer, sizeof(m_RenameBuffer),
                                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                            if (enter || ImGui::IsItemDeactivated())
                            {
                                if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                                    RenameSelection(m_RenameBuffer);
                                m_RenameKind = 0;
                            }
                        }
                        else
                        {
                            copen = ImGui::TreeNodeEx("##composite", cflags, "%s  %s", ICON_FA_DIAGRAM_PROJECT, InputActionAssets::DisplayName(b).c_str());
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                                Select(mapIndex, ai, bi);
                        }
                        if (ImGui::BeginDragDropSource())
                        {
                            int payload[3] = { ai, first, last };
                            ImGui::SetDragDropPayload("IA_BINDING", payload, sizeof(payload));
                            ImGui::TextUnformatted(InputActionAssets::DisplayName(b).c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IA_BINDING"))
                            {
                                const int* d = (const int*)p->Data;
                                if (d[0] == ai)
                                    blockMove = { ai, d[1], d[2], first };
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (ImGui::BeginPopupContextItem("##compositeContext"))
                        {
                            Select(mapIndex, ai, bi);
                            if (ImGui::MenuItem("Rename", "F2"))
                            {
                                m_RenameKind = 3;
                                SetBuffer(m_RenameBuffer, sizeof(m_RenameBuffer), b.Name);
                                m_RenameFocus = true;
                            }
                            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                                DuplicateSelection();
                            if (ImGui::MenuItem("Delete", "Del"))
                                DeleteSelection();
                            ImGui::EndPopup();
                        }
                        if (copen)
                        {
                            for (int k = first + 1; k < last; ++k)
                            {
                                const auto& part = action.Bindings[(size_t)k];
                                if (!BindingVisible(part) && !m_Scheme.empty())
                                    continue;
                                ImGui::PushID(k);
                                ImGuiTreeNodeFlags pflags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                                if (m_Sel.Action == ai && m_Sel.Binding == k)
                                    pflags |= ImGuiTreeNodeFlags_Selected;
                                ImGui::TreeNodeEx("##part", pflags, "%s", InputActionAssets::DisplayName(part).c_str());
                                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                                    Select(mapIndex, ai, k);
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        ImGuiTreeNodeFlags lflags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (sel)
                            lflags |= ImGuiTreeNodeFlags_Selected;
                        const std::string label = InputActionAssets::DisplayName(b);
                        if (b.Path.empty())
                            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Warning);
                        ImGui::TreeNodeEx("##binding", lflags, "%s", label.c_str());
                        if (b.Path.empty())
                            ImGui::PopStyleColor();
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                            Select(mapIndex, ai, bi);
                        if (ImGui::BeginDragDropSource())
                        {
                            int payload[3] = { ai, first, last };
                            ImGui::SetDragDropPayload("IA_BINDING", payload, sizeof(payload));
                            ImGui::TextUnformatted(label.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IA_BINDING"))
                            {
                                const int* d = (const int*)p->Data;
                                if (d[0] == ai)
                                    blockMove = { ai, d[1], d[2], first };
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (ImGui::BeginPopupContextItem("##bindingContext"))
                        {
                            Select(mapIndex, ai, bi);
                            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                                DuplicateSelection();
                            if (ImGui::MenuItem("Delete", "Del"))
                                DeleteSelection();
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::PopID();
                    bi = last;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (moveActionFrom >= 0 && moveActionTo >= 0 && moveActionFrom != moveActionTo)
        {
            BeginEdit();
            InputActionDef moved = map.Actions[(size_t)moveActionFrom];
            map.Actions.erase(map.Actions.begin() + moveActionFrom);
            map.Actions.insert(map.Actions.begin() + moveActionTo, moved);
            Select(mapIndex, moveActionTo);
            EndEdit();
        }
        if (blockMove.Action >= 0 && blockMove.Target != blockMove.First)
        {
            auto& action = map.Actions[(size_t)blockMove.Action];
            BeginEdit();
            std::vector<InputBindingDef> block(action.Bindings.begin() + blockMove.First, action.Bindings.begin() + blockMove.Last);
            action.Bindings.erase(action.Bindings.begin() + blockMove.First, action.Bindings.begin() + blockMove.Last);
            int target = blockMove.Target;
            if (target > blockMove.First)
                target -= blockMove.Last - blockMove.First;
            target = std::clamp(target, 0, (int)action.Bindings.size());
            action.Bindings.insert(action.Bindings.begin() + target, block.begin(), block.end());
            Select(mapIndex, blockMove.Action, target);
            EndEdit();
        }
        if (map.Actions.empty())
            ImGui::TextDisabled("No actions.\nPress + to add one.");
    }

    void InputActionsEditorPanel::DrawProperties()
    {
        if (m_Sel.Map < 0 || m_Sel.Action < 0)
        {
            ImGui::TextDisabled("Properties");
            ImGui::Separator();
            ImGui::TextDisabled("Select an action or a binding.");
            return;
        }
        auto& action = m_Data.Maps[(size_t)m_Sel.Map].Actions[(size_t)m_Sel.Action];
        if (m_Sel.Binding < 0)
        {
            ImGui::TextDisabled("Action Properties");
            ImGui::Separator();
            DrawActionProperties(action);
        }
        else
        {
            ImGui::TextDisabled("Binding Properties");
            ImGui::Separator();
            DrawBindingProperties(action, m_Sel.Binding);
        }
    }

    void InputActionsEditorPanel::DrawActionProperties(InputActionDef& action)
    {
        UI::SubHeader("Action");
        static const char* kinds[] = { "Value", "Button", "Pass Through" };
        int kind = (int)action.Kind;
        if (UI::Combo("Action Type", kind, kinds, 3,
                      "Value: a continuous value (movement, look) - Started when it leaves zero, Performed on every change, Canceled at zero.\n"
                      "Button: press / release - Performed when pressed past the press point.\n"
                      "Pass Through: every change of any bound control is Performed, without conflict resolution."))
        {
            BeginEdit();
            action.Kind = (InputActionKind)kind;
            if (action.Kind == InputActionKind::Button)
                action.Control = InputControlKind::Button;
            else if (action.Control == InputControlKind::Button)
                action.Control = InputControlKind::Any;
            EndEdit();
        }
        if (action.Kind != InputActionKind::Button)
        {
            static const char* controls[] = { "Any", "Button", "Axis", "Vector 2" };
            int control = (int)action.Control;
            if (UI::Combo("Control Type", control, controls, 4, "The kind of value the action produces (Vector 2 for movement / look, Axis for a single -1..1 value)."))
            {
                BeginEdit();
                action.Control = (InputControlKind)control;
                EndEdit();
            }
        }
        if (action.Kind != InputActionKind::Value)
        {
            bool check = action.InitialStateCheck;
            if (UI::Checkbox("Initial State Check", check, "When the action is enabled, react to a control that is already held (Value actions always do)."))
            {
                BeginEdit();
                action.InitialStateCheck = check;
                EndEdit();
            }
        }
        const bool vector = action.Control == InputControlKind::Vector2;
        std::string interactions = action.Interactions;
        if (DrawSpecList("Interactions", interactions, true, vector))
        {
            BeginEdit();
            action.Interactions = interactions;
            EndEdit();
        }
        std::string processors = action.Processors;
        if (DrawSpecList("Processors", processors, false, vector))
        {
            BeginEdit();
            action.Processors = processors;
            EndEdit();
        }
    }

    bool InputActionsEditorPanel::DrawSchemeUsage(InputActionDef& action, int binding)
    {
        UI::SubHeader("Use in control scheme");
        if (m_Data.Schemes.empty())
        {
            ImGui::TextDisabled("No control schemes. Add one from the toolbar (All Control Schemes > Add Control Scheme...).");
            return false;
        }
        int first = 0, last = 0;
        BindingBlock(action, binding, first, last);
        bool changed = false;
        const bool isComposite = action.Bindings[(size_t)binding].IsComposite();
        for (const auto& scheme : m_Data.Schemes)
        {
            bool in = false;
            if (isComposite && last > first + 1)
            {
                // Composite: bütün parçalar bu şemadaysa işaretli
                in = true;
                for (int k = first + 1; k < last; ++k)
                {
                    const auto& g = action.Bindings[(size_t)k].Groups;
                    in &= std::find(g.begin(), g.end(), scheme.Name) != g.end();
                }
            }
            else
            {
                const auto& groups = action.Bindings[(size_t)binding].Groups;
                in = std::find(groups.begin(), groups.end(), scheme.Name) != groups.end();
            }
            ImGui::PushID(scheme.Name.c_str());
            if (ImGui::Checkbox(scheme.Name.c_str(), &in))
            {
                BeginEdit();
                // Composite: bütün parçalar birlikte (kökte grup tutulmaz); tek bağlama / parça: yalnız kendisi
                const int from = isComposite ? first + 1 : binding, to = isComposite ? last : binding + 1;
                for (int k = from; k < to; ++k)
                {
                    auto& g = action.Bindings[(size_t)k].Groups;
                    g.erase(std::remove(g.begin(), g.end(), scheme.Name), g.end());
                    if (in)
                        g.push_back(scheme.Name);
                }
                EndEdit();
                changed = true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    void InputActionsEditorPanel::DrawBindingProperties(InputActionDef& action, int binding)
    {
        auto& b = action.Bindings[(size_t)binding];
        if (b.IsComposite())
        {
            UI::SubHeader("Composite");
            std::string name = b.Name;
            if (UI::InputText("Name", name))
            {
                BeginEdit();
                b.Name = name;
                EndEdit();
            }
            const auto& types = InputActionAssets::CompositeTypes();
            int type = 0;
            for (size_t i = 0; i < types.size(); ++i)
                if (types[i] == b.Composite)
                    type = (int)i;
            std::vector<std::string> displays;
            std::vector<const char*> labels;
            for (const auto& t : types)
                displays.push_back(InputActionAssets::CompositeDisplay(t));
            for (const auto& d : displays)
                labels.push_back(d.c_str());
            if (UI::Combo("Composite Type", type, labels.data(), (int)labels.size(),
                          "2D Vector: up / down / left / right (WASD). 1D Axis: negative / positive.\nOne / Two Modifiers: a binding that needs modifier keys held (Ctrl+S)."))
            {
                BeginEdit();
                int first = 0, last = 0;
                BindingBlock(action, binding, first, last);
                std::vector<InputBindingDef> oldParts(action.Bindings.begin() + first + 1, action.Bindings.begin() + last);
                const std::string newType = types[(size_t)type];
                InputBindingDef root = action.Bindings[(size_t)first];
                root.Composite = newType;
                root.CompositeParams = newType == "2DVector" ? "mode=DigitalNormalized" : newType == "1DAxis" ? "whichSideWins=Neither" : std::string();
                if (root.Name == InputActionAssets::CompositeDisplay(b.Composite))
                    root.Name = InputActionAssets::CompositeDisplay(newType);
                std::vector<InputBindingDef> block{ root };
                const auto parts = InputActionAssets::CompositeParts(newType);
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    InputBindingDef part = i < oldParts.size() ? oldParts[i] : InputBindingDef{};
                    part.Part = parts[i];
                    if (i >= oldParts.size() && !oldParts.empty())
                        part.Groups = oldParts.front().Groups;
                    block.push_back(part);
                }
                action.Bindings.erase(action.Bindings.begin() + first, action.Bindings.begin() + last);
                action.Bindings.insert(action.Bindings.begin() + first, block.begin(), block.end());
                Select(m_Sel.Map, m_Sel.Action, first);
                EndEdit();
                return;   // b geçersiz
            }
            if (b.Composite == "2DVector")
            {
                static const char* modes[] = { "Digital Normalized", "Digital", "Analog" };
                static const char* modeKeys[] = { "DigitalNormalized", "Digital", "Analog" };
                const std::string mode = InputActionAssets::Param(b.CompositeParams, "mode", "DigitalNormalized");
                int m = mode == "Digital" ? 1 : mode == "Analog" ? 2 : 0;
                if (UI::Combo("Mode", m, modes, 3, "Digital Normalized: diagonals have length 1 (default). Digital: diagonals are (1, 1).\nAnalog: uses the pressure of analog parts."))
                {
                    BeginEdit();
                    b.CompositeParams = InputActionAssets::SetParam(b.CompositeParams, "mode", modeKeys[m]);
                    EndEdit();
                }
            }
            else if (b.Composite == "1DAxis")
            {
                static const char* sides[] = { "Neither", "Positive", "Negative" };
                const std::string side = InputActionAssets::Param(b.CompositeParams, "whichSideWins", "Neither");
                int s = side == "Positive" ? 1 : side == "Negative" ? 2 : 0;
                if (UI::Combo("Which Side Wins", s, sides, 3, "When both negative and positive are held: Neither gives 0."))
                {
                    BeginEdit();
                    b.CompositeParams = InputActionAssets::SetParam(b.CompositeParams, "whichSideWins", sides[s]);
                    EndEdit();
                }
            }
            DrawSchemeUsage(action, binding);
            std::string interactions = b.Interactions;
            if (DrawSpecList("Interactions", interactions, true, b.Composite == "2DVector"))
            {
                BeginEdit();
                action.Bindings[(size_t)binding].Interactions = interactions;
                EndEdit();
            }
            std::string processors = action.Bindings[(size_t)binding].Processors;
            if (DrawSpecList("Processors", processors, false, b.Composite == "2DVector"))
            {
                BeginEdit();
                action.Bindings[(size_t)binding].Processors = processors;
                EndEdit();
            }
            return;
        }

        UI::SubHeader(b.IsPart() ? ("Composite Part: " + Pretty(b.Part)).c_str() : "Binding");
        DrawPathPicker(b);
        DrawSchemeUsage(action, binding);
        const InputControlInfo* control = InputActionAssets::FindControl(b.Path);
        const bool vector = control && control->Is2D;
        if (!b.IsPart())
        {
            std::string interactions = b.Interactions;
            if (DrawSpecList("Interactions", interactions, true, vector))
            {
                BeginEdit();
                action.Bindings[(size_t)binding].Interactions = interactions;
                EndEdit();
            }
        }
        std::string processors = action.Bindings[(size_t)binding].Processors;
        if (DrawSpecList("Processors", processors, false, vector))
        {
            BeginEdit();
            action.Bindings[(size_t)binding].Processors = processors;
            EndEdit();
        }
    }

    void InputActionsEditorPanel::ListenForInput()
    {
        auto add = [&](const std::string& path) {
            if (InputActionAssets::FindControl(path) && std::find(m_ListenMatches.begin(), m_ListenMatches.end(), path) == m_ListenMatches.end())
                m_ListenMatches.push_back(path);
        };
        // Klavye: bu karede basılan tuşlar
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
            if (ImGui::IsKeyPressed((ImGuiKey)key, false))
            {
                const int code = GameInput::KeyCodeFromImGuiKey(key);
                for (const auto& c : InputActionAssets::Controls())
                    if (c.Source == InputControlSource::Key && c.Code == code)
                        add(c.Path);
            }
        // Gamepad: düğmeler, tetikler, çubuk yönleri (bağlı her gamepad)
        for (int pad = 0; pad < 4; ++pad)
        {
            if (!Input::IsGamepadConnected(pad))
                continue;
            for (const auto& c : InputActionAssets::Controls())
            {
                if (c.Device != "Gamepad")
                    continue;
                if (c.Source == InputControlSource::PadButton && Input::IsGamepadButtonPressed(c.Code, pad))
                    add(c.Path);
                else if (c.Source == InputControlSource::PadTrigger &&
                         (c.Code == 0 ? Input::GetGamepadLeftTrigger(pad) : Input::GetGamepadRightTrigger(pad)) > 0.5f)
                    add(c.Path);
                else if (c.Source == InputControlSource::PadStick || c.Source == InputControlSource::PadStickDir)
                {
                    glm::vec2 s = c.Code == 0 ? Input::GetGamepadLeftStick(pad) : Input::GetGamepadRightStick(pad);
                    s.y = -s.y;
                    if (glm::length(s) < 0.5f)
                        continue;
                    if (c.Source == InputControlSource::PadStick)
                        add(c.Path);
                    else
                    {
                        const float v = c.Extra == 0 ? s.y : c.Extra == 1 ? -s.y : c.Extra == 2 ? -s.x : s.x;
                        if (v > 0.5f)
                            add(c.Path);
                    }
                }
            }
        }
    }

    bool InputActionsEditorPanel::DrawPathPicker(InputBindingDef& binding)
    {
        bool changed = false;
        auto apply = [&](const std::string& path) {
            BeginEdit();
            binding.Path = path;
            if (!m_Scheme.empty() && binding.Groups.empty())
                binding.Groups.push_back(m_Scheme);
            EndEdit();
            changed = true;
        };
        UI::BeginProperty("Path", "The control this binding reads, e.g. <Keyboard>/space or <Gamepad>/leftStick.");
        const float buttonWidth = ImGui::GetFrameHeight();
        if (m_TextPath)
        {
            std::string path = binding.Path;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
            char buf[160];
            SetBuffer(buf, sizeof(buf), path);
            if (ImGui::InputText("##pathText", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
                if (path != buf)
                    apply(buf);
        }
        else
        {
            const std::string label = (binding.Path.empty() ? std::string("<No Binding>") : InputActionAssets::ControlDisplay(binding.Path)) + "##pathButton";
            if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
            {
                m_PickerSearch[0] = 0;
                m_Listening = false;
                m_ListenMatches.clear();
                // Unity açılır listesi gibi alanın hemen altında, alan genişliğinde
                m_PickerPos[0] = ImGui::GetItemRectMin().x;
                m_PickerPos[1] = ImGui::GetItemRectMax().y + 2.0f;
                m_PickerWidth = ImGui::GetItemRectSize().x;
                ImGui::OpenPopup("##controlPicker");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(m_TextPath ? ICON_FA_LIST "##pathMode" : "T##pathMode", ImVec2(buttonWidth, 0.0f)))
            m_TextPath = !m_TextPath;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(m_TextPath ? "Pick the control from the list" : "Edit the control path as text");

        ImGui::SetNextWindowPos(ImVec2(m_PickerPos[0], m_PickerPos[1]), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(330.0f, m_PickerWidth), 120.0f), ImVec2(std::max(460.0f, m_PickerWidth), 520.0f));
        if (ImGui::BeginPopup("##controlPicker"))
        {
            // Listen: basılan kontroller listelenir, seçilen atanır
            if (UI::ToggleButton(m_Listening ? ICON_FA_EAR_LISTEN "  Listening... press a key, button or move a stick" : ICON_FA_EAR_LISTEN "  Listen",
                                 m_Listening, ImVec2(-1.0f, 0.0f)))
                m_ListenMatches.clear();
            if (m_Listening)
            {
                ListenForInput();
                // Fare: bu alana tıklayınca / kaydırınca
                const ImVec2 areaSize(-1.0f, 46.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::Button(ICON_FA_COMPUTER_MOUSE "  Click or scroll here to bind the mouse##mouseArea", areaSize);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    const char* buttons[] = { "<Mouse>/leftButton", "<Mouse>/rightButton", "<Mouse>/middleButton" };
                    for (int mb = 0; mb < 3; ++mb)
                        if (ImGui::IsMouseClicked(mb) &&
                            std::find(m_ListenMatches.begin(), m_ListenMatches.end(), buttons[mb]) == m_ListenMatches.end())
                            m_ListenMatches.push_back(buttons[mb]);
                    if (ImGui::GetIO().MouseWheel != 0.0f &&
                        std::find(m_ListenMatches.begin(), m_ListenMatches.end(), "<Mouse>/scroll") == m_ListenMatches.end())
                        m_ListenMatches.push_back("<Mouse>/scroll");
                }
                if (m_ListenMatches.empty())
                    ImGui::TextDisabled("Waiting for input...");
                for (const auto& path : m_ListenMatches)
                    if (ImGui::Selectable(InputActionAssets::ControlDisplay(path).c_str()))
                    {
                        apply(path);
                        m_Listening = false;
                        ImGui::CloseCurrentPopup();
                    }
                ImGui::Separator();
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##pickerSearch", ICON_FA_MAGNIFYING_GLASS "  Search controls", m_PickerSearch, sizeof(m_PickerSearch));
            // Seçili şemanın aygıtları (yoksa hepsi)
            std::vector<std::string> devices = { "Keyboard", "Mouse", "Gamepad" };
            if (!m_Scheme.empty())
                for (const auto& s : m_Data.Schemes)
                    if (s.Name == m_Scheme)
                        devices = s.Devices;
            if (ImGui::BeginChild("##pickerList", ImVec2(0.0f, 320.0f)))
            {
                if (ImGui::Selectable("<No Binding>", binding.Path.empty()))
                {
                    apply(std::string());
                    ImGui::CloseCurrentPopup();
                }
                const bool searching = m_PickerSearch[0] != 0;
                for (const auto& device : devices)
                {
                    const char* icon = device == "Keyboard" ? ICON_FA_KEYBOARD : device == "Mouse" ? ICON_FA_COMPUTER_MOUSE : ICON_FA_GAMEPAD;
                    if (!searching && !ImGui::TreeNodeEx((std::string(icon) + "  " + device).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
                        continue;
                    std::string category;
                    bool categoryOpen = true;
                    for (const auto& c : InputActionAssets::Controls())
                    {
                        if (c.Device != device)
                            continue;
                        if (searching)
                        {
                            if (!Contains(c.Display, m_PickerSearch) && !Contains(c.Path, m_PickerSearch))
                                continue;
                            if (ImGui::Selectable((c.Display + " [" + c.Device + "]").c_str(), c.Path == binding.Path))
                            {
                                apply(c.Path);
                                ImGui::CloseCurrentPopup();
                            }
                            continue;
                        }
                        if (c.Category != category)
                        {
                            if (!category.empty() && categoryOpen && device != "Mouse")
                                ImGui::TreePop();
                            category = c.Category;
                            categoryOpen = device == "Mouse" || ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                        }
                        if (!categoryOpen)
                            continue;
                        if (ImGui::Selectable(c.Display.c_str(), c.Path == binding.Path))
                        {
                            apply(c.Path);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (!searching)
                    {
                        if (!category.empty() && categoryOpen && device != "Mouse")
                            ImGui::TreePop();
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        else
            m_Listening = false;
        UI::EndProperty();
        return changed;
    }

    bool InputActionsEditorPanel::DrawSpecList(const char* title, std::string& specsText, bool interactions, bool vector)
    {
        auto specs = InputActionAssets::ParseSpecs(specsText);
        bool changed = false;
        ImGui::PushID(title);
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", title);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::SmallButton(ICON_FA_PLUS "##addSpec"))
            ImGui::OpenPopup("##specMenu");
        ImGui::Separator();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(interactions ? "Add Interaction" : "Add Processor");
        if (ImGui::BeginPopup("##specMenu"))
        {
            const auto& catalog = interactions ? InputActionAssets::InteractionCatalog() : InputActionAssets::ProcessorCatalog();
            for (const auto& info : catalog)
            {
                // 2B olmayan değere 2B işlemcileri önerme (ve tersi)
                const std::string name = info.Name;
                const bool vectorOnly = name == "invertVector2" || name == "normalizeVector2" || name == "scaleVector2" || name == "stickDeadzone";
                const bool scalarOnly = name == "axisDeadzone" || name == "clamp" || name == "normalize";
                if (!interactions && ((vectorOnly && !vector) || (scalarOnly && vector)))
                    continue;
                if (ImGui::MenuItem(info.Display))
                {
                    InputActionAssets::Spec spec;
                    spec.Name = info.Name;
                    spec.Params = info.Defaults;
                    specs.push_back(spec);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && info.Tooltip)
                    ImGui::SetTooltip("%s", info.Tooltip);
            }
            ImGui::EndPopup();
        }
        if (specs.empty())
            ImGui::TextDisabled(interactions ? "No interactions (default behaviour of the action type)." : "No processors.");
        int remove = -1, moveUp = -1;
        for (int i = 0; i < (int)specs.size(); ++i)
        {
            auto& spec = specs[(size_t)i];
            const auto* info = interactions ? InputActionAssets::FindInteraction(spec.Name) : InputActionAssets::FindProcessor(spec.Name);
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(UI::Colors::Accent, "%s", info ? info->Display : spec.Name.c_str());
            if (info && info->Tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", info->Tooltip);
            const float right = ImGui::GetContentRegionMax().x;
            ImGui::SameLine(right - ImGui::GetFrameHeight() * 2.0f - ImGui::GetStyle().ItemSpacing.x);
            ImGui::BeginDisabled(i == 0);
            if (ImGui::SmallButton(ICON_FA_ARROW_UP))
                moveUp = i;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK))
                remove = i;
            // Parametreler (katalogdaki varsayılanlar + dosyadakiler)
            std::vector<std::pair<std::string, float>> params = info ? info->Defaults : std::vector<std::pair<std::string, float>>{};
            for (const auto& [k, v] : spec.Params)
                if (std::none_of(params.begin(), params.end(), [&](const auto& p) { return p.first == k; }))
                    params.push_back({ k, v });
            for (const auto& [key, def] : params)
            {
                float value = spec.Get(key, def);
                const std::string label = Pretty(key);
                bool edited = false;
                if (spec.Name == "press" && key == "behavior")
                {
                    static const char* behaviors[] = { "Press Only", "Release Only", "Press And Release" };
                    int b = std::clamp((int)std::lround(value), 0, 2);
                    if (UI::Combo(label.c_str(), b, behaviors, 3))
                    {
                        value = (float)b;
                        edited = true;
                    }
                }
                else if (spec.Name == "multiTap" && key == "tapCount")
                {
                    int count = std::max(1, (int)std::lround(value));
                    if (UI::DragInt(label.c_str(), count, 0.1f, 1, 10))
                    {
                        value = (float)count;
                        edited = true;
                    }
                }
                else if (spec.Name == "invertVector2")
                {
                    bool on = value != 0.0f;
                    if (UI::Checkbox(label.c_str(), on))
                    {
                        value = on ? 1.0f : 0.0f;
                        edited = true;
                    }
                }
                else
                    edited = UI::DragFloat(label.c_str(), value, 0.01f, key == "factor" || key == "x" || key == "y" || key == "min" || key == "zero" ? -1000.0f : 0.0f, 1000.0f, "%.3f");
                if (edited)
                {
                    spec.Set(key, value);
                    changed = true;
                }
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            specs.erase(specs.begin() + remove);
            changed = true;
        }
        if (moveUp > 0)
        {
            std::swap(specs[(size_t)moveUp], specs[(size_t)moveUp - 1]);
            changed = true;
        }
        ImGui::PopID();
        if (changed)
            specsText = InputActionAssets::FormatSpecs(specs);
        return changed;
    }

    void InputActionsEditorPanel::DrawSchemePopup()
    {
        if (m_SchemePopup)
        {
            ImGui::OpenPopup("Control Scheme##ia");
            m_SchemePopup = false;
        }
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Control Scheme##ia", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted("Scheme Name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##schemeName", m_SchemeName, sizeof(m_SchemeName));
        ImGui::Spacing();
        ImGui::TextUnformatted("Devices");
        static const char* deviceNames[] = { "Keyboard", "Mouse", "Gamepad" };
        static const char* need[] = { "Required", "Optional" };
        int removeDevice = -1;
        for (int i = 0; i < (int)m_SchemeDevices.size(); ++i)
        {
            ImGui::PushID(i);
            int d = 0;
            for (int k = 0; k < 3; ++k)
                if (m_SchemeDevices[(size_t)i] == deviceNames[k])
                    d = k;
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::Combo("##device", &d, deviceNames, 3))
                m_SchemeDevices[(size_t)i] = deviceNames[d];
            ImGui::SameLine();
            int opt = m_SchemeOptional[(size_t)i] ? 1 : 0;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("##need", &opt, need, 2))
                m_SchemeOptional[(size_t)i] = opt == 1;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK))
                removeDevice = i;
            ImGui::PopID();
        }
        if (removeDevice >= 0)
        {
            m_SchemeDevices.erase(m_SchemeDevices.begin() + removeDevice);
            m_SchemeOptional.erase(m_SchemeOptional.begin() + removeDevice);
        }
        if (ImGui::Button(ICON_FA_PLUS "  Add Device"))
        {
            m_SchemeDevices.push_back("Gamepad");
            m_SchemeOptional.push_back(false);
        }
        ImGui::Separator();
        std::string name = m_SchemeName;
        while (!name.empty() && std::isspace((unsigned char)name.back()))
            name.pop_back();
        bool unique = !name.empty();
        for (size_t i = 0; i < m_Data.Schemes.size(); ++i)
            if ((int)i != m_SchemeEditIndex && m_Data.Schemes[i].Name == name)
                unique = false;
        ImGui::BeginDisabled(!unique || m_SchemeDevices.empty());
        if (UI::AccentButton(m_SchemeEditIndex >= 0 ? "Save" : "Add", ImVec2(110.0f, 0.0f)))
        {
            BeginEdit();
            if (m_SchemeEditIndex >= 0 && m_SchemeEditIndex < (int)m_Data.Schemes.size())
            {
                auto& s = m_Data.Schemes[(size_t)m_SchemeEditIndex];
                const std::string old = s.Name;
                s.Name = name;
                s.Devices = m_SchemeDevices;
                s.Optional = m_SchemeOptional;
                if (old != name)
                    for (auto& map : m_Data.Maps)
                        for (auto& action : map.Actions)
                            for (auto& b : action.Bindings)
                                for (auto& g : b.Groups)
                                    if (g == old)
                                        g = name;
            }
            else
                m_Data.Schemes.push_back({ name, m_SchemeDevices, m_SchemeOptional });
            m_Scheme = name;
            m_Device.clear();
            EndEdit();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (!unique && !name.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(UI::Colors::Error, "A scheme with this name exists.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void InputActionsEditorPanel::DrawClosePrompt()
    {
        if (m_ClosePrompt)
        {
            ImGui::OpenPopup("Unsaved Input Actions##ia");
            m_ClosePrompt = false;
        }
        if (!ImGui::BeginPopupModal("Unsaved Input Actions##ia", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("%s has unsaved changes.", fs::path(m_Path).filename().string().c_str());
        ImGui::Spacing();
        if (UI::AccentButton("Save", ImVec2(110.0f, 0.0f)))
        {
            Save();
            m_Open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110.0f, 0.0f)))
        {
            m_Open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
