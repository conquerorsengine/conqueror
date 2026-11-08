#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Conqueror
{
    // std140 düzeni: Shaders/Include/FrameData.glsl ile BİREBİR aynı olmalı.
    // Tüm üyeler vec4/mat4 olduğundan C++ düzeni std140 ile örtüşür.
    constexpr int kMaxCascades = 4;
    constexpr int kMaxPointLights = 64;
    constexpr int kMaxSpotLights = 32;
    constexpr int kMaxAreaLights = 16;

    struct FrameUBO
    {
        glm::mat4 View;
        glm::mat4 Projection;
        glm::mat4 ViewProjection;
        glm::mat4 InvView;
        glm::mat4 InvProjection;
        glm::vec4 CameraPosTime;
        glm::vec4 ScreenSize;

        glm::vec4 AmbientSkyColor;
        glm::vec4 AmbientEquatorColor;
        glm::vec4 AmbientGroundColor;
        glm::ivec4 AmbientParams;
        glm::vec4 IrradianceSH[9];
        glm::vec4 IBLParams;

        glm::vec4 FogColor;
        glm::vec4 FogParams;
        glm::vec4 FogParams2;

        glm::vec4 SunDirection;
        glm::vec4 SunColor;

        glm::vec4 ShadowParams;
        glm::vec4 ShadowParams2;
        glm::vec4 ShadowColor;
        glm::mat4 LightSpace[kMaxCascades];
        glm::vec4 CascadeSplits;
        glm::vec4 CascadeTexelWorld;
        glm::vec4 CascadeRects[kMaxCascades];

        glm::ivec4 FeatureFlags;
        glm::ivec4 DebugParams;   // x: görünüm modu (0 normal, 1 unlit, 2 normals, 3 lighting only, 4 metal/rough, 5 AO), y: alan ışığı sayısı
        glm::vec4 RenderParams;   // x: malzeme dokusu mip sapması (yükseltici iç çözünürlüğü: log2(ölçek), yerelde 0)
    };
    static_assert(sizeof(FrameUBO) == 1104, "FrameUBO must match std140 CQ_FrameBlock");

    struct GPUPointLight
    {
        glm::vec4 PositionRange;
        glm::vec4 ColorIntensity;
        glm::vec4 Attenuation;
    };

    struct GPUSpotLight
    {
        glm::vec4 PositionRange;
        glm::vec4 ColorIntensity;
        glm::vec4 DirectionCosOuter;
        glm::vec4 AttenuationCosInner;
    };

    // Dikdörtgen / disk alan ışığı: merkez + yarım genişlik / yükseklik vektörleri, ışık +Z (sağ x yukarı) yönüne yayar
    struct GPUAreaLight
    {
        glm::vec4 PositionRange;
        glm::vec4 ColorIntensity;     // w: iki yüzlü
        glm::vec4 Right;              // xyz yarım genişlik vektörü, w: şekil (0 dikdörtgen, 1 disk)
        glm::vec4 Up;                 // xyz yarım yükseklik vektörü
    };

    struct LightsUBO
    {
        GPUPointLight PointLights[kMaxPointLights];
        GPUSpotLight SpotLights[kMaxSpotLights];
        GPUAreaLight AreaLights[kMaxAreaLights];
    };
    static_assert(sizeof(LightsUBO) == kMaxPointLights * 48 + kMaxSpotLights * 64 + kMaxAreaLights * 64, "LightsUBO must match std140 CQ_LightBlock");
}
