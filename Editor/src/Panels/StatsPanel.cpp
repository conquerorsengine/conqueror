#include "StatsPanel.h"

#include <imgui.h>

namespace Conqueror::Editor
{
    void StatsPanel::OnImGuiRender()
    {
        ImGui::Begin("Stats");

        // FPS ve Frame Time
        ImGui::Text("FPS: %.1f", m_FPS);
        ImGui::Text("Frame Time: %.3f ms", m_FrameTime);
        
        ImGui::Separator();
        
        // Render Stats
        ImGui::Text("Draw Calls: %u", m_Stats.DrawCalls);
        ImGui::Text("Vertices: %u", m_Stats.Vertices);
        ImGui::Text("Triangles: %u", m_Stats.Triangles);
        
        ImGui::Separator();
        
        // 2D Stats
        ImGui::Text("Quads: %u", m_Stats.QuadCount);
        ImGui::Text("Sprites: %u", m_Stats.SpriteCount);
        ImGui::Text("Text: %u", m_Stats.TextCount);
        
        ImGui::Separator();
        
        // 3D Stats
        ImGui::Text("Meshes: %u", m_Stats.MeshCount);
        ImGui::Text("Lights: %u", m_Stats.LightCount);
        
        ImGui::Separator();
        
        // Memory
        ImGui::Text("Texture Memory: %.2f MB", m_Stats.TextureMemory);
        ImGui::Text("Mesh Memory: %.2f MB", m_Stats.MeshMemory);

        ImGui::Separator();

        // Debug
        ImGui::Text("Debug Lines: %u", m_Stats.DebugLineCount);
        ImGui::Text("Debug Line Vertices: %u", m_Stats.DebugLineVertices);
        ImGui::Text("Debug Solid Tris: %u", m_Stats.DebugSolidTriangles);
        ImGui::Text("Debug Text Entries: %u", m_Stats.DebugTextEntries);
        ImGui::Text("Debug Timed Entries: %u", m_Stats.DebugTimedEntries);

        ImGui::End();
    }

    void StatsPanel::SetStats(const RendererStats& stats)
    {
        m_Stats = stats;
    }
}
