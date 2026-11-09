#include "SimpleAssetInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace Conqueror::Editor::SimpleAssetInspector
{
    namespace fs = std::filesystem;
    using namespace SimpleAssets;

    namespace
    {
        std::unordered_map<std::string, Extra>& Extras()
        {
            static std::unordered_map<std::string, Extra> e;
            return e;
        }

        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;

        void CommitEdit(bool force)
        {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(
                    s_Edit.Path, s_Edit.Before, std::move(after), "Edit " + fs::path(s_Edit.Path).filename().string()));
        }

        std::string FloatText(float v)
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%g", v);
            return buf;
        }

        std::vector<float> Floats(const std::string& text, size_t count, float def)
        {
            std::vector<float> v(count, def);
            std::stringstream ss(text);
            std::string part;
            for (size_t i = 0; i < count && std::getline(ss, part, ' '); ++i)
                v[i] = (float)std::atof(part.c_str());
            return v;
        }

        std::string Join(const float* v, size_t count)
        {
            std::string s;
            for (size_t i = 0; i < count; ++i)
                s += (i ? " " : "") + FloatText(v[i]);
            return s;
        }

        bool DrawCell(const Column& column, std::string& value, float width)
        {
            ImGui::SetNextItemWidth(width);
            switch (column.Type)
            {
                case FieldType::Float:
                {
                    float v = (float)std::atof(value.c_str());
                    if (ImGui::DragFloat("##c", &v, 0.01f, 0.0f, 0.0f, "%.3g"))
                    {
                        value = FloatText(v);
                        return true;
                    }
                    return false;
                }
                case FieldType::Bool:
                {
                    bool v = value == "true" || value == "1";
                    if (ImGui::Checkbox("##c", &v))
                    {
                        value = v ? "true" : "false";
                        return true;
                    }
                    return false;
                }
                case FieldType::Choice:
                {
                    bool changed = false;
                    if (ImGui::BeginCombo("##c", value.c_str()))
                    {
                        for (const auto& o : column.Options)
                            if (ImGui::Selectable(o.c_str(), o == value))
                            {
                                value = o;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    return changed;
                }
                case FieldType::Asset:
                {
                    ImGui::BeginGroup();
                    bool changed = AssetUI::AssetSlot("##a", column.Name, column.Options, value);
                    ImGui::EndGroup();
                    return changed;
                }
                default:
                    return AssetUI::InputString("##c", value);
            }
        }
    }

    bool IsSimpleAsset(const std::string& path)
    {
        return ForExtension(fs::path(path).extension().string()) != nullptr;
    }

    void RegisterExtra(const std::string& kind, Extra extra)
    {
        Extras()[kind] = std::move(extra);
    }

    bool CreateDefault(const Def& def, const std::string& path)
    {
        return Save(path, def, Defaults(def));
    }

    void Draw(const std::string& path)
    {
        const Def* def = ForExtension(fs::path(path).extension().string());
        if (!def)
            return;
        static std::string s_Path;
        static fs::file_time_type s_Time;
        static Data s_Data;
        static bool s_Ok = false;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Ok = Load(path, s_Data);
        }
        ImGui::TextColored(UI::Colors::Accent, "%s", def->Icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", def->Label.c_str());
        ImGui::Separator();
        if (!s_Ok)
        {
            UI::InfoBox("The file could not be read.", UI::InfoType::Error);
            return;
        }
        if (s_Edit.Open && s_Edit.Path != path)
            CommitEdit(true);
        bool changed = false;
        Data& data = s_Data;
        for (const auto& f : def->Fields)
        {
            ImGui::PushID(f.Name.c_str());
            const char* tip = f.Tooltip.empty() ? nullptr : f.Tooltip.c_str();
            switch (f.Type)
            {
                case FieldType::Float:
                {
                    float v = data.Float(f.Name);
                    const bool edited = f.Min < f.Max ? UI::SliderFloat(f.Name.c_str(), v, f.Min, f.Max, "%.3g", tip)
                                                      : UI::DragFloat(f.Name.c_str(), v, 0.01f, 0.0f, 0.0f, "%.3g", tip);
                    if (edited)
                    {
                        data.Values[f.Name] = FloatText(v);
                        changed = true;
                    }
                    break;
                }
                case FieldType::Int:
                {
                    int v = data.Int(f.Name);
                    const bool edited = f.Min < f.Max ? UI::SliderInt(f.Name.c_str(), v, (int)f.Min, (int)f.Max, tip) : UI::DragInt(f.Name.c_str(), v, 1.0f, 0, 0, tip);
                    if (edited)
                    {
                        data.Values[f.Name] = std::to_string(v);
                        changed = true;
                    }
                    break;
                }
                case FieldType::Bool:
                {
                    bool v = data.Bool(f.Name);
                    if (UI::Checkbox(f.Name.c_str(), v, tip))
                    {
                        data.Values[f.Name] = v ? "true" : "false";
                        changed = true;
                    }
                    break;
                }
                case FieldType::Text:
                {
                    std::string v = data.Str(f.Name);
                    if (UI::InputText(f.Name.c_str(), v, tip))
                    {
                        data.Values[f.Name] = v;
                        changed = true;
                    }
                    break;
                }
                case FieldType::Multiline:
                {
                    UI::SubHeader(f.Name.c_str());
                    std::string v = data.Str(f.Name);
                    if (AssetUI::InputMultiline("##m", v, ImVec2(-1.0f, 110.0f)))
                    {
                        data.Values[f.Name] = v;
                        changed = true;
                    }
                    break;
                }
                case FieldType::Color:
                {
                    const auto v = Floats(data.Str(f.Name, f.Default), 4, 1.0f);
                    float c[4] = { v[0], v[1], v[2], v[3] };
                    UI::BeginProperty(f.Name.c_str(), tip);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::ColorEdit4("##col", c, ImGuiColorEditFlags_Float))
                    {
                        data.Values[f.Name] = Join(c, 4);
                        changed = true;
                    }
                    UI::EndProperty();
                    break;
                }
                case FieldType::Vec3:
                {
                    const auto v = Floats(data.Str(f.Name, f.Default), 3, 0.0f);
                    float c[3] = { v[0], v[1], v[2] };
                    UI::BeginProperty(f.Name.c_str(), tip);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat3("##v", c, 0.01f))
                    {
                        data.Values[f.Name] = Join(c, 3);
                        changed = true;
                    }
                    UI::EndProperty();
                    break;
                }
                case FieldType::Choice:
                {
                    const std::string current = data.Str(f.Name, f.Default);
                    UI::BeginProperty(f.Name.c_str(), tip);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##e", current.c_str()))
                    {
                        for (const auto& o : f.Options)
                            if (ImGui::Selectable(o.c_str(), o == current))
                            {
                                data.Values[f.Name] = o;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    UI::EndProperty();
                    break;
                }
                case FieldType::Asset:
                {
                    std::string v = data.Str(f.Name);
                    UI::BeginProperty(f.Name.c_str(), tip);
                    if (AssetUI::AssetSlot("##a", f.Name, f.Options, v))
                    {
                        data.Values[f.Name] = v;
                        changed = true;
                    }
                    UI::EndProperty();
                    break;
                }
                case FieldType::Table:
                {
                    UI::SubHeader(f.Name.c_str());
                    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("%s", tip);
                    auto& rows = data.Tables[f.Name];
                    const float deleteWidth = ImGui::GetFrameHeight();
                    const size_t columns = std::max<size_t>(f.Columns.size(), 1);
                    if (!rows.empty() && ImGui::BeginTable("##t", (int)columns + 1, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerH))
                    {
                        for (const auto& c : f.Columns)
                            ImGui::TableSetupColumn(c.Name.c_str());
                        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, deleteWidth);
                        ImGui::TableHeadersRow();
                        int remove = -1;
                        for (size_t r = 0; r < rows.size(); ++r)
                        {
                            ImGui::PushID((int)r);
                            ImGui::TableNextRow();
                            rows[r].resize(columns);
                            for (size_t c = 0; c < f.Columns.size(); ++c)
                            {
                                ImGui::TableSetColumnIndex((int)c);
                                ImGui::PushID((int)c);
                                changed |= DrawCell(f.Columns[c], rows[r][c], -1.0f);
                                ImGui::PopID();
                            }
                            ImGui::TableSetColumnIndex((int)f.Columns.size());
                            if (ImGui::SmallButton(ICON_FA_XMARK))
                                remove = (int)r;
                            ImGui::PopID();
                        }
                        if (remove >= 0)
                        {
                            rows.erase(rows.begin() + remove);
                            changed = true;
                        }
                        ImGui::EndTable();
                    }
                    if (ImGui::SmallButton(ICON_FA_PLUS "  Add"))
                    {
                        std::vector<std::string> row;
                        for (const auto& c : f.Columns)
                            row.push_back(c.Default);
                        rows.push_back(std::move(row));
                        changed = true;
                    }
                    ImGui::Spacing();
                    break;
                }
            }
            ImGui::PopID();
        }
        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            Save(path, *def, data);
            s_Time = fs::last_write_time(path, ec);
        }
        CommitEdit(false);
        auto extra = Extras().find(def->Kind);
        if (extra != Extras().end())
        {
            ImGui::Spacing();
            extra->second(path, data);
        }
        if (!def->Tooltip.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", def->Tooltip.c_str());
            ImGui::PopStyleColor();
        }
    }
}
