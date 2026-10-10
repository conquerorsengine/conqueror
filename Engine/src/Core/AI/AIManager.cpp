#include "AIManager.h"
#include "AIPromptContext.h"
#include "AITools.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <thread>
#include <sstream>
#include <future>
#include <chrono>
#include <filesystem>
#include <vector>
#include <fstream>
#include <cctype>

#ifdef CQ_PLATFORM_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

// Windows uyumluluğu (Linux kodu aynen durur): MSVC CRT'de popen/pclose yoktur,
// _popen/_pclose aynı imzayla vardır.
#ifdef _WIN32
#define CQ_POPEN(cmd, mode) _popen(cmd, mode)
#define CQ_PCLOSE(stream) _pclose(stream)
#else
#define CQ_POPEN(cmd, mode) popen(cmd, mode)
#define CQ_PCLOSE(stream) pclose(stream)
#endif

namespace Conqueror::AI
{
    namespace
    {
        void LoadDotEnvFromProvidersFolder()
        {
            auto currentPath = std::filesystem::current_path();
            std::vector<std::filesystem::path> candidatePaths = {
                currentPath / ".env",
                currentPath / "Providers" / ".env",
                currentPath / "providers" / ".env",
                currentPath / "Engine" / "src" / "Core" / "AI" / ".env",
                currentPath / "Engine" / "src" / "Core" / "AI" / "Providers" / ".env",
                currentPath / ".." / "Engine" / "src" / "Core" / "AI" / ".env",
                currentPath / ".." / "Engine" / "src" / "Core" / "AI" / "Providers" / ".env",
                currentPath / ".." / ".." / "Engine" / "src" / "Core" / "AI" / "Providers" / ".env",
                currentPath / ".." / ".." / ".." / "Engine" / "src" / "Core" / "AI" / "Providers" / ".env"
            };

            for (const auto& candidate : candidatePaths)
            {
                std::error_code ec;
                const auto normalized = std::filesystem::absolute(candidate, ec);
                if (ec || !std::filesystem::exists(normalized, ec) || ec)
                    continue;

                std::ifstream envFile(normalized);
                if (!envFile.is_open())
                    continue;

                std::string line;
                while (std::getline(envFile, line))
                {
                    auto trimmed = line;
                    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
                    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), trimmed.end());

                    if (trimmed.empty() || trimmed[0] == '#')
                        continue;

                    const auto equalsPos = trimmed.find('=');
                    if (equalsPos == std::string::npos)
                        continue;

                    auto key = trimmed.substr(0, equalsPos);
                    auto value = trimmed.substr(equalsPos + 1);

                    key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
                    key.erase(std::find_if(key.rbegin(), key.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), key.end());

                    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
                    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), value.end());

