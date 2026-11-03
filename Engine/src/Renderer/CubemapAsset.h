#pragma once

#include "Core/Base/Base.h"

#include <string>

// Cubemap varlığı (.cqcubemap, Unity Create > Legacy > Cubemap): altı yüz dokusu (+X -X +Y -Y +Z -Z) ya da bir panorama
// (equirectangular HDR / LDR). Cubemap::CreateFromEquirectangular bu dosyayı da kabul eder: Skybox (Lighting penceresi,
// Camera Skybox, SetSkybox) .cqcubemap ile çalışır; sahne dosyası bu yolu saklar.
namespace Conqueror
{
    struct CubemapAssetData
    {
        enum class SourceKind : int { Faces = 0, Panorama };
        SourceKind Source = SourceKind::Faces;
        std::string Faces[6];      // proje göreli; sıra +X -X +Y -Y +Z -Z
        std::string Panorama;
        int Resolution = 512;      // panoramadan çevrilen yüz boyutu
    };

    namespace CubemapAssets
    {
        inline constexpr const char* Extension = ".cqcubemap";
        CQ_API bool IsAsset(const std::string& path);
        CQ_API bool Load(const std::string& path, CubemapAssetData& out, std::string* error = nullptr);
        CQ_API bool Save(const std::string& path, const CubemapAssetData& data);
        CQ_API const char* FaceName(int face);   // "+X (Right)" ...
    }
}
