#include "BlendSpaceInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Animation/BlendSpace.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Animation/AnimationStateMachine.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Conqueror::Editor::BlendSpaceInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        // Sürükleme gibi süren düzenlemeler tek geri alma adımı: başlangıç metni tutulur, bırakınca kaydedilir
        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;

        void BeginEdit(const std::string& path)
        {
            if (s_Edit.Open)
                return;
            s_Edit.Open = true;
            s_Edit.Path = path;
            s_Edit.Before = AssetFileCommand::ReadFile(path);
        }

        void Commit(bool force)
        {
            if (!s_Edit.Open || (!force && (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left))))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after),
                                                                                    "Edit " + fs::path(s_Edit.Path).filename().string()));
        }

        // Önizleme noktasındaki örnek ağırlıkları (Animator'ın blend tree hesabıyla aynı)
        std::vector<float> PreviewWeights(const BlendSpaceAsset& a, const glm::vec2& p)
        {
            AnimState st;
            st.Type = AnimStateType::BlendTree;
            st.BlendType = a.Is2D ? AnimBlendTreeType::FreeformCartesian2D : AnimBlendTreeType::Simple1D;
            st.BlendParameter = "x";
            st.BlendParameterY = "y";
            std::vector<int> order;
            for (size_t i = 0; i < a.Samples.size(); ++i)
                order.push_back((int)i);
            if (!a.Is2D)
                std::sort(order.begin(), order.end(), [&](int l, int r) { return a.Samples[(size_t)l].Position.x < a.Samples[(size_t)r].Position.x; });
            for (int i : order)
            {
                AnimBlendChild c;
                c.Threshold = a.Samples[(size_t)i].Position.x;
                c.Position = a.Samples[(size_t)i].Position;
                st.BlendChildren.push_back(c);
            }
            AnimationComponent ac;
            AnimParameterRuntimeValue vx, vy;
            vx.Name = "x"; vx.Type = AnimParameterType::Float; vx.FloatValue = p.x;
            vy.Name = "y"; vy.Type = AnimParameterType::Float; vy.FloatValue = p.y;
            ac.Parameters.push_back(vx);
            ac.Parameters.push_back(vy);
            std::vector<float> sorted, out(a.Samples.size(), 0.0f);
            AnimationStateMachine::ComputeBlendWeights(st, &ac, sorted);
            for (size_t k = 0; k < order.size() && k < sorted.size(); ++k)
                out[(size_t)order[k]] = sorted[k];
            return out;
        }
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path, s_Error;
        static fs::file_time_type s_Time{};
        static BlendSpaceAsset s_Asset;
        static bool s_Loaded = false;
        static int s_Selected = -1, s_Dragging = -1;
        static glm::vec2 s_Preview{ 0.0f };
        static bool s_DragPreview = false;

        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Edit.Open && s_Edit.Path != path)
            Commit(true);
        if (s_Path != path || s_Time != time)
        {
            if (s_Path != path)
            {
                s_Selected = -1;
                s_Dragging = -1;
            }
            s_Path = path;
            s_Time = time;
            s_Loaded = BlendSpaces::Load(path, s_Asset, &s_Error);
            if (s_Loaded && s_Path != path)
                s_Preview = { s_Asset.AxisX.Min, s_Asset.AxisY.Min };
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_BRAILLE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(s_Loaded && !s_Asset.Is2D ? "Blend Space 1D" : "Blend Space");
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The blend space could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }

        BlendSpaceAsset& a = s_Asset;
        bool changed = false;
        auto edit = [&] { BeginEdit(path); changed = true; };

        // ---- Eksenler
        auto axisUI = [&](const char* id, const char* title, BlendSpaceAxis& axis) {
            ImGui::PushID(id);
            UI::SubHeader(title);
            UI::BeginProperty("Name", "Shown on the grid; the Animator state's blend parameter drives this axis.");
            if (AssetUI::InputString("##name", axis.Name)) edit();
            UI::EndProperty();
            float mn = axis.Min, mx = axis.Max;
            if (UI::DragFloat("Minimum", mn, 1.0f, -100000.0f, 100000.0f, "%.2f") && mn < axis.Max) { axis.Min = mn; edit(); }
            if (UI::DragFloat("Maximum", mx, 1.0f, -100000.0f, 100000.0f, "%.2f") && mx > axis.Min) { axis.Max = mx; edit(); }
            int div = axis.Divisions;
            if (UI::SliderInt("Grid Divisions", div, 1, 16)) { axis.Divisions = div; edit(); }
            ImGui::PopID();
        };
        axisUI("ax", a.Is2D ? "Horizontal Axis" : "Axis", a.AxisX);
        if (a.Is2D)
            axisUI("ay", "Vertical Axis", a.AxisY);

        UI::SubHeader("Samples");
        UI::BeginProperty("Clips From", "A model whose clips the samples use (lists the clip names).");
        if (AssetUI::AssetSlot("##clipsfrom", "Model", { ".glb", ".gltf", ".fbx", ".obj", ".dae" }, a.ClipsFrom)) edit();
        UI::EndProperty();
        std::vector<std::string> clips;
        if (!a.ClipsFrom.empty())
            if (auto model = ModelLoader::Load(AssetUI::Resolve(a.ClipsFrom).string()))
                for (const auto& c : model->Animations)
                    if (c)
                        clips.push_back(c->Name);

        // ---- Izgara
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = a.Is2D ? std::min(w, 300.0f) : 70.0f;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##grid", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = 14.0f;
        const ImVec2 g0(o.x + pad, o.y + pad), g1(o.x + w - pad, o.y + h - pad);
        auto toScreen = [&](const glm::vec2& v) {
            const float tx = (v.x - a.AxisX.Min) / (a.AxisX.Max - a.AxisX.Min);
            const float ty = a.Is2D ? (v.y - a.AxisY.Min) / (a.AxisY.Max - a.AxisY.Min) : 0.5f;
            return ImVec2(g0.x + tx * (g1.x - g0.x), g1.y - ty * (g1.y - g0.y));
        };
        auto toValue = [&](const ImVec2& s) {
            glm::vec2 v;
            v.x = a.AxisX.Min + std::clamp((s.x - g0.x) / (g1.x - g0.x), 0.0f, 1.0f) * (a.AxisX.Max - a.AxisX.Min);
            v.y = a.Is2D ? a.AxisY.Min + std::clamp((g1.y - s.y) / (g1.y - g0.y), 0.0f, 1.0f) * (a.AxisY.Max - a.AxisY.Min) : 0.0f;
            return v;
        };
        // Izgara çizgisine yapıştır (Unreal gibi): Shift basılıysa serbest
        auto snap = [&](glm::vec2 v) {
            if (ImGui::GetIO().KeyShift)
                return v;
            const float sx = (a.AxisX.Max - a.AxisX.Min) / (float)a.AxisX.Divisions;
            v.x = a.AxisX.Min + std::round((v.x - a.AxisX.Min) / sx) * sx;
            if (a.Is2D)
            {
                const float sy = (a.AxisY.Max - a.AxisY.Min) / (float)a.AxisY.Divisions;
                v.y = a.AxisY.Min + std::round((v.y - a.AxisY.Min) / sy) * sy;
            }
            return v;
        };
        dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), IM_COL32(26, 27, 31, 255), 4.0f);
        for (int i = 0; i <= a.AxisX.Divisions; ++i)
        {
            const float x = g0.x + (g1.x - g0.x) * (float)i / (float)a.AxisX.Divisions;
            dl->AddLine(ImVec2(x, g0.y), ImVec2(x, g1.y), IM_COL32(255, 255, 255, i == 0 || i == a.AxisX.Divisions ? 50 : 18));
        }
        if (a.Is2D)
            for (int i = 0; i <= a.AxisY.Divisions; ++i)
            {
                const float y = g0.y + (g1.y - g0.y) * (float)i / (float)a.AxisY.Divisions;
                dl->AddLine(ImVec2(g0.x, y), ImVec2(g1.x, y), IM_COL32(255, 255, 255, i == 0 || i == a.AxisY.Divisions ? 50 : 18));
            }
        else
            dl->AddLine(ImVec2(g0.x, (g0.y + g1.y) * 0.5f), ImVec2(g1.x, (g0.y + g1.y) * 0.5f), IM_COL32(255, 255, 255, 50));

        // Önizleme noktası ve ağırlıklar
        const std::vector<float> weights = a.Samples.empty() ? std::vector<float>{} : PreviewWeights(a, s_Preview);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int hoveredSample = -1;
        for (size_t i = 0; i < a.Samples.size(); ++i)
        {
            const ImVec2 sp = toScreen(a.Samples[i].Position);
            if (hovered && std::hypot(mouse.x - sp.x, mouse.y - sp.y) < 8.0f)
                hoveredSample = (int)i;
        }
        for (size_t i = 0; i < a.Samples.size(); ++i)
        {
            const ImVec2 sp = toScreen(a.Samples[i].Position);
            const float wgt = i < weights.size() ? weights[i] : 0.0f;
            const bool sel = s_Selected == (int)i;
            const ImU32 col = sel ? IM_COL32(255, 200, 80, 255) : (hoveredSample == (int)i ? IM_COL32(200, 220, 255, 255) : IM_COL32(120, 170, 255, 255));
            if (wgt > 0.001f)
                dl->AddCircleFilled(sp, 6.0f + 10.0f * wgt, IM_COL32(120, 200, 120, (int)(60 + 120 * wgt)));
            dl->AddQuadFilled(ImVec2(sp.x, sp.y - 6), ImVec2(sp.x + 6, sp.y), ImVec2(sp.x, sp.y + 6), ImVec2(sp.x - 6, sp.y), col);
            const std::string label = a.Samples[i].Clip.empty() ? std::string("(no clip)") : a.Samples[i].Clip;
            dl->AddText(ImVec2(sp.x + 8, sp.y - 16), IM_COL32(220, 225, 235, 230), label.c_str());
        }
        const ImVec2 pp = toScreen(s_Preview);
        dl->AddCircle(pp, 7.0f, IM_COL32(120, 255, 140, 255), 0, 2.0f);
        dl->AddLine(ImVec2(pp.x - 10, pp.y), ImVec2(pp.x + 10, pp.y), IM_COL32(120, 255, 140, 200));
        dl->AddLine(ImVec2(pp.x, pp.y - 10), ImVec2(pp.x, pp.y + 10), IM_COL32(120, 255, 140, 200));
        // Eksen adları
        dl->AddText(ImVec2(g1.x - ImGui::CalcTextSize(a.AxisX.Name.c_str()).x, g1.y + 1), IM_COL32(200, 200, 210, 200), a.AxisX.Name.c_str());
        if (a.Is2D)
            dl->AddText(ImVec2(g0.x + 2, g0.y - 13), IM_COL32(200, 200, 210, 200), a.AxisY.Name.c_str());

        // Etkileşim: örnek sürükle / seç, boş yerde sürükle = önizleme, çift tık = örnek ekle, sağ tık = örneği sil
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoveredSample >= 0)
            {
                s_Selected = s_Dragging = hoveredSample;
                BeginEdit(path);
            }
            else
                s_DragPreview = true;
        }
        if (s_Dragging >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_Dragging < (int)a.Samples.size())
        {
            const glm::vec2 v = snap(toValue(mouse));
            if (v != a.Samples[(size_t)s_Dragging].Position)
            {
                a.Samples[(size_t)s_Dragging].Position = v;
                changed = true;
            }
        }
        if (s_DragPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            s_Preview = toValue(mouse);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            s_Dragging = -1;
            s_DragPreview = false;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredSample < 0)
        {
            edit();
            BlendSpaceSample sample;
            sample.Position = snap(toValue(mouse));
            sample.Clip = clips.empty() ? std::string() : clips.front();
            a.Samples.push_back(sample);
            s_Selected = (int)a.Samples.size() - 1;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredSample >= 0)
        {
            edit();
            a.Samples.erase(a.Samples.begin() + hoveredSample);
            s_Selected = -1;
        }
        ImGui::TextDisabled("Drag a sample to move it (Shift: no snapping), double-click to add, right-click to delete. "
                            "Drag on the grid to preview.");

        // ---- Örnek tablosu
        int removeIdx = -1;
        if (ImGui::BeginTable("##samples", a.Is2D ? 5 : 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(a.AxisX.Name.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            if (a.Is2D)
                ImGui::TableSetupColumn(a.AxisY.Name.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < a.Samples.size(); ++i)
            {
                auto& smp = a.Samples[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                if (s_Selected == (int)i)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 200, 80, 40));
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##clip", smp.Clip.empty() ? "(no clip)" : smp.Clip.c_str()))
                {
                    for (const auto& c : clips)
                        if (ImGui::Selectable(c.c_str(), c == smp.Clip)) { edit(); smp.Clip = c; }
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##x", &smp.Position.x, (a.AxisX.Max - a.AxisX.Min) / 200.0f, a.AxisX.Min, a.AxisX.Max, "%.2f")) edit();
                if (a.Is2D)
                {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("##y", &smp.Position.y, (a.AxisY.Max - a.AxisY.Min) / 200.0f, a.AxisY.Min, a.AxisY.Max, "%.2f")) edit();
                }
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", i < weights.size() ? weights[i] : 0.0f);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                    removeIdx = (int)i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeIdx >= 0)
        {
            edit();
            a.Samples.erase(a.Samples.begin() + removeIdx);
            s_Selected = -1;
        }
        if (ImGui::Button(ICON_FA_PLUS "  Add Sample", ImVec2(-1, 0)))
        {
            edit();
            BlendSpaceSample sample;
            sample.Position = { a.AxisX.Min, a.Is2D ? a.AxisY.Min : 0.0f };
            sample.Clip = clips.empty() ? std::string() : clips.front();
            a.Samples.push_back(sample);
        }
        if (clips.empty())
            UI::InfoBox("Set Clips From to a model to choose its clips.", UI::InfoType::Info);
        ImGui::TextWrapped("Use it in the Animator: a Blend Tree state's Blend Space slot. The state's blend parameters drive the axes.");

        if (changed)
        {
            BlendSpaces::Save(path, a);
            s_Time = fs::last_write_time(path, ec);
        }
        Commit(false);
    }
}
