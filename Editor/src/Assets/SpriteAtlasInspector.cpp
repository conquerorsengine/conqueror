#include "SpriteAtlasInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/SpriteAtlas.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>
#include <memory>

namespace Conqueror::Editor::SpriteAtlasInspector
{
    namespace fs = std::filesystem;

    void Draw(const std::string& path)
    {
        struct State
        {
            std::string Path;
            SpriteAtlasAsset Atlas;
            std::shared_ptr<Texture2D> Preview;
            fs::file_time_type PreviewTime{};
            std::string Message;
            bool Error = false;
        };
        static State* st = new State();   // GL dokusu bağlamdan sonra yok edilmesin
        if (st->Path != path)
        {
            *st = State{};
            st->Path = path;
            SpriteAtlases::Load(path, st->Atlas);
        }
        SpriteAtlasAsset& a = st->Atlas;

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_OBJECT_GROUP);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Sprite Atlas");
        ImGui::Separator();

        bool changed = false;
        if (AssetUI::BeginProperties("##atlas"))
        {
            AssetUI::Row("Include in Build");
            changed |= ImGui::Checkbox("##build", &a.IncludeInBuild);
            AssetUI::Row("Padding");
            changed |= ImGui::SliderInt("##pad", &a.Padding, 0, 16);
            AssetUI::Row("Max Texture Size");
            static const int sizes[] = { 256, 512, 1024, 2048, 4096, 8192 };
            int idx = 3;
            for (int i = 0; i < 6; ++i) if (sizes[i] == a.MaxSize) idx = i;
            static const char* sizeNames[] = { "256", "512", "1024", "2048", "4096", "8192" };
            if (ImGui::Combo("##max", &idx, sizeNames, 6)) { a.MaxSize = sizes[idx]; changed = true; }
            AssetUI::Row("Filter Mode");
            int filter = a.PointFilter ? 1 : 0;
            static const char* filters[] = { "Bilinear", "Point (no filter)" };
            if (ImGui::Combo("##filter", &filter, filters, 2)) { a.PointFilter = filter == 1; changed = true; }
            AssetUI::EndProperties();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted(ICON_FA_IMAGES "  Objects for Packing");
        ImGui::Separator();
        int remove = -1;
        for (int i = 0; i < (int)a.Objects.size(); ++i)
        {
            ImGui::PushID(i);
            std::error_code ec;
            const bool folder = fs::is_directory(AssetUI::Resolve(a.Objects[(size_t)i]), ec);
            ImGui::TextUnformatted(folder ? ICON_FA_FOLDER : ICON_FA_IMAGE);
            ImGui::SameLine();
            ImGui::TextUnformatted(a.Objects[(size_t)i].c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
            ImGui::PopID();
        }
        if (remove >= 0) { a.Objects.erase(a.Objects.begin() + remove); changed = true; }
        ImGui::Button(ICON_FA_PLUS "  Drop sprites or folders here", ImVec2(-30.0f, 26.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                a.Objects.push_back(AssetUI::ToProjectRelative((const char*)p->Data));
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0.0f, 4.0f);
        std::string picked;
        if (ObjectPicker::Button("##addsprite", "Sprite", ObjectPicker::ImageExtensions(), std::string(), picked) && !picked.empty())
        {
            a.Objects.push_back(AssetUI::ToProjectRelative(picked));
            changed = true;
        }

        if (changed)
        {
            // Ayarları kaydet (paket sonucu korunur); yeniden paketleme Pack Preview / Play / Build ile
            SpriteAtlasAsset onDisk;
            SpriteAtlases::Load(path, onDisk);
            a.Packed = onDisk.Packed;
            a.PackedSize = onDisk.PackedSize;
            SpriteAtlases::Save(path, a);
        }

        ImGui::Spacing();
        if (UI::ButtonFullWidth(ICON_FA_BOXES_PACKING "  Pack Preview", 28.0f))
        {
            std::string error;
            if (SpriteAtlases::Pack(path, &error))
            {
                SpriteAtlases::Load(path, a);
                st->Message = "Packed " + std::to_string(a.Packed.size()) + " sprites into " + std::to_string(a.PackedSize.x) + " x " +
                              std::to_string(a.PackedSize.y) + ".";
                st->Error = false;
            }
            else
            {
                st->Message = error;
                st->Error = true;
            }
            st->Preview.reset();
        }
        if (!st->Message.empty())
            UI::InfoBox(st->Message.c_str(), st->Error ? UI::InfoType::Error : UI::InfoType::Success);

        // Önizleme
        const std::string png = SpriteAtlases::PackedTexturePath(path);
        std::error_code ec;
        if (fs::exists(png, ec))
        {
            const auto t = fs::last_write_time(png, ec);
            if (!st->Preview || st->PreviewTime != t)
            {
                st->Preview = Texture2D::Create(png);
                st->PreviewTime = t;
            }
            if (st->Preview && st->Preview->GetRendererID() && st->Preview->GetWidth() > 0)
            {
                ImGui::TextDisabled("%d sprites  |  %u x %u", (int)a.Packed.size(), st->Preview->GetWidth(), st->Preview->GetHeight());
                const float w = std::min(ImGui::GetContentRegionAvail().x, 360.0f);
                const float h = w * (float)st->Preview->GetHeight() / (float)st->Preview->GetWidth();
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                // Saydam alan için dama tahtası
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float c = 8.0f;
                for (float y = 0; y < h; y += c)
                    for (float x = 0; x < w; x += c)
                        dl->AddRectFilled(ImVec2(p0.x + x, p0.y + y), ImVec2(p0.x + std::min(x + c, w), p0.y + std::min(y + c, h)),
                                          ((int)(x / c) + (int)(y / c)) % 2 ? IM_COL32(60, 60, 64, 255) : IM_COL32(44, 44, 48, 255));
                ImGui::Image((ImTextureID)(uintptr_t)st->Preview->GetRendererID(), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
            }
        }
        else
            ImGui::TextDisabled("Not packed yet: press Pack Preview (it also packs automatically on Play and Build).");
        ImGui::Spacing();
        ImGui::TextWrapped("Sprite Renderers that use these sprites draw them from the atlas texture in the editor, in Play mode "
                           "and in builds (uncheck Include in Build to turn the atlas off).");
    }
}
