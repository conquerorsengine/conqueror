#pragma once

#include "Scene/Entity.h"
#include "Grid/GridComponents.h"
#include "ViewportPanel.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Unity Tile Palette: karo paleti (renk, doku, katı, yürünebilir, maliyet, yükseklik) ve Scene görünümünde fırça araçları:
    // Select, Move, Paint, Box Fill, Picker, Eraser, Flood Fill; fırça türleri (Default, Line, Random, GameObject, Group, Prefab);
    // çok hücreli damga (palet ızgarasından ya da sahneden kutuyla), döndürme / aynalama ([ ] / Shift+[ ]), Active Tilemap,
    // Focus Mode, Grid Selection denetçisi, Scene görünümü katmanı (palet + Brush Picks), geri al
    class TilePalettePanel
    {
    public:
        // Değerler otomasyon ve kayıtlarla uyumlu kalsın diye yeni araçlar sona eklenir (araç çubuğu Unity sırasıyla çizer)
        enum class Tool : int { None = -1, Paint = 0, Box, Fill, Erase, Picker, Select, Move, Count };
        enum class BrushType : int { Default = 0, Line, Random, GameObject, Group, Prefab, Count };
        enum class FocusMode : int { None = 0, Tilemap, Grid };

        // Çok hücreli fırça (damga): hücre sol alt köşeye göre; karo tanımı kopya (başka haritaya da uygulanabilir)
        struct StampCell
        {
            glm::ivec2 Offset{ 0 };
            TileCell Cell;
            TileDefinition Def;
        };
        // Unity 6 Brush Picks: kaydedilen fırça (tür, karo / damga, dönüşüm)
        struct BrushPick
        {
            std::string Name;
            BrushType Type = BrushType::Default;
            std::vector<StampCell> Stamp;
            uint8_t Transform = 0;
            std::string Prefab;
        };

        void SetContext(Scene* scene)
        {
            m_Context = scene;
            m_UndoStack.clear();
            m_RedoStack.clear();
            m_Stroking = false;
            m_BoxActive = false;
            m_LineActive = false;
            m_HasSelection = false;
            m_Moving = false;
            m_ActiveTilemap = 0;
        }
        // DestroyEntityCommand (GameObject / Prefab fırçasıyla silme geri alınabilsin) sahnenin paylaşılan işaretçisini ister
        void SetSceneRef(const std::shared_ptr<Scene>& scene) { m_SceneRef = scene; }
        void OnImGuiRender(bool* open);
        bool OnViewportTool(const ViewportToolContext& ctx);

        // Active Tilemap (seçildiyse) ya da seçili karo haritası / seçili ızgaranın ilk karo haritası / sahnedeki ilk karo haritası
        Entity GetTarget() const;
        Tool GetTool() const { return m_Tool; }
        void SetTool(Tool tool) { m_Tool = tool; }
        BrushType GetBrushType() const { return m_BrushType; }
        void SetBrushType(BrushType type) { m_BrushType = type; }
        int GetSelectedTile() const { return m_SelectedTile; }
        void SetSelectedTile(int tile) { m_SelectedTile = tile; m_Stamp.clear(); }
        int GetBrushSize() const { return m_BrushSize; }
        void SetBrushSize(int size) { m_BrushSize = std::clamp(size, 1, 16); }
        glm::vec4& PaintColor() { return m_PaintColor; }
        float& Elevation() { return m_Elevation; }
        uint8_t GetBrushTransform() const { return m_BrushTransform; }
        void RotateBrush(int quarterTurnsCCW);
        void FlipBrush(bool vertical);
        void SetActiveTilemap(Entity e);
        void SetFocusMode(FocusMode mode) { m_Focus = mode; }
        void SetGameObjectPrefab(const std::string& path) { m_GameObjectPrefab = path; }
        void SetPrefabs(const std::vector<std::string>& paths) { m_Prefabs = paths; }
        void SetRandomTiles(const std::vector<std::pair<int, float>>& tiles) { m_RandomTiles = tiles; }
        void SetGroupSettings(int gap, int limit) { m_GroupGap = std::max(0, gap); m_GroupLimit = std::max(1, limit); }
        const std::vector<StampCell>& GetStamp() const { return m_Stamp; }

        // Otomasyon/test: geçerli aracı uygula (kutu / çizgi / seçim / damga alma: a..b köşeleri, diğerleri: a); geri alınabilir
        void ApplyTool(Entity tilemap, const glm::ivec2& a, const glm::ivec2& b, bool invert);
        // Grid Selection: seçimi ayarla / taşı / sil / fırçaya kopyala
        void SetSelection(const glm::ivec2& a, const glm::ivec2& b);
        void ClearSelection() { m_HasSelection = false; }
        bool HasSelection() const { return m_HasSelection; }
        void MoveSelection(Entity e, const glm::ivec2& offset);
        void DeleteSelection(Entity e);
        void CopySelectionToBrush(Entity e);
        void Undo();
        void Redo();
        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }
        // Son karede fare altındaki hücre (viewport aracı etkinken)
        bool GetHoverCell(glm::ivec2& cell) const { cell = m_HoverCell; return m_HasHover; }
        // Brush Picks
        void SaveBrushPick(const std::string& name);
        bool ApplyBrushPick(int index);
        int GetBrushPickCount() const { return (int)m_Picks.size(); }

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

        bool IsTargetActive(Entity e) const;
        // Çok hücreli fırça Default ve Group fırçasınındır (Unity GridBrush); Line / Random / GameObject / Prefab kendi karosunu koyar
        bool UsesStamp() const { return !m_Stamp.empty() && (m_BrushType == BrushType::Default || m_BrushType == BrushType::Group); }
        void PushUndo(Entity e);
        void Restore(std::deque<Snapshot>& from, std::deque<Snapshot>& to);
        void Touch(Entity e);
        void BrushCells(const GridComponent& g, const glm::ivec2& center, std::vector<glm::ivec2>& out) const;
        // Fırçanın bu hücreye koyacağı karo (Random fırça: ağırlıklı rastgele); -1: yok
        int BrushTileAt();
        TileCell MakeCell(const TilemapComponent& map, int tile, uint8_t extraTransform = 0) const;
        void PaintCells(Entity e, const std::vector<glm::ivec2>& cells, bool erase);
        // Damga: dönüşümle hücre konumları (dikdörtgen / izometrik ızgarada desen de döner)
        void StampCells(const GridComponent& g, const glm::ivec2& at, std::vector<std::pair<glm::ivec2, const StampCell*>>& out) const;
        void PaintStamp(Entity e, const glm::ivec2& at);
        int EnsureTile(Entity e, TilemapComponent& map, const TileDefinition& def);
        void LineCells(const GridComponent& g, const glm::ivec2& a, const glm::ivec2& b, std::vector<glm::ivec2>& out) const;
        void PickStamp(Entity e, const glm::ivec2& a, const glm::ivec2& b);
        void PickGroup(Entity e, const glm::ivec2& start);
        // GameObject / Prefab fırçası
        Entity ObjectAtCell(Entity e, const glm::ivec2& cell) const;
        void PaintObject(Entity e, const glm::ivec2& cell);
        void EraseObject(Entity e, const glm::ivec2& cell);
        void RemovePaletteEntry(Entity e, int index);
        void DrawPreview(const ViewportToolContext& ctx, Entity e, const std::vector<glm::ivec2>& cells, unsigned int color);
        bool DrawSceneOverlay(const ViewportToolContext& ctx, Entity e, TilemapComponent& map);

        void DrawCreate();
        void DrawActiveTilemap(Entity e);
        // Tile Palette varlığı (.cqpalette): karo varlıklarından (.cqtile) oluşan, projeler arası paylaşılan palet; Unity gibi
        // düzenlenebilir ızgara (Edit: taşı, bırak, sil; kutuyla çok hücreli seçim = damga)
        void DrawPaletteAsset(Entity e, TilemapComponent& map);
        int EnsureTileInMap(Entity e, TilemapComponent& map, const std::string& tileAsset);
        void DrawToolbar();
        void DrawPalette(Entity e, TilemapComponent& map);
        void DrawTileEditor(Entity e, TilemapComponent& map);
        void DrawBrush(Entity e, TilemapComponent& map);
        void DrawGridSelection(Entity e, TilemapComponent& map);
        void DrawBrushPicks();
        void DrawMapSettings(Entity e, TilemapComponent& map);
        void HandleShortcuts(Entity e, bool viewportHovered);
        void LoadBrushPicks();
        void SaveBrushPicks() const;

        Scene* m_Context = nullptr;
        std::weak_ptr<Scene> m_SceneRef;
        Tool m_Tool = Tool::Paint;
        BrushType m_BrushType = BrushType::Default;
        int m_SelectedTile = 0;
        std::string m_PaletteAsset;   // seçili .cqpalette (proje göreli)
    public:
        void SetPaletteAsset(const std::string& path) { m_PaletteAsset = path; }
        const std::string& GetPaletteAsset() const { return m_PaletteAsset; }
        // Otomasyon/test: palet varlığının i. karosunu seç (haritaya ekler)
        bool SelectPaletteAssetTile(int index);
        // Otomasyon/test: palet varlığının ızgarasında (x0,y0)-(x1,y1) kutusunu damga yap
        bool PickPaletteAssetBox(const glm::ivec2& a, const glm::ivec2& b);
    private:
        int m_BrushSize = 1;
        glm::vec4 m_PaintColor{ 1.0f };
        float m_Elevation = 0.0f;
        uint8_t m_BrushTransform = 0;
        std::vector<StampCell> m_Stamp;
        bool m_Stroking = false;
        bool m_BoxActive = false;
        glm::ivec2 m_BoxStart{ 0 };
        glm::ivec2 m_BoxEnd{ 0 };
        bool m_LineActive = false;
        glm::ivec2 m_LineStart{ 0 };
        bool m_LineFillGaps = true;
        std::vector<std::pair<int, float>> m_RandomTiles;   // (karo, ağırlık)
        std::mt19937 m_Random{ 1234567u };
        std::string m_GameObjectPrefab;
        std::vector<std::string> m_Prefabs;
        bool m_PrefabRandomRotation = false;
        int m_GroupGap = 0, m_GroupLimit = 256;
        bool m_HasSelection = false;
        glm::ivec2 m_SelMin{ 0 }, m_SelMax{ 0 };
        bool m_SelDragging = false;
        glm::ivec2 m_SelStart{ 0 };
        bool m_Moving = false;
        glm::ivec2 m_MoveStart{ 0 }, m_MoveOffset{ 0 };
        uint64_t m_ActiveTilemap = 0;
        FocusMode m_Focus = FocusMode::None;
        bool m_ShowOverlay = true;
        bool m_PaletteEdit = false;
        int m_PaletteDrag = -1;                  // Edit kipinde sürüklenen palet karosu
        bool m_PaletteBoxActive = false;
        glm::ivec2 m_PaletteBoxStart{ 0 }, m_PaletteBoxEnd{ 0 };
        std::vector<BrushPick> m_Picks;
        bool m_PicksLoaded = false;
        glm::ivec2 m_HoverCell{ 0 };
        bool m_HasHover = false;
        bool m_ConfirmClear = false;

        std::deque<Snapshot> m_UndoStack;
        std::deque<Snapshot> m_RedoStack;
    };
}
