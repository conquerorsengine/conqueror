#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    // Lightmap Parameters varlığı (Unity Lightmap Parameters): nesne başına pişirme kalitesi.
    // Lighting Settings'te varsayılanı, Mesh Renderer'da nesneye özel olanı seçilir.
    struct LightmapParameters
    {
        int AntiAliasingSamples = 1;      // texel başına alt örnek (kenarlarda tırtık azalır)
        float BackfaceTolerance = 0.9f;   // ışınların bu oranı ön yüze çarpmalı; yoksa texel geçersiz (komşulardan doldurulur)
        float Pushoff = 0.02f;            // ışın başlangıcı yüzeyden bu kadar (m) uzaklaştırılır (gölge sivilcesi / sızıntı dengesi)
        float IndirectSampleScale = 1.0f; // dolaylı ışık örnek sayısı çarpanı
    };

    namespace LightmapParameterAssets
    {
        constexpr const char* Extension = ".cqlmparams";
        // Önbellekli (dosya değişince yeniden okunur); bulunamazsa false ve varsayılanlar
        CQ_API bool Load(const std::string& path, LightmapParameters& out);
        CQ_API bool Save(const std::string& path, const LightmapParameters& params);
    }
}
