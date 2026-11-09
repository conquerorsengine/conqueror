#include "PlayableAssetInspector.h"

#include "Assets/AssetUI.h"
#include "Assets/ScriptFieldUI.h"
#include "Scripting/DataAssets.h"
#include "Scripting/ScriptRuntime.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>

namespace Conqueror::Editor::PlayableAssetInspector
{
    namespace fs = std::filesystem;

    bool IsPlayable(const std::string& path)
    {
        return fs::path(path).extension() == ".cqplayable";
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time;
        static DataAssets::DataAssetFile s_File;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_File = {};
            DataAssets::Load(path, s_File);
        }
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_FILE_CODE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Playable Asset");
        ImGui::Separator();
        std::vector<std::string> exts;
        for (auto* runtime : Scripting::ScriptRuntimes::All())
            if (std::string(runtime->GetLanguageId()) != "stategraph")
                for (const auto& e : runtime->GetExtensions())
                    exts.push_back(e);
        bool changed = false;
        UI::BeginProperty("Behaviour", "A script with OnClipStart, OnClipUpdate(time, progress) and OnClipStop.");
        changed |= AssetUI::AssetSlot("##behaviour", "Script", exts, s_File.Script);
        UI::EndProperty();
        if (!s_File.Script.empty())
        {
            const std::string file = AssetUI::Resolve(s_File.Script).string();
            if (auto* runtime = Scripting::ScriptRuntimes::ForPath(file))
            {
                std::vector<Scripting::ScriptFieldInfo> fields;
                std::string error;
                if (runtime->GetFields(file, runtime->GetDefaultClass(file), fields, &error) && !fields.empty())
                {
                    UI::SubHeader("Fields");
                    changed |= ScriptFieldUI::DrawFieldTable("##playable", fields, s_File.Fields);
                }
            }
        }
        if (changed)
        {
            DataAssets::Save(path, s_File);
            s_Time = fs::last_write_time(path, ec);
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Drop this asset on a Playable track in the Timeline window. Each clip creates its own copy of the behaviour.");
    }
}
