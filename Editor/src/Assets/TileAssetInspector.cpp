#include "TileAssetInspector.h"

#include "Assets/AssetUI.h"
#include "Grid/GridSystem.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Serialization/SceneSerializer.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>
#include <map>
#include <memory>

namespace Conqueror::Editor::TileAssetInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        std::shared_ptr<Texture2D> Thumb(const std::string& path)
        {
            static auto* cache = new std::map<std::string, std::shared_ptr<Texture2D>>();   // GL bağlamından sonra yok edilmesin
            if (path.empty())
                return nullptr;
            auto& t = (*cache)[path];
            if (!t)
                t = Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            return t && t->GetRendererID() ? t : nullptr;
        }

        void Header(const char* icon, const std::string& path, const char* kind)
        {
            ImGui::TextColored(UI::Colors::Accent, "%s", icon);
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", kind);
            ImGui::Separator();
        }

        const std::vector<std::string>& Images() { return ObjectPicker::ImageExtensions(); }

        // 3x3 komşu deseni: merkez çıktı dokusu, çevre hücreler tıklanınca Fark etmez -> Bu karo -> Bu karo değil
        bool RuleGrid(TileRule& rule, const std::string& fallbackTexture)
        {
            // Izgara konumları (satır, sütun) -> komşu indisi (K, KD, D, GD, G, GB, B, KB)
            static const int slot[3][3] = { { 7, 0, 1 }, { 6, -1, 2 }, { 5, 4, 3 } };
            const float s = 26.0f;
            bool changed = false;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::BeginGroup();
            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 3; ++c)
                {
                    if (c) ImGui::SameLine(0.0f, 2.0f);
                    ImGui::PushID(r * 3 + c);
                    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + s, a.y + s);
                    const int n = slot[r][c];
                    if (n < 0)
                    {
                        ImGui::Dummy(ImVec2(s, s));
                        dl->AddRectFilled(a, b, IM_COL32(40, 42, 48, 255), 3.0f);
                        auto tex = Thumb(rule.Texture.empty() ? fallbackTexture : rule.Texture);
                        if (tex)
                            dl->AddImage((ImTextureID)(uintptr_t)tex->GetRendererID(), ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), ImVec2(0, 1), ImVec2(1, 0));
                    }
                    else
                    {
                        if (ImGui::InvisibleButton("##n", ImVec2(s, s)))
                        {
                            rule.Neighbors[n] = (rule.Neighbors[n] + 1) % 3;
                            changed = true;
                        }
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            rule.Neighbors[n] = 0;
                            changed = true;
                        }
                        dl->AddRectFilled(a, b, IM_COL32(32, 34, 38, 255), 3.0f);
                        dl->AddRect(a, b, IM_COL32(80, 84, 92, 255), 3.0f);
                        const ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                        if (rule.Neighbors[n] == 1)
                        {
                            // Bu karo: yeşil ok (komşuya doğru)
                            const ImVec2 d((float)(c - 1), (float)(r - 1));
                            const float len = std::sqrt(d.x * d.x + d.y * d.y);
                            const ImVec2 u(d.x / len, d.y / len);
                            dl->AddLine(ImVec2(m.x - u.x * 7, m.y - u.y * 7), ImVec2(m.x + u.x * 7, m.y + u.y * 7), IM_COL32(90, 220, 110, 255), 2.5f);
                            dl->AddTriangleFilled(ImVec2(m.x + u.x * 9, m.y + u.y * 9), ImVec2(m.x + u.x * 3 - u.y * 5, m.y + u.y * 3 + u.x * 5),
                                                  ImVec2(m.x + u.x * 3 + u.y * 5, m.y + u.y * 3 - u.x * 5), IM_COL32(90, 220, 110, 255));
                        }
                        else if (rule.Neighbors[n] == 2)
                        {
                            dl->AddLine(ImVec2(m.x - 6, m.y - 6), ImVec2(m.x + 6, m.y + 6), IM_COL32(235, 90, 80, 255), 2.5f);
                            dl->AddLine(ImVec2(m.x + 6, m.y - 6), ImVec2(m.x - 6, m.y + 6), IM_COL32(235, 90, 80, 255), 2.5f);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            ImGui::SetTooltip("Click: Don't care -> This tile (green) -> Not this tile (red). Right-click: Don't care.");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndGroup();
            return changed;
        }

        struct TileState
        {
            std::string Path;
            TileDefinition Def;
            bool Rule = false, Animated = false;
        };
        TileState& Tile() { static TileState s; return s; }
    }

    void DrawTile(const std::string& path)
    {
        auto& st = Tile();
        if (st.Path != path)
        {
            st = {};
            st.Path = path;
            GridSystem::LoadTileAsset(path, st.Def);
            st.Rule = !st.Def.Rules.empty();
            st.Animated = st.Def.FrameRate > 0.0f || !st.Def.Frames.empty();
        }
        TileDefinition& d = st.Def;
        Header(st.Rule ? ICON_FA_TABLE_CELLS_LARGE : (st.Animated ? ICON_FA_FILM : ICON_FA_SQUARE), path,
               st.Rule ? "Rule Tile" : (st.Animated ? "Animated Tile" : "Tile"));
        bool changed = false;
        if (AssetUI::BeginProperties("##tile"))
        {
            AssetUI::Row("Name");
            changed |= AssetUI::InputString("##name", d.Name);
            AssetUI::Row(st.Rule ? "Default Sprite" : (st.Animated ? "Preview Sprite" : "Sprite"));
            changed |= AssetUI::AssetSlot("sprite", "Sprite", Images(), d.Texture);
            AssetUI::Row("Color");
            changed |= ImGui::ColorEdit4("##color", &d.Color.r, ImGuiColorEditFlags_NoInputs);
            // Unity Tile: Collider Type ve Flags, karo başına GameObject
            AssetUI::Row("Collider Type");
            {
                static const char* kCollider[] = { "None", "Sprite", "Grid" };
                int collider = !d.Solid ? 0 : (d.SpriteCollider ? 1 : 2);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##collidertype", &collider, kCollider, 3))
                {
                    d.Solid = collider != 0;
                    d.SpriteCollider = collider == 1;
                    changed = true;
                }
            }
            AssetUI::Row("Lock Color");
            changed |= ImGui::Checkbox("##lockcolor", &d.LockColor);
            AssetUI::Row("Lock Transform");
            changed |= ImGui::Checkbox("##locktransform", &d.LockTransform);
            AssetUI::Row("GameObject");
            changed |= AssetUI::AssetSlot("gameobject", "Prefab", { ".cqprefab" }, d.GameObject);
            if (!d.GameObject.empty())
            {
                AssetUI::Row("Instantiate Runtime Only");
                changed |= ImGui::Checkbox("##goruntime", &d.GameObjectRuntimeOnly);
            }
            AssetUI::Row("Walkable");
            changed |= ImGui::Checkbox("##walk", &d.Walkable);
            AssetUI::Row("Path Cost");
            changed |= ImGui::DragInt("##cost", &d.Cost, 0.1f, 1, 100);
            AssetUI::Row("Height (3D)");
            changed |= ImGui::DragFloat("##height", &d.Height, 0.01f, 0.01f, 20.0f);
            AssetUI::EndProperties();
        }

        if (st.Rule)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(ICON_FA_TABLE_CELLS_LARGE "  Tiling Rules");
            ImGui::TextDisabled("The first rule whose neighbours match decides the sprite; no match = Default Sprite.");
            ImGui::Separator();
            int remove = -1, up = -1;
            for (int i = 0; i < (int)d.Rules.size(); ++i)
            {
                ImGui::PushID(i);
                TileRule& r = d.Rules[(size_t)i];
                ImGui::TextDisabled("Rule %d", i + 1);
                changed |= RuleGrid(r, d.Texture);
                ImGui::SameLine(0.0f, 12.0f);
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Output Sprite");
                ImGui::SetNextItemWidth(220.0f);
                changed |= AssetUI::AssetSlot("out", "Sprite", Images(), r.Texture);
                static const char* rot[] = { "Fixed", "Rotated (x90°)" };
                ImGui::SetNextItemWidth(160.0f);
                changed |= ImGui::Combo("##rot", &r.Rotation, rot, 2);
                if (ImGui::SmallButton(ICON_FA_ARROW_UP) && i > 0) up = i;
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH)) remove = i;
                ImGui::EndGroup();
                ImGui::Separator();
                ImGui::PopID();
            }
            if (up > 0) { std::swap(d.Rules[(size_t)up], d.Rules[(size_t)up - 1]); changed = true; }
            if (remove >= 0) { d.Rules.erase(d.Rules.begin() + remove); changed = true; }
            if (ImGui::Button(ICON_FA_PLUS "  Add Rule", ImVec2(-1.0f, 0.0f)))
            {
                d.Rules.push_back({});
                changed = true;
            }
        }
        if (st.Animated)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(ICON_FA_FILM "  Animation");
            ImGui::Separator();
            if (AssetUI::BeginProperties("##anim"))
            {
                AssetUI::Row("Speed (frames/s)");
                changed |= ImGui::DragFloat("##fps", &d.FrameRate, 0.1f, 0.1f, 120.0f, "%.1f");
                AssetUI::Row("Frames");
                ImGui::Text("%d", (int)d.Frames.size());
                int remove = -1;
                for (int i = 0; i < (int)d.Frames.size(); ++i)
                {
                    ImGui::PushID(i);
                    char label[24];
                    snprintf(label, sizeof(label), "Frame %d", i);
                    AssetUI::Row(label);
                    if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
                    ImGui::SameLine();
                    changed |= AssetUI::AssetSlot("frame", "Sprite", Images(), d.Frames[(size_t)i]);
                    ImGui::PopID();
                }
                AssetUI::EndProperties();
                if (remove >= 0) { d.Frames.erase(d.Frames.begin() + remove); changed = true; }
            }
            if (ImGui::Button(ICON_FA_PLUS "  Add Frame", ImVec2(-1.0f, 0.0f)))
            {
                d.Frames.push_back(d.Frames.empty() ? d.Texture : d.Frames.back());
                changed = true;
            }
            // Karelere sprite bırakma
            ImGui::Button(ICON_FA_IMAGES "  Drop sprites here to add frames", ImVec2(-1.0f, 24.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    d.Frames.push_back(AssetUI::ToProjectRelative((const char*)p->Data));
                    if (d.Texture.empty()) d.Texture = d.Frames.front();
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
        }
        if (changed)
            GridSystem::SaveTileAsset(path, d);
        ImGui::Spacing();
        ImGui::TextWrapped("Add it to a Tile Palette (Tile Palette window > drop it on the palette), then paint it on a Tilemap. "
                           "Tilemaps that use it update when you change this asset.");
    }

    void DrawPalette(const std::string& path)
    {
        static std::string loadedPath;
        static GridSystem::TilePaletteAsset palette;
        if (loadedPath != path)
        {
            loadedPath = path;
            palette = {};
            GridSystem::LoadPalette(path, palette);
        }
        Header(ICON_FA_SWATCHBOOK, path, "Tile Palette");
        bool changed = false;
        if (AssetUI::BeginProperties("##palette"))
        {
            AssetUI::Row("Grid");
            static const char* layouts[] = { "Rectangle", "Hexagon (Pointy Top)", "Hexagon (Flat Top)", "Isometric" };
            int layout = (int)palette.Layout;
            if (ImGui::Combo("##layout", &layout, layouts, 4)) { palette.Layout = (GridLayout)layout; changed = true; }
            AssetUI::Row("Cell Size");
            changed |= ImGui::DragFloat2("##cell", &palette.CellSize.x, 0.01f, 0.01f, 100.0f);
            AssetUI::Row("Tiles");
            ImGui::Text("%d", (int)palette.Tiles.size());
            int remove = -1;
            for (int i = 0; i < (int)palette.Tiles.size(); ++i)
            {
                ImGui::PushID(i);
                char label[24];
                snprintf(label, sizeof(label), "Tile %d", i);
                AssetUI::Row(label);
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
                ImGui::SameLine();
                changed |= AssetUI::AssetSlot("tile", "Tile", { GridSystem::TileExtension }, palette.Tiles[(size_t)i]);
                ImGui::PopID();
            }
            AssetUI::EndProperties();
            if (remove >= 0) { palette.Tiles.erase(palette.Tiles.begin() + remove); changed = true; }
        }
        std::string add;
        if (AssetUI::AssetSlot("addtile", "Tile", { GridSystem::TileExtension }, add) && !add.empty())
        {
            palette.Tiles.push_back(add);
            changed = true;
        }
        ImGui::TextDisabled("Drop or pick a Tile above to add it.");
        if (changed)
            GridSystem::SavePalette(path, palette);
        ImGui::Spacing();
        ImGui::TextWrapped("Open Window > Tile Palette, choose this palette and paint its tiles on any Tilemap.");
    }
}
