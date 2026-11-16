#include "BindWidgetsPanel.h"

#include "Core/Project/Project.h"
#include "EditorState.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        // Türün öğede bulunması gereken bileşeni (Panel: yalnız Rect Transform)
        bool HasType(Entity e, const std::string& type)
        {
            if (type == "Button") return e.HasComponent<UIButtonComponent>();
            if (type == "Text" || type == "TextBlock" || type == "Label") return e.HasComponent<UITextComponent>();
            if (type == "Image") return e.HasComponent<UIImageComponent>();
            if (type == "Toggle" || type == "CheckBox") return e.HasComponent<UIToggleComponent>();
            if (type == "Slider") return e.HasComponent<UISliderComponent>();
            if (type == "Scrollbar") return e.HasComponent<UIScrollbarComponent>();
            if (type == "InputField" || type == "EditableText") return e.HasComponent<UIInputFieldComponent>();
            if (type == "Dropdown" || type == "ComboBox") return e.HasComponent<UIDropdownComponent>();
            if (type == "ScrollView" || type == "ScrollBox") return e.HasComponent<UIScrollRectComponent>();
            if (type == "RawImage") return e.HasComponent<UIRawImageComponent>();
            return e.HasComponent<RectTransformComponent>();   // Panel / Widget / bilinmeyen: herhangi bir UI öğesi
        }

        std::string TypeOf(Entity e)
        {
            if (e.HasComponent<UIButtonComponent>()) return "Button";
            if (e.HasComponent<UIToggleComponent>()) return "Toggle";
            if (e.HasComponent<UISliderComponent>()) return "Slider";
            if (e.HasComponent<UIInputFieldComponent>()) return "InputField";
            if (e.HasComponent<UIDropdownComponent>()) return "Dropdown";
            if (e.HasComponent<UITextComponent>()) return "Text";
            if (e.HasComponent<UIImageComponent>()) return "Image";
            return "Panel";
        }

        const char* CreateType(const std::string& type)
        {
            if (type == "TextBlock" || type == "Label") return "Text";
            if (type == "CheckBox") return "Toggle";
            if (type == "InputField" || type == "EditableText") return "Input Field";
            if (type == "ComboBox") return "Dropdown";
            if (type == "ScrollView" || type == "ScrollBox") return "Scroll View";
            if (type == "RawImage") return "Raw Image";
            if (type == "Button" || type == "Text" || type == "Image" || type == "Toggle" || type == "Slider" || type == "Scrollbar" || type == "Dropdown")
                return nullptr;   // aynı ad
            return "Panel";
        }

        void Walk(Entity e, std::vector<Entity>& out)
        {
            out.push_back(e);
            for (Entity c : e.GetChildren())
                Walk(c, out);
        }
    }

    std::vector<WidgetBinding> BindWidgetsPanel::Scan(Entity canvas)
    {
        std::vector<WidgetBinding> out;
        if (!canvas)
            return out;
        std::vector<Entity> all;
        Walk(canvas, all);
        // Betikler: canvas ve altındaki öğelere bağlı betik dosyaları
        std::set<std::string> scripts;
        for (Entity e : all)
            if (e.HasComponent<ConquerorScriptComponent>())
                for (const auto& s : e.GetComponent<ConquerorScriptComponent>().Scripts)
                    if (!s.ScriptPath.empty())
                        scripts.insert(s.ScriptPath);
        static const std::regex comment(R"(@bind(\?)?\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*))");
        static const std::regex attribute(R"((?:\[|@)BindWidget(Optional)?\]?\s*(?:public\s+|private\s+|protected\s+)?([A-Za-z_]\w*)\s+([A-Za-z_]\w*))");
        static const std::regex call(R"(BindWidget(Optional)?\s*\(\s*["']([A-Za-z_]\w*)["']\s*,\s*["']([A-Za-z_]\w*)["']\s*\))");
        const fs::path project = Project::GetActiveProjectDirectory();
        for (const std::string& script : scripts)
        {
            fs::path p(script);
            if (p.is_relative())
                p = project / p;
            std::ifstream in(p);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto add = [&](const std::string& type, const std::string& name, bool optional) {
                for (const auto& b : out)
                    if (b.Name == name)
                        return;
                WidgetBinding b{ name, type, fs::path(script).filename().string(), optional, 1, {} };
                for (Entity e : all)
                    if (e != canvas && e.GetComponent<TagComponent>().Tag == name)
                    {
                        b.FoundType = TypeOf(e);
                        b.Status = HasType(e, type) ? 0 : 2;
                        break;
                    }
                out.push_back(b);
            };
            for (auto it = std::sregex_iterator(text.begin(), text.end(), comment); it != std::sregex_iterator(); ++it)
                add((*it)[2], (*it)[3], (*it)[1].matched);
            for (auto it = std::sregex_iterator(text.begin(), text.end(), attribute); it != std::sregex_iterator(); ++it)
                add((*it)[2], (*it)[3], (*it)[1].matched);
            for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it)
                add((*it)[3], (*it)[2], (*it)[1].matched);
        }
        return out;
    }

    std::vector<std::string> BindWidgetsPanel::MissingRequired(Entity canvas)
    {
        std::vector<std::string> out;
        for (const auto& b : Scan(canvas))
            if (!b.Optional && b.Status != 0)
                out.push_back(b.Script + " expects " + b.Type + " '" + b.Name + "' in " + canvas.GetComponent<TagComponent>().Tag +
                              (b.Status == 2 ? " (it is a " + b.FoundType + ")" : " (missing)"));
        return out;
    }

    void BindWidgetsPanel::OnImGuiRender(bool* open, Entity canvas)
    {
        if (!ImGui::Begin(ICON_FA_LINK " Bind Widgets###BindWidgets", open))
        {
            ImGui::End();
            return;
        }
        if (!canvas)
        {
            ImGui::TextDisabled("Open a canvas in the UI Editor.");
            ImGui::End();
            return;
        }
        const auto bindings = Scan(canvas);
        ImGui::TextDisabled("UI elements the canvas's scripts expect by name (@bind Type Name, [BindWidget], BindWidget(\"Name\", \"Type\")).");
        if (bindings.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("No script on this canvas declares bindings. Example (LuaQ): -- @bind Button StartButton");
            ImGui::End();
            return;
        }
        if (ImGui::BeginTable("##binds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Widget");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (const auto& b : bindings)
            {
                ImGui::PushID(b.Name.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.Name.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Declared in %s", b.Script.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s%s", b.Type.c_str(), b.Optional ? " (optional)" : "");
                ImGui::TableNextColumn();
                if (b.Status == 0)
                    ImGui::TextColored(ImVec4(0.42f, 0.85f, 0.52f, 1), ICON_FA_CIRCLE_CHECK " Bound");
                else if (b.Status == 2)
                    ImGui::TextColored(ImVec4(1, 0.47f, 0.43f, 1), ICON_FA_TRIANGLE_EXCLAMATION " Is a %s", b.FoundType.c_str());
                else
                    ImGui::TextColored(b.Optional ? ImVec4(0.98f, 0.72f, 0.25f, 1) : ImVec4(1, 0.47f, 0.43f, 1),
                                       b.Optional ? ICON_FA_CIRCLE_MINUS " Missing" : ICON_FA_CIRCLE_XMARK " Missing (required)");
                ImGui::TableNextColumn();
                if (b.Status == 1 && ImGui::SmallButton("Create"))
                {
                    const char* t = CreateType(b.Type);
                    Entity e = UISystem::CreateElement(*canvas.GetScene(), t ? t : b.Type, canvas);
                    if (e)
                        e.GetComponent<TagComponent>().Tag = b.Name;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }
}
