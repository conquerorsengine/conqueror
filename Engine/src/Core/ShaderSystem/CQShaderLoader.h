#pragma once

#include "Core/Base/Base.h"
#include "Core/ShaderSystem/ShaderLanguage.h"

#include <string>
#include <vector>

namespace Conqueror
{
    struct CQShaderSource
    {
        ShaderLanguage Language = ShaderLanguage::GLSL;
        std::string Source;
        // Kaynağa katılan tüm dosyalar (hot-reload ve önbellek anahtarı için)
        std::vector<std::string> Dependencies;
    };

    // .cqsh yükleyici
    //   #language GLSL               -> dil başlığı (ilk satır, isteğe bağlı)
    //   #include "Common/Lighting.glsl"
    //       önce shader'ın klasörüne, sonra Engine/src/Shaders köküne göre çözülür;
    //       her dosya bir kez eklenir (include guard gerekmez).
    class CQ_API CQShaderLoader
    {
    public:
        static CQShaderSource Load(const std::string& filepath);
        // Bellekteki kaynağı (Shader Editor) include çözümleyerek işler
        static CQShaderSource LoadFromString(const std::string& source, const std::string& virtualPath = "");
    private:
        static CQShaderSource ParseHeader(const std::string& rawSource);
    };
}
