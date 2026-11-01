#include "LogPanel.h"
#include "UI/EditorUI.h"
#include "Core/Project/Project.h"

#include <imgui.h>
#include <nfd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <filesystem>
#include <fstream>

namespace Conqueror::Editor
{
    namespace
    {
        bool ContainsCaseInsensitive(const std::string& haystack, const char* needle)
        {
            if (!needle || !needle[0])
                return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle, needle + strlen(needle),
                [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != haystack.end();
        }

        ImVec4 SeverityColor(LogSeverity s)
        {
            switch (s)
            {
                case LogSeverity::Trace:   return ImVec4(0.55f, 0.57f, 0.60f, 1.0f);
                case LogSeverity::Warning: return ImVec4(1.00f, 0.78f, 0.30f, 1.0f);
                case LogSeverity::Error:   return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);
                default:                   return ImVec4(0.86f, 0.87f, 0.89f, 1.0f);
            }
        }

        const char* SeverityGlyph(LogSeverity s)
        {
            switch (s)
            {
                case LogSeverity::Trace:   return "-";
                case LogSeverity::Warning: return "!";
                case LogSeverity::Error:   return "x";
                default:                   return "i";
            }
        }
    }

    void LogPanel::AddMessageFromLog(const std::string& formatted, int spdlogLevel)
    {
        LogEntry entry;
        // "zaman \x1f logger \x1f mesaj\n"
        std::string text = formatted;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        size_t a = text.find('\x1f');
        size_t b = a == std::string::npos ? std::string::npos : text.find('\x1f', a + 1);
        if (a != std::string::npos && b != std::string::npos)
        {
            entry.Time = text.substr(0, a);
            entry.Source = text.substr(a + 1, b - a - 1);
            entry.Text = text.substr(b + 1);
        }
        else
        {
            entry.Text = text;
        }

        // spdlog: 0 trace, 1 debug, 2 info, 3 warn, 4 err, 5 critical
        if (spdlogLevel >= 4) entry.Severity = LogSeverity::Error;
        else if (spdlogLevel == 3) entry.Severity = LogSeverity::Warning;
        else if (spdlogLevel <= 1) entry.Severity = LogSeverity::Trace;
        else entry.Severity = LogSeverity::Info;

        std::lock_guard<std::mutex> lock(m_PendingMutex);
        if (m_Pending.size() < 50000)
            m_Pending.push_back(std::move(entry));
    }

    void LogPanel::AddMessage(const std::string& text, LogSeverity severity, const std::string& source)
    {
        LogEntry entry;
        entry.Text = text;
        entry.Severity = severity;
        entry.Source = source;
        std::lock_guard<std::mutex> lock(m_PendingMutex);
        m_Pending.push_back(std::move(entry));
    }

    void LogPanel::Clear()
    {
        {
            std::lock_guard<std::mutex> lock(m_PendingMutex);
            m_Pending.clear();
        }
        m_Entries.clear();
        m_InfoCount = m_WarningCount = m_ErrorCount = 0;
        m_Selected = -1;
    }

    void LogPanel::OnPlay()
    {
        if (m_ClearOnPlay)
            Clear();
    }

    bool LogPanel::ConsumeErrorPauseRequest()
    {
        bool r = m_ErrorPauseRequested;
        m_ErrorPauseRequested = false;
        return r;
    }

    void LogPanel::DrainPending()
    {
        std::vector<LogEntry> incoming;
        {
            std::lock_guard<std::mutex> lock(m_PendingMutex);
            incoming.swap(m_Pending);
        }
        for (auto& e : incoming)
        {
            switch (e.Severity)
            {
                case LogSeverity::Warning: ++m_WarningCount; break;
                case LogSeverity::Error:
                    ++m_ErrorCount;
                    if (m_ErrorPause)
                        m_ErrorPauseRequested = true;
                    break;
                case LogSeverity::Info: ++m_InfoCount; break;
                default: break;
            }
            // Ardışık aynı mesajlar tek satırda sayılır
            if (!m_Entries.empty() && m_Entries.back().Text == e.Text && m_Entries.back().Severity == e.Severity)
            {
                m_Entries.back().Count++;
                m_Entries.back().Time = e.Time;
                continue;
            }
            m_Entries.push_back(std::move(e));
        }
        while (m_Entries.size() > kMaxEntries)
        {
            m_Entries.pop_front();
            if (m_Selected >= 0)
                --m_Selected;
        }
    }

