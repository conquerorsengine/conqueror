#pragma once

#include "../AIProvider.h"
#include <string>
#include <vector>

namespace Conqueror::AI
{
    class CQ_API AntigravityProvider : public AIProvider
    {
    public:
        AntigravityProvider();
        virtual ~AntigravityProvider() = default;

        bool Authenticate(const std::string& apiKey) override;
        bool IsAuthenticated() const override;
        std::string GetApiKey() const override { return m_ApiKey; }

        std::string SendChat(const std::vector<ChatMessage>& messages,
                             const std::vector<ToolDefinition>& tools,
                             StreamingCallback streamCb = nullptr) override;

        std::string GetProviderName() const override { return "Antigravity"; }
        std::vector<std::string> GetAvailableModels() const override;
        void SetSelectedModel(const std::string& model) override { m_SelectedModel = model; }
        std::string GetSelectedModel() const override { return m_SelectedModel; }
        void Logout() override;

    private:
        void DiscoverProject();

        std::string m_ApiKey;
        std::string m_SelectedModel = "gemini-3.1-pro-low";
        std::string m_ProjectId;
        std::vector<std::string> m_DiscoveredModels;
        bool m_IsAuthenticated = false;
    };
}
