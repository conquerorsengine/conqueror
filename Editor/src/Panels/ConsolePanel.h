#pragma once

#include <string>
#include <vector>

namespace Conqueror::Editor
{
    enum class ConsoleMessageType
    {
        Info = 0,
        Warning,
        Error
    };

    struct ConsoleMessage
    {
        std::string Message;
        ConsoleMessageType Type;
        int Count = 1;

        ConsoleMessage(const std::string& message, ConsoleMessageType type)
            : Message(message), Type(type) {}
    };

    class ConsolePanel
    {
    public:
        ConsolePanel() = default;
        ~ConsolePanel() = default;

        void OnImGuiRender();
        
        void AddMessage(const std::string& message, ConsoleMessageType type = ConsoleMessageType::Info);
        void AddMessageFromLog(const std::string& message, int level); // spdlog'dan gelen mesajlar için
        void Clear();

    private:
        std::vector<ConsoleMessage> m_Messages;
        bool m_AutoScroll = true;
        bool m_ShowInfo = true;
        bool m_ShowWarnings = true;
        bool m_ShowErrors = true;
    };
}
