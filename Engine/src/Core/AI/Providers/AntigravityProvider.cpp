#include "AntigravityProvider.h"
#include "Core/Logging/Log.h"
#include "Networking/WebClient.h"
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace Conqueror::AI
{
    static const std::string ENDPOINT_DAILY    = "https://daily-cloudcode-pa.sandbox.googleapis.com";
    static const std::string ENDPOINT_AUTOPUSH = "https://autopush-cloudcode-pa.sandbox.googleapis.com";
    static const std::string ENDPOINT_PROD     = "https://cloudcode-pa.googleapis.com";
    static const std::string DEFAULT_PROJECT   = "rising-fact-p41fc";

    AntigravityProvider::AntigravityProvider()
    {
    }

    bool AntigravityProvider::Authenticate(const std::string& apiKey)
    {
        if (!apiKey.empty())
        {
            m_ApiKey = apiKey;
            m_IsAuthenticated = true;
            CQ_CORE_INFO("Antigravity Provider authenticated successfully.");

            // Discover project via loadCodeAssist after auth
            DiscoverProject();
            return true;
        }
        return false;
    }

    bool AntigravityProvider::IsAuthenticated() const
    {
        return m_IsAuthenticated;
    }

    std::vector<std::string> AntigravityProvider::GetAvailableModels() const
    {
        if (!m_DiscoveredModels.empty())
            return m_DiscoveredModels;

        // Verified working models on Sandbox endpoint
        return {
            "gemini-3.1-pro-low",
            "claude-sonnet-4-6",
            "claude-opus-4-6-thinking"
        };
    }

    void AntigravityProvider::Logout()
    {
        m_ApiKey.clear();
        m_IsAuthenticated = false;
        m_ProjectId.clear();
        m_DiscoveredModels.clear();
    }

    // ─── JSON helpers ────────────────────────────────────────────────────────────

    static std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;      break;
            }
        }
        return out;
    }

    // Very small JSON value extractor — pulls the first string value for a key.
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
        // Unescape
        std::string result;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '\\' && i + 1 < raw.size())
            {
                switch (raw[i + 1])
                {
                    case 'n':  result += '\n'; ++i; break;
                    case 'r':  result += '\r'; ++i; break;
                    case 't':  result += '\t'; ++i; break;
                    case '"':  result += '"';  ++i; break;
                    case '\\': result += '\\'; ++i; break;
                    default:   result += raw[i]; break;
                }
            }
            else result += raw[i];
        }
        return result;
    }

    // ─── Model ID mapping matching Astro-Agent getApiModelId ─────────────────────

    static std::string GetApiModelId(const std::string& modelId)
    {
        std::string id = modelId;
        // Lowercase for matching
        std::string lower = id;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Strip "antigravity-" prefix if present
        if (lower.find("antigravity-") == 0)
            id = id.substr(12);

        if (lower.find("claude-sonnet-4-6-thinking") != std::string::npos) return "claude-sonnet-4-6";
        if (lower.find("claude-opus-4-6-thinking") != std::string::npos) return "claude-opus-4-6-thinking";
        if (lower.find("gemini-3-1-pro-low") != std::string::npos) return "gemini-3.1-pro-low";

        return id;
    }

    // ─── Project Discovery via loadCodeAssist ────────────────────────────────────

    void AntigravityProvider::DiscoverProject()
    {
        if (m_ApiKey.empty()) return;

        std::string body = "{\"metadata\":{\"ideType\":\"IDE_UNSPECIFIED\",\"platform\":\"PLATFORM_UNSPECIFIED\",\"pluginType\":\"GEMINI\"}}";

        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Authorization"] = "Bearer " + m_ApiKey;
        headers["User-Agent"] = "google-api-nodejs-client/9.15.1";
        headers["X-Goog-Api-Client"] = "google-cloud-sdk vscode_cloudshelleditor/0.1";
        headers["Client-Metadata"] = "{\"ideType\":\"IDE_UNSPECIFIED\",\"platform\":\"PLATFORM_UNSPECIFIED\",\"pluginType\":\"GEMINI\"}";

        // Astro-Agent uses prod first, then daily for discoverProject
        std::vector<std::string> endpoints = { ENDPOINT_PROD, ENDPOINT_DAILY };

        for (const auto& ep : endpoints)
        {
            std::string url = ep + "/v1internal:loadCodeAssist";
            auto resp = Networking::WebClient::HttpsRequest(url, "POST", body, headers);

            if (resp.Status == 200 && !resp.Body.empty())
            {
                // Astro-Agent strictly extracts cloudaicompanionProject (string or object with id)
                size_t projPos = resp.Body.find("\"cloudaicompanionProject\"");
                if (projPos != std::string::npos)
                {
                    std::string projSection = resp.Body.substr(projPos);
                    std::string projectId = JsonExtractString(projSection, "cloudaicompanionProject");
                    if (projectId.empty())
                    {
                        projectId = JsonExtractString(projSection, "id");
                    }
                    if (!projectId.empty())
                    {
                        m_ProjectId = projectId;
                        CQ_CORE_INFO("Discovered project: {0}", m_ProjectId);
                        return;
                    }
                }
            }
        }

        // Fallback matching Astro-Agent DEFAULT_PROJECT_ID
        m_ProjectId = DEFAULT_PROJECT;
        CQ_CORE_INFO("Using default project: {0}", m_ProjectId);
    }

    // ─── Extract text from SSE chunks ────────────────────────────────────────────

    static std::string ParseSseStreamResponse(const std::string& sseRaw)
    {
        std::string fullText;
        std::stringstream ss(sseRaw);
        std::string line;

        while (std::getline(ss, line))
        {
            // Remove trailing \r if present
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (line.rfind("data:", 0) != 0) continue;

            std::string jsonStr = line.substr(5);
            // Trim leading whitespace
            size_t start = jsonStr.find_first_not_of(' ');
            if (start == std::string::npos) continue;
            jsonStr = jsonStr.substr(start);

            // Find the JSON object
            size_t braceStart = jsonStr.find('{');
            if (braceStart == std::string::npos) continue;
            jsonStr = jsonStr.substr(braceStart);

            // The SSE response wraps in {"response": {"candidates": [{"content": {"parts": [{"text": "..."}]}}]}}
            // Extract text from the nested structure
            std::string chunkText = JsonExtractString(jsonStr, "text");
            if (!chunkText.empty())
            {
                fullText += chunkText;
            }
        }

        // Fallback: try extracting from raw if SSE parsing found nothing
        if (fullText.empty())
        {
            fullText = JsonExtractString(sseRaw, "text");
        }

        return fullText;
    }

    // ─── SendChat with endpoint fallback and retry ───────────────────────────────

    std::string AntigravityProvider::SendChat(const std::vector<ChatMessage>& messages,
                                              const std::vector<ToolDefinition>& /*tools*/,
                                              StreamingCallback streamCb)
    {
        if (!m_IsAuthenticated || m_ApiKey.empty())
            return "Not authenticated. Please connect via browser.";

        if (messages.empty())
            return "No messages provided.";

        if (m_ProjectId.empty())
        {
            DiscoverProject();
        }

        std::string model = m_SelectedModel.empty() ? "gemini-3.1-pro-low" : m_SelectedModel;
        std::string apiModelId = GetApiModelId(model);
        std::string projectId = m_ProjectId.empty() ? DEFAULT_PROJECT : m_ProjectId;

        // Build `contents` array - MUST end with a user message (Claude API requirement)
        std::string systemText;
        std::string contentsJson;
        for (const auto& msg : messages)
        {
            if (msg.Role == "system")
            {
                systemText = msg.Content;
                continue;
            }
            // Skip empty/placeholder messages and pending assistant prefill messages
            if (msg.Content.empty() || msg.Content == "..." || msg.Content == "Thinking...") continue;

            std::string role = (msg.Role == "assistant") ? "model" : "user";
            if (!contentsJson.empty()) contentsJson += ",";
            contentsJson += "{\"role\":\"" + role + "\","
                            "\"parts\":[{\"text\":\"" + JsonEscape(msg.Content) + "\"}]}";
        }

        // Build inner request object
        std::string requestJson = "{";
        if (!systemText.empty())
        {
            requestJson += "\"systemInstruction\":{\"role\":\"system\",\"parts\":[{\"text\":\"" + JsonEscape(systemText) + "\"}]},";
        }
        requestJson += "\"contents\":[" + contentsJson + "],"
                       "\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":8192}"
                       "}";

        // Build outer body matching Astro-Agent's CloudCodeAssistRequest structure exactly
        // CRITICAL: requestType "engine" and userAgent "antigravity" are REQUIRED for Antigravity endpoints
        std::string bodyJson = "{\"project\":\"" + projectId + "\","
                               "\"model\":\"" + apiModelId + "\","
                               "\"request\":" + requestJson + ","
                               "\"requestType\":\"engine\","
                               "\"userAgent\":\"antigravity\""
                               "}";

        // Headers matching Astro-Agent getAntigravityHeaders
        std::string requestId = "agent-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "text/event-stream";
        headers["User-Agent"] = "antigravity/1.28.0 darwin/arm64";
        headers["Authorization"] = "Bearer " + m_ApiKey;
        headers["X-Request-Id"] = requestId;

        // Claude models need thinking beta header
        if (apiModelId.find("claude-") == 0)
        {
            headers["anthropic-beta"] = "thinking-2025-01-31,interleaved-thinking-2025-05-14";
        }

        CQ_CORE_INFO("Sending request to API (model={0}, mapped={1})", model, apiModelId);

        // Endpoint fallback matching Astro-Agent: daily -> autopush -> prod
        std::vector<std::string> endpoints = { ENDPOINT_DAILY, ENDPOINT_AUTOPUSH, ENDPOINT_PROD };

        const int MAX_RETRIES = 3;
        const int BASE_DELAY_MS = 1000;
        int endpointIndex = 0;

        for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt)
        {
            const std::string& ep = endpoints[static_cast<size_t>(endpointIndex)];
            std::string url = ep + "/v1internal:streamGenerateContent?alt=sse";

            std::string accumulatedText;

            auto onLine = [streamCb, &accumulatedText](const std::string& line) {
                if (line.rfind("data:", 0) != 0) return;

                std::string jsonStr = line.substr(5);
                size_t start = jsonStr.find_first_not_of(' ');
                if (start == std::string::npos) return;
                jsonStr = jsonStr.substr(start);

                size_t braceStart = jsonStr.find('{');
                if (braceStart == std::string::npos) return;
                jsonStr = jsonStr.substr(braceStart);

                std::string chunkText = JsonExtractString(jsonStr, "text");
                if (!chunkText.empty())
                {
                    accumulatedText += chunkText;
                    if (streamCb)
                    {
                        streamCb(chunkText);
                    }
                }
            };

            auto resp = Networking::WebClient::HttpsStreamRequest(url, "POST", bodyJson, headers, onLine);

            if (resp.Status == 200 && !accumulatedText.empty())
            {
                return accumulatedText;
            }

            // On 403/404, cascade to next endpoint immediately (no delay) - matches Astro-Agent
            if ((resp.Status == 403 || resp.Status == 404) && endpointIndex < (int)endpoints.size() - 1)
            {
                CQ_CORE_WARN("Endpoint {0} returned {1}, trying next...", ep, resp.Status);
                endpointIndex++;
                --attempt; // Don't count this as a retry attempt
                continue;
            }

            // Retryable errors: 429, 500, 502, 503, 504
            bool retryable = (resp.Status == 429 || resp.Status == 500 || resp.Status == 502 ||
                             resp.Status == 503 || resp.Status == 504);

            if (attempt < MAX_RETRIES && retryable)
            {
                // Advance endpoint if possible
                if (endpointIndex < (int)endpoints.size() - 1)
                    endpointIndex++;

                int delayMs = BASE_DELAY_MS * (1 << attempt); // Exponential backoff
                CQ_CORE_WARN("Endpoint returned {0}, retrying in {1}ms (attempt {2}/{3})...",
                    resp.Status, delayMs, attempt + 1, MAX_RETRIES);
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                continue;
            }

            // Non-retryable or max retries exceeded
            CQ_CORE_WARN("Final status {0}: {1}", resp.Status, resp.Body.substr(0, 300));

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

        return "Failed after retries. Please try again.";
    }
}
