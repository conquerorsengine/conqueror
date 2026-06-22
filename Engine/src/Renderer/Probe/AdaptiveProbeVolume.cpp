#include "AdaptiveProbeVolume.h"
#include "Scene/Scene.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    AdaptiveProbeVolume::AdaptiveProbeVolume(const AdaptiveProbeVolumeSettings& settings)
        : m_Settings(settings)
    {
    }

    void AdaptiveProbeVolume::Bake(Scene* scene)
    {
        if (!scene)
            return;

        CQ_CORE_INFO("Baking adaptive probe volume");

        // Once probe'lari yerlestir
        PlaceProbes();

        // Her probe'i bake et
        for (auto& probe : m_Probes)
        {
            probe->Bake(scene);
        }

        CQ_CORE_INFO("Adaptive probe volume bake complete - {0} probes", m_Probes.size());
    }

    void AdaptiveProbeVolume::PlaceProbes()
    {
        m_Probes.clear();

        glm::vec3 minBound = m_Settings.Origin;
        glm::vec3 maxBound = m_Settings.Origin + m_Settings.Size;

        // Spacing hesapla (adaptive - buyuk bosluklarda daha seyrek)
        float spacing = m_Settings.MinSpacing;

        // Grid uzerine probe'lar yerlestir
        for (float x = minBound.x + m_Settings.ProbeOffset.x; x <= maxBound.x; x += spacing)
        {
            for (float y = minBound.y + m_Settings.ProbeOffset.y; y <= maxBound.y; y += spacing)
            {
                for (float z = minBound.z + m_Settings.ProbeOffset.z; z <= maxBound.z; z += spacing)
                {
                    LightProbeSettings probeSettings;
                    probeSettings.Position = glm::vec3(x, y, z);
                    probeSettings.Resolution = 32;

                    auto probe = LightProbe::Create(probeSettings);
                    m_Probes.push_back(probe);
                }
            }
        }

        CQ_CORE_INFO("Placed {0} probes in adaptive probe volume", m_Probes.size());
    }

    std::shared_ptr<LightProbe> AdaptiveProbeVolume::FindNearestProbe(const glm::vec3& position) const
    {
        if (m_Probes.empty())
            return nullptr;

        std::shared_ptr<LightProbe> nearest = nullptr;
        float minDist = std::numeric_limits<float>::max();

        for (const auto& probe : m_Probes)
        {
            float dist = glm::length(probe->GetSettings().Position - position);
            if (dist < minDist)
            {
                minDist = dist;
                nearest = probe;
            }
        }

        return nearest;
    }

    std::shared_ptr<AdaptiveProbeVolume> AdaptiveProbeVolume::Create(const AdaptiveProbeVolumeSettings& settings)
    {
        return std::make_shared<AdaptiveProbeVolume>(settings);
    }
}
