#include "AIAuth.h"
#include "Core/Logging/Log.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace Conqueror::AI
{
    std::filesystem::path AIAuth::GetAuthFilePath()
    {
        const char* homeDir = std::getenv("HOME");
        if (!homeDir)
            homeDir = ".";
        
        std::filesystem::path dir = std::filesystem::path(homeDir) / ".conqueror_engine";
        std::filesystem::create_directories(dir);
        return dir / "ai_auth.json";
    }

    void AIAuth::SaveCredentials(const AIAuthCredentials& creds)
    {
        try
        {
            auto path = GetAuthFilePath();
            std::ofstream file(path);
            if (!file.is_open()) return;

            file << "{\n";
            file << "  \"provider\": \"" << creds.ProviderName << "\",\n";
            file << "  \"apiKey\": \"" << creds.ApiKey << "\",\n";
            file << "  \"selectedModel\": \"" << creds.SelectedModel << "\",\n";
            file << "  \"isLoggedIn\": " << (creds.IsLoggedIn ? "true" : "false") << "\n";
            file << "}\n";
            file.close();
            CQ_CORE_INFO("AI credentials saved to {0}", path.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to save AI credentials: {0}", e.what());
        }
    }

    AIAuthCredentials AIAuth::LoadCredentials()
    {
        AIAuthCredentials creds;
        try
        {
            auto path = GetAuthFilePath();
            if (!std::filesystem::exists(path)) return creds;

            std::ifstream file(path);
            if (!file.is_open()) return creds;

            std::string line;
            while (std::getline(file, line))
            {
                if (line.find("\"provider\":") != std::string::npos)
                {
                    size_t first = line.find('"', line.find(':'));
                    size_t last = line.rfind('"');
                    if (first != std::string::npos && last != std::string::npos && last > first)
                    {
                        creds.ProviderName = line.substr(first + 1, last - first - 1);
                    }
                }
                else if (line.find("\"apiKey\":") != std::string::npos)
                {
                    size_t first = line.find('"', line.find(':'));
                    size_t last = line.rfind('"');
                    if (first != std::string::npos && last != std::string::npos && last > first)
                    {
                        creds.ApiKey = line.substr(first + 1, last - first - 1);
                    }
                }
                else if (line.find("\"selectedModel\":") != std::string::npos)
                {
                    size_t first = line.find('"', line.find(':'));
                    size_t last = line.rfind('"');
                    if (first != std::string::npos && last != std::string::npos && last > first)
                    {
                        creds.SelectedModel = line.substr(first + 1, last - first - 1);
                    }
                }
                else if (line.find("\"isLoggedIn\": true") != std::string::npos)
                {
                    creds.IsLoggedIn = true;
                }
            }
            file.close();
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load AI credentials: {0}", e.what());
        }
        return creds;
    }

    void AIAuth::ClearCredentials()
    {
        try
        {
            auto path = GetAuthFilePath();
            if (std::filesystem::exists(path))
            {
                std::filesystem::remove(path);
            }
        }
        catch (...) {}
    }
}
