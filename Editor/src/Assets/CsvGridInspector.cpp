#include "CsvGridInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Conqueror::Editor::CsvGridInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        using Grid = std::vector<std::vector<std::string>>;

        Grid Parse(const std::string& text)
        {
            Grid grid;
            std::vector<std::string> row;
            std::string cell;
            bool quoted = false;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quoted)
                {
                    if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                    else if (c == '"') quoted = false;
                    else cell += c;
                }
                else if (c == '"') quoted = true;
                else if (c == ',') { row.push_back(cell); cell.clear(); }
                else if (c == '\n')
                {
                    row.push_back(cell);
                    cell.clear();
                    grid.push_back(row);
                    row.clear();
                }
                else if (c != '\r') cell += c;
            }
            if (!cell.empty() || !row.empty())
            {
                row.push_back(cell);
                grid.push_back(row);
            }
            return grid;
        }

        std::string Write(const Grid& grid)
        {
            std::string out;
            for (const auto& row : grid)
            {
                for (size_t c = 0; c < row.size(); ++c)
                {
                    if (c) out += ',';
                    if (row[c].find_first_of(",\"\n") != std::string::npos)
                    {
                        out += '"';
                        for (char ch : row[c]) { if (ch == '"') out += '"'; out += ch; }
                        out += '"';
                    }
                    else
                        out += row[c];
                }
                out += '\n';
            }
            return out;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        struct EditSession { std::string Path, Before; bool Open = false; } s_Edit;

        void Commit(bool force)
        {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after), "Edit " + fs::path(s_Edit.Path).filename().string()));
        }
    }

    bool IsGrid(const std::string& path)
    {
        const std::string e = Lower(fs::path(path).extension().string());
        return e == ".cqdatatable" || e == ".cqcurvetable";
    }

    void Draw(const std::string& path)
    {
        const bool curve = Lower(fs::path(path).extension().string()) == ".cqcurvetable";
        static std::string s_Path;
        static fs::file_time_type s_Time;
        static Grid s_Grid;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            std::ifstream in(path, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            s_Grid = Parse(ss.str());
        }
        if (s_Edit.Open && s_Edit.Path != path)
            Commit(true);
        ImGui::TextColored(UI::Colors::Accent, "%s", curve ? ICON_FA_CHART_LINE : ICON_FA_TABLE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(curve ? "Curve Table" : "Data Table");
        ImGui::Separator();
        Grid& grid = s_Grid;
        if (grid.empty())
            grid.push_back({ "Name" });
        size_t columns = 0;
        for (const auto& r : grid)
            columns = std::max(columns, r.size());
        for (auto& r : grid)
            r.resize(columns);
        bool changed = false;
        if (ImGui::SmallButton(ICON_FA_PLUS "  Add Row"))
        {
            std::vector<std::string> row(columns);
            row[0] = "Row" + std::to_string(grid.size());
            grid.push_back(std::move(row));
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_PLUS "  Add Column"))
        {
            for (size_t r = 0; r < grid.size(); ++r)
                grid[r].push_back(r == 0 ? (curve ? std::to_string(columns - 1) : "Column" + std::to_string(columns)) : "");
            ++columns;
            changed = true;
        }
        if (ImGui::BeginTable("##grid", (int)columns + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, std::min(420.0f, ImGui::GetFrameHeightWithSpacing() * (grid.size() + 2.5f)))))
        {
            int removeRow = -1, removeColumn = -1;
            for (size_t r = 0; r < grid.size(); ++r)
            {
                ImGui::PushID((int)r);
                ImGui::TableNextRow();
                for (size_t c = 0; c < columns; ++c)
                {
                    ImGui::TableSetColumnIndex((int)c);
                    ImGui::PushID((int)c);
                    ImGui::SetNextItemWidth(c == 0 ? 130.0f : 90.0f);
                    changed |= AssetUI::InputString("##cell", grid[r][c]);
                    if (r == 0 && c > 0 && ImGui::BeginPopupContextItem("##col"))
                    {
                        if (ImGui::MenuItem("Delete Column"))
                            removeColumn = (int)c;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::TableSetColumnIndex((int)columns);
                if (r > 0 && ImGui::SmallButton(ICON_FA_XMARK))
                    removeRow = (int)r;
                ImGui::PopID();
            }
            if (removeRow > 0)
            {
                grid.erase(grid.begin() + removeRow);
                changed = true;
            }
            if (removeColumn > 0)
            {
                for (auto& r : grid)
                    r.erase(r.begin() + removeColumn);
                changed = true;
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(curve ? "Row names on the left, key times in the header. Scripts read values with CurveTableEvaluate(path, row, time). Right-click a header to delete its column."
                                 : "The first column holds the row keys. Scripts read cells with DataTableGetValue(path, row, column). Right-click a header to delete its column.");
        ImGui::PopStyleColor();
        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            std::ofstream(path, std::ios::binary | std::ios::trunc) << Write(grid);
            s_Time = fs::last_write_time(path, ec);
        }
        Commit(false);
    }
}
