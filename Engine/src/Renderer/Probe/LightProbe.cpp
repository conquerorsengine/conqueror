#include "LightProbe.h"
#include "Scene/Scene.h"
#include "Core/Logging/Log.h"

#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Conqueror
{
    LightProbe::LightProbe(const LightProbeSettings& settings)
        : m_Settings(settings)
    {
    }

    void LightProbe::Bake(Scene* scene)
    {
        if (!scene)
            return;

        CQ_CORE_INFO("Baking light probe at ({0}, {1}, {2})",
                      m_Settings.Position.x, m_Settings.Position.y, m_Settings.Position.z);

        // Cubemap data'si topla (basitlestirilmis)
        uint32_t res = m_Settings.Resolution;
        std::vector<glm::vec3> cubemapData(res * res * 6);

        // Bu su an basitlestirilmis - gercektesinde render yapilmali
        // Simdi default degerler kullanalim
        for (auto& color : cubemapData)
        {
            color = glm::vec3(0.3f, 0.4f, 0.5f); // Default ambient
        }

        // SH'ye donustur
        SHFromCubemap(cubemapData, res);

        CQ_CORE_INFO("Light probe bake complete");
    }

    glm::vec3 LightProbe::EvaluateIrradiance(const glm::vec3& normal) const
    {
        // SH L2 evaluate
        float x = normal.x;
        float y = normal.y;
        float z = normal.z;

        glm::vec3 irradiance = glm::vec3(0.0f);

        // Band 0
        irradiance += m_SH.L00 * 0.282095f;

        // Band 1
        irradiance += m_SH.L1_1 * 0.488603f * y;
        irradiance += m_SH.L10  * 0.488603f * z;
        irradiance += m_SH.L11  * 0.488603f * x;

        // Band 2
        irradiance += m_SH.L2_2 * 1.092548f * x * y;
        irradiance += m_SH.L2_1 * 1.092548f * y * z;
        irradiance += m_SH.L20  * 0.315392f * (3.0f * z * z - 1.0f);
        irradiance += m_SH.L21  * 1.092548f * x * z;
        irradiance += m_SH.L22  * 0.546274f * (x * x - y * y);

        return glm::max(irradiance, glm::vec3(0.0f));
    }

    void LightProbe::SHFromCubemap(const std::vector<glm::vec3>& cubemapData, uint32_t resolution)
    {
        // SH coefficients'lari hesapla (basitlestirilmis)
        // Gercektesinde cubemap her pikseli icin SH integrali yapilir

        // Default degerler (sonra gercek bake ile degistirilecek)
        m_SH.L00  = glm::vec3(0.5f, 0.5f, 0.5f);
        m_SH.L1_1 = glm::vec3(0.1f, 0.1f, 0.1f);
        m_SH.L10  = glm::vec3(0.2f, 0.2f, 0.2f);
        m_SH.L11  = glm::vec3(0.1f, 0.1f, 0.1f);
        m_SH.L2_2 = glm::vec3(0.0f);
        m_SH.L2_1 = glm::vec3(0.0f);
        m_SH.L20  = glm::vec3(0.0f);
        m_SH.L21  = glm::vec3(0.0f);
        m_SH.L22  = glm::vec3(0.0f);
    }

    std::shared_ptr<LightProbe> LightProbe::Create(const LightProbeSettings& settings)
    {
        return std::make_shared<LightProbe>(settings);
    }
}
