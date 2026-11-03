#include "InputActionsInspector.h"

#include "Assets/AssetUI.h"
#include "Core/Input/InputActionAssets.h"
#include "Core/Input/InputActionRuntime.h"
#include "Core/Project/ProjectSettings.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ProjectSettingsPanel.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>

namespace Conqueror::Editor::InputActionsInspector
{
    namespace fs = std::filesystem;

    void Draw(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_GAMEPAD);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Input Actions");
        ImGui::Separator();

        if (UI::ButtonFullWidth(ICON_FA_PEN_TO_SQUARE "  Edit Asset", 28.0f) && InspectorPanel::s_OpenInputActions)
            InspectorPanel::s_OpenInputActions(path);

        // Project-wide Actions (Unity 6: Project Settings > Input System Package > Project-wide Actions)
        const std::string relative = AssetUI::ToProjectRelative(path);
        const std::string& projectWide = ProjectSettings::ProjectWideActions();
        const bool isProjectWide = !projectWide.empty() && AssetUI::Resolve(projectWide) == AssetUI::Resolve(relative);
        if (isProjectWide)
            UI::InfoBox("This is the Project-wide Actions asset: InputGetAction, InputActionVector and the other global action queries read it.",
                        UI::InfoType::Success);
        else if (UI::ButtonFullWidth(ICON_FA_GLOBE "  Assign as the Project-wide Input Actions"))
        {
            ProjectSettings::ProjectWideActions() = relative;
            ProjectSettings::Apply();
            ProjectSettingsPanel::SaveProjectFile();
        }

        const InputActionAssetData* data = InputActionAssets::Get(path);
        if (!data)
        {
            UI::InfoBox("The file could not be read.", UI::InfoType::Error);
            return;
        }

        UI::SubHeader("Control Schemes");
        if (data->Schemes.empty())
            ImGui::TextDisabled("None (every binding is used with every device).");
        for (const auto& s : data->Schemes)
        {
            std::string devices;
            for (size_t i = 0; i < s.Devices.size(); ++i)
                devices += (i ? ", " : "") + s.Devices[i] + (i < s.Optional.size() && s.Optional[i] ? " (optional)" : "");
            ImGui::BulletText("%s  -  %s", s.Name.c_str(), devices.c_str());
        }

        UI::SubHeader("Action Maps");
        for (const auto& map : data->Maps)
        {
            ImGui::PushID(map.Name.c_str());
            if (ImGui::TreeNodeEx("##map", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s  %s  (%zu actions)",
                                  ICON_FA_LAYER_GROUP, map.Name.c_str(), map.Actions.size()))
            {
                if (ImGui::BeginTable("##actions", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Action");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Bindings");
                    ImGui::TableSetupColumn("Interactions");
                    ImGui::TableHeadersRow();
                    for (const auto& a : map.Actions)
                    {
                        int bindings = 0;
                        for (const auto& b : a.Bindings)
                            bindings += b.IsPart() ? 0 : 1;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(a.Name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled("%s / %s", InputActionAssets::KindName(a.Kind), InputActionAssets::ControlName(a.Control));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%d", bindings);
                        if (ImGui::IsItemHovered() && !a.Bindings.empty())
                        {
                            ImGui::BeginTooltip();
                            for (const auto& b : a.Bindings)
                                ImGui::TextUnformatted(((b.IsPart() ? "    " : "") + InputActionAssets::DisplayName(b)).c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextDisabled("%s", a.Interactions.empty() ? "-" : a.Interactions.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Assign this asset to a Player Input component's Actions slot: its scripts receive On<Action>(value) "
                           "(or On<Action>(x, y) for 2D actions) and can poll PlayerInputVector / PlayerInputPhase / PlayerInputPerformed.");
    }
}
