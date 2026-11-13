#include "UIControls.h"

#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

// Godot denetimlerinin kaydı (Add Component, betik alanları, dosya biçimi) ve GameObject > UI > Controls ile oluşturma
namespace Conqueror
{
    namespace
    {
        template<typename C>
        auto Interactable() { return [](C& c) -> bool& { return c.Selectable.Interactable; }; }

        void SaveStrings(YAML::Emitter& out, const char* key, const std::vector<std::string>& list)
        {
            out << YAML::Key << key << YAML::Value << YAML::BeginSeq;
            for (const auto& s : list) out << s;
            out << YAML::EndSeq;
        }
        void LoadStrings(const YAML::Node& n, const char* key, std::vector<std::string>& list)
        {
            if (auto l = n[key]; l && l.IsSequence())
            {
                list.clear();
                for (const auto& s : l) list.push_back(s.as<std::string>());
            }
        }
        std::string Str(const YAML::Node& n, const char* key) { return n[key] ? n[key].as<std::string>() : std::string(); }
    }

    namespace UIControls
    {
        void RegisterComponents()
        {
            {
                using C = UISpinBoxComponent;
                ComponentDef<C>("Spin Box", "UISpinBoxComponent", "UI", ICON_FA_ARROW_DOWN_UP_ACROSS_LINE)
                    .Describe("Godot SpinBox: a number field with up / down arrows (hold to repeat, mouse wheel, type a value). Emits "
                              "value_changed and calls OnValueChanged. Unity UGUI has no equivalent (UI Toolkit IntegerField / FloatField).")
                    .Field("Value", "Value", &C::Value)
                    .Field("MinValue", "Min Value", &C::MinValue)
                    .Field("MaxValue", "Max Value", &C::MaxValue)
                    .Field("Step", "Step", &C::Step).Range(0.0f, 100000.0f, 0.01f)
                    .Field("AllowGreater", "Allow Greater", &C::AllowGreater)
                    .Field("AllowLesser", "Allow Lesser", &C::AllowLesser)
                    .Field("Prefix", "Prefix", &C::Prefix)
                    .Field("Suffix", "Suffix", &C::Suffix)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("Editable", "Editable", &C::Editable)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnValueChanged", "On Value Changed", &C::OnValueChanged);
            }
            {
                using C = UIColorPickerComponent;
                ComponentDef<C>("Color Picker", "UIColorPickerComponent", "UI", ICON_FA_PALETTE)
                    .Describe("Godot ColorPicker: saturation / value square, hue bar, alpha bar and hex value. Emits color_changed and calls "
                              "OnColorChanged. (Unity has no runtime color picker.)")
                    .Field("Color", "Color", &C::Color, FieldKind::Color4)
                    .Field("EditAlpha", "Edit Alpha", &C::EditAlpha)
                    .Field("ShowHex", "Show Hex", &C::ShowHex)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnColorChanged", "On Color Changed", &C::OnColorChanged);
            }
            {
                using C = UIColorPickerButtonComponent;
                ComponentDef<C>("Color Picker Button", "UIColorPickerButtonComponent", "UI", ICON_FA_FILL_DRIP)
                    .Describe("Godot ColorPickerButton: shows the color; clicking opens a color picker below it. Emits color_changed, "
                              "picker_created and popup_closed.")
                    .Field("Color", "Color", &C::Color, FieldKind::Color4)
                    .Field("EditAlpha", "Edit Alpha", &C::EditAlpha)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnColorChanged", "On Color Changed", &C::OnColorChanged)
                    .Field("Open", "Open", &C::Open).ReadOnly();
            }
            {
                using C = UIMenuButtonComponent;
                ComponentDef<C>("Menu Button", "UIMenuButtonComponent", "UI", ICON_FA_BARS)
                    .Describe("Godot MenuButton: a button that opens a menu of actions. Items: \"-\" separator, \"[x] Name\" / \"[ ] Name\" "
                              "check item, \"!Name\" disabled. Emits id_pressed (index, text) and calls OnMenuItemPressed.")
                    .Field("Text", "Text", &C::Text)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("Flat", "Flat", &C::Flat)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnItemPressed", "On Item Pressed", &C::OnItemPressed)
                    .Field("LastPressed", "Last Pressed", &C::LastPressed).ReadOnly()
                    .Field("Open", "Open", &C::Open).ReadOnly()
                    .CustomData([](YAML::Emitter& out, C& c) { SaveStrings(out, "Items", c.Items); },
                                [](const YAML::Node& n, C& c) { LoadStrings(n, "Items", c.Items); });
            }
            {
                using C = UIPopupMenuComponent;
                ComponentDef<C>("Popup Menu", "UIPopupMenuComponent", "UI", ICON_FA_LIST)
                    .Describe("Godot PopupMenu: a context menu opened from scripts with PopupMenuShow(x, y) (local position of its top-left "
                              "corner). Closes when clicking elsewhere (popup_hide). Emits id_pressed and calls OnMenuItemPressed.")
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("HideOnItemSelection", "Hide On Item Selection", &C::HideOnItemSelection)
                    .Field("Visible", "Visible", &C::Visible)
                    .Field("Position", "Position", &C::Position)
                    .Field("OnItemPressed", "On Item Pressed", &C::OnItemPressed)
                    .Field("LastPressed", "Last Pressed", &C::LastPressed).ReadOnly()
                    .CustomData([](YAML::Emitter& out, C& c) { SaveStrings(out, "Items", c.Items); },
                                [](const YAML::Node& n, C& c) { LoadStrings(n, "Items", c.Items); });
            }
            {
                using C = UIMenuBarComponent;
                ComponentDef<C>("Menu Bar", "UIMenuBarComponent", "UI", ICON_FA_GRIP_LINES)
                    .Describe("Godot MenuBar: a row of menus (File, Edit ...). While one is open, hovering another title switches menus. "
                              "Emits menu_item_pressed (menu, item, text) and calls OnMenuBarItemPressed.")
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("OnItemPressed", "On Item Pressed", &C::OnItemPressed)
                    .Field("LastMenu", "Last Menu", &C::LastMenu).ReadOnly()
                    .Field("LastItem", "Last Item", &C::LastItem).ReadOnly()
                    .CustomData(
                        [](YAML::Emitter& out, C& c) {
                            out << YAML::Key << "Menus" << YAML::Value << YAML::BeginSeq;
                            for (const auto& m : c.Menus)
                            {
                                out << YAML::BeginMap << YAML::Key << "Title" << YAML::Value << m.Title;
                                SaveStrings(out, "Items", m.Items);
                                out << YAML::EndMap;
                            }
                            out << YAML::EndSeq;
                        },
                        [](const YAML::Node& n, C& c) {
                            if (auto l = n["Menus"]; l && l.IsSequence())
                            {
                                c.Menus.clear();
                                for (const auto& m : l)
                                {
                                    UIMenuBarComponent::Menu menu;
                                    menu.Title = Str(m, "Title");
                                    LoadStrings(m, "Items", menu.Items);
                                    c.Menus.push_back(menu);
                                }
                            }
                        });
            }
            {
                using C = UITabContainerComponent;
                ComponentDef<C>("Tab Container", "UITabContainerComponent", "UI", ICON_FA_FOLDER)
                    .Describe("Godot TabContainer: each child is a tab (its name is the title); only the current tab is shown and fills "
                              "the area under the tabs. Emits tab_changed and calls OnTabChanged. (Unity UI Toolkit TabView.)")
                    .Field("CurrentTab", "Current Tab", &C::CurrentTab)
                    .Field("TabsVisible", "Tabs Visible", &C::TabsVisible)
                    .Field("TabHeight", "Tab Height", &C::TabHeight).Range(10.0f, 200.0f, 0.5f)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("OnTabChanged", "On Tab Changed", &C::OnTabChanged)
                    .Field("TabCount", "Tab Count", &C::TabCount).ReadOnly();
            }
            {
                using C = UITabBarComponent;
                ComponentDef<C>("Tab Bar", "UITabBarComponent", "UI", ICON_FA_TABLE_COLUMNS)
                    .Describe("Godot TabBar: a row of tabs without content (switch content yourself on tab_changed). Optional close "
                              "buttons emit tab_close_pressed.")
                    .Field("CurrentTab", "Current Tab", &C::CurrentTab)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("CloseButtons", "Close Buttons", &C::CloseButtons)
                    .Field("OnTabChanged", "On Tab Changed", &C::OnTabChanged)
                    .CustomData([](YAML::Emitter& out, C& c) { SaveStrings(out, "Tabs", c.Tabs); },
                                [](const YAML::Node& n, C& c) { LoadStrings(n, "Tabs", c.Tabs); });
            }
            {
                using C = UISplitContainerComponent;
                ComponentDef<C>("Split Container", "UISplitContainerComponent", "UI", ICON_FA_TABLE_COLUMNS)
                    .Describe("Godot HSplitContainer / VSplitContainer: the first two children fill two panes separated by a draggable "
                              "handle. Emits dragged (ratio). (Unity UI Toolkit TwoPaneSplitView.)")
                    .Field("Vertical", "Vertical", &C::Vertical).Tooltip("Off: side by side; on: top and bottom")
                    .Field("SplitRatio", "Split Ratio", &C::SplitRatio).Range(0.0f, 1.0f, 0.005f)
                    .Field("MinPaneSize", "Min Pane Size", &C::MinPaneSize).Range(0.0f, 2000.0f, 1.0f)
                    .Field("Collapsed", "Collapsed", &C::Collapsed)
                    .Field("DraggerVisible", "Dragger Visible", &C::DraggerVisible)
                    .Field("DraggerThickness", "Dragger Thickness", &C::DraggerThickness).Range(1.0f, 100.0f, 0.5f)
                    .Field("OnDragged", "On Dragged", &C::OnDragged);
            }
            {
                using C = UIItemListComponent;
                ComponentDef<C>("Item List", "UIItemListComponent", "UI", ICON_FA_LIST_UL)
                    .Describe("Godot ItemList: a scrolling list of items with optional icons, single or multi selection and columns. Emits "
                              "item_selected / multi_selected / item_activated (double click). (Unity UI Toolkit ListView.)")
                    .Field("SelectMode", "Select Mode", &C::SelectMode).Options({ "Single", "Multi" })
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("RowHeight", "Row Height", &C::RowHeight).Range(0.0f, 400.0f, 0.5f).Tooltip("0: from the font size")
                    .Field("MaxColumns", "Max Columns", &C::MaxColumns).Range(1.0f, 32.0f, 1.0f)
                    .Field("AllowReselect", "Allow Reselect", &C::AllowReselect)
                    .Field("OnItemSelected", "On Item Selected", &C::OnItemSelected)
                    .Field("LastActivated", "Last Activated", &C::LastActivated).ReadOnly()
                    .CustomData(
                        [](YAML::Emitter& out, C& c) {
                            out << YAML::Key << "Items" << YAML::Value << YAML::BeginSeq;
                            for (const auto& i : c.Items)
                                out << YAML::Flow << YAML::BeginMap << YAML::Key << "Text" << YAML::Value << i.Text << YAML::Key << "Icon" << YAML::Value << i.Icon
                                    << YAML::Key << "Disabled" << YAML::Value << i.Disabled << YAML::EndMap;
                            out << YAML::EndSeq;
                            out << YAML::Key << "Selected" << YAML::Value << YAML::Flow << c.Selected;
                        },
                        [](const YAML::Node& n, C& c) {
                            if (auto l = n["Items"]; l && l.IsSequence())
                            {
                                c.Items.clear();
                                for (const auto& i : l)
                                    c.Items.push_back({ Str(i, "Text"), Str(i, "Icon"), i["Disabled"] && i["Disabled"].as<bool>() });
                            }
                            if (auto s = n["Selected"]; s && s.IsSequence())
                            {
                                c.Selected.clear();
                                for (const auto& v : s) c.Selected.push_back(v.as<int>());
                            }
                        });
            }
            {
                using C = UITreeComponent;
                ComponentDef<C>("Tree", "UITreeComponent", "UI", ICON_FA_SITEMAP)
                    .Describe("Godot Tree: a hierarchical list; items with children fold with their arrow. Emits item_selected, "
                              "item_activated (double click) and item_collapsed. (Unity UI Toolkit TreeView.)")
                    .Field("HideRoot", "Hide Root", &C::HideRoot)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("Indent", "Indent", &C::Indent).Range(0.0f, 200.0f, 0.5f)
                    .Field("OnItemSelected", "On Item Selected", &C::OnItemSelected)
                    .Field("Selected", "Selected", &C::Selected).ReadOnly()
                    .Field("LastActivated", "Last Activated", &C::LastActivated).ReadOnly()
                    .CustomData(
                        [](YAML::Emitter& out, C& c) {
                            out << YAML::Key << "Items" << YAML::Value << YAML::BeginSeq;
                            for (const auto& i : c.Items)
                                out << YAML::Flow << YAML::BeginMap << YAML::Key << "Text" << YAML::Value << i.Text << YAML::Key << "Depth" << YAML::Value << i.Depth
                                    << YAML::Key << "Collapsed" << YAML::Value << i.Collapsed << YAML::EndMap;
                            out << YAML::EndSeq;
                        },
                        [](const YAML::Node& n, C& c) {
                            if (auto l = n["Items"]; l && l.IsSequence())
                            {
                                c.Items.clear();
                                for (const auto& i : l)
                                    c.Items.push_back({ Str(i, "Text"), i["Depth"] ? i["Depth"].as<int>() : 0, i["Collapsed"] && i["Collapsed"].as<bool>() });
                            }
                        });
            }
            {
                using C = UILinkButtonComponent;
                ComponentDef<C>("Link Button", "UILinkButtonComponent", "UI", ICON_FA_LINK)
                    .Describe("Godot LinkButton: underlined text that acts as a button; with a URI it opens it in the browser. Emits pressed "
                              "and calls OnPressed.")
                    .Field("Text", "Text", &C::Text)
                    .Field("Uri", "URI", &C::Uri)
                    .Field("Underline", "Underline", &C::Underline).Options({ "Always", "On Hover", "Never" })
                    .Field("Color", "Color", &C::Color, FieldKind::Color4)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnClick", "On Click", &C::OnClick)
                    .Field("Clicks", "Clicks", &C::Clicks).ReadOnly();
            }
            {
                using C = UIProgressBarComponent;
                ComponentDef<C>("Progress Bar", "UIProgressBarComponent", "UI", ICON_FA_BARS_PROGRESS)
                    .Describe("Godot ProgressBar: fill from a value between Min and Max with the percentage written on it; Indeterminate "
                              "animates while the amount is unknown. (Unity UI Toolkit ProgressBar.)")
                    .Field("Value", "Value", &C::Value)
                    .Field("MinValue", "Min Value", &C::MinValue)
                    .Field("MaxValue", "Max Value", &C::MaxValue)
                    .Field("ShowPercentage", "Show Percentage", &C::ShowPercentage)
                    .Field("FillMode", "Fill Mode", &C::FillMode).Options({ "Begin To End", "End To Begin", "Top To Bottom", "Bottom To Top" })
                    .Field("Indeterminate", "Indeterminate", &C::Indeterminate)
                    .Field("FillColor", "Fill Color", &C::FillColor, FieldKind::Color4)
                    .Field("BackgroundColor", "Background Color", &C::BackgroundColor, FieldKind::Color4)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f);
            }
            {
                using C = UIDialogComponent;
                ComponentDef<C>("Dialog", "UIDialogComponent", "UI", ICON_FA_WINDOW_MAXIMIZE)
                    .Describe("Godot AcceptDialog / ConfirmationDialog: a modal panel with a title, text and OK (and Cancel) buttons over a "
                              "dimmed screen; nothing else can be clicked while it is open. Open it with DialogPopup. Emits confirmed / "
                              "canceled and calls OnConfirmed / OnCanceled.")
                    .Field("Kind", "Kind", &C::Kind).Options({ "Accept", "Confirmation" })
                    .Field("Title", "Title", &C::Title)
                    .Field("Text", "Text", &C::Text)
                    .Field("OkText", "OK Text", &C::OkText)
                    .Field("CancelText", "Cancel Text", &C::CancelText)
                    .Field("Visible", "Visible", &C::Visible)
                    .Field("Size", "Size", &C::Size)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("HideOnOk", "Hide On OK", &C::HideOnOk)
                    .Field("OnConfirmed", "On Confirmed", &C::OnConfirmed)
                    .Field("Confirmed", "Confirmed", &C::Confirmed).ReadOnly()
                    .Field("Canceled", "Canceled", &C::Canceled).ReadOnly();
            }
            {
                using C = UIFileDialogComponent;
                ComponentDef<C>("File Dialog", "UIFileDialogComponent", "UI", ICON_FA_FOLDER_OPEN)
                    .Describe("Godot FileDialog: an in-game file browser (open one or more files, a folder, or save a file) in the user data "
                              "folder, the game folder or the whole file system, with filters such as \"*.png, *.jpg\". Open it with "
                              "FileDialogPopup; the choice comes with file_selected / OnFileSelected(path) and FileDialogGetPath.")
                    .Field("FileMode", "File Mode", &C::FileMode).Options({ "Open File", "Open Files", "Open Dir", "Save File" })
                    .Field("Access", "Access", &C::Access).Options({ "User Data", "Game Data", "File System" })
                    .Field("Title", "Title", &C::Title)
                    .Field("RootSubfolder", "Root Subfolder", &C::RootSubfolder)
                    .Field("Filters", "Filters", &C::Filters).Tooltip("Comma separated, e.g. *.png, *.jpg (empty: all files)")
                    .Field("Visible", "Visible", &C::Visible)
                    .Field("Size", "Size", &C::Size)
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("OnFileSelected", "On File Selected", &C::OnFileSelected)
                    .Field("CurrentDir", "Current Dir", &C::CurrentDir)
                    .Field("LastSelected", "Last Selected", &C::LastSelected).ReadOnly();
            }
            {
                using C = UITextEditComponent;
                ComponentDef<C>("Text Edit", "UITextEditComponent", "UI", ICON_FA_FILE_LINES)
                    .Describe("Godot TextEdit / CodeEdit: multi-line text editing (caret, arrows, Home / End, scrolling). Code Editor adds line "
                              "numbers, syntax colors for keywords / numbers / strings / comments, auto indent and bracket closing. Emits "
                              "text_changed and calls OnTextChanged.")
                    .Field("Text", "Text", &C::Text)
                    .Field("Editable", "Editable", &C::Editable)
                    .Field("CodeEditor", "Code Editor", &C::CodeEditor)
                    .Field("LineNumbers", "Line Numbers", &C::LineNumbers)
                    .Field("HighlightSyntax", "Highlight Syntax", &C::HighlightSyntax)
                    .Field("AutoBraceCompletion", "Auto Brace Completion", &C::AutoBraceCompletion)
                    .Field("AutoIndent", "Auto Indent", &C::AutoIndent)
                    .Field("Keywords", "Keywords", &C::Keywords).Tooltip("Space separated words colored as keywords")
                    .Field("FontSize", "Font Size", &C::FontSize).Range(6.0f, 200.0f, 0.5f)
                    .Field("Placeholder", "Placeholder", &C::Placeholder)
                    .FieldVia("Interactable", "Interactable", Interactable<C>())
                    .Field("OnTextChanged", "On Text Changed", &C::OnTextChanged)
                    .Field("CaretLine", "Caret Line", &C::CaretLine).ReadOnly()
                    .Field("CaretColumn", "Caret Column", &C::CaretColumn).ReadOnly();
            }
            {
                using C = UIGraphEditComponent;
                ComponentDef<C>("Graph Edit", "UIGraphEditComponent", "UI", ICON_FA_DIAGRAM_PROJECT)
                    .Describe("Godot GraphEdit: an area for Graph Node children with a grid, panning (drag the background), zoom (wheel) "
                              "and connections. Dragging from an output port to an input port emits connection_request (Auto Connect adds "
                              "it); dragging a connected input away emits disconnection_request.")
                    .Field("ScrollOffset", "Scroll Offset", &C::ScrollOffset)
                    .Field("Zoom", "Zoom", &C::Zoom).Range(0.3f, 3.0f, 0.01f)
                    .Field("ShowGrid", "Show Grid", &C::ShowGrid)
                    .Field("AutoConnect", "Auto Connect", &C::AutoConnect)
                    .Field("OnConnectionRequest", "On Connection Request", &C::OnConnectionRequest)
                    .CustomData(
                        [](YAML::Emitter& out, C& c) {
                            out << YAML::Key << "Connections" << YAML::Value << YAML::BeginSeq;
                            for (const auto& k : c.Connections)
                                out << YAML::Flow << YAML::BeginSeq << k.From << k.FromPort << k.To << k.ToPort << YAML::EndSeq;
                            out << YAML::EndSeq;
                        },
                        [](const YAML::Node& n, C& c) {
                            if (auto l = n["Connections"]; l && l.IsSequence())
                            {
                                c.Connections.clear();
                                for (const auto& k : l)
                                    if (k.IsSequence() && k.size() >= 4)
                                        c.Connections.push_back({ k[0].as<uint64_t>(), k[1].as<int>(), k[2].as<uint64_t>(), k[3].as<int>() });
                            }
                        });
            }
            {
                using C = UIGraphNodeComponent;
                ComponentDef<C>("Graph Node", "UIGraphNodeComponent", "UI", ICON_FA_CUBE)
                    .Describe("Godot GraphNode: a node inside a Graph Edit with a title bar (drag to move) and input / output ports.")
                    .Field("Title", "Title", &C::Title)
                    .Field("PositionOffset", "Position Offset", &C::PositionOffset)
                    .Field("InputPorts", "Input Ports", &C::InputPorts).Range(0.0f, 32.0f, 1.0f)
                    .Field("OutputPorts", "Output Ports", &C::OutputPorts).Range(0.0f, 32.0f, 1.0f)
                    .Field("TitleColor", "Title Color", &C::TitleColor, FieldKind::Color4)
                    .Field("Draggable", "Draggable", &C::Draggable)
                    .Field("Selected", "Selected", &C::Selected);
            }
        }

