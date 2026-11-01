#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <memory>

namespace Conqueror
{
    struct ProceduralSkySettings
    {
        glm::vec3 SunDirection = glm::vec3(-0.4f, -0.8f, -0.3f); // ışığın gittiği yön
        glm::vec3 SunColor = glm::vec3(1.0f, 0.96f, 0.9f);
        float SunIntensity = 1.0f;
        glm::vec3 ZenithColor = glm::vec3(0.16f, 0.36f, 0.78f);
        glm::vec3 HorizonColor = glm::vec3(0.65f, 0.78f, 0.92f);
        glm::vec3 GroundColor = glm::vec3(0.30f, 0.28f, 0.26f);
        float Turbidity = 2.0f;

        // Fiziksel atmosfer (Sky Atmosphere): Rayleigh + Mie + ozon tek saçılma
        bool Physical = false;
        float Rayleigh = 1.0f;
        float Mie = 1.0f;
        float MieAnisotropy = 0.8f;
        float Ozone = 1.0f;
        float SunDisk = 1.0f;
        glm::vec3 GroundAlbedo = glm::vec3(0.12f);

        bool operator==(const ProceduralSkySettings& o) const;
    };

    // Görüntü tabanlı aydınlatma verisi:
    //  - difüz: L2 küresel harmonikler (kosinüs lobu ile konvolüsyonlu, /π)
    //  - speküler: GGX ile ön filtrelenmiş mip zincirli küp harita
    //  - BRDF LUT: tüm ortamlar için paylaşılan 2D tablo
    class CQ_API EnvironmentMap
    {
    public:
        ~EnvironmentMap();

        // Mevcut bir HDR küp haritadan üretir (GL bağlamı gerekli)
        static std::shared_ptr<EnvironmentMap> FromCubemap(uint32_t cubemapID, uint32_t resolution);
        // Prosedürel gökyüzü küp haritası çizer ve ondan IBL üretir
        static std::shared_ptr<EnvironmentMap> FromProceduralSky(const ProceduralSkySettings& settings, uint32_t resolution = 128);

        uint32_t GetPrefilteredID() const { return m_Prefiltered; }
        float GetPrefilteredMaxMip() const { return (float)(m_PrefilteredMips - 1); }
        const std::array<glm::vec3, 9>& GetIrradianceSH() const { return m_SH; }
        // Prosedürel gökyüzünde skybox olarak çizilecek küp (yoksa 0)
        uint32_t GetSkyCubemapID() const { return m_OwnedSky; }

        static uint32_t GetBRDFLUT();
        static void Shutdown();

    private:
        EnvironmentMap() = default;
        void ComputeSH(uint32_t cubemapID, uint32_t resolution);
        void Prefilter(uint32_t cubemapID, uint32_t resolution);

        uint32_t m_Prefiltered = 0;
        int m_PrefilteredMips = 1;
        uint32_t m_OwnedSky = 0;
        std::array<glm::vec3, 9> m_SH{};
    };
}
