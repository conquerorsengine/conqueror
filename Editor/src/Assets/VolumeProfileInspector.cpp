#include "VolumeProfileInspector.h"

#include "Assets/AssetUI.h"
#include "EditorState.h"
#include "Panels/InspectorPanel.h"
#include "Core/Project/Project.h"
#include "Scene/Components.h"
#include "Scene/PostProcessVolume.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>

namespace Conqueror::Editor::VolumeProfileInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        // Profil ayarlarını çiz; değişince dosyaya yaz
        void DrawProfileFields(const std::string& path)
        {
            PostProcessVolumeComponent values;   // varsayılanlar + dosyadaki ayarlar (Profile boş: alanlar görünür)
            if (!VolumeProfiles::Load(path, values))
            {
                UI::InfoBox("The volume profile could not be read.", UI::InfoType::Error);
                return;
            }
            if (InspectorPanel::DrawObjectFields("PostProcessVolumeComponent", &values, &VolumeProfiles::IsProfileField))
                VolumeProfiles::Save(path, values);
        }

        fs::path UniquePath(const fs::path& p)
        {
            std::error_code ec;
            fs::path out = p;
            for (int i = 1; fs::exists(out, ec); ++i)
                out = p.parent_path() / (p.stem().string() + " " + std::to_string(i) + p.extension().string());
            return out;
        }
    }

    void DrawAsset(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CUBES_STACKED);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Volume Profile");
        ImGui::Separator();
        DrawProfileFields(path);
        ImGui::Spacing();
        ImGui::TextWrapped("Assign it to a Post Process Volume's Profile slot. Every volume that uses this profile changes with it.");
    }

    void DrawVolumeExtra(Entity entity)
    {
        auto& v = entity.GetComponent<PostProcessVolumeComponent>();
        ImGui::Spacing();
        if (v.Profile.empty())
        {
            // Unity Volume "New": hacmin şimdiki ayarlarıyla profil oluştur ve ata
            if (ImGui::Button(ICON_FA_PLUS "  New Profile", ImVec2(-1.0f, 0.0f)) && Project::GetActive())
            {
                std::error_code ec;
                const fs::path dir = Project::GetActiveProjectDirectory() / "Assets" / "Volume Profiles";
                fs::create_directories(dir, ec);
                const fs::path file = UniquePath(dir / (entity.GetComponent<TagComponent>().Tag + " Profile" + VolumeProfiles::Extension));
                if (VolumeProfiles::Save(file.string(), v))
                    v.Profile = AssetUI::ToProjectRelative(file);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Save these overrides as a Volume Profile asset that other volumes can share.");
            return;
        }
        UI::SubHeader("Profile Overrides");
        ImGui::TextDisabled("%s", v.Profile.c_str());
        if (ImGui::Button(ICON_FA_CLONE "  Clone"))
        {
            // Unity "Clone": bu hacme özel kopya (diğer hacimler eski profili kullanmaya devam eder)
            const fs::path src = AssetUI::Resolve(v.Profile);
            const fs::path dst = UniquePath(src.parent_path() / (src.stem().string() + " Copy" + src.extension().string()));
            std::error_code ec;
            if (fs::copy_file(src, dst, ec))
                v.Profile = AssetUI::ToProjectRelative(dst);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Select Asset"))
            EditorState::Get().SelectAsset(AssetUI::Resolve(v.Profile).string());
        DrawProfileFields(v.Profile);
    }
}
