#include "AnimationClipInspector.h"

#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Animation/PropertyAnimation.h"
#include "Scene/ComponentRegistry.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Conqueror::Editor::AnimationClipInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;

        void Commit(bool force)
        {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after),
                                                                                    "Edit " + fs::path(s_Edit.Path).filename().string()));
        }

        std::string Seconds(float t, float fps)
        {
            const int f = std::max(1, (int)std::lround(fps));
            const long frames = std::lround(std::max(0.0f, t) * (float)f);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%ld:%02ld (%.2f s)", frames / f, frames % f, t);
            return buf;
        }
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time{};
        static PropertyClip s_Clip;
        static bool s_Loaded = false;
        static std::string s_Error;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Edit.Open && s_Edit.Path != path)
            Commit(true);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Clip = PropertyClip{};
            s_Loaded = PropertyClips::Load(path, s_Clip, &s_Error);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CLAPPERBOARD);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Animation Clip");
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The clip could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }
        if (ImGui::Button(ICON_FA_CLAPPERBOARD "  Open Animation Window##ClipOpenWindow", ImVec2(-1, 0)) && s_OpenWindow)
            s_OpenWindow(path);

        bool changed = false;
        UI::BeginProperty("Length", "Clip length in seconds: Loop wraps here, Once stops here. Keys past it extend it.");
        float length = s_Clip.Length;
        if (ImGui::DragFloat("##ClipLength", &length, 0.01f, 1.0f / std::max(1.0f, s_Clip.FrameRate), 3600.0f, "%.3f s"))
        {
            s_Clip.Length = std::max(length, 1.0f / std::max(1.0f, s_Clip.FrameRate));
            changed = true;
        }
        UI::EndProperty();
        UI::BeginProperty("Samples", "Frames per second: keys snap to these frames in the Animation window.");
        int fps = (int)std::lround(s_Clip.FrameRate);
        if (ImGui::InputInt("##ClipSamples", &fps, 1, 10))
        {
            s_Clip.FrameRate = (float)std::clamp(fps, 1, 240);
            changed = true;
        }
        UI::EndProperty();
        UI::BeginProperty("Loop Time", "Play again from the start when the clip ends (the Animation component's Wrap Mode 'Default' uses this).");
        if (ImGui::Checkbox("##ClipLoop", &s_Clip.Loop))
            changed = true;
        UI::EndProperty();
        ImGui::TextDisabled("%d curves, %d events, last key at %s", (int)s_Clip.Curves.size(), (int)s_Clip.Events.size(),
                            Seconds(s_Clip.ContentEnd(), s_Clip.FrameRate).c_str());

        if (ImGui::CollapsingHeader("Curves", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (s_Clip.Curves.empty())
                ImGui::TextDisabled("No properties yet. Open the Animation window and use Add Property or Record.");
            else if (ImGui::BeginTable("##ClipCurves", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Object");
                ImGui::TableSetupColumn("Property");
                ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 44.0f);
                ImGui::TableHeadersRow();
                for (const auto& c : s_Clip.Curves)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(c.Path.empty() ? "(root)" : c.Path.c_str());
                    ImGui::TableNextColumn();
                    std::string prop = c.Field;
                    if (c.Component != "Transform")
                    {
                        ComponentInfo* info = ComponentRegistry::Find(c.Component);
                        prop = (info ? info->Name : c.Component) + "." + c.Field;
                    }
                    ImGui::TextUnformatted(prop.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", (int)c.Channel.Keys.size());
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (s_Clip.Events.empty())
                ImGui::TextDisabled("No Animation Events.");
            else if (ImGui::BeginTable("##ClipEvents", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Function");
                ImGui::TableSetupColumn("String");
                ImGui::TableHeadersRow();
                for (const auto& e : s_Clip.Events)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Seconds(e.Time, s_Clip.FrameRate).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.Function.empty() ? "(none)" : e.Function.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.Argument.c_str());
                }
                ImGui::EndTable();
            }
        }

        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            PropertyClips::Save(path, s_Clip);
            s_Time = fs::last_write_time(path, ec);
            if (s_ClipChanged)
                s_ClipChanged();
        }
        Commit(false);
        ImGui::Spacing();
        ImGui::TextWrapped("Play it with an Animation component (Add Component > Animation), or from a script: AnimationPlay(entity, \"%s\").",
                           fs::path(path).stem().string().c_str());
    }
}
