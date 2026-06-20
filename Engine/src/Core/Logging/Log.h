#pragma once

#include "Core/Base/Base.h"

#ifdef _MSC_VER
    #pragma warning(push, 0)
#endif
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/async.h>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <map>

namespace Conqueror
{
    template<typename Mutex>
    class ConsoleSink;

    enum class LogLevel
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

    using LogTag = std::string;

    struct LogCategory
    {
        std::string Name;
        LogLevel Level;
        bool Enabled;
        std::vector<LogTag> DefaultTags;
    };

    struct LogMessage
    {
        std::string Text;
        LogLevel Level;
        std::string Category;
        std::string Time;
        
        // Gelişmiş Telemetri Verileri
        uint32_t ThreadID;
        std::string ThreadName;
        size_t MemoryUsageMB;
        std::vector<LogTag> Tags;
        
        // Olay Bağlamı (Context) ve Routing (Yönlendirme)
        std::string ContextStack;
        uint64_t CategoryBitmask;
    };

    struct LogConfig
    {
        bool UseAsyncLogger = true;        // Performans için asenkron loglama
        size_t AsyncQueueSize = 8192;      // Asenkron kuyruk boyutu
        size_t AsyncNumThreads = 1;        // Arka plan log thread sayısı

        bool EnableConsole = true;
        bool EnableFile = true;
        bool EnableHtmlExport = true;
        bool EnableJsonExport = true;

        size_t FileMaxSize = 1048576 * 10; // 10 MB limit
        size_t FileMaxCount = 5;           // Rotasyon limiti
        
        size_t RingBufferSize = 2500;      // Oyun içi konsol için history
        
        std::string DefaultLogPath = "Conqueror.log";
        std::string CrashDumpPath = "crash_dumps/";

        // Advanced Features
        bool EnableChromeTracing = true;     // Chrome/Perfetto Profiler JSON export
        bool EnableDeduplication = true;     // Anti-spam log throttling
        std::string DiscordWebhookURL = "";  // Remote alert system via Webhooks
        
        uint64_t CategoryBitmask = 0xFFFFFFFFFFFFFFFF; // Category Routing
    };

    struct LogProfileResult
    {
        std::string Name;
        long long Start, End;
        uint32_t ThreadID;
        std::string Category;
    };

    enum class MetricType { Counter, Gauge, Timer };

    struct MetricData
    {
        MetricType Type;
        std::atomic<double> Value{0.0};
        std::atomic<uint64_t> Count{0}; 
        std::atomic<double> MinValue{9999999.0};
        std::atomic<double> MaxValue{-9999999.0};

        MetricData(MetricType type) : Type(type) {}
    };

    class CQ_API Log
    {
    public:
        using ConsoleCallback = std::function<void(const std::string&, int)>;
        using CriticalCallback = std::function<void(const std::string&)>;

        static void Init(const LogConfig& config = LogConfig());
        static void Shutdown();

        static const LogConfig& GetConfig() { return s_Config; }

        static void SetConsoleCallback(ConsoleCallback callback);
        static void SetCriticalErrorCallback(CriticalCallback callback);

        static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

        // Kategori ve Etiket (Tag) Sistemi
        static void RegisterCategory(const std::string& name, LogLevel level = LogLevel::Info);
        static void SetCategoryLevel(const std::string& name, LogLevel level);
        static void SetCategoryEnabled(const std::string& name, bool enabled);
        static bool IsCategoryEnabled(const std::string& name);

        static Ref<spdlog::logger> CreateLogger(const std::string& name, const std::string& filePath = "");
        static Ref<spdlog::logger> GetLogger(const std::string& name);

        static void SetGlobalLevel(LogLevel level);
        static LogLevel GetGlobalLevel();

        static void FlushAll();
        static void RotateLogFile();

        // Olay Bağlamı (Context Yığıtı)
        static void PushContext(const std::string& context);
        static void PopContext();
        static std::string GetCurrentContext();

        // Yapılandırılmış (Structured) Loglama
        static void LogStructured(const std::string& category, const std::unordered_map<std::string, std::string>& data, LogLevel level = LogLevel::Info);
        static void LogJSON(const std::string& category, const std::string& jsonString, LogLevel level = LogLevel::Info);

        // Ağ üzerinden Telemetri (Canlı Log Akışı)
        static void EnableNetworkBroadcaster(int port = 49152);
        static void DisableNetworkBroadcaster();
        static void InternalBroadcastUDP(const LogMessage& msg);

        // Metrik ve Performans Analizi (Grafana/Prometheus uyumlu altyapı)
        static void MetricIncrement(const std::string& name, double amount = 1.0);
        static void MetricSetGauge(const std::string& name, double value);
        static void MetricRecordTime(const std::string& name, double timeMs);
        static std::map<std::string, double> GetMetricsSnapshot();
        static void DumpMetricsToLog();

        // Thread ve Bellek Context'leri
        static void SetCurrentThreadName(const std::string& name);
        static std::string GetThreadName(uint32_t threadID);
        static std::string GetCurrentThreadName();
        static size_t GetProcessMemoryUsage();
        static size_t GetSystemTotalMemory();

