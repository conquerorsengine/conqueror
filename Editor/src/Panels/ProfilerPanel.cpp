#include "ProfilerPanel.h"
#include "UI/EditorUI.h"
#include "Core/Project/Project.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <map>

namespace Conqueror::Editor
{
    using namespace Profiling;

    namespace
    {
        ImU32 ToU32(const glm::vec4& c, float alphaScale = 1.0f)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alphaScale));
        }

        // Yığın sırası (alttan üste): Unity'deki gibi render en altta
        constexpr Category kStackOrder[] = { Category::Rendering, Category::Scripts, Category::Physics, Category::Animation, Category::GC,
                                             Category::UI, Category::Particles, Category::Audio, Category::AI, Category::Editor, Category::VSync,
                                             Category::Other };

        // Takip edilen (en yeni) karede GPU henüz çözülmemişse, çözülmüş en yeni kare
        const FrameData* LatestWithGPU()
        {
            const auto& frames = FrameProfiler::GetFrames();
            for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                if (it->GPUMs >= 0.0)
                    return &*it;
            return nullptr;
        }

        double NiceCeil(double v)
        {
            // Grafik ölçeği: 16.6 / 33.3 / 66.6 ms ve üstü için 1-2-5 dizisi
            const double steps[] = { 1.0, 2.0, 4.0, 8.0, 16.67, 33.33, 66.67, 100.0, 200.0, 500.0, 1000.0, 2000.0 };
            for (double s : steps)
                if (v <= s)
                    return s;
            return std::ceil(v / 1000.0) * 1000.0;
        }

        bool ContainsNoCase(const char* hay, const std::string& needle)
        {
            if (needle.empty())
                return true;
            std::string h(hay ? hay : "");
            std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            std::string n = needle;
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return h.find(n) != std::string::npos;
        }

        std::string FormatCount(double v)
        {
            char buf[32];
            if (v >= 1.0e6) std::snprintf(buf, sizeof(buf), "%.2fM", v / 1.0e6);
            else if (v >= 1.0e3) std::snprintf(buf, sizeof(buf), "%.1fk", v / 1.0e3);
            else std::snprintf(buf, sizeof(buf), "%.0f", v);
            return buf;
        }
    }

    void ProfilerPanel::UpdateRecordingState(bool windowOpen)
    {
        FrameProfiler::SetRecording(windowOpen && m_Record);
    }

    bool ProfilerPanel::SelectFrame(uint64_t index)
    {
        if (!FrameProfiler::FindFrame(index))
            return false;
        m_SelectedFrame = index;
        m_FollowLatest = false;
        return true;
    }

    uint64_t ProfilerPanel::GetSelectedFrame() const
    {
        const auto* f = SelectedFrameData();
        return f ? f->Index : 0;
    }

    const FrameData* ProfilerPanel::SelectedFrameData() const
    {
        const auto& frames = FrameProfiler::GetFrames();
        if (frames.empty())
            return nullptr;
        if (m_FollowLatest)
            return &frames.back();
        if (const FrameData* f = FrameProfiler::FindFrame(m_SelectedFrame))
            return f;
        return &frames.back();
    }

    void ProfilerPanel::OnImGuiRender(bool* open)
    {
        // Unity gibi ilk açılışta yüzen pencere (oyun görünümü yanında görünür kalır); kullanıcı istediği yere yerleştirir
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(std::min(1100.0f, vp->WorkSize.x * 0.8f), std::min(680.0f, vp->WorkSize.y * 0.75f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.55f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(ICON_FA_GAUGE_HIGH "  Profiler###Profiler", open))
        {
            ImGui::End();
            return;
        }

        DrawToolbar();
        ImGui::Separator();

        // Üst: grafikler; alt: ayrıntılar. Arada sürüklenebilir ayırıcı.
        const float avail = ImGui::GetContentRegionAvail().y;
        m_ChartHeight = std::clamp(m_ChartHeight, 120.0f, std::max(120.0f, avail - 140.0f));
        DrawCharts(m_ChartHeight);

        ImGui::InvisibleButton("##splitter", ImVec2(-1, 6.0f));
        if (ImGui::IsItemActive())
            m_ChartHeight += ImGui::GetIO().MouseDelta.y;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            float y = (a.y + b.y) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        }

        DrawDetails();
        ImGui::End();
    }

    void ProfilerPanel::DrawToolbar()
    {
        const auto& frames = FrameProfiler::GetFrames();

        // Kayıt düğmesi (kırmızı nokta)
        {
            const bool rec = m_Record;
            if (rec)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.32f, 0.32f, 1.0f));
            if (ImGui::Button(rec ? ICON_FA_CIRCLE " Record" : ICON_FA_CIRCLE_DOT " Record"))
                m_Record = !m_Record;
            if (rec)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(rec ? "Recording frames (click to pause)" : "Paused (click to record)");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH_CAN " Clear"))
        {
            FrameProfiler::Clear();
            m_FollowLatest = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Kare gezinme
        const FrameData* sel = SelectedFrameData();
        auto step = [&](int dir) {
            if (!sel || frames.empty())
                return;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                if (frames[i].Index != sel->Index)
                    continue;
                long j = (long)i + dir;
                if (j >= 0 && j < (long)frames.size())
                {
                    m_SelectedFrame = frames[(size_t)j].Index;
                    m_FollowLatest = false;
                }
                break;
            }
        };
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) step(-1);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) step(+1);
        ImGui::SameLine();
        if (ImGui::Selectable("Current", m_FollowLatest, 0, ImVec2(ImGui::CalcTextSize("Current").x, 0)))
            m_FollowLatest = true;
        ImGui::SameLine();
        if (sel)
        {
            const double fps = sel->CPUMs > 0.0 ? 1000.0 / sel->CPUMs : 0.0;
            ImGui::Text("Frame %llu   CPU %.2f ms (%.0f FPS)", (unsigned long long)sel->Index, sel->CPUMs, fps);
            ImGui::SameLine();
            const FrameData* gpu = sel->GPUMs >= 0.0 ? sel : (m_FollowLatest ? LatestWithGPU() : nullptr);
            if (gpu)
                ImGui::Text("  GPU %.2f ms", gpu->GPUMs);
            else
                ImGui::TextDisabled("  GPU -");
        }
        else
        {
            ImGui::TextDisabled(m_Record ? "Waiting for frames..." : "No frames recorded");
        }

        // Sağ: dışa aktar
        const float exportW = ImGui::CalcTextSize(ICON_FA_FILE_EXPORT " Export Trace").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - exportW));
        if (ImGui::Button(ICON_FA_FILE_EXPORT " Export Trace"))
        {
            auto dir = Project::GetActiveProjectDirectory();
            std::filesystem::path base = dir.empty() ? std::filesystem::current_path() : dir / "Library" / "Profiler";
            std::error_code ec;
            std::filesystem::create_directories(base, ec);
            std::time_t t = std::time(nullptr);
            char name[64];
            std::strftime(name, sizeof(name), "trace_%Y%m%d_%H%M%S.json", std::localtime(&t));
            auto path = base / name;
            m_Status = FrameProfiler::ExportChromeTrace(path) ? "Saved " + path.string() + " (open in chrome://tracing or ui.perfetto.dev)"
                                                              : "Could not write " + path.string();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save the recorded frames as a Chrome / Perfetto trace");
        if (!m_Status.empty())
            ImGui::TextDisabled("%s", m_Status.c_str());
    }

    void ProfilerPanel::DrawCharts(float height)
    {
        const float legendW = 280.0f;
        ImGui::BeginChild("##profcharts", ImVec2(0, height), ImGuiChildFlags_None);
        // CPU grafiği en önemlisi: yüksekliğin ~%40'ı; diğer üçü kalanı paylaşır
        const float usable = height - ImGui::GetStyle().ItemSpacing.y * 3.0f;
        const float cpuH = std::floor(usable * 0.4f);
        const float rowH = std::floor((usable - cpuH) / 3.0f);
        const float chartW = std::max(50.0f, ImGui::GetContentRegionAvail().x - legendW - ImGui::GetStyle().ItemSpacing.x);

        // CPU (kategori göstergesi solda)
        const float rowStartX = ImGui::GetCursorScreenPos().x;
        ImGui::BeginGroup();
        DrawLegend(legendW, cpuH);
        ImGui::EndGroup();
        ImGui::SameLine();
        DrawCPUChart(ImVec2(chartW, cpuH));
        ImGui::SetCursorScreenPos(ImVec2(rowStartX, ImGui::GetCursorScreenPos().y));

        const char* titles[] = { "GPU", "Rendering", "Memory" };
        for (int k = 0; k < 3; ++k)
        {
            ImGui::BeginChild(ImGui::GetID(titles[k]), ImVec2(legendW, rowH), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(titles[k]);
            const FrameData* sel = SelectedFrameData();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.72f, 0.76f, 1.0f));
            if (sel)
            {
                if (k == 0)
                {
                    const FrameData* gpu = sel->GPUMs >= 0.0 ? sel : (m_FollowLatest ? LatestWithGPU() : nullptr);
                    if (gpu) ImGui::Text("GPU Frame  %.2f ms", gpu->GPUMs);
                    else ImGui::TextUnformatted("GPU Frame  -");
                }
                else if (k == 1)
                {
                    ImGui::TextColored(ImVec4(0.42f, 0.78f, 0.31f, 1.0f), "Draw Calls  %s", FormatCount(sel->Counters[(size_t)Counter::DrawCalls]).c_str());
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Triangles   %s", FormatCount(sel->Counters[(size_t)Counter::Triangles]).c_str());
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.29f, 0.56f, 0.89f, 1.0f), "Textures  %.1f MB", sel->Counters[(size_t)Counter::TextureMB]);
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.65f, 1.0f), "Meshes    %.1f MB", sel->Counters[(size_t)Counter::MeshMB]);
                }
            }
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::SameLine();
            DrawLineChart(titles[k], ImVec2(chartW, rowH), k);
            ImGui::SetCursorScreenPos(ImVec2(rowStartX, ImGui::GetCursorScreenPos().y));
        }
        // İmleci taşıdıktan sonra bir öğe gönderilmeli (ImGui sınır kuralı)
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::EndChild();
    }

    void ProfilerPanel::DrawLegend(float width, float height)
    {
        ImGui::BeginChild("##cpulegend", ImVec2(width, height), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("CPU Usage");
        ImGui::SameLine();
        ImGui::TextDisabled("(click to hide)");
        const FrameData* sel = SelectedFrameData();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        if (ImGui::BeginTable("##legend", 2, ImGuiTableFlags_SizingStretchSame))
        {
            for (Category c : kStackOrder)
            {
                const size_t ci = (size_t)c;
                ImGui::TableNextColumn();
                ImGui::PushID((int)ci);
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float sz = ImGui::GetTextLineHeight() * 0.65f;
                const float yOff = (ImGui::GetTextLineHeight() - sz) * 0.5f;
                dl->AddRectFilled(ImVec2(p.x, p.y + yOff), ImVec2(p.x + sz, p.y + yOff + sz), ToU32(FrameProfiler::CategoryColor(c), m_ShowCategory[ci] ? 1.0f : 0.25f), 2.0f);
                ImGui::Dummy(ImVec2(sz + 3.0f, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0, 0);
                if (!m_ShowCategory[ci])
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                char label[64];
                // ###: kimlik sabit (değişen ms değeri tıklamayı yutmasın)
                if (sel)
                    std::snprintf(label, sizeof(label), "%s %.2f###cat%d", FrameProfiler::CategoryName(c), sel->CategoryMs[ci], ci);
                else
                    std::snprintf(label, sizeof(label), "%s###cat%d", FrameProfiler::CategoryName(c), ci);
                if (ImGui::Selectable(label, false))
                    m_ShowCategory[ci] = !m_ShowCategory[ci];
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s: %.3f ms self time in this frame", FrameProfiler::CategoryName(c), sel ? sel->CategoryMs[ci] : 0.0f);
                if (!m_ShowCategory[ci])
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    bool ProfilerPanel::HandleChartInput(const ImVec2& pos, const ImVec2& size, float frameStep)
    {
        const auto& frames = FrameProfiler::GetFrames();
        if (frames.empty())
            return false;
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##chartinput", size);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered || ImGui::IsItemActive())
        {
            const float mx = ImGui::GetIO().MousePos.x;
            // En yeni kare sağda
            long fromRight = (long)std::floor((pos.x + size.x - mx) / frameStep);
            long idx = (long)frames.size() - 1 - fromRight;
            // Henüz kare kaydedilmemiş (boş) bölge
            if (idx < 0)
                return hovered;
            idx = std::min(idx, (long)frames.size() - 1);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_SelectedFrame = frames[(size_t)idx].Index;
                m_FollowLatest = false;
            }
            if (hovered)
            {
                const FrameData& f = frames[(size_t)idx];
                ImGui::BeginTooltip();
                ImGui::Text("Frame %llu", (unsigned long long)f.Index);
                ImGui::Text("CPU %.2f ms", f.CPUMs);
                if (f.GPUMs >= 0.0)
                    ImGui::Text("GPU %.2f ms", f.GPUMs);
                ImGui::Text("Draw Calls %s  Tris %s", FormatCount(f.Counters[(size_t)Counter::DrawCalls]).c_str(),
                            FormatCount(f.Counters[(size_t)Counter::Triangles]).c_str());
                ImGui::EndTooltip();
            }
        }
        return hovered;
    }

    void ProfilerPanel::DrawCPUChart(const ImVec2& size)
    {
        const auto& frames = FrameProfiler::GetFrames();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, end, IM_COL32(24, 25, 28, 255), 3.0f);
        dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);

        const float step = size.x / (float)FrameProfiler::MaxFrames;
        double maxMs = 1.0;
        for (const auto& f : frames)
        {
            double sum = 0.0;
            for (Category c : kStackOrder)
                if (m_ShowCategory[(size_t)c])
                    sum += f.CategoryMs[(size_t)c];
            maxMs = std::max(maxMs, sum);
        }
        const double scale = NiceCeil(maxMs * 1.05);
        auto yOf = [&](double ms) { return end.y - (float)(ms / scale) * (size.y - 4.0f); };

        dl->PushClipRect(pos, end, true);
        // Yığılmış sütunlar (her kare bir sütun; geniş görünümde dolu alan gibi görünür)
        const size_t n = frames.size();
        for (size_t i = 0; i < n; ++i)
        {
            const FrameData& f = frames[i];
            const float x1 = end.x - (float)(n - 1 - i) * step;
            const float x0 = x1 - std::max(step, 1.0f);
            double acc = 0.0;
            for (Category c : kStackOrder)
            {
                const size_t ci = (size_t)c;
                if (!m_ShowCategory[ci] || f.CategoryMs[ci] <= 0.0f)
                    continue;
                const float yTop = yOf(acc + f.CategoryMs[ci]);
                const float yBot = yOf(acc);
                if (yBot - yTop >= 0.25f)
                    dl->AddRectFilled(ImVec2(x0, yTop), ImVec2(x1, yBot), ToU32(FrameProfiler::CategoryColor(c), 0.9f));
                acc += f.CategoryMs[ci];
            }
        }
        // 60 / 30 FPS kılavuzları
        for (double guide : { 16.667, 33.333, 66.667 })
        {
            if (guide > scale)
                continue;
            const float y = yOf(guide);
            dl->AddLine(ImVec2(pos.x, y), ImVec2(end.x, y), IM_COL32(255, 255, 255, 50), 1.0f);
            char label[32];
            std::snprintf(label, sizeof(label), "%.1fms (%dFPS)", guide, (int)std::round(1000.0 / guide));
            dl->AddText(ImVec2(pos.x + 4.0f, y - ImGui::GetTextLineHeight()), IM_COL32(255, 255, 255, 120), label);
        }
        {
            char label[32];
            std::snprintf(label, sizeof(label), "%.1f ms", scale);
            dl->AddText(ImVec2(pos.x + 4.0f, pos.y + 2.0f), IM_COL32(255, 255, 255, 110), label);
        }
        // Seçili kare
        if (const FrameData* sel = SelectedFrameData())
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (frames[i].Index != sel->Index)
                    continue;
                const float x = end.x - (float)(n - 1 - i) * step - step * 0.5f;
                dl->AddLine(ImVec2(x, pos.y), ImVec2(x, end.y), IM_COL32(255, 255, 255, 220), 1.5f);
                break;
            }
        }
        dl->PopClipRect();

        HandleChartInput(pos, size, step);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, end.y + ImGui::GetStyle().ItemSpacing.y));
    }

    void ProfilerPanel::DrawLineChart(const char* id, const ImVec2& size, int kind)
    {
        ImGui::PushID(id);
        const auto& frames = FrameProfiler::GetFrames();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, end, IM_COL32(24, 25, 28, 255), 3.0f);
        dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
        const float step = size.x / (float)FrameProfiler::MaxFrames;
        const size_t n = frames.size();

        struct Series { Counter C; ImU32 Color; bool Gpu; };
        std::vector<Series> series;
        if (kind == 0) series = { { Counter::Count, IM_COL32(110, 200, 80, 255), true } };
        else if (kind == 1) series = { { Counter::DrawCalls, IM_COL32(107, 199, 79, 255), false }, { Counter::Triangles, IM_COL32(242, 191, 64, 255), false } };
        else series = { { Counter::TextureMB, IM_COL32(74, 143, 227, 255), false }, { Counter::MeshMB, IM_COL32(242, 115, 166, 255), false } };

        dl->PushClipRect(pos, end, true);
        for (const auto& s : series)
        {
            double maxV = 0.0;
            for (const auto& f : frames)
                maxV = std::max(maxV, s.Gpu ? f.GPUMs : f.Counters[(size_t)s.C]);
            if (s.Gpu)
                maxV = NiceCeil(std::max(maxV, 1.0));
            else
                maxV = std::max(maxV * 1.15, 1.0);
            ImVec2 prev;
            bool hasPrev = false;
            for (size_t i = 0; i < n; ++i)
            {
                const double v = s.Gpu ? frames[i].GPUMs : frames[i].Counters[(size_t)s.C];
                if (v < 0.0)
                {
                    hasPrev = false;
                    continue;
                }
                ImVec2 p(end.x - (float)(n - 1 - i) * step - step * 0.5f, end.y - 2.0f - (float)(v / maxV) * (size.y - 6.0f));
                if (s.Gpu)
                    dl->AddRectFilled(ImVec2(p.x - step * 0.5f, p.y), ImVec2(p.x + step * 0.5f, end.y), (s.Color & 0x00FFFFFF) | 0xC0000000);
                else if (hasPrev)
                    dl->AddLine(prev, p, s.Color, 1.5f);
                prev = p;
                hasPrev = true;
            }
            if (s.Gpu)
            {
                for (double guide : { 16.667, 33.333 })
                {
                    if (guide > maxV)
                        continue;
                    float y = end.y - 2.0f - (float)(guide / maxV) * (size.y - 6.0f);
                    dl->AddLine(ImVec2(pos.x, y), ImVec2(end.x, y), IM_COL32(255, 255, 255, 50));
                }
                char label[32];
                std::snprintf(label, sizeof(label), "%.1f ms", maxV);
                dl->AddText(ImVec2(pos.x + 4.0f, pos.y + 2.0f), IM_COL32(255, 255, 255, 110), label);
            }
        }
        if (const FrameData* sel = SelectedFrameData())
        {
            for (size_t i = 0; i < n; ++i)
                if (frames[i].Index == sel->Index)
                {
                    const float x = end.x - (float)(n - 1 - i) * step - step * 0.5f;
                    dl->AddLine(ImVec2(x, pos.y), ImVec2(x, end.y), IM_COL32(255, 255, 255, 220), 1.5f);
                    break;
                }
        }
        if (kind == 0 && n > 0 && frames.back().GPUMs < 0.0 && std::none_of(frames.begin(), frames.end(), [](const FrameData& f) { return f.GPUMs >= 0.0; }))
            dl->AddText(ImVec2(pos.x + 8.0f, pos.y + size.y * 0.5f - 8.0f), IM_COL32(255, 255, 255, 110), "GPU timings appear a few frames after recording starts");
        dl->PopClipRect();
        HandleChartInput(pos, size, step);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, end.y + ImGui::GetStyle().ItemSpacing.y));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }

    void ProfilerPanel::DrawDetails()
    {
        const FrameData* frame = SelectedFrameData();
        if (ImGui::BeginTabBar("##profdetails"))
        {
            const View views[] = { View::Hierarchy, View::Timeline, View::GPU };
            const char* names[] = { ICON_FA_LIST_UL " Hierarchy", ICON_FA_BARS_STAGGERED " Timeline", ICON_FA_MICROCHIP " GPU" };
            for (int i = 0; i < 3; ++i)
            {
                const ImGuiTabItemFlags flags = m_RequestedView == i ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(names[i], nullptr, flags))
                {
                    m_View = views[i];
                    ImGui::EndTabItem();
                }
            }
            m_RequestedView = -1;
            ImGui::EndTabBar();
        }
        if (!frame)
        {
            ImGui::TextDisabled(m_Record ? "Recording... frames appear here while the editor runs." : "Press Record to capture frames.");
            return;
        }
        switch (m_View)
        {
            case View::Hierarchy: DrawHierarchy(*frame); break;
            case View::Timeline: DrawTimeline(*frame); break;
            case View::GPU:
            {
                // En yeni kare izleniyorsa GPU sonuçları birkaç kare geriden gelir
                const FrameData* gpu = (frame->GPUMs < 0.0 && m_FollowLatest) ? LatestWithGPU() : frame;
                if (gpu && gpu != frame)
                    ImGui::TextDisabled("Showing frame %llu (latest frame with GPU timings)", (unsigned long long)gpu->Index);
                DrawGPU(gpu ? *gpu : *frame);
                break;
            }
        }
    }

    void ProfilerPanel::BuildTree(const FrameData& frame, std::vector<Node>& nodes) const
    {
        // Aynı üst altındaki aynı adlı örnekler tek düğümde birleşir (Calls)
        nodes.clear();
        nodes.push_back(Node{}); // kök
        std::vector<int> sampleNode(frame.Samples.size(), 0);
        std::map<std::pair<int, const char*>, int> lookup;
        for (size_t i = 0; i < frame.Samples.size(); ++i)
        {
            const Sample& s = frame.Samples[i];
            const int parentNode = s.Parent == UINT32_MAX ? 0 : sampleNode[s.Parent];
            auto key = std::make_pair(parentNode, s.Name);
            auto it = lookup.find(key);
            int idx;
            if (it == lookup.end())
            {
                idx = (int)nodes.size();
                Node n;
                n.Name = s.Name;
                n.Cat = s.Cat;
                n.Parent = parentNode;
                nodes.push_back(n);
                nodes[(size_t)parentNode].Children.push_back(idx);
                lookup.emplace(key, idx);
            }
            else
            {
                idx = it->second;
            }
            sampleNode[i] = idx;
            nodes[(size_t)idx].TotalMs += (double)s.DurationNs / 1.0e6;
            nodes[(size_t)idx].Calls += 1;
        }
        for (size_t i = 1; i < nodes.size(); ++i)
        {
            double child = 0.0;
            for (int c : nodes[i].Children)
                child += nodes[(size_t)c].TotalMs;
            nodes[i].SelfMs = std::max(0.0, nodes[i].TotalMs - child);
        }
        for (auto& n : nodes)
            std::sort(n.Children.begin(), n.Children.end(), [&](int a, int b) { return nodes[(size_t)a].TotalMs > nodes[(size_t)b].TotalMs; });
    }

    void ProfilerPanel::DrawHierarchyNode(const std::vector<Node>& nodes, int index, double frameMs)
    {
        const Node& n = nodes[(size_t)index];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(index);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow;
        if (n.Children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (n.TotalMs > frameMs * 0.05)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        // Kategori rengi küçük şerit
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 4.0f, p.y + 2.0f), ImVec2(p.x - 1.0f, p.y + ImGui::GetTextLineHeight() - 1.0f),
                                                  ToU32(FrameProfiler::CategoryColor(n.Cat)));
        const bool open = ImGui::TreeNodeEx(n.Name, flags);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f%%", frameMs > 0.0 ? n.TotalMs / frameMs * 100.0 : 0.0);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f%%", frameMs > 0.0 ? n.SelfMs / frameMs * 100.0 : 0.0);
        ImGui::TableNextColumn();
        ImGui::Text("%d", n.Calls);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", n.TotalMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", n.SelfMs);
        if (open && !n.Children.empty())
        {
            for (int c : n.Children)
                DrawHierarchyNode(nodes, c, frameMs);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ProfilerPanel::DrawHierarchy(const FrameData& frame)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", m_Search.c_str());
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputTextWithHint("##profsearch", ICON_FA_MAGNIFYING_GLASS " Search samples", buf, sizeof(buf)))
            m_Search = buf;
        ImGui::SameLine();
        ImGui::TextDisabled("%zu samples", frame.Samples.size());

        std::vector<Node> nodes;
        BuildTree(frame, nodes);
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("##profhier", 6, tflags))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Overview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Time ms", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableHeadersRow();

        if (m_Search.empty())
        {
            for (int c : nodes[0].Children)
                DrawHierarchyNode(nodes, c, frame.CPUMs);
        }
        else
        {
            // Düz liste: aynı adlı düğümler toplanır
            std::map<std::string, Node> flat;
            for (size_t i = 1; i < nodes.size(); ++i)
            {
                if (!ContainsNoCase(nodes[i].Name, m_Search))
                    continue;
                Node& f = flat[nodes[i].Name];
                f.Name = nodes[i].Name;
                f.Cat = nodes[i].Cat;
                f.TotalMs += nodes[i].TotalMs;
                f.SelfMs += nodes[i].SelfMs;
                f.Calls += nodes[i].Calls;
            }
            std::vector<Node> list;
            for (auto& [k, v] : flat)
                list.push_back(v);
            std::sort(list.begin(), list.end(), [](const Node& a, const Node& b) { return a.TotalMs > b.TotalMs; });
            for (size_t i = 0; i < list.size(); ++i)
            {
                const Node& n = list[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(n.Name);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", frame.CPUMs > 0.0 ? n.TotalMs / frame.CPUMs * 100.0 : 0.0);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", frame.CPUMs > 0.0 ? n.SelfMs / frame.CPUMs * 100.0 : 0.0);
                ImGui::TableNextColumn();
                ImGui::Text("%d", n.Calls);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", n.TotalMs);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", n.SelfMs);
            }
        }
        ImGui::EndTable();
    }

    void ProfilerPanel::DrawTimeline(const FrameData& frame)
    {
        if (m_TimelineFrame != frame.Index)
        {
            // Yeni kare: önceki yakınlaştırma görünür aralıkta kalsın, ama kareye sığmayan durumda sıfırla
            m_TimelineFrame = frame.Index;
            if (m_TimelineSpanMs <= 0.0 || m_TimelineStartMs > frame.CPUMs)
            {
                m_TimelineStartMs = 0.0;
                m_TimelineSpanMs = 0.0;
            }
        }
        if (ImGui::Button("Fit"))
        {
            m_TimelineStartMs = 0.0;
            m_TimelineSpanMs = 0.0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Mouse wheel: zoom, drag: pan");

        const double frameMs = std::max(frame.CPUMs, 0.001);
        double span = m_TimelineSpanMs > 0.0 ? m_TimelineSpanMs : frameMs;
        double start = m_TimelineSpanMs > 0.0 ? m_TimelineStartMs : 0.0;

        ImGui::BeginChild("##timeline", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float rulerH = ImGui::GetTextLineHeight() + 4.0f;
        const float rowH = ImGui::GetTextLineHeight() + 6.0f;
        int maxDepth = 0;
        for (const auto& s : frame.Samples)
            maxDepth = std::max(maxDepth, (int)s.Depth);
        const float contentH = std::max(avail.y, rulerH + (float)(maxDepth + 1) * rowH + 8.0f);
        ImGui::InvisibleButton("##tlcanvas", ImVec2(std::max(avail.x, 10.0f), contentH));
        const bool hovered = ImGui::IsItemHovered();
        const float width = std::max(avail.x, 10.0f);
        auto xOf = [&](double ms) { return pos.x + (float)((ms - start) / span) * width; };

        // Etkileşim: tekerlek yakınlaştırır (fare altındaki zaman sabit), sürükleme kaydırır
        if (hovered)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const double mouseMs = start + (ImGui::GetIO().MousePos.x - pos.x) / width * span;
                const double newSpan = std::clamp(span * std::pow(0.8, (double)wheel), 0.001, frameMs);
                start = mouseMs - (mouseMs - start) * (newSpan / span);
                span = newSpan;
            }
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
        {
            start -= ImGui::GetIO().MouseDelta.x / width * span;
        }
        start = std::clamp(start, 0.0, std::max(0.0, frameMs - span));
        m_TimelineStartMs = start;
        m_TimelineSpanMs = span >= frameMs - 1e-9 ? 0.0 : span;

        // Cetvel
        {
            const double targetTicks = std::max(2.0, width / 90.0);
            double tick = span / targetTicks;
            const double mag = std::pow(10.0, std::floor(std::log10(tick)));
            for (double m : { 1.0, 2.0, 5.0, 10.0 })
                if (m * mag >= tick) { tick = m * mag; break; }
            for (double t = std::floor(start / tick) * tick; t <= start + span + 1e-9; t += tick)
            {
                const float x = xOf(t);
                if (x < pos.x - 1.0f)
                    continue;
                dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + contentH), IM_COL32(255, 255, 255, 22));
                char label[32];
                std::snprintf(label, sizeof(label), tick < 0.1 ? "%.3fms" : "%.2fms", t);
                dl->AddText(ImVec2(x + 3.0f, pos.y + 1.0f), IM_COL32(200, 200, 205, 160), label);
            }
        }

        const ImVec2 clipMin(pos.x, pos.y + rulerH), clipMax(pos.x + width, pos.y + contentH);
        dl->PushClipRect(clipMin, clipMax, true);
        const Sample* hoveredSample = nullptr;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (const auto& s : frame.Samples)
        {
            const double s0 = (double)s.StartNs / 1.0e6;
            const double s1 = s0 + (double)s.DurationNs / 1.0e6;
            if (s1 < start || s0 > start + span)
                continue;
            float x0 = xOf(s0), x1 = xOf(s1);
            if (x1 - x0 < 1.0f)
                x1 = x0 + 1.0f;
            const float y0 = pos.y + rulerH + (float)s.Depth * rowH;
            const float y1 = y0 + rowH - 2.0f;
            const glm::vec4 c = FrameProfiler::CategoryColor(s.Cat);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ToU32(c, 0.85f), 2.0f);
            if (x1 - x0 > 40.0f)
            {
                char label[160];
                std::snprintf(label, sizeof(label), "%s %.2fms", s.Name, (double)s.DurationNs / 1.0e6);
                const float tx = std::max(x0, pos.x) + 3.0f;
                dl->PushClipRect(ImVec2(std::max(x0, pos.x), y0), ImVec2(std::min(x1, pos.x + width), y1), true);
                dl->AddText(ImVec2(tx, y0 + 2.0f), IM_COL32(15, 15, 18, 255), label);
                dl->PopClipRect();
            }
            if (hovered && mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
                hoveredSample = &s;
        }
        dl->PopClipRect();
        if (hoveredSample)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(hoveredSample->Name);
            ImGui::TextDisabled("%s", FrameProfiler::CategoryName(hoveredSample->Cat));
            ImGui::Text("%.3f ms  (starts at %.3f ms)", (double)hoveredSample->DurationNs / 1.0e6, (double)hoveredSample->StartNs / 1.0e6);
            ImGui::EndTooltip();
        }
        ImGui::EndChild();
    }

    void ProfilerPanel::DrawGPU(const FrameData& frame)
    {
        if (frame.GPUMs < 0.0)
        {
            UI::InfoBox("GPU timings for this frame are not available yet (they are read back a few frames later) "
                        "or the graphics driver does not support timer queries.");
            return;
        }
        ImGui::Text("GPU frame: %.3f ms", frame.GPUMs);
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("##gputable", 4, tflags))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();
        for (const auto& g : frame.GPUSamples)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Indent((float)g.Depth * 12.0f + 1.0f);
            ImGui::TextUnformatted(g.Name);
            ImGui::Unindent((float)g.Depth * 12.0f + 1.0f);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", g.DurationMs);
            ImGui::TableNextColumn();
            const double pct = frame.GPUMs > 0.0 ? g.DurationMs / frame.GPUMs : 0.0;
            ImGui::Text("%.1f", pct * 100.0);
            ImGui::TableNextColumn();
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = 150.0f * (float)std::clamp(pct, 0.0, 1.0);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3.0f), ImVec2(p.x + std::max(w, 1.0f), p.y + ImGui::GetTextLineHeight() - 2.0f),
                                                      IM_COL32(110, 200, 80, 220), 2.0f);
            ImGui::Dummy(ImVec2(150.0f, ImGui::GetTextLineHeight()));
        }
        ImGui::EndTable();
    }
}
