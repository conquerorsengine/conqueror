#include "OcclusionPanel.h"
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

    void OcclusionPanel::OnImGuiRender(bool* open)
    {
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
        if (UI::BeginSection("Bake"))
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
            UI::EndSection();
        }
        if (UI::BeginSection("Visualization"))
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
            UI::EndSection();
        }
        ImGui::End();
    }
}
