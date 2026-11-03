#include "SignalReceiverInspector.h"

#include "Assets/AssetUI.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/Signals.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>

namespace Conqueror::Editor::SignalReceiverInspector
{
    void Draw(Entity entity)
    {
        auto& rc = entity.GetComponent<SignalReceiverComponent>();
        Scene* scene = entity.GetScene();
        int remove = -1;
        for (int i = 0; i < (int)rc.Reactions.size(); ++i)
        {
            auto& r = rc.Reactions[(size_t)i];
            ImGui::PushID(i);
            ImGui::Separator();
            ImGui::TextDisabled("Reaction %d", i + 1);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            UI::BeginProperty("Signal");
            AssetUI::AssetSlot("signal", "Signal", { SignalAssets::Extension }, r.Signal);
            UI::EndProperty();
            UI::BeginProperty("Target", "The object whose script method runs (Self = this object).");
            {
                Entity target = r.Target && scene ? scene->FindEntityByUUID(r.Target) : Entity{};
                const std::string preview = !r.Target ? std::string("Self") : (target ? target.GetComponent<TagComponent>().Tag : std::string("(missing)"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##target", preview.c_str()))
                {
                    if (ImGui::Selectable("Self", r.Target == 0))
                        r.Target = 0;
                    if (scene)
                        scene->m_Registry.view<IDComponent, TagComponent>().each([&](auto, IDComponent& id, TagComponent& tag) {
                            ImGui::PushID((int)(uint64_t)id.ID);
                            if (ImGui::Selectable(tag.Tag.c_str(), r.Target == (uint64_t)id.ID))
                                r.Target = (uint64_t)id.ID;
                            ImGui::PopID();
                        });
                    ImGui::EndCombo();
                }
            }
            UI::EndProperty();
            UI::BeginProperty("Method", "Script method called on the target.");
            ImGui::SetNextItemWidth(-1.0f);
            AssetUI::InputString("##method", r.Method);
            UI::EndProperty();
            UI::BeginProperty("Argument", "Optional text argument.");
            ImGui::SetNextItemWidth(-1.0f);
            AssetUI::InputString("##arg", r.Argument);
            UI::EndProperty();
            ImGui::PopID();
        }
        if (remove >= 0)
            rc.Reactions.erase(rc.Reactions.begin() + remove);
        ImGui::Spacing();
        if (UI::ButtonFullWidth(ICON_FA_PLUS "  Add Reaction", 24.0f))
            rc.Reactions.push_back({});
    }

    void DrawAsset(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_TOWER_BROADCAST);
        ImGui::SameLine();
        ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Signal");
        ImGui::Separator();
        ImGui::TextWrapped("Add it to a Timeline Signal track marker (Emit Signal) or send it from a script with SignalSend(entity, \"%s\"). "
                           "Objects with a Signal Receiver react to it.", AssetUI::ToProjectRelative(path).c_str());
    }
}
