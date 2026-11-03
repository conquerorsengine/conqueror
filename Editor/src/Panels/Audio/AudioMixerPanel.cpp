#include "AudioMixerPanel.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Project/Project.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        // Fader konumu <-> dB (Unity benzeri: -80..-60 alt %15, -60..+20 doğrusal)
        float DbToPos(float db)
        {
            db = std::clamp(db, -80.0f, 20.0f);
            if (db >= -60.0f)
                return 0.15f + 0.85f * (db + 60.0f) / 80.0f;
            return 0.15f * (db + 80.0f) / 20.0f;
        }

        float PosToDb(float p)
        {
            p = std::clamp(p, 0.0f, 1.0f);
            if (p >= 0.15f)
                return -60.0f + (p - 0.15f) / 0.85f * 80.0f;
            return -80.0f + p / 0.15f * 20.0f;
        }

        ImU32 ToU32(const glm::vec3& c, float a = 1.0f)
        {
            return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, a));
        }

        std::string FormatDb(float db)
        {
            char buf[16];
            if (db <= -79.9f) snprintf(buf, sizeof(buf), "-inf");
            else snprintf(buf, sizeof(buf), "%+.1f", db);
            return buf;
        }
    }

    // ------------------------------------------------------------------ Widget'lar

    bool AudioMixerPanel::Fader(const char* id, float& db, const ImVec2& size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, size);
        bool changed = false;
        const float knobH = 12.0f;
        const float travel = size.y - knobH;

        if (ImGui::IsItemActive())
        {
            static float s_GrabOffset = 0.0f;
            if (ImGui::IsItemActivated())
            {
                float knobY = pos.y + (1.0f - DbToPos(db)) * travel;
                float my = ImGui::GetIO().MousePos.y;
                s_GrabOffset = (my >= knobY && my <= knobY + knobH) ? my - knobY : knobH * 0.5f;
            }
            float p = 1.0f - (ImGui::GetIO().MousePos.y - s_GrabOffset - pos.y) / std::max(1.0f, travel);
            float newDb = PosToDb(p);
            if (ImGui::GetIO().KeyShift) // ince ayar: 0.1 dB'e yuvarla
                newDb = std::round(newDb * 10.0f) / 10.0f;
            else
                newDb = std::round(newDb * 2.0f) / 2.0f;
            if (newDb != db) { db = newDb; changed = true; }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            db = 0.0f;
            changed = true;
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        {
            db = std::clamp(db + ImGui::GetIO().MouseWheel * (ImGui::GetIO().KeyShift ? 0.1f : 1.0f), -80.0f, 20.0f);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s dB  (double-click: 0 dB, wheel: +-1 dB, Shift: fine)", FormatDb(db).c_str());

        // Ray + 0 dB çizgisi
        const float cx = pos.x + size.x * 0.5f;
        dl->AddRectFilled(ImVec2(cx - 2.0f, pos.y + knobH * 0.5f), ImVec2(cx + 2.0f, pos.y + size.y - knobH * 0.5f), IM_COL32(15, 15, 18, 255), 2.0f);
        float zeroY = pos.y + (1.0f - DbToPos(0.0f)) * travel + knobH * 0.5f;
        dl->AddLine(ImVec2(pos.x + 2.0f, zeroY), ImVec2(pos.x + size.x - 2.0f, zeroY), IM_COL32(255, 255, 255, 40));

        // Topuz
        float ky = pos.y + (1.0f - DbToPos(db)) * travel;
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        dl->AddRectFilled(ImVec2(pos.x + 1.0f, ky), ImVec2(pos.x + size.x - 1.0f, ky + knobH),
                          hot ? IM_COL32(215, 220, 230, 255) : IM_COL32(170, 175, 185, 255), 3.0f);
        dl->AddLine(ImVec2(pos.x + 3.0f, ky + knobH * 0.5f), ImVec2(pos.x + size.x - 3.0f, ky + knobH * 0.5f), IM_COL32(40, 40, 45, 255), 2.0f);
        return changed;
    }

    bool AudioMixerPanel::PanKnob(const char* id, float& pan, float radius)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
        bool changed = false;
        if (ImGui::IsItemActive())
        {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            float delta = (d.x - d.y) * (ImGui::GetIO().KeyShift ? 0.002f : 0.01f);
            if (delta != 0.0f)
            {
                pan = std::clamp(pan + delta, -1.0f, 1.0f);
                changed = true;
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            pan = 0.0f;
            changed = true;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            if (std::fabs(pan) < 0.005f) ImGui::SetTooltip("Pan: Center");
            else ImGui::SetTooltip("Pan: %.0f%% %s", std::fabs(pan) * 100.0f, pan < 0 ? "L" : "R");
        }

        const ImVec2 c(pos.x + radius, pos.y + radius);
        const float a0 = IM_PI * 0.75f, a1 = IM_PI * 2.25f;      // -135°..+135° (ekran açısı)
        const float mid = IM_PI * 1.5f;                            // üst
        const float a = mid + pan * IM_PI * 0.75f;
        dl->AddCircleFilled(c, radius - 1.0f, IM_COL32(28, 29, 33, 255), 24);
        dl->PathArcTo(c, radius - 3.0f, a0, a1, 24);
        dl->PathStroke(IM_COL32(60, 62, 70, 255), 0, 3.0f);
        dl->PathArcTo(c, radius - 3.0f, std::min(mid, a), std::max(mid, a), 16);
        dl->PathStroke(ImGui::GetColorU32(UI::Colors::Accent), 0, 3.0f);
        dl->AddLine(c, ImVec2(c.x + std::cos(a) * (radius - 4.0f), c.y + std::sin(a) * (radius - 4.0f)), IM_COL32(230, 230, 235, 255), 2.0f);
        return changed;
    }

    void AudioMixerPanel::Meter(const ImVec2& pos, const ImVec2& size, float peak, float rms, float hold)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(12, 12, 14, 255), 2.0f);

        auto yFor = [&](float db) { return pos.y + (1.0f - DbToPos(db)) * size.y; };
        const float peakDb = AudioMixer::LinearToDb(peak), rmsDb = AudioMixer::LinearToDb(rms), holdDb = AudioMixer::LinearToDb(hold);

        // Renk bölgeleri: yeşil < -12, sarı -12..-3, kırmızı > -3
        struct Zone { float From, To; ImU32 Col; };
        const Zone zones[] = { { -80.0f, -12.0f, IM_COL32(60, 200, 90, 255) }, { -12.0f, -3.0f, IM_COL32(230, 200, 60, 255) },
                               { -3.0f, 20.0f, IM_COL32(235, 70, 60, 255) } };
        for (const auto& z : zones)
        {
            float top = std::min(peakDb, z.To);
            if (top <= z.From)
                continue;
            ImU32 dim = (z.Col & 0x00FFFFFF) | 0x70000000;
            dl->AddRectFilled(ImVec2(pos.x + 1.0f, yFor(top)), ImVec2(pos.x + size.x - 1.0f, yFor(z.From)), dim);
            float rtop = std::min(rmsDb, z.To);
            if (rtop > z.From)
                dl->AddRectFilled(ImVec2(pos.x + 1.0f, yFor(rtop)), ImVec2(pos.x + size.x - 1.0f, yFor(z.From)), z.Col);
        }
        if (hold > 0.0001f)
        {
            float hy = yFor(holdDb);
            dl->AddLine(ImVec2(pos.x, hy), ImVec2(pos.x + size.x, hy), holdDb > -3.0f ? IM_COL32(255, 90, 80, 255) : IM_COL32(230, 230, 230, 220), 1.5f);
        }
    }

    // ------------------------------------------------------------------ Panel

    void AudioMixerPanel::OnImGuiRender(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(1100, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_SLIDERS " Audio Mixer###Audio Mixer", open))
        {
            ImGui::End();
            return;
        }

        auto& mixer = AudioMixer::Get();
        if (!AudioEngine::IsInitialized() || !mixer.IsInitialized())
        {
            UI::InfoBox("Audio device is not available - the mixer is disabled.", UI::InfoType::Warning);
            ImGui::End();
            return;
        }
        mixer.EnsureProjectLoaded(Project::GetActiveProjectDirectory());
        // Audio Mixer varlıkları dışarıda değiştiyse / silindiyse (saniyede bir)
        {
            static double lastCheck = 0.0;
            if (ImGui::GetTime() - lastCheck > 1.0 && !m_Dirty)
            {
                lastCheck = ImGui::GetTime();
                mixer.CheckAssetChanges();
            }
        }
        mixer.Update();
        if (!mixer.FindGroup(m_SelectedGroup))
            SelectGroup("Master");

        DrawToolbar();
        ImGui::Separator();

        const float leftW = 200.0f, rightW = 310.0f;
        const float h = ImGui::GetContentRegionAvail().y;

        ImGui::BeginChild("##MixerLeft", ImVec2(leftW, h), ImGuiChildFlags_None);
        DrawGroupTree();
        ImGui::Spacing();
        DrawSnapshots();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##MixerStrips", ImVec2(ImGui::GetContentRegionAvail().x - rightW - 8.0f, h), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        DrawStrips();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##MixerInspector", ImVec2(0, h), ImGuiChildFlags_None);
        DrawInspector();
        ImGui::EndChild();

        // Otomatik kayıt (değişiklikten 1 sn sonra)
        if (m_Dirty && m_AutoSave)
        {
            m_AutoSaveTimer += ImGui::GetIO().DeltaTime;
            if (m_AutoSaveTimer > 1.0f && !ImGui::IsAnyItemActive())
            {
                mixer.SaveAll();   // proje mikseri + Audio Mixer varlıkları
                m_Dirty = false;
                m_AutoSaveTimer = 0.0f;
            }
        }

        ImGui::End();
    }

    void AudioMixerPanel::DrawToolbar()
    {
        auto& mixer = AudioMixer::Get();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));

        if (ImGui::Button(m_Dirty ? ICON_FA_FLOPPY_DISK " Save*" : ICON_FA_FLOPPY_DISK " Save"))
        {
            if (mixer.SaveAll())
                m_Dirty = false;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", mixer.GetFilePath().string().c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE " Reload") && !mixer.GetFilePath().empty())
        {
            mixer.Load(mixer.GetFilePath());
            m_Dirty = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT " Reset"))
            ImGui::OpenPopup("Reset Mixer?");
        if (ImGui::BeginPopupModal("Reset Mixer?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Reset all groups, effects and snapshots to the default layout?");
            if (UI::DangerButton("Reset", ImVec2(110, 0)))
            {
                auto path = mixer.GetFilePath();
                mixer.ResetToDefault();
                SelectGroup("Master");
                MarkDirty();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Save", &m_AutoSave);

        // Test tonu
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_FA_WAVE_SQUARE " Test Tone");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##ToneFreq", &m_TestFrequency, 40.0f, 8000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLAY "##tone"))
            mixer.PlayTestTone(m_SelectedGroup, m_TestFrequency, 1.5f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Play a 1.5 s sine tone through the selected group");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP "##tonestop"))
            mixer.StopTestTones();

        // Snapshot
        auto& snaps = mixer.GetSnapshots();
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_FA_CAMERA " Snapshot");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        m_ActiveSnapshot = std::clamp(m_ActiveSnapshot, 0, std::max(0, (int)snaps.size() - 1));
        if (ImGui::BeginCombo("##Snapshot", snaps.empty() ? "<none>" : snaps[(size_t)m_ActiveSnapshot].Name.c_str()))
        {
            for (int i = 0; i < (int)snaps.size(); ++i)
                if (ImGui::Selectable(snaps[(size_t)i].Name.c_str(), i == m_ActiveSnapshot))
                    m_ActiveSnapshot = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(snaps.empty());
        if (ImGui::Button("Apply"))
            mixer.ApplySnapshot(snaps[(size_t)m_ActiveSnapshot].Name, m_SnapshotTransition);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("##SnapT", &m_SnapshotTransition, 0.0f, 5.0f, "%.1f s");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Transition time");

        ImGui::PopStyleVar();
    }

    void AudioMixerPanel::DrawGroupTree()
    {
        auto& mixer = AudioMixer::Get();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_LAYER_GROUP "  Groups");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 22.0f);
        if (ImGui::SmallButton(ICON_FA_PLUS "##addgroup"))
        {
            MixerGroup* g = mixer.AddGroup("New Group", m_SelectedGroup);
            if (g)
            {
                SelectGroup(g->Name);
                MarkDirty();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Add child group to the selected group");
        ImGui::Separator();
        DrawGroupTreeNode("Master", 0);
    }

    void AudioMixerPanel::DrawGroupTreeNode(const std::string& name, int depth)
    {
        auto& mixer = AudioMixer::Get();
        MixerGroup* g = mixer.FindGroup(name);
        if (!g || depth > 32)
            return;

        std::vector<std::string> children;
        for (auto& c : mixer.GetGroups())
            if (c.Parent == name)
                children.push_back(c.Name);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_SelectedGroup == name)
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(name.c_str());
        bool openNode;
        if (m_RenamingGroup == name)
        {
            openNode = ImGui::TreeNodeEx("##rn", flags | ImGuiTreeNodeFlags_AllowOverlap, "%s", "");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
                ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                if (mixer.RenameGroup(name, m_RenameBuffer))
                {
                    if (m_SelectedGroup == name)
                        m_SelectedGroup = m_RenameBuffer;
                    MarkDirty();
                }
                m_RenamingGroup.clear();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_RenamingGroup.clear();
        }
        else
        {
            openNode = ImGui::TreeNodeEx("##node", flags, "%s", "");
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                SelectGroup(name);
            ImVec2 min = ImGui::GetItemRectMin();
            float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, min.y + 4.0f), ImVec2(x + 4.0f, min.y + ImGui::GetFontSize()), ToU32(g->Color), 1.0f);
            ImU32 col = ImGui::GetColorU32(g->Mute ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(x + 10.0f, min.y), col, name.c_str());

            if (name != "Master")
            {
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("MIXER_GROUP", name.c_str(), name.size() + 1);
                    ImGui::Text("%s", name.c_str());
                    ImGui::EndDragDropSource();
                }
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MIXER_GROUP"))
                {
                    if (mixer.SetGroupParent((const char*)p->Data, name))
                        MarkDirty();
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("##groupctx"))
            {
                if (ImGui::MenuItem(ICON_FA_PLUS "  Add Child Group"))
                {
                    if (MixerGroup* ng = mixer.AddGroup("New Group", name))
                    {
                        SelectGroup(ng->Name);
                        MarkDirty();
                    }
                }
                if (ImGui::MenuItem(ICON_FA_PEN "  Rename", nullptr, false, name != "Master"))
                {
                    m_RenamingGroup = name;
                    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
                }
                if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", nullptr, false, name != "Master"))
                {
                    MixerGroup copy = *g;
                    if (MixerGroup* ng = mixer.AddGroup(name, g->Parent))
                    {
                        std::string newName = ng->Name;
                        ng->VolumeDb = copy.VolumeDb; ng->Pan = copy.Pan; ng->Pitch = copy.Pitch; ng->Color = copy.Color;
                        for (auto e : copy.Effects) { e.Runtime.reset(); ng->Effects.push_back(e); }
                        mixer.RebuildChain(newName);
                        SelectGroup(newName);
                        MarkDirty();
                    }
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
                if (ImGui::MenuItem(ICON_FA_TRASH "  Delete", nullptr, false, name != "Master"))
                {
                    mixer.RemoveGroup(name);
                    SelectGroup("Master");
                    MarkDirty();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && name != "Master")
            {
                m_RenamingGroup = name;
                snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
            }
        }

        if (openNode)
        {
            for (auto& c : children)
                DrawGroupTreeNode(c, depth + 1);
            if (!(flags & ImGuiTreeNodeFlags_Leaf))
                ImGui::TreePop();
            else
                ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void AudioMixerPanel::DrawSnapshots()
    {
        auto& mixer = AudioMixer::Get();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CAMERA "  Snapshots");
        ImGui::Separator();
        auto& snaps = mixer.GetSnapshots();
        int remove = -1;
        for (int i = 0; i < (int)snaps.size(); ++i)
        {
            ImGui::PushID(i);
            bool sel = i == m_ActiveSnapshot;
            if (ImGui::Selectable(snaps[(size_t)i].Name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_ActiveSnapshot = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    mixer.ApplySnapshot(snaps[(size_t)i].Name, m_SnapshotTransition);
            }
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Apply"))
                    mixer.ApplySnapshot(snaps[(size_t)i].Name, m_SnapshotTransition);
                if (ImGui::MenuItem("Overwrite With Current"))
                {
                    mixer.CaptureSnapshot(snaps[(size_t)i].Name);
                    MarkDirty();
                }
                if (ImGui::MenuItem("Delete"))
                    remove = i;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            snaps.erase(snaps.begin() + remove);
            MarkDirty();
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        ImGui::InputText("##newsnap", m_NewSnapshot, sizeof(m_NewSnapshot));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS "##capture") && m_NewSnapshot[0])
        {
            mixer.CaptureSnapshot(m_NewSnapshot);
            m_ActiveSnapshot = (int)snaps.size() - 1;
            MarkDirty();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Capture current fader levels as a snapshot");
    }

    void AudioMixerPanel::DrawStrips()
    {
        auto& groups = AudioMixer::Get().GetGroups();
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize - 4.0f;
        for (int i = 0; i < (int)groups.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, 6.0f);
            DrawStrip(i, std::max(280.0f, h));
        }
    }

    void AudioMixerPanel::DrawStrip(int index, float height)
    {
        auto& mixer = AudioMixer::Get();
        MixerGroup& g = mixer.GetGroups()[(size_t)index];
        const std::string name = g.Name; // grup listesi bu karede değişebilir
        const bool selected = m_SelectedGroup == name;
        const float W = 112.0f;

        ImGui::PushID(name.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? ImVec4(0.17f, 0.19f, 0.24f, 1.0f) : ImVec4(0.12f, 0.125f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, selected ? UI::Colors::Accent : ImVec4(1, 1, 1, 0.06f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::BeginChild("##strip", ImVec2(W, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float innerW = ImGui::GetContentRegionAvail().x;

        // Başlık
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + innerW, p.y + 22.0f), ToU32(g.Color, 0.85f), 3.0f);
            const char* label = name.c_str();
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->PushClipRect(p, ImVec2(p.x + innerW, p.y + 22.0f), true);
            dl->AddText(ImVec2(p.x + std::max(4.0f, (innerW - ts.x) * 0.5f), p.y + 3.0f), IM_COL32(15, 15, 18, 255), label);
            dl->PopClipRect();
            ImGui::InvisibleButton("##header", ImVec2(innerW, 22.0f));
            if (ImGui::IsItemClicked())
                SelectGroup(name);
            ImGui::TextDisabled("%s %s", ICON_FA_ARROW_RIGHT, g.Parent.empty() ? "Output" : g.Parent.c_str());
        }

        // Ölçer + fader
        const float bottomBlock = 210.0f;
        const float meterH = std::max(90.0f, height - bottomBlock - 50.0f);
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            // dB ölçeği
            const float scaleW = 24.0f;
            static const float ticks[] = { 20, 10, 0, -10, -20, -40, -60, -80 };
            ImFont* font = ImGui::GetFont();
            for (float t : ticks)
            {
                float y = p.y + 6.0f + (1.0f - DbToPos(t)) * (meterH - 12.0f);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", (int)t);
                dl->AddText(font, 10.0f, ImVec2(p.x, y - 6.0f), IM_COL32(150, 150, 160, 200), buf);
                dl->AddLine(ImVec2(p.x + scaleW - 4.0f, y), ImVec2(p.x + scaleW, y), IM_COL32(150, 150, 160, 120));
            }
            auto& rt = *g.RT;
            Meter(ImVec2(p.x + scaleW + 2.0f, p.y + 6.0f), ImVec2(8.0f, meterH - 12.0f), rt.DisplayPeakL, rt.DisplayRmsL, rt.HoldL);
            Meter(ImVec2(p.x + scaleW + 12.0f, p.y + 6.0f), ImVec2(8.0f, meterH - 12.0f), rt.DisplayPeakR, rt.DisplayRmsR, rt.HoldR);
            // Klip göstergesi
            ImVec2 clipMin(p.x + scaleW + 2.0f, p.y - 2.0f);
            dl->AddRectFilled(clipMin, ImVec2(clipMin.x + 18.0f, clipMin.y + 5.0f), rt.Clipped ? IM_COL32(255, 60, 50, 255) : IM_COL32(50, 25, 25, 255), 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(p.x + scaleW + 36.0f, p.y));
            float db = g.VolumeDb;
            if (Fader("##fader", db, ImVec2(innerW - scaleW - 38.0f, meterH)))
            {
                g.VolumeDb = db;
                MarkDirty();
            }
            if (ImGui::IsItemClicked())
                SelectGroup(name);
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + meterH + 4.0f));
        }

        // Ses düzeyi değeri
        ImGui::SetNextItemWidth(innerW);
        if (ImGui::DragFloat("##vol", &g.VolumeDb, 0.1f, -80.0f, 20.0f, g.VolumeDb <= -79.9f ? "-inf dB" : "%.1f dB"))
            MarkDirty();

        // Pan
        {
            const float r = 15.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innerW - r * 2.0f) * 0.5f);
            if (PanKnob("##pan", g.Pan, r))
                MarkDirty();
            char buf[16];
            if (std::fabs(g.Pan) < 0.005f) snprintf(buf, sizeof(buf), "C");
            else snprintf(buf, sizeof(buf), "%s%.0f", g.Pan < 0 ? "L" : "R", std::fabs(g.Pan) * 100.0f);
            float tw = ImGui::CalcTextSize(buf).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innerW - tw) * 0.5f);
            ImGui::TextDisabled("%s", buf);
        }

        // M / S / B
        {
            const float bw = (innerW - 8.0f) / 3.0f;
            auto toggle = [&](const char* label, bool& v, ImVec4 on, const char* tip) {
                ImGui::PushStyleColor(ImGuiCol_Button, v ? on : ImVec4(0.22f, 0.23f, 0.26f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v ? on : ImVec4(0.30f, 0.31f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, v ? ImVec4(0.06f, 0.06f, 0.07f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                bool pressed = ImGui::Button(label, ImVec2(bw, 0));
                ImGui::PopStyleColor(3);
                if (pressed) { v = !v; MarkDirty(); }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
            };
            toggle("M", g.Mute, ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "Mute");
            ImGui::SameLine(0.0f, 4.0f);
            toggle("S", g.Solo, ImVec4(0.98f, 0.80f, 0.25f, 1.0f), "Solo");
            ImGui::SameLine(0.0f, 4.0f);
            bool bypass = g.BypassEffects;
            toggle("B", bypass, ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "Bypass all effects");
            if (bypass != g.BypassEffects)
            {
                g.BypassEffects = bypass;
                mixer.RebuildChain(name);
            }
        }

        // Efekt yuvaları
        ImGui::Spacing();
        int moveUp = -1, removeIdx = -1;
        for (int e = 0; e < (int)g.Effects.size(); ++e)
        {
            auto& fx = g.Effects[(size_t)e];
            ImGui::PushID(e);
            bool fxSelected = selected && m_SelectedEffect == e;
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.45f));
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##fx", fxSelected, 0, ImVec2(innerW, 0)))
            {
                SelectGroup(name);
                m_SelectedEffect = e;
            }
            ImGui::PopStyleColor();
            ImU32 dot = fx.Bypass || g.BypassEffects ? IM_COL32(110, 110, 115, 255) : IM_COL32(90, 220, 120, 255);
            dl->AddCircleFilled(ImVec2(rowMin.x + 6.0f, rowMin.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f), 3.5f, dot);
            dl->AddText(ImVec2(rowMin.x + 14.0f, rowMin.y), ImGui::GetColorU32(fx.Bypass ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                        MixerEffect::GetTypeName(fx.Type));
            if (ImGui::BeginPopupContextItem("##fxctx"))
            {
                if (ImGui::MenuItem(fx.Bypass ? "Enable" : "Bypass")) { fx.Bypass = !fx.Bypass; mixer.RebuildChain(name); MarkDirty(); }
                if (ImGui::MenuItem("Move Up", nullptr, false, e > 0)) moveUp = e;
                if (ImGui::MenuItem("Move Down", nullptr, false, e + 1 < (int)g.Effects.size())) moveUp = e + 1;
                if (ImGui::MenuItem("Remove")) removeIdx = e;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (moveUp > 0)
        {
            std::swap(g.Effects[(size_t)moveUp], g.Effects[(size_t)moveUp - 1]);
            mixer.RebuildChain(name);
            MarkDirty();
        }
        if (removeIdx >= 0)
        {
            g.Effects.erase(g.Effects.begin() + removeIdx);
            mixer.RebuildChain(name);
            m_SelectedEffect = -1;
            MarkDirty();
        }

        ImGui::SetNextItemWidth(innerW);
        if (ImGui::BeginCombo("##addfx", ICON_FA_PLUS " Add Effect", ImGuiComboFlags_NoArrowButton))
        {
            for (int t = 0; t < (int)MixerEffectType::Count; ++t)
            {
                if (ImGui::Selectable(MixerEffect::GetTypeName((MixerEffectType)t)))
                {
                    g.Effects.push_back(MixerEffect::CreateDefault((MixerEffectType)t));
                    mixer.RebuildChain(name);
                    SelectGroup(name);
                    m_SelectedEffect = (int)mixer.FindGroup(name)->Effects.size() - 1;
                    MarkDirty();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    void AudioMixerPanel::DrawInspector()
    {
        auto& mixer = AudioMixer::Get();
        MixerGroup* g = mixer.FindGroup(m_SelectedGroup);
        if (!g)
            return;
        const std::string name = g->Name;

        if (UI::BeginSection("Group", true, ICON_FA_LAYER_GROUP))
        {
            if (name != "Master")
            {
                static char s_NameBuf[64] = {};
                static std::string s_NameFor;
                if (s_NameFor != name)
                {
                    snprintf(s_NameBuf, sizeof(s_NameBuf), "%s", name.c_str());
                    s_NameFor = name;
                }
                UI::BeginProperty("Name", "Press Enter to rename");
                bool commit = ImGui::InputText("##gname", s_NameBuf, sizeof(s_NameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                commit |= ImGui::IsItemDeactivatedAfterEdit();
                UI::EndProperty();
                if (commit && s_NameBuf[0] && name != s_NameBuf && mixer.RenameGroup(name, s_NameBuf))
                {
                    m_SelectedGroup = s_NameBuf;
                    s_NameFor = s_NameBuf;
                    MarkDirty();
                    UI::EndSection();
                    return;
                }
            }
            else
            {
                UI::Text("Name", "Master");
            }
            if (UI::Color3("Color", g->Color)) MarkDirty();

            if (name != "Master")
            {
                UI::BeginProperty("Output");
                if (ImGui::BeginCombo("##output", g->Parent.c_str()))
                {
                    for (auto& other : mixer.GetGroups())
                    {
                        if (other.Name == name || mixer.IsDescendant(other.Name, name))
                            continue;
                        if (ImGui::Selectable(other.Name.c_str(), other.Name == g->Parent))
                        {
                            mixer.SetGroupParent(name, other.Name);
                            MarkDirty();
                        }
                    }
                    ImGui::EndCombo();
                }
                UI::EndProperty();
            }

            g = mixer.FindGroup(name);
            if (UI::SliderFloat("Volume", g->VolumeDb, -80.0f, 20.0f, "%.1f dB")) MarkDirty();
            if (UI::SliderFloat("Pan", g->Pan, -1.0f, 1.0f, "%.2f")) MarkDirty();
            if (UI::SliderFloat("Pitch", g->Pitch, 0.1f, 3.0f, "%.2fx")) MarkDirty();
            if (UI::Checkbox("Mute", g->Mute)) MarkDirty();
            if (UI::Checkbox("Solo", g->Solo)) MarkDirty();
            if (UI::Checkbox("Bypass Effects", g->BypassEffects))
            {
                mixer.RebuildChain(name);
                MarkDirty();
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Metering", true, ICON_FA_CHART_SIMPLE))
        {
            auto& rt = *g->RT;
            UI::Text("Peak", "%s / %s dB", FormatDb(AudioMixer::LinearToDb(rt.DisplayPeakL)).c_str(), FormatDb(AudioMixer::LinearToDb(rt.DisplayPeakR)).c_str());
            UI::Text("RMS", "%s / %s dB", FormatDb(AudioMixer::LinearToDb(rt.DisplayRmsL)).c_str(), FormatDb(AudioMixer::LinearToDb(rt.DisplayRmsR)).c_str());
            if (!g->DuckSource.empty())
                UI::Text("Ducking", "%.1f dB", rt.DuckGainDb);
            UI::BeginProperty("Clip");
            if (rt.Clipped)
            {
                ImGui::TextColored(UI::Colors::Error, "CLIPPED");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset"))
                    rt.Clipped = false;
            }
            else
            {
                ImGui::TextDisabled("OK");
            }
            UI::EndProperty();
            UI::EndSection();
        }

        if (UI::BeginSection("Ducking (Side-chain)", false, ICON_FA_ARROW_DOWN_WIDE_SHORT))
        {
            UI::BeginProperty("Source", "When this group gets louder than the threshold, the selected group is attenuated");
            if (ImGui::BeginCombo("##ducksrc", g->DuckSource.empty() ? "None" : g->DuckSource.c_str()))
            {
                if (ImGui::Selectable("None", g->DuckSource.empty())) { g->DuckSource.clear(); MarkDirty(); }
                for (auto& other : mixer.GetGroups())
                {
                    if (other.Name == name)
                        continue;
                    if (ImGui::Selectable(other.Name.c_str(), other.Name == g->DuckSource)) { g->DuckSource = other.Name; MarkDirty(); }
                }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            ImGui::BeginDisabled(g->DuckSource.empty());
            if (UI::SliderFloat("Threshold", g->DuckThresholdDb, -80.0f, 0.0f, "%.1f dB")) MarkDirty();
            if (UI::SliderFloat("Attenuation", g->DuckAmountDb, -60.0f, 0.0f, "%.1f dB")) MarkDirty();
            if (UI::SliderFloat("Attack", g->DuckAttack, 0.001f, 2.0f, "%.3f s")) MarkDirty();
            if (UI::SliderFloat("Release", g->DuckRelease, 0.01f, 5.0f, "%.2f s")) MarkDirty();
            ImGui::EndDisabled();
            UI::EndSection();
        }

        if (UI::BeginSection("Effects", true, ICON_FA_WAND_MAGIC_SPARKLES))
        {
            if (g->Effects.empty())
                ImGui::TextDisabled("No effects. Use \"Add Effect\" on the strip.");
            for (int e = 0; e < (int)g->Effects.size(); ++e)
            {
                auto& fx = g->Effects[(size_t)e];
                ImGui::PushID(e);
                bool open = ImGui::CollapsingHeader(MixerEffect::GetTypeName(fx.Type), m_SelectedEffect == e ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (m_SelectedEffect == e && ImGui::IsWindowAppearing())
                    ImGui::SetScrollHereY();
                if (open)
                {
                    bool bypass = fx.Bypass;
                    if (UI::Checkbox("Bypass", bypass))
                    {
                        fx.Bypass = bypass;
                        mixer.RebuildChain(name);
                        MarkDirty();
                    }
                    for (int p = 0; p < MixerEffect::GetParamCount(fx.Type); ++p)
                    {
                        float mn, mx;
                        const char* fmt;
                        bool lg;
                        MixerEffect::GetParamRange(fx.Type, p, mn, mx, fmt, lg);
                        UI::BeginProperty(MixerEffect::GetParamName(fx.Type, p));
                        ImGui::PushID(p);
                        if (ImGui::SliderFloat("##p", &fx.Params[p], mn, mx, fmt, lg ? ImGuiSliderFlags_Logarithmic : 0))
                        {
                            mixer.UpdateEffectParams(name);
                            MarkDirty();
                        }
                        ImGui::PopID();
                        UI::EndProperty();
                    }
                    if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT " Defaults"))
                    {
                        MixerEffect def = MixerEffect::CreateDefault(fx.Type);
                        std::copy(std::begin(def.Params), std::end(def.Params), std::begin(fx.Params));
                        mixer.UpdateEffectParams(name);
                        MarkDirty();
                    }
                }
                ImGui::PopID();
            }
            UI::EndSection();
        }
    }
}
