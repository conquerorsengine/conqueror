#include "CopilotProvider.h"
#include "Core/Logging/Log.h"
#include "Networking/WebClient.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <functional>

namespace Conqueror::AI
{
    namespace
    {
        static std::string JsonExtractString(const std::string& json, const std::string& key)
        {
            std::string needle = "\"" + key + "\"";
            size_t keyPos = json.find(needle);
            if (keyPos == std::string::npos) return "";

            size_t colon = json.find(':', keyPos + needle.size());
            if (colon == std::string::npos) return "";

            size_t quoteStart = json.find('"', colon + 1);
            if (quoteStart == std::string::npos) return "";

            size_t quoteEnd = quoteStart + 1;
            while (quoteEnd < json.size())
            {
                if (json[quoteEnd] == '\\') { quoteEnd += 2; continue; }
                if (json[quoteEnd] == '"') break;
                ++quoteEnd;
            }
            if (quoteEnd >= json.size()) return "";

            std::string raw = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            std::string result;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == '\\' && i + 1 < raw.size())
                {
                    switch (raw[i + 1])
                    {
                        case 'n': result += '\n'; ++i; break;
                        case 'r': result += '\r'; ++i; break;
                        case 't': result += '\t'; ++i; break;
                        case '"': result += '"'; ++i; break;
                        case '\\': result += '\\'; ++i; break;
                        default: result += raw[i]; break;
                    }
                }
                else
                {
                    result += raw[i];
                }
            }

