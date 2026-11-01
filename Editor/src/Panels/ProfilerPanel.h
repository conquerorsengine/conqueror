#pragma once

#include "Core/Profiling/FrameProfiler.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Unity Profiler benzeri pencere: kategori yığınlı CPU grafiği, GPU / render / bellek grafikleri,
    // seçili kare için hiyerarşi (Total/Self/Calls), zaman çizelgesi ve GPU örnekleri.
    class ProfilerPanel
    {
    public:
        enum class View : int { Hierarchy = 0, Timeline, GPU };

        void OnImGuiRender(bool* open);
        // Pencere kapalıyken kayıt yapılmaz (Unity gibi); EditorLayer her karede çağırır
        void UpdateRecordingState(bool windowOpen);

        bool IsRecordEnabled() const { return m_Record; }
        void SetRecordEnabled(bool record) { m_Record = record; }
        void SetView(View v) { m_View = v; m_RequestedView = (int)v; }
        void SelectLatest() { m_FollowLatest = true; }
        bool SelectFrame(uint64_t index);
        uint64_t GetSelectedFrame() const;

    private:
        struct Node
        {
            const char* Name = "";
            Profiling::Category Cat = Profiling::Category::Other;
            double TotalMs = 0.0;
            double SelfMs = 0.0;
            int Calls = 0;
            int Parent = -1;
            std::vector<int> Children;
        };

        void DrawToolbar();
        void DrawCharts(float height);
        void DrawCPUChart(const ImVec2& size);
        void DrawLineChart(const char* id, const ImVec2& size, int kind);
        void DrawLegend(float width, float height);
        bool HandleChartInput(const ImVec2& pos, const ImVec2& size, float frameStep);
        void DrawDetails();
        void DrawHierarchy(const Profiling::FrameData& frame);
        void DrawHierarchyNode(const std::vector<Node>& nodes, int index, double frameMs);
        void DrawTimeline(const Profiling::FrameData& frame);
        void DrawGPU(const Profiling::FrameData& frame);
        void BuildTree(const Profiling::FrameData& frame, std::vector<Node>& nodes) const;
        const Profiling::FrameData* SelectedFrameData() const;

    private:
        bool m_Record = true;
        bool m_FollowLatest = true;
        uint64_t m_SelectedFrame = 0;
        View m_View = View::Hierarchy;
        int m_RequestedView = -1;        // SetView ile sekme seçimi (bir kare)
        bool m_ShowCategory[(size_t)Profiling::Category::Count] = { true, true, true, true, true, true, true, true, true, true, true, true };
        float m_ChartHeight = 330.0f;
        std::string m_Search;
        std::string m_Status;

        // Zaman çizelgesi görünümü
        double m_TimelineStartMs = 0.0;
        double m_TimelineSpanMs = 0.0;   // 0: kareye sığdır
        uint64_t m_TimelineFrame = 0;
    };
}
