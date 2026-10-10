#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <filesystem>

namespace Conqueror::AI
{
    struct AIAuthCredentials
    {
        std::string ProviderName = "Antigravity";
        std::string ApiKey;
        std::string SelectedModel = "gemini-3.1-pro-low";
        bool IsLoggedIn = false;
    };

    class CQ_API AIAuth
    {
    public:
        static void SaveCredentials(const AIAuthCredentials& creds);
        static AIAuthCredentials LoadCredentials();
        static std::filesystem::path GetAuthFilePath();
        static void ClearCredentials();
    };
}
