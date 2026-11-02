#pragma once

#include "Scene/Entity.h"
#include "Grid/GridComponents.h"
#include "ViewportPanel.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Unity Tile Palette: karo paleti (renk, doku, katı, yürünebilir, maliyet, yükseklik) ve Scene görünümünde
    // fırça araçları — boya, kutu doldur, kova doldur, sil, damlalık; fırça boyutu, hücre rengi/yüksekliği, geri al.
    class TilePalettePanel
    {
    public:
        enum class Tool : int { None = -1, Paint = 0, Box, Fill, Erase, Picker, Count };

        void SetContext(Scene* scene)
        {
            m_Context = scene;
            m_UndoStack.clear();
            m_RedoStack.clear();
            m_Stroking = false;
            m_BoxActive = false;
        }
        void OnImGuiRender(bool* open);
        bool OnViewportTool(const ViewportToolContext& ctx);

        // Seçili karo haritası (ya da seçili ızgaranın ilk karo haritası / sahnedeki ilk karo haritası)
        Entity GetTarget() const;
        Tool GetTool() const { return m_Tool; }
        void SetTool(Tool tool) { m_Tool = tool; }
        int GetSelectedTile() const { return m_SelectedTile; }
        void SetSelectedTile(int tile) { m_SelectedTile = tile; }
        int GetBrushSize() const { return m_BrushSize; }
        void SetBrushSize(int size) { m_BrushSize = std::clamp(size, 1, 16); }
        glm::vec4& PaintColor() { return m_PaintColor; }
        float& Elevation() { return m_Elevation; }

        // Otomasyon/test: geçerli aracı uygula (kutu: a..b köşeleri, diğerleri: a); geri alınabilir
        void ApplyTool(Entity tilemap, const glm::ivec2& a, const glm::ivec2& b, bool invert);
        void Undo();
        void Redo();
        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }
        // Son karede fare altındaki hücre (viewport aracı etkinken)
        bool GetHoverCell(glm::ivec2& cell) const { cell = m_HoverCell; return m_HasHover; }

        static Entity CreateTilemap(Scene* scene, GridLayout layout, GridPlane plane, bool render3D, Entity parent = {});
        // Tilemap denetçisinin alt kısmı (ComponentRegistry ExtraInspector)
        static void DrawTilemapInspector(Entity e);
        static std::function<void()> s_OpenPanel;

    private:
        struct Snapshot
        {
            uint64_t EntityID = 0;
            std::unordered_map<uint64_t, TileCell> Cells;
            std::vector<TileDefinition> Palette;
        };

        bool IsTargetSelected(Entity e) const;
        void PushUndo(Entity e);
        void Restore(std::deque<Snapshot>& from, std::deque<Snapshot>& to);
        void Touch(Entity e);
        void BrushCells(const GridComponent& g, const glm::ivec2& center, std::vector<glm::ivec2>& out) const;
        void PaintCells(Entity e, const std::vector<glm::ivec2>& cells, bool erase);
        void RemovePaletteEntry(Entity e, int index);
        void DrawPreview(const ViewportToolContext& ctx, Entity e, const std::vector<glm::ivec2>& cells, unsigned int color);

        void DrawCreate();
        // Tile Palette varlığı (.cqpalette): karo varlıklarından (.cqtile) oluşan, projeler arası paylaşılan palet
        void DrawPaletteAsset(Entity e, TilemapComponent& map);
        int EnsureTileInMap(Entity e, TilemapComponent& map, const std::string& tileAsset);
        void DrawToolbar();
        void DrawPalette(Entity e, TilemapComponent& map);
        void DrawTileEditor(Entity e, TilemapComponent& map);
        void DrawBrush(Entity e, TilemapComponent& map);
        void DrawMapSettings(Entity e, TilemapComponent& map);

        Scene* m_Context = nullptr;
        Tool m_Tool = Tool::Paint;
        int m_SelectedTile = 0;
        std::string m_PaletteAsset;   // seçili .cqpalette (proje göreli)
    public:
        void SetPaletteAsset(const std::string& path) { m_PaletteAsset = path; }
        const std::string& GetPaletteAsset() const { return m_PaletteAsset; }
        // Otomasyon/test: palet varlığının i. karosunu seç (haritaya ekler)
        bool SelectPaletteAssetTile(int index);
    private:
        int m_BrushSize = 1;
        glm::vec4 m_PaintColor{ 1.0f };
        float m_Elevation = 0.0f;
        bool m_Stroking = false;
        bool m_BoxActive = false;
        glm::ivec2 m_BoxStart{ 0 };
        glm::ivec2 m_BoxEnd{ 0 };
        glm::ivec2 m_HoverCell{ 0 };
        bool m_HasHover = false;
        bool m_ConfirmClear = false;

        std::deque<Snapshot> m_UndoStack;
        std::deque<Snapshot> m_RedoStack;
    };
}
