#include "GodotNodeInspectors.h"

#include "Assets/AssetUI.h"
#include "Core/Assets/SimpleAsset.h"
#include "Scene/GodotNodes.h"
#include "Scene/StatusIndicator.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Conqueror::Editor::GodotNodeInspectors
{
    void DrawResourcePreloader(Entity entity)
    {
        auto& p = entity.GetComponent<ResourcePreloaderComponent>();
        static const std::vector<std::string> anyAsset = {};
        UI::SubHeader("Resources");
        int remove = -1;
        for (int i = 0; i < (int)p.Resources.size(); ++i)
        {
            auto& r = p.Resources[(size_t)i];
            ImGui::PushID(i);
            UI::InputText("Name", r.Name, "Scripts read the path with PreloaderGetResource(\"Name\").");
            UI::BeginProperty("Asset");
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::SameLine();
            if (AssetUI::AssetSlot("asset", "Asset", anyAsset, r.Path) && r.Name.empty())
                r.Name = std::filesystem::path(r.Path).stem().string();
            UI::EndProperty();
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0)
            p.Resources.erase(p.Resources.begin() + remove);
        std::string add;
        UI::BeginProperty("Add Resource", "Drop or pick an asset; its file name becomes the name.");
        if (AssetUI::AssetSlot("add", "Asset", anyAsset, add) && !add.empty())
            p.Resources.push_back({ std::filesystem::path(add).stem().string(), add });
        UI::EndProperty();
    }

    void DrawShaderGlobalsOverride(Entity entity)
    {
        auto& c = entity.GetComponent<ShaderGlobalsOverrideComponent>();
        if (c.Collection.empty())
        {
            UI::InfoBox("Pick a Material Parameter Collection (.cqmpc): its scalars and vectors are the global shader parameters.", UI::InfoType::Info);
            return;
        }
        // Koleksiyonun parametreleri (ad, vektör mü, varsayılan)
        struct Param { std::string Name; bool Vector; glm::vec4 Default; };
        std::vector<Param> params;
        SimpleAssets::Data data;
        if (SimpleAssets::Load(c.Collection, data))
        {
            for (const auto& row : data.Table("Scalars"))
                params.push_back({ SimpleAssets::Cell(row, 0), false, glm::vec4(SimpleAssets::CellFloat(row, 1)) });
            for (const auto& row : data.Table("Vectors"))
                params.push_back({ SimpleAssets::Cell(row, 0), true, glm::vec4(SimpleAssets::CellFloat(row, 1), SimpleAssets::CellFloat(row, 2),
                                                                               SimpleAssets::CellFloat(row, 3), SimpleAssets::CellFloat(row, 4, 1.0f)) });
        }
        UI::SubHeader("Overrides");
        int remove = -1;
        for (int i = 0; i < (int)c.Entries.size(); ++i)
        {
            auto& e = c.Entries[(size_t)i];
            ImGui::PushID(i);
            UI::BeginProperty("Parameter");
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##param", e.Name.empty() ? "(choose)" : e.Name.c_str()))
            {
                for (const auto& prm : params)
                    if (ImGui::Selectable(prm.Name.c_str(), prm.Name == e.Name))
                    {
                        e.Name = prm.Name;
                        e.IsVector = prm.Vector;
                        e.Value = prm.Default;
                    }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            if (e.IsVector)
            {
                UI::BeginProperty("Value");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat4("##v", &e.Value.x, 0.01f);
                UI::EndProperty();
            }
            else
                UI::DragFloat("Value", e.Value.x, 0.01f);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0)
            c.Entries.erase(c.Entries.begin() + remove);
        if (ImGui::Button(ICON_FA_PLUS "  Add Override") )
        {
            ShaderGlobalsOverrideComponent::Entry e;
            for (const auto& prm : params)
            {
                bool used = false;
                for (const auto& x : c.Entries) used |= x.Name == prm.Name;
                if (!used) { e.Name = prm.Name; e.IsVector = prm.Vector; e.Value = prm.Default; break; }
            }
            c.Entries.push_back(e);
        }
        if (params.empty())
            ImGui::TextDisabled("The collection has no parameters yet.");
    }

    void DrawStatusIndicator(Entity entity)
    {
        auto& c = entity.GetComponent<StatusIndicatorComponent>();
        UI::SubHeader("Menu");
        int remove = -1;
        for (int i = 0; i < (int)c.MenuItems.size(); ++i)
        {
            ImGui::PushID(i);
            char label[24];
            snprintf(label, sizeof(label), "Item %d", i);
            UI::BeginProperty(label, "\"-\" is a separator. Scripts get the index in OnStatusMenuItemPressed(index).");
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            char buf[256];
            snprintf(buf, sizeof(buf), "%s", c.MenuItems[(size_t)i].c_str());
            if (ImGui::InputText("##item", buf, sizeof(buf)))
                c.MenuItems[(size_t)i] = buf;
            UI::EndProperty();
            ImGui::PopID();
        }
        if (remove >= 0)
            c.MenuItems.erase(c.MenuItems.begin() + remove);
        if (ImGui::Button(ICON_FA_PLUS "  Add Menu Item"))
            c.MenuItems.push_back("Item " + std::to_string(c.MenuItems.size()));
        if (!StatusIndicators::Supported())
            UI::InfoBox("Tray icons need libdbus-1 on Linux / BSD (or Windows); this system has neither.", UI::InfoType::Warning);
    }
}
