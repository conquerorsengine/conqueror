#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RenderSettings.h"

#include <glm/glm.hpp>

// Unity Volume / Unreal Post Process Volume: kamera hacmin içine girdikçe post-processing ayarları
// (pozlama, bloom, renk düzenleme, vinyet, renk sapması, film greni, AO, alan derinliği) yumuşakça değişir.
// Global hacimler her yerde etkilidir; öncelik sırası düşükten yükseğe uygulanır.
namespace Conqueror
{
    class Scene;

    enum class VolumeShape : int { Global = 0, Box, Sphere };

    struct PostProcessVolumeComponent
    {
        VolumeShape Shape = VolumeShape::Box;
        glm::vec3 Size{ 10.0f, 10.0f, 10.0f };   // kutunun tam boyu (yerel, ölçekle çarpılır)
        float Radius = 5.0f;                      // küre
        float BlendDistance = 2.0f;               // dışarıda bu mesafe boyunca solar
        float Weight = 1.0f;                      // 0..1
        int Priority = 0;                         // yüksek olan sonra uygulanır

        bool OverrideExposure = false;
        float Exposure = 0.0f;                    // EV

        bool OverrideBloom = false;
        float BloomIntensity = 0.6f;
        float BloomThreshold = 1.0f;
        glm::vec3 BloomTint{ 1.0f };

        bool OverrideColorGrading = false;
        float Contrast = 1.0f;
        float Saturation = 1.0f;
        float Temperature = 0.0f;
        float Tint = 0.0f;
        glm::vec3 ColorFilter{ 1.0f };            // kazanç (Gain)

        bool OverrideVignette = false;
        float VignetteIntensity = 0.35f;
        float VignetteSmoothness = 0.4f;
        glm::vec3 VignetteColor{ 0.0f };

        bool OverrideChromaticAberration = false;
        float ChromaticAberrationIntensity = 0.3f;

        bool OverrideFilmGrain = false;
        float FilmGrainIntensity = 0.15f;

        bool OverrideAmbientOcclusion = false;
        float AOIntensity = 1.0f;

        // Alan derinliği (Unreal Post Process Volume / Unity Volume Depth Of Field): sinematiklerde netlik kaydırma
        bool OverrideDepthOfField = false;
        float FocusDistance = 10.0f;              // m
        float Aperture = 2.8f;                    // f-stop (küçük = daha fazla bulanıklık)
        float FocalLength = 50.0f;                // mm

        // Çalışma zamanı: son hesaplanan etki (0..1)
        float CurrentInfluence = 0.0f;
    };

    namespace PostProcessVolumes
    {
        // Kameranın konumuna göre sahnenin temel ayarlarına hacimleri harmanlar
        CQ_API PostProcessSettings Resolve(Scene& scene, const PostProcessSettings& base, const glm::vec3& cameraPosition);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterPostProcessVolumeComponents();
}
