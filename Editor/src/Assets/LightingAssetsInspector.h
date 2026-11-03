#pragma once

#include <string>

namespace Conqueror::Editor::LightingAssetsInspector
{
    // Lighting Settings (.cqlighting) ve Lightmap Parameters (.cqlmparams) varlıklarının Inspector'ı
    void DrawLightingSettings(const std::string& path);
    void DrawLightmapParameters(const std::string& path);
}
