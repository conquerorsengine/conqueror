#include "OcclusionPanel.h"
#include "EditorState.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

namespace Conqueror::Editor
{
    bool OcclusionPanel::StartBake(std::string& error)
    {
        if (!m_Context)
        {
            error = "No scene.";
            return false;
        }
        if (!m_Baker.Begin(*m_Context, m_Settings, error))
        {
            m_Status = error;
            return false;
        }
        m_BakeScene = m_Context;
        m_Status = "Baking...";
        return true;
    }

    void OcclusionPanel::Clear()
    {
        m_Baker.Cancel();
        if (m_Context)
            m_Context->SetOcclusionData(nullptr);
        m_Status = "Occlusion data cleared.";
    }

    void OcclusionPanel::Tick()
    {
        if (!m_Baker.IsRunning())
            return;
        // Sahne değiştiyse (açma / oynatma) pişirme iptal
        if (m_BakeScene != m_Context)
        {
            m_Baker.Cancel();
            m_Status = "Bake cancelled: the scene changed.";
            return;
        }
        if (m_Baker.Step(30.0f))
        {
            if (auto result = m_Baker.Result())
            {
                m_Context->SetOcclusionData(result);
                m_Status = "Baked " + std::to_string(result->Cells.size()) + " cells in " + std::to_string((int)(result->BakeSeconds * 10.0f) / 10.0f).substr(0, 5) +
                           " s. Save the scene to keep it.";
            }
        }
    }