    bool LogPanel::PassesFilter(const LogEntry& entry) const
    {
        switch (entry.Severity)
        {
            case LogSeverity::Trace: if (!m_ShowTrace) return false; break;
            case LogSeverity::Info: if (!m_ShowInfo) return false; break;
            case LogSeverity::Warning: if (!m_ShowWarnings) return false; break;
            case LogSeverity::Error: if (!m_ShowErrors) return false; break;
        }
        return ContainsCaseInsensitive(entry.Text, m_Search) || ContainsCaseInsensitive(entry.Source, m_Search);
    }

    std::string LogPanel::BuildClipboardText(bool filteredOnly) const
    {
        std::string out;
        for (const auto& e : m_Entries)
        {
            if (filteredOnly && !PassesFilter(e))
                continue;
            out += "[" + e.Time + "] [" + e.Source + "] " + e.Text;
            if (e.Count > 1)
                out += " (x" + std::to_string(e.Count) + ")";
            out += "\n";
        }
        return out;
    }

    void LogPanel::SaveToFile()
    {
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Log Files", "log,txt" } };
        std::string defaultDir = Project::GetActiveProjectDirectory().string();
        if (NFD_SaveDialogU8(&outPath, filters, 1, defaultDir.empty() ? nullptr : defaultDir.c_str(), "Editor.log") == NFD_OKAY)
        {
            std::ofstream out(outPath);
            out << BuildClipboardText(false);
            NFD_FreePathU8(outPath);
        }
    }

    void LogPanel::OnImGuiRender(bool* open)
    {
        DrainPending();

        if (!ImGui::Begin("Log", open))
        {
            ImGui::End();
            return;
        }

        // ---- Araç çubuğu
        if (ImGui::Button("Clear"))
            Clear();
        ImGui::SameLine();
        UI::ToggleButton("Collapse", m_Collapse);
        ImGui::SameLine();
        UI::ToggleButton("Clear on Play", m_ClearOnPlay);
        ImGui::SameLine();
        UI::ToggleButton("Error Pause", m_ErrorPause);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Pauses Play mode when an error is logged.");
        ImGui::SameLine();
        if (ImGui::Button("..."))
            ImGui::OpenPopup("##logoptions");
        if (ImGui::BeginPopup("##logoptions"))
        {
            ImGui::MenuItem("Auto Scroll", nullptr, &m_AutoScroll);
            ImGui::MenuItem("Show Timestamps", nullptr, &m_ShowTimestamps);
            ImGui::MenuItem("Show Trace/Debug", nullptr, &m_ShowTrace);
            ImGui::Separator();
            if (ImGui::MenuItem("Copy All"))
                ImGui::SetClipboardText(BuildClipboardText(false).c_str());
            if (ImGui::MenuItem("Copy Visible"))
                ImGui::SetClipboardText(BuildClipboardText(true).c_str());
            if (ImGui::MenuItem("Save to File..."))
                SaveToFile();
            ImGui::EndPopup();
        }

        // Sağ taraf: arama + seviye sayaçları
        float countersW = 64.0f * 3.0f + 4.0f + 8.0f;
        ImGui::SameLine();
        float searchW = std::max(80.0f, ImGui::GetContentRegionAvail().x - countersW);
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##search", "Search", m_Search, sizeof(m_Search));
        ImGui::SameLine();
        auto counter = [](const char* id, const char* glyph, int count, bool& toggle, const ImVec4& color)
        {
            char label[48];
            snprintf(label, sizeof(label), "%s %d%s", glyph, count > 999 ? 999 : count, count > 999 ? "+" : "");
            ImGui::PushID(id);
            if (!toggle)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, color);
            UI::ToggleButton(label, toggle, ImVec2(64.0f, 0.0f));
            ImGui::PopStyleColor();
            ImGui::PopID();
        };
        counter("info", "i", m_InfoCount, m_ShowInfo, SeverityColor(LogSeverity::Info));
        ImGui::SameLine(0, 2);
        counter("warn", "!", m_WarningCount, m_ShowWarnings, SeverityColor(LogSeverity::Warning));
        ImGui::SameLine(0, 2);
        counter("err", "x", m_ErrorCount, m_ShowErrors, SeverityColor(LogSeverity::Error));

