#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Fog Volume (Godot FogVolume, Unreal Local Fog Volume): hacimsel sise (Lighting > Post > Volumetric Fog) yerel yoğunluk ekler —
// kutu, elipsoit, silindir, koni ya da her yer. Negatif yoğunluk sisi oyar (mağara girişi, ev içi). Kenar yumuşaması, yükseklikle
// incelme, kendi rengi (albedo) ve ışıması (kendinden parlayan sis). Hacimsel sis kapalıyken etkisizdir (Godot gibi); genel yoğunluk
// 0 yapılırsa yalnız hacimler görünür
namespace Conqueror
{
    class Scene;

    enum class FogVolumeShape : int { Box = 0, Ellipsoid, Cylinder, Cone, World };

    struct FogVolumeComponent
    {
        FogVolumeShape Shape = FogVolumeShape::Box;
        glm::vec3 Size{ 4.0f, 2.0f, 4.0f };      // yerel tam boyut (ölçekle çarpılır)
        float Density = 0.2f;                    // birim başına (negatif: sisi oyar)
        glm::vec3 Albedo{ 1.0f };
        glm::vec3 Emission{ 0.0f };              // kendinden ışık (yoğunlukla orantılı)
        float HeightFalloff = 0.0f;              // hacmin tabanından yukarı incelme
        float EdgeFade = 0.1f;                   // kenarlarda yumuşak geçiş payı (0..1)
    };

    namespace FogVolumes
    {
        constexpr int MaxVolumes = 32;

        // Shader düzeni: yerel uzaya dönüşüm (dünya -> birim hacim), parametreler
        struct GPUVolume
        {
            glm::mat4 WorldToUnit;               // nokta -> [-1, 1] küp
            glm::vec4 Params;                    // x şekil, y yoğunluk, z yükseklik incelmesi (birim uzay), w kenar
            glm::vec4 Albedo;
            glm::vec4 Emission;
        };

        // Görünümün (Scene::RenderScene3D) etkin hacimleri; hacimsel sis geçişi okur
        CQ_API void Collect(Scene& scene);
        CQ_API const std::vector<GPUVolume>& GetFrameVolumes();
    }

    void RegisterFogVolumeComponents();
}
