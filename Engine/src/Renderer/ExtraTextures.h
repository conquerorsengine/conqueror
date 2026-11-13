#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>

// Texture 2D Array, Volume Texture ve Cube Render Target varlıkları (OpenGL dokuları) ile Material Parameter Collection.
// Shader Graph düğümleri bunları örnekler; Scene Capture Cube bileşeni küp hedefine çizer.
namespace Conqueror
{
    struct SceneCaptureCubeComponent
    {
        std::string Target;           // .cqcubert
        float Interval = 0.0f;        // 0: oyun başında bir kez, >0: bu kadar saniyede bir

        // Çalışma zamanı
        float Timer = 0.0f;
        bool Captured = false;
    };

    namespace ExtraTextures
    {
        // OpenGL doku adı; dosya değişince yeniden kurulur. 0: kurulamadı
        CQ_API uint32_t TextureArray(const std::string& path);
        CQ_API uint32_t Volume(const std::string& path);
        CQ_API uint32_t Cube(const std::string& path, int* size = nullptr);
        CQ_API void Shutdown();
        void RegisterComponents();
    }

    namespace MaterialParameterCollections
    {
        CQ_API bool GetScalar(const std::string& path, const std::string& name, float& out);
        CQ_API bool GetVector(const std::string& path, const std::string& name, glm::vec4& out);
        CQ_API void SetScalar(const std::string& path, const std::string& name, float value);
        CQ_API void SetVector(const std::string& path, const std::string& name, const glm::vec4& value);
        CQ_API void ResetOverrides();
        // Tek geçersiz kılmayı kaldır (değer varlıktakine döner)
        CQ_API void ClearOverride(const std::string& path, const std::string& name);
    }
}
