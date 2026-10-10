#pragma once

#include "../AIProvider.h"
#include <string>
#include <vector>

namespace Conqueror::AI
{
    class CQ_API CopilotProvider : public AIProvider
    {
    public:
        CopilotProvider() = default;
        ~CopilotProvider() override = default;

        bool Authenticate(const std::string& apiKey) override;
        bool IsAuthenticated() const override;
        std::string GetApiKey() const override { return m_ApiKey; }

        std::string SendChat(const std::vector<ChatMessage>& messages,
                             const std::vector<ToolDefinition>& tools,
                             StreamingCallback streamCb = nullptr) override;

        std::string GetProviderName() const override { return "GitHub Copilot"; }
        std::vector<std::string> GetAvailableModels() const override;
        void SetSelectedModel(const std::string& model) override { m_SelectedModel = model; }
        std::string GetSelectedModel() const override { return m_SelectedModel; }
        void Logout() override;

    private:
        void DiscoverModels();

        std::string m_ApiKey;
        std::string m_SelectedModel;
        std::vector<std::string> m_DiscoveredModels;
        bool m_IsAuthenticated = false;
        bool m_ModelDiscoveryAttempted = false;
    };
}
