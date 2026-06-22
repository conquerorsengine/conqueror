#pragma once

#include "Core/Base/Base.h"
#include "LightProbe.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    struct AdaptiveProbeVolumeSettings
    {
        glm::vec3 Origin = glm::vec3(0.0f);
        glm::vec3 Size = glm::vec3(10.0f, 10.0f, 10.0f);
        glm::vec3 ProbeOffset = glm::vec3(0.0f);
        float MinSpacing = 1.0f;
        float MaxSpacing = 9.0f;
        int BakingMode = 0; // 0 = Single Scene, 1 = Multi Scene
    };

    // Probe invalidity settings
    struct ProbeInvaliditySettings
    {
        bool Dilation = false;
        bool VirtualOffset = true;
        float ValidityThreshold = 0.75f;
        float SearchDistanceMultiplier = 0.2f;
        float GeometryBias = 0.01f;
        float RayOriginBias = -0.001f;
    };

    class CQ_API AdaptiveProbeVolume
    {
    public:
        AdaptiveProbeVolume(const AdaptiveProbeVolumeSettings& settings = {});
        ~AdaptiveProbeVolume() = default;

        void Bake(Scene* scene);
        void PlaceProbes();

        const std::vector<std::shared_ptr<LightProbe>>& GetProbes() const { return m_Probes; }
        const AdaptiveProbeVolumeSettings& GetSettings() const { return m_Settings; }
        void SetSettings(const AdaptiveProbeVolumeSettings& settings) { m_Settings = settings; }

        const ProbeInvaliditySettings& GetInvaliditySettings() const { return m_InvaliditySettings; }
        void SetInvaliditySettings(const ProbeInvaliditySettings& settings) { m_InvaliditySettings = settings; }

        std::shared_ptr<LightProbe> FindNearestProbe(const glm::vec3& position) const;

        static std::shared_ptr<AdaptiveProbeVolume> Create(const AdaptiveProbeVolumeSettings& settings = {});

    private:
        AdaptiveProbeVolumeSettings m_Settings;
        ProbeInvaliditySettings m_InvaliditySettings;
        std::vector<std::shared_ptr<LightProbe>> m_Probes;
    };
}
