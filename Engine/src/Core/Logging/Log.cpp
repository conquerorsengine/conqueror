#include "Log.h"
#include "ConsoleSink.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "Core/Project/Project.h"
#include <nfd.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/resource.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <execinfo.h>
    #include <cxxabi.h>
#endif

namespace Conqueror
{
    thread_local std::vector<std::string> t_ContextStack;
    // -----------------------------------------------------------------------------------------
    // Advanced Diagnostic Globals
    // -----------------------------------------------------------------------------------------
    static std::mutex s_ProfilerMutex;
    static std::vector<LogProfileResult> s_ProfileResults;

    struct DeduplicationEntry {
        std::string LastMessage;
        std::chrono::time_point<std::chrono::system_clock> LastTime;
        int Count = 1;
    };
    static std::mutex s_DeduplicationMutex;
    static std::unordered_map<std::string, DeduplicationEntry> s_DeduplicationCache;

    // -----------------------------------------------------------------------------------------
    // Advanced Ring Buffer Sink - Kayıt geçmişi, bellek durumu, thread isimleri tutar
    // -----------------------------------------------------------------------------------------
    template<typename Mutex>
    class AdvancedRingBufferSink : public spdlog::sinks::base_sink<Mutex>
    {
    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            LogMessage m;
            m.Text = fmt::to_string(msg.payload);
            m.Category = fmt::to_string(msg.logger_name);
            m.Level = static_cast<LogLevel>(msg.level);
            
