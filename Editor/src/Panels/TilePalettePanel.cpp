#include "ObjectPicker.h"
#include "TilePalettePanel.h"
#include "EditorState.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Grid/GridSystem.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Project/Project.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <cmath>
#include <filesystem>

namespace Conqueror::Editor
{
    std::function<void()> TilePalettePanel::s_OpenPanel;

    namespace
    {
        const char* kToolNames[] = { "Paint Brush (B)", "Box Fill (U)", "Flood Fill (G)", "Eraser (D)", "Picker (I)" };
        const char* kToolIcons[] = { ICON_FA_PAINTBRUSH, ICON_FA_VECTOR_SQUARE, ICON_FA_FILL_DRIP, ICON_FA_ERASER, ICON_FA_EYE_DROPPER };
        const char* kToolHelp[] = {
            "Click and drag in the Scene view to paint. Shift erases, Ctrl+Click picks a tile.",
            "Drag a rectangle in the Scene view to fill it. Shift erases the rectangle.",
            "Click to flood-fill connected cells of the same tile. Shift erases the region.",
            "Click and drag to erase tiles.",
            "Click a painted cell to pick its tile, then switches back to the brush."
        };
        const char* kLayoutNames[] = { "Rectangle", "Hexagon (Pointy Top)", "Hexagon (Flat Top)", "Isometric" };

        ImU32 ToU32(const glm::vec4& c)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
        }

        bool IsHex(const GridComponent& g)
        {
            return g.Layout == GridLayout::HexagonPointyTop || g.Layout == GridLayout::HexagonFlatTop;
        }

