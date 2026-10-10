#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <vector>
#include <functional>

namespace Conqueror::AI
{
    struct ChatMessage
    {
        std::string Role;    // "system", "user", "assistant", "tool"
        std::string Content;
        std::string DisplayContent;
        std::string ToolCallId;
        std::string Name;
    };

    struct ToolParameter
    {
        std::string Name;
        std::string Type; // "string", "number", "boolean", "object"
        std::string Description;
        bool Required = false;
    };

    struct ToolDefinition
    {
        std::string Name;
        std::string Description;
        std::vector<ToolParameter> Parameters;
    };

    using StreamingCallback = std::function<void(const std::string& chunk)>;

    class CQ_API AIProvider
    {
    public:
        virtual ~AIProvider() = default;

        virtual bool Authenticate(const std::string& apiKey) = 0;
        virtual bool IsAuthenticated() const = 0;
        virtual std::string GetApiKey() const = 0;

        virtual std::string SendChat(const std::vector<ChatMessage>& messages,
                                     const std::vector<ToolDefinition>& tools,
                                     StreamingCallback streamCb = nullptr) = 0;

        virtual std::string GetProviderName() const = 0;
        virtual std::vector<std::string> GetAvailableModels() const = 0;
        virtual void SetSelectedModel(const std::string& model) = 0;
        virtual std::string GetSelectedModel() const = 0;
        virtual void Logout() = 0;
    };
}
