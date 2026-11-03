#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Conqueror
{
    // Compute Shader (Unity ComputeShader): GPU'da genel amaçlı hesap. Dosya (.comp) GLSL compute shader'ıdır;
    // betikler parametreleri verip çalıştırır:
    //   ComputeSetTexture("Assets/Noise.comp", "u_Result", "Assets/Noise.cqrt");
    //   ComputeSetFloat("Assets/Noise.comp", "u_Time", Time());
    //   ComputeDispatch("Assets/Noise.comp", 32, 32, 1);
    // image2D uniform'larına render texture (rgba8) yazılır; sampler2D uniform'larına herhangi bir doku okunur.
    namespace ComputeShaders
    {
        constexpr const char* Extension = ".comp";
        CQ_API bool SetTexture(const std::string& shader, const std::string& name, const std::string& texturePath);
        CQ_API bool SetFloat(const std::string& shader, const std::string& name, float value);
        CQ_API bool SetInt(const std::string& shader, const std::string& name, int value);
        CQ_API bool SetVector(const std::string& shader, const std::string& name, const glm::vec4& value);
        // Grup sayısı (x, y, z); başarısızsa false (GetError)
        CQ_API bool Dispatch(const std::string& shader, int groupsX, int groupsY, int groupsZ);
        CQ_API std::string GetError(const std::string& shader);
        // Derleme ve yansıtma (Inspector): iş parçacığı grubu boyutu ve uniform adları
        struct Info { bool Compiled = false; glm::ivec3 GroupSize{ 0 }; std::vector<std::pair<std::string, std::string>> Uniforms; std::string Error; };
        CQ_API Info Inspect(const std::string& shader);
        CQ_API void Shutdown();
    }
}
