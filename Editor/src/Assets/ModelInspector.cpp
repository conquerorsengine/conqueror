#include "ModelInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ObjectPicker.h"
#include "UI/EditorUI.h"

#include "Core/Animation/AnimationClip.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Conqueror::Editor::ModelInspector
{
    namespace fs = std::filesystem;

    bool IsModel(const std::string& path)
    {
        static const char* exts[] = { ".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".blend", ".ply", ".stl" };
        std::string e = fs::path(path).extension().string();
        for (char& c : e)
            c = (char)std::tolower((unsigned char)c);
        return std::any_of(std::begin(exts), std::end(exts), [&](const char* x) { return e == x; });
    }

    void Draw(const std::string& path)
    {
        struct State
        {
            std::string Path;
            fs::file_time_type Time{}, MetaTime{};
            std::vector<std::string> Names;
            std::vector<std::pair<std::string, std::string>> Remap;
            std::string Status;
        };
        static State st;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        const auto metaTime = fs::last_write_time(path + ".meta", ec);
        if (st.Path != path || st.Time != time || st.MetaTime != metaTime)
        {
            if (st.Path != path)
                st.Status.clear();
            st.Path = path;
            st.Time = time;
            st.MetaTime = metaTime;
            st.Names = ModelLoader::MaterialNames(path);
            st.Remap = ModelLoader::ReadMaterialRemap(path);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CUBE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Model");
        ImGui::Separator();

        auto model = ModelLoader::Load(path);
        if (!model)
        {
            UI::InfoBox("The model could not be read (see the Console).", UI::InfoType::Error);
            return;
        }
        UI::SubHeader("Model");
        UI::Text("Meshes", "%d static, %d skinned", (int)model->Meshes.size(), (int)model->SkinnedMeshes.size());
        UI::Text("Rig", "%s", model->IsSkinned ? "Skinned (has a skeleton)" : "None");

        // Malzemeler (Unity Materials sekmesi)
        UI::SubHeader("Materials");
        const bool readOnly = ContentBrowserPanel::IsReadOnly(path);
        auto reloadScene = [&]() {
            if (ContentBrowserPanel::s_ReloadModelInScene)
                ContentBrowserPanel::s_ReloadModelInScene(path);
        };
        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button(ICON_FA_FILE_EXPORT "  Extract Materials...", ImVec2(-1.0f, 0.0f)))
        {
            std::vector<std::string> created;
            std::string error;
            const fs::path outDir = fs::path(path).parent_path() / "Materials";
            if (ModelLoader::ExtractMaterials(path, outDir.string(), created, &error))
            {
                st.Status = "Extracted " + std::to_string(created.size()) + " file(s) to " + AssetUI::ToProjectRelative(outDir) + ".";
                reloadScene();
            }
            else
                st.Status = "Extract failed: " + error;
            st.Path.clear();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Writes every material (and embedded texture) as a .cqmat next to the model and remaps the model to them;\n"
                              "editing a .cqmat then changes every instance of the model.");
        if (!st.Status.empty())
            ImGui::TextWrapped("%s", st.Status.c_str());

        ImGui::TextDisabled("Remapped Materials");
        bool changed = false;
        for (const auto& name : st.Names)
        {
            std::string current;
            for (const auto& [n, file] : st.Remap)
                if (n == name)
                    current = file;
            ImGui::PushID(name.c_str());
            UI::BeginProperty(name.c_str(), "Use this material asset instead of the one inside the model (None: the model's own).");
            const float h = ImGui::GetFrameHeight();
            const std::string shown = current.empty() ? std::string("None (embedded)") : fs::path(current).stem().string();
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            ImGui::Button((std::string(ICON_FA_CIRCLE_HALF_STROKE "  ") + shown + "##slot").c_str(),
                          ImVec2(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::PopStyleVar();
            std::string picked;
            bool set = false;
            if (!readOnly && ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                    if (fs::path(dropped).extension() == ".cqmat")
                    {
                        picked = dropped;
                        set = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(readOnly);
            if (ObjectPicker::Button("pick", "Material", { ".cqmat" }, current.empty() ? current : SceneSerializer::ResolveAssetPath(current), picked))
                set = true;
            ImGui::EndDisabled();
            UI::EndProperty();
            if (set)
            {
                bool found = false;
                const std::string value = picked.empty() ? std::string() : SceneSerializer::ToAssetPath(picked);
                for (auto& [n, file] : st.Remap)
                    if (n == name)
                    {
                        file = value;
                        found = true;
                    }
                if (!found)
                    st.Remap.push_back({ name, value });
                changed = true;
            }
            ImGui::PopID();
        }
        if (changed)
        {
            ModelLoader::WriteMaterialRemap(path, st.Remap);
            reloadScene();
            st.Path.clear();
        }
        bool anyRemap = false;
        for (const auto& [n, file] : st.Remap)
            anyRemap |= !file.empty();
        ImGui::BeginDisabled(!anyRemap || readOnly);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Use Embedded Materials", ImVec2(-1.0f, 0.0f)))
        {
            ModelLoader::WriteMaterialRemap(path, {});
            reloadScene();
            st.Path.clear();
        }
        ImGui::EndDisabled();

        if (!model->Animations.empty())
        {
            UI::SubHeader("Animations");
            for (const auto& clip : model->Animations)
                if (clip)
                    UI::Text(clip->Name.empty() ? "(unnamed)" : clip->Name.c_str(), "%.2f s", clip->DurationSeconds());
        }
    }
}
