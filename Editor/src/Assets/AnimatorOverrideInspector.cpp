#include "AnimatorOverrideInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Animation/AnimationController.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <map>

namespace Conqueror::Editor::AnimatorOverrideInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        struct OverrideData
        {
            std::string Controller;                     // proje göreli .cqcont
            std::string ClipsFrom;                      // proje göreli model (açılır listedeki klip adları)
            std::map<std::string, std::string> Map;     // özgün klip adı -> yerine geçen
        };

        bool Read(const std::string& path, OverrideData& out, std::string& error)
        {
            out = {};
            try
            {
                YAML::Node root = YAML::LoadFile(path);
                YAML::Node o = root["AnimatorOverrideController"];
                if (!o)
                {
                    error = "not an Animator Override Controller file";
                    return false;
                }
                out.Controller = o["Controller"].as<std::string>("");
                out.ClipsFrom = o["ClipsFrom"].as<std::string>("");
                if (auto list = o["Overrides"]; list && list.IsSequence())
                    for (const auto& item : list)
                    {
                        const std::string from = item["Original"].as<std::string>(""), to = item["Override"].as<std::string>("");
                        if (!from.empty() && !to.empty())
                            out.Map[from] = to;
                    }
                return true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                return false;
            }
        }

        std::string ToText(const OverrideData& d)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "AnimatorOverrideController" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Controller" << YAML::Value << d.Controller;
            out << YAML::Key << "ClipsFrom" << YAML::Value << d.ClipsFrom;
            out << YAML::Key << "Overrides" << YAML::Value << YAML::BeginSeq;
            for (const auto& [from, to] : d.Map)
                out << YAML::BeginMap << YAML::Key << "Original" << YAML::Value << from << YAML::Key << "Override" << YAML::Value << to << YAML::EndMap;
            out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
            return std::string("# Conqueror's Engine - Animator Override Controller\n") + out.c_str() + "\n";
        }

        // Düzenleme (dosyanın önceki / sonraki metni, Ctrl+Z ile geri alınır)
        void Save(const std::string& path, const OverrideData& d)
        {
            const std::string before = AssetFileCommand::ReadFile(path);
            const std::string after = ToText(d);
            if (before == after)
                return;
            UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(path, before, after, "Edit " + fs::path(path).filename().string()));
        }
    }

    std::string DefaultContent()
    {
        return ToText(OverrideData{});
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path, s_Error;
        static fs::file_time_type s_Time{};
        static OverrideData s_Data;
        static bool s_Loaded = false;
        // Temel controller'ın klip adları (dosya değişince yeniden)
        static std::string s_BasePath;
        static fs::file_time_type s_BaseTime{};
        static std::vector<std::string> s_BaseClips;

        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Loaded = Read(path, s_Data, s_Error);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_PERSON_RUNNING);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Animator Override Controller");
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The file could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }

        OverrideData edit = s_Data;
        bool changed = false;
        if (AssetUI::BeginProperties("##ovprops"))
        {
            AssetUI::Row("Controller");
            changed |= AssetUI::AssetSlot("##ovcontroller", "Animator Controller", { ".cqcont" }, edit.Controller);
            AssetUI::Row("Clips From");
            changed |= AssetUI::AssetSlot("##ovmodel", "Model", { ".glb", ".gltf", ".fbx", ".obj", ".dae" }, edit.ClipsFrom);
            AssetUI::EndProperties();
        }

        // Temel controller'daki klipler
        const std::string base = edit.Controller.empty() ? std::string() : AssetUI::Resolve(edit.Controller).string();
        const auto baseTime = base.empty() ? fs::file_time_type{} : fs::last_write_time(base, ec);
        if (base != s_BasePath || baseTime != s_BaseTime)
        {
            s_BasePath = base;
            s_BaseTime = baseTime;
            s_BaseClips.clear();
            if (!base.empty())
                if (auto c = AnimationController::Deserialize(base))
                    s_BaseClips = c->CollectClipNames();
        }
        // Açılır listedeki klipler: seçilen model
        std::vector<std::string> available;
        if (!edit.ClipsFrom.empty())
            if (auto model = ModelLoader::Load(AssetUI::Resolve(edit.ClipsFrom).string()))
                for (const auto& clip : model->Animations)
                    if (clip)
                        available.push_back(clip->Name);

        ImGui::Spacing();
        if (edit.Controller.empty())
            UI::InfoBox("Choose the Animator Controller whose clips this file replaces. States, transitions and parameters come from it.",
                        UI::InfoType::Info);
        else if (s_BaseClips.empty())
            UI::InfoBox("The controller has no clips.", UI::InfoType::Warning);
        else if (ImGui::BeginTable("##overrides", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Original");
            ImGui::TableSetupColumn("Override");
            ImGui::TableHeadersRow();
            for (const auto& clip : s_BaseClips)
            {
                ImGui::PushID(clip.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(clip.c_str());
                ImGui::TableSetColumnIndex(1);
                auto it = edit.Map.find(clip);
                const std::string current = it == edit.Map.end() ? std::string() : it->second;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##override", current.empty() ? "None (keeps the original)" : current.c_str()))
                {
                    if (ImGui::Selectable("None (keeps the original)", current.empty()))
                    {
                        edit.Map.erase(clip);
                        changed = true;
                    }
                    for (const auto& name : available)
                        if (ImGui::Selectable(name.c_str(), name == current))
                        {
                            edit.Map[clip] = name;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (available.empty())
                UI::InfoBox("Set Clips From to a model to list the clips you can choose.", UI::InfoType::Info);
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Use it in the Animation component's Controller slot instead of the Animator Controller.");

        if (changed)
        {
            Save(path, edit);
            s_Data = edit;
            s_Time = fs::last_write_time(path, ec);
        }
    }
}
