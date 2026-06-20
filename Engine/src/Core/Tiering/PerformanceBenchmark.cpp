#include "PerformanceBenchmark.h"
#include "Core/Logging/Log.h"
#include "Core/Time/Clock.h"
#include <thread>
#include <algorithm>

namespace Conqueror
{
    PerformanceBenchmark::BenchmarkResult PerformanceBenchmark::s_LastResult = {};

    void PerformanceBenchmark::Init()
    {
        CQ_CORE_INFO("PerformanceBenchmark initialized");
    }

    PerformanceBenchmark::BenchmarkResult PerformanceBenchmark::RunBenchmark(float duration)
    {
        CQ_CORE_INFO("Running performance benchmark ({0}s)...", duration);
        
        BenchmarkResult result;
        MeasureFrameTime(result, duration);
        MeasureMemoryUsage(result);
        
        // Recommended quality belirle
        if (result.AverageFPS >= 60.0f)
            result.RecommendedQuality = QualityLevel::Ultra;
        else if (result.AverageFPS >= 45.0f)
            result.RecommendedQuality = QualityLevel::High;
        else if (result.AverageFPS >= 30.0f)
            result.RecommendedQuality = QualityLevel::Medium;
        else
            result.RecommendedQuality = QualityLevel::Low;
        
        result.Passed = result.AverageFPS >= 30.0f;
        
        s_LastResult = result;
        
        CQ_CORE_INFO("Benchmark complete:");
        CQ_CORE_INFO("  Average FPS: {0}", result.AverageFPS);
        CQ_CORE_INFO("  Min FPS: {0}", result.MinFPS);
        CQ_CORE_INFO("  Max FPS: {0}", result.MaxFPS);
        CQ_CORE_INFO("  Recommended Quality: {0}", (int)result.RecommendedQuality);
        
        return result;
    }

    PerformanceBenchmark::BenchmarkResult PerformanceBenchmark::RunBenchmarkAtQuality(QualityLevel level, float duration)
    {
        QualityLevel originalLevel = QualitySettings::GetQualityLevel();
        QualitySettings::SetQualityLevel(level);
        
        BenchmarkResult result = RunBenchmark(duration);
        
        QualitySettings::SetQualityLevel(originalLevel);
        return result;
    }

    QualityLevel PerformanceBenchmark::FindOptimalQuality(float targetFPS)
    {
        CQ_CORE_INFO("Finding optimal quality for target FPS: {0}", targetFPS);
        
        // Her quality level'da kısa benchmark
        std::vector<std::pair<QualityLevel, float>> results;
        
        for (int i = 0; i < 4; ++i)
        {
            QualityLevel level = static_cast<QualityLevel>(i);
            auto result = RunBenchmarkAtQuality(level, 2.0f);
            results.push_back({level, result.AverageFPS});
        }
        
        // Target FPS'e en yakın olanı bul
        QualityLevel optimal = QualityLevel::Medium;
        float minDiff = 1000.0f;
        
        for (const auto& [level, fps] : results)
        {
            float diff = std::abs(fps - targetFPS);
            if (diff < minDiff && fps >= targetFPS * 0.9f) // %90 tolerance
            {
                minDiff = diff;
                optimal = level;
            }
        }
        
        CQ_CORE_INFO("Optimal quality: {0}", (int)optimal);
        return optimal;
    }

    PerformanceBenchmark::BenchmarkResult PerformanceBenchmark::RunCustomBenchmark(
        std::function<void()> setupFunc,
        std::function<void(float)> updateFunc,
        float duration)
    {
        BenchmarkResult result;
        
        if (setupFunc)
            setupFunc();
        
        Clock clock;
        clock.Start();
        
        std::vector<float> frameTimes;
        float elapsed = 0.0f;
        
        while (elapsed < duration)
        {
            auto frameStart = std::chrono::high_resolution_clock::now();
            
            if (updateFunc)
                updateFunc(0.016f); // Assume 60 FPS
            
            auto frameEnd = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> frameTime = frameEnd - frameStart;
            
            frameTimes.push_back(frameTime.count());
            elapsed = clock.GetElapsedSeconds();
        }
        
        // Calculate stats
        if (!frameTimes.empty())
        {
            float sum = 0.0f;
            result.MinFPS = 1000.0f;
            result.MaxFPS = 0.0f;
            
            for (float ft : frameTimes)
            {
                sum += ft;
                float fps = 1.0f / ft;
                result.MinFPS = std::min(result.MinFPS, fps);
                result.MaxFPS = std::max(result.MaxFPS, fps);
            }
            
            result.AverageFrameTime = (sum / frameTimes.size()) * 1000.0f; // ms
            result.AverageFPS = 1.0f / (sum / frameTimes.size());
        }
        
        MeasureMemoryUsage(result);
        
        s_LastResult = result;
        return result;
    }

    void PerformanceBenchmark::MeasureFrameTime(BenchmarkResult& result, float duration)
    {
        // Placeholder - gerçek implementasyon render loop'a entegre edilmeli
        result.AverageFPS = 60.0f;
        result.MinFPS = 55.0f;
        result.MaxFPS = 65.0f;
        result.AverageFrameTime = 16.67f;
    }

    void PerformanceBenchmark::MeasureMemoryUsage(BenchmarkResult& result)
    {
        // Placeholder - gerçek memory tracking gerekli
        result.MemoryUsed = 512; // MB
        result.VRAMUsed = 256;   // MB
    }
}
