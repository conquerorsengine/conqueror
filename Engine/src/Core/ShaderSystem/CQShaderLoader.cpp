#include "CQShaderLoader.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/FileSystem.h"

#include <sstream>

namespace Conqueror
{
    CQShaderSource CQShaderLoader::Load(const std::string& filepath)
    {
        std::string rawContent;
        if (!FileSystem::ReadFile(filepath, rawContent))
        {
            CQ_CORE_ERROR("CQShaderLoader: Could not read file '{0}'", filepath);
            return {};
        }

        return ParseHeader(rawContent);
    }

    CQShaderSource CQShaderLoader::ParseHeader(const std::string& rawSource)
    {
        CQShaderSource result;
        result.Language = ShaderLanguage::GLSL;

        std::istringstream stream(rawSource);
        std::string line;

        bool headerParsed = false;
        std::string cleanSource;

        while (std::getline(stream, line))
        {
            if (!headerParsed)
            {
                std::string trimmed = line;
                // Trim leading whitespace
                size_t start = trimmed.find_first_not_of(" \t");
                if (start != std::string::npos)
                    trimmed = trimmed.substr(start);

                if (trimmed.rfind("#language", 0) == 0)
                {
                    // Extract language name after "#language "
                    size_t spacePos = trimmed.find(' ');
                    if (spacePos != std::string::npos)
                    {
                        std::string langStr = trimmed.substr(spacePos + 1);
                        // Trim trailing whitespace
                        size_t end = langStr.find_last_not_of(" \t\r\n");
                        if (end != std::string::npos)
                            langStr = langStr.substr(0, end + 1);
                        result.Language = ShaderLanguageFromString(langStr);
                    }
                    headerParsed = true;
                    continue;
                }
                else
                {
                    headerParsed = true;
                }
            }

            cleanSource += line + "\n";
        }

        result.Source = cleanSource;
        return result;
    }
}
