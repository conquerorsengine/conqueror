#include "ObjectPicker.h"
#include "TilePalettePanel.h"
#include "EditorState.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Grid/GridSystem.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Project/Project.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "UndoRedo/UndoRedoManager.h"
#include "UndoRedo/EditorCommand.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace Conqueror::Editor
{
    std::function<void()> TilePalettePanel::s_OpenPanel;

    namespace
    {
        using Tool = TilePalettePanel::Tool;
        using BrushType = TilePalettePanel::BrushType;

        // Araç çubuğu Unity sırasıyla: Select, Move, Paint, Box Fill, Picker, Eraser, Flood Fill
        const Tool kToolOrder[] = { Tool::Select, Tool::Move, Tool::Paint, Tool::Box, Tool::Picker, Tool::Erase, Tool::Fill };
        const char* ToolName(Tool t)
        {
            switch (t)
            {
                case Tool::Select: return "Select (S)";
                case Tool::Move: return "Move (M)";
                case Tool::Paint: return "Paint Brush (B)";
                case Tool::Box: return "Box Fill (U)";
                case Tool::Picker: return "Picker (I)";
                case Tool::Erase: return "Eraser (D)";
                case Tool::Fill: return "Flood Fill (G)";
                default: return "";
            }
        }
        const char* ToolIcon(Tool t)
        {
            switch (t)
            {
                case Tool::Select: return ICON_FA_OBJECT_GROUP;
                case Tool::Move: return ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT;
                case Tool::Paint: return ICON_FA_PAINTBRUSH;
                case Tool::Box: return ICON_FA_VECTOR_SQUARE;
                case Tool::Picker: return ICON_FA_EYE_DROPPER;
                case Tool::Erase: return ICON_FA_ERASER;
                case Tool::Fill: return ICON_FA_FILL_DRIP;
                default: return "";
            }
        }
        const char* ToolHelp(Tool t)
        {
            switch (t)
            {
                case Tool::Select: return "Drag a rectangle in the Scene view to select cells (Grid Selection below). Delete removes them, Ctrl+C copies them to the brush.";
                case Tool::Move: return "Drag inside the selection to move its tiles.";
                case Tool::Paint: return "Click and drag in the Scene view to paint. Shift erases, Ctrl+Click picks a tile. [ ] rotate, Shift+[ ] flip.";
                case Tool::Box: return "Drag a rectangle in the Scene view to fill it (a multi-cell brush is repeated). Shift erases the rectangle.";
                case Tool::Picker: return "Click a cell to pick its tile, or drag a rectangle to pick a multi-cell brush; then switches back to the brush.";
                case Tool::Erase: return "Click and drag to erase tiles.";
                case Tool::Fill: return "Click to flood-fill connected cells of the same tile. Shift erases the region.";
                default: return "";
            }
        }
        const char* kBrushNames[] = { "Default Brush", "Line Brush", "Random Brush", "GameObject Brush", "Group Brush", "Prefab Brush" };
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

        uint64_t UUIDOf(Entity e) { return e && e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0; }

        // apply(T, q) == apply(C, apply(B, q)): fırça dönüşümü B'nin hücre dönüşümü C'nin üstüne uygulanmışı
        uint8_t ComposeTransform(uint8_t brush, uint8_t cell)
        {
            const glm::vec2 probes[3] = { { 0.13f, 0.27f }, { 0.71f, 0.33f }, { 0.41f, 0.89f } };
            for (uint8_t t = 0; t < 8; ++t)
            {
                bool ok = true;
                for (const auto& q : probes)
                    ok = ok && glm::all(glm::lessThan(glm::abs(ApplyTileTransform(t, q) - ApplyTileTransform(cell, ApplyTileTransform(brush, q))), glm::vec2(1e-4f)));
                if (ok)
                    return t;
            }
            return cell;
        }

        bool SameDefinition(const TileDefinition& a, const TileDefinition& b)
        {
            if (!a.Asset.empty() || !b.Asset.empty())
                return a.Asset == b.Asset;
            return a.Name == b.Name && a.Texture == b.Texture && a.Color == b.Color && a.Solid == b.Solid && a.Walkable == b.Walkable &&
                   a.Height == b.Height && a.Autotile == b.Autotile;
        }

        std::filesystem::path BrushPicksPath()
        {
            const std::filesystem::path dir = Project::GetActiveProjectDirectory();
            return dir.empty() ? std::filesystem::path() : dir / "UserSettings" / "TileBrushPicks.yaml";
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
        // Unity Active Tilemap: seçimden bağımsız boyama hedefi
        if (m_ActiveTilemap != 0)
            if (Entity a = m_Context->FindEntityByUUID(m_ActiveTilemap); a && a.HasComponent<TilemapComponent>())
                return a;
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

    bool TilePalettePanel::IsTargetActive(Entity e) const
    {
        if (!e)
            return false;
        if (m_ActiveTilemap != 0 && UUIDOf(e) == m_ActiveTilemap)
            return true;
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (!sel)
            return false;
        if (sel == e)
            return true;
        return sel.HasComponent<GridComponent>() && e.GetParent() == sel;
    }

    void TilePalettePanel::SetActiveTilemap(Entity e)
    {
        m_ActiveTilemap = e && e.HasComponent<TilemapComponent>() ? UUIDOf(e) : 0;
        m_HasSelection = false;
    }

    // ------------------------------------------------------------------ Geri al

    void TilePalettePanel::PushUndo(Entity e)
    {
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        Snapshot s;
        s.EntityID = UUIDOf(e);
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

    // ------------------------------------------------------------------ Fırça

    void TilePalettePanel::RotateBrush(int quarterTurnsCCW)
    {
        m_BrushTransform = RotateTileTransform(m_BrushTransform, quarterTurnsCCW);
    }

    void TilePalettePanel::FlipBrush(bool vertical)
    {
        m_BrushTransform = vertical ? FlipTileTransformY(m_BrushTransform) : FlipTileTransformX(m_BrushTransform);
    }

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

    int TilePalettePanel::BrushTileAt()
    {
        if (m_BrushType == BrushType::Random && !m_RandomTiles.empty())
        {
            float total = 0.0f;
            for (const auto& [t, w] : m_RandomTiles)
                total += std::max(0.0f, w);
            if (total > 0.0f)
            {
                float pick = std::uniform_real_distribution<float>(0.0f, total)(m_Random);
                for (const auto& [t, w] : m_RandomTiles)
                {
                    pick -= std::max(0.0f, w);
                    if (pick <= 0.0f)
                        return t;
                }
                return m_RandomTiles.back().first;
            }
        }
        return m_SelectedTile;
    }

    TileCell TilePalettePanel::MakeCell(const TilemapComponent& map, int tile, uint8_t extraTransform) const
    {
        TileCell c;
        c.Tile = tile;
        const TileDefinition* def = tile >= 0 && tile < (int)map.Palette.size() ? &map.Palette[(size_t)tile] : nullptr;
        // Unity Tile Flags: Lock Color / Lock Transform
        c.Color = def && def->LockColor ? glm::vec4(1.0f) : m_PaintColor;
        c.Transform = def && def->LockTransform ? 0 : ComposeTransform(m_BrushTransform, extraTransform);
        c.Elevation = m_Elevation;
        return c;
    }

    void TilePalettePanel::PaintCells(Entity e, const std::vector<glm::ivec2>& cells, bool erase)
    {
        auto& map = e.GetComponent<TilemapComponent>();
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
            const int tile = BrushTileAt();
            if (tile < 0 || tile >= (int)map.Palette.size())
                continue;
            const TileCell cell = MakeCell(map, tile);
            if (it != map.Cells.end() && it->second.Tile == cell.Tile && it->second.Color == cell.Color && it->second.Elevation == cell.Elevation &&
                it->second.Transform == cell.Transform)
                continue;
            map.Cells[key] = cell;
            changed = true;
        }
        if (changed)
        {
            map.Version++;
            Touch(e);
        }
    }

    void TilePalettePanel::StampCells(const GridComponent& g, const glm::ivec2& at, std::vector<std::pair<glm::ivec2, const StampCell*>>& out) const
    {
        out.clear();
        if (m_Stamp.empty())
            return;
        glm::ivec2 size(1);
        for (const auto& s : m_Stamp)
            size = glm::max(size, s.Offset + glm::ivec2(1));
        // Desen konumları fırça dönüşümüyle (altıgende hücre komşuluğu dönmez: desen yerinde kalır, hücreler döner)
        const bool turnPattern = !IsHex(g);
        for (const auto& s : m_Stamp)
        {
            glm::ivec2 o = s.Offset;
            glm::ivec2 dim = size;
            if (turnPattern)
            {
                // InverseTileTransform'un tamsayı karşılığı: önce ters dönüşler, sonra ayna
                for (int k = 0; k < TileTurns(m_BrushTransform); ++k)
                {
                    o = glm::ivec2(dim.y - 1 - o.y, o.x);
                    dim = glm::ivec2(dim.y, dim.x);
                }
                if (TileMirrored(m_BrushTransform))
                    o.x = dim.x - 1 - o.x;
            }
            out.push_back({ at + o, &s });
        }
    }

    int TilePalettePanel::EnsureTile(Entity e, TilemapComponent& map, const TileDefinition& def)
    {
        (void)e;
        for (int i = 0; i < (int)map.Palette.size(); ++i)
            if (SameDefinition(map.Palette[(size_t)i], def))
                return i;
        map.Palette.push_back(def);
        map.Version++;
        return (int)map.Palette.size() - 1;
    }

    void TilePalettePanel::PaintStamp(Entity e, const glm::ivec2& at)
    {
        auto& map = e.GetComponent<TilemapComponent>();
        const GridComponent& g = GridSystem::GridOf(e);
        std::vector<std::pair<glm::ivec2, const StampCell*>> cells;
        StampCells(g, at, cells);
        bool changed = false;
        for (const auto& [c, s] : cells)
        {
            const int tile = EnsureTile(e, map, s->Def);
            TileCell cell = s->Cell;
            cell.Tile = tile;
            const TileDefinition& def = map.Palette[(size_t)tile];
            cell.Transform = def.LockTransform ? 0 : ComposeTransform(m_BrushTransform, s->Cell.Transform);
            if (def.LockColor)
                cell.Color = glm::vec4(1.0f);
            auto& slot = map.Cells[GridKey(c.x, c.y)];
            if (slot.Tile != cell.Tile || slot.Color != cell.Color || slot.Transform != cell.Transform || slot.Elevation != cell.Elevation)
            {
                slot = cell;
                changed = true;
            }
        }
        if (changed)
        {
            map.Version++;
            Touch(e);
        }
    }

    void TilePalettePanel::LineCells(const GridComponent& g, const glm::ivec2& a, const glm::ivec2& b, std::vector<glm::ivec2>& out) const
    {
        out.clear();
        if (!IsHex(g))
        {
            // Bresenham; Fill Gaps (Unity Line Brush): çapraz adımlar yatay + dikey olur (kesintisiz kenar)
            glm::ivec2 p = a;
            const glm::ivec2 d(std::abs(b.x - a.x), -std::abs(b.y - a.y)), s(a.x < b.x ? 1 : -1, a.y < b.y ? 1 : -1);
            int err = d.x + d.y;
            out.push_back(p);
            while (p != b)
            {
                const int e2 = 2 * err;
                glm::ivec2 next = p;
                if (e2 >= d.y) { err += d.y; next.x += s.x; }
                if (e2 <= d.x) { err += d.x; next.y += s.y; }
                if (m_LineFillGaps && next.x != p.x && next.y != p.y)
                    out.push_back({ next.x, p.y });
                p = next;
                out.push_back(p);
            }
            return;
        }
        // Altıgen: hücre merkezleri arasında eşit adımlar
        const int n = std::max(1, GridSystem::Distance(g, a, b));
        const glm::vec2 pa = GridSystem::CellToPlane(g, a), pb = GridSystem::CellToPlane(g, b);
        for (int i = 0; i <= n; ++i)
        {
            const glm::ivec2 c = GridSystem::PlaneToCell(g, glm::mix(pa, pb, (float)i / (float)n + 1e-4f));
            if (out.empty() || out.back() != c)
                out.push_back(c);
        }
    }

    void TilePalettePanel::PickStamp(Entity e, const glm::ivec2& a, const glm::ivec2& b)
    {
        auto& map = e.GetComponent<TilemapComponent>();
        const glm::ivec2 mn = glm::min(a, b), mx = glm::max(a, b);
        m_Stamp.clear();
        for (int y = mn.y; y <= mx.y; ++y)
            for (int x = mn.x; x <= mx.x; ++x)
            {
                auto it = map.Cells.find(GridKey(x, y));
                if (it == map.Cells.end() || it->second.Tile < 0 || it->second.Tile >= (int)map.Palette.size())
                    continue;
                m_Stamp.push_back({ glm::ivec2(x, y) - mn, it->second, map.Palette[(size_t)it->second.Tile] });
            }
        m_BrushTransform = 0;
        if (m_Stamp.size() == 1)
        {
            m_SelectedTile = m_Stamp[0].Cell.Tile;
            m_BrushTransform = m_Stamp[0].Cell.Transform;
            m_Stamp.clear();
        }
    }

    void TilePalettePanel::PickGroup(Entity e, const glm::ivec2& start)
    {
        // Unity Group Brush: başlangıç hücresine bağlı (Gap kadar boşluk atlayarak) dolu hücreler, en çok Limit tane
        auto& map = e.GetComponent<TilemapComponent>();
        if (!map.Cells.count(GridKey(start.x, start.y)))
            return;
        std::vector<glm::ivec2> group;
        std::set<std::pair<int, int>> seen{ { start.x, start.y } };
        std::deque<glm::ivec2> open{ start };
        const int reach = m_GroupGap + 1;
        while (!open.empty() && (int)group.size() < m_GroupLimit)
        {
            const glm::ivec2 c = open.front();
            open.pop_front();
            group.push_back(c);
            for (int dy = -reach; dy <= reach; ++dy)
                for (int dx = -reach; dx <= reach; ++dx)
                {
                    const glm::ivec2 n = c + glm::ivec2(dx, dy);
                    if ((dx == 0 && dy == 0) || seen.count({ n.x, n.y }) || !map.Cells.count(GridKey(n.x, n.y)))
                        continue;
                    seen.insert({ n.x, n.y });
                    open.push_back(n);
                }
        }
        glm::ivec2 mn(INT32_MAX);
        for (const auto& c : group)
            mn = glm::min(mn, c);
        m_Stamp.clear();
        for (const auto& c : group)
        {
            const TileCell& cell = map.Cells[GridKey(c.x, c.y)];
            if (cell.Tile >= 0 && cell.Tile < (int)map.Palette.size())
                m_Stamp.push_back({ c - mn, cell, map.Palette[(size_t)cell.Tile] });
        }
        m_BrushTransform = 0;
    }

    // GameObject / Prefab fırçası: örnekler karo haritasının çocuğu, hücre merkezinde (Unity GameObject Brush)
    Entity TilePalettePanel::ObjectAtCell(Entity e, const glm::ivec2& cell) const
    {
        const GridComponent& g = GridSystem::GridOf(e);
        for (Entity child : e.GetChildren())
        {
            if (child.HasComponent<TileObjectComponent>() || !child.HasComponent<TransformComponent>())
                continue;   // karo prefab örnekleri haritadan yönetilir
            const glm::ivec2 c = GridSystem::PlaneToCell(g, GridSystem::LocalToPlane(g, child.GetComponent<TransformComponent>().Position));
            if (c == cell)
                return child;
        }
        return {};
    }

    void TilePalettePanel::PaintObject(Entity e, const glm::ivec2& cell)
    {
        if (!m_Context || ObjectAtCell(e, cell))
            return;
        std::string prefab = m_GameObjectPrefab;
        if (m_BrushType == BrushType::Prefab)
        {
            if (m_Prefabs.empty())
                return;
            prefab = m_Prefabs[std::uniform_int_distribution<size_t>(0, m_Prefabs.size() - 1)(m_Random)];
        }
        if (prefab.empty())
            return;
        Entity inst = PrefabSerializer::Instantiate(*m_Context, prefab, e);
        if (!inst)
            return;
        const GridComponent& g = GridSystem::GridOf(e);
        auto& tc = inst.GetComponent<TransformComponent>();
        tc.Position = GridSystem::PlaneToLocal(g, GridSystem::CellToPlane(g, cell), m_Elevation);
        int turns = TileTurns(m_BrushTransform);
        if (m_BrushType == BrushType::Prefab && m_PrefabRandomRotation)
            turns += std::uniform_int_distribution<int>(0, 3)(m_Random);
        const float turn = glm::radians(90.0f * (float)turns);
        if (g.Plane == GridPlane::XZ) tc.Rotation.y += turn; else tc.Rotation.z += turn;
        if (TileMirrored(m_BrushTransform))
            tc.Scale.x = -tc.Scale.x;
    }

    void TilePalettePanel::EraseObject(Entity e, const glm::ivec2& cell)
    {
        Entity obj = ObjectAtCell(e, cell);
        if (!obj)
            return;
        if (auto scene = m_SceneRef.lock(); scene && scene.get() == m_Context)
            UndoRedoManager::Get().Execute(std::make_shared<DestroyEntityCommand>(scene, obj));
        else
            m_Context->DestroyEntity(obj);
    }

    // ------------------------------------------------------------------ Seçim (Grid Selection)

    void TilePalettePanel::SetSelection(const glm::ivec2& a, const glm::ivec2& b)
    {
        m_SelMin = glm::min(a, b);
        m_SelMax = glm::max(a, b);
        m_HasSelection = true;
    }

    void TilePalettePanel::MoveSelection(Entity e, const glm::ivec2& offset)
    {
        if (!e || !m_HasSelection || offset == glm::ivec2(0))
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        PushUndo(e);
        std::vector<std::pair<glm::ivec2, TileCell>> moved;
        for (int y = m_SelMin.y; y <= m_SelMax.y; ++y)
            for (int x = m_SelMin.x; x <= m_SelMax.x; ++x)
            {
                auto it = map.Cells.find(GridKey(x, y));
                if (it == map.Cells.end())
                    continue;
                moved.push_back({ glm::ivec2(x, y) + offset, it->second });
                map.Cells.erase(it);
            }
        for (const auto& [c, cell] : moved)
            map.Cells[GridKey(c.x, c.y)] = cell;
        m_SelMin += offset;
        m_SelMax += offset;
        map.Version++;
        Touch(e);
    }

    void TilePalettePanel::DeleteSelection(Entity e)
    {
        if (!e || !m_HasSelection)
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        PushUndo(e);
        for (int y = m_SelMin.y; y <= m_SelMax.y; ++y)
            for (int x = m_SelMin.x; x <= m_SelMax.x; ++x)
                map.Cells.erase(GridKey(x, y));
        map.Version++;
        Touch(e);
    }

    void TilePalettePanel::CopySelectionToBrush(Entity e)
    {
        if (!e || !m_HasSelection)
            return;
        PickStamp(e, m_SelMin, m_SelMax);
        m_Tool = Tool::Paint;
    }

    // ------------------------------------------------------------------ Araç uygulama

    void TilePalettePanel::ApplyTool(Entity e, const glm::ivec2& a, const glm::ivec2& b, bool invert)
    {
        if (!e || !e.HasComponent<TilemapComponent>())
            return;
        auto& map = e.GetComponent<TilemapComponent>();
        const GridComponent& g = GridSystem::GridOf(e);
        const bool objects = m_BrushType == BrushType::GameObject || m_BrushType == BrushType::Prefab;
        std::vector<glm::ivec2> cells;
        switch (m_Tool)
        {
            case Tool::Paint:
            case Tool::Erase:
            {
                const bool erase = invert || m_Tool == Tool::Erase;
                if (objects)
                {
                    if (m_BrushType == BrushType::Prefab || a == b)
                        cells = { a };
                    else
                        LineCells(g, a, b, cells);
                    for (const auto& c : cells)
                        erase ? EraseObject(e, c) : PaintObject(e, c);
                    break;
                }
                PushUndo(e);
                if (m_BrushType == BrushType::Line)
                {
                    LineCells(g, a, b, cells);
                    PaintCells(e, cells, erase);
                }
                else if (UsesStamp() && !erase)
                    PaintStamp(e, a);
                else
                {
                    BrushCells(g, a, cells);
                    PaintCells(e, cells, erase);
                }
                break;
            }
            case Tool::Box:
            {
                PushUndo(e);
                const glm::ivec2 mn = glm::min(a, b), mx = glm::max(a, b);
                if (UsesStamp() && !invert)
                {
                    // Çok hücreli fırça kutuyu döşer (desen tekrarlanır)
                    glm::ivec2 size(1);
                    std::vector<std::pair<glm::ivec2, const StampCell*>> pattern;
                    StampCells(g, glm::ivec2(0), pattern);
                    for (const auto& [c, s] : pattern)
                        size = glm::max(size, c + glm::ivec2(1));
                    for (int y = mn.y; y <= mx.y; y += size.y)
                        for (int x = mn.x; x <= mx.x; x += size.x)
                            for (const auto& [c, s] : pattern)
                            {
                                const glm::ivec2 cell = glm::ivec2(x, y) + c;
                                if (cell.x > mx.x || cell.y > mx.y)
                                    continue;
                                const int tile = EnsureTile(e, map, s->Def);
                                TileCell tc = s->Cell;
                                tc.Tile = tile;
                                tc.Transform = map.Palette[(size_t)tile].LockTransform ? 0 : ComposeTransform(m_BrushTransform, s->Cell.Transform);
                                map.Cells[GridKey(cell.x, cell.y)] = tc;
                            }
                    map.Version++;
                    Touch(e);
                    break;
                }
                for (int y = mn.y; y <= mx.y; ++y)
                    for (int x = mn.x; x <= mx.x; ++x)
                        cells.push_back({ x, y });
                PaintCells(e, cells, invert);
                break;
            }
            case Tool::Fill:
            {
                const int tile = BrushTileAt();
                if (!invert && (tile < 0 || tile >= (int)map.Palette.size()))
                    break;
                PushUndo(e);
                const int before = GridSystem::GetTile(map, a);
                GridSystem::FloodFill(g, map, a, invert ? -1 : tile);
                // Doldurulan hücreler fırçanın rengini / dönüşümünü alır
                if (!invert && before != tile)
                    for (auto& [key, cell] : map.Cells)
                        if (cell.Tile == tile && cell.Transform == 0 && cell.Color == glm::vec4(1.0f))
                        {
                            const TileCell made = MakeCell(map, tile);
                            cell.Color = made.Color;
                            cell.Transform = made.Transform;
                        }
                Touch(e);
                break;
            }
            case Tool::Picker:
            {
                if (a == b && m_BrushType == BrushType::Group)
                    PickGroup(e, a);
                else if (a == b)
                {
                    auto it = map.Cells.find(GridKey(a.x, a.y));
                    if (it != map.Cells.end() && it->second.Tile >= 0 && it->second.Tile < (int)map.Palette.size())
                    {
                        m_SelectedTile = it->second.Tile;
                        m_BrushTransform = it->second.Transform;
                        m_Stamp.clear();
                    }
                    else
                        break;
                }
                else
                    PickStamp(e, a, b);
                m_Tool = Tool::Paint;
                break;
            }
            case Tool::Select:
                SetSelection(a, b);
                break;
            case Tool::Move:
                MoveSelection(e, b - a);
                break;
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
        m_RandomTiles.erase(std::remove_if(m_RandomTiles.begin(), m_RandomTiles.end(), [&](auto& p) { return p.first == index; }), m_RandomTiles.end());
        for (auto& p : m_RandomTiles)
            if (p.first > index) p.first--;
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
            ctx.DrawList->AddConvexPolyFilled(pts.data(), (int)pts.size(), fill);
            ctx.DrawList->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_Closed, 2.0f);
        }
    }

    void TilePalettePanel::HandleShortcuts(Entity e, bool viewportHovered)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
            return;
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!panelFocused && !viewportHovered)
            return;
        if (io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && m_HasSelection) CopySelectionToBrush(e);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) m_Tool = Tool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) m_Tool = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) m_Tool = Tool::Paint;
        if (ImGui::IsKeyPressed(ImGuiKey_U, false)) m_Tool = Tool::Box;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_Tool = Tool::Fill;
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) m_Tool = Tool::Erase;
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) m_Tool = Tool::Picker;
        // Unity: [ saat yönü tersine, ] saat yönüne döndür; Shift+[ yatay, Shift+] dikey aynala
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) io.KeyShift ? FlipBrush(false) : RotateBrush(1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) io.KeyShift ? FlipBrush(true) : RotateBrush(-1);
        // Fırça boyutu (Unity'de yok): - / =
        if (ImGui::IsKeyPressed(ImGuiKey_Minus)) SetBrushSize(m_BrushSize - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Equal)) SetBrushSize(m_BrushSize + 1);
        if (m_HasSelection && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
            DeleteSelection(e);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_HasSelection = false;
            m_Stamp.clear();
        }
    }

    bool TilePalettePanel::OnViewportTool(const ViewportToolContext& ctx)
    {
        m_HasHover = false;
        Entity e = GetTarget();
        if (!EditorState::Get().IsEditing() || !e)
            return false;
        auto& map = e.GetComponent<TilemapComponent>();
        // Scene görünümü katmanı (Unity 6 Tile Palette Clipboard + Brush Picks): üstündeyken boyanmaz
        if (m_ShowOverlay && IsTargetActive(e) && DrawSceneOverlay(ctx, e, map))
            return true;
        if (m_Tool == Tool::None || !IsTargetActive(e))
        {
            m_Stroking = false;
            m_BoxActive = false;
            m_LineActive = false;
            m_SelDragging = false;
            m_Moving = false;
            return false;
        }
        HandleShortcuts(e, ctx.Hovered);
        const GridComponent& g = GridSystem::GridOf(e);

        // Seçimin çerçevesi her araçta görünür
        if (m_HasSelection)
        {
            std::vector<glm::ivec2> sel;
            const glm::ivec2 off = m_Moving ? m_MoveOffset : glm::ivec2(0);
            if ((int64_t)(m_SelMax.x - m_SelMin.x + 1) * (m_SelMax.y - m_SelMin.y + 1) <= 4096)
                for (int y = m_SelMin.y; y <= m_SelMax.y; ++y)
                    for (int x = m_SelMin.x; x <= m_SelMax.x; ++x)
                        sel.push_back(glm::ivec2(x, y) + off);
            DrawPreview(ctx, e, sel, IM_COL32(255, 210, 60, 220));
        }

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
            {
                m_Stroking = false;
                m_LineActive = false;
                m_SelDragging = false;
                m_Moving = false;
            }
            return m_Stroking || m_BoxActive || m_LineActive || m_SelDragging || m_Moving;
        }

        const glm::ivec2 cell = GridSystem::WorldToCell(e, hit);
        const bool newCell = !m_HasHover || cell != m_HoverCell;
        m_HoverCell = cell;
        m_HasHover = true;
        const bool objects = m_BrushType == BrushType::GameObject || m_BrushType == BrushType::Prefab;
        const bool erase = m_Tool == Tool::Erase || ctx.Shift;
        const ImU32 previewColor = erase ? IM_COL32(255, 90, 90, 230)
                                 : (m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move) ? IM_COL32(255, 220, 90, 230)
                                 : (m_SelectedTile >= 0 && m_SelectedTile < (int)map.Palette.size())
                                       ? (ToU32(glm::vec4(glm::vec3(map.Palette[(size_t)m_SelectedTile].Color) * 0.5f + 0.5f, 1.0f)))
                                       : IM_COL32(120, 200, 255, 230);

        // Önizleme
        std::vector<glm::ivec2> cells;
        auto boxCells = [&](const glm::ivec2& p0, const glm::ivec2& p1) {
            const glm::ivec2 mn = glm::min(p0, p1), mx = glm::max(p0, p1);
            if ((int64_t)(mx.x - mn.x + 1) * (mx.y - mn.y + 1) <= 4096)
                for (int y = mn.y; y <= mx.y; ++y)
                    for (int x = mn.x; x <= mx.x; ++x)
                        cells.push_back({ x, y });
        };
        if ((m_Tool == Tool::Box || m_Tool == Tool::Picker) && m_BoxActive)
        {
            m_BoxEnd = cell;
            boxCells(m_BoxStart, cell);
        }
        else if (m_Tool == Tool::Select && m_SelDragging)
            boxCells(m_SelStart, cell);
        else if ((m_Tool == Tool::Paint || m_Tool == Tool::Erase) && m_BrushType == BrushType::Line && m_LineActive)
            LineCells(g, m_LineStart, cell, cells);
        else if ((m_Tool == Tool::Paint) && UsesStamp() && !erase && !objects)
        {
            std::vector<std::pair<glm::ivec2, const StampCell*>> stamp;
            StampCells(g, cell, stamp);
            for (const auto& [c, s] : stamp)
                cells.push_back(c);
        }
        else if ((m_Tool == Tool::Paint || m_Tool == Tool::Erase) && !objects && m_BrushType != BrushType::Line)
            BrushCells(g, cell, cells);
        else if (m_Tool != Tool::Move)
            cells.push_back(cell);
        DrawPreview(ctx, e, cells, previewColor);
        if (ctx.DrawList && ctx.WorldToScreen)
        {
            ImVec2 s;
            if (ctx.WorldToScreen(hit, s))
            {
                char label[96];
                const uint8_t t = m_BrushTransform;
                if (t != 0)
                    snprintf(label, sizeof(label), "%d, %d  %d%s", cell.x, cell.y, TileTurns(t) * 90, TileMirrored(t) ? "° mirrored" : "°");
                else
                    snprintf(label, sizeof(label), "%d, %d", cell.x, cell.y);
                ctx.DrawList->AddText(ImVec2(s.x + 14.0f, s.y + 10.0f), IM_COL32(0, 0, 0, 160), label);
                ctx.DrawList->AddText(ImVec2(s.x + 13.0f, s.y + 9.0f), IM_COL32(255, 255, 255, 230), label);
            }
        }

        // Ctrl+Click: damlalık
        if (ctx.Ctrl && ctx.MouseClicked && m_Tool != Tool::Picker && m_Tool != Tool::Select && m_Tool != Tool::Move)
        {
            auto it = map.Cells.find(GridKey(cell.x, cell.y));
            if (it != map.Cells.end() && it->second.Tile >= 0)
            {
                m_SelectedTile = it->second.Tile;
                m_BrushTransform = it->second.Transform;
                m_Stamp.clear();
            }
            return true;
        }

        switch (m_Tool)
        {
            case Tool::Paint:
            case Tool::Erase:
                if (m_BrushType == BrushType::Line)
                {
                    if (ctx.MouseClicked)
                    {
                        m_LineActive = true;
                        m_LineStart = cell;
                    }
                    if (m_LineActive && !ctx.MouseDown)
                    {
                        ApplyTool(e, m_LineStart, cell, ctx.Shift);
                        m_LineActive = false;
                    }
                    break;
                }
                if (objects)
                {
                    if (ctx.MouseClicked || (ctx.MouseDown && m_Stroking && newCell))
                    {
                        m_Stroking = true;
                        erase ? EraseObject(e, cell) : PaintObject(e, cell);
                    }
                    break;
                }
                if (ctx.MouseClicked)
                {
                    PushUndo(e);
                    m_Stroking = true;
                }
                if (m_Stroking && ctx.MouseDown)
                {
                    if (UsesStamp() && !erase)
                    {
                        if (ctx.MouseClicked || newCell)
                            PaintStamp(e, cell);
                    }
                    else
                    {
                        BrushCells(g, cell, cells);
                        PaintCells(e, cells, erase);
                    }
                }
                break;
            case Tool::Box:
            case Tool::Picker:
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
                if (ctx.MouseClicked)
                    ApplyTool(e, cell, cell, ctx.Shift);
                break;
            case Tool::Select:
                if (ctx.MouseClicked)
                {
                    m_SelDragging = true;
                    m_SelStart = cell;
                }
                if (m_SelDragging && !ctx.MouseDown)
                {
                    SetSelection(m_SelStart, cell);
                    m_SelDragging = false;
                }
                break;
            case Tool::Move:
            {
                const bool inside = m_HasSelection && cell.x >= m_SelMin.x && cell.y >= m_SelMin.y && cell.x <= m_SelMax.x && cell.y <= m_SelMax.y;
                if (ctx.MouseClicked && inside)
                {
                    m_Moving = true;
                    m_MoveStart = cell;
                }
                if (m_Moving)
                {
                    m_MoveOffset = cell - m_MoveStart;
                    if (!ctx.MouseDown)
                    {
                        MoveSelection(e, m_MoveOffset);
                        m_Moving = false;
                        m_MoveOffset = glm::ivec2(0);
                    }
                }
                break;
            }
            default: break;
        }
        if (!ctx.MouseDown)
            m_Stroking = false;
        return true;
    }

    bool TilePalettePanel::DrawSceneOverlay(const ViewportToolContext& ctx, Entity e, TilemapComponent& map)
    {
        if (ctx.ViewportMax.x - ctx.ViewportMin.x < 260.0f || ctx.ViewportMax.y - ctx.ViewportMin.y < 200.0f)
            return false;
        LoadBrushPicks();
        const ImVec2 saved = ImGui::GetCursorScreenPos();
        const float width = std::min(340.0f, ctx.ViewportMax.x - ctx.ViewportMin.x - 20.0f);
        const float sw = 26.0f, gap = 4.0f;
        const int perRow = std::max(1, (int)((width - 16.0f + gap) / (sw + gap)));
        const int tileRows = std::min(3, ((int)map.Palette.size() + perRow - 1) / std::max(1, perRow));
        // Araç satırı + palet satırları + Brush Picks satırı + iç boşluk
        const float height = 26.0f + (float)tileRows * (sw + gap) + 24.0f + 14.0f;
        ImGui::SetCursorScreenPos(ImVec2(ctx.ViewportMin.x + 10.0f, ctx.ViewportMax.y - height - 10.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.10f, 0.12f, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        bool hovered = false;
        if (ImGui::BeginChild("##TilePaletteOverlay", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            // Araçlar
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            for (Tool t : kToolOrder)
            {
                const bool active = m_Tool == t;
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushID((int)t + 50);
                if (ImGui::Button(ToolIcon(t), ImVec2(26.0f, 22.0f)))
                    m_Tool = active ? Tool::None : t;
                ImGui::PopID();
                if (active)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", ToolName(t));
                ImGui::SameLine(0.0f, 3.0f);
            }
            ImGui::TextDisabled(" %s", kBrushNames[(int)m_BrushType]);
            ImGui::PopStyleVar();
            // Palet panosu
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const GridComponent& g = GridSystem::GridOf(e);
            for (int i = 0; i < (int)map.Palette.size() && i < perRow * 3; ++i)
            {
                if (i % perRow != 0)
                    ImGui::SameLine(0.0f, gap);
                ImGui::PushID(2000 + i);
                const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + sw, a.y + sw);
                if (ImGui::InvisibleButton("##ovtile", ImVec2(sw, sw)))
                {
                    m_SelectedTile = i;
                    m_Stamp.clear();
                    if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move)
                        m_Tool = Tool::Paint;
                }
                DrawSwatch(dl, a, b, map.Palette[(size_t)i], IsHex(g));
                if (i == m_SelectedTile && m_Stamp.empty())
                    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), ImGui::GetColorU32(UI::Colors::Accent), 3.0f, 0, 2.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", map.Palette[(size_t)i].Name.c_str());
                ImGui::PopID();
            }
            // Brush Picks
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
            if (ImGui::Button(ICON_FA_PLUS "##savepick"))
                SaveBrushPick("Pick " + std::to_string(m_Picks.size() + 1));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Save the current brush as a Brush Pick");
            for (int i = 0; i < (int)m_Picks.size(); ++i)
            {
                ImGui::SameLine(0.0f, 3.0f);
                ImGui::PushID(3000 + i);
                if (ImGui::Button(m_Picks[(size_t)i].Name.c_str()))
                    ApplyBrushPick(i);
                ImGui::PopID();
            }
            ImGui::PopStyleVar();
            hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::IsAnyItemActive();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(saved);
        return hovered;
    }

    // ------------------------------------------------------------------ Brush Picks

    void TilePalettePanel::LoadBrushPicks()
    {
        if (m_PicksLoaded)
            return;
        m_PicksLoaded = true;
        m_Picks.clear();
        const auto path = BrushPicksPath();
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec))
            return;
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            for (const auto& p : root["BrushPicks"])
            {
                BrushPick pick;
                pick.Name = p["Name"].as<std::string>("Pick");
                pick.Type = (BrushType)std::clamp(p["Type"].as<int>(0), 0, (int)BrushType::Count - 1);
                pick.Transform = (uint8_t)(p["Transform"].as<int>(0) & 7);
                pick.Prefab = p["Prefab"].as<std::string>("");
                for (const auto& s : p["Stamp"])
                {
                    StampCell c;
                    c.Offset = { s["X"].as<int>(0), s["Y"].as<int>(0) };
                    if (auto col = s["Color"]; col && col.size() == 4)
                        c.Cell.Color = { col[0].as<float>(), col[1].as<float>(), col[2].as<float>(), col[3].as<float>() };
                    c.Cell.Transform = (uint8_t)(s["Transform"].as<int>(0) & 7);
                    c.Cell.Elevation = s["Elevation"].as<float>(0.0f);
                    if (s["Tile"])
                        GridSystem::ReadTileDefinition(s["Tile"], c.Def);
                    pick.Stamp.push_back(c);
                }
                m_Picks.push_back(std::move(pick));
            }
        }
        catch (...) {}
    }

    void TilePalettePanel::SaveBrushPicks() const
    {
        const auto path = BrushPicksPath();
        if (path.empty())
            return;
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "BrushPicks" << YAML::Value << YAML::BeginSeq;
        for (const auto& p : m_Picks)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << p.Name;
            out << YAML::Key << "Type" << YAML::Value << (int)p.Type;
            out << YAML::Key << "Transform" << YAML::Value << (int)p.Transform;
            if (!p.Prefab.empty())
                out << YAML::Key << "Prefab" << YAML::Value << p.Prefab;
            out << YAML::Key << "Stamp" << YAML::Value << YAML::BeginSeq;
            for (const auto& s : p.Stamp)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "X" << YAML::Value << s.Offset.x << YAML::Key << "Y" << YAML::Value << s.Offset.y;
                out << YAML::Key << "Color" << YAML::Value << YAML::Flow << YAML::BeginSeq << s.Cell.Color.r << s.Cell.Color.g << s.Cell.Color.b << s.Cell.Color.a << YAML::EndSeq;
                out << YAML::Key << "Transform" << YAML::Value << (int)s.Cell.Transform;
                out << YAML::Key << "Elevation" << YAML::Value << s.Cell.Elevation;
                out << YAML::Key << "Tile" << YAML::Value;
                GridSystem::EmitTileDefinition(out, s.Def);
                out << YAML::EndMap;
            }
            out << YAML::EndSeq << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream(path, std::ios::trunc) << out.c_str() << "\n";
    }

    void TilePalettePanel::SaveBrushPick(const std::string& name)
    {
        LoadBrushPicks();
        BrushPick pick;
        pick.Name = name;
        pick.Type = m_BrushType;
        pick.Transform = m_BrushTransform;
        pick.Prefab = m_GameObjectPrefab;
        if (!m_Stamp.empty())
            pick.Stamp = m_Stamp;
        else if (Entity e = GetTarget())
        {
            auto& map = e.GetComponent<TilemapComponent>();
            if (m_SelectedTile >= 0 && m_SelectedTile < (int)map.Palette.size())
            {
                StampCell c;
                c.Cell.Tile = m_SelectedTile;
                c.Cell.Color = m_PaintColor;
                c.Def = map.Palette[(size_t)m_SelectedTile];
                pick.Stamp.push_back(c);
            }
        }
        m_Picks.push_back(std::move(pick));
        SaveBrushPicks();
    }

    bool TilePalettePanel::ApplyBrushPick(int index)
    {
        LoadBrushPicks();
        if (index < 0 || index >= (int)m_Picks.size())
            return false;
        const BrushPick& pick = m_Picks[(size_t)index];
        m_BrushType = pick.Type;
        m_BrushTransform = pick.Transform;
        m_GameObjectPrefab = pick.Prefab;
        m_Stamp.clear();
        if (pick.Stamp.size() == 1)
        {
            if (Entity e = GetTarget())
            {
                auto& map = e.GetComponent<TilemapComponent>();
                PushUndo(e);
                m_SelectedTile = EnsureTile(e, map, pick.Stamp[0].Def);
                m_PaintColor = pick.Stamp[0].Cell.Color;
            }
        }
        else
            m_Stamp = pick.Stamp;
        if (m_Tool == Tool::None || m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move)
            m_Tool = Tool::Paint;
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
        GridSystem::GridOf(e, &gridEntity);
        const bool active = IsTargetActive(e);
        // Focus Mode (Unity): odak dışındaki karo haritaları soluk
        if (m_Focus != FocusMode::None)
            GridSystem::SetEditorFocus((uint32_t)e, gridEntity ? (uint32_t)gridEntity : 0u, (int)m_Focus);

        DrawActiveTilemap(e);
        if (!gridEntity)
            UI::InfoBox("This Tilemap has no Grid parent; a default 1x1 rectangle grid is used.", UI::InfoType::Warning);

        DrawToolbar();
        if (!active && m_Tool != Tool::None)
            UI::InfoBox("Choose the Active Tilemap above (or select the Tilemap / its Grid) to paint in the Scene view.", UI::InfoType::Warning);

        ImGui::BeginChild("##tilepalettebody");
        DrawPaletteAsset(e, map);
        DrawPalette(e, map);
        DrawTileEditor(e, map);
        DrawBrush(e, map);
        DrawGridSelection(e, map);
        DrawBrushPicks();
        DrawMapSettings(e, map);
        ImGui::EndChild();

        HandleShortcuts(e, false);
        ImGui::End();
    }

    void TilePalettePanel::DrawActiveTilemap(Entity e)
    {
        // Unity Active Tilemap açılır listesi + Focus On
        std::vector<Entity> maps;
        m_Context->m_Registry.view<TilemapComponent>().each([&](auto h, auto&) { maps.push_back(Entity{ h, m_Context }); });
        auto labelOf = [](Entity m) {
            std::string s = m.GetComponent<TagComponent>().Tag;
            if (Entity p = m.GetParent(); p && p.HasComponent<TagComponent>())
                s = p.GetComponent<TagComponent>().Tag + " / " + s;
            return s;
        };
        UI::BeginProperty("Active Tilemap", "The Tilemap the brushes paint on (Unity Active Tilemap); painting no longer needs it to be selected");
        ImGui::SetNextItemWidth(-1.0f);
        const std::string current = labelOf(e) + (m_ActiveTilemap == 0 ? "  (from selection)" : "");
        if (ImGui::BeginCombo("##activetilemap", current.c_str()))
        {
            if (ImGui::Selectable("Follow Selection", m_ActiveTilemap == 0))
                SetActiveTilemap({});
            for (Entity m : maps)
            {
                ImGui::PushID((int)(uint32_t)m);
                if (ImGui::Selectable(labelOf(m).c_str(), m_ActiveTilemap != 0 && UUIDOf(m) == m_ActiveTilemap))
                    SetActiveTilemap(m);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        UI::EndProperty();
        static const char* kFocus[] = { "None", "Tilemap", "Grid" };
        int focus = (int)m_Focus;
        if (UI::Combo("Focus On", focus, kFocus, 3, "Fades the other Tilemaps while painting (Tilemap: only the active one stays, Grid: the Tilemaps of its Grid stay)"))
            m_Focus = (FocusMode)focus;
        const GridComponent& g = GridSystem::GridOf(e);
        ImGui::TextDisabled("%s  |  %d tiles", kLayoutNames[std::clamp((int)g.Layout, 0, 3)], (int)e.GetComponent<TilemapComponent>().Cells.size());
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
        const int toolCount = (int)(sizeof(kToolOrder) / sizeof(kToolOrder[0]));
        const float bw = std::max(26.0f, (ImGui::GetContentRegionAvail().x - spacing * (float)(toolCount + 1) - 30.0f * 2.0f - 8.0f) / (float)toolCount);
        for (int i = 0; i < toolCount; ++i)
        {
            const Tool t = kToolOrder[i];
            if (i > 0) ImGui::SameLine(0.0f, spacing);
            bool active = m_Tool == t;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            }
            ImGui::PushID((int)t);
            if (ImGui::Button(ToolIcon(t), ImVec2(bw, 30.0f)))
                m_Tool = active ? Tool::None : t;
            ImGui::PopID();
            if (active)
                ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", ToolName(t));
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

        // Fırça türü (Unity: paletin altındaki brush açılır listesi) ve dönüşüm
        int brush = (int)m_BrushType;
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 4.0f * 30.0f - 20.0f));
        if (ImGui::Combo("##brushtype", &brush, kBrushNames, (int)BrushType::Count))
            m_BrushType = (BrushType)brush;
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##rotccw", ImVec2(30, 0))) RotateBrush(1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Rotate counter-clockwise ([)");
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT "##rotcw", ImVec2(30, 0))) RotateBrush(-1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Rotate clockwise (])");
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(ICON_FA_LEFT_RIGHT "##flipx", ImVec2(30, 0))) FlipBrush(false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Flip X (Shift+[)");
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(ICON_FA_UP_DOWN "##flipy", ImVec2(30, 0))) FlipBrush(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Flip Y (Shift+])");

        if (m_Tool != Tool::None)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
            ImGui::TextWrapped("%s", ToolHelp(m_Tool));
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
        m_Stamp.clear();
        if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move)
            m_Tool = Tool::Paint;
        return true;
    }

    bool TilePalettePanel::PickPaletteAssetBox(const glm::ivec2& a, const glm::ivec2& b)
    {
        Entity e = GetTarget();
        GridSystem::TilePaletteAsset palette;
        if (!e || m_PaletteAsset.empty() || !GridSystem::LoadPalette(m_PaletteAsset, palette))
            return false;
        auto& map = e.GetComponent<TilemapComponent>();
        const glm::ivec2 mn = glm::min(a, b), mx = glm::max(a, b);
        std::vector<StampCell> stamp;
        for (size_t i = 0; i < palette.Tiles.size(); ++i)
        {
            const glm::ivec2 p = palette.Positions[i];
            if (p.x < mn.x || p.y < mn.y || p.x > mx.x || p.y > mx.y)
                continue;
            const int tile = EnsureTileInMap(e, map, palette.Tiles[i]);
            if (tile < 0)
                continue;
            StampCell c;
            c.Offset = p - mn;
            c.Cell.Tile = tile;
            c.Cell.Color = m_PaintColor;
            c.Def = map.Palette[(size_t)tile];
            stamp.push_back(c);
        }
        if (stamp.empty())
            return false;
        m_BrushTransform = 0;
        if (stamp.size() == 1)
        {
            m_SelectedTile = stamp[0].Cell.Tile;
            m_Stamp.clear();
        }
        else
            m_Stamp = std::move(stamp);
        if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move)
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
        ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 70.0f));
        if (ImGui::BeginCombo("##paletteasset", current.c_str()))
        {
            if (ImGui::Selectable("None (use the Tilemap's own tiles)", m_PaletteAsset.empty()))
                m_PaletteAsset.clear();
            for (const auto& p : palettes)
                if (ImGui::Selectable(p.c_str(), p == m_PaletteAsset))
                    m_PaletteAsset = p;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_PaletteAsset.empty());
        if (m_PaletteEdit)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
        if (ImGui::Button(m_PaletteEdit ? ICON_FA_PEN " Done" : ICON_FA_PEN " Edit", ImVec2(-1.0f, 0.0f)))
            m_PaletteEdit = !m_PaletteEdit;
        if (m_PaletteEdit)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Edit the palette: drag tiles to rearrange them, drop tiles or sprites onto a cell, right-click to remove");
        if (palettes.empty())
            ImGui::TextDisabled("Create > 2D > Tile Palette in the Content Browser to make one.");
        GridSystem::TilePaletteAsset palette;
        if (!m_PaletteAsset.empty() && GridSystem::LoadPalette(m_PaletteAsset, palette))
        {
            // Unity paleti gibi ızgara: karolar kendi konumlarında (y yukarı). Tıkla = seç, sürükle = kutuyla çok hücreli fırça;
            // Edit: karoyu sürükleyip taşı, boş hücreye karo / görsel bırak, sağ tık sil
            const GridComponent& g = GridSystem::GridOf(e);
            const float cell = 46.0f;
            glm::ivec2 mn(0), mx(0);
            for (const auto& p : palette.Positions) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
            if (m_PaletteEdit) { mn -= glm::ivec2(1); mx += glm::ivec2(1); }
            const int cols = mx.x - mn.x + 1, rows = mx.y - mn.y + 1;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto cellRect = [&](const glm::ivec2& p, ImVec2& a, ImVec2& b) {
                a = ImVec2(origin.x + (float)(p.x - mn.x) * cell, origin.y + (float)(mx.y - p.y) * cell);
                b = ImVec2(a.x + cell - 3.0f, a.y + cell - 3.0f);
            };
            ImGui::InvisibleButton("##palettegrid", ImVec2((float)cols * cell, (float)rows * cell));
            const bool gridHovered = ImGui::IsItemHovered();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const glm::ivec2 hoverCell(mn.x + (int)std::floor((mouse.x - origin.x) / cell), mx.y - (int)std::floor((mouse.y - origin.y) / cell));
            auto tileAt = [&](const glm::ivec2& p) {
                for (size_t i = 0; i < palette.Positions.size(); ++i)
                    if (palette.Positions[i] == p) return (int)i;
                return -1;
            };
            bool changed = false;
            if (gridHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const int hit = tileAt(hoverCell);
                if (m_PaletteEdit)
                    m_PaletteDrag = hit;
                else
                {
                    m_PaletteBoxActive = true;
                    m_PaletteBoxStart = m_PaletteBoxEnd = hoverCell;
                }
            }
            if (m_PaletteBoxActive)
            {
                m_PaletteBoxEnd = hoverCell;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    m_PaletteBoxActive = false;
                    if (m_PaletteBoxStart == m_PaletteBoxEnd)
                    {
                        if (int hit = tileAt(m_PaletteBoxStart); hit >= 0)
                            SelectPaletteAssetTile(hit);
                    }
                    else
                        PickPaletteAssetBox(m_PaletteBoxStart, m_PaletteBoxEnd);
                }
            }
            if (m_PaletteEdit && m_PaletteDrag >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // Bırakılan hücre doluysa yer değiştirir
                if (hoverCell != palette.Positions[(size_t)m_PaletteDrag])
                {
                    if (int other = tileAt(hoverCell); other >= 0)
                        palette.Positions[(size_t)other] = palette.Positions[(size_t)m_PaletteDrag];
                    palette.Positions[(size_t)m_PaletteDrag] = hoverCell;
                    changed = true;
                }
                m_PaletteDrag = -1;
            }
            if (m_PaletteEdit && gridHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                if (int hit = tileAt(hoverCell); hit >= 0)
                {
                    palette.Tiles.erase(palette.Tiles.begin() + hit);
                    palette.Positions.erase(palette.Positions.begin() + hit);
                    changed = true;
                }
            // Çizim
            for (int y = mn.y; y <= mx.y; ++y)
                for (int x = mn.x; x <= mx.x; ++x)
                {
                    ImVec2 a, b;
                    cellRect({ x, y }, a, b);
                    dl->AddRectFilled(a, b, m_PaletteEdit ? IM_COL32(34, 36, 42, 255) : IM_COL32(28, 30, 34, 255), 4.0f);
                }
            const glm::ivec2 boxMin = glm::min(m_PaletteBoxStart, m_PaletteBoxEnd), boxMax = glm::max(m_PaletteBoxStart, m_PaletteBoxEnd);
            for (size_t i = 0; i < palette.Tiles.size(); ++i)
            {
                glm::ivec2 p = palette.Positions[i];
                if ((int)i == m_PaletteDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    p = hoverCell;
                ImVec2 a, b;
                cellRect(p, a, b);
                TileDefinition def;
                if (GridSystem::LoadTileAsset(palette.Tiles[i], def))
                    DrawSwatch(dl, ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), def, IsHex(g));
                else
                    dl->AddText(ImVec2(a.x + 4, a.y + 14), IM_COL32(230, 120, 110, 255), "missing");
                const bool isSel = m_Stamp.empty() && m_SelectedTile >= 0 && m_SelectedTile < (int)map.Palette.size() &&
                                   map.Palette[(size_t)m_SelectedTile].Asset == palette.Tiles[i];
                if (isSel)
                    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), ImGui::GetColorU32(UI::Colors::Accent), 4.0f, 0, 2.5f);
                if (gridHovered && hoverCell == palette.Positions[i])
                    ImGui::SetTooltip("%s\n%s", def.Name.c_str(), palette.Tiles[i].c_str());
            }
            if (m_PaletteBoxActive)
            {
                ImVec2 a0, b0, a1, b1;
                cellRect({ boxMin.x, boxMax.y }, a0, b0);
                cellRect({ boxMax.x, boxMin.y }, a1, b1);
                dl->AddRect(a0, b1, ImGui::GetColorU32(UI::Colors::Accent), 4.0f, 0, 2.0f);
            }
            // Karo / görsel bırak: hücreye (Edit) ya da ilk boş konuma eklenir (görselden yeni Tile varlığı, Unity gibi)
            auto acceptDrop = [&](bool atHover) {
                if (!ImGui::BeginDragDropTarget())
                    return;
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
                        fs::path out = root / fs::path(m_PaletteAsset).parent_path() / (dropped.stem().string() + GridSystem::TileExtension);
                        TileDefinition def;
                        def.Name = dropped.stem().string();
                        def.Texture = fs::relative(dropped, root, ec).generic_string();
                        if (fs::exists(out, ec) || GridSystem::SaveTileAsset(out.string(), def))
                            tilePath = fs::relative(out, root, ec).generic_string();
                    }
                    if (!tilePath.empty())
                    {
                        const glm::ivec2 at = atHover ? hoverCell : palette.FreePosition();
                        if (int occupied = tileAt(at); occupied >= 0)
                            palette.Tiles[(size_t)occupied] = tilePath;
                        else
                        {
                            palette.Tiles.push_back(tilePath);
                            palette.Positions.push_back(at);
                        }
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            };
            if (m_PaletteEdit)
            {
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##palettedrop", ImVec2((float)cols * cell, (float)rows * cell));
                acceptDrop(true);
            }
            ImGui::Button(ICON_FA_PLUS "  Drop Tiles or Sprites here", ImVec2(-1.0f, 26.0f));
            acceptDrop(false);
            if (changed)
                GridSystem::SavePalette(m_PaletteAsset, palette);
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
                m_Stamp.clear();
                if (m_Tool == Tool::None || m_Tool == Tool::Erase || m_Tool == Tool::Picker || m_Tool == Tool::Select || m_Tool == Tool::Move)
                    m_Tool = Tool::Paint;
            }
            const bool hovered = ImGui::IsItemHovered();
            // Sağ tık: kopyala / sil / Random fırçaya ekle
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
                if (ImGui::MenuItem(ICON_FA_DICE "  Add to Random Brush"))
                {
                    m_RandomTiles.push_back({ i, 1.0f });
                    m_BrushType = BrushType::Random;
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
                const bool isSel = i == m_SelectedTile && m_Stamp.empty();
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
                {
                    m_SelectedTile = k;
                    m_Stamp.clear();
                }
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
        // Unity Tile: Collider Type (None / Sprite / Grid) ve Flags
        static const char* kCollider[] = { "None", "Sprite", "Grid" };
        int collider = !def.Solid ? 0 : (def.SpriteCollider ? 1 : 2);
        if (UI::Combo("Collider Type", collider, kCollider, 3,
                      "Tilemap Collider 2D shape: None, Sprite (the opaque area of the texture, turns with the cell) or Grid (the whole cell)"))
        {
            def.Solid = collider != 0;
            def.SpriteCollider = collider == 1;
            changed = true;
        }
        changed |= UI::Checkbox("Lock Color", def.LockColor, "Painting keeps the tile's own color (the brush tint is not applied)");
        changed |= UI::Checkbox("Lock Transform", def.LockTransform, "Painting ignores the brush rotation and flip");
        UI::BeginProperty("GameObject", "Prefab instantiated on every cell of this tile (child of the Tilemap, at the cell center)");
        ImGui::TextUnformatted(def.GameObject.empty() ? "None" : std::filesystem::path(def.GameObject).stem().string().c_str());
        ImGui::SameLine();
        if (std::string picked; ObjectPicker::Button("tilegameobject", "Prefab", { ".cqprefab" }, def.GameObject, picked))
        {
            def.GameObject = picked;
            changed = true;
        }
        UI::EndProperty();
        if (!def.GameObject.empty())
            changed |= UI::Checkbox("Instantiate Runtime Only", def.GameObjectRuntimeOnly,
                                    "Off: the instances are also shown in the editor (they are not saved; the Tilemap recreates them)");
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
        UI::Text("Type", "%s", kBrushNames[(int)m_BrushType]);
        if (!m_Stamp.empty())
        {
            glm::ivec2 size(1);
            for (const auto& s : m_Stamp) size = glm::max(size, s.Offset + glm::ivec2(1));
            UI::Text("Multi-Cell Brush", "%d x %d (%d tiles)", size.x, size.y, (int)m_Stamp.size());
            if (ImGui::SmallButton(ICON_FA_XMARK " Clear Multi-Cell Brush"))
                m_Stamp.clear();
        }
        UI::Text("Transform", "%d°%s", TileTurns(m_BrushTransform) * 90, TileMirrored(m_BrushTransform) ? " mirrored" : "");
        switch (m_BrushType)
        {
            case BrushType::Default:
            {
                int size = m_BrushSize;
                if (UI::SliderInt("Size", size, 1, 16, "Brush radius in cells (- and =)"))
                    SetBrushSize(size);
                break;
            }
            case BrushType::Line:
                UI::Checkbox("Fill Gaps", m_LineFillGaps, "Diagonal steps become a horizontal + vertical step (an unbroken edge)");
                ImGui::TextDisabled("Click the start cell and release on the end cell.");
                break;
            case BrushType::Random:
            {
                ImGui::TextDisabled("Tiles painted at random (right-click a tile > Add to Random Brush).");
                for (size_t i = 0; i < m_RandomTiles.size(); ++i)
                {
                    auto& [t, w] = m_RandomTiles[i];
                    ImGui::PushID((int)i);
                    const char* name = t >= 0 && t < (int)map.Palette.size() ? map.Palette[(size_t)t].Name.c_str() : "(missing)";
                    UI::DragFloat(name, w, 0.01f, 0.0f, 100.0f, "%.2f", "Relative probability");
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK))
                    {
                        m_RandomTiles.erase(m_RandomTiles.begin() + (std::ptrdiff_t)i);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(ICON_FA_PLUS " Add Selected Tile") && m_SelectedTile >= 0)
                    m_RandomTiles.push_back({ m_SelectedTile, 1.0f });
                break;
            }
            case BrushType::GameObject:
            {
                UI::BeginProperty("Prefab", "Painted cells get an instance of this prefab (child of the Tilemap, at the cell center). Shift / Eraser removes it");
                std::string picked;
                ImGui::TextUnformatted(m_GameObjectPrefab.empty() ? "None" : std::filesystem::path(m_GameObjectPrefab).stem().string().c_str());
                ImGui::SameLine();
                if (ObjectPicker::Button("tilegopick", "Prefab", { ".cqprefab" }, m_GameObjectPrefab, picked))
                    m_GameObjectPrefab = picked;
                UI::EndProperty();
                break;
            }
            case BrushType::Group:
            {
                int gap = m_GroupGap, limit = m_GroupLimit;
                if (UI::DragInt("Gap", gap, 0.1f, 0, 8, "Empty cells that may separate tiles of the same group"))
                    m_GroupGap = std::max(0, gap);
                if (UI::DragInt("Limit", limit, 1.0f, 1, 4096, "Largest group size"))
                    m_GroupLimit = std::max(1, limit);
                ImGui::TextDisabled("With the Picker, click a tile to pick its whole group.");
                break;
            }
            case BrushType::Prefab:
            {
                ImGui::TextDisabled("Each painted cell gets one of these prefabs at random.");
                for (size_t i = 0; i < m_Prefabs.size(); ++i)
                {
                    ImGui::PushID((int)i);
                    ImGui::BulletText("%s", std::filesystem::path(m_Prefabs[i]).stem().string().c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK))
                    {
                        m_Prefabs.erase(m_Prefabs.begin() + (std::ptrdiff_t)i);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                std::string picked;
                if (ObjectPicker::Button("tileprefabadd", "Prefab", { ".cqprefab" }, "", picked) && !picked.empty())
                    m_Prefabs.push_back(picked);
                ImGui::SameLine();
                ImGui::TextDisabled("Add prefab");
                UI::Checkbox("Random Rotation", m_PrefabRandomRotation, "Turns each instance by a random multiple of 90°");
                break;
            }
            default: break;
        }
        UI::Color4("Cell Tint", m_PaintColor, "Per-cell color multiplier applied while painting");
        if (map.Render3D)
            UI::DragFloat("Elevation", m_Elevation, 0.02f, -50.0f, 50.0f, "%.2f", "Extra height for painted 3D tiles");
        UI::Checkbox("Scene View Overlay", m_ShowOverlay, "Shows the tools, the palette and the Brush Picks over the Scene view (Unity 6 Tile Palette overlays)");
        if (m_HasHover)
        {
            int t = GridSystem::GetTile(map, m_HoverCell);
            UI::Text("Hover Cell", "%d, %d  %s", m_HoverCell.x, m_HoverCell.y,
                     t >= 0 && t < (int)map.Palette.size() ? map.Palette[(size_t)t].Name.c_str() : "(empty)");
        }
        (void)e;
        UI::EndSection();
    }

    void TilePalettePanel::DrawGridSelection(Entity e, TilemapComponent& map)
    {
        if (!m_HasSelection)
            return;
        if (!UI::BeginSection("Grid Selection", true, ICON_FA_OBJECT_GROUP))
            return;
        const glm::ivec2 size = m_SelMax - m_SelMin + glm::ivec2(1);
        int painted = 0;
        for (int y = m_SelMin.y; y <= m_SelMax.y; ++y)
            for (int x = m_SelMin.x; x <= m_SelMax.x; ++x)
                painted += map.Cells.count(GridKey(x, y)) ? 1 : 0;
        UI::Text("Position", "%d, %d", m_SelMin.x, m_SelMin.y);
        UI::Text("Size", "%d x %d  (%d painted)", size.x, size.y, painted);
        // Seçimdeki hücrelere uygulanır (tek hücrede değerleri gösterir)
        TileCell* single = nullptr;
        if (size == glm::ivec2(1))
            if (auto it = map.Cells.find(GridKey(m_SelMin.x, m_SelMin.y)); it != map.Cells.end())
                single = &it->second;
        auto forEach = [&](auto&& fn) {
            PushUndo(e);
            for (int y = m_SelMin.y; y <= m_SelMax.y; ++y)
                for (int x = m_SelMin.x; x <= m_SelMax.x; ++x)
                    if (auto it = map.Cells.find(GridKey(x, y)); it != map.Cells.end())
                        fn(it->second);
            map.Version++;
            Touch(e);
        };
        if (painted > 0)
        {
            std::vector<const char*> names;
            for (const auto& d : map.Palette) names.push_back(d.Name.c_str());
            int tile = single ? single->Tile : -1;
            UI::BeginProperty("Tile");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##seltile", &tile, names.data(), (int)names.size()) && tile >= 0)
                forEach([&](TileCell& c) { c.Tile = tile; });
            UI::EndProperty();
            glm::vec4 color = single ? single->Color : glm::vec4(1.0f);
            if (UI::Color4("Color", color))
                forEach([&](TileCell& c) { c.Color = color; });
            static const char* kTurns[] = { "0°", "90°", "180°", "270°" };
            int turns = single ? TileTurns(single->Transform) : 0;
            if (UI::Combo("Rotation", turns, kTurns, 4, "Counter-clockwise"))
                forEach([&](TileCell& c) { c.Transform = MakeTileTransform(turns, TileMirrored(c.Transform)); });
            bool mirrored = single ? TileMirrored(single->Transform) : false;
            if (UI::Checkbox("Mirror", mirrored))
                forEach([&](TileCell& c) { c.Transform = MakeTileTransform(TileTurns(c.Transform), mirrored); });
            if (map.Render3D)
            {
                float elevation = single ? single->Elevation : 0.0f;
                if (UI::DragFloat("Elevation", elevation, 0.02f, -50.0f, 50.0f, "%.2f"))
                    forEach([&](TileCell& c) { c.Elevation = elevation; });
            }
            if (ImGui::Button(ICON_FA_ROTATE_LEFT " Rotate")) forEach([&](TileCell& c) { c.Transform = RotateTileTransform(c.Transform, 1); });
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_LEFT_RIGHT " Flip X")) forEach([&](TileCell& c) { c.Transform = FlipTileTransformX(c.Transform); });
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UP_DOWN " Flip Y")) forEach([&](TileCell& c) { c.Transform = FlipTileTransformY(c.Transform); });
        }
        if (ImGui::Button(ICON_FA_COPY " Copy to Brush")) CopySelectionToBrush(e);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILL_DRIP " Fill"))
        {
            const Tool keep = m_Tool;
            m_Tool = Tool::Box;
            ApplyTool(e, m_SelMin, m_SelMax, false);
            m_Tool = keep;
        }
        ImGui::SameLine();
        if (UI::DangerButton(ICON_FA_TRASH " Delete")) DeleteSelection(e);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK)) m_HasSelection = false;
        UI::EndSection();
    }

    void TilePalettePanel::DrawBrushPicks()
    {
        LoadBrushPicks();
        if (!UI::BeginSection("Brush Picks", false, ICON_FA_BOOKMARK))
            return;
        static std::string newName = "Pick";
        UI::InputText("Name", newName);
        if (ImGui::Button(ICON_FA_PLUS " Save Current Brush", ImVec2(-1.0f, 0.0f)))
            SaveBrushPick(newName.empty() ? "Pick " + std::to_string(m_Picks.size() + 1) : newName);
        for (int i = 0; i < (int)m_Picks.size(); ++i)
        {
            ImGui::PushID(i);
            const auto& p = m_Picks[(size_t)i];
            if (ImGui::Selectable((p.Name + "  (" + kBrushNames[(int)p.Type] + ", " + std::to_string(p.Stamp.size()) + " tiles)").c_str()))
                ApplyBrushPick(i);
            if (ImGui::BeginPopupContextItem("##pickctx"))
            {
                if (ImGui::MenuItem(ICON_FA_TRASH "  Remove"))
                {
                    m_Picks.erase(m_Picks.begin() + i);
                    SaveBrushPicks();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
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
