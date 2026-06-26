#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Texture.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace Conqueror
{
    class Scene;

    struct LightmapSettings
    {
        int Lightmapper = 1;
        bool ImportanceSampling = true;
        int DirectSamples = 32;
        int IndirectSamples = 512;
        int EnvironmentSamples = 256;
        int LightProbeSampleMultiplier = 4;
        int MaxBounces = 2;
        int Filtering = 0;
        int Packing = 0;
        int Resolution = 40;
        int MaxLightmapSize = 1024;
        bool UseMipmapLimits = true;
        int Compression = 2;
        bool AmbientOcclusion = true;
        int DirectionalMode = 0;
        float AlbedoBoost = 1.0f;
        float IndirectIntensity = 1.0f;
    };

    struct LightmapAtlas
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<glm::vec3> Texels;
        std::vector<uint32_t> TriangleIDs;
        std::vector<glm::vec2> UVCoordinates;
    };

    using LightmapProgressCallback = std::function<void(float, const std::string&)>;

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

        void SetProgressCallback(LightmapProgressCallback callback) { m_ProgressCallback = callback; }

        std::shared_ptr<Texture2D> CreateLightmapTexture() const;

        static std::shared_ptr<LightmapBaker> Create(const LightmapSettings& settings = {});

    private:
        void ReportProgress(float progress, const std::string& step);
        void ApplyFiltering();

        LightmapSettings m_Settings;
        LightmapAtlas m_Atlas;
        bool m_IsBaking = false;
        float m_Progress = 0.0f;
        LightmapProgressCallback m_ProgressCallback;
    };
}
