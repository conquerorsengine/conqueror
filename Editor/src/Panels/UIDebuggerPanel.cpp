#include "UIDebuggerPanel.h"

#include "EditorState.h"
#include "Scene/Components.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace Conqueror::Editor
{
    namespace
    {
        const char* Kind(Entity e)
        {
            if (e.HasComponent<CanvasComponent>()) return "Canvas";
            if (e.HasComponent<UIButtonComponent>()) return "Button";
            if (e.HasComponent<UIToggleComponent>()) return "Toggle";
            if (e.HasComponent<UISliderComponent>()) return "Slider";
            if (e.HasComponent<UIScrollbarComponent>()) return "Scrollbar";
            if (e.HasComponent<UIInputFieldComponent>()) return "Input Field";
            if (e.HasComponent<UIDropdownComponent>()) return "Dropdown";
            if (e.HasComponent<UIScrollRectComponent>()) return "Scroll View";
            if (e.HasComponent<UITextComponent>()) return "Text";
            if (e.HasComponent<UIRawImageComponent>()) return "Raw Image";
            if (e.HasComponent<UIImageComponent>()) return "Image";
            return "Rect";
        }

        bool HoveredChain(Entity e)
        {
            if (const UISelectableState* s = UISystem::GetSelectableState(e); s && (s->Hovered || s->Pressed || s->Selected))
                return true;
            for (Entity c : e.GetChildren())
                if (HoveredChain(c))
                    return true;
            return false;
        }
    }

    void UIDebuggerPanel::CollectEvents()
    {
        for (const UIEvent& ev : UISystem::GetEvents())
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const double t = std::chrono::duration<double>(now).count();
            char buf[256];
            Entity source = ev.Source;
            const std::string src = source && source.HasComponent<TagComponent>() ? source.GetComponent<TagComponent>().Tag : std::string("?");
            std::snprintf(buf, sizeof(buf), "%6.2f  %-12s %s%s%s  (%.2f)", std::fmod(t, 1000.0), ev.Type.c_str(), src.c_str(),
                          ev.Name.empty() || ev.Name == src ? "" : "  -> ", ev.Name == src ? "" : ev.Name.c_str(), ev.Value);
            m_Events.push_front(buf);
            if (m_Events.size() > 60)
                m_Events.pop_back();
        }
    }

    void UIDebuggerPanel::DrawElement(Entity e, int depth)
    {
        if (m_OnlyHovered && !HoveredChain(e))
            return;
        const std::string& name = e.GetComponent<TagComponent>().Tag;
        const bool filtered = m_Filter[0] && name.find(m_Filter) == std::string::npos;
        ImGui::PushID((int)(uint32_t)(entt::entity)e);
        if (!filtered)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Indent((float)depth * 12.0f);
            const bool selected = EditorState::Get().IsEntitySelected(e);
            const bool active = e.GetScene()->IsEntityActive(e);
            ImGui::PushStyleColor(ImGuiCol_Text, active ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && OnSelect)
                OnSelect(e);
            ImGui::PopStyleColor();
            ImGui::Unindent((float)depth * 12.0f);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", Kind(e));
            ImGui::TableNextColumn();
            if (e.HasComponent<RectTransformComponent>())
            {
                const glm::vec4 r = e.GetComponent<RectTransformComponent>().ComputedRect;
                ImGui::Text("%.0f, %.0f  %.0f x %.0f", r.x, r.y, r.z, r.w);
            }
            ImGui::TableNextColumn();
            std::string state;
            if (!active) state += "inactive ";
            if (const UISelectableState* s = UISystem::GetSelectableState(e))
            {
                if (!s->Interactable || !s->GroupInteractable) state += "disabled ";
                if (s->Hovered) state += ICON_FA_ARROW_POINTER " hover ";
                if (s->Pressed) state += "pressed ";
                if (s->Selected) state += ICON_FA_GAMEPAD " selected ";
            }
            if (e.HasComponent<UIImageComponent>() && !e.GetComponent<UIImageComponent>().RaycastTarget)
                state += "no raycast ";
            ImGui::TextUnformatted(state.c_str());
        }
        ImGui::PopID();
        for (Entity c : e.GetChildren())
            if (c.HasComponent<RectTransformComponent>())
                DrawElement(c, depth + 1);
    }

    void UIDebuggerPanel::OnImGuiRender(bool* open)
    {
        if (!ImGui::Begin(ICON_FA_BUG " UI Debugger###UIDebugger", open))
        {
            ImGui::End();
            return;
        }
        if (!m_Context)
        {
            ImGui::End();
            return;
        }
        ImGui::Checkbox("Only hovered / selected", &m_OnlyHovered);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputTextWithHint("##uidbgfilter", ICON_FA_MAGNIFYING_GLASS " Filter", m_Filter, sizeof(m_Filter));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", UISystem::IsPointerOverUI() ? ICON_FA_ARROW_POINTER " pointer over UI" : "");
        const float eventsH = std::min(180.0f, ImGui::GetContentRegionAvail().y * 0.35f);
        if (ImGui::BeginTable("##uitree", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - eventsH)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Rect (x, y  w x h)", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (auto h : m_Context->m_Registry.view<CanvasComponent>())
                DrawElement(Entity{ h, m_Context.get() }, 0);
            ImGui::EndTable();
        }
        ImGui::SeparatorText("UI events");
        ImGui::BeginChild("##uievents");
        if (m_Events.empty())
            ImGui::TextDisabled("Clicks, value changes and submits appear here while the game plays.");
        for (const auto& line : m_Events)
            ImGui::TextUnformatted(line.c_str());
        ImGui::EndChild();
        ImGui::End();
    }
}
