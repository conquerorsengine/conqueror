#include "CurveInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Math/CurveAsset.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Conqueror::Editor::CurveInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        ImU32 ChannelColor(const CurveAsset& c, size_t i)
        {
            if (c.Type == CurveType::Float)
                return ImGui::GetColorU32(UI::Colors::Accent);
            static const ImU32 rgb[] = { IM_COL32(235, 80, 80, 255), IM_COL32(110, 205, 90, 255), IM_COL32(85, 140, 245, 255),
                                         IM_COL32(200, 200, 200, 255) };
            return rgb[std::min<size_t>(i, 3)];
        }

        // Düzenleyici durumu (bir anda tek düzenleyici etkileşimde)
        struct EditorState
        {
            std::string Id;
            int Channel = 0, Key = -1;
            int Drag = 0;            // 0 yok, 1 anahtar, 2 giriş teğeti, 3 çıkış teğeti, 4 kaydırma
            float T0 = 0.0f, T1 = 1.0f, V0 = 0.0f, V1 = 1.0f;
            bool Fit = true;
            bool Visible[4] = { true, true, true, true };
        };
        EditorState& State(const char* id)
        {
            static EditorState s;
            if (s.Id != id)
            {
                s = EditorState{};
                s.Id = id;
            }
            return s;
        }

        void FitView(EditorState& st, const CurveAsset& c)
        {
            float t0, t1, v0, v1;
            c.Range(t0, t1, v0, v1);
            if (t1 - t0 < 1e-3f)
            {
                t0 -= 0.5f;
                t1 += 0.5f;
            }
            if (v1 - v0 < 1e-3f)
            {
                v0 -= 0.5f;
                v1 += 0.5f;
            }
            const float pt = (t1 - t0) * 0.06f, pv = (v1 - v0) * 0.12f;
            st.T0 = t0 - pt;
            st.T1 = t1 + pt;
            st.V0 = v0 - pv;
            st.V1 = v1 + pv;
        }

        // Izgara adımı: 1-2-5 dizisinden ~hedef piksel aralığına en yakın
        float NiceStep(float range, float pixels, float targetPx)
        {
            const float raw = range * targetPx / std::max(pixels, 1.0f);
            const float p = std::pow(10.0f, std::floor(std::log10(std::max(raw, 1e-6f))));
            for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
                if (p * m >= raw)
                    return p * m;
            return p * 10.0f;
        }

        // Düzenleme oturumu (Data Asset gibi): ilk değişiklikte önceki dosya, etkileşim bitince tek geri alma adımı
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
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after),
                                                                                    "Edit " + fs::path(s_Edit.Path).filename().string()));
        }
    }

    bool CurveEditorWidget(const char* id, CurveAsset& curve, float height)
    {
        EditorState& st = State(id);
        ImGui::PushID(id);
        bool changed = false;
        if (st.Fit)
        {
            FitView(st, curve);
            st.Fit = false;
        }
        if (st.Channel >= (int)curve.Channels.size())
            st.Channel = 0;

        // Kanal görünürlüğü (Vector / Color)
        if (curve.Channels.size() > 1)
        {
            for (size_t i = 0; i < curve.Channels.size() && i < 4; ++i)
            {
                if (i > 0)
                    ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(ChannelColor(curve, i)));
                ImGui::Checkbox((curve.Channels[i].Name + "##vis").c_str(), &st.Visible[i]);
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(ICON_FA_EXPAND " Frame All"))
            FitView(st, curve);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Fit every key in view (F with the editor hovered)");
        ImGui::SameLine();
        ImGui::TextDisabled("Double-click: add key  |  Drag: move  |  Right-click: key menu  |  Wheel: zoom (Ctrl: values)");

        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), IM_COL32(24, 25, 29, 255), 4.0f);
        const float tr = std::max(st.T1 - st.T0, 1e-4f), vr = std::max(st.V1 - st.V0, 1e-4f);
        auto toScreen = [&](float t, float v) { return ImVec2(p.x + (t - st.T0) / tr * w, p.y + height - (v - st.V0) / vr * height); };
        auto toCurve = [&](ImVec2 s) { return ImVec2(st.T0 + (s.x - p.x) / w * tr, st.V0 + (p.y + height - s.y) / height * vr); };
        dl->PushClipRect(p, ImVec2(p.x + w, p.y + height), true);

        // Izgara + etiketler
        const float ts = NiceStep(tr, w, 70.0f), vs = NiceStep(vr, height, 36.0f);
        for (float t = std::ceil(st.T0 / ts) * ts; t <= st.T1; t += ts)
        {
            const float x = toScreen(t, 0).x;
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + height), std::fabs(t) < ts * 0.01f ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 16));
            char b[32];
            std::snprintf(b, sizeof(b), "%g", std::round(t / ts) * ts + 0.0);   // + 0.0: "-0" yazmasın
            dl->AddText(ImVec2(x + 3, p.y + height - 16), IM_COL32(255, 255, 255, 90), b);
        }
        for (float v = std::ceil(st.V0 / vs) * vs; v <= st.V1; v += vs)
        {
            const float y = toScreen(0, v).y;
            dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), std::fabs(v) < vs * 0.01f ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 16));
            char b[32];
            std::snprintf(b, sizeof(b), "%g", std::round(v / vs) * vs + 0.0);
            dl->AddText(ImVec2(p.x + 3, y - 15), IM_COL32(255, 255, 255, 90), b);
        }

        // Eğriler (anahtar aralığının dışı soluk: sonsuzluk modu)
        for (size_t c = 0; c < curve.Channels.size(); ++c)
        {
            if (c < 4 && !st.Visible[c])
                continue;
            const auto& ch = curve.Channels[c];
            const ImU32 col = ChannelColor(curve, c);
            const ImU32 faint = (col & 0x00FFFFFF) | 0x50000000;
            const float firstT = ch.Keys.empty() ? 0.0f : ch.Keys.front().Time, lastT = ch.Keys.empty() ? 0.0f : ch.Keys.back().Time;
            ImVec2 prev;
            const int samples = (int)std::max(64.0f, w / 3.0f);
            for (int i = 0; i <= samples; ++i)
            {
                const float t = st.T0 + tr * (float)i / (float)samples;
                const ImVec2 cur = toScreen(t, ch.Evaluate(t, curve.PreInfinity, curve.PostInfinity));
                if (i > 0)
                    dl->AddLine(prev, cur, (t < firstT || t > lastT) ? faint : col, (int)c == st.Channel ? 2.2f : 1.5f);
                prev = cur;
            }
        }

        ImGui::InvisibleButton("##canvas", ImVec2(w, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                                  ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;

        // Anahtar ve teğet tutamaçları
        int hoverChannel = -1, hoverKey = -1, hoverHandle = 0;
        auto handlePos = [&](const CurveKey& k, bool out) {
            const ImVec2 kp = toScreen(k.Time, k.Value);
            const float slope = out ? k.OutTangent : k.InTangent;
            ImVec2 d(w / tr, -slope * height / vr);
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            d = ImVec2(d.x / len * 42.0f, d.y / len * 42.0f);
            return out ? ImVec2(kp.x + d.x, kp.y + d.y) : ImVec2(kp.x - d.x, kp.y - d.y);
        };
        for (size_t c = 0; c < curve.Channels.size(); ++c)
        {
            if (c < 4 && !st.Visible[c])
                continue;
            auto& ch = curve.Channels[c];
            for (size_t i = 0; i < ch.Keys.size(); ++i)
            {
                const auto& k = ch.Keys[i];
                const ImVec2 kp = toScreen(k.Time, k.Value);
                const bool selected = (int)c == st.Channel && (int)i == st.Key;
                if (selected && k.Mode != CurveKeyMode::Constant)
                    for (int side = 0; side < 2; ++side)
                    {
                        const bool out = side == 1;
                        if ((out && i + 1 >= ch.Keys.size()) || (!out && i == 0))
                            continue;
                        const ImVec2 hp = handlePos(k, out);
                        dl->AddLine(kp, hp, IM_COL32(255, 255, 255, 110));
                        dl->AddCircleFilled(hp, 3.5f, IM_COL32(255, 255, 255, 200));
                        if (hovered && std::fabs(mouse.x - hp.x) < 6 && std::fabs(mouse.y - hp.y) < 6)
                        {
                            hoverChannel = (int)c;
                            hoverKey = (int)i;
                            hoverHandle = out ? 3 : 2;
                        }
                    }
                const bool near = hovered && std::fabs(mouse.x - kp.x) < 6 && std::fabs(mouse.y - kp.y) < 6;
                if (near && hoverHandle == 0 && hoverKey < 0)
                {
                    hoverChannel = (int)c;
                    hoverKey = (int)i;
                }
                const float r = selected ? 5.5f : 4.5f;
                const ImU32 fill = selected ? IM_COL32(255, 255, 255, 255) : (near ? IM_COL32(255, 230, 140, 255) : ChannelColor(curve, c));
                dl->AddQuadFilled(ImVec2(kp.x, kp.y - r), ImVec2(kp.x + r, kp.y), ImVec2(kp.x, kp.y + r), ImVec2(kp.x - r, kp.y), fill);
                dl->AddQuad(ImVec2(kp.x, kp.y - r), ImVec2(kp.x + r, kp.y), ImVec2(kp.x, kp.y + r), ImVec2(kp.x - r, kp.y), IM_COL32(0, 0, 0, 180));
            }
        }
        dl->PopClipRect();

        // Etkileşim
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoverKey >= 0)
            {
                st.Channel = hoverChannel;
                st.Key = hoverKey;
                st.Drag = hoverHandle ? hoverHandle : 1;
            }
            else if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                st.Key = -1;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoverKey < 0)
        {
            // Eğrinin üstüne çift tık: o kanala eğri değeriyle; boşluğa: seçili kanala imlecin değeriyle
            const ImVec2 cv = toCurve(mouse);
            int target = -1;
            float best = 12.0f;
            for (size_t c = 0; c < curve.Channels.size(); ++c)
            {
                if (c < 4 && !st.Visible[c])
                    continue;
                const float y = toScreen(cv.x, curve.Channels[c].Evaluate(cv.x, curve.PreInfinity, curve.PostInfinity)).y;
                if (std::fabs(y - mouse.y) < best)
                {
                    best = std::fabs(y - mouse.y);
                    target = (int)c;
                }
            }
            const int c = target >= 0 ? target : st.Channel;
            if (c >= 0 && c < (int)curve.Channels.size())
            {
                auto& ch = curve.Channels[(size_t)c];
                const float value = target >= 0 ? ch.Evaluate(cv.x, curve.PreInfinity, curve.PostInfinity) : cv.y;
                st.Channel = c;
                st.Key = ch.AddKey(cv.x, value);
                changed = true;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            st.Drag = 4;
        if (st.Drag != 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            st.Drag = 0;
        if (st.Drag == 4)
        {
            st.T0 -= io.MouseDelta.x / w * tr;
            st.T1 -= io.MouseDelta.x / w * tr;
            st.V0 += io.MouseDelta.y / height * vr;
            st.V1 += io.MouseDelta.y / height * vr;
        }
        else if (st.Drag != 0 && st.Channel < (int)curve.Channels.size())
        {
            auto& ch = curve.Channels[(size_t)st.Channel];
            if (st.Key >= 0 && st.Key < (int)ch.Keys.size() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            {
                auto& k = ch.Keys[(size_t)st.Key];
                const ImVec2 cv = toCurve(mouse);
                if (st.Drag == 1)
                {
                    // Komşu anahtarların arasında kalır (sıra değişmez)
                    const float lo = st.Key > 0 ? ch.Keys[(size_t)st.Key - 1].Time + 1e-3f : -1e9f;
                    const float hi = st.Key + 1 < (int)ch.Keys.size() ? ch.Keys[(size_t)st.Key + 1].Time - 1e-3f : 1e9f;
                    k.Time = std::clamp(cv.x, lo, hi);
                    k.Value = cv.y;
                }
                else
                {
                    // Teğet: anahtardan imlece eğim; sürüklenen teğet Free olur (kırık teğet)
                    const float dt = cv.x - k.Time, dv = cv.y - k.Value;
                    float slope = std::fabs(dt) > 1e-5f ? dv / dt : (dv > 0 ? 1e4f : -1e4f);
                    slope = std::clamp(slope, -1e4f, 1e4f);
                    k.Mode = CurveKeyMode::Free;
                    if (st.Drag == 3)
                        k.OutTangent = slope;
                    else
                        k.InTangent = slope;
                    if (!io.KeyAlt)   // Alt: kırık teğet, değilse iki taraf aynı eğim
                    {
                        k.InTangent = slope;
                        k.OutTangent = slope;
                    }
                }
                ch.Recalculate();
                changed = true;
            }
        }
        // Yakınlaştırma: tekerlek zaman ekseni, Ctrl + tekerlek değer ekseni (imleç sabit)
        if (hovered && io.MouseWheel != 0.0f)
        {
            const ImVec2 cv = toCurve(mouse);
            const float f = io.MouseWheel > 0 ? 0.85f : 1.0f / 0.85f;
            if (io.KeyCtrl)
            {
                st.V0 = cv.y + (st.V0 - cv.y) * f;
                st.V1 = cv.y + (st.V1 - cv.y) * f;
            }
            else
            {
                st.T0 = cv.x + (st.T0 - cv.x) * f;
                st.T1 = cv.x + (st.T1 - cv.x) * f;
            }
        }
        if (hovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
            FitView(st, curve);
        // Sağ tık: anahtar menüsü
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoverKey >= 0)
        {
            st.Channel = hoverChannel;
            st.Key = hoverKey;
            ImGui::OpenPopup("##keyMenu");
        }
        if (ImGui::BeginPopup("##keyMenu"))
        {
            if (st.Channel < (int)curve.Channels.size() && st.Key >= 0 && st.Key < (int)curve.Channels[(size_t)st.Channel].Keys.size())
            {
                auto& ch = curve.Channels[(size_t)st.Channel];
                auto& k = ch.Keys[(size_t)st.Key];
                for (int m = 0; m < 4; ++m)
                    if (ImGui::MenuItem(CurveAssets::ModeName((CurveKeyMode)m), nullptr, k.Mode == (CurveKeyMode)m))
                    {
                        k.Mode = (CurveKeyMode)m;
                        ch.Recalculate();
                        changed = true;
                    }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_TRASH "  Delete Key", "Del", false, ch.Keys.size() > 1))
                {
                    ch.Keys.erase(ch.Keys.begin() + st.Key);
                    ch.Recalculate();
                    st.Key = -1;
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        if (hovered && st.Key >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && st.Channel < (int)curve.Channels.size() &&
            curve.Channels[(size_t)st.Channel].Keys.size() > 1)
        {
            auto& ch = curve.Channels[(size_t)st.Channel];
            if (st.Key < (int)ch.Keys.size())
            {
                ch.Keys.erase(ch.Keys.begin() + st.Key);
                ch.Recalculate();
                st.Key = -1;
                changed = true;
            }
        }
        if (hoverKey >= 0 && st.Drag == 0)
        {
            const auto& k = curve.Channels[(size_t)hoverChannel].Keys[(size_t)hoverKey];
            ImGui::SetTooltip("%s  t=%.3f  v=%.3f  (%s)", curve.Channels[(size_t)hoverChannel].Name.c_str(), k.Time, k.Value,
                              CurveAssets::ModeName(k.Mode));
        }

        // Seçili anahtarın alanları
        if (st.Channel < (int)curve.Channels.size() && st.Key >= 0 && st.Key < (int)curve.Channels[(size_t)st.Channel].Keys.size())
        {
            auto& ch = curve.Channels[(size_t)st.Channel];
            auto& k = ch.Keys[(size_t)st.Key];
            ImGui::TextDisabled("Key %d of %s", st.Key + 1, ch.Name.c_str());
            const float fw = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
            ImGui::SetNextItemWidth(fw);
            float t = k.Time;
            if (ImGui::DragFloat("##kt", &t, 0.01f, 0.0f, 0.0f, "t %.3f"))
            {
                const float lo = st.Key > 0 ? ch.Keys[(size_t)st.Key - 1].Time + 1e-3f : -1e9f;
                const float hi = st.Key + 1 < (int)ch.Keys.size() ? ch.Keys[(size_t)st.Key + 1].Time - 1e-3f : 1e9f;
                k.Time = std::clamp(t, lo, hi);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw);
            changed |= ImGui::DragFloat("##kv", &k.Value, 0.01f, 0.0f, 0.0f, "v %.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw);
            int mode = (int)k.Mode;
            if (ImGui::Combo("##km", &mode, "Auto\0Linear\0Constant\0Free\0"))
            {
                k.Mode = (CurveKeyMode)mode;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw);
            ImGui::BeginDisabled(k.Mode != CurveKeyMode::Free);
            float tangents[2] = { k.InTangent, k.OutTangent };
            if (ImGui::DragFloat2("##ktan", tangents, 0.01f, 0.0f, 0.0f, "%.2f"))
            {
                k.InTangent = tangents[0];
                k.OutTangent = tangents[1];
                changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("In / out tangent (slope, value per second); editable in Free mode");
            if (changed)
                ch.Recalculate();
        }
        ImGui::PopID();
        return changed;
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time{};
        static CurveAsset s_Curve;
        static bool s_Loaded = false;
        static std::string s_Error;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Edit.Open && s_Edit.Path != path)
            CommitEdit(true);
        if (s_Path != path || s_Time != time)
        {
            if (s_Path != path)
                State(("##curve" + path).c_str()).Fit = true;
            s_Path = path;
            s_Time = time;
            s_Loaded = CurveAssets::Load(path, s_Curve, &s_Error);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CHART_LINE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Curve - %s", CurveAssets::TypeName(s_Curve.Type));
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The curve could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }

        bool changed = false;
        int type = (int)s_Curve.Type;
        UI::BeginProperty("Type", "Float: one value, Vector: X Y Z, Color: R G B A. Changing it resets the keys.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##type", &type, "Float\0Vector\0Color\0") && type != (int)s_Curve.Type)
        {
            const auto pre = s_Curve.PreInfinity, post = s_Curve.PostInfinity;
            s_Curve = CurveAsset::Default((CurveType)type);
            s_Curve.PreInfinity = pre;
            s_Curve.PostInfinity = post;
            State(("##curve" + path).c_str()).Fit = true;
            changed = true;
        }
        UI::EndProperty();
        int pre = (int)s_Curve.PreInfinity, post = (int)s_Curve.PostInfinity;
        UI::BeginProperty("Pre-Infinity", "Value before the first key: Clamp (hold), Loop, PingPong, Linear (continue the slope).");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##pre", &pre, "Clamp\0Loop\0PingPong\0Linear\0"))
        {
            s_Curve.PreInfinity = (CurveInfinity)pre;
            changed = true;
        }
        UI::EndProperty();
        UI::BeginProperty("Post-Infinity", "Value after the last key.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##post", &post, "Clamp\0Loop\0PingPong\0Linear\0"))
        {
            s_Curve.PostInfinity = (CurveInfinity)post;
            changed = true;
        }
        UI::EndProperty();

        // Hazır eğriler (Unity eğri ön ayarları): seçili / ilk kanala
        if (s_Curve.Type == CurveType::Float)
        {
            UI::BeginProperty("Presets", "Replace the keys with a preset shape (0..1 over 0..1 s).");
            auto preset = [&](const char* label, std::initializer_list<CurveKey> keys) {
                if (ImGui::SmallButton(label))
                {
                    s_Curve.Channels[0].Keys.assign(keys.begin(), keys.end());
                    s_Curve.Channels[0].Recalculate();
                    State(("##curve" + path).c_str()).Fit = true;
                    changed = true;
                }
                ImGui::SameLine();
            };
            preset("Linear", { { 0, 0, 0, 0, CurveKeyMode::Linear }, { 1, 1, 0, 0, CurveKeyMode::Linear } });
            preset("Ease In", { { 0, 0, 0, 0, CurveKeyMode::Free }, { 1, 1, 2, 2, CurveKeyMode::Free } });
            preset("Ease Out", { { 0, 0, 2, 2, CurveKeyMode::Free }, { 1, 1, 0, 0, CurveKeyMode::Free } });
            preset("Ease In-Out", { { 0, 0, 0, 0, CurveKeyMode::Auto }, { 1, 1, 0, 0, CurveKeyMode::Auto } });
            preset("Constant", { { 0, 1, 0, 0, CurveKeyMode::Constant }, { 1, 1, 0, 0, CurveKeyMode::Constant } });
            preset("Bell", { { 0, 0, 0, 0, CurveKeyMode::Auto }, { 0.5f, 1, 0, 0, CurveKeyMode::Auto }, { 1, 0, 0, 0, CurveKeyMode::Auto } });
            ImGui::NewLine();
            UI::EndProperty();
        }

        changed |= CurveEditorWidget(("##curve" + path).c_str(), s_Curve);

        // Color: renk geçişi önizlemesi
        if (s_Curve.Type == CurveType::Color)
        {
            float t0, t1, v0, v1;
            s_Curve.Range(t0, t1, v0, v1);
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int steps = 64;
            for (int i = 0; i < steps; ++i)
            {
                const glm::vec4 c = glm::clamp(s_Curve.Evaluate(t0 + (t1 - t0) * (i + 0.5f) / steps), glm::vec4(0.0f), glm::vec4(1.0f));
                dl->AddRectFilled(ImVec2(p.x + w * i / steps, p.y), ImVec2(p.x + w * (i + 1) / steps, p.y + 18.0f),
                                  ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f)));
            }
            ImGui::Dummy(ImVec2(w, 22.0f));
        }

        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            CurveAssets::Save(path, s_Curve);
            s_Time = fs::last_write_time(path, ec);
        }
        CommitEdit(false);

        UI::SubHeader("Use in scripts");
        const std::string code = "CurveEvaluate(\"" + AssetUI::ToProjectRelative(path) + "\", t)";
        ImGui::TextWrapped("%s", code.c_str());
        if (ImGui::SmallButton(ICON_FA_COPY " Copy"))
            ImGui::SetClipboardText(code.c_str());
    }
}
