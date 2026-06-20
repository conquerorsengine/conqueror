#include "CommandLine.h"
#include "Core/Logging/Log.h"
#include <sstream>

namespace Conqueror
{
    std::unordered_map<std::string, std::string> CommandLine::s_Arguments;
    std::vector<std::string> CommandLine::s_PositionalArgs;
    std::string CommandLine::s_ExecutablePath;

    void CommandLine::Parse(int argc, char** argv)
    {
        if (argc > 0)
            s_ExecutablePath = argv[0];

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            
            // Flag argument (--name=value veya --name value)
            if (arg.rfind("--", 0) == 0)
            {
                size_t equalPos = arg.find('=');
                if (equalPos != std::string::npos)
                {
                    // --name=value formatı
                    std::string name = arg.substr(2, equalPos - 2);
                    std::string value = arg.substr(equalPos + 1);
                    s_Arguments[name] = value;
                }
                else
                {
                    // --name value formatı veya sadece --flag
                    std::string name = arg.substr(2);
                    
                    if (i + 1 < argc && argv[i + 1][0] != '-')
                    {
                        s_Arguments[name] = argv[i + 1];
                        ++i;
                    }
                    else
                    {
                        s_Arguments[name] = "true";
                    }
                }
            }
            // Short flag (-n value)
            else if (arg.rfind("-", 0) == 0 && arg.length() > 1)
            {
                std::string name = arg.substr(1);
                
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    s_Arguments[name] = argv[i + 1];
                    ++i;
                }
                else
                {
                    s_Arguments[name] = "true";
                }
            }
            // Positional argument
            else
            {
                s_PositionalArgs.push_back(arg);
            }
        }
    }

    bool CommandLine::HasArgument(const std::string& name)
    {
        return s_Arguments.find(name) != s_Arguments.end();
    }

    std::string CommandLine::GetArgument(const std::string& name, const std::string& defaultValue)
    {
        auto it = s_Arguments.find(name);
        return (it != s_Arguments.end()) ? it->second : defaultValue;
    }

    int CommandLine::GetArgumentInt(const std::string& name, int defaultValue)
    {
        auto it = s_Arguments.find(name);
        if (it != s_Arguments.end())
        {
            try
            {
                return std::stoi(it->second);
            }
            catch (...)
            {
                CQ_CORE_WARN("CommandLine: Failed to parse '{}' as int", name);
            }
        }
        return defaultValue;
    }

    float CommandLine::GetArgumentFloat(const std::string& name, float defaultValue)
    {
        auto it = s_Arguments.find(name);
        if (it != s_Arguments.end())
        {
            try
            {
                return std::stof(it->second);
            }
            catch (...)
            {
                CQ_CORE_WARN("CommandLine: Failed to parse '{}' as float", name);
            }
        }
        return defaultValue;
    }

    bool CommandLine::GetArgumentBool(const std::string& name, bool defaultValue)
    {
        auto it = s_Arguments.find(name);
        if (it != s_Arguments.end())
        {
            std::string value = it->second;
            if (value == "true" || value == "1" || value == "yes" || value == "on")
                return true;
            if (value == "false" || value == "0" || value == "no" || value == "off")
                return false;
        }
        return defaultValue;
    }

    void CommandLine::Clear()
    {
        s_Arguments.clear();
        s_PositionalArgs.clear();
        s_ExecutablePath.clear();
    }
}
