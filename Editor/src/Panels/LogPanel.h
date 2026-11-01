#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    enum class LogSeverity : int
    {
        Trace = 0,
        Info,
        Warning,
        Error
    };

    struct LogEntry
    {
        std::string Time;
        std::string Source;     // CORE / APP / ...
        std::string Text;
        LogSeverity Severity = LogSeverity::Info;
        int Count = 1;
    };

    // Unity Console benzeri log görüntüleyici. AddMessageFromLog herhangi bir
    // thread'den çağrılabilir (asenkron spdlog thread'i); mesajlar kilitli bir
    // kuyrukta bekler ve UI thread'inde boşaltılır.
    class LogPanel
    {
    public:
        LogPanel() = default;
        ~LogPanel() = default;

        void OnImGuiRender(bool* open = nullptr);

        void AddMessageFromLog(const std::string& formatted, int spdlogLevel);
        void AddMessage(const std::string& text, LogSeverity severity, const std::string& source = "EDITOR");
        void Clear();

        // Play moduna girerken çağrılır
        void OnPlay();
        bool ConsumeErrorPauseRequest();

        int GetErrorCount() const { return m_ErrorCount; }
        int GetWarningCount() const { return m_WarningCount; }

    private:
        void DrainPending();
        bool PassesFilter(const LogEntry& entry) const;
        void SaveToFile();
        std::string BuildClipboardText(bool filteredOnly) const;

        std::mutex m_PendingMutex;
        std::vector<LogEntry> m_Pending;

        std::deque<LogEntry> m_Entries;
        static constexpr size_t kMaxEntries = 20000;

        int m_InfoCount = 0;
        int m_WarningCount = 0;
        int m_ErrorCount = 0;

        bool m_ShowTrace = false;
        bool m_ShowInfo = true;
        bool m_ShowWarnings = true;
        bool m_ShowErrors = true;
        bool m_Collapse = false;
        bool m_ClearOnPlay = true;
        bool m_ErrorPause = false;
        bool m_AutoScroll = true;
        bool m_ShowTimestamps = true;
        bool m_ErrorPauseRequested = false;
        char m_Search[128] = {};
        int m_Selected = -1;
        float m_DetailHeight = 90.0f;
    };
}
