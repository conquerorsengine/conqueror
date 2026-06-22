#pragma once

#include "LightmapBaker.h"

namespace Conqueror
{
    // Scene serializer icin lightmap verileri
    struct LightmapData
    {
        bool HasLightmap = false;
        std::string LightmapPath;
        uint32_t Width = 0;
        uint32_t Height = 0;
        LightmapSettings Settings;
    };
}
