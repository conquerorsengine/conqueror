#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    struct LightProbeSettings
    {
        glm::vec3 Position = glm::vec3(0.0f);
        uint32_t Resolution = 32;
        int Bounces = 2;
    };

    // Spherical Harmonics L2 coefficients (9 per channel)
    struct SHCoefficients
    {
        glm::vec3 L00  = glm::vec3(0.0f);   // Band 0
        glm::vec3 L1_1 = glm::vec3(0.0f);   // Band 1
        glm::vec3 L10  = glm::vec3(0.0f);
        glm::vec3 L11  = glm::vec3(0.0f);
        glm::vec3 L2_2 = glm::vec3(0.0f);   // Band 2
        glm::vec3 L2_1 = glm::vec3(0.0f);
        glm::vec3 L20  = glm::vec3(0.0f);
        glm::vec3 L21  = glm::vec3(0.0f);
        glm::vec3 L22  = glm::vec3(0.0f);
    };

    class CQ_API LightProbe
    {
    public:
        LightProbe(const LightProbeSettings& settings = {});
        ~LightProbe() = default;

        void Bake(Scene* scene);
        const SHCoefficients& GetSH() const { return m_SH; }

        // SH'den irradiance hesaplama
        glm::vec3 EvaluateIrradiance(const glm::vec3& normal) const;

        const LightProbeSettings& GetSettings() const { return m_Settings; }
        void SetSettings(const LightProbeSettings& settings) { m_Settings = settings; }

        static std::shared_ptr<LightProbe> Create(const LightProbeSettings& settings = {});

    private:
        void SHFromCubemap(const std::vector<glm::vec3>& cubemapData, uint32_t resolution);

        LightProbeSettings m_Settings;
        SHCoefficients m_SH;
    };
}