    void OcclusionPanel::DrawObjectTab()
    {
        auto& reg = m_Context->m_Registry;
        // Sahne süzgeci (Unity "Scene Filter"): listedeki nesneye tıklamak onu seçer
        ImGui::TextDisabled("Scene Filter");
        ImGui::SameLine();
        ImGui::RadioButton("All", &m_ObjectFilter, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Renderers", &m_ObjectFilter, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Occlusion Areas", &m_ObjectFilter, 2);
        Entity selected = EditorState::Get().GetSelectedEntity();
        if (ImGui::BeginChild("##OcclusionObjects", ImVec2(0.0f, 150.0f), ImGuiChildFlags_Borders))
        {
            int shown = 0;
            for (auto e : reg.view<TagComponent>())
            {
                const bool renderer = reg.all_of<MeshRendererComponent>(e);
                const bool area = reg.all_of<OcclusionAreaComponent>(e) || reg.all_of<OcclusionPortalComponent>(e);
                if ((m_ObjectFilter == 1 && !renderer) || (m_ObjectFilter == 2 && !area) || (m_ObjectFilter == 0 && !renderer && !area))
                    continue;
                const auto& tag = reg.get<TagComponent>(e);
                std::string label = std::string(area ? ICON_FA_CUBE "  " : ICON_FA_SHAPES "  ") + tag.Tag;
                if (renderer && tag.IsStatic)
                {
                    const bool occluder = (tag.StaticFlags & TagComponent::kOccluderStatic) != 0;
                    const bool occludee = (tag.StaticFlags & TagComponent::kOccludeeStatic) != 0;
                    label += occluder && occludee ? "   (Occluder, Occludee)" : occluder ? "   (Occluder)" : occludee ? "   (Occludee)" : "   (Static)";
                }
                ImGui::PushID((int)(uint32_t)e);
                if (ImGui::Selectable(label.c_str(), selected && (entt::entity)selected == e))
                    EditorState::Get().SelectEntity(Entity{ e, m_Context });
                ImGui::PopID();
                ++shown;
            }
            if (shown == 0)
                ImGui::TextDisabled(m_ObjectFilter == 2 ? "No Occlusion Areas in the scene." : "No objects.");
        }
        ImGui::EndChild();
        ImGui::Spacing();

        selected = EditorState::Get().GetSelectedEntity();
        if (selected && selected.GetScene() == m_Context && selected.HasComponent<MeshRendererComponent>() && selected.HasComponent<TagComponent>())
        {
            auto& tag = selected.GetComponent<TagComponent>();
            UI::SubHeader(tag.Tag.c_str());
            UI::Checkbox("Static", tag.IsStatic, "Only static objects take part in occlusion culling.");
            ImGui::BeginDisabled(!tag.IsStatic);
            bool occluder = (tag.StaticFlags & TagComponent::kOccluderStatic) != 0;
            bool occludee = (tag.StaticFlags & TagComponent::kOccludeeStatic) != 0;
            if (UI::Checkbox("Occluder Static", occluder, "This object hides the objects behind it. Turn off for small or see-through objects."))
                tag.StaticFlags = occluder ? (tag.StaticFlags | TagComponent::kOccluderStatic) : (tag.StaticFlags & ~TagComponent::kOccluderStatic);
            if (UI::Checkbox("Occludee Static", occludee, "This object is not drawn while other objects hide it. Turn off for large objects that are always seen."))
                tag.StaticFlags = occludee ? (tag.StaticFlags | TagComponent::kOccludeeStatic) : (tag.StaticFlags & ~TagComponent::kOccludeeStatic);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Bake again after changing these.");
        }
        else if (selected && selected.GetScene() == m_Context && selected.HasComponent<OcclusionAreaComponent>())
        {
            auto& a = selected.GetComponent<OcclusionAreaComponent>();
            UI::SubHeader(selected.GetComponent<TagComponent>().Tag.c_str());
            UI::Vec3("Center", a.Center);
            UI::Vec3("Size", a.Size, 0.05f, 1.0f);
            UI::Checkbox("Is View Volume", a.IsViewVolume, "The camera can be inside this volume: view cells are baked here.");
        }
        else if (selected && selected.GetScene() == m_Context && selected.HasComponent<OcclusionPortalComponent>())
        {
            auto& p = selected.GetComponent<OcclusionPortalComponent>();
            UI::SubHeader(selected.GetComponent<TagComponent>().Tag.c_str());
            UI::Checkbox("Open", p.Open, "A closed portal (shut door) hides what is behind it.");
            UI::Vec3("Center", p.Center);
            UI::Vec3("Size", p.Size, 0.05f, 1.0f);
        }
        else
            ImGui::TextWrapped("Select a Mesh Renderer, an Occlusion Area or an Occlusion Portal in the scene.");
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_PLUS " Create New Occlusion Area", ImVec2(-FLT_MIN, 0.0f)))
        {
            Entity e = m_Context->CreateEntity("Occlusion Area");
            e.AddComponent<OcclusionAreaComponent>();
            EditorState::Get().SelectEntity(e);
        }
    }

    void OcclusionPanel::OnImGuiRender(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_EYE_SLASH " Occlusion Culling###OcclusionCulling", open))
        {
            ImGui::End();
            return;
        }
        if (!m_Context)
        {
            ImGui::TextDisabled("No scene.");
            ImGui::End();
            return;
        }
        UI::InfoBox("Static objects hidden behind other static objects are not drawn. Mark walls and props as Static, add Occlusion Areas "
                    "where the camera can go (optional) and Occlusion Portals on doors, then Bake.", UI::InfoType::Info);
        // Unity Occlusion Culling penceresi gibi üç sekme: Object (seçili nesnenin Occluder / Occludee Static ayarı, Occlusion Area'lar),
        // Bake (pişirme ayarları), Visualization (görselleştirme ve istatistik)
        if (ImGui::BeginTabBar("##OcclusionTabs"))
        {
            if (ImGui::BeginTabItem("Object"))
            {
                DrawObjectTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bake"))
            {
                UI::DragFloat("Cell Size", m_Settings.CellSize, 0.05f, 0.25f, 100.0f, "%.2f m");
                int res = m_Settings.Resolution;
                const char* resNames[] = { "32", "64", "128", "256" };
                const int resValues[] = { 32, 64, 128, 256 };
                int resIndex = 1;
                for (int i = 0; i < 4; ++i) if (resValues[i] == res) resIndex = i;
                if (UI::Combo("Precision", resIndex, resNames, 4, "Pixels per view direction: higher finds smaller gaps but bakes slower."))
                    m_Settings.Resolution = resValues[resIndex];
                const char* sampleNames[] = { "1 (center)", "5", "9" };
                const int sampleValues[] = { 1, 5, 9 };
                int si = m_Settings.SamplesPerCell >= 9 ? 2 : m_Settings.SamplesPerCell >= 5 ? 1 : 0;
                if (UI::Combo("Samples Per Cell", si, sampleNames, 3, "Viewpoints per cell. More = fewer objects popping in."))
                    m_Settings.SamplesPerCell = sampleValues[si];
                ImGui::Spacing();
                if (m_Baker.IsRunning())
                {
                    ImGui::ProgressBar(m_Baker.Progress(), ImVec2(-FLT_MIN, 0));
                    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0)))
                    {
                        m_Baker.Cancel();
                        m_Status = "Bake cancelled.";
                    }
                }
                else
                {
                    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    if (ImGui::Button("Clear", ImVec2(w, 0)))
                        Clear();
                    ImGui::SameLine();
                    if (UI::AccentButton("Bake", ImVec2(w, 0)))
                    {
                        std::string err;
                        StartBake(err);
                    }
                }
                if (!m_Status.empty())
                    ImGui::TextWrapped("%s", m_Status.c_str());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visualization"))
            {
                bool vis = m_Context->GetOcclusionVisualize();
                if (UI::Checkbox("Cull In Scene View", vis, "Hide occluded objects in the Scene view as seen from the scene camera, and draw the cells."))
                    m_Context->SetOcclusionVisualize(vis);
                if (auto data = m_Context->GetOcclusionData())
                {
                    UI::Text("Cells", "%d", (int)data->Cells.size());
                    UI::Text("Static Objects", "%d", (int)data->Renderers.size());
                    UI::Text("Portals", "%d", (int)data->Portals.size());
                    UI::Text("Hidden Now", "%d", (int)m_Context->GetOcclusionHiddenCount());
                }
                else
                    ImGui::TextDisabled("Not baked.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}