        Entity CreateElement(Scene& scene, const std::string& type, Entity parent)
        {
            auto make = [&](const std::string& name, Entity p, glm::vec2 size) {
                Entity e = scene.CreateEntity(name);
                auto& rt = e.AddComponent<RectTransformComponent>();
                rt.SizeDelta = size;
                if (e.HasComponent<LayerComponent>()) e.GetComponent<LayerComponent>().Layer = 5;
                e.SetParent(p);
                return e;
            };
            auto stretch = [](Entity e) {
                auto& rt = e.GetComponent<RectTransformComponent>();
                rt.AnchorMin = glm::vec2(0.0f);
                rt.AnchorMax = glm::vec2(1.0f);
                rt.SizeDelta = glm::vec2(0.0f);
                rt.AnchoredPosition = glm::vec2(0.0f);
            };
            auto panel = [&](const std::string& name, Entity p, const std::string& text) {
                Entity e = make(name, p, { 0, 0 });
                auto& img = e.AddComponent<UIImageComponent>();
                img.Color = glm::vec4(0.96f, 0.96f, 0.98f, 1.0f);
                Entity t = make("Text", e, { 0, 0 });
                stretch(t);
                auto& tc = t.AddComponent<UITextComponent>();
                tc.Text = text;
                tc.FontSize = 24.0f;
                tc.AlignH = UITextAlignH::Center;
                return e;
            };
            Entity e;
            if (type == "SpinBox") { e = make("Spin Box", parent, { 200, 48 }); e.AddComponent<UISpinBoxComponent>(); }
            else if (type == "ColorPicker") { e = make("Color Picker", parent, { 300, 320 }); e.AddComponent<UIColorPickerComponent>(); }
            else if (type == "ColorPickerButton") { e = make("Color Picker Button", parent, { 90, 44 }); e.AddComponent<UIColorPickerButtonComponent>(); }
            else if (type == "MenuButton") { e = make("Menu Button", parent, { 140, 44 }); e.AddComponent<UIMenuButtonComponent>(); }
            else if (type == "PopupMenu") { e = make("Popup Menu", parent, { 10, 10 }); e.AddComponent<UIPopupMenuComponent>(); }
            else if (type == "MenuBar") { e = make("Menu Bar", parent, { 600, 40 }); e.AddComponent<UIMenuBarComponent>(); }
            else if (type == "TabContainer")
            {
                e = make("Tab Container", parent, { 520, 360 });
                e.AddComponent<UITabContainerComponent>();
                for (int i = 1; i <= 3; ++i) panel("Tab " + std::to_string(i), e, "Content of tab " + std::to_string(i));
            }
            else if (type == "TabBar") { e = make("Tab Bar", parent, { 420, 44 }); e.AddComponent<UITabBarComponent>(); }
            else if (type == "HSplitContainer" || type == "VSplitContainer")
            {
                e = make(type == "HSplitContainer" ? "HSplit Container" : "VSplit Container", parent, { 520, 320 });
                e.AddComponent<UISplitContainerComponent>().Vertical = type == "VSplitContainer";
                panel("Pane A", e, "A");
                panel("Pane B", e, "B");
            }
            else if (type == "ItemList") { e = make("Item List", parent, { 300, 260 }); e.AddComponent<UIItemListComponent>(); }
            else if (type == "Tree") { e = make("Tree", parent, { 300, 260 }); e.AddComponent<UITreeComponent>(); }
            else if (type == "LinkButton") { e = make("Link Button", parent, { 160, 40 }); e.AddComponent<UILinkButtonComponent>(); }
            else if (type == "ProgressBar") { e = make("Progress Bar", parent, { 320, 30 }); e.AddComponent<UIProgressBarComponent>().Value = 40.0f; }
            else if (type == "AcceptDialog" || type == "ConfirmationDialog")
            {
                e = make(type == "AcceptDialog" ? "Accept Dialog" : "Confirmation Dialog", parent, { 0, 0 });
                stretch(e);
                auto& d = e.AddComponent<UIDialogComponent>();
                d.Kind = type == "ConfirmationDialog" ? 1 : 0;
                if (d.Kind == 0) d.Text = "Done.";
            }
            else if (type == "FileDialog") { e = make("File Dialog", parent, { 0, 0 }); stretch(e); e.AddComponent<UIFileDialogComponent>(); }
            else if (type == "TextEdit") { e = make("Text Edit", parent, { 420, 220 }); e.AddComponent<UITextEditComponent>().Placeholder = "Type here..."; }
            else if (type == "CodeEdit")
            {
                e = make("Code Edit", parent, { 520, 300 });
                auto& t = e.AddComponent<UITextEditComponent>();
                t.CodeEditor = true;
                t.Text = "func _ready() {\n    var speed = 10 // units\n    print(\"hello\")\n}";
            }
            else if (type == "GraphEdit")
            {
                e = make("Graph Edit", parent, { 640, 400 });
                e.AddComponent<UIGraphEditComponent>();
                Entity a = make("Node A", e, { 160, 110 });
                auto& na = a.AddComponent<UIGraphNodeComponent>();
                na.Title = "Node A";
                na.PositionOffset = { 40.0f, 60.0f };
                na.InputPorts = 0;
                na.OutputPorts = 2;
                Entity b = make("Node B", e, { 160, 110 });
                auto& nb = b.AddComponent<UIGraphNodeComponent>();
                nb.Title = "Node B";
                nb.PositionOffset = { 340.0f, 120.0f };
                nb.InputPorts = 2;
                nb.OutputPorts = 1;
            }
            else if (type == "GraphNode") { e = make("Graph Node", parent, { 160, 110 }); e.AddComponent<UIGraphNodeComponent>(); }
            return e;
        }
    }
}
