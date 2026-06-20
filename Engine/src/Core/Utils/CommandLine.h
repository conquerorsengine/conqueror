#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Conqueror
{
    // Command line argument parser
    class CQ_API CommandLine
    {
    public:
        static void Parse(int argc, char** argv);
        
        // Check if argument exists
        static bool HasArgument(const std::string& name);
        
        // Get argument value
        static std::string GetArgument(const std::string& name, const std::string& defaultValue = "");
        static int GetArgumentInt(const std::string& name, int defaultValue = 0);
        static float GetArgumentFloat(const std::string& name, float defaultValue = 0.0f);
        static bool GetArgumentBool(const std::string& name, bool defaultValue = false);
        
        // Get all arguments
        static const std::unordered_map<std::string, std::string>& GetAllArguments() { return s_Arguments; }
        
        // Get positional arguments (non-flag arguments)
        static const std::vector<std::string>& GetPositionalArguments() { return s_PositionalArgs; }
        
        // Clear all arguments
        static void Clear();
        
        // Get executable path
        static const std::string& GetExecutablePath() { return s_ExecutablePath; }

    private:
        static std::unordered_map<std::string, std::string> s_Arguments;
        static std::vector<std::string> s_PositionalArgs;
        static std::string s_ExecutablePath;
    };
}
