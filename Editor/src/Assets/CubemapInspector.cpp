#include "CubemapInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/ObjectPicker.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/CubemapAsset.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>

namespace Conqueror::Editor::CubemapInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        const std::vector<std::string> kImageExts = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".exr", ".psd" };

        // Küçük resimli yüz yuvası: tıkla -> seçici, Content Browser'dan bırak
        bool FaceSlot(const char* id, const char* label, std::string& value, float size)
        {
            bool changed = false;
            ImGui::PushID(id);
            ImGui::BeginGroup();
            std::shared_ptr<Texture2D> tex;
            if (!value.empty())
                tex = Texture2D::Create(SceneSerializer::ResolveAssetPath(value));
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            if (tex && tex->GetRendererID())
                ImGui::ImageButton("##face", (ImTextureID)(uintptr_t)tex->GetRendererID(), ImVec2(size, size), ImVec2(0, 1), ImVec2(1, 0));
            else
            {
                ImGui::Button("##face", ImVec2(size + ImGui::GetStyle().FramePadding.x * 2, size + ImGui::GetStyle().FramePadding.y * 2));
                ImGui::GetWindowDrawList()->AddText(ImVec2(cursor.x + 8, cursor.y + size * 0.5f - 6), IM_COL32(255, 255, 255, 110), "None");
            }
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    value = SceneSerializer::ToAssetPath(std::string((const char*)payload->Data, payload->DataSize - 1));
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s\n%s\nClick to pick, drop an image, right-click to clear", label, value.empty() ? "None" : value.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !value.empty())
            {
                value.clear();
                changed = true;
            }
            const ImGuiID pickId = ImGui::GetID("##pick");
            if (clicked)
                ObjectPicker::Open(pickId, "Texture", kImageExts, value.empty() ? value : SceneSerializer::ResolveAssetPath(value));
            if (std::string picked; ObjectPicker::Consume(pickId, picked))
            {
                value = picked.empty() ? picked : SceneSerializer::ToAssetPath(picked);
                changed = true;
            }
            ImGui::TextDisabled("%s", label);
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time{};
        static CubemapAssetData s_Data;
        static bool s_Loaded = false;
        static std::string s_Error;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        auto commit = [](bool force) {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after),
                                                                                    "Edit " + fs::path(s_Edit.Path).filename().string()));
        };
        if (s_Edit.Open && s_Edit.Path != path)
            commit(true);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Loaded = CubemapAssets::Load(path, s_Data, &s_Error);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CUBE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Cubemap");
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The cubemap could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }

        bool changed = false;
        int source = (int)s_Data.Source;
        UI::BeginProperty("Source", "Six Faces: one image per side. Panorama: one equirectangular image (HDR or LDR).");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##src", &source, "Six Faces\0Panorama\0"))
        {
            s_Data.Source = (CubemapAssetData::SourceKind)source;
            changed = true;
        }
        UI::EndProperty();

        if (s_Data.Source == CubemapAssetData::SourceKind::Faces)
        {
            // Çapraz düzen: üstte +Y, ortada -X +Z +X -Z, altta -Y
            const float size = std::max(40.0f, (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f - 2 * ImGui::GetStyle().FramePadding.x);
            const float cell = size + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x;
            const float x0 = ImGui::GetCursorPosX();
            const int layout[3][4] = { { -1, 2, -1, -1 }, { 1, 4, 0, 5 }, { -1, 3, -1, -1 } };
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    const int face = layout[row][col];
                    if (face < 0)
                        continue;
                    ImGui::SetCursorPosX(x0 + col * cell);
                    changed |= FaceSlot(CubemapAssets::FaceName(face), CubemapAssets::FaceName(face), s_Data.Faces[face], size);
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        }
        else
        {
            UI::BeginProperty("Panorama", "An equirectangular image (2:1), HDR (.hdr / .exr) or LDR.");
            changed |= AssetUI::AssetSlot("##pano", "Texture", kImageExts, s_Data.Panorama);
            UI::EndProperty();
            UI::BeginProperty("Face Size", "Resolution of each cube face made from the panorama.");
            static const int sizes[] = { 256, 512, 1024, 2048, 4096 };
            int index = 1;
            for (int i = 0; i < 5; ++i)
                if (sizes[i] == s_Data.Resolution)
                    index = i;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##res", &index, "256\0" "512\0" "1024\0" "2048\0" "4096\0"))
            {
                s_Data.Resolution = sizes[index];
                changed = true;
            }
            UI::EndProperty();
        }

        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            CubemapAssets::Save(path, s_Data);
            s_Time = fs::last_write_time(path, ec);
        }
        commit(false);

        ImGui::Spacing();
        ImGui::BeginDisabled(!s_UseAsSkybox);
        if (ImGui::Button(ICON_FA_CLOUD_SUN "  Use as Skybox", ImVec2(-1, 0)) && s_UseAsSkybox)
            s_UseAsSkybox(path);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Also usable in Lighting > Environment > Skybox, a Camera's Skybox and SetSkybox(\"%s\").",
                           AssetUI::ToProjectRelative(path).c_str());
    }
}
