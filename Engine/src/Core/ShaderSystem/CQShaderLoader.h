#pragma once

#include "Core/Base/Base.h"
#include "Core/ShaderSystem/ShaderLanguage.h"

#include <string>

namespace Conqueror
{
    struct CQShaderSource
    {
        ShaderLanguage Language = ShaderLanguage::GLSL;
        std::string Source;
    };

    class CQ_API CQShaderLoader
    {
    public:
        static CQShaderSource Load(const std::string& filepath);
    private:
        static CQShaderSource ParseHeader(const std::string& rawSource);
    };
}