            return result;
        }

        static std::string JsonEscape(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c; break;
                }
            }
            return out;
        }

        static std::vector<std::string> ExtractModelNames(const std::string& body)
        {
            std::vector<std::string> models;
            std::vector<std::string> fallbackModels;

            auto pushIfUnique = [&models](const std::string& value)
            {
                if (value.empty())
                    return;

                for (const auto& existing : models)
                {
                    if (existing == value)
                        return;
                }

                models.push_back(value);
            };

            auto pushFallbackIfUnique = [&fallbackModels](const std::string& value)
            {
                if (value.empty())
                    return;

                for (const auto& existing : fallbackModels)
                {
                    if (existing == value)
                        return;
                }

                fallbackModels.push_back(value);
            };

            size_t pos = 0;
            while ((pos = body.find("\"id\"", pos)) != std::string::npos)
            {
                size_t colon = body.find(':', pos + 3);
                if (colon == std::string::npos)
                    break;

                size_t quoteStart = body.find('"', colon + 1);
                if (quoteStart == std::string::npos)
                    break;

                size_t quoteEnd = quoteStart + 1;
                while (quoteEnd < body.size())
                {
                    if (body[quoteEnd] == '\\') { quoteEnd += 2; continue; }
                    if (body[quoteEnd] == '"') break;
                    ++quoteEnd;
                }

                if (quoteEnd >= body.size())
                    break;

                std::string value = body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                pushIfUnique(value);
                pos = quoteEnd + 1;
            }

            if (models.empty())
            {
                pos = 0;
                while ((pos = body.find("\"name\"", pos)) != std::string::npos)
                {
                    size_t colon = body.find(':', pos + 5);
                    if (colon == std::string::npos)
                        break;

                    size_t quoteStart = body.find('"', colon + 1);
                    if (quoteStart == std::string::npos)
                        break;

                    size_t quoteEnd = quoteStart + 1;
                    while (quoteEnd < body.size())
                    {
                        if (body[quoteEnd] == '\\') { quoteEnd += 2; continue; }
                        if (body[quoteEnd] == '"') break;
                        ++quoteEnd;
                    }

                    if (quoteEnd >= body.size())
                        break;

                    std::string value = body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    pushFallbackIfUnique(value);
                    pos = quoteEnd + 1;
                }
                return fallbackModels;
            }

            return models;
        }
    }

    bool CopilotProvider::Authenticate(const std::string& apiKey)
    {
        if (!apiKey.empty())
        {
            m_ApiKey = apiKey;
            m_IsAuthenticated = true;
            m_ModelDiscoveryAttempted = false;
            m_DiscoveredModels.clear();
            m_SelectedModel.clear();
            DiscoverModels();
            return true;
        }
        return false;
    }

    bool CopilotProvider::IsAuthenticated() const
    {
        return m_IsAuthenticated;
    }

    std::vector<std::string> CopilotProvider::GetAvailableModels() const
    {
        if (m_DiscoveredModels.empty() && !m_ApiKey.empty() && !m_ModelDiscoveryAttempted)
        {
            const_cast<CopilotProvider*>(this)->DiscoverModels();
        }

        return m_DiscoveredModels;
    }

    void CopilotProvider::Logout()
    {
        m_ApiKey.clear();
        m_SelectedModel.clear();
        m_DiscoveredModels.clear();
        m_IsAuthenticated = false;
        m_ModelDiscoveryAttempted = false;
    }

    void CopilotProvider::DiscoverModels()
    {
        if (m_ApiKey.empty()) return;

        m_ModelDiscoveryAttempted = true;
        m_DiscoveredModels.clear();
        m_SelectedModel.clear();

        std::unordered_map<std::string, std::string> headers;
        headers["Authorization"] = "Bearer " + m_ApiKey;
        headers["Accept"] = "application/json";

        std::string url = "https://api.githubcopilot.com/models";
        auto resp = Networking::WebClient::HttpsRequest(url, "GET", "", headers);

        if (resp.Status == 200 && !resp.Body.empty())
        {
            auto apiModels = ExtractModelNames(resp.Body);
            if (!apiModels.empty())
            {
                m_DiscoveredModels = apiModels;
                m_SelectedModel = apiModels.front();
                CQ_CORE_INFO("AI: Copilot live models discovered. Using supported model: {0}", m_SelectedModel);
            }
            else
            {
                CQ_CORE_WARN("AI: Copilot API returned success but no model names were found. Raw response: {0}", resp.Body.substr(0, 500));
            }
        }
        else
        {
            CQ_CORE_WARN("AI: Copilot model discovery failed with status {0}. Raw response: {1}", resp.Status, resp.Body.substr(0, 500));
        }

        if (m_SelectedModel.empty() && !m_DiscoveredModels.empty())
            m_SelectedModel = m_DiscoveredModels.front();
    }

    std::string CopilotProvider::SendChat(const std::vector<ChatMessage>& messages,
                                        const std::vector<ToolDefinition>& /*tools*/,
                                        StreamingCallback streamCb)
    {
        if (!m_IsAuthenticated || m_ApiKey.empty())
            return "Not authenticated. Please connect via browser.";

        if (messages.empty())
            return "No messages provided.";

        const std::string model = m_SelectedModel.empty()
            ? (m_DiscoveredModels.empty() ? "" : m_DiscoveredModels.front())
            : m_SelectedModel;

        if (model.empty())
            return "No Copilot models are available yet. Please try reconnecting.";

        std::string contentsJson;

        for (const auto& msg : messages)
        {
            if (msg.Content.empty() || msg.Content == "..." || msg.Content == "Thinking...")
                continue;

            std::string role = msg.Role;
            if (role != "system" && role != "assistant" && role != "user")
                role = "user";

            if (!contentsJson.empty()) contentsJson += ",";
            contentsJson += "{\"role\":\"" + role + "\",\"content\":\"" + JsonEscape(msg.Content) + "\"}";
        }

        std::string requestJson = "{\"model\":\"" + model + "\",\"messages\":[" + contentsJson + "],\"stream\":true,\"max_completion_tokens\":8192}";

        std::unordered_map<std::string, std::string> headers;
        headers["Authorization"] = "Bearer " + m_ApiKey;
        headers["Accept"] = "text/event-stream";
        headers["Content-Type"] = "application/json";

        CQ_CORE_INFO("AI: Sending Copilot chat request (model={0}, streaming)", model);

        std::string accumulatedText;

        auto onLine = [streamCb, &accumulatedText](const std::string& line) {
            // SSE format: each line is "data: {json}" or "data: [DONE]"
            if (line.rfind("data:", 0) != 0) return;

            std::string jsonStr = line.substr(5);
            size_t start = jsonStr.find_first_not_of(' ');
            if (start == std::string::npos) return;
            jsonStr = jsonStr.substr(start);

            // Stream terminator
            if (jsonStr == "[DONE]") return;

            // Find the JSON object
            size_t braceStart = jsonStr.find('{');
            if (braceStart == std::string::npos) return;
            jsonStr = jsonStr.substr(braceStart);

            // OpenAI-compatible format: extract delta.content
            // {"choices":[{"delta":{"content":"..."}}]}
            size_t deltaPos = jsonStr.find("\"delta\"");
            if (deltaPos == std::string::npos) return;

            size_t deltaBrace = jsonStr.find('{', deltaPos + 7);
            if (deltaBrace == std::string::npos) return;

            // Find matching closing brace for the delta object
            int depth = 1;
            size_t deltaEnd = deltaBrace + 1;
            while (deltaEnd < jsonStr.size() && depth > 0)
            {
                if (jsonStr[deltaEnd] == '{') depth++;
                else if (jsonStr[deltaEnd] == '}') depth--;
                deltaEnd++;
            }

            std::string deltaStr = jsonStr.substr(deltaBrace, deltaEnd - deltaBrace);
            std::string chunkText = JsonExtractString(deltaStr, "content");

            if (!chunkText.empty())
            {
                accumulatedText += chunkText;
                if (streamCb)
                    streamCb(chunkText);
            }
        };

        auto resp = Networking::WebClient::HttpsStreamRequest(
            "https://api.githubcopilot.com/chat/completions", "POST",
            requestJson, headers, onLine);

        if (resp.Status != 200 || accumulatedText.empty())
        {
            CQ_CORE_WARN("AI: Copilot chat request failed with status {0}. Response: {1}", resp.Status, resp.Body.substr(0, 500));

            if (resp.Status == 401 || resp.Status == 403)
                return "Session expired. Please reconnect via browser.";
            if (resp.Status == 400)
                return "Invalid request. Please try rephrasing.";
            if (resp.Status == 429)
                return "Rate limit exceeded. Please wait a moment and try again.";
            if (resp.Status == 0)
                return "Unable to connect. Please check your network.";

            return "Service error (" + std::to_string(resp.Status) + "). Please try again.";
        }

        return accumulatedText;
    }
}
