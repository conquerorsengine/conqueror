#include "AudioAssetsInspector.h"

#include "Assets/AssetUI.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Audio/AudioRandomContainer.h"
#include "Core/Audio/InteractiveMusic.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/Serialization/SceneSerializer.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Conqueror::Editor::AudioAssetsInspector
{
    namespace fs = std::filesystem;
    static std::function<void()> s_OpenMixer;
    void SetOpenMixerWindow(std::function<void()> open) { s_OpenMixer = std::move(open); }

    void DrawMixer(const std::string& path)
    {
        auto& mixer = AudioMixer::Get();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_SLIDERS);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Audio Mixer");
        ImGui::Separator();
        const std::string key = fs::weakly_canonical(path).string();
        const std::string prefix = AudioMixer::AssetPrefix(path) + "/";
        MixerGroup* master = mixer.FindGroup(prefix + "Master");
        if (!master)
        {
            if (mixer.IsInitialized() && ImGui::Button(ICON_FA_ROTATE "  Load"))
                mixer.LoadAssetMixer(path);
            ImGui::TextDisabled("Not loaded (the audio device may be off).");
            return;
        }
        // Çıkış: bu mikserin Master'ı hangi gruba akar (Unity: Output)
        UI::BeginProperty("Output", "The group this mixer's Master flows into (another mixer's group or the project Master).");
        if (ImGui::BeginCombo("##output", master->Parent.c_str()))
        {
            for (const auto& g : mixer.GetGroups())
                if (g.Asset != key && ImGui::Selectable(g.Name.c_str(), g.Name == master->Parent))
                {
                    mixer.SetGroupParent(master->Name, g.Name);
                    mixer.SaveAssetMixer(path);
                }
            ImGui::EndCombo();
        }
        UI::EndProperty();
        UI::SubHeader("Groups");
        for (const auto& g : mixer.GetGroups())
            if (g.Asset == key)
                UI::Text(g.Name.substr(prefix.size()).c_str(), "%.1f dB%s", g.VolumeDb, g.Mute ? "  (muted)" : "");
        ImGui::Spacing();
        if (s_OpenMixer && ImGui::Button(ICON_FA_SLIDERS "  Open Audio Mixer"))
            s_OpenMixer();
        ImGui::Spacing();
        ImGui::TextWrapped("Its groups appear as \"%s<group>\" in every Audio Source's Output list. Edit groups, effects and "
                           "snapshots in the Audio Mixer window; the changes are saved into this file.", prefix.c_str());
    }

    void DrawRandomContainer(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_SHUFFLE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Audio Random Container");
        ImGui::Separator();
        AudioRandomContainerAsset a;
        if (!AudioRandomContainers::Load(path, a))
        {
            UI::InfoBox("The container could not be read.", UI::InfoType::Error);
            return;
        }
        bool c = false;
        UI::SubHeader("Clips");
        int remove = -1;
        static const std::vector<std::string> clipExts = { ".wav", ".mp3", ".ogg", ".flac" };
        for (int i = 0; i < (int)a.Clips.size(); ++i)
        {
            ImGui::PushID(i);
            char label[24];
            snprintf(label, sizeof(label), "Clip %d", i);
            UI::BeginProperty(label);
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::SameLine();
            c |= AssetUI::AssetSlot("clip", "Audio Clip", clipExts, a.Clips[(size_t)i]);
            UI::EndProperty();
            ImGui::PopID();
        }
        if (remove >= 0) { a.Clips.erase(a.Clips.begin() + remove); c = true; }
        std::string add;
        UI::BeginProperty("Add Clip");
        if (AssetUI::AssetSlot("addclip", "Audio Clip", clipExts, add) && !add.empty()) { a.Clips.push_back(add); c = true; }
        UI::EndProperty();
        UI::SubHeader("Volume & Pitch");
        c |= UI::DragFloat("Volume", a.VolumeDb, 0.1f, -80.0f, 20.0f, "%.1f dB");
        c |= UI::DragFloat("Volume Randomization", a.VolumeRandomDb, 0.1f, 0.0f, 40.0f, "+- %.1f dB");
        c |= UI::DragFloat("Pitch", a.PitchSemitones, 0.1f, -24.0f, 24.0f, "%.1f semitones");
        c |= UI::DragFloat("Pitch Randomization", a.PitchRandomSemitones, 0.05f, 0.0f, 12.0f, "+- %.2f semitones");
        UI::SubHeader("Playlist");
        const char* modes[] = { "Sequential", "Shuffle", "Random" };
        c |= UI::Combo("Playback Mode", a.Mode, modes, 3, "Sequential: in order. Shuffle: each clip once per round. Random: any clip, avoiding the last ones.");
        if (a.Mode == 2)
            c |= UI::SliderInt("Avoid Repeating Last", a.AvoidRepeat, 0, 8);
        UI::SubHeader("Trigger");
        const char* triggers[] = { "Manual (each Play)", "Automatic" };
        c |= UI::Combo("Trigger", a.Trigger, triggers, 2);
        if (a.Trigger == 1)
        {
            c |= UI::DragFloat("Interval", a.Interval, 0.01f, 0.01f, 60.0f, "%.2f s");
            c |= UI::DragFloat("Interval Randomization", a.IntervalRandom, 0.01f, 0.0f, 30.0f, "+- %.2f s");
            c |= UI::DragInt("Count", a.Count, 0.1f, 0, 1000, "0 = until stopped");
        }
        if (c)
            AudioRandomContainers::Save(path, a);
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_PLAY "  Play Preview"))
        {
            AudioRandomContainers::Pick pick;
            if (AudioRandomContainers::Next(path, 0xC0FFEEull, pick))
                AudioOneShots::Play(SceneSerializer::ResolveAssetPath(pick.Clip), pick.VolumeScale, pick.PitchScale, 0.0f, "Master");
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Put it in an Audio Source's Clip slot. Every Play picks a clip; Automatic keeps playing at the interval.");
    }
    namespace
    {
        const std::vector<std::string>& StreamExtensions()
        {
            static const std::vector<std::string> exts = { ".wav", ".mp3", ".ogg", ".flac", InteractiveMusic::PlaylistExtension,
                                                           InteractiveMusic::SynchronizedExtension, InteractiveMusic::InteractiveExtension };
            return exts;
        }

        // Parça / katman / klip: akış (ses dosyası ya da iç içe müzik varlığı), döngü, ritim
        bool DrawClipFields(MusicClip& c, bool volume)
        {
            bool changed = false;
            UI::BeginProperty("Stream", "An audio file or another music asset (nested, e.g. a synchronized track inside an interactive clip).");
            changed |= AssetUI::AssetSlot("stream", "Audio Stream", StreamExtensions(), c.Stream);
            UI::EndProperty();
            if (volume)
                changed |= UI::DragFloat("Volume", c.VolumeDb, 0.1f, -80.0f, 24.0f, "%.1f dB", "Layer volume at start; scripts change it with MusicSetLayerVolume.");
            changed |= UI::Checkbox("Loop", c.Loop, "The file starts over at its end.");
            changed |= UI::DragFloat("BPM", c.Bpm, 0.1f, 0.0f, 400.0f, "%.1f", "Beats per minute for beat / bar synced transitions (0: unknown, 120 is used).");
            changed |= UI::DragInt("Beat Count", c.BeatCount, 0.1f, 0, 100000, "Musical length in beats (0: the file length). Audio after it is a tail that fades during the next track.");
            changed |= UI::DragInt("Bar Beats", c.BarBeats, 0.05f, 1, 64, "Beats in a bar (Next Bar transitions).");
            return changed;
        }
    }

    void DrawMusic(const std::string& path)
    {
        MusicAsset a;
        const bool loaded = InteractiveMusic::Load(path, a);
        ImGui::TextColored(UI::Colors::Accent, a.Kind == MusicAsset::Type::Interactive ? ICON_FA_DIAGRAM_PROJECT
                                              : a.Kind == MusicAsset::Type::Synchronized ? ICON_FA_LAYER_GROUP : ICON_FA_LIST_OL);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", InteractiveMusic::TypeName(a.Kind));
        ImGui::Separator();
        if (!loaded)
        {
            UI::InfoBox("The music asset could not be read.", UI::InfoType::Error);
            return;
        }
        bool c = false;
        if (a.Kind == MusicAsset::Type::Playlist || a.Kind == MusicAsset::Type::Synchronized)
        {
            const bool sync = a.Kind == MusicAsset::Type::Synchronized;
            UI::SubHeader(sync ? "Layers" : "Tracks");
            int remove = -1;
            for (int i = 0; i < (int)a.Clips.size(); ++i)
            {
                ImGui::PushID(i);
                char title[48];
                snprintf(title, sizeof(title), "%s %d  %s", sync ? "Layer" : "Track", i, fs::path(a.Clips[(size_t)i].Stream).filename().string().c_str());
                if (ImGui::TreeNodeEx("##clip", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", title))
                {
                    c |= DrawClipFields(a.Clips[(size_t)i], sync);
                    if (ImGui::SmallButton(ICON_FA_XMARK "  Remove")) remove = i;
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (remove >= 0) { a.Clips.erase(a.Clips.begin() + remove); c = true; }
            std::string add;
            UI::BeginProperty(sync ? "Add Layer" : "Add Track");
            if (AssetUI::AssetSlot("add", "Audio Stream", StreamExtensions(), add) && !add.empty())
            {
                MusicClip clip;
                clip.Stream = add;
                a.Clips.push_back(clip);
                c = true;
            }
            UI::EndProperty();
            if (!sync)
            {
                UI::SubHeader("Playback");
                c |= UI::Checkbox("Shuffle", a.Shuffle, "Random order each round (the same track never plays twice in a row).");
                c |= UI::Checkbox("Loop", a.Loop, "Start over after the last track.");
                c |= UI::DragFloat("Fade Time", a.FadeTime, 0.01f, 0.0f, 30.0f, "%.2f s", "The track that ends fades out over this time while the next one starts.");
            }
        }
        else
        {
            std::vector<std::string> names;
            for (const auto& ic : a.InteractiveClips) names.push_back(ic.Name);
            std::vector<const char*> items;
            for (const auto& n : names) items.push_back(n.c_str());
            std::vector<const char*> withAny = { "Any" };
            withAny.insert(withAny.end(), items.begin(), items.end());
            std::vector<const char*> withNone = { "None" };
            withNone.insert(withNone.end(), items.begin(), items.end());

            UI::SubHeader("Clips");
            if (!items.empty())
                c |= UI::Combo("Initial Clip", a.InitialClip, items.data(), (int)items.size(), "The clip that plays first.");
            int remove = -1;
            for (int i = 0; i < (int)a.InteractiveClips.size(); ++i)
            {
                auto& ic = a.InteractiveClips[(size_t)i];
                ImGui::PushID(i);
                if (ImGui::TreeNodeEx("##iclip", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%d  %s", i, ic.Name.c_str()))
                {
                    c |= UI::InputText("Name", ic.Name, "Scripts switch with MusicSwitchToClip(\"Name\").");
                    c |= DrawClipFields(ic.Clip, false);
                    const char* advance[] = { "Disabled", "Enabled", "Return To Hold" };
                    c |= UI::Combo("Auto Advance", ic.AutoAdvance, advance, 3,
                                   "At the clip's end: Disabled stays (loops if Loop is on), Enabled goes to Next Clip, Return To Hold goes back to the held clip.");
                    if (ic.AutoAdvance == 1)
                    {
                        int next = ic.NextClip + 1;
                        if (UI::Combo("Next Clip", next, withNone.data(), (int)withNone.size())) { ic.NextClip = next - 1; c = true; }
                    }
                    if (ImGui::SmallButton(ICON_FA_XMARK "  Remove")) remove = i;
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (remove >= 0)
            {
                a.InteractiveClips.erase(a.InteractiveClips.begin() + remove);
                // Dizinleri kaydır (silinen klibe giden geçişler kalkar)
                auto fix = [&](int& v) { if (v == remove) v = -2; else if (v > remove) --v; };
                for (auto& ic : a.InteractiveClips) { fix(ic.NextClip); if (ic.NextClip == -2) ic.NextClip = -1; }
                for (auto& t : a.Transitions) { fix(t.From); fix(t.To); fix(t.FillerClip); if (t.FillerClip == -2) { t.FillerClip = -1; t.UseFiller = false; } }
                a.Transitions.erase(std::remove_if(a.Transitions.begin(), a.Transitions.end(), [](const MusicTransition& t) { return t.From == -2 || t.To == -2; }), a.Transitions.end());
                a.InitialClip = std::clamp(a.InitialClip, 0, std::max(0, (int)a.InteractiveClips.size() - 1));
                c = true;
            }
            if (ImGui::Button(ICON_FA_PLUS "  Add Clip"))
            {
                MusicInteractiveClip ic;
                ic.Name = "Clip " + std::to_string(a.InteractiveClips.size());
                a.InteractiveClips.push_back(ic);
                c = true;
            }

            UI::SubHeader("Transitions");
            ImGui::TextDisabled("Most specific wins: From -> To, From -> Any, Any -> To, Any -> Any. Without one the switch is immediate.");
            int removeT = -1;
            for (int i = 0; i < (int)a.Transitions.size(); ++i)
            {
                auto& t = a.Transitions[(size_t)i];
                ImGui::PushID(1000 + i);
                const char* fromName = t.From >= 0 && t.From < (int)items.size() ? items[(size_t)t.From] : "Any";
                const char* toName = t.To >= 0 && t.To < (int)items.size() ? items[(size_t)t.To] : "Any";
                if (ImGui::TreeNodeEx("##tr", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s  " ICON_FA_ARROW_RIGHT "  %s", fromName, toName))
                {
                    int from = t.From + 1, to = t.To + 1;
                    if (UI::Combo("From", from, withAny.data(), (int)withAny.size())) { t.From = from - 1; c = true; }
                    if (UI::Combo("To", to, withAny.data(), (int)withAny.size())) { t.To = to - 1; c = true; }
                    const char* fromTimes[] = { "Immediate", "Next Beat", "Next Bar", "End" };
                    c |= UI::Combo("From Time", t.FromTime, fromTimes, 4, "When the switch happens in the playing clip.");
                    const char* toTimes[] = { "Same Position", "Start" };
                    c |= UI::Combo("To Time", t.ToTime, toTimes, 2, "Where the next clip starts.");
                    const char* fades[] = { "Disabled", "Fade In", "Fade Out", "Cross", "Automatic" };
                    c |= UI::Combo("Fade Mode", t.FadeMode, fades, 5, "Automatic: cross-fade to the same position, otherwise fade the old clip out.");
                    if (t.FadeMode != 0)
                        c |= UI::DragFloat("Fade Beats", t.FadeBeats, 0.05f, 0.0f, 64.0f, "%.2f", "Fade length in beats of the playing clip.");
                    c |= UI::Checkbox("Use Filler Clip", t.UseFiller, "Play a filler clip first; the destination starts when it ends.");
                    if (t.UseFiller && !items.empty())
                    {
                        int filler = std::max(0, t.FillerClip);
                        if (UI::Combo("Filler Clip", filler, items.data(), (int)items.size()) || t.FillerClip < 0) { t.FillerClip = filler; c = true; }
                    }
                    c |= UI::Checkbox("Hold Previous", t.HoldPrevious, "Remember the clip being left; a clip with Auto Advance = Return To Hold goes back to where it was.");
                    if (ImGui::SmallButton(ICON_FA_XMARK "  Remove")) removeT = i;
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (removeT >= 0) { a.Transitions.erase(a.Transitions.begin() + removeT); c = true; }
            if (ImGui::Button(ICON_FA_PLUS "  Add Transition"))
            {
                a.Transitions.push_back(MusicTransition{});
                c = true;
            }
        }
        if (c)
            InteractiveMusic::Save(path, a);

        // Önizleme (editörde, sahneden bağımsız)
        UI::SubHeader("Preview");
        const bool playing = InteractiveMusic::PreviewIsPlaying();
        if (ImGui::Button(playing ? ICON_FA_STOP "  Stop" : ICON_FA_PLAY "  Play Preview"))
        {
            if (playing) InteractiveMusic::PreviewStop();
            else InteractiveMusic::PreviewPlay(path);
        }
        if (playing && a.Kind == MusicAsset::Type::Interactive)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Playing: %s", InteractiveMusic::PreviewCurrentClip().c_str());
            for (int i = 0; i < (int)a.InteractiveClips.size(); ++i)
            {
                if (i > 0) ImGui::SameLine();
                ImGui::PushID(2000 + i);
                if (ImGui::SmallButton(a.InteractiveClips[(size_t)i].Name.c_str()))
                    InteractiveMusic::PreviewSwitch(a.InteractiveClips[(size_t)i].Name);
                ImGui::PopID();
            }
        }
        if (playing && a.Kind == MusicAsset::Type::Synchronized)
        {
            static std::vector<float> s_Layer;
            s_Layer.resize(a.Clips.size(), 0.0f);
            for (int i = 0; i < (int)a.Clips.size(); ++i)
            {
                ImGui::PushID(3000 + i);
                char label[24];
                snprintf(label, sizeof(label), "Layer %d", i);
                if (UI::SliderFloat(label, s_Layer[(size_t)i], -80.0f, 6.0f, "%.1f dB"))
                    InteractiveMusic::PreviewSetLayerVolume(i, s_Layer[(size_t)i]);
                ImGui::PopID();
            }
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Put it in an Audio Source's Clip slot (Godot: AudioStreamPlayer stream). Unity has no built-in equivalent "
                           "(FMOD / Wwise); Unreal builds it with Quartz and MetaSounds.");
    }
}
