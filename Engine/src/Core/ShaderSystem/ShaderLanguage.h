#pragma once

#include <string>

namespace Conqueror
{
    enum class ShaderLanguage
    {
        GLSL = 0,
        HLSL
    };

    inline const char* ShaderLanguageToString(ShaderLanguage lang)
    {
        switch (lang)
        {
            case ShaderLanguage::GLSL: return "GLSL";
            case ShaderLanguage::HLSL: return "HLSL";
        }
        return "Unknown";
    }

    inline ShaderLanguage ShaderLanguageFromString(const std::string& str)
    {
        if (str == "GLSL") return ShaderLanguage::GLSL;
        if (str == "HLSL") return ShaderLanguage::HLSL;
        return ShaderLanguage::GLSL;
    }
}