            std::time_t t = std::chrono::system_clock::to_time_t(msg.time);
            char timeStr[100];
            std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&t));
            m.Time = timeStr;
            
            m.ThreadID = static_cast<uint32_t>(msg.thread_id);
            m.ThreadName = Log::GetThreadName(m.ThreadID);
            m.MemoryUsageMB = Log::GetProcessMemoryUsage() / (1024 * 1024);

            m.ContextStack = Log::GetCurrentContext();
            m.CategoryBitmask = Log::GetConfig().CategoryBitmask;

            if (Log::GetConfig().EnableDeduplication)
            {
                std::lock_guard<std::mutex> dedupLock(s_DeduplicationMutex);
                auto& entry = s_DeduplicationCache[m.Category];
                if (entry.LastMessage == m.Text && (std::chrono::system_clock::now() - entry.LastTime) < std::chrono::milliseconds(2000))
                {
                    entry.Count++;
                    // Lock-free dizide geriye dönük silme/değiştirme çok tehlikelidir, bu yüzden spami yutuyoruz.
                    return; 
                }
                else
                {
                    entry.LastMessage = m.Text;
                    entry.LastTime = std::chrono::system_clock::now();
                    entry.Count = 1;
                }
            }

            // Lock-Free Array Push
            size_t index = Log::s_LogHistoryIndex.fetch_add(1, std::memory_order_relaxed) % Log::MAX_LOG_HISTORY;
            Log::s_LogHistoryArray[index] = m;

            // Black Box (Unbuffered Memory I/O)
            static std::ofstream s_BlackBox;
            static std::once_flag s_BlackBoxFlag;
            std::call_once(s_BlackBoxFlag, [](){
                s_BlackBox.open("conqueror_blackbox.bin", std::ios::binary | std::ios::app);
                if(s_BlackBox.is_open()) s_BlackBox.rdbuf()->pubsetbuf(nullptr, 0); // İşletim sistemine anında flush
            });
            if(s_BlackBox.is_open()) {
                uint8_t level = static_cast<uint8_t>(m.Level);
                s_BlackBox.write(reinterpret_cast<const char*>(&level), sizeof(level));
                size_t len = m.Text.size();
                s_BlackBox.write(reinterpret_cast<const char*>(&len), sizeof(len));
                s_BlackBox.write(m.Text.c_str(), len);
            }

            // UDP Ağ Yayını Açıksa
            Log::InternalBroadcastUDP(m);
        }
        void flush_() override {}
    };

    // -----------------------------------------------------------------------------------------
    // Static Member İlklemeleri
    // -----------------------------------------------------------------------------------------
    Ref<spdlog::logger> Log::s_CoreLogger;
    Ref<spdlog::logger> Log::s_ClientLogger;
    std::shared_ptr<ConsoleSink<std::mutex>> Log::s_ConsoleSink;

    std::unordered_map<std::string, LogCategory> Log::s_Categories;
    std::unordered_map<std::string, Ref<spdlog::logger>> Log::s_Loggers;

    LogConfig Log::s_Config;
    LogLevel Log::s_GlobalLevel = LogLevel::Trace;
    Log::CriticalCallback Log::s_CriticalCallback = nullptr;

    LogMessage Log::s_LogHistoryArray[MAX_LOG_HISTORY];
    std::atomic<size_t> Log::s_LogHistoryIndex{0};

    std::mutex Log::s_MetricsMutex;
    std::unordered_map<std::string, std::shared_ptr<MetricData>> Log::s_Metrics;

    std::mutex Log::s_ThreadNamesMutex;
    std::unordered_map<uint32_t, std::string> Log::s_ThreadNames;

    std::atomic<bool> Log::s_NetworkEnabled{false};
    int Log::s_NetworkPort = 49152;
    int Log::s_NetworkSocket = -1;

    // -----------------------------------------------------------------------------------------
    // Helper Functions
    // -----------------------------------------------------------------------------------------
    static spdlog::level::level_enum GetSpdlogLevel(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace: return spdlog::level::trace;
            case LogLevel::Debug: return spdlog::level::debug;
            case LogLevel::Info: return spdlog::level::info;
            case LogLevel::Warn: return spdlog::level::warn;
            case LogLevel::Error: return spdlog::level::err;
            case LogLevel::Critical: return spdlog::level::critical;
            case LogLevel::Off: return spdlog::level::off;
            default: return spdlog::level::trace;
        }
    }

    static std::string LogLevelToString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO";
            case LogLevel::Warn: return "WARN";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

    // -----------------------------------------------------------------------------------------
    // Kurulum (Initialization)
    // -----------------------------------------------------------------------------------------
    void Log::Init(const LogConfig& config)
    {
        s_Config = config;

        // Kilit gerektiren log sistemi (stdout)
        s_ConsoleSink = std::make_shared<ConsoleSink<std::mutex>>();
            spdlog::init_thread_pool(config.AsyncQueueSize, config.AsyncNumThreads);

        std::vector<spdlog::sink_ptr> logSinks;

        // 1. Console Sink (Renkli, Detaylı)
        if (config.EnableConsole)
        {
            auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            // [Zaman] [Thread] [Logger] Mesaj
            stdout_sink->set_pattern("%^[%T] [T:%t] [%n] %v%$");
            logSinks.push_back(stdout_sink);
        }

        // 2. Rotating File Sink
        if (config.EnableFile)
        {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(config.DefaultLogPath, config.FileMaxSize, config.FileMaxCount, true);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] [%n] %v");
            logSinks.push_back(file_sink);
        }

        // 3. UI/Editor Console Sink
        s_ConsoleSink = std::make_shared<ConsoleSink<std::mutex>>();
        s_ConsoleSink->set_pattern("[%T] %v");
        logSinks.push_back(s_ConsoleSink);

        // 4. Advanced Ring Buffer Sink (Memory & Thread takipli)
        auto ring_buffer_sink = std::make_shared<AdvancedRingBufferSink<std::mutex>>();
        logSinks.push_back(ring_buffer_sink);

        // Logger Nesnelerini Oluşturma
        if (config.UseAsyncLogger)
        {
            s_CoreLogger = std::make_shared<spdlog::async_logger>("CORE", begin(logSinks), end(logSinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            s_ClientLogger = std::make_shared<spdlog::async_logger>("APP", begin(logSinks), end(logSinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        }
        else
        {
            s_CoreLogger = std::make_shared<spdlog::logger>("CORE", begin(logSinks), end(logSinks));
            s_ClientLogger = std::make_shared<spdlog::logger>("APP", begin(logSinks), end(logSinks));
        }

        spdlog::register_logger(s_CoreLogger);
        spdlog::register_logger(s_ClientLogger);

        SetGlobalLevel(LogLevel::Trace);
        SetCurrentThreadName("MainThread");
        
        CQ_CORE_INFO("Advanced Logging System Initialized (Async: {0})", config.UseAsyncLogger);
    }

    void Log::Shutdown()
    {
        DisableNetworkBroadcaster();
        FlushAll();
        spdlog::drop_all();
    }

    void Log::SetConsoleCallback(ConsoleCallback callback)
    {
        if (s_ConsoleSink) s_ConsoleSink->SetCallback(callback);
    }

    void Log::SetCriticalErrorCallback(CriticalCallback callback)
    {
        s_CriticalCallback = callback;
    }

    // -----------------------------------------------------------------------------------------
    // Kategori ve Etiketler
    // -----------------------------------------------------------------------------------------
    void Log::RegisterCategory(const std::string& name, LogLevel level)
    {
        if (s_Categories.find(name) == s_Categories.end())
        {
            s_Categories[name] = { name, level, true, {} };
            CreateLogger(name);
        }
    }

    void Log::SetCategoryLevel(const std::string& name, LogLevel level)
    {
        if (s_Categories.find(name) != s_Categories.end())
        {
            s_Categories[name].Level = level;
            if (s_Loggers.find(name) != s_Loggers.end())
                s_Loggers[name]->set_level(GetSpdlogLevel(level));
        }
    }

    void Log::SetCategoryEnabled(const std::string& name, bool enabled)
    {
        if (s_Categories.find(name) != s_Categories.end())
            s_Categories[name].Enabled = enabled;
    }

    bool Log::IsCategoryEnabled(const std::string& name)
    {
        auto it = s_Categories.find(name);
        return it != s_Categories.end() ? it->second.Enabled : false;
    }

    Ref<spdlog::logger> Log::CreateLogger(const std::string& name, const std::string& filePath)
    {
        if (s_Loggers.find(name) != s_Loggers.end()) return s_Loggers[name];

        std::vector<spdlog::sink_ptr> sinks;
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        std::string path = filePath.empty() ? name + ".log" : filePath;
        sinks.emplace_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, s_Config.FileMaxSize, s_Config.FileMaxCount, true));
        sinks.emplace_back(s_ConsoleSink);
        sinks.emplace_back(std::make_shared<AdvancedRingBufferSink<std::mutex>>());

        sinks[0]->set_pattern("%^[%T] [" + name + "] %v%$");
        sinks[1]->set_pattern("[%T] [%l] [" + name + "] %v");
        sinks[2]->set_pattern("[" + name + "] %v");

        std::shared_ptr<spdlog::logger> logger;
        if (s_Config.UseAsyncLogger)
            logger = std::make_shared<spdlog::async_logger>(name, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        else
            logger = std::make_shared<spdlog::logger>(name, begin(sinks), end(sinks));

        spdlog::register_logger(logger);
        
        LogLevel lvl = s_GlobalLevel;
        if (s_Categories.find(name) != s_Categories.end()) lvl = s_Categories[name].Level;
        logger->set_level(GetSpdlogLevel(lvl));
        logger->flush_on(spdlog::level::trace);
        
        s_Loggers[name] = logger;
        return logger;
    }

    Ref<spdlog::logger> Log::GetLogger(const std::string& name)
    {
        auto it = s_Loggers.find(name);
        if (it != s_Loggers.end()) return it->second;
        return s_CoreLogger; 
    }

    void Log::SetGlobalLevel(LogLevel level)
    {
        s_GlobalLevel = level;
        auto spdLevel = GetSpdlogLevel(level);
        if (s_CoreLogger) s_CoreLogger->set_level(spdLevel);
        if (s_ClientLogger) s_ClientLogger->set_level(spdLevel);
        for (auto& [name, logger] : s_Loggers)
            logger->set_level(spdLevel);
    }

    LogLevel Log::GetGlobalLevel() { return s_GlobalLevel; }

    void Log::FlushAll()
    {
        if (s_CoreLogger) s_CoreLogger->flush();
        if (s_ClientLogger) s_ClientLogger->flush();
        for (auto& [name, logger] : s_Loggers)
            logger->flush();
    }

    void Log::RotateLogFile()
    {
        // Rotating file sink handles this natively based on size.
    }

    // -----------------------------------------------------------------------------------------
    // Structured JSON Logging
    // -----------------------------------------------------------------------------------------
    void Log::LogStructured(const std::string& category, const std::unordered_map<std::string, std::string>& data, LogLevel level)
    {
        if (!IsCategoryEnabled(category)) return;
        
        std::stringstream json;
        json << "{ \"category\": \"" << category << "\", \"data\": { ";
        bool first = true;
        for (const auto& [k, v] : data)
        {
            if (!first) json << ", ";
            json << "\"" << k << "\": \"" << v << "\"";
            first = false;
        }
        json << "} }";

        GetLogger(category)->log(GetSpdlogLevel(level), json.str());
    }

    void Log::LogJSON(const std::string& category, const std::string& jsonString, LogLevel level)
    {
        if (IsCategoryEnabled(category))
            GetLogger(category)->log(GetSpdlogLevel(level), "JSON_PAYLOAD: {}", jsonString);
    }

    // -----------------------------------------------------------------------------------------
    // UDP Network Broadcaster (Live Telemetry)
    // -----------------------------------------------------------------------------------------
    void Log::EnableNetworkBroadcaster(int port)
    {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
        s_NetworkSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        s_NetworkSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
        if (s_NetworkSocket >= 0)
        {
            int broadcastEnable = 1;
            setsockopt(s_NetworkSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcastEnable, sizeof(broadcastEnable));
            s_NetworkPort = port;
            s_NetworkEnabled = true;
            CQ_CORE_INFO("Network Telemetry Broadcaster Enabled on UDP Port: {0}", port);
        }
    }

    void Log::DisableNetworkBroadcaster()
    {
        if (s_NetworkEnabled)
        {
            s_NetworkEnabled = false;
#ifdef _WIN32
            if (s_NetworkSocket >= 0) closesocket(s_NetworkSocket);
            WSACleanup();
#else
            if (s_NetworkSocket >= 0) close(s_NetworkSocket);
#endif
            s_NetworkSocket = -1;
        }
    }

    void Log::InternalBroadcastUDP(const LogMessage& msg)
    {
        if (!s_NetworkEnabled || s_NetworkSocket < 0) return;

        // JSON formatında stream
        std::string payload = fmt::format(
            "{{\"time\":\"{}\", \"level\":\"{}\", \"category\":\"{}\", \"thread\":\"{}\", \"memory_mb\":{}, \"msg\":\"{}\"}}",
            msg.Time, LogLevelToString(msg.Level), msg.Category, msg.ThreadName, msg.MemoryUsageMB, msg.Text
        );

        struct sockaddr_in broadcastAddr;
        memset(&broadcastAddr, 0, sizeof(broadcastAddr));
        broadcastAddr.sin_family = AF_INET;
        broadcastAddr.sin_port = htons(s_NetworkPort);
        
#ifdef _WIN32
        broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(s_NetworkSocket, payload.c_str(), (int)payload.length(), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
#else
        inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr);
        sendto(s_NetworkSocket, payload.c_str(), payload.length(), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
#endif
    }

    // -----------------------------------------------------------------------------------------
    // Metrics & Performance API
    // -----------------------------------------------------------------------------------------
    void Log::MetricIncrement(const std::string& name, double amount)
    {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        if (s_Metrics.find(name) == s_Metrics.end())
            s_Metrics[name] = std::make_shared<MetricData>(MetricType::Counter);
        
        auto& metric = s_Metrics[name];
        metric->Value = metric->Value + amount;
    }

    void Log::MetricSetGauge(const std::string& name, double value)
    {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        if (s_Metrics.find(name) == s_Metrics.end())
            s_Metrics[name] = std::make_shared<MetricData>(MetricType::Gauge);
            
        auto& metric = s_Metrics[name];
        metric->Value = value;
        
        double currentMax = metric->MaxValue.load();
        while(value > currentMax && !metric->MaxValue.compare_exchange_weak(currentMax, value));
        
        double currentMin = metric->MinValue.load();
        while(value < currentMin && !metric->MinValue.compare_exchange_weak(currentMin, value));
    }

    void Log::MetricRecordTime(const std::string& name, double timeMs)
    {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        if (s_Metrics.find(name) == s_Metrics.end())
            s_Metrics[name] = std::make_shared<MetricData>(MetricType::Timer);
            
        auto& metric = s_Metrics[name];
        metric->Value = metric->Value + timeMs;
        metric->Count++;
        
        double currentMax = metric->MaxValue.load();
        while(timeMs > currentMax && !metric->MaxValue.compare_exchange_weak(currentMax, timeMs));
        
        double currentMin = metric->MinValue.load();
        while(timeMs < currentMin && !metric->MinValue.compare_exchange_weak(currentMin, timeMs));
    }

    std::map<std::string, double> Log::GetMetricsSnapshot()
    {
        std::map<std::string, double> snapshot;
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        for (const auto& [name, metric] : s_Metrics)
        {
            if (metric->Type == MetricType::Timer && metric->Count > 0)
                snapshot[name + "_avg_ms"] = metric->Value / metric->Count;
            else
                snapshot[name] = metric->Value;
        }
        return snapshot;
    }

    void Log::DumpMetricsToLog()
    {
        auto snapshot = GetMetricsSnapshot();
        CQ_CORE_INFO("--- Metrics Dump ---");
        for (const auto& [name, val] : snapshot)
        {
            CQ_CORE_INFO("  {0}: {1}", name, val);
        }
    }

    // -----------------------------------------------------------------------------------------
    // Thread Naming & Memory Stats
    // -----------------------------------------------------------------------------------------
    void Log::SetCurrentThreadName(const std::string& name)
    {
        uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::lock_guard<std::mutex> lock(s_ThreadNamesMutex);
        s_ThreadNames[tid] = name;
    }

    std::string Log::GetThreadName(uint32_t threadID)
    {
        std::lock_guard<std::mutex> lock(s_ThreadNamesMutex);
        auto it = s_ThreadNames.find(threadID);
        if (it != s_ThreadNames.end()) return it->second;
        return std::to_string(threadID);
    }

    std::string Log::GetCurrentThreadName()
    {
        uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        return GetThreadName(tid);
    }

    size_t Log::GetProcessMemoryUsage()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
            return pmc.WorkingSetSize;
        return 0;
#else
        // Linux /proc/self/statm
        long rss = 0L;
        FILE* fp = fopen("/proc/self/statm", "r");
        if (fp != nullptr) {
            if (fscanf(fp, "%*s%ld", &rss) == 1) {
                fclose(fp);
                return (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
            }
            fclose(fp);
        }
        return 0;
#endif
    }

    size_t Log::GetSystemTotalMemory()
    {
#ifdef _WIN32
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);
        return status.ullTotalPhys;
#else
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGE_SIZE);
        return pages * page_size;
#endif
    }

    // -----------------------------------------------------------------------------------------
    // Context System
    // -----------------------------------------------------------------------------------------
    void Log::PushContext(const std::string& context) { t_ContextStack.push_back(context); }
    void Log::PopContext() { if(!t_ContextStack.empty()) t_ContextStack.pop_back(); }
    std::string Log::GetCurrentContext() 
    {
        std::string res = "";
        for(size_t i=0; i<t_ContextStack.size(); ++i) {
            res += t_ContextStack[i];
            if(i < t_ContextStack.size()-1) res += " -> ";
        }
        return res;
    }

    // -----------------------------------------------------------------------------------------
    // History & Exporters (HTML & JSON)
    // -----------------------------------------------------------------------------------------
    std::vector<LogMessage> Log::GetLogHistory() 
    { 
        std::vector<LogMessage> history;
        size_t total = s_LogHistoryIndex.load(std::memory_order_acquire);
        size_t count = std::min(total, MAX_LOG_HISTORY);
        history.reserve(count);
        size_t start = total > MAX_LOG_HISTORY ? (total % MAX_LOG_HISTORY) : 0;
        for(size_t i=0; i<count; ++i) {
            history.push_back(s_LogHistoryArray[(start + i) % MAX_LOG_HISTORY]);
        }
        return history; 
    }
    
    void Log::ClearLogHistory() 
    { 
        s_LogHistoryIndex.store(0, std::memory_order_release);
    }

    void Log::ExportHistoryToHtml(const std::string& filepath)
    {
        std::ofstream file(filepath);
        if (!file.is_open()) return;

        file << "<!DOCTYPE html><html><head><title>Conqueror Log Report</title>\n";
        file << "<style>body { font-family: Arial, sans-serif; background-color: #1e1e1e; color: #d4d4d4; } table { width: 100%; border-collapse: collapse; } th, td { padding: 8px; text-align: left; border-bottom: 1px solid #333; } th { background-color: #2d2d30; } .level-TRACE { color: #808080; } .level-INFO { color: #569cd6; } .level-WARN { color: #d7ba7d; font-weight: bold; } .level-ERROR, .level-CRITICAL { color: #f44747; font-weight: bold; }</style>\n";
        file << "</head><body><h2>Engine Log Report</h2><table>\n";
        file << "<tr><th>Time</th><th>Thread</th><th>Memory (MB)</th><th>Category</th><th>Level</th><th>Message</th></tr>\n";

        auto history = GetLogHistory();
        for (const auto& msg : history)
        {
            file << "<tr>"
                 << "<td>" << msg.Time << "</td>"
                 << "<td>" << msg.ThreadName << "</td>"
                 << "<td>" << msg.MemoryUsageMB << "</td>"
                 << "<td>" << msg.Category << "</td>"
                 << "<td class='level-" << LogLevelToString(msg.Level) << "'>" << LogLevelToString(msg.Level) << "</td>"
                 << "<td>" << msg.Text << "</td>"
                 << "</tr>\n";
        }

        file << "</table></body></html>";
        file.close();
        CQ_CORE_INFO("Exported HTML Log to {0}", filepath);
    }

    void Log::ExportHistoryToJson(const std::string& filepath)
    {
        std::ofstream file(filepath);
        if (!file.is_open()) return;

        file << "[\n";
        auto history = GetLogHistory();
        for (size_t i = 0; i < history.size(); ++i)
        {
            const auto& msg = history[i];
            file << "  {\n"
                 << "    \"time\": \"" << msg.Time << "\",\n"
                 << "    \"thread\": \"" << msg.ThreadName << "\",\n"
                 << "    \"memory_mb\": " << msg.MemoryUsageMB << ",\n"
                 << "    \"category\": \"" << msg.Category << "\",\n"
                 << "    \"level\": \"" << LogLevelToString(msg.Level) << "\",\n"
                 << "    \"message\": \"" << msg.Text << "\"\n"
                 << "  }" << (i < history.size() - 1 ? "," : "") << "\n";
        }
        file << "]\n";
        file.close();
        CQ_CORE_INFO("Exported JSON Log to {0}", filepath);
    }

    // -----------------------------------------------------------------------------------------
    // Advanced Crash Dumper & Callstack
    // -----------------------------------------------------------------------------------------
    std::string Log::GetCallstack(int skipFrames)
    {
        std::stringstream ss;
#ifdef _WIN32
        // Windows stack trace implementation placeholder (Requires dbghelp)
        ss << "[Callstack generation requires DbgHelp on Windows. Please check minidump.]";
#else
        void* array[50];
        int size = backtrace(array, 50);
        char** messages = backtrace_symbols(array, size);
        for (int i = skipFrames + 1; i < size && messages != nullptr; ++i)
        {
            std::string symbol = messages[i];
            
            // Extract the mangled name from the symbol string (e.g. ./ConquerorEngine(_ZN9Conqueror...)+0x...)
            size_t openParen = symbol.find('(');
            size_t plus = symbol.find('+', openParen);
            if (openParen != std::string::npos && plus != std::string::npos && plus > openParen + 1)
            {
                std::string mangled = symbol.substr(openParen + 1, plus - openParen - 1);
                int status = 0;
                char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                if (status == 0 && demangled)
                {
                    symbol = symbol.substr(0, openParen + 1) + demangled + symbol.substr(plus);
                    free(demangled);
                }
            }
            ss << "[Frame " << i - skipFrames << "] " << symbol << "\n";
        }
        free(messages);
#endif
        return ss.str();
    }

    void Log::GenerateCrashDump(const std::string& reason)
    {
        FlushAll();
        
        std::string fileName = "crash_";
        std::time_t t = std::time(nullptr);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", std::localtime(&t));
        fileName += timeStr;
        
        std::string finalPath = "";
        std::string dumpDir = s_Config.CrashDumpPath;
        
        // 1. Check if a project is active
        auto activeProject = Project::GetActive();
        if (activeProject)
        {
            std::filesystem::path projPath = Project::GetActiveProjectDirectory();
            if (!projPath.empty())
            {
                dumpDir = (projPath / "crash_dumps").string() + "/";
                if (!std::filesystem::exists(dumpDir))
                    std::filesystem::create_directories(dumpDir);
                finalPath = dumpDir + fileName + ".txt";
            }
        }
        
        // 2. If no project is active, prompt the user using NFD
        if (finalPath.empty())
        {
            NFD_Init();
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filterItem[1] = { { "Crash Dump File", "txt" } };
            std::string defaultName = fileName + ".txt";
            
            nfdresult_t result = NFD_SaveDialog(&outPath, filterItem, 1, nullptr, defaultName.c_str());
            if (result == NFD_OKAY)
            {
                finalPath = outPath;
                NFD_FreePath(outPath);
            }
            else
            {
                // Fallback to engine bin directory if cancelled or error
                if (!std::filesystem::exists(s_Config.CrashDumpPath))
                    std::filesystem::create_directories(s_Config.CrashDumpPath);
                finalPath = s_Config.CrashDumpPath + fileName + ".txt";
                dumpDir = s_Config.CrashDumpPath;
            }
            NFD_Quit();
        }
        
        std::ofstream file(finalPath);
        if (file.is_open())
        {
            file << "=========================================================\n";
            file << "                CONQUEROR ENGINE CRASH DUMP              \n";
            file << "=========================================================\n";
            file << "Reason: " << reason << "\n";
            file << "Time: " << timeStr << "\n";
            file << "Memory Usage: " << (GetProcessMemoryUsage() / (1024*1024)) << " MB\n";
            file << "Total System Memory: " << (GetSystemTotalMemory() / (1024*1024)) << " MB\n";
            file << "Faulting Thread: " << GetCurrentThreadName() << "\n\n";
            
            file << "--- Callstack ---\n";
            file << GetCallstack(1) << "\n";
            
            std::stringstream analysis;
            HeuristicAnalyzeCrash(analysis);
            file << analysis.str();
            
            file << "--- System Metrics ---\n";
            auto metrics = GetMetricsSnapshot();
            for (const auto& [name, val] : metrics)
                file << "  " << name << ": " << val << "\n";
                
            file << "\n--- Recent Log History ---\n";
            auto history = GetLogHistory();
            int startIdx = std::max(0, (int)history.size() - 200);
            for (int i = startIdx; i < history.size(); i++)
            {
                file << "[" << history[i].Time << "] [T:" << history[i].ThreadName 
                     << "] [" << history[i].Category << "] ";
                if(!history[i].ContextStack.empty()) file << "[Context: " << history[i].ContextStack << "] ";
                file << history[i].Text << "\n";
            }
            file.close();
        }
        
        if (s_CriticalCallback) s_CriticalCallback(reason);
        
        // Save the HTML history right next to the crash text file
        std::string htmlPath = finalPath;
        if (htmlPath.length() > 4) {
            htmlPath = htmlPath.substr(0, htmlPath.length() - 4) + "_history.html";
            ExportHistoryToHtml(htmlPath);
        }
        
        // Webhook and Profiler output
        SendWebhookAlert("CRITICAL CRASH DETECTED: " + reason);
        std::string profilePath = finalPath;
        if (profilePath.length() > 4) {
            profilePath = profilePath.substr(0, profilePath.length() - 4) + "_profile.json";
            ExportProfilerSession(profilePath);
        }

        FlushAll();
    }

    // -----------------------------------------------------------------------------------------
    // Scoped Timer Implementation
    // -----------------------------------------------------------------------------------------
    ScopedLogTimer::ScopedLogTimer(const std::string& name, const std::string& category)
        : m_Name(name), m_Category(category), m_Start(std::chrono::high_resolution_clock::now())
    {
    }

    ScopedLogTimer::~ScopedLogTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = end - m_Start;
        
        auto startUs = std::chrono::time_point_cast<std::chrono::microseconds>(m_Start).time_since_epoch().count();
        auto endUs = std::chrono::time_point_cast<std::chrono::microseconds>(end).time_since_epoch().count();
        uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        Log::AddProfileResult({m_Name, startUs, endUs, tid, m_Category});
        
        if (Log::IsCategoryEnabled(m_Category))
        {
            auto logger = Log::GetLogger(m_Category);
            logger->trace("[Timer] Scope '{0}' completed in {1:.3f} ms", m_Name, ms.count());
        }
        
        Log::MetricRecordTime("Scope_" + m_Name, ms.count());
    }
    
    // -----------------------------------------------------------------------------------------
    // Advanced Diagnostics Implementation
    // -----------------------------------------------------------------------------------------
    
    void Log::AddProfileResult(const LogProfileResult& result)
    {
        if (!s_Config.EnableChromeTracing) return;
        std::lock_guard<std::mutex> lock(s_ProfilerMutex);
        s_ProfileResults.push_back(result);
    }

    void Log::ExportProfilerSession(const std::string& filepath)
    {
        std::ofstream file(filepath);
        if (!file.is_open()) return;
        
        file << "{\"otherData\": {},\"traceEvents\":[";
        std::lock_guard<std::mutex> lock(s_ProfilerMutex);
        for (size_t i = 0; i < s_ProfileResults.size(); ++i)
        {
            const auto& result = s_ProfileResults[i];
            file << "{";
            file << "\"cat\":\"" << result.Category << "\",";
            file << "\"dur\":" << (result.End - result.Start) << ",";
            file << "\"name\":\"" << result.Name << "\",";
            file << "\"ph\":\"X\",";
            file << "\"pid\":0,";
            file << "\"tid\":" << result.ThreadID << ",";
            file << "\"ts\":" << result.Start;
            file << "}";
            if (i < s_ProfileResults.size() - 1)
                file << ",";
        }
        file << "]}";
        file.close();
    }

    void Log::HeuristicAnalyzeCrash(std::stringstream& outStream)
    {
        outStream << "--- Heuristic Crash Analysis ---\n";
        int errorCount = 0;
        int warnCount = 0;
        std::string probableCause = "Unknown (Sudden fatal signal or manual trigger)";

        auto history = GetLogHistory();
        int startIdx = std::max(0, (int)history.size() - 50);
        for (int i = startIdx; i < history.size(); i++)
        {
            if (history[i].Level == LogLevel::Error || history[i].Level == LogLevel::Critical) {
                errorCount++;
                probableCause = history[i].Text;
            }
            if (history[i].Level == LogLevel::Warn) warnCount++;
            
            std::string lowerText = history[i].Text;
            std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
            if (lowerText.find("null") != std::string::npos || lowerText.find("failed") != std::string::npos || lowerText.find("exception") != std::string::npos)
            {
                probableCause = history[i].Text;
            }
        }
        
        outStream << "Warnings in last context: " << warnCount << "\n";
        outStream << "Errors in last context: " << errorCount << "\n";
        outStream << "Probable Cause: " << probableCause << "\n\n";
    }

    void Log::SendWebhookAlert(const std::string& message)
    {
        if (s_Config.DiscordWebhookURL.empty()) return;
        std::string url = s_Config.DiscordWebhookURL;
        std::string safeMessage = message; 
        
        std::thread([url, safeMessage]() {
            std::string payload = "{\"content\": \"**[Conqueror Engine Alert]** " + safeMessage + "\"}";
            std::string curlCmd = "curl -s -H \"Content-Type: application/json\" -X POST -d '" + payload + "' " + url + " > /dev/null 2>&1";
            std::system(curlCmd.c_str());
        }).detach();
    }
}
