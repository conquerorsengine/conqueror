#include "ShaderVariantInspector.h"

#include "Assets/AssetUI.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"
#include "Renderer/ShaderVariantCollection.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <filesystem>

namespace Conqueror::Editor::ShaderVariantInspector
{
    static std::function<std::vector<std::string>()> s_Scene;
    void SetSceneProvider(std::function<std::vector<std::string>()> provider) { s_Scene = std::move(provider); }

    void Draw(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_LAYER_GROUP);
        ImGui::SameLine();
        ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Shader Variant Collection");
        ImGui::Separator();
        std::vector<std::string> entries;
        ShaderVariantCollections::Load(path, entries);
        bool changed = false;
        int remove = -1;
        UI::SubHeader("Shaders");
        for (int i = 0; i < (int)entries.size(); ++i)
        {
            ImGui::PushID(i);
            ImGui::BulletText("%s", entries[(size_t)i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::PopID();
        }
        if (entries.empty())
            ImGui::TextDisabled("Empty. Add materials (.cqmat) or shaders (.cqsh, .comp).");
        if (remove >= 0) { entries.erase(entries.begin() + remove); changed = true; }
        std::string add;
        UI::BeginProperty("Add");
        if (AssetUI::AssetSlot("add", "Material or Shader", { ".cqmat", ".cqsh", ".comp" }, add) && !add.empty() &&
            std::find(entries.begin(), entries.end(), add) == entries.end())
        {
            entries.push_back(add);
            changed = true;
        }
        UI::EndProperty();
        if (s_Scene && ImGui::Button(ICON_FA_PLUS "  Add Shaders Used In Scene"))
        {
            for (const auto& s : s_Scene())
                if (std::find(entries.begin(), entries.end(), s) == entries.end())
                {
                    entries.push_back(s);
                    changed = true;
                }
        }
        if (changed)
            ShaderVariantCollections::Save(path, entries);

        ImGui::Spacing();
        static std::string message;
        if (ImGui::Button(ICON_FA_FIRE "  Warm Up"))
        {
            std::vector<std::string> errors;
            const int n = ShaderVariantCollections::WarmUp(path, &errors);
            message = "Compiled " + std::to_string(n) + " of " + std::to_string(entries.size()) + (errors.empty() ? "." : " (see Console).");
        }
        ImGui::SameLine();
        auto& preloaded = ProjectSettings::Graphics().PreloadedShaders;
        const std::string rel = AssetUI::ToProjectRelative(path);
        const bool listed = std::find(preloaded.begin(), preloaded.end(), rel) != preloaded.end();
        if (ImGui::Button(listed ? ICON_FA_CHECK "  In Preloaded Shaders" : ICON_FA_LIST "  Add to Preloaded Shaders") && !listed)
        {
            preloaded.push_back(rel);
            if (auto project = Project::GetActive())
                ProjectSerializer(project).Serialize(Project::GetActiveProjectFile());
        }
        if (!message.empty())
            ImGui::TextDisabled("%s", message.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Collections listed in Project Settings > Graphics > Preloaded Shaders are compiled when the game starts.");
    }
}
