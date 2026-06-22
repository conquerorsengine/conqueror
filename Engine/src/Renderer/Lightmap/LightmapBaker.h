#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    struct LightmapSettings
    {
        int Lightmapper = 1; // 0 = None, 1 = Progressive CPU, 2 = Progressive GPU
        bool ImportanceSampling = true;
        int DirectSamples = 32;
        int IndirectSamples = 512;
        int EnvironmentSamples = 256;
        int LightProbeSampleMultiplier = 4;
        int MaxBounces = 2;
        int Filtering = 0; // 0 = Auto, 1 = None
        int Packing = 0; // 0 = Auto, 1 = Manual
        int Resolution = 40; // texels per unit
        int MaxLightmapSize = 1024;
        bool UseMipmapLimits = true;
        int Compression = 2; // 0 = None, 1 = Low, 2 = Medium, 3 = High
        bool AmbientOcclusion = true;
        int DirectionalMode = 0; // 0 = Directional, 1 = Non-Directional
        float AlbedoBoost = 1.0f;
        float IndirectIntensity = 1.0f;
    };

    struct LightmapAtlas
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<glm::vec3> Texels; // RGB data
        std::vector<uint32_t> TriangleIDs;
        std::vector<glm::vec2> UVCoordinates;
    };

    class CQ_API LightmapBaker
    {
    public:
        LightmapBaker(const LightmapSettings& settings = {});
        ~LightmapBaker() = default;

        void Bake(Scene* scene);
        bool IsBaking() const { return m_IsBaking; }
        float GetProgress() const { return m_Progress; }

        const LightmapSettings& GetSettings() const { return m_Settings; }
        void SetSettings(const LightmapSettings& settings) { m_Settings = settings; }

        const LightmapAtlas& GetAtlas() const { return m_Atlas; }

        static std::shared_ptr<LightmapBaker> Create(const LightmapSettings& settings = {});

    private:
        void BakeDirectLighting(Scene* scene);
        void BakeIndirectLighting(Scene* scene);
        void BakeAmbientOcclusion(Scene* scene);
        void GenerateUV2();
        void PackLightmaps();
        void ApplyFiltering();

        LightmapSettings m_Settings;
        LightmapAtlas m_Atlas;
        bool m_IsBaking = false;
        float m_Progress = 0.0f;
    };
}
