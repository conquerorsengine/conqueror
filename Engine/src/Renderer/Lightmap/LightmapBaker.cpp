#include "LightmapBaker.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace Conqueror
{
    LightmapBaker::LightmapBaker(const LightmapSettings& settings)
        : m_Settings(settings)
    {
    }

    void LightmapBaker::Bake(Scene* scene)
    {
        if (!scene || m_IsBaking)
            return;

        m_IsBaking = true;
        m_Progress = 0.0f;

        CQ_CORE_INFO("Starting lightmap bake...");

        // 1. UV2 generation
        CQ_CORE_INFO("Step 1: Generating UV2 coordinates...");
        GenerateUV2();
        m_Progress = 0.1f;

        // 2. Direct lighting
        CQ_CORE_INFO("Step 2: Baking direct lighting...");
        BakeDirectLighting(scene);
        m_Progress = 0.4f;

        // 3. Indirect lighting
        CQ_CORE_INFO("Step 3: Baking indirect lighting...");
        BakeIndirectLighting(scene);
        m_Progress = 0.7f;

        // 4. Ambient occlusion
        if (m_Settings.AmbientOcclusion)
        {
            CQ_CORE_INFO("Step 4: Baking ambient occlusion...");
            BakeAmbientOcclusion(scene);
        }
        m_Progress = 0.8f;

        // 5. Filtering
        CQ_CORE_INFO("Step 5: Applying filtering...");
        ApplyFiltering();
        m_Progress = 0.9f;

        // 6. Packing
        CQ_CORE_INFO("Step 6: Packing lightmaps...");
        PackLightmaps();
        m_Progress = 1.0f;

        m_IsBaking = false;
        CQ_CORE_INFO("Lightmap bake complete!");
    }

    void LightmapBaker::BakeDirectLighting(Scene* scene)
    {
        // Her texel icin direct lighting hesapla
        // Bu su an basitlestirilmis
        uint32_t width = m_Settings.MaxLightmapSize;
        uint32_t height = m_Settings.MaxLightmapSize;

        m_Atlas.Width = width;
        m_Atlas.Height = height;
        m_Atlas.Texels.resize(width * height, glm::vec3(0.0f));

        // Random sampling ile direct lighting
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);

        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                // Bu texel icin rastgele yonlerde sample al
                glm::vec3 directLight(0.0f);

                for (int s = 0; s < m_Settings.DirectSamples; s++)
                {
                    // Rastgele yon
                    float theta = dis(gen) * 2.0f * 3.14159f;
                    float phi = dis(gen) * 3.14159f;
                    glm::vec3 direction(
                        sin(phi) * cos(theta),
                        cos(phi),
                        sin(phi) * sin(theta)
                    );

                    // Simplified direct lighting
                    float ndotl = std::max(0.0f, glm::dot(glm::vec3(0, 1, 0), direction));
                    directLight += glm::vec3(1.0f, 0.9f, 0.8f) * ndotl;
                }

                directLight /= (float)m_Settings.DirectSamples;
                m_Atlas.Texels[y * width + x] = directLight;
            }
        }
    }

    void LightmapBaker::BakeIndirectLighting(Scene* scene)
    {
        // Indirect lighting (bounces)
        uint32_t width = m_Atlas.Width;
        uint32_t height = m_Atlas.Height;

        for (int bounce = 0; bounce < m_Settings.MaxBounces; bounce++)
        {
            CQ_CORE_INFO("Indirect bounce {0}/{1}", bounce + 1, m_Settings.MaxBounces);

            std::vector<glm::vec3> newTexels(width * height, glm::vec3(0.0f));

            for (uint32_t y = 0; y < height; y++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    // Bu texel'den cikan isin baska yuzeylere carpmasini hesapla
                    // Simplified: rastgele yonlerde sample al
                    glm::vec3 indirectLight(0.0f);

                    for (int s = 0; s < m_Settings.IndirectSamples; s++)
                    {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_real_distribution<float> dis(0.0f, 1.0f);

                        float theta = dis(gen) * 2.0f * 3.14159f;
                        float phi = dis(gen) * 3.14159f;
                        glm::vec3 direction(
                            sin(phi) * cos(theta),
                            cos(phi),
                            sin(phi) * sin(theta)
                        );

                        // Simplified: mevcut texel rengini yon ile carptir
                        indirectLight += m_Atlas.Texels[y * width + x] * 0.1f;
                    }

                    indirectLight /= (float)m_Settings.IndirectSamples;
                    newTexels[y * width + x] = m_Atlas.Texels[y * width + x] + indirectLight;
                }
            }

            m_Atlas.Texels = newTexels;
        }
    }

    void LightmapBaker::BakeAmbientOcclusion(Scene* scene)
    {
        uint32_t width = m_Atlas.Width;
        uint32_t height = m_Atlas.Height;

        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                // AO hesapla (simplified)
                float ao = 1.0f;

                // Rastgele yonlerde ray cast
                for (int s = 0; s < 16; s++)
                {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

                    float theta = dis(gen) * 2.0f * 3.14159f;
                    float phi = dis(gen) * 3.14159f;

                    // Simplified: yakin texel'lerde karanlik varsa ao dusur
                    if (x > 0 && x < width - 1 && y > 0 && y < height - 1)
                    {
                        float neighbors = 0.0f;
                        neighbors += glm::length(m_Atlas.Texels[(y-1) * width + x]);
                        neighbors += glm::length(m_Atlas.Texels[(y+1) * width + x]);
                        neighbors += glm::length(m_Atlas.Texels[y * width + (x-1)]);
                        neighbors += glm::length(m_Atlas.Texels[y * width + (x+1)]);
                        neighbors /= 4.0f;

                        ao = std::min(ao, neighbors);
                    }
                }

                m_Atlas.Texels[y * width + x] *= ao;
            }
        }
    }

    void LightmapBaker::GenerateUV2()
    {
        // UV2 generation (lightmap UV)
        // Bu su an basitlestirilmis - gercektesinde mesh'lerin UV2'si olusturulmali
        CQ_CORE_INFO("Generating UV2 coordinates for lightmapping...");
    }

    void LightmapBaker::PackLightmaps()
    {
        // Lightmap'leri paketle
        CQ_CORE_INFO("Packing lightmaps...");
    }

    void LightmapBaker::ApplyFiltering()
    {
        if (m_Settings.Filtering == 1) // None
            return;

        // Auto filtering - gaussian blur
        uint32_t width = m_Atlas.Width;
        uint32_t height = m_Atlas.Height;
        std::vector<glm::vec3> filtered = m_Atlas.Texels;

        for (uint32_t y = 1; y < height - 1; y++)
        {
            for (uint32_t x = 1; x < width - 1; x++)
            {
                glm::vec3 sum(0.0f);
                float weight = 0.0f;

                for (int ky = -1; ky <= 1; ky++)
                {
                    for (int kx = -1; kx <= 1; kx++)
                    {
                        float w = 1.0f / (1.0f + std::abs((float)kx) + std::abs((float)ky));
                        sum += m_Atlas.Texels[(y + ky) * width + (x + kx)] * w;
                        weight += w;
                    }
                }

                filtered[y * width + x] = sum / weight;
            }
        }

        m_Atlas.Texels = filtered;
    }

    std::shared_ptr<LightmapBaker> LightmapBaker::Create(const LightmapSettings& settings)
    {
        return std::make_shared<LightmapBaker>(settings);
    }
}
