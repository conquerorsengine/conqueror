#include "EditorUI.h"

#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Conqueror::Editor::UI
{
    namespace
    {
        float s_LabelRatio = 0.40f;

        void Tooltip(const char* tooltip)
        {
            if (tooltip && tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", tooltip);
        }
    }

    float LabelWidth()
    {
        float avail = ImGui::GetContentRegionAvail().x;
        return std::clamp(avail * s_LabelRatio, 90.0f, 260.0f);
    }

    void SetLabelRatio(float ratio)
    {
        s_LabelRatio = std::clamp(ratio, 0.15f, 0.7f);
    }

    bool BeginSection(const char* title, bool defaultOpen, const char* icon)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;
        if (defaultOpen)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.19f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.23f, 0.24f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.21f, 0.24f, 1.0f));
        bool open;
        if (icon && icon[0])
        {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%s  %s", icon, title);
            open = ImGui::TreeNodeEx(title, flags, "%s", buffer);
        }
        else
        {
            open = ImGui::TreeNodeEx(title, flags, "%s", title);
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (open)
        {
            ImGui::Spacing();
            ImGui::Indent(4.0f);
        }
        return open;
    }

    void EndSection()
    {
        ImGui::Unindent(4.0f);
        ImGui::Spacing();
        ImGui::TreePop();
    }

    void SubHeader(const char* text)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::TextDim);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
    }

    void Separator(float spacing)
    {
        ImGui::Dummy(ImVec2(0.0f, spacing * 0.5f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, spacing * 0.5f));
    }

    void BeginProperty(const char* label, const char* tooltip)
    {
        ImGui::PushID(label);
        float labelWidth = LabelWidth();
        float startX = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();

        // Uzun etiketleri kısalt (… ile), tam metni ipucunda göster
        const char* visibleEnd = ImGui::FindRenderedTextEnd(label);
        ImVec2 textSize = ImGui::CalcTextSize(label, visibleEnd);
        if (textSize.x > labelWidth - 8.0f)
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float h = ImGui::GetFrameHeight();
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(pos.x, pos.y + ImGui::GetStyle().FramePadding.y),
                ImVec2(pos.x + labelWidth - 8.0f, pos.y + h), pos.x + labelWidth - 8.0f, label, visibleEnd, &textSize);
            ImGui::Dummy(ImVec2(labelWidth - 8.0f, h));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s%s%s", (int)(visibleEnd - label), label, tooltip ? "\n\n" : "", tooltip ? tooltip : "");
        }
        else
        {
            ImGui::TextUnformatted(label, visibleEnd);
            Tooltip(tooltip);
        }
        ImGui::SameLine(startX + labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void EndProperty()
    {
        ImGui::PopID();
    }

    bool Checkbox(const char* label, bool& value, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::Checkbox("##value", &value);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool DragFloat(const char* label, float& value, float speed, float min, float max, const char* format, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::DragFloat("##value", &value, speed, min, max, format, (min < max) ? ImGuiSliderFlags_AlwaysClamp : 0);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool SliderFloat(const char* label, float& value, float min, float max, const char* format, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::SliderFloat("##value", &value, min, max, format);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool DragInt(const char* label, int& value, float speed, int min, int max, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::DragInt("##value", &value, speed, min, max, "%d", (min < max) ? ImGuiSliderFlags_AlwaysClamp : 0);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool SliderInt(const char* label, int& value, int min, int max, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::SliderInt("##value", &value, min, max);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool Combo(const char* label, int& index, const char* const items[], int count, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        index = std::clamp(index, 0, std::max(0, count - 1));
        bool changed = ImGui::Combo("##value", &index, items, count);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool Color3(const char* label, glm::vec3& color, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::ColorEdit3("##value", glm::value_ptr(color), ImGuiColorEditFlags_Float);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool Color4(const char* label, glm::vec4& color, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::ColorEdit4("##value", glm::value_ptr(color), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool Vec2(const char* label, glm::vec2& value, float speed, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = ImGui::DragFloat2("##value", glm::value_ptr(value), speed);
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    bool Vec3(const char* label, glm::vec3& value, float speed, float resetValue, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        bool changed = false;
        float total = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        float buttonW = ImGui::GetFrameHeight();
        float fieldW = std::max(20.0f, (total - 3.0f * buttonW - 2.0f * spacing) / 3.0f);
        const char* axes[3] = { "X", "Y", "Z" };
        const ImVec4 colors[3] = { Colors::AxisX, Colors::AxisY, Colors::AxisZ };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        for (int i = 0; i < 3; ++i)
        {
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[i].x * 1.15f, colors[i].y * 1.15f, colors[i].z * 1.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[i]);
            if (ImGui::Button(axes[i], ImVec2(buttonW, ImGui::GetFrameHeight())))
            {
                value[i] = resetValue;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Sıfırla (%.2f)", resetValue);
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fieldW);
            changed |= ImGui::DragFloat("##v", &value[i], speed, 0.0f, 0.0f, "%.3f");
            if (i < 2)
                ImGui::SameLine(0.0f, spacing);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        EndProperty();
        return changed;
    }

    bool InputText(const char* label, std::string& value, const char* tooltip)
    {
        BeginProperty(label, tooltip);
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        bool changed = ImGui::InputText("##value", buffer, sizeof(buffer));
        if (changed)
            value = buffer;
        Tooltip(tooltip);
        EndProperty();
        return changed;
    }

    void Text(const char* label, const char* fmt, ...)
    {
        BeginProperty(label);
        va_list args;
        va_start(args, fmt);
        ImGui::TextV(fmt, args);
        va_end(args);
        EndProperty();
    }

    void InfoBox(const char* text, InfoType type)
    {
        ImVec4 color = Colors::Accent;
        const char* tag = "i";
        switch (type)
        {
            case InfoType::Warning: color = Colors::Warning; tag = "!"; break;
            case InfoType::Error:   color = Colors::Error; tag = "x"; break;
            case InfoType::Success: color = Colors::Success; tag = "+"; break;
            default: break;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 start = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        float pad = 6.0f;
        float iconW = 18.0f;
        float wrapWidth = width - iconW - pad * 3.0f;
        ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, std::max(wrapWidth, 10.0f));
        float height = std::max(textSize.y, ImGui::GetTextLineHeight()) + pad * 2.0f;
        ImVec4 bg = ImVec4(color.x, color.y, color.z, 0.12f);
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + height), ImGui::GetColorU32(bg), 4.0f);
        dl->AddRectFilled(start, ImVec2(start.x + 3.0f, start.y + height), ImGui::GetColorU32(color), 2.0f);
        ImVec2 center(start.x + pad + iconW * 0.5f + 2.0f, start.y + height * 0.5f);
        dl->AddCircleFilled(center, 8.0f, ImGui::GetColorU32(color));
        ImVec2 tagSize = ImGui::CalcTextSize(tag);
        dl->AddText(ImVec2(center.x - tagSize.x * 0.5f, center.y - tagSize.y * 0.5f), IM_COL32(20, 20, 24, 255), tag);
        ImGui::SetCursorScreenPos(ImVec2(start.x + iconW + pad * 2.0f, start.y + pad));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + 4.0f));
        ImGui::Dummy(ImVec2(width, 0.0f));
    }

    void HelpMarker(const char* text)
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    bool ButtonFullWidth(const char* label, float height)
    {
        return ImGui::Button(label, ImVec2(-FLT_MIN, height));
    }

    bool AccentButton(const char* label, const ImVec2& size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentDim);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::Accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.40f, 0.70f, 1.0f));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    bool DangerButton(const char* label, const ImVec2& size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.24f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.14f, 0.14f, 1.0f));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    bool ToggleButton(const char* label, bool& value, const ImVec2& size)
    {
        if (value)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::Accent);
        }
        bool pressed = ImGui::Button(label, size);
        if (value)
            ImGui::PopStyleColor(2);
        if (pressed)
            value = !value;
        return pressed;
    }

    void StatusDot(const ImVec4& color)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + h * 0.5f + 1.0f), 4.0f, ImGui::GetColorU32(color));
        ImGui::Dummy(ImVec2(12.0f, h));
    }

    void Badge(const char* text, const ImVec4& color)
    {
        ImVec2 size = ImGui::CalcTextSize(text);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 pad(5.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x + pad.x * 2.0f, p.y + size.y + pad.y * 2.0f),
            ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.25f)), 3.0f);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(color), text);
        ImGui::Dummy(ImVec2(size.x + pad.x * 2.0f, size.y + pad.y * 2.0f));
    }

    void ProgressBarLabeled(float fraction, const char* overlay, const ImVec4& color)
    {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), overlay);
        ImGui::PopStyleColor();
    }

    bool GradientEditor(const char* label, Conqueror::ColorGradient& g)
    {
        bool changed = false;
        BeginProperty(label, "Click: add key  |  Drag: move  |  Right click: delete  |  Double click: color");
        ImGui::PushID(label);
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 22.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Saydamlık için dama + gradyan şeritleri
        for (float x = 0; x < w; x += 8.0f)
            for (int r = 0; r < 3; ++r)
                if (((int)(x / 8.0f) + r) & 1)
                    dl->AddRectFilled(ImVec2(p.x + x, p.y + r * 8.0f), ImVec2(p.x + std::min(x + 8.0f, w), p.y + std::min((r + 1) * 8.0f, h)), IM_COL32(110, 110, 115, 255));
                else
                    dl->AddRectFilled(ImVec2(p.x + x, p.y + r * 8.0f), ImVec2(p.x + std::min(x + 8.0f, w), p.y + std::min((r + 1) * 8.0f, h)), IM_COL32(70, 70, 75, 255));
        const int steps = 48;
        for (int i = 0; i < steps; ++i)
        {
            float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
            glm::vec4 a = g.Evaluate(t0), b = g.Evaluate(t1);
            dl->AddRectFilledMultiColor(ImVec2(p.x + t0 * w, p.y), ImVec2(p.x + t1 * w, p.y + h),
                                        ImGui::GetColorU32(ImVec4(a.r, a.g, a.b, a.a)), ImGui::GetColorU32(ImVec4(b.r, b.g, b.b, b.a)),
                                        ImGui::GetColorU32(ImVec4(b.r, b.g, b.b, b.a)), ImGui::GetColorU32(ImVec4(a.r, a.g, a.b, a.a)));
        }
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 200));

        // Anahtar işaretleri (altta üçgenler)
        ImGui::InvisibleButton("##grad", ImVec2(w, h + 12.0f));
        const bool hovered = ImGui::IsItemHovered();
        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID dragId = ImGui::GetID("drag");
        int dragging = st->GetInt(dragId, -1);
        ImGuiIO& io = ImGui::GetIO();
        int hoverKey = -1;
        for (int i = 0; i < (int)g.Keys.size(); ++i)
        {
            float x = p.x + g.Keys[(size_t)i].Time * w;
            ImVec2 a(x, p.y + h), b(x - 6, p.y + h + 10), c(x + 6, p.y + h + 10);
            bool near = std::fabs(io.MousePos.x - x) < 7.0f && io.MousePos.y > p.y && io.MousePos.y < p.y + h + 12;
            if (near && hoverKey < 0) hoverKey = i;
            glm::vec4 kc = g.Keys[(size_t)i].Color;
            dl->AddTriangleFilled(a, b, c, ImGui::GetColorU32(ImVec4(kc.r, kc.g, kc.b, 1.0f)));
            dl->AddTriangle(a, b, c, (dragging == i || near) ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 220), 1.5f);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoverKey >= 0)
                dragging = hoverKey;
            else
            {
                float t = std::clamp((io.MousePos.x - p.x) / w, 0.0f, 1.0f);
                g.Keys.push_back({ t, g.Evaluate(t) });
                dragging = (int)g.Keys.size() - 1;
                changed = true;
            }
        }
        if (dragging >= 0 && dragging < (int)g.Keys.size())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                float t = std::clamp((io.MousePos.x - p.x) / w, 0.0f, 1.0f);
                if (t != g.Keys[(size_t)dragging].Time) { g.Keys[(size_t)dragging].Time = t; changed = true; }
            }
            else
            {
                // Bırakınca sırala (sürüklenen anahtarın indisi değişebilir)
                g.Sort();
                dragging = -1;
            }
        }
        st->SetInt(dragId, dragging);
        if (hovered && hoverKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && g.Keys.size() > 1)
        {
            g.Keys.erase(g.Keys.begin() + hoverKey);
            changed = true;
        }
        ImGuiID editId = ImGui::GetID("edit");
        if (hovered && hoverKey >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            st->SetInt(editId, hoverKey);
            ImGui::OpenPopup("##gradcolor");
        }
        if (ImGui::BeginPopup("##gradcolor"))
        {
            int k = st->GetInt(editId, -1);
            if (k >= 0 && k < (int)g.Keys.size())
                changed |= ImGui::ColorPicker4("##pick", &g.Keys[(size_t)k].Color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        EndProperty();
        return changed;
    }

    bool CurveEditor(const char* label, Conqueror::FloatCurve& curve, float minValue, float maxValue, float height)
    {
        bool changed = false;
        BeginProperty(label, "Double click: add point  |  Drag: move  |  Right click: delete");
        ImGui::PushID(label);
        const float w = ImGui::GetContentRegionAvail().x;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), IM_COL32(28, 29, 33, 255), 4.0f);
        for (int i = 1; i < 4; ++i)
        {
            dl->AddLine(ImVec2(p.x + w * i / 4.0f, p.y), ImVec2(p.x + w * i / 4.0f, p.y + height), IM_COL32(255, 255, 255, 18));
            dl->AddLine(ImVec2(p.x, p.y + height * i / 4.0f), ImVec2(p.x + w, p.y + height * i / 4.0f), IM_COL32(255, 255, 255, 18));
        }
        const float range = std::max(maxValue - minValue, 1e-4f);
        auto toScreen = [&](float t, float v) { return ImVec2(p.x + t * w, p.y + height - (v - minValue) / range * height); };
        ImVec2 prev = toScreen(0.0f, curve.Evaluate(0.0f));
        for (int i = 1; i <= 64; ++i)
        {
            float t = i / 64.0f;
            ImVec2 cur = toScreen(t, curve.Evaluate(t));
            dl->AddLine(prev, cur, ImGui::GetColorU32(Colors::Accent), 2.0f);
            prev = cur;
        }
        ImGui::InvisibleButton("##curve", ImVec2(w, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID dragId = ImGui::GetID("drag");
        int dragging = st->GetInt(dragId, -1);
        int hoverKey = -1;
        for (int i = 0; i < (int)curve.Keys.size(); ++i)
        {
            ImVec2 kp = toScreen(curve.Keys[(size_t)i].Time, curve.Keys[(size_t)i].Value);
            bool near = std::fabs(io.MousePos.x - kp.x) < 6 && std::fabs(io.MousePos.y - kp.y) < 6;
            if (near && hoverKey < 0) hoverKey = i;
            dl->AddCircleFilled(kp, 4.5f, (near || dragging == i) ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 200, 90, 255));
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoverKey < 0)
        {
            float t = std::clamp((io.MousePos.x - p.x) / w, 0.0f, 1.0f);
            float v = std::clamp(minValue + (p.y + height - io.MousePos.y) / height * range, minValue, maxValue);
            curve.Keys.push_back({ t, v });
            curve.Sort();
            changed = true;
        }
        else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverKey >= 0)
            dragging = hoverKey;
        if (dragging >= 0 && dragging < (int)curve.Keys.size())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                auto& k = curve.Keys[(size_t)dragging];
                float lo = dragging > 0 ? curve.Keys[(size_t)dragging - 1].Time : 0.0f;
                float hi = dragging + 1 < (int)curve.Keys.size() ? curve.Keys[(size_t)dragging + 1].Time : 1.0f;
                k.Time = std::clamp((io.MousePos.x - p.x) / w, lo, hi);
                k.Value = std::clamp(minValue + (p.y + height - io.MousePos.y) / height * range, minValue, maxValue);
                changed = true;
            }
            else
                dragging = -1;
        }
        st->SetInt(dragId, dragging);
        if (hovered && hoverKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && curve.Keys.size() > 1)
        {
            curve.Keys.erase(curve.Keys.begin() + hoverKey);
            changed = true;
        }
        if (hoverKey >= 0)
            ImGui::SetTooltip("t=%.2f  v=%.3f", curve.Keys[(size_t)hoverKey].Time, curve.Keys[(size_t)hoverKey].Value);
        ImGui::PopID();
        EndProperty();
        return changed;
    }
}
