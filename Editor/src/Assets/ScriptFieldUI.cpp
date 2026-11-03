#include "ScriptFieldUI.h"

#include "Core/Serialization/SceneSerializer.h"
#include "Panels/ObjectPicker.h"

#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Conqueror::Editor::ScriptFieldUI
{
    bool ParseRange(const std::vector<std::string>& attrs, float& mn, float& mx)
    {
        for (const auto& a : attrs)
            if (a.rfind("Range(", 0) == 0)
                return std::sscanf(a.c_str(), "Range(%f,%f)", &mn, &mx) == 2 || std::sscanf(a.c_str(), "Range(%f, %f)", &mn, &mx) == 2;
        return false;
    }

    std::string Quoted(const std::vector<std::string>& attrs, const char* prefix)
    {
        for (const auto& a : attrs)
            if (a.rfind(prefix, 0) == 0)
            {
                const size_t q1 = a.find('"'), q2 = a.rfind('"');
                if (q1 != std::string::npos && q2 > q1)
                    return a.substr(q1 + 1, q2 - q1 - 1);
            }
        return {};
    }

    std::string Tooltip(const std::vector<std::string>& attrs) { return Quoted(attrs, "Tooltip("); }

    bool HasSpace(const std::vector<std::string>& attrs)
    {
        for (const auto& a : attrs)
            if (a == "Space" || a.rfind("Space(", 0) == 0)
                return true;
        return false;
    }

    std::string AssetFilter(const std::vector<std::string>& attrs) { return Quoted(attrs, "Asset("); }

    std::string PrettyName(const std::string& name)
    {
        // moveSpeed -> Move Speed, m_Health -> Health
        std::string n = name;
        if (n.rfind("m_", 0) == 0)
            n = n.substr(2);
        std::string out;
        for (size_t i = 0; i < n.size(); ++i)
        {
            const char c = n[i];
            if (c == '_')
            {
                out += ' ';
                continue;
            }
            if (i > 0 && std::isupper((unsigned char)c) && std::islower((unsigned char)n[i - 1]))
                out += ' ';
            out += i == 0 ? (char)std::toupper((unsigned char)c) : c;
        }
        return out;
    }

    bool DrawValue(const char* id, Scripting::ScriptValue& v, const Scripting::ScriptFieldInfo& f)
    {
        using Kind = Scripting::ScriptValue::Kind;
        float mn = 0.0f, mx = 0.0f;
        const bool hasRange = ParseRange(f.Attributes, mn, mx);
        // [Asset("...")]: dosya yuvası
        const std::string filter = AssetFilter(f.Attributes);
        if (!filter.empty() && (v.Type == Kind::String || v.Type == Kind::Null))
        {
            bool changed = false;
            const auto exts = ObjectPicker::ParseFilter(filter);
            const float h = ImGui::GetFrameHeight();
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x);
            char buf[1024];
            std::snprintf(buf, sizeof(buf), "%s", v.String.c_str());
            if (ImGui::InputTextWithHint("##path", ("None (" + ObjectPicker::TypeNameFor(exts) + ")").c_str(), buf, sizeof(buf)))
            {
                v = Scripting::ScriptValue::MakeString(buf);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    v = Scripting::ScriptValue::MakeString(SceneSerializer::ToAssetPath(std::string((const char*)payload->Data, payload->DataSize - 1)));
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (std::string picked; ObjectPicker::Button("assetpick", ObjectPicker::TypeNameFor(exts), exts,
                                                         v.String.empty() ? v.String : SceneSerializer::ResolveAssetPath(v.String), picked))
            {
                v = Scripting::ScriptValue::MakeString(picked.empty() ? picked : SceneSerializer::ToAssetPath(picked));
                changed = true;
            }
            ImGui::PopID();
            return changed;
        }
        ImGui::SetNextItemWidth(-1);
        switch (v.Type)
        {
        case Kind::Bool: return ImGui::Checkbox(id, &v.Bool);
        case Kind::Int:
        {
            int x = (int)v.Int;
            const bool changed = hasRange ? ImGui::SliderInt(id, &x, (int)mn, (int)mx) : ImGui::DragInt(id, &x, 0.2f);
            if (changed)
                v.Int = x;
            return changed;
        }
        case Kind::Float:
        {
            float x = (float)v.Float;
            const bool changed = hasRange ? ImGui::SliderFloat(id, &x, mn, mx, "%.3f") : ImGui::DragFloat(id, &x, 0.05f, 0.0f, 0.0f, "%.3f");
            if (changed)
                v.Float = x;
            return changed;
        }
        case Kind::String:
        {
            char buf[512] = {};
            std::strncpy(buf, v.String.c_str(), sizeof(buf) - 1);
            if (ImGui::InputText(id, buf, sizeof(buf)))
            {
                v.String = buf;
                return true;
            }
            return false;
        }
        default: ImGui::TextDisabled("(%s)", f.TypeName.empty() ? "object" : f.TypeName.c_str()); return false;
        }
    }

    bool DrawFieldTable(const char* id, const std::vector<Scripting::ScriptFieldInfo>& fields, std::map<std::string, Scripting::ScriptValue>& overrides)
    {
        bool changed = false;
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& f : fields)
        {
            const std::string header = Quoted(f.Attributes, "Header(");
            if (!header.empty() || HasSpace(f.Attributes))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(0.0f, header.empty() ? 6.0f : 3.0f));
                if (!header.empty())
                    ImGui::TextColored(ImVec4(0.62f, 0.74f, 1.0f, 1.0f), "%s", header.c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool overridden = overrides.count(f.Name) != 0;
            if (overridden)
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 6, p.y + 2), ImVec2(p.x - 4, p.y + ImGui::GetFrameHeight() - 2),
                                                          ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 1)));
            }
            ImGui::TextUnformatted(PrettyName(f.Name).c_str());
            const std::string tip = Tooltip(f.Attributes);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s%s%s (%s)", tip.c_str(), tip.empty() ? "" : "\n", f.Name.c_str(), f.TypeName.empty() ? "var" : f.TypeName.c_str());
            if (ImGui::BeginPopupContextItem(("##ctx" + f.Name).c_str()))
            {
                if (ImGui::MenuItem("Reset to Default", nullptr, false, overridden))
                {
                    overrides.erase(f.Name);
                    changed = true;
                }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            Scripting::ScriptValue v = overridden ? overrides[f.Name] : f.Default;
            if (DrawValue(("##" + f.Name).c_str(), v, f))
            {
                overrides[f.Name] = v;
                changed = true;
            }
        }
        ImGui::EndTable();
        return changed;
    }
}
