#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <vector>

namespace Conqueror
{
    // Shader Variant Collection (Unity): oyun açılırken önceden derlenecek shader'lar (malzemeler, kod shader'ları,
    // Image Effect / Compute shader'ları). İlk kullanımda derleme takılmasını (hitch) önler. Project Settings >
    // Graphics > Preloaded Shaders listesindeki koleksiyonlar oyun başında ısıtılır; Inspector'da "Warm Up" düğmesi.
    namespace ShaderVariantCollections
    {
        constexpr const char* Extension = ".cqshadervariants";
        CQ_API bool Load(const std::string& path, std::vector<std::string>& entries);
        CQ_API bool Save(const std::string& path, const std::vector<std::string>& entries);
        // Koleksiyondaki her şeyi derle; derlenen sayısı (errors: derlenemeyenler)
        CQ_API int WarmUp(const std::string& path, std::vector<std::string>* errors = nullptr);
        // Project Settings > Graphics > Preloaded Shaders (bir kez; sonraki çağrılar boş geçer)
        CQ_API int WarmUpPreloaded();
        CQ_API void ResetPreloaded();
    }
}
