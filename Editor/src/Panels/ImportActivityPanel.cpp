#include "ImportActivityPanel.h"

#include "UI/EditorUI.h"

#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/ImportActivity.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Conqueror::Editor
{
    namespace
    {
        bool ContainsNoCase(const std::string& a, const char* b)
        {
            std::string x = a, y = b;
            for (char& c : x)
                c = (char)std::tolower((unsigned char)c);
            for (char& c : y)
                c = (char)std::tolower((unsigned char)c);
            return x.find(y) != std::string::npos;
        }
    }

    void ImportActivityPanel::SetFilter(const std::string& filter)
    {
        std::snprintf(m_Filter, sizeof(m_Filter), "%s", filter.c_str());
    }

    void ImportActivityPanel::OnImGuiRender(bool* open)
    {
        if (!*open)
            return;
        ImGui::SetNextWindowSize(ImVec2(820.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_CLOCK_ROTATE_LEFT "  Import Activity###ImportActivity", open))
        {
            ImGui::End();
            return;
        }
        const auto& records = ImportActivity::Records();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##iaFilter", ICON_FA_MAGNIFYING_GLASS "  Filter by path or importer", m_Filter, sizeof(m_Filter));
        ImGui::SameLine();
        ImGui::Checkbox("Failed only", &m_OnlyFailed);
        ImGui::SameLine();
        ImGui::Checkbox("Slowest first", &m_SortBySlowest);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Clear"))
            ImportActivity::Clear();

        std::vector<const ImportActivity::Record*> rows;
        double total = 0.0;
        for (const auto& r : records)
        {
            if (m_OnlyFailed && r.Ok)
                continue;
            if (m_Filter[0] && !ContainsNoCase(r.Path, m_Filter) && !ContainsNoCase(r.Importer, m_Filter))
                continue;
            rows.push_back(&r);
            total += r.Milliseconds;
        }
        if (m_SortBySlowest)
            std::stable_sort(rows.begin(), rows.end(), [](auto* a, auto* b) { return a->Milliseconds > b->Milliseconds; });
        else
            std::reverse(rows.begin(), rows.end());   // en yeni üstte
        ImGui::TextDisabled("%d import(s), %.1f ms in total", (int)rows.size(), total);

        if (ImGui::BeginTable("##ia", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Importer", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto& r = *rows[(size_t)i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%.1f s", r.Time);
                    ImGui::TableNextColumn();
                    // Proje içindeki dosyalar proje göreli (Assets/...), motor kaynakları olduğu gibi
                    const std::string shown = SceneSerializer::ToAssetPath(r.Path);
                    ImGui::TextUnformatted(shown.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", r.Path.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.Importer.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f ms", r.Milliseconds);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.Reimport ? "Reimport" : "Import");
                    ImGui::TableNextColumn();
                    if (r.Ok)
                        ImGui::TextColored(UI::Colors::Success, ICON_FA_CHECK " %s", r.Info.c_str());
                    else
                        ImGui::TextColored(UI::Colors::Error, ICON_FA_XMARK " %s", r.Info.c_str());
                }
            ImGui::EndTable();
        }
        ImGui::End();
    }
}