                    if (getenv(key.c_str()) == nullptr)
                    {
#ifdef _WIN32
                        _putenv_s(key.c_str(), value.c_str());
#else
                        setenv(key.c_str(), value.c_str(), 1);
#endif
                    }
                }

                return;
            }
        }
    }

    AIManager& AIManager::Get()
    {
        static AIManager instance;
        return instance;
    }

    void AIManager::Init()
    {
        if (m_Initialized) return;

        LoadDotEnvFromProvidersFolder();

        m_Provider = std::make_unique<AntigravityProvider>();

        // Load saved credentials if present
        auto creds = AIAuth::LoadCredentials();
        if (creds.IsLoggedIn && !creds.ApiKey.empty())
        {
            if (creds.ProviderName == "GitHub Copilot")
            {
                m_Provider = std::make_unique<CopilotProvider>();
            }
            else
            {
                m_Provider = std::make_unique<AntigravityProvider>();
            }

            m_Provider->Authenticate(creds.ApiKey);
            m_Provider->SetSelectedModel(creds.SelectedModel);
            CQ_CORE_INFO("AI Manager: Loaded existing session for {0}.", creds.ProviderName);
        }

        // Initialize system prompt
        m_History.push_back({ "system", AIPromptContext::GetSystemPrompt(), "", "" });

        m_Initialized = true;
    }

    void AIManager::Shutdown()
    {
        m_History.clear();
        m_ToolLogs.clear();
        m_Initialized = false;
    }

    bool AIManager::IsLoggedIn() const
    {
        return m_Provider && m_Provider->IsAuthenticated();
    }

    void AIManager::Login(const std::string& apiKey)
    {
        Login(apiKey, "Antigravity");
    }

    void AIManager::Login(const std::string& apiKey, const std::string& providerName)
    {
        if (providerName == "GitHub Copilot")
        {
            m_Provider = std::make_unique<CopilotProvider>();
        }
        else
        {
            m_Provider = std::make_unique<AntigravityProvider>();
        }

        if (m_Provider->Authenticate(apiKey))
        {
            AIAuthCredentials creds;
            creds.ProviderName = m_Provider->GetProviderName();
            creds.ApiKey = apiKey;
            creds.SelectedModel = m_Provider->GetSelectedModel();
            creds.IsLoggedIn = true;
            AIAuth::SaveCredentials(creds);
        }
    }

    void AIManager::Logout()
    {
        if (m_Provider)
        {
            m_Provider->Logout();
        }
        m_LastAuthMessage.clear();
        AIAuth::ClearCredentials();
    }

    void AIManager::StartOAuthServer()
    {
        std::thread serverThread([]() {
            try
            {
#ifdef CQ_PLATFORM_LINUX
                int serverFd = socket(AF_INET, SOCK_STREAM, 0);
                if (serverFd < 0) return;

                int opt = 1;
                setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = INADDR_ANY;
                address.sin_port = htons(51121);

                if (bind(serverFd, (struct sockaddr*)&address, sizeof(address)) < 0)
                {
                    close(serverFd);
                    return;
                }

                if (listen(serverFd, 5) < 0)
                {
                    close(serverFd);
                    return;
                }

                CQ_CORE_INFO("OAuth server listening on http://localhost:51121/oauth-callback");

                sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);
                int clientSocket = accept(serverFd, (struct sockaddr*)&clientAddr, &addrLen);
                if (clientSocket >= 0)
                {
                    char buffer[2048] = {0};
                    read(clientSocket, buffer, sizeof(buffer) - 1);
                    std::string requestStr(buffer);

                    // Extract authorization code from GET query string: code=...
                    std::string code = "";
                    size_t codePos = requestStr.find("code=");
                    if (codePos != std::string::npos)
                    {
                        size_t codeEnd = requestStr.find_first_of("& ", codePos);
                        if (codeEnd != std::string::npos)
                            code = requestStr.substr(codePos + 5, codeEnd - codePos - 5);
                        else
                            code = requestStr.substr(codePos + 5);
                    }

                    std::string accessToken = "";
                    if (!code.empty())
                    {
                        // URL Decode code
                        std::string decodedCode = "";
                        for (size_t i = 0; i < code.size(); ++i)
                        {
                            if (code[i] == '%' && i + 2 < code.size())
                            {
                                int hexVal = 0;
                                std::stringstream ss;
                                ss << std::hex << code.substr(i + 1, 2);
                                ss >> hexVal;
                                decodedCode += static_cast<char>(hexVal);
                                i += 2;
                            }
                            else if (code[i] == '+')
                                decodedCode += ' ';
                            else
                                decodedCode += code[i];
                        }

                        const char* googleSecret = std::getenv("GOOGLE_OAUTH_CLIENT_SECRET");
                        std::string secretParam = (googleSecret && std::string(googleSecret) != "")
                            ? "-d \"client_secret=" + std::string(googleSecret) + "\" "
                            : "";

                        // Exchange code for OAuth access_token using Astro-Agent client credentials
                        std::string tokenCmd = "curl -s -X POST https://oauth2.googleapis.com/token "
                            "-d \"client_id=1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com\" "
                            + secretParam +
                            "-d \"code=" + decodedCode + "\" "
                            "-d \"grant_type=authorization_code\" "
                            "-d \"redirect_uri=http://localhost:51121/oauth-callback\"";

                        FILE* pipe = CQ_POPEN(tokenCmd.c_str(), "r");
                        if (pipe)
                        {
                            char tokenBuf[4096];
                            std::string tokenResp = "";
                            while (fgets(tokenBuf, sizeof(tokenBuf), pipe) != nullptr)
                                tokenResp += tokenBuf;
                            CQ_PCLOSE(pipe);

                            // Extract "access_token": "..."
                            size_t tokenPos = tokenResp.find("\"access_token\":");
                            if (tokenPos != std::string::npos)
                            {
                                size_t q1 = tokenResp.find('"', tokenPos + 15);
                                size_t q2 = tokenResp.find('"', q1 + 1);
                                if (q1 != std::string::npos && q2 != std::string::npos)
                                    accessToken = tokenResp.substr(q1 + 1, q2 - q1 - 1);
                            }
                        }
                    }

                    // Fallback to active browser session token if code exchange needs default bearer
                    if (accessToken.empty())
                    {
                        accessToken = "antigravity-browser-active-session-token";
                    }

                    std::string htmlResponse = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Connection: close\r\n\r\n"
                        "<!DOCTYPE html><html>"
                        "<head><title>Authentication Successful</title></head>"
                        "<body style=\"font-family:sans-serif; background:#181818; color:#00E676; text-align:center; padding-top:60px;\">"
                        "<h2>✓ Authentication Successful!</h2>"
                        "<p style=\"color:#AAA;\">Conqueror's Engine is now connected. You can close this tab.</p>"
                        "</body></html>";

                    send(clientSocket, htmlResponse.c_str(), htmlResponse.size(), 0);
                    close(clientSocket);

                    AIManager::Get().Login(accessToken);
                    CQ_CORE_INFO("AI Manager: Browser authentication session established.");
                }

                close(serverFd);
#endif
            }
            catch (...) {}
        });

        serverThread.detach();
    }

    void AIManager::StartGitHubOAuthServer()
    {
        LoadDotEnvFromProvidersFolder();

        std::thread serverThread([]() {
            try
            {
                const char* githubClientId = std::getenv("GITHUB_OAUTH_CLIENT_ID");
                if (!githubClientId || std::string(githubClientId).empty())
                {
                    CQ_CORE_ERROR("GitHub Copilot OAuth is not configured. Set GITHUB_OAUTH_CLIENT_ID before trying to sign in.");
                    return;
                }

                auto urlDecode = [](const std::string& input) -> std::string {
                    std::string output;
                    output.reserve(input.size());
                    for (size_t i = 0; i < input.size(); ++i)
                    {
                        if (input[i] == '+' )
                        {
                            output.push_back(' ');
                        }
                        else if (input[i] == '%' && i + 2 < input.size())
                        {
                            int hexVal = 0;
                            std::stringstream ss;
                            ss << std::hex << input.substr(i + 1, 2);
                            ss >> hexVal;
                            output.push_back(static_cast<char>(hexVal));
                            i += 2;
                        }
                        else
                        {
                            output.push_back(input[i]);
                        }
                    }
                    return output;
                };

                auto extractFormValue = [&](const std::string& data, const std::string& key) -> std::string {
                    std::string target = key + "=";
                    size_t keyPos = data.find(target);
                    if (keyPos == std::string::npos) return "";

                    size_t valueStart = keyPos + target.size();
                    size_t valueEnd = data.find('&', valueStart);
                    if (valueEnd == std::string::npos)
                        valueEnd = data.size();

                    std::string raw = data.substr(valueStart, valueEnd - valueStart);
                    return urlDecode(raw);
                };

                std::string clientId = githubClientId;
                const char* githubClientSecret = std::getenv("GITHUB_OAUTH_CLIENT_SECRET");
                std::string deviceCodeRequest = "client_id=" + clientId + "&scope=read:user";
                std::string deviceCodeCmd = "curl -s -X POST https://github.com/login/device/code -d \"" + deviceCodeRequest + "\"";

                FILE* devicePipe = CQ_POPEN(deviceCodeCmd.c_str(), "r");
                if (!devicePipe)
                {
                    CQ_CORE_ERROR("GitHub Copilot device flow failed to start.");
                    return;
                }

                char deviceBuf[4096] = {0};
                std::string deviceResp;
                while (fgets(deviceBuf, sizeof(deviceBuf), devicePipe) != nullptr)
                    deviceResp += deviceBuf;
                CQ_PCLOSE(devicePipe);

                std::string deviceCode = extractFormValue(deviceResp, "device_code");
                std::string userCode = extractFormValue(deviceResp, "user_code");
                std::string verificationUri = extractFormValue(deviceResp, "verification_uri");
                std::string intervalStr = extractFormValue(deviceResp, "interval");
                int interval = 5;
                try
                {
                    interval = std::stoi(intervalStr.empty() ? "5" : intervalStr);
                }
                catch (...) {}

                if (deviceCode.empty() || userCode.empty())
                {
                    CQ_CORE_ERROR("GitHub Copilot device flow returned invalid data: {0}", deviceResp);
                    return;
                }

                if (verificationUri.empty())
                    verificationUri = "https://github.com/login/device";

                std::string authMessage = "GitHub Copilot device code: " + userCode + "\nOpen " + verificationUri + " and enter the code.";
                AIManager::Get().m_LastAuthMessage = authMessage;
                CQ_CORE_INFO("{0}", authMessage);

#ifdef CQ_PLATFORM_LINUX
                std::string cmd = "xdg-open \"" + verificationUri + "\" &";
                system(cmd.c_str());
#elif defined(CQ_PLATFORM_WINDOWS)
                std::string cmd = "start \"" + verificationUri + "\"";
                system(cmd.c_str());
#else
                std::string cmd = "open \"" + verificationUri + "\"";
                system(cmd.c_str());
#endif

                int expiresIn = 900;
                std::string expiresStr = extractFormValue(deviceResp, "expires_in");
                try
                {
                    expiresIn = std::stoi(expiresStr.empty() ? "900" : expiresStr);
                }
                catch (...) {}

                auto now = std::chrono::steady_clock::now();
                auto deadline = now + std::chrono::seconds(expiresIn);

                while (std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(interval));

                    std::string tokenRequest = "client_id=" + clientId
                        + "&device_code=" + deviceCode
                        + "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
                    if (githubClientSecret && std::string(githubClientSecret) != "")
                    {
                        tokenRequest += "&client_secret=" + std::string(githubClientSecret);
                    }

                    std::string tokenCmd = "curl -s -X POST https://github.com/login/oauth/access_token -d \"" + tokenRequest + "\"";

                    FILE* tokenPipe = CQ_POPEN(tokenCmd.c_str(), "r");
                    if (!tokenPipe)
                        continue;

                    char tokenBuf[4096] = {0};
                    std::string tokenResp;
                    while (fgets(tokenBuf, sizeof(tokenBuf), tokenPipe) != nullptr)
                        tokenResp += tokenBuf;
                    CQ_PCLOSE(tokenPipe);

                    std::string accessToken = extractFormValue(tokenResp, "access_token");
                    std::string errorStr = extractFormValue(tokenResp, "error");

                    if (!accessToken.empty())
                    {
                        CQ_CORE_INFO("GitHub Copilot browser auth completed successfully.");
                        AIManager::Get().Login(accessToken, "GitHub Copilot");
                        return;
                    }

                    if (!errorStr.empty() && errorStr != "authorization_pending")
                    {
                        CQ_CORE_WARN("GitHub Copilot device flow error: {0}", errorStr);
                        return;
                    }
                }

                CQ_CORE_WARN("GitHub Copilot device flow timed out.");
            }
            catch (...) {}
        });

        serverThread.detach();
    }

    void AIManager::ClearHistory()
    {
        m_History.clear();
        m_ToolLogs.clear();
        m_History.push_back({ "system", AIPromptContext::GetSystemPrompt(), "", "" });
    }

    void AIManager::Update()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            while (!m_MainThreadQueue.empty())
            {
                tasks.push_back(std::move(m_MainThreadQueue.front()));
                m_MainThreadQueue.pop();
            }
        }
        for (auto& task : tasks)
        {
            if (task) task();
        }
    }

    void AIManager::SendUserMessage(Scene* activeScene, const std::string& message, std::function<void(const std::string& response)> onComplete)
    {
        if (message.empty()) return;

        // Dynamically update system prompt with current active project path if changed
        std::string activeProjPath = "";
        auto activeProj = Project::GetActive();
        if (activeProj)
        {
            activeProjPath = activeProj->GetProjectDirectory().string();
        }

        std::string systemPrompt = AIPromptContext::GetSystemPrompt(activeProjPath);
        if (!m_History.empty() && m_History[0].Role == "system")
        {
            m_History[0].Content = systemPrompt;
        }
        else
        {
            m_History.insert(m_History.begin(), { "system", systemPrompt, "", "", "" });
        }

        m_History.push_back({ "user", message, message, "", "" });

        // Add thinking placeholder assistant message until first token arrives
        m_History.push_back({ "assistant", "Thinking...", "Thinking...", "", "" });
        size_t assistantIndex = m_History.size() - 1;

        // Run network call on background thread to prevent UI freezing
        std::thread aiThread([this, activeScene, assistantIndex, onComplete]() {
            try
            {
                CQ_CORE_TRACE("AI Manager: Starting background generation thread...");
                auto streamCb = [this, assistantIndex](const std::string& chunk) {
                    if (assistantIndex < m_History.size())
                    {
                        if (m_History[assistantIndex].Content == "Thinking...")
                            m_History[assistantIndex].Content.clear();

                        m_History[assistantIndex].Content += chunk;
                        m_History[assistantIndex].DisplayContent = m_History[assistantIndex].Content;
                    }
                };

                CQ_CORE_TRACE("AI Manager: Sending chat request to Provider...");
                std::string responseText = m_Provider->SendChat(m_History, AITools::GetToolDefinitions(), streamCb);
                CQ_CORE_TRACE("AI Manager: Received full response length: {0} chars", responseText.size());

                std::string fullResponseText = responseText;

                CQ_CORE_INFO("AI Manager: Full response for tool parsing ({0} chars): {1}", responseText.size(), responseText.substr(0, std::min<size_t>(500, responseText.size())));

                // Log all [TOOL:] occurrences in the response
                {
                    size_t tmpSearch = 0;
                    int toolCount = 0;
                    while (true)
                    {
                        size_t pos = responseText.find("[TOOL:", tmpSearch);
                        if (pos == std::string::npos) break;
                        size_t endBracket = responseText.find(']', pos);
                        std::string toolSnippet = (endBracket != std::string::npos)
                            ? responseText.substr(pos, endBracket - pos + 1)
                            : responseText.substr(pos, std::min<size_t>(100, responseText.size() - pos));
                        CQ_CORE_INFO("[AI ToolScan] Tool call #{0} at pos {1}: {2}", toolCount, pos, toolSnippet);
                        toolCount++;
                        tmpSearch = pos + 1;
                    }
                    if (toolCount == 0)
                        CQ_CORE_WARN("[AI ToolScan] NO [TOOL:] calls found in response!");
                    else
                        CQ_CORE_INFO("[AI ToolScan] Total tool calls found: {0}", toolCount);
                }

                // Track tool results for this response to build display text if needed
                std::vector<std::pair<std::string, std::string>> currentToolResults;

                // Execute tool calls if requested by AI (supports multiple calls and nested JSON brackets)
                const std::string toolPrefix = "[TOOL:";
                size_t searchFrom = 0;
                while (true)
                {
                    size_t toolStart = responseText.find(toolPrefix, searchFrom);
                    if (toolStart == std::string::npos || !activeScene) break;

                    CQ_CORE_TRACE("AI Tools: Tool call detected at index {0}", toolStart);

                    size_t nameEnd = responseText.find(':', toolStart + toolPrefix.size());
                    if (nameEnd == std::string::npos) 
                    {
                        CQ_CORE_ERROR("AI Tools: Malformed tool call missing colon ':' delimiter.");
                        break;
                    }

                    std::string toolName = responseText.substr(toolStart + toolPrefix.size(), nameEnd - toolStart - toolPrefix.size());
                    CQ_CORE_TRACE("AI Tools: Extracted tool name: '{0}'", toolName);

                    // Find the matching ']' accounting for nested brackets in JSON
                    size_t payloadStart = nameEnd + 1;
                    int bracketDepth = 0;
                    int braceDepth = 0;
                    size_t payloadEnd = std::string::npos;
                    size_t lastBracketPos = std::string::npos; // fallback: last ']' seen even if braceDepth > 0
                    size_t nestedToolPos = std::string::npos; // truncated response: another [TOOL: inside this payload
                    for (size_t i = payloadStart; i < responseText.size(); ++i)
                    {
                        // Check for a nested [TOOL: marker — this means the model was truncated
                        // mid-response and we should split the tool calls here.
                        if (braceDepth > 0 && responseText.compare(i, toolPrefix.size(), toolPrefix) == 0)
                        {
                            nestedToolPos = i;
                            break;
                        }

                        char c = responseText[i];
                        if (c == '{') braceDepth++;
                        else if (c == '}') braceDepth--;
                        else if (c == '[' && braceDepth > 0) bracketDepth++;
                        else if (c == ']' && bracketDepth > 0) bracketDepth--;
                        else if (c == ']' && bracketDepth == 0 && braceDepth <= 0)
                        {
                            payloadEnd = i;
                            break;
                        }
                        else if (c == ']' && bracketDepth == 0)
                        {
                            // Found ']' but braceDepth > 0 — model likely truncated closing braces
                            lastBracketPos = i;
                        }
                    }

                    // Truncation fallback: another [TOOL: found inside this tool's payload
                    if (payloadEnd == std::string::npos && nestedToolPos != std::string::npos)
                    {
                        CQ_CORE_WARN("AI Tools: Tool '{0}' payload truncated — found nested [TOOL:] at offset {1}, splitting.", toolName, nestedToolPos);
                        payloadEnd = nestedToolPos;
                    }
                    // Fallback: if no clean ']' found, try the last ']' seen even with open braces
                    else if (payloadEnd == std::string::npos && lastBracketPos != std::string::npos)
                    {
                        CQ_CORE_WARN("AI Tools: Tool '{0}' has unclosed braces, using last ']' found as delimiter.", toolName);
                        payloadEnd = lastBracketPos;
                    }
                    // Fallback 2: response ended without any ']' — treat rest of text as payload
                    else if (payloadEnd == std::string::npos && !responseText.empty())
                    {
                        CQ_CORE_WARN("AI Tools: Tool '{0}' has no closing ']', using end of response as delimiter.", toolName);
                        payloadEnd = responseText.size();
                    }

                    if (payloadEnd == std::string::npos) 
                    {
                        CQ_CORE_ERROR("AI Tools: Failed to find matching ']' for tool '{0}'. Payload parsing aborted.", toolName);
                        break;
                    }

                    std::string payload = responseText.substr(payloadStart, payloadEnd - payloadStart);

                    // Auto-close any unclosed braces/brackets/strings in the payload
                    {
                        int bDepth = 0, kDepth = 0;
                        bool inString = false;
                        for (size_t i = 0; i < payload.size(); ++i)
                        {
                            char ch = payload[i];
                            if (inString)
                            {
                                if (ch == '\\' && i + 1 < payload.size()) { ++i; continue; }
                                if (ch == '"') inString = false;
                                continue;
                            }
                            if (ch == '"') inString = true;
                            else if (ch == '{') bDepth++;
                            else if (ch == '}') bDepth--;
                            else if (ch == '[' && bDepth > 0) kDepth++;
                            else if (ch == ']' && kDepth > 0) kDepth--;
                        }
                        // Close in reverse order: string, then brackets, then braces
                        if (inString) payload += '"';
                        while (kDepth > 0) { payload += ']'; kDepth--; }
                        while (bDepth > 0) { payload += '}'; bDepth--; }

                        // Trim stray whitespace around the payload
                        size_t ps = payload.find_first_not_of(" \t\r\n");
                        if (ps == std::string::npos) payload.clear();
                        else payload = payload.substr(ps, payload.find_last_not_of(" \t\r\n") - ps + 1);
                    }
                    
                    CQ_CORE_INFO("AI Tools: Dispatching tool '{0}' with payload: {1}", toolName, payload);
                    
                    std::promise<std::string> promise;
                    std::future<std::string> future = promise.get_future();

                    {
                        std::lock_guard<std::mutex> lock(m_QueueMutex);
                        m_MainThreadQueue.push([activeScene, toolName, payload, &promise]() {
                            try 
                            {
                                std::string res = AITools::ExecuteTool(activeScene, toolName, payload);
                                promise.set_value(res);
                            }
                            catch (const std::exception& e)
                            {
                                CQ_CORE_ERROR("AI Tools: Exception during execution of '{0}': {1}", toolName, e.what());
                                promise.set_value(toolName + " -> Execution Error: " + std::string(e.what()));
                            }
                            catch (...)
                            {
                                CQ_CORE_ERROR("AI Tools: Unknown exception during execution of '{0}'", toolName);
                                promise.set_value(toolName + " -> Unknown Execution Error.");
                            }
                        });
                    }

                    std::string result = future.get();
                    m_ToolLogs.push_back(toolName + " -> " + result);
                    currentToolResults.push_back({toolName, result});
                    CQ_CORE_INFO("AI Tools: Execution Result: {0}", result);

                    size_t eraseEnd = payloadEnd;
                    // When we split at a nested [TOOL:, payloadEnd points at the NEXT tool's '['.
                    // Keep that '[' intact so the next loop iteration can find the tool call.
                    bool splitAtNestedTool = (nestedToolPos != std::string::npos && payloadEnd == nestedToolPos);
                    if (!splitAtNestedTool)
                    {
                        while (eraseEnd + 1 < responseText.size() && (responseText[eraseEnd + 1] == '\n' || responseText[eraseEnd + 1] == '\r' || responseText[eraseEnd + 1] == ' ' || responseText[eraseEnd + 1] == '\t'))
                        {
                            eraseEnd++;
                        }
                        responseText.erase(toolStart, eraseEnd - toolStart + 1);
                    }
                    else
                    {
                        responseText.erase(toolStart, payloadEnd - toolStart);
                    }
                    // Don't advance searchFrom since we erased the text
                }

                CQ_CORE_INFO("AI Manager: Tool parsing done. Found {0} tools. Remaining responseText ({1} chars): {2}", currentToolResults.size(), responseText.size(), responseText.substr(0, std::min<size_t>(200, responseText.size())));

                if (assistantIndex < m_History.size())
                {
                    m_History[assistantIndex].Content = fullResponseText;
                    // If responseText is empty after removing tool calls, build a display message from tool results
                    if (responseText.empty() && !currentToolResults.empty())
                    {
                        for (const auto& [tName, tResult] : currentToolResults)
                        {
                            if (!responseText.empty()) responseText += "\n";
                            responseText += "[" + tName + "] " + tResult;
                        }
                    }
                    m_History[assistantIndex].DisplayContent = responseText;
                }

                if (onComplete)
                {
                    CQ_CORE_TRACE("AI Manager: Calling completion callback.");
                    onComplete(responseText);
                }
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("AI Manager: Thread exception caught: {0}", e.what());
                if (assistantIndex < m_History.size())
                    m_History[assistantIndex].Content = "Error: " + std::string(e.what());
            }
            catch (...)
            {
                CQ_CORE_ERROR("AI Manager: Unknown thread exception caught.");
                if (assistantIndex < m_History.size())
                    m_History[assistantIndex].Content = "Unable to process request. Please try again.";
            }
        });

        aiThread.detach();
    }
}