        // Oyun İçi Geçmiş ve Dışa Aktarma
        static std::vector<LogMessage> GetLogHistory(); // Returned by value for thread-safety
        static void ClearLogHistory();
        static void ExportHistoryToHtml(const std::string& filepath = "LogExport.html");
        static void ExportHistoryToJson(const std::string& filepath = "LogExport.json");

        // Crash (Çökme) Raporlama - Minidump altyapısı
        static std::string GetCallstack(int skipFrames = 1);
        static void GenerateCrashDump(const std::string& reason);

        // Advanced Diagnostics
        static void ExportProfilerSession(const std::string& filepath);
        static void AddProfileResult(const LogProfileResult& result);
        static void HeuristicAnalyzeCrash(std::stringstream& outStream);
        static void SendWebhookAlert(const std::string& message);

    public:
        // Sink'ler tarafından erişilmesi için
        static constexpr size_t MAX_LOG_HISTORY = 4096;
        static LogMessage s_LogHistoryArray[MAX_LOG_HISTORY];
        static std::atomic<size_t> s_LogHistoryIndex;

    private:
        static Ref<spdlog::logger> s_CoreLogger;
        static Ref<spdlog::logger> s_ClientLogger;
        static std::shared_ptr<ConsoleSink<std::mutex>> s_ConsoleSink;
        
        static std::unordered_map<std::string, LogCategory> s_Categories;
        static std::unordered_map<std::string, Ref<spdlog::logger>> s_Loggers;
        
        static LogConfig s_Config;
        static LogLevel s_GlobalLevel;
        static CriticalCallback s_CriticalCallback;

        // Metrikler
        static std::mutex s_MetricsMutex;
        static std::unordered_map<std::string, std::shared_ptr<MetricData>> s_Metrics;

        // Thread İsimlendirmesi
        static std::mutex s_ThreadNamesMutex;
        static std::unordered_map<uint32_t, std::string> s_ThreadNames;

        // Ağ Soket Verileri
        static std::atomic<bool> s_NetworkEnabled;
        static int s_NetworkPort;
        static int s_NetworkSocket;
    };

    // Belirli bir scope'un çalışma süresini otomatik ölçen ve loglayan sınıf
    class CQ_API ScopedLogTimer
    {
    public:
        ScopedLogTimer(const std::string& name, const std::string& category = "CORE");
        ~ScopedLogTimer();
    private:
        std::string m_Name;
        std::string m_Category;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    };
}

// Standart Core Makroları
#define CQ_CORE_TRACE(...)    ::Conqueror::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define CQ_CORE_DEBUG(...)    ::Conqueror::Log::GetCoreLogger()->debug(__VA_ARGS__)
#define CQ_CORE_INFO(...)     ::Conqueror::Log::GetCoreLogger()->info(__VA_ARGS__)
#define CQ_CORE_WARN(...)     ::Conqueror::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define CQ_CORE_ERROR(...)    ::Conqueror::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CQ_CORE_CRITICAL(...) do { ::Conqueror::Log::GetCoreLogger()->critical(__VA_ARGS__); ::Conqueror::Log::GenerateCrashDump("Critical Error - Check Log History"); } while(0)

// Standart Client Makroları
#define CQ_TRACE(...)         ::Conqueror::Log::GetClientLogger()->trace(__VA_ARGS__)
#define CQ_DEBUG(...)         ::Conqueror::Log::GetClientLogger()->debug(__VA_ARGS__)
#define CQ_INFO(...)          ::Conqueror::Log::GetClientLogger()->info(__VA_ARGS__)
#define CQ_WARN(...)          ::Conqueror::Log::GetClientLogger()->warn(__VA_ARGS__)
#define CQ_ERROR(...)         ::Conqueror::Log::GetClientLogger()->error(__VA_ARGS__)
#define CQ_CRITICAL(...)      do { ::Conqueror::Log::GetClientLogger()->critical(__VA_ARGS__); ::Conqueror::Log::GenerateCrashDump("Critical Error - Check Log History"); } while(0)

// Kategori Makroları
#define CQ_LOG_CATEGORY(category, level, ...) \
    do { \
        if (::Conqueror::Log::IsCategoryEnabled(category)) { \
            auto logger = ::Conqueror::Log::GetLogger(category); \
            if (logger) logger->log(spdlog::level::level, __VA_ARGS__); \
        } \
    } while(0)

// Performans Analiz Makroları
#define CQ_LOG_TIME_SCOPE(name) ::Conqueror::ScopedLogTimer timer_##__LINE__(name)
#define CQ_LOG_TIME_SCOPE_CAT(name, cat) ::Conqueror::ScopedLogTimer timer_##__LINE__(name, cat)

// Telemetri/Metrik Makroları
#define CQ_METRIC_INC(name) ::Conqueror::Log::MetricIncrement(name)
#define CQ_METRIC_INC_VAL(name, val) ::Conqueror::Log::MetricIncrement(name, val)
#define CQ_METRIC_GAUGE(name, val) ::Conqueror::Log::MetricSetGauge(name, val)
