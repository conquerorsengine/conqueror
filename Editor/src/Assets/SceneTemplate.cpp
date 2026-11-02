#include "SceneTemplate.h"

#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Assets/AssetUI.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/RHI/Texture.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>

namespace Conqueror::Editor::SceneTemplates
{
    namespace fs = std::filesystem;

    fs::path Resolve(const std::string& projectRelative)
    {
        if (projectRelative.empty())
            return {};
        fs::path p(projectRelative);
        if (p.is_relative() && Project::GetActive())
            p = Project::GetActiveProjectDirectory() / p;
        return p;
    }

    std::string ToProjectRelative(const fs::path& absolute)
    {
        if (absolute.empty())
            return {};
        if (Project::GetActive())
        {
            std::error_code ec;
            auto rel = fs::relative(absolute, Project::GetActiveProjectDirectory(), ec);
            if (!ec && !rel.empty() && rel.native()[0] != '.')
                return rel.generic_string();
        }
        return absolute.generic_string();
    }

    bool Load(const fs::path& path, SceneTemplate& out)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            YAML::Node n = root["SceneTemplate"];
            if (!n)
                return false;
            out = {};
            out.Name = n["Name"] ? n["Name"].as<std::string>() : path.stem().string();
            if (n["Description"]) out.Description = n["Description"].as<std::string>();
            if (n["Scene"]) out.ScenePath = n["Scene"].as<std::string>();
            if (n["Thumbnail"]) out.ThumbnailPath = n["Thumbnail"].as<std::string>();
            if (n["Pinned"]) out.Pinned = n["Pinned"].as<bool>();
            return true;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("Scene template could not be read: {0} ({1})", path.string(), e.what());
            return false;
        }
    }

    bool Save(const fs::path& path, const SceneTemplate& t)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "SceneTemplate" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "Name" << YAML::Value << t.Name;
        e << YAML::Key << "Description" << YAML::Value << t.Description;
        e << YAML::Key << "Scene" << YAML::Value << t.ScenePath;
        e << YAML::Key << "Thumbnail" << YAML::Value << t.ThumbnailPath;
        e << YAML::Key << "Pinned" << YAML::Value << t.Pinned;
        e << YAML::EndMap << YAML::EndMap;
        std::ofstream f(path);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return true;
    }

    std::vector<fs::path> FindAll()
    {
        std::vector<std::pair<SceneTemplate, fs::path>> found;
        if (!Project::GetActive())
            return {};
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(Project::GetActiveProjectDirectory(), fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            const auto name = it->path().filename().string();
            if (it->is_directory(ec) && (name == "Library" || name == "Builds" || name.rfind('.', 0) == 0))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() == Extension)
            {
                SceneTemplate t;
                if (Load(it->path(), t))
                    found.emplace_back(t, it->path());
            }
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            if (a.first.Pinned != b.first.Pinned)
                return a.first.Pinned;
            return a.first.Name < b.first.Name;
        });
        std::vector<fs::path> out;
        for (auto& f : found)
            out.push_back(f.second);
        return out;
    }

    namespace
    {
        struct EditState
        {
            std::string Path;
            SceneTemplate Data;
            std::shared_ptr<Texture2D> Thumb;
            std::string ThumbPath;
        };
        EditState& State()
        {
            static EditState* s = new EditState();   // GL dokusu bağlamdan sonra yok edilmesin
            return *s;
        }
    }

    static std::function<bool(const fs::path&)> s_Snapshot;
    void SetSnapshotProvider(std::function<bool(const fs::path&)> provider) { s_Snapshot = std::move(provider); }

    void DrawInspector(const std::string& path)
    {
        const auto& captureThumbnail = s_Snapshot;
        auto& st = State();
        if (st.Path != path)
        {
            st = {};
            st.Path = path;
            if (!Load(path, st.Data))
                st.Data.Name = fs::path(path).stem().string();
        }
        bool changed = false;

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CLAPPERBOARD);
        ImGui::SameLine();
        ImGui::TextUnformatted((fs::path(path).filename().string() + "  (Scene Template)").c_str());
        ImGui::Separator();

        if (ImGui::BeginTable("##sceneTemplate", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Template Scene");
            ImGui::TableNextColumn();
            if (AssetUI::AssetSlot("scene", "Scene", { ".cqscene" }, st.Data.ScenePath)) changed = true;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Title");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1.0f);
            if (AssetUI::InputString("##name", st.Data.Name)) changed = true;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("Description");
            ImGui::TableNextColumn();
            if (AssetUI::InputMultiline("##desc", st.Data.Description, ImVec2(-1.0f, 70.0f))) changed = true;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Pin in New Scene");
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##pin", &st.Data.Pinned)) changed = true;

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Thumbnail");
            ImGui::TableNextColumn();
            {
                if (AssetUI::AssetSlot("thumb", "Texture", ObjectPicker::ImageExtensions(), st.Data.ThumbnailPath)) changed = true;
                if (ImGui::Button(ICON_FA_CAMERA "  Snapshot") && captureThumbnail)
                {
                    // Şablonun yanında "<şablon> Thumbnail.png" (Unity: Snapshot > Game View / Main Camera)
                    fs::path out = fs::path(path).parent_path() / (fs::path(path).stem().string() + " Thumbnail.png");
                    if (captureThumbnail(out))
                    {
                        st.Data.ThumbnailPath = ToProjectRelative(out);
                        st.ThumbPath.clear();
                        st.Thumb.reset();
                        changed = true;
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Capture the Game view (or the Scene view) as this template's thumbnail.");
            }
            ImGui::EndTable();
        }

        // Önizleme
        const fs::path thumbAbs = Resolve(st.Data.ThumbnailPath);
        if (st.ThumbPath != thumbAbs.string())
        {
            st.ThumbPath = thumbAbs.string();
            std::error_code ec;
            st.Thumb = (!thumbAbs.empty() && fs::exists(thumbAbs, ec)) ? Texture2D::Create(thumbAbs.string()) : nullptr;
        }
        if (st.Thumb && st.Thumb->GetRendererID() && st.Thumb->GetWidth() > 0)
        {
            const float w = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
            const float h = w * (float)st.Thumb->GetHeight() / (float)st.Thumb->GetWidth();
            ImGui::Image((ImTextureID)(uintptr_t)st.Thumb->GetRendererID(), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
        }

        if (st.Data.ScenePath.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(UI::Colors::Warning, ICON_FA_TRIANGLE_EXCLAMATION "  Assign a scene: new scenes made from this template start as a copy of it.");
        }
        if (changed && !Save(path, st.Data))
            CQ_CORE_WARN("Scene template could not be saved: {0}", path);
    }
}
