#pragma once

#include <cstdint>

namespace Conqueror
{
    // Doku birimi ataması (shader tarafı: Shaders/Include/Lighting.glsl)
    namespace TextureSlot
    {
        constexpr uint32_t Albedo = 0;
        constexpr uint32_t Normal = 1;
        constexpr uint32_t Metallic = 2;
        constexpr uint32_t Roughness = 3;
        constexpr uint32_t AO = 4;
        constexpr uint32_t Emission = 5;
        constexpr uint32_t GraphFirst = 8;     // Shader Graph dokuları 8..11
        constexpr uint32_t ShadowAtlas = 12;
        constexpr uint32_t IBLPrefiltered = 13;
        constexpr uint32_t BRDFLut = 14;
        constexpr uint32_t Lightmap = 15;
        constexpr uint32_t ReflectionProbeFirst = 16; // 16..19
        constexpr uint32_t APVPositions = 20;
        constexpr uint32_t APVSH = 21;
        constexpr uint32_t PlanarReflection = 22;
        constexpr uint32_t MaxReflectionProbes = 4;
    }

    namespace UniformBinding
    {
        constexpr uint32_t Frame = 0;
        constexpr uint32_t Lights = 1;
        constexpr uint32_t PostProcessConstants = 7;   // son geçiş hesaplama gölgelendiricilerinin sabitleri (NIS ...)
    }
}
