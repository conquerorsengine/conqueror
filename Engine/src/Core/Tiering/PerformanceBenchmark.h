#pragma once

#include "Core/Base/Base.h"
#include "QualitySettings.h"
#include <functional>

namespace Conqueror
{
    // Performans benchmark ve otomatik quality ayarlama
    class CQ_API PerformanceBenchmark
    {
    public:
        struct BenchmarkResult
        {
            float AverageFPS = 0.0f;
            float MinFPS = 0.0f;
            float MaxFPS = 0.0f;
            float AverageFrameTime = 0.0f; // ms
            
            float CPUTime = 0.0f; // ms
            float GPUTime = 0.0f; // ms
            
            uint64_t MemoryUsed = 0; // MB
            uint64_t VRAMUsed = 0;   // MB
            
            QualityLevel RecommendedQuality = QualityLevel::Medium;
            bool Passed = false;
        };

        static void Init();
        
        // Benchmark çalıştır (duration saniye boyunca)
        static BenchmarkResult RunBenchmark(float duration = 5.0f);
        
        // Belirli bir quality level'da benchmark
        static BenchmarkResult RunBenchmarkAtQuality(QualityLevel level, float duration = 3.0f);
        
        // Otomatik quality level bul (target FPS'e göre)
        static QualityLevel FindOptimalQuality(float targetFPS = 60.0f);
        
        // Custom benchmark (kullanıcı tanımlı scene)
        static BenchmarkResult RunCustomBenchmark(
            std::function<void()> setupFunc,
            std::function<void(float)> updateFunc,
            float duration = 5.0f
        );
        
        // Son benchmark sonucu
        static const BenchmarkResult& GetLastResult() { return s_LastResult; }
        
    private:
        static BenchmarkResult s_LastResult;
        
        static void MeasureFrameTime(BenchmarkResult& result, float duration);
        static void MeasureMemoryUsage(BenchmarkResult& result);
    };
}