        // Palet karesi: doku varsa doku, yoksa renk; altıgen ızgarada altıgen şekil
        void DrawSwatch(ImDrawList* dl, ImVec2 a, ImVec2 b, const TileDefinition& def, bool hex)
        {
            const ImU32 col = ToU32(def.Color);
            auto tex = GridSystem::TileTexture(def.Texture);
            ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            float r = std::min(b.x - a.x, b.y - a.y) * 0.5f;
            if (tex)
            {
                dl->AddImage((ImTextureID)(uint64_t)tex->GetRendererID(), a, b, ImVec2(0, 1), ImVec2(1, 0), col);
            }
            else if (hex)
            {
                ImVec2 pts[6];
                for (int i = 0; i < 6; ++i)
                {
                    float ang = (60.0f * (float)i - 30.0f) * 3.14159265f / 180.0f;
                    pts[i] = ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r);
                }
                dl->AddConvexPolyFilled(pts, 6, col);
                dl->AddPolyline(pts, 6, IM_COL32(0, 0, 0, 90), ImDrawFlags_Closed, 1.0f);
            }
            else
            {
                dl->AddRectFilled(a, b, col, 3.0f);
            }
            // Katı / yürünemez işaretleri
            if (!def.Walkable)
                dl->AddText(ImVec2(b.x - 13.0f, a.y + 1.0f), IM_COL32(255, 255, 255, 200), ICON_FA_BAN);
            if (def.Solid)
                dl->AddText(ImVec2(a.x + 2.0f, b.y - 15.0f), IM_COL32(255, 255, 255, 170), ICON_FA_CUBE);
        }
    }

    // ------------------------------------------------------------------ Hedef / oluşturma

    Entity TilePalettePanel::CreateTilemap(Scene* scene, GridLayout layout, GridPlane plane, bool render3D, Entity parent)
    {
        if (!scene)
            return {};
        const bool hex = layout == GridLayout::HexagonPointyTop || layout == GridLayout::HexagonFlatTop;
        const char* gridName = hex ? (render3D ? "Hex Board" : "Hex Grid") : layout == GridLayout::Isometric ? "Isometric Grid" : "Grid";
        Entity grid = scene->CreateEntity(gridName);
        if (parent)
            grid.SetParent(parent);
        auto& g = grid.AddComponent<GridComponent>();
        g.Layout = layout;
        g.Plane = plane;
        // Düzgün altıgen: genişlik 1, yükseklik 2/sqrt(3); izometrik: 2:1 elmas
        if (layout == GridLayout::HexagonPointyTop)
            g.CellSize = { 1.0f, 1.1547005f };
        else if (layout == GridLayout::HexagonFlatTop)
            g.CellSize = { 1.1547005f, 1.0f };
        else if (layout == GridLayout::Isometric)
            g.CellSize = { 1.0f, 0.5f };

        Entity tilemap = scene->CreateEntity("Tilemap");
        tilemap.SetParent(grid);
        auto& map = tilemap.AddComponent<TilemapComponent>();
        map.Render3D = render3D;
        return tilemap;
    }

    Entity TilePalettePanel::GetTarget() const
    {
        if (!m_Context)
            return {};
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (sel && sel.GetScene() == m_Context)
        {
            if (sel.HasComponent<TilemapComponent>())
                return sel;
            // Izgara seçili: ilk karo haritası çocuğu
            if (sel.HasComponent<GridComponent>())
                for (Entity child : sel.GetChildren())
                    if (child.HasComponent<TilemapComponent>())
                        return child;
        }
        auto view = m_Context->m_Registry.view<TilemapComponent>();
        if (!view.empty())
            return Entity{ view.front(), m_Context };
        return {};
    }

    bool TilePalettePanel::IsTargetSelected(Entity e) const
    {
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (!sel)
            return false;
        if (sel == e)
            return true;
        return sel.HasComponent<GridComponent>() && e.GetParent() == sel;
    }

    // ------------------------------------------------------------------ Geri al

    void TilePalettePanel::PushUndo(Entity e)
    {
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        Snapshot s;
        s.EntityID = (uint64_t)e.GetComponent<IDComponent>().ID;
        s.Cells = map.Cells;
        s.Palette = map.Palette;
        m_UndoStack.push_back(std::move(s));
        if (m_UndoStack.size() > 48)
            m_UndoStack.pop_front();
        m_RedoStack.clear();
    }

    void TilePalettePanel::Restore(std::deque<Snapshot>& from, std::deque<Snapshot>& to)
    {
        if (from.empty() || !m_Context)
            return;
        Snapshot s = std::move(from.back());
        from.pop_back();
        Entity e = m_Context->FindEntityByUUID(s.EntityID);
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        to.push_back(Snapshot{ s.EntityID, map.Cells, map.Palette });
        map.Cells = std::move(s.Cells);
        map.Palette = std::move(s.Palette);
        map.Version++;
        m_SelectedTile = std::clamp(m_SelectedTile, 0, std::max(0, (int)map.Palette.size() - 1));
        Touch(e);
    }

    void TilePalettePanel::Undo() { Restore(m_UndoStack, m_RedoStack); }
    void TilePalettePanel::Redo() { Restore(m_RedoStack, m_UndoStack); }

    void TilePalettePanel::Touch(Entity e)
    {
        if (auto* w = e.GetScene()->GetPhysicsWorld2D())
            if (e.HasComponent<TilemapCollider2DComponent>())
                w->MarkDirty(e);
    }

    // ------------------------------------------------------------------ Düzenleme

    void TilePalettePanel::BrushCells(const GridComponent& g, const glm::ivec2& center, std::vector<glm::ivec2>& out) const
    {
        out.clear();
        const int r = std::max(0, m_BrushSize - 1);
        const bool hex = IsHex(g);
        for (int dy = -r - 1; dy <= r + 1; ++dy)
            for (int dx = -r - 1; dx <= r + 1; ++dx)
            {
                glm::ivec2 c = center + glm::ivec2(dx, dy);
                bool inside = hex ? GridSystem::Distance(g, center, c) <= r : std::max(std::abs(dx), std::abs(dy)) <= r;
                if (inside)
                    out.push_back(c);
            }
    }

    void TilePalettePanel::PaintCells(Entity e, const std::vector<glm::ivec2>& cells, bool erase)
    {
        auto& map = e.GetComponent<TilemapComponent>();
        if (!erase && (m_SelectedTile < 0 || m_SelectedTile >= (int)map.Palette.size()))
            return;
        bool changed = false;
        for (const auto& c : cells)
        {
            uint64_t key = GridKey(c.x, c.y);
            auto it = map.Cells.find(key);
            if (erase)
            {
                if (it != map.Cells.end())
                {
                    map.Cells.erase(it);
                    changed = true;
                }
                continue;
            }
            if (it != map.Cells.end() && it->second.Tile == m_SelectedTile && it->second.Color == m_PaintColor && it->second.Elevation == m_Elevation)
                continue;
            map.Cells[key] = TileCell{ m_SelectedTile, m_PaintColor, m_Elevation };
            changed = true;
        }
        if (changed)
        {
            map.Version++;
            Touch(e);
        }
    }

    void TilePalettePanel::ApplyTool(Entity e, const glm::ivec2& a, const glm::ivec2& b, bool invert)
    {
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        const GridComponent& g = GridSystem::GridOf(e);
        std::vector<glm::ivec2> cells;
        switch (m_Tool)
        {
            case Tool::Paint:
            case Tool::Erase:
                PushUndo(e);
                BrushCells(g, a, cells);
                PaintCells(e, cells, invert || m_Tool == Tool::Erase);
                break;
            case Tool::Box:
            {
                PushUndo(e);
                glm::ivec2 mn = glm::min(a, b), mx = glm::max(a, b);
                for (int y = mn.y; y <= mx.y; ++y)
                    for (int x = mn.x; x <= mx.x; ++x)
                        cells.push_back({ x, y });
                PaintCells(e, cells, invert);
                break;
            }
            case Tool::Fill:
                if (!invert && (m_SelectedTile < 0 || m_SelectedTile >= (int)map.Palette.size()))
                    break;
                PushUndo(e);
                GridSystem::FloodFill(g, map, a, invert ? -1 : m_SelectedTile);
                Touch(e);
                break;
            case Tool::Picker:
            {
                int t = GridSystem::GetTile(map, a);
                if (t >= 0)
                {
                    m_SelectedTile = t;
                    m_Tool = Tool::Paint;
                }
                break;
            }
            default: break;
        }
    }

    void TilePalettePanel::RemovePaletteEntry(Entity e, int index)
    {
        auto& map = e.GetComponent<TilemapComponent>();
        if (index < 0 || index >= (int)map.Palette.size())
            return;
        PushUndo(e);
        map.Palette.erase(map.Palette.begin() + index);
        // Bu karoyu kullanan hücreleri sil, sonrakileri kaydır
        for (auto it = map.Cells.begin(); it != map.Cells.end();)
        {
            if (it->second.Tile == index)
            {
                it = map.Cells.erase(it);
                continue;
            }
            if (it->second.Tile > index)
                it->second.Tile--;
            ++it;
        }
        map.Version++;
        m_SelectedTile = std::clamp(m_SelectedTile, 0, std::max(0, (int)map.Palette.size() - 1));
        Touch(e);
    }

    // ------------------------------------------------------------------ Viewport

    void TilePalettePanel::DrawPreview(const ViewportToolContext& ctx, Entity e, const std::vector<glm::ivec2>& cells, unsigned int color)
    {
        if (!ctx.DrawList || !ctx.WorldToScreen)
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        std::vector<glm::vec3> corners;
        std::vector<ImVec2> pts;
        const ImU32 fill = (color & 0x00FFFFFF) | 0x38000000;
        for (const auto& c : cells)
        {
            // 3D karo haritasında mevcut karonun üst yüzüne oturt
            float h = 0.002f;
            if (map.Render3D)
            {
                auto it = map.Cells.find(GridKey(c.x, c.y));
                if (it != map.Cells.end() && it->second.Tile >= 0 && it->second.Tile < (int)map.Palette.size())
                    h = it->second.Elevation + std::max(0.01f, map.Palette[(size_t)it->second.Tile].Height) + 0.002f;
            }
            GridSystem::CellWorldCorners(e, c, corners, h);
            pts.clear();
            bool ok = true;
            for (auto& w : corners)
            {
                ImVec2 s;
                if (!ctx.WorldToScreen(w, s)) { ok = false; break; }
                pts.push_back(s);
            }
            if (!ok || pts.size() < 3)
                continue;
            // Dışbükey çokgen: sargı yönünden bağımsız dolgu için iki yönde de dene (ImGui tek yönde AA)
            ctx.DrawList->AddConvexPolyFilled(pts.data(), (int)pts.size(), fill);
            ctx.DrawList->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_Closed, 2.0f);
        }
    }

    bool TilePalettePanel::OnViewportTool(const ViewportToolContext& ctx)
    {
        m_HasHover = false;
        if (m_Tool == Tool::None || !EditorState::Get().IsEditing())
            return false;
        Entity e = GetTarget();
        if (!e || !IsTargetSelected(e))
        {
            m_Stroking = false;
            m_BoxActive = false;
            return false;
        }
        auto& map = e.GetComponent<TilemapComponent>();
        const GridComponent& g = GridSystem::GridOf(e);

        glm::vec3 hit;
        const bool onPlane = ctx.Hovered && GridSystem::RaycastPlane(e, ctx.RayOrigin, ctx.RayDirection, hit);
        if (!onPlane)
        {
            // Düzlem dışına çıkılırken bırakıldıysa kutuyu son hücreyle uygula
            if (m_BoxActive && !ctx.MouseDown)
            {
                ApplyTool(e, m_BoxStart, m_BoxEnd, ctx.Shift);
                m_BoxActive = false;
            }
            if (!ctx.MouseDown)
                m_Stroking = false;
            return m_Stroking || m_BoxActive;
        }

        const glm::ivec2 cell = GridSystem::WorldToCell(e, hit);
        m_HoverCell = cell;
        m_HasHover = true;
        const bool erase = m_Tool == Tool::Erase || ctx.Shift;
        const ImU32 previewColor = erase ? IM_COL32(255, 90, 90, 230)
                                 : m_Tool == Tool::Picker ? IM_COL32(255, 220, 90, 230)
                                 : (m_SelectedTile >= 0 && m_SelectedTile < (int)map.Palette.size())
                                       ? (ToU32(glm::vec4(glm::vec3(map.Palette[(size_t)m_SelectedTile].Color) * 0.5f + 0.5f, 1.0f)))
                                       : IM_COL32(120, 200, 255, 230);

        // Önizleme
        std::vector<glm::ivec2> cells;
        if (m_Tool == Tool::Box && m_BoxActive)
        {
            m_BoxEnd = cell;
            glm::ivec2 mn = glm::min(m_BoxStart, cell), mx = glm::max(m_BoxStart, cell);
            if ((int64_t)(mx.x - mn.x + 1) * (mx.y - mn.y + 1) <= 4096)
                for (int y = mn.y; y <= mx.y; ++y)
                    for (int x = mn.x; x <= mx.x; ++x)
                        cells.push_back({ x, y });
        }
        else if (m_Tool == Tool::Paint || m_Tool == Tool::Erase)
            BrushCells(g, cell, cells);
        else
            cells.push_back(cell);
        DrawPreview(ctx, e, cells, previewColor);
        if (ctx.DrawList && ctx.WorldToScreen)
        {
            ImVec2 s;
            if (ctx.WorldToScreen(hit, s))
            {
                char label[64];
                snprintf(label, sizeof(label), "%d, %d", cell.x, cell.y);
                ctx.DrawList->AddText(ImVec2(s.x + 14.0f, s.y + 10.0f), IM_COL32(0, 0, 0, 160), label);
                ctx.DrawList->AddText(ImVec2(s.x + 13.0f, s.y + 9.0f), IM_COL32(255, 255, 255, 230), label);
            }
        }

        // Ctrl+Click: damlalık
        if (ctx.Ctrl && ctx.MouseClicked && m_Tool != Tool::Picker)
        {
            int t = GridSystem::GetTile(map, cell);
            if (t >= 0)
                m_SelectedTile = t;
            return true;
        }

        switch (m_Tool)
        {
            case Tool::Paint:
            case Tool::Erase:
                if (ctx.MouseClicked)
                {
                    PushUndo(e);
                    m_Stroking = true;
                }
                if (m_Stroking && ctx.MouseDown)
                {
                    BrushCells(g, cell, cells);
                    PaintCells(e, cells, erase);
                }
                break;
            case Tool::Box:
                if (ctx.MouseClicked)
                {
                    m_BoxActive = true;
                    m_BoxStart = m_BoxEnd = cell;
                }
                if (m_BoxActive && !ctx.MouseDown)
                {
                    ApplyTool(e, m_BoxStart, cell, ctx.Shift);
                    m_BoxActive = false;
                }
                break;
            case Tool::Fill:
            case Tool::Picker:
                if (ctx.MouseClicked)
                    ApplyTool(e, cell, cell, ctx.Shift);
                break;
            default: break;
        }
        if (!ctx.MouseDown)
            m_Stroking = false;
        return true;
    }

    // ------------------------------------------------------------------ Panel

    void TilePalettePanel::OnImGuiRender(bool* open)
    {
        if (!ImGui::Begin(ICON_FA_TABLE_CELLS " Tile Palette###TilePalette", open))
        {
            ImGui::End();
            return;
        }

        Entity e = GetTarget();
        if (!e)
        {
            DrawCreate();
            ImGui::End();
            return;
        }

        auto& map = e.GetComponent<TilemapComponent>();
        Entity gridEntity;
        const GridComponent& g = GridSystem::GridOf(e, &gridEntity);
        const bool selected = IsTargetSelected(e);

        // Başlık: hedef karo haritası
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_TABLE_CELLS);
        ImGui::SameLine();
        ImGui::TextUnformatted(e.GetComponent<TagComponent>().Tag.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  %d tiles", kLayoutNames[std::clamp((int)g.Layout, 0, 3)], (int)map.Cells.size());
        if (!selected)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
                EditorState::Get().SelectEntity(e);
        }
        if (!gridEntity)
            UI::InfoBox("This Tilemap has no Grid parent; a default 1x1 rectangle grid is used.", UI::InfoType::Warning);

        DrawToolbar();
        if (!selected && m_Tool != Tool::None)
            UI::InfoBox("Select the Tilemap (or its Grid) to paint in the Scene view.", UI::InfoType::Warning);

        ImGui::BeginChild("##tilepalettebody");
        DrawPaletteAsset(e, map);
        DrawPalette(e, map);
        DrawTileEditor(e, map);
        DrawBrush(e, map);
        DrawMapSettings(e, map);
        ImGui::EndChild();

        // Kısayollar (panel odaktayken)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (!io.KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_B, false)) m_Tool = Tool::Paint;
                if (ImGui::IsKeyPressed(ImGuiKey_U, false)) m_Tool = Tool::Box;
                if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_Tool = Tool::Fill;
                if (ImGui::IsKeyPressed(ImGuiKey_D, false)) m_Tool = Tool::Erase;
                if (ImGui::IsKeyPressed(ImGuiKey_I, false)) m_Tool = Tool::Picker;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) SetBrushSize(m_BrushSize - 1);
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) SetBrushSize(m_BrushSize + 1);
            }
        }
        ImGui::End();
    }

    void TilePalettePanel::DrawCreate()
    {
        ImGui::Spacing();
        UI::InfoBox("No Tilemap in the scene. Create one to start painting.", UI::InfoType::Info);
        ImGui::Spacing();
        struct Preset { const char* Label; GridLayout Layout; GridPlane Plane; bool Render3D; };
        const Preset presets[] = {
            { ICON_FA_TABLE_CELLS "  2D Rectangle Tilemap", GridLayout::Rectangle, GridPlane::XY, false },
            { ICON_FA_DICE_D6 "  2D Hexagon Tilemap (Pointy)", GridLayout::HexagonPointyTop, GridPlane::XY, false },
            { ICON_FA_DICE_D6 "  2D Hexagon Tilemap (Flat)", GridLayout::HexagonFlatTop, GridPlane::XY, false },
            { ICON_FA_CUBES_STACKED "  2D Isometric Tilemap", GridLayout::Isometric, GridPlane::XY, false },
            { ICON_FA_CHESS_BOARD "  3D Hex Board", GridLayout::HexagonPointyTop, GridPlane::XZ, true },
            { ICON_FA_CHESS_BOARD "  3D Square Board", GridLayout::Rectangle, GridPlane::XZ, true },
        };
        for (const auto& p : presets)
        {
            if (UI::ButtonFullWidth(p.Label, 28.0f))
            {
                Entity created = CreateTilemap(m_Context, p.Layout, p.Plane, p.Render3D);
                if (created)
                {
                    EditorState::Get().SelectEntity(created);
                    m_Tool = Tool::Paint;
                }
            }
        }
    }

    void TilePalettePanel::DrawToolbar()
    {
        ImGui::Spacing();
        const float spacing = 4.0f;
        const float bw = std::max(28.0f, (ImGui::GetContentRegionAvail().x - spacing * 6.0f - 30.0f * 2.0f - 8.0f) / 5.0f);
        for (int i = 0; i < (int)Tool::Count; ++i)
        {
            if (i > 0) ImGui::SameLine(0.0f, spacing);
            bool active = (int)m_Tool == i;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            }
            ImGui::PushID(i);
            if (ImGui::Button(kToolIcons[i], ImVec2(bw, 30.0f)))
                m_Tool = active ? Tool::None : (Tool)i;
            ImGui::PopID();
            if (active)
                ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", kToolNames[i]);
        }
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(30, 30))) Undo();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo (Ctrl+Z)");
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT, ImVec2(30, 30))) Redo();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo (Ctrl+Y)");

        if (m_Tool != Tool::None)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
            ImGui::TextWrapped("%s", kToolHelp[(int)m_Tool]);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }

    int TilePalettePanel::EnsureTileInMap(Entity e, TilemapComponent& map, const std::string& tileAsset)
    {
        for (int i = 0; i < (int)map.Palette.size(); ++i)
            if (map.Palette[(size_t)i].Asset == tileAsset)
                return i;
        TileDefinition def;
        if (!GridSystem::LoadTileAsset(tileAsset, def))
            return -1;
        PushUndo(e);
        def.Asset = tileAsset;
        map.Palette.push_back(def);
        map.Version++;
        return (int)map.Palette.size() - 1;
    }

    bool TilePalettePanel::SelectPaletteAssetTile(int index)
    {
        Entity e = GetTarget();
        GridSystem::TilePaletteAsset palette;
        if (!e || m_PaletteAsset.empty() || !GridSystem::LoadPalette(m_PaletteAsset, palette) || index < 0 || index >= (int)palette.Tiles.size())
            return false;
        const int tile = EnsureTileInMap(e, e.GetComponent<TilemapComponent>(), palette.Tiles[(size_t)index]);
        if (tile < 0)
            return false;
        m_SelectedTile = tile;
        if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker)
            m_Tool = Tool::Paint;
        return true;
    }

    void TilePalettePanel::DrawPaletteAsset(Entity e, TilemapComponent& map)
    {
        namespace fs = std::filesystem;
        // Projedeki paletler (saniyede bir taranır)
        static std::vector<std::string> palettes;
        static double lastScan = -10.0;
        if (ImGui::GetTime() - lastScan > 1.0 && Project::GetActive())
        {
            lastScan = ImGui::GetTime();
            palettes.clear();
            std::error_code ec;
            const fs::path root = Project::GetActiveProjectDirectory();
            for (auto it = fs::recursive_directory_iterator(root / "Assets", fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                if (it->path().extension() == GridSystem::PaletteExtension)
                    palettes.push_back(fs::relative(it->path(), root, ec).generic_string());
            }
            std::sort(palettes.begin(), palettes.end());
        }
        if (!UI::BeginSection("Tile Palette Asset", true, ICON_FA_SWATCHBOOK))
            return;
        const std::string current = m_PaletteAsset.empty() ? std::string("None (use the Tilemap's own tiles)") : fs::path(m_PaletteAsset).stem().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##paletteasset", current.c_str()))
        {
            if (ImGui::Selectable("None (use the Tilemap's own tiles)", m_PaletteAsset.empty()))
                m_PaletteAsset.clear();
            for (const auto& p : palettes)
                if (ImGui::Selectable(p.c_str(), p == m_PaletteAsset))
                    m_PaletteAsset = p;
            ImGui::EndCombo();
        }
        if (palettes.empty())
            ImGui::TextDisabled("Create > 2D > Tile Palette in the Content Browser to make one.");
        GridSystem::TilePaletteAsset palette;
        if (!m_PaletteAsset.empty() && GridSystem::LoadPalette(m_PaletteAsset, palette))
        {
            const GridComponent& g = GridSystem::GridOf(e);
            const float cell = 52.0f, gap = 6.0f;
            const int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cell + gap)));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int i = 0; i < (int)palette.Tiles.size(); ++i)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0.0f, gap);
                ImGui::PushID(1000 + i);
                ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + cell, a.y + cell);
                TileDefinition def;
                const bool ok = GridSystem::LoadTileAsset(palette.Tiles[(size_t)i], def);
                if (ImGui::InvisibleButton("##assettile", ImVec2(cell, cell)) && ok)
                    SelectPaletteAssetTile(i);
                const bool hovered = ImGui::IsItemHovered();
                dl->AddRectFilled(a, b, IM_COL32(28, 30, 34, 255), 5.0f);
                if (ok)
                {
                    DrawSwatch(dl, ImVec2(a.x + 6, a.y + 4), ImVec2(b.x - 6, b.y - 14), def, IsHex(g));
                    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
                    std::string name = def.Name.size() > 8 ? def.Name.substr(0, 7) + ".." : def.Name;
                    ImVec2 ts = ImGui::CalcTextSize(name.c_str());
                    dl->AddText(ImVec2(a.x + (cell - ts.x) * 0.5f, b.y - ts.y - 1.0f), IM_COL32(220, 222, 226, 255), name.c_str());
                    ImGui::PopFont();
                    const char* kind = !def.Rules.empty() ? "Rule Tile" : (!def.Frames.empty() ? "Animated Tile" : "Tile");
                    if (hovered)
                        ImGui::SetTooltip("%s (%s)\n%s", def.Name.c_str(), kind, palette.Tiles[(size_t)i].c_str());
                }
                else
                    dl->AddText(ImVec2(a.x + 6, a.y + 18), IM_COL32(230, 120, 110, 255), "missing");
                const bool isSel = ok && m_SelectedTile >= 0 && m_SelectedTile < (int)map.Palette.size() &&
                                   map.Palette[(size_t)m_SelectedTile].Asset == palette.Tiles[(size_t)i];
                if (isSel)
                    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), ImGui::GetColorU32(UI::Colors::Accent), 5.0f, 0, 2.5f);
                else if (hovered)
                    dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 5.0f, 0, 1.5f);
                if (ImGui::BeginPopupContextItem("##assettilectx"))
                {
                    if (ImGui::MenuItem(ICON_FA_TRASH "  Remove from Palette"))
                    {
                        palette.Tiles.erase(palette.Tiles.begin() + i);
                        GridSystem::SavePalette(m_PaletteAsset, palette);
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            // Karo / görsel bırak: palete eklenir (görselden yeni Tile varlığı oluşturulur, Unity gibi)
            ImGui::Button(ICON_FA_PLUS "  Drop Tiles or Sprites here", ImVec2(-1.0f, 26.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    fs::path dropped((const char*)p->Data);
                    std::error_code ec;
                    const fs::path root = Project::GetActiveProjectDirectory();
                    std::string ext = dropped.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    std::string tilePath;
                    if (ext == GridSystem::TileExtension)
                        tilePath = fs::relative(dropped, root, ec).generic_string();
                    else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
                    {
                        // Unity: sprite'ı palete sürüklemek bir Tile varlığı oluşturur (paletin yanında)
                        fs::path out = root / fs::path(m_PaletteAsset).parent_path() / (dropped.stem().string() + GridSystem::TileExtension);
                        TileDefinition def;
                        def.Name = dropped.stem().string();
                        def.Texture = fs::relative(dropped, root, ec).generic_string();
                        if (!fs::exists(out, ec) && GridSystem::SaveTileAsset(out.string(), def))
                            tilePath = fs::relative(out, root, ec).generic_string();
                        else if (fs::exists(out, ec))
                            tilePath = fs::relative(out, root, ec).generic_string();
                    }
                    if (!tilePath.empty() && std::find(palette.Tiles.begin(), palette.Tiles.end(), tilePath) == palette.Tiles.end())
                    {
                        palette.Tiles.push_back(tilePath);
                        GridSystem::SavePalette(m_PaletteAsset, palette);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        UI::EndSection();
    }

    void TilePalettePanel::DrawPalette(Entity e, TilemapComponent& map)
    {
        if (!UI::BeginSection("Palette", true, ICON_FA_PALETTE))
            return;
        const GridComponent& g = GridSystem::GridOf(e);
        const float cell = 52.0f, gap = 6.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, (int)((avail + gap) / (cell + gap)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < (int)map.Palette.size(); ++i)
        {
            if (i % columns != 0)
                ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            ImGui::BeginGroup();
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImVec2 b(a.x + cell, a.y + cell);
            if (ImGui::InvisibleButton("##tile", ImVec2(cell, cell)))
            {
                m_SelectedTile = i;
                if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker)
                    m_Tool = Tool::Paint;
            }
            const bool hovered = ImGui::IsItemHovered();
            // Sağ tık: kopyala / sil
            if (ImGui::BeginPopupContextItem("##tilectx"))
            {
                if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate"))
                {
                    PushUndo(e);
                    TileDefinition copy = map.Palette[(size_t)i];
                    copy.Name += " Copy";
                    map.Palette.insert(map.Palette.begin() + i + 1, copy);
                    for (auto& [key, c] : map.Cells)
                        if (c.Tile > i) c.Tile++;
                    map.Version++;
                    m_SelectedTile = i + 1;
                }
                if (ImGui::MenuItem(ICON_FA_TRASH "  Remove", nullptr, false, map.Palette.size() > 1))
                    RemovePaletteEntry(e, i);
                ImGui::EndPopup();
            }
            if (i < (int)map.Palette.size())
            {
                const TileDefinition& def = map.Palette[(size_t)i];
                dl->AddRectFilled(a, b, IM_COL32(28, 30, 34, 255), 5.0f);
                DrawSwatch(dl, ImVec2(a.x + 6, a.y + 4), ImVec2(b.x - 6, b.y - 14), def, IsHex(g));
                // Ad (kısaltılmış)
                std::string name = def.Name;
                if (ImGui::CalcTextSize(name.c_str()).x > cell - 4.0f)
                {
                    while (name.size() > 1 && ImGui::CalcTextSize((name + "..").c_str()).x > cell - 4.0f)
                        name.pop_back();
                    name += "..";
                }
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
                ImVec2 ts = ImGui::CalcTextSize(name.c_str());
                dl->AddText(ImVec2(a.x + (cell - ts.x) * 0.5f, b.y - ts.y - 1.0f), IM_COL32(220, 222, 226, 255), name.c_str());
                ImGui::PopFont();
                const bool isSel = i == m_SelectedTile;
                if (isSel)
                    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), ImGui::GetColorU32(UI::Colors::Accent), 5.0f, 0, 2.5f);
                else if (hovered)
                    dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 5.0f, 0, 1.5f);
                if (hovered)
                    ImGui::SetTooltip("%s\n%s%s  Cost %d  Height %.2f", def.Name.c_str(), def.Solid ? "Solid  " : "", def.Walkable ? "Walkable" : "Blocked",
                                      def.Cost, def.Height);
                // Kısayol numarası (1-9)
                if (i < 9)
                {
                    char num[4];
                    snprintf(num, sizeof(num), "%d", i + 1);
                    dl->AddText(ImVec2(a.x + 3, a.y + 1), IM_COL32(255, 255, 255, 120), num);
                }
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        // Yeni karo
        if (!map.Palette.empty() && (int)map.Palette.size() % columns != 0)
            ImGui::SameLine(0.0f, gap);
        if (ImGui::Button(ICON_FA_PLUS "##addtile", ImVec2(cell, cell)))
        {
            PushUndo(e);
            TileDefinition def;
            def.Name = "Tile " + std::to_string(map.Palette.size());
            // Sıradaki renk: altın oran tonu
            float hue = std::fmod(0.13f + 0.618034f * (float)map.Palette.size(), 1.0f);
            ImVec4 rgb;
            ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, rgb.x, rgb.y, rgb.z);
            def.Color = { rgb.x, rgb.y, rgb.z, 1.0f };
            map.Palette.push_back(def);
            map.Version++;
            m_SelectedTile = (int)map.Palette.size() - 1;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Add a tile to the palette");
        // Doku sürükle-bırak: yeni karo
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string path((const char*)payload->Data, payload->DataSize - 1);
                PushUndo(e);
                TileDefinition def;
                def.Name = std::filesystem::path(path).stem().string();
                def.Texture = path;
                map.Palette.push_back(def);
                map.Version++;
                m_SelectedTile = (int)map.Palette.size() - 1;
            }
            ImGui::EndDragDropTarget();
        }

        // 1-9: karo seçimi (panel odaktayken)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl)
            for (int k = 0; k < 9 && k < (int)map.Palette.size(); ++k)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false))
                    m_SelectedTile = k;
        UI::EndSection();
    }

    void TilePalettePanel::DrawTileEditor(Entity e, TilemapComponent& map)
    {
        if (m_SelectedTile < 0 || m_SelectedTile >= (int)map.Palette.size())
            return;
        if (!UI::BeginSection("Selected Tile", true, ICON_FA_SQUARE))
            return;
        TileDefinition& def = map.Palette[(size_t)m_SelectedTile];
        bool changed = false;
        changed |= UI::InputText("Name", def.Name);
        changed |= UI::Color4("Color", def.Color);
        changed |= UI::InputText("Texture", def.Texture, "Drag a texture from the Content Browser");
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                def.Texture = std::string((const char*)payload->Data, payload->DataSize - 1);
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        UI::BeginProperty("##tilepickrow");
        if (std::string picked; ObjectPicker::Button("tiletexpick", "Texture", ObjectPicker::ImageExtensions(), def.Texture, picked))
        {
            def.Texture = picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Select texture");
        UI::EndProperty();
        changed |= UI::Checkbox("Solid", def.Solid, "Generates a collider in Tilemap Collider 2D");
        changed |= UI::Checkbox("Walkable", def.Walkable, "Pathfinding (GridFindPath) can walk over this tile");
        if (def.Walkable)
            changed |= UI::DragInt("Path Cost", def.Cost, 0.1f, 1, 100, "Higher cost tiles are avoided by pathfinding");
        changed |= UI::DragFloat("Height", def.Height, 0.01f, 0.01f, 50.0f, "%.2f", "Prism height when the Tilemap renders as 3D tiles");
        // Otomatik döşeme (Godot terrain / Unity Rule Tile)
        static const char* kAutotile[] = { "None", "16 Tiles (4 edges, 4x4 atlas)", "47 Tiles Blob (8 neighbours, 8x6 atlas)" };
        changed |= UI::Combo("Autotile", def.Autotile, kAutotile, 3,
                             "The texture is an atlas; each cell picks the variant that matches its neighbours (rectangle / isometric grids)");
        if (def.Autotile != 0)
        {
            changed |= UI::DragInt("Terrain", def.Terrain, 0.1f, -1, 64, "Tiles with the same Terrain id connect to each other (-1: only this tile)");
            if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES " Create Template Texture"))
            {
                // Sanatçı şablonu: projede Assets/Textures/Autotile_<mod>.png, karoya atanır
                const std::string relative = def.Autotile == 2 ? "Assets/Textures/Autotile_Blob47.png" : "Assets/Textures/Autotile_16.png";
                const auto projectDir = Project::GetActiveProjectDirectory();
                const std::string full = projectDir.empty() ? relative : (projectDir / relative).string();
                if (GridSystem::WriteAutotileTemplate(def.Autotile, 32, full))
                {
                    Texture2D::ReloadFromDisk(full);
                    def.Texture = relative;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Writes a layout template (dark = edge without a neighbour) you can paint over");
        }
        if (changed)
        {
            def.Cost = std::max(1, def.Cost);
            map.Version++;
            Touch(e);
        }
        UI::EndSection();
    }

    void TilePalettePanel::DrawBrush(Entity e, TilemapComponent& map)
    {
        if (!UI::BeginSection("Brush", true, ICON_FA_PAINTBRUSH))
            return;
        int size = m_BrushSize;
        if (UI::SliderInt("Size", size, 1, 16, "Brush radius in cells ([ and ])"))
            SetBrushSize(size);
        UI::Color4("Cell Tint", m_PaintColor, "Per-cell color multiplier applied while painting");
        if (map.Render3D)
            UI::DragFloat("Elevation", m_Elevation, 0.02f, -50.0f, 50.0f, "%.2f", "Extra height for painted 3D tiles");
        if (m_HasHover)
        {
            int t = GridSystem::GetTile(map, m_HoverCell);
            UI::Text("Hover Cell", "%d, %d  %s", m_HoverCell.x, m_HoverCell.y,
                     t >= 0 && t < (int)map.Palette.size() ? map.Palette[(size_t)t].Name.c_str() : "(empty)");
        }
        (void)e;
        UI::EndSection();
    }

    void TilePalettePanel::DrawMapSettings(Entity e, TilemapComponent& map)
    {
        if (!UI::BeginSection("Tilemap", false, ICON_FA_GEAR))
            return;
        Entity gridEntity;
        GridSystem::GridOf(e, &gridEntity);
        if (gridEntity)
        {
            auto& g = gridEntity.GetComponent<GridComponent>();
            int layout = (int)g.Layout;
            if (UI::Combo("Layout", layout, kLayoutNames, 4))
            {
                g.Layout = (GridLayout)layout;
                map.Version++;
                Touch(e);
            }
            const char* planes[] = { "XY (2D)", "XZ (3D Board)" };
            int plane = (int)g.Plane;
            if (UI::Combo("Plane", plane, planes, 2))
            {
                g.Plane = (GridPlane)plane;
                map.Version++;
            }
            if (UI::Vec2("Cell Size", g.CellSize, 0.01f))
            {
                g.CellSize = glm::max(g.CellSize, glm::vec2(0.01f));
                map.Version++;
                Touch(e);
            }
        }
        UI::Checkbox("Render As 3D Tiles", map.Render3D);
        UI::Checkbox("Cell Outlines", map.Outline);
        UI::Color4("Tint", map.Tint);

        // Sınırlar
        if (!map.Cells.empty())
        {
            glm::ivec2 mn(INT32_MAX), mx(INT32_MIN);
            for (auto& [key, c] : map.Cells)
            {
                glm::ivec2 p = GridKeyToCell(key);
                mn = glm::min(mn, p);
                mx = glm::max(mx, p);
            }
            UI::Text("Bounds", "(%d, %d) - (%d, %d)", mn.x, mn.y, mx.x, mx.y);
        }
        UI::Text("Painted Cells", "%d", (int)map.Cells.size());
        ImGui::Spacing();
        if (!m_ConfirmClear)
        {
            if (UI::DangerButton(ICON_FA_TRASH "  Clear All Tiles", ImVec2(-1, 0)) && !map.Cells.empty())
                m_ConfirmClear = true;
        }
        else
        {
            ImGui::TextColored(UI::Colors::Warning, "Erase %d tiles?", (int)map.Cells.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Erase"))
            {
                PushUndo(e);
                map.Cells.clear();
                map.Version++;
                Touch(e);
                m_ConfirmClear = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel"))
                m_ConfirmClear = false;
        }
        UI::EndSection();
    }

    // ------------------------------------------------------------------ Denetçi

    void TilePalettePanel::DrawTilemapInspector(Entity e)
    {
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        const GridComponent& g = GridSystem::GridOf(e);
        ImGui::Spacing();
        // Palet şeridi
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float sw = 22.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float x = p.x;
        const float maxX = p.x + ImGui::GetContentRegionAvail().x;
        int shown = 0;
        for (const auto& def : map.Palette)
        {
            if (x + sw > maxX)
                break;
            DrawSwatch(dl, ImVec2(x, p.y), ImVec2(x + sw, p.y + sw), def, IsHex(g));
            x += sw + 4.0f;
            ++shown;
        }
        ImGui::Dummy(ImVec2(0.0f, shown > 0 ? sw : 0.0f));
        ImGui::TextDisabled("%d tiles painted  |  %d palette entries", (int)map.Cells.size(), (int)map.Palette.size());
        if (s_OpenPanel && UI::AccentButton(ICON_FA_PALETTE "  Open Tile Palette", ImVec2(-1, 0)))
        {
            EditorState::Get().SelectEntity(e);
            s_OpenPanel();
        }
    }
}
