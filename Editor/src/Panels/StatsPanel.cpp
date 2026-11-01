#include "StatsPanel.h"
#include <string>
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <algorithm>

namespace Conqueror::Editor
{
    void StatsPanel::SetStats(const RendererStats& stats)
    {
        m_Stats = stats;
    }

    void StatsPanel::SetFPS(float fps, float frameTime)
    {
        m_FPS = fps;
        m_FrameTime = frameTime;
        m_FrameHistory[(size_t)m_HistoryOffset] = frameTime;
        m_HistoryOffset = (m_HistoryOffset + 1) % (int)m_FrameHistory.size();
    }

    void StatsPanel::OnImGuiRender()
    {
        if (ImGui::Begin("Stats"))
            DrawContents();
        ImGui::End();
    }

    void StatsPanel::DrawOverlay(const char* id, const ImVec2& anchor, bool alignRight, bool& expanded, float maxHeight)
    {
        const float width = expanded ? 250.0f : 0.0f;
        const ImVec4 fpsColor = m_FPS >= 55.0f ? UI::Colors::Success : (m_FPS >= 30.0f ? UI::Colors::Warning : UI::Colors::Error);

        char header[96];
        snprintf(header, sizeof(header), "%s  Stats   %.0f FPS  %.2f ms",
                 expanded ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT, m_FPS, m_FrameTime);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float headerW = ImGui::CalcTextSize(header).x + style.FramePadding.x * 2.0f + 8.0f;
        const float w = std::max(width, headerW);

        ImGui::SetCursorScreenPos(ImVec2(alignRight ? anchor.x - w : anchor.x, anchor.y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.065f, 0.075f, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
        ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        if (maxHeight > 0.0f)
            ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, maxHeight));
        if (ImGui::BeginChild(id, ImVec2(w, 0.0f), childFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, expanded ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : fpsColor);
            // Etiket her karede değişiyor (FPS); kimlik sabit olmalı, yoksa basma ile bırakma arasında
            // değişen etiket tıklamayı yutar (Stats bazen 2-3 tıklamada açılıyordu)
            std::string headerLabel = std::string(header) + "###StatsHeader";
            if (ImGui::Button(headerLabel.c_str(), ImVec2(-1.0f, 0.0f)))
                expanded = !expanded;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(expanded ? "Collapse statistics" : "Expand statistics");
            if (expanded)
                DrawContents();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    void StatsPanel::DrawContents()
    {
        // Kare süresi grafiği
        float maxFrame = 1.0f;
        for (float f : m_FrameHistory) maxFrame = std::max(maxFrame, f);
        char overlay[48];
        snprintf(overlay, sizeof(overlay), "%.1f FPS  (%.2f ms)", m_FPS, m_FrameTime);
        ImGui::PushStyleColor(ImGuiCol_PlotLines, UI::Colors::Accent);
        ImGui::PlotLines("##frametime", m_FrameHistory.data(), (int)m_FrameHistory.size(), m_HistoryOffset,
                         overlay, 0.0f, maxFrame * 1.2f, ImVec2(-1.0f, 42.0f));
        ImGui::PopStyleColor();

        auto row = [](const char* label, const char* fmt, auto value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(UI::Colors::TextDim, "%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(fmt, value);
        };
        auto section = [](const char* title)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(UI::Colors::Accent, "%s", title);
        };

        if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            section("Rendering");
            row("Draw Calls", "%u", m_Stats.DrawCalls);
            row("Triangles", "%u", m_Stats.Triangles);
            row("Vertices", "%u", m_Stats.Vertices);
            row("Entities", "%u", m_EntityCount);
            section("3D");
            row("Meshes", "%u", m_Stats.MeshCount);
            row("Culled (off screen)", "%u", m_Stats.CulledObjects);
            row("Shadow Draws", "%u", m_Stats.ShadowDrawCalls);
            row("Lights", "%u", m_Stats.LightCount);
            section("2D");
            row("Quads", "%u", m_Stats.QuadCount);
            row("Sprites", "%u", m_Stats.SpriteCount);
            row("Text", "%u", m_Stats.TextCount);
            section("Memory");
            row("Textures", "%.2f MB", m_Stats.TextureMemory);
            row("Meshes", "%.2f MB", m_Stats.MeshMemory);
            if (m_Stats.DebugLineCount || m_Stats.DebugSolidTriangles || m_Stats.DebugTextEntries)
            {
                section("Debug Draw");
                row("Lines", "%u", m_Stats.DebugLineCount);
                row("Solid Tris", "%u", m_Stats.DebugSolidTriangles);
                row("Text", "%u", m_Stats.DebugTextEntries);
            }
            ImGui::EndTable();
        }
    }
}
