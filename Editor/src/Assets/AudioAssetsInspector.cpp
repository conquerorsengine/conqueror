#include "AudioAssetsInspector.h"

#include "Assets/AssetUI.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Audio/AudioRandomContainer.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/Serialization/SceneSerializer.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

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
}
