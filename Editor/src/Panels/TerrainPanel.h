#pragma once

#include "Terrain/Terrain.h"
#include "Scene/Entity.h"
#include "ViewportPanel.h"

#include <deque>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Unity Terrain araçları: yükselt/alçalt, yumuşat, düzleştir, gürültü, doku boya;
    // prosedürel üretim, erozyon, otomatik boyama, heightmap içe/dışa aktarma.
    // Viewport'ta fırça önizlemesi ve sürükleyerek düzenleme (ViewportToolHook).
    class TerrainPanel
    {
    public:
        enum class Tool : int { None = -1, RaiseLower = 0, Smooth, Flatten, Noise, Paint, Count };

        void SetContext(Scene* scene) { m_Context = scene; m_UndoStack.clear(); m_RedoStack.clear(); }
        void OnImGuiRender(bool* open);
        bool OnViewportTool(const ViewportToolContext& ctx);

        // Seçili (ya da sahnedeki ilk) terrain
        Entity GetTarget() const;
        Tool GetTool() const { return m_Tool; }
        void SetTool(Tool tool) { m_Tool = tool; }
        TerrainBrush& GetBrush() { return m_Brush; }
        TerrainNoiseSettings& GetNoise() { return m_Noise; }

        // Otomasyon/test: yerel xz noktasına tek fırça darbesi
        void ApplyStroke(Entity terrain, const glm::vec2& localCenter, float dt, bool invert);

        static Entity CreateTerrain(Scene* scene, int resolution = 129, float size = 100.0f);

    private:
        struct Snapshot
        {
            uint64_t EntityID = 0;
            std::vector<float> Heights;
            std::vector<uint8_t> Splat;
        };

        void DrawToolbar(TerrainComponent& t);
        void DrawBrushSettings(TerrainComponent& t);
        void DrawLayers(TerrainComponent& t);
        void DrawGenerate(TerrainComponent& t, Entity e);
        void DrawSettings(TerrainComponent& t, Entity e);
        void PushUndo(Entity e);
        void Undo();
        void Redo();
        TerrainBrushOp CurrentOp(bool invert) const;

        Scene* m_Context = nullptr;
        Tool m_Tool = Tool::RaiseLower;
        TerrainBrush m_Brush;
        TerrainNoiseSettings m_Noise;
        bool m_Stroking = false;
        glm::vec3 m_LastHit{ 0.0f };
        bool m_HasHit = false;
        int m_NewResolution = 129;

        std::deque<Snapshot> m_UndoStack;
        std::deque<Snapshot> m_RedoStack;
    };
}