        ImGui::Separator();

        // ---- Görünür satırlar (Collapse: aynı metinleri birleştir)
        std::vector<int> visible;
        visible.reserve(m_Entries.size());
        std::vector<int> collapsedCount;
        if (m_Collapse)
        {
            std::unordered_map<std::string, size_t> seen;
            for (int i = 0; i < (int)m_Entries.size(); ++i)
            {
                const auto& e = m_Entries[i];
                if (!PassesFilter(e))
                    continue;
                auto it = seen.find(e.Text);
                if (it == seen.end())
                {
                    seen[e.Text] = visible.size();
                    visible.push_back(i);
                    collapsedCount.push_back(e.Count);
                }
                else
                {
                    collapsedCount[it->second] += e.Count;
                }
            }
        }
        else
        {
            for (int i = 0; i < (int)m_Entries.size(); ++i)
                if (PassesFilter(m_Entries[i]))
                    visible.push_back(i);
        }

        // ---- Liste + detay bölmesi
        float detailH = (m_Selected >= 0 && m_Selected < (int)m_Entries.size()) ? m_DetailHeight : 0.0f;
        ImGui::BeginChild("##logscroll", ImVec2(0, -detailH), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size());
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                int index = visible[row];
                const auto& e = m_Entries[index];
                ImGui::PushID(index);
                ImVec4 color = SeverityColor(e.Severity);

                // Satır arka planı (zebra + seçim)
                ImVec2 p = ImGui::GetCursorScreenPos();
                float lineH = ImGui::GetTextLineHeightWithSpacing();
                float width = ImGui::GetContentRegionAvail().x;
                if (row % 2 == 1)
                    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + width + ImGui::GetScrollX(), p.y + lineH), IM_COL32(255, 255, 255, 8));

                bool selected = (m_Selected == index);
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, lineH - 1.0f)))
                    m_Selected = index;
                if (ImGui::BeginPopupContextItem("##rowctx"))
                {
                    if (ImGui::MenuItem("Copy"))
                        ImGui::SetClipboardText(e.Text.c_str());
                    if (ImGui::MenuItem("Copy With Time"))
                        ImGui::SetClipboardText(("[" + e.Time + "] " + e.Text).c_str());
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0, 0);
                ImGui::SetCursorScreenPos(p);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text(" %s ", SeverityGlyph(e.Severity));
                ImGui::SameLine(0, 2);
                if (m_ShowTimestamps && !e.Time.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.52f, 0.56f, 1.0f));
                    ImGui::Text("[%s]", e.Time.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 6);
                }
                ImGui::TextUnformatted(e.Text.c_str());
                int count = m_Collapse ? collapsedCount[row] : e.Count;
                if (count > 1)
                {
                    ImGui::SameLine();
                    UI::Badge(std::to_string(count).c_str(), color);
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        if (detailH > 0.0f)
        {
            // Sürüklenebilir ayırıcı
            ImGui::InvisibleButton("##splitter", ImVec2(-FLT_MIN, 4.0f));
            if (ImGui::IsItemActive())
                m_DetailHeight = std::clamp(m_DetailHeight - ImGui::GetIO().MouseDelta.y, 40.0f, 600.0f);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            const auto& e = m_Entries[m_Selected];
            ImGui::BeginChild("##logdetail", ImVec2(0, 0), ImGuiChildFlags_Borders);
            ImGui::TextDisabled("%s  %s", e.Time.c_str(), e.Source.c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(e.Severity));
            ImGui::TextUnformatted(e.Text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::End();
    }
}
