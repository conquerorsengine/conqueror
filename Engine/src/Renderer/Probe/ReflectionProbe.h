#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Cubemap.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    struct ReflectionProbeSettings
    {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 BoxOffset = glm::vec3(-1.0f, -1.0f, -1.0f);
        glm::vec3 BoxSize = glm::vec3(1.0f, 1.0f, 1.0f);
        uint32_t Resolution = 128;
        float Intensity = 1.0f;
        int Bounces = 1;
        bool RealtimeUpdate = false;
    };

    class CQ_API ReflectionProbe
    {
    public:
        ReflectionProbe(const ReflectionProbeSettings& settings = {});
        ~ReflectionProbe() = default;

        void Capture(Scene* scene);
        std::shared_ptr<Cubemap> GetCubemap() const { return m_Cubemap; }

        const ReflectionProbeSettings& GetSettings() const { return m_Settings; }
        void SetSettings(const ReflectionProbeSettings& settings) { m_Settings = settings; }

        bool ContainsPoint(const glm::vec3& point) const;
        float DistanceToPoint(const glm::vec3& point) const;

        static std::shared_ptr<ReflectionProbe> Create(const ReflectionProbeSettings& settings = {});

    private:
        void RenderFace(Scene* scene, const glm::vec3& position, const glm::vec3& center,
                        const glm::vec3& up, uint32_t faceIndex);

        ReflectionProbeSettings m_Settings;
        std::shared_ptr<Cubemap> m_Cubemap;
    };
}
