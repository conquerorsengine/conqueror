#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace Conqueror::Profiling
{
    // Unity Profiler'daki CPU kategorileri (grafikte yığılmış alanlar)
    enum class Category : uint8_t
    {
        Other = 0,
        Scripts,
        Physics,
        Animation,
        Rendering,
        Particles,
        Audio,
        UI,
        AI,
        GC,
        Editor,
        VSync,
        Count
    };

    struct Sample
    {
        const char* Name = "";          // kalıcı (string literal ya da Intern)
        uint64_t StartNs = 0;           // kare başından itibaren
        uint64_t DurationNs = 0;
        uint32_t Parent = UINT32_MAX;   // aynı karedeki üst örnek
        uint16_t Depth = 0;
        Category Cat = Category::Other;
    };

    struct GPUSample
    {
        const char* Name = "";
        double StartMs = 0.0;           // karenin ilk GPU damgasından itibaren
        double DurationMs = 0.0;
        uint16_t Depth = 0;
    };

    // Kare başına sayaçlar (grafikler)
    enum class Counter : uint8_t
    {
        DrawCalls = 0,
        Triangles,
        Batches2D,
        Lights,
        TextureMB,
        MeshMB,
        ScriptObjects,
        Entities,
        PhysicsBodies,
        Count
    };

    struct FrameData
    {
        uint64_t Index = 0;
        double CPUMs = 0.0;
        double GPUMs = -1.0;            // < 0: henüz çözülmedi / desteklenmiyor
        float CategoryMs[(size_t)Category::Count] = {};   // kategori başına öz (self) süre
        double Counters[(size_t)Counter::Count] = {};
        std::vector<Sample> Samples;
        std::vector<GPUSample> GPUSamples;
    };

    // Ana iş parçacığı kare profilleyicisi. Kayıt kapalıyken örnek maliyeti tek bir bool denetimidir.
    class CQ_API FrameProfiler
    {
    public:
        static constexpr size_t MaxFrames = 300;

        static void BeginFrame();
        static void EndFrame();

        static void BeginSample(const char* name, Category cat = Category::Other);
        static void EndSample();

        // GPU zaman damgaları (GL_TIMESTAMP; iç içe geçebilir, sonuçlar 2-3 kare sonra çözülür)
        static void BeginGPUSample(const char* name);
        static void EndGPUSample();

        static void SetCounter(Counter c, double value);

        static bool IsRecording();
        static void SetRecording(bool recording);
        static void Clear();

        static const std::deque<FrameData>& GetFrames();
        static const FrameData* FindFrame(uint64_t index);
        static uint64_t GetCurrentFrameIndex();

        // Betik örnek adları gibi geçici dizgiler için kalıcı kopya
        static const char* Intern(const std::string& name);

        // chrome://tracing / Perfetto JSON
        static bool ExportChromeTrace(const std::filesystem::path& path);

        static const char* CategoryName(Category c);
        static glm::vec4 CategoryColor(Category c);
        static const char* CounterName(Counter c);

        // Kapanışta GPU sorgularını sil (GL bağlamı hâlâ geçerliyken)
        static void Shutdown();
    };

    class ScopedSample
    {
    public:
        ScopedSample(const char* name, Category cat) : m_Active(FrameProfiler::IsRecording())
        {
            if (m_Active)
                FrameProfiler::BeginSample(name, cat);
        }
        ~ScopedSample()
        {
            if (m_Active)
                FrameProfiler::EndSample();
        }
        ScopedSample(const ScopedSample&) = delete;
        ScopedSample& operator=(const ScopedSample&) = delete;

    private:
        bool m_Active;
    };

    class ScopedGPUSample
    {
    public:
        explicit ScopedGPUSample(const char* name) : m_Active(FrameProfiler::IsRecording())
        {
            if (m_Active)
                FrameProfiler::BeginGPUSample(name);
        }
        ~ScopedGPUSample()
        {
            if (m_Active)
                FrameProfiler::EndGPUSample();
        }
        ScopedGPUSample(const ScopedGPUSample&) = delete;
        ScopedGPUSample& operator=(const ScopedGPUSample&) = delete;

    private:
        bool m_Active;
    };
}

#define CQ_PROFILE_CONCAT_INNER(a, b) a##b
#define CQ_PROFILE_CONCAT(a, b) CQ_PROFILE_CONCAT_INNER(a, b)
// Örnek: CQ_PROFILE_SAMPLE("Physics.Step", Physics);
#define CQ_PROFILE_SAMPLE(name, cat) \
    ::Conqueror::Profiling::ScopedSample CQ_PROFILE_CONCAT(cqProfileSample_, __LINE__)(name, ::Conqueror::Profiling::Category::cat)
#define CQ_PROFILE_GPU(name) \
    ::Conqueror::Profiling::ScopedGPUSample CQ_PROFILE_CONCAT(cqProfileGPU_, __LINE__)(name)
