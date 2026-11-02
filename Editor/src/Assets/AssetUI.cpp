#include "AssetUI.h"

#include "Core/Project/Project.h"
#include "Panels/ObjectPicker.h"

#include <algorithm>
#include <cctype>

namespace Conqueror::Editor::AssetUI
{
    namespace fs = std::filesystem;

    namespace
    {
        int ResizeCallback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto* str = (std::string*)data->UserData;
                str->resize((size_t)data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }
    }

    bool InputString(const char* id, std::string& value, ImGuiInputTextFlags flags)
    {
        flags |= ImGuiInputTextFlags_CallbackResize;
        return ImGui::InputText(id, value.data(), value.capacity() + 1, flags, ResizeCallback, &value);
    }

    bool InputMultiline(const char* id, std::string& value, const ImVec2& size, ImGuiInputTextFlags flags)
    {
        flags |= ImGuiInputTextFlags_CallbackResize;
        return ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1, size, flags, ResizeCallback, &value);
    }

    fs::path Resolve(const std::string& projectRelative)
    {
        if (projectRelative.empty())
            return {};
        fs::path p(projectRelative);
        if (p.is_relative() && Project::GetActive())
            p = Project::GetActiveProjectDirectory() / p;
        return p;
    }

    std::string ToProjectRelative(const fs::path& absolute)
    {
        if (absolute.empty())
            return {};
        if (Project::GetActive())
        {
            std::error_code ec;
            auto rel = fs::relative(absolute, Project::GetActiveProjectDirectory(), ec);
            if (!ec && !rel.empty() && rel.native()[0] != '.')
                return rel.generic_string();
        }
        return absolute.generic_string();
    }

    bool AssetSlot(const char* id, const std::string& typeName, const std::vector<std::string>& extensions, std::string& value)
    {
        ImGui::PushID(id);
        bool changed = false;
        std::string shown = value.empty() ? "None (" + typeName + ")" : fs::path(value).filename().string();
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 28.0f));
        ImGui::BeginDisabled();
        InputString("##name", shown, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string dropped((const char*)p->Data);
                const std::string ext = Lower(fs::path(dropped).extension().string());
                if (extensions.empty() || std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
                {
                    value = ToProjectRelative(dropped);
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0.0f, 4.0f);
        std::string picked;
        if (ObjectPicker::Button("##pick", typeName, extensions, Resolve(value).string(), picked))
        {
            value = ToProjectRelative(picked);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool BeginProperties(const char* id, float labelWidth)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void Row(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
    }

    void EndProperties()
    {
        ImGui::EndTable();
    }
}
