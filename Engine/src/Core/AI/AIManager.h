#pragma once

#include "Core/Base/Base.h"
#include "AIProvider.h"
#include "AIAuth.h"
#include "Providers/AntigravityProvider.h"
#include "Providers/CopilotProvider.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::AI
{
    class CQ_API AIManager
    {
    public:
        static AIManager& Get();

        void Init();
        void Shutdown();

        AIProvider* GetProvider() { return m_Provider.get(); }
        bool IsLoggedIn() const;
        void Login(const std::string& apiKey);
        void Login(const std::string& apiKey, const std::string& providerName);
        void Logout();
        void StartOAuthServer();
        void StartGitHubOAuthServer();

        void SendUserMessage(Scene* activeScene, const std::string& message, std::function<void(const std::string& response)> onComplete);
        void Update();
        
        const std::vector<ChatMessage>& GetHistory() const { return m_History; }
        void ClearHistory();

        const std::vector<std::string>& GetToolLogs() const { return m_ToolLogs; }
        const std::string& GetLastAuthMessage() const { return m_LastAuthMessage; }

    private:
        AIManager() = default;
        ~AIManager() = default;

        std::unique_ptr<AIProvider> m_Provider;
        std::vector<ChatMessage> m_History;
        std::vector<std::string> m_ToolLogs;
        std::string m_LastAuthMessage;
        bool m_Initialized = false;

        std::mutex m_QueueMutex;
        std::queue<std::function<void()>> m_MainThreadQueue;
    };
}
