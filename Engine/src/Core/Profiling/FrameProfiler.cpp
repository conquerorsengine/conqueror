#include "FrameProfiler.h"
#include "Renderer/RHI/RendererAPI.h"

#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace Conqueror::Profiling
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct OpenSample
        {
            uint32_t Index;
            Clock::time_point Start;
        };

        // GPU damgası bekleyen örnekler (kare kare)
        struct PendingGPUSample
        {
            const char* Name;
            uint16_t Depth;
            uint32_t BeginQuery;
            uint32_t EndQuery;
        };
        struct PendingGPUFrame
        {
            uint64_t FrameIndex{};
            uint32_t FrameBegin = 0;
            uint32_t FrameEnd = 0;
            std::vector<PendingGPUSample> Samples;
        };

        struct State
        {
            bool Recording = false;     // Profiler penceresi açıkken kayıt yapar (Unity gibi)
            bool InFrame = false;
            std::thread::id MainThread;
            uint64_t FrameIndex = 0;
            Clock::time_point FrameStart;
            FrameData Current;
            std::vector<OpenSample> Stack;
            std::deque<FrameData> Frames;
            std::unordered_set<std::string> Interned;

            // GPU
            bool GPUSupported = false;
            bool GPUChecked = false;
            std::vector<uint32_t> FreeQueries;
            std::vector<uint32_t> AllQueries;
            PendingGPUFrame GPUCurrent;
            std::deque<PendingGPUFrame> GPUPending;
            std::vector<std::pair<const char*, uint32_t>> GPUStack;   // ad, başlangıç sorgusu
            std::vector<size_t> GPUStackSlot;
        };

        State& S()
        {
            static State s;
            return s;
        }

        bool IsMainThread()
        {
            return std::this_thread::get_id() == S().MainThread;
        }

        uint64_t NsSince(Clock::time_point start, Clock::time_point now)
        {
            return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
        }

        bool GPUAvailable()
        {
            State& s = S();
            if (!s.GPUChecked)
            {
                s.GPUChecked = true;
                // GL_TIMESTAMP sorguları GL 3.3+ çekirdek; Vulkan yolunda ayrı uygulanacak
                s.GPUSupported = RendererAPI::GetAPI() == RendererAPI::API::OpenGL && glQueryCounter != nullptr && glGenQueries != nullptr;
            }
            return s.GPUSupported;
        }

        uint32_t AcquireQuery()
        {
            State& s = S();
            if (s.FreeQueries.empty())
            {
                // En fazla 4096 sorgu: bir şey sızarsa kendini sınırla
                if (s.AllQueries.size() >= 4096)
                    return 0;
                uint32_t ids[32];
                glGenQueries(32, ids);
                for (uint32_t id : ids)
                {
                    s.AllQueries.push_back(id);
                    s.FreeQueries.push_back(id);
                }
            }
            uint32_t q = s.FreeQueries.back();
            s.FreeQueries.pop_back();
            return q;
        }

        void ReleaseFrameQueries(PendingGPUFrame& f)
        {
            State& s = S();
            auto give = [&](uint32_t q) { if (q) s.FreeQueries.push_back(q); };
            give(f.FrameBegin);
            give(f.FrameEnd);
            for (auto& smp : f.Samples)
            {
                give(smp.BeginQuery);
                give(smp.EndQuery);
            }
            f.Samples.clear();
            f.FrameBegin = f.FrameEnd = 0;
        }

        FrameData* FindFrameMutable(uint64_t index)
        {
            for (auto& f : S().Frames)
                if (f.Index == index)
                    return &f;
            return nullptr;
        }

        // Hazır olan GPU karelerini çöz (bloklamadan)
        void ResolveGPU()
        {
            State& s = S();
            while (!s.GPUPending.empty())
            {
                PendingGPUFrame& f = s.GPUPending.front();
                GLint available = 0;
                if (f.FrameEnd)
                    glGetQueryObjectiv(f.FrameEnd, GL_QUERY_RESULT_AVAILABLE, &available);
                // 8 kareden eski ve hâlâ hazır değilse bırak (sürücü sorunları)
                const bool stale = s.FrameIndex > f.FrameIndex + 8;
                if (!available && !stale)
                    break;
                if (available && f.FrameBegin)
                {
                    GLuint64 t0 = 0, t1 = 0;
                    glGetQueryObjectui64v(f.FrameBegin, GL_QUERY_RESULT, &t0);
                    glGetQueryObjectui64v(f.FrameEnd, GL_QUERY_RESULT, &t1);
                    if (FrameData* fd = FindFrameMutable(f.FrameIndex))
                    {
                        // GPU süresi = üst düzey örneklerin toplamı (kare damgaları arası süre CPU beklemelerini de içerir)
                        const double spanMs = t1 > t0 ? (double)(t1 - t0) / 1.0e6 : 0.0;
                        fd->GPUMs = 0.0;
                        fd->GPUSamples.clear();
                        fd->GPUSamples.reserve(f.Samples.size());
                        for (const auto& smp : f.Samples)
                        {
                            if (!smp.BeginQuery || !smp.EndQuery)
                                continue;
                            GLuint64 a = 0, b = 0;
                            glGetQueryObjectui64v(smp.BeginQuery, GL_QUERY_RESULT, &a);
                            glGetQueryObjectui64v(smp.EndQuery, GL_QUERY_RESULT, &b);
                            GPUSample g;
                            g.Name = smp.Name;
                            g.Depth = smp.Depth;
                            g.StartMs = a > t0 ? (double)(a - t0) / 1.0e6 : 0.0;
                            g.DurationMs = b > a ? (double)(b - a) / 1.0e6 : 0.0;
                            if (g.Depth == 0)
                                fd->GPUMs += g.DurationMs;
                            fd->GPUSamples.push_back(g);
                        }
                        if (fd->GPUSamples.empty())
                            fd->GPUMs = spanMs;
                    }
                }
                ReleaseFrameQueries(f);
                s.GPUPending.pop_front();
            }
        }
    }

    void FrameProfiler::BeginFrame()
    {
        State& s = S();
        if (s.MainThread == std::thread::id())
            s.MainThread = std::this_thread::get_id();
        ++s.FrameIndex;
        s.InFrame = s.Recording;
        if (!s.Recording)
            return;

        s.FrameStart = Clock::now();
        s.Stack.clear();
        s.Current = FrameData{};
        s.Current.Index = s.FrameIndex;
        s.Current.Samples.reserve(256);

        if (GPUAvailable())
        {
            ResolveGPU();
            s.GPUCurrent = PendingGPUFrame{};
            s.GPUCurrent.FrameIndex = s.FrameIndex;
            s.GPUCurrent.FrameBegin = AcquireQuery();
            if (s.GPUCurrent.FrameBegin)
                glQueryCounter(s.GPUCurrent.FrameBegin, GL_TIMESTAMP);
            s.GPUStack.clear();
            s.GPUStackSlot.clear();
        }
    }

    void FrameProfiler::EndFrame()
    {
        State& s = S();
        if (!s.InFrame)
            return;
        s.InFrame = false;

        const auto now = Clock::now();
        // Kapanmamış örnekleri kapat (istisna / erken dönüş)
        while (!s.Stack.empty())
        {
            auto& open = s.Stack.back();
            s.Current.Samples[open.Index].DurationNs = NsSince(open.Start, now);
            s.Stack.pop_back();
        }
        s.Current.CPUMs = (double)NsSince(s.FrameStart, now) / 1.0e6;

        // Kategori öz süreleri: çocukların süresi üst örnekten düşülür
        std::vector<uint64_t> childNs(s.Current.Samples.size(), 0);
        uint64_t topLevelNs = 0;
        for (const auto& smp : s.Current.Samples)
        {
            if (smp.Parent != UINT32_MAX)
                childNs[smp.Parent] += smp.DurationNs;
            else
                topLevelNs += smp.DurationNs;
        }
        for (size_t i = 0; i < s.Current.Samples.size(); ++i)
        {
            const auto& smp = s.Current.Samples[i];
            const uint64_t self = smp.DurationNs > childNs[i] ? smp.DurationNs - childNs[i] : 0;
            s.Current.CategoryMs[(size_t)smp.Cat] += (float)((double)self / 1.0e6);
        }
        // Ölçülmeyen süre "Other"
        const double untracked = s.Current.CPUMs - (double)topLevelNs / 1.0e6;
        if (untracked > 0.0)
            s.Current.CategoryMs[(size_t)Category::Other] += (float)untracked;

        if (GPUAvailable() && s.GPUCurrent.FrameBegin)
        {
            while (!s.GPUStack.empty())
                EndGPUSample();
            s.GPUCurrent.FrameEnd = AcquireQuery();
            if (s.GPUCurrent.FrameEnd)
            {
                glQueryCounter(s.GPUCurrent.FrameEnd, GL_TIMESTAMP);
                s.GPUPending.push_back(std::move(s.GPUCurrent));
            }
            else
            {
                ReleaseFrameQueries(s.GPUCurrent);
            }
            s.GPUCurrent = PendingGPUFrame{};
        }

        s.Frames.push_back(std::move(s.Current));
        while (s.Frames.size() > MaxFrames)
            s.Frames.pop_front();
        s.Current = FrameData{};
    }

    void FrameProfiler::BeginSample(const char* name, Category cat)
    {
        State& s = S();
        if (!s.InFrame || !IsMainThread())
            return;
        Sample smp;
        smp.Name = name ? name : "";
        smp.Cat = cat;
        smp.Depth = (uint16_t)s.Stack.size();
        smp.Parent = s.Stack.empty() ? UINT32_MAX : s.Stack.back().Index;
        const auto now = Clock::now();
        smp.StartNs = NsSince(s.FrameStart, now);
        // Bir karede aşırı örnek (sonsuz döngüdeki betik) belleği şişirmesin
        if (s.Current.Samples.size() >= 200000)
            return;
        s.Current.Samples.push_back(smp);
        s.Stack.push_back({ (uint32_t)(s.Current.Samples.size() - 1), now });
    }

    void FrameProfiler::EndSample()
    {
        State& s = S();
        if (!s.InFrame || !IsMainThread() || s.Stack.empty())
            return;
        auto open = s.Stack.back();
        s.Stack.pop_back();
        s.Current.Samples[open.Index].DurationNs = NsSince(open.Start, Clock::now());
    }

    void FrameProfiler::BeginGPUSample(const char* name)
    {
        State& s = S();
        if (!s.InFrame || !IsMainThread() || !GPUAvailable() || !s.GPUCurrent.FrameBegin)
            return;
        uint32_t q = AcquireQuery();
        if (!q)
            return;
        glQueryCounter(q, GL_TIMESTAMP);
        PendingGPUSample smp{ name ? name : "", (uint16_t)s.GPUStack.size(), q, 0 };
        s.GPUCurrent.Samples.push_back(smp);
        s.GPUStack.push_back({ smp.Name, q });
        s.GPUStackSlot.push_back(s.GPUCurrent.Samples.size() - 1);
    }

    void FrameProfiler::EndGPUSample()
    {
        State& s = S();
        if (!IsMainThread() || s.GPUStack.empty())
            return;
        const size_t slot = s.GPUStackSlot.back();
        s.GPUStack.pop_back();
        s.GPUStackSlot.pop_back();
        if (!GPUAvailable() || slot >= s.GPUCurrent.Samples.size())
            return;
        uint32_t q = AcquireQuery();
        if (!q)
            return;
        glQueryCounter(q, GL_TIMESTAMP);
        s.GPUCurrent.Samples[slot].EndQuery = q;
    }

    void FrameProfiler::SetCounter(Counter c, double value)
    {
        State& s = S();
        if (!s.InFrame || (size_t)c >= (size_t)Counter::Count)
            return;
        s.Current.Counters[(size_t)c] = value;
    }

    bool FrameProfiler::IsRecording()
    {
        return S().Recording;
    }

    void FrameProfiler::SetRecording(bool recording)
    {
        S().Recording = recording;
    }

    void FrameProfiler::Clear()
    {
        State& s = S();
        s.Frames.clear();
    }

    const std::deque<FrameData>& FrameProfiler::GetFrames()
    {
        return S().Frames;
    }

    const FrameData* FrameProfiler::FindFrame(uint64_t index)
    {
        return FindFrameMutable(index);
    }

    uint64_t FrameProfiler::GetCurrentFrameIndex()
    {
        return S().FrameIndex;
    }

    const char* FrameProfiler::Intern(const std::string& name)
    {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        auto& set = S().Interned;
        if (set.size() > 20000)
            return "(too many sample names)";
        return set.insert(name).first->c_str();
    }

    bool FrameProfiler::ExportChromeTrace(const std::filesystem::path& path)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open())
            return false;
        auto escape = [](const char* s) {
            std::string r;
            for (; s && *s; ++s)
            {
                if (*s == '"' || *s == '\\')
                    r += '\\';
                if ((unsigned char)*s < 0x20)
                    continue;
                r += *s;
            }
            return r;
        };
        out << "{\"traceEvents\":[";
        bool first = true;
        double frameStartUs = 0.0;
        char buf[512];
        for (const auto& f : S().Frames)
        {
            auto emit = [&](const char* name, const char* cat, double ts, double dur, int tid) {
                std::snprintf(buf, sizeof(buf), "%s{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%d}",
                              first ? "" : ",", escape(name).c_str(), cat, ts, dur, tid);
                out << buf;
                first = false;
            };
            std::snprintf(buf, sizeof(buf), "Frame %llu", (unsigned long long)f.Index);
            emit(buf, "Frame", frameStartUs, f.CPUMs * 1000.0, 1);
            for (const auto& smp : f.Samples)
                emit(smp.Name, CategoryName(smp.Cat), frameStartUs + (double)smp.StartNs / 1000.0, (double)smp.DurationNs / 1000.0, 1);
            for (const auto& g : f.GPUSamples)
                emit(g.Name, "GPU", frameStartUs + g.StartMs * 1000.0, g.DurationMs * 1000.0, 2);
            frameStartUs += f.CPUMs * 1000.0;
        }
        out << "],\"displayTimeUnit\":\"ms\"}";
        return out.good();
    }

    const char* FrameProfiler::CategoryName(Category c)
    {
        switch (c)
        {
            case Category::Scripts: return "Scripts";
            case Category::Physics: return "Physics";
            case Category::Animation: return "Animation";
            case Category::Rendering: return "Rendering";
            case Category::Particles: return "Particles";
            case Category::Audio: return "Audio";
            case Category::UI: return "UI";
            case Category::AI: return "AI";
            case Category::GC: return "GC";
            case Category::Editor: return "Editor";
            case Category::VSync: return "VSync";
            default: return "Others";
        }
    }

    glm::vec4 FrameProfiler::CategoryColor(Category c)
    {
        // Unity Profiler renk düzenine yakın
        switch (c)
        {
            case Category::Scripts: return { 0.29f, 0.56f, 0.89f, 1.0f };
            case Category::Physics: return { 0.98f, 0.60f, 0.20f, 1.0f };
            case Category::Animation: return { 0.30f, 0.80f, 0.85f, 1.0f };
            case Category::Rendering: return { 0.42f, 0.78f, 0.31f, 1.0f };
            case Category::Particles: return { 0.95f, 0.45f, 0.65f, 1.0f };
            case Category::Audio: return { 0.62f, 0.45f, 0.95f, 1.0f };
            case Category::UI: return { 0.85f, 0.85f, 0.35f, 1.0f };
            case Category::AI: return { 0.55f, 0.75f, 0.55f, 1.0f };
            case Category::GC: return { 0.95f, 0.85f, 0.15f, 1.0f };
            case Category::Editor: return { 0.55f, 0.55f, 0.60f, 1.0f };
            case Category::VSync: return { 0.80f, 0.74f, 0.42f, 1.0f };
            default: return { 0.40f, 0.42f, 0.46f, 1.0f };
        }
    }

    const char* FrameProfiler::CounterName(Counter c)
    {
        switch (c)
        {
            case Counter::DrawCalls: return "Draw Calls";
            case Counter::Triangles: return "Triangles";
            case Counter::Batches2D: return "2D Quads";
            case Counter::Lights: return "Lights";
            case Counter::TextureMB: return "Texture Memory (MB)";
            case Counter::MeshMB: return "Mesh Memory (MB)";
            case Counter::ScriptObjects: return "Script Objects";
            case Counter::Entities: return "Entities";
            case Counter::PhysicsBodies: return "Physics Bodies";
            default: return "?";
        }
    }

    void FrameProfiler::Shutdown()
    {
        State& s = S();
        if (!s.AllQueries.empty() && glDeleteQueries)
            glDeleteQueries((GLsizei)s.AllQueries.size(), s.AllQueries.data());
        s.AllQueries.clear();
        s.FreeQueries.clear();
        s.GPUPending.clear();
        s.GPUCurrent = PendingGPUFrame{};
        s.GPUStack.clear();
        s.GPUStackSlot.clear();
        s.Frames.clear();
        s.GPUChecked = false;
        s.GPUSupported = false;
    }
}
