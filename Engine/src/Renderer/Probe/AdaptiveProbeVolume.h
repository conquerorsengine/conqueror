#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Texture.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Conqueror
{
    class Scene;

    // Spherical Harmonics L2 - 9 coefficients per channel
    struct SHL2Coefficients
    {
        glm::vec3 Coeffs[9]; // L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22

        SHL2Coefficients() { for (int i = 0; i < 9; i++) Coeffs[i] = glm::vec3(0.0f); }

        glm::vec3 Evaluate(const glm::vec3& direction) const
        {
            float x = direction.x;
            float y = direction.y;
            float z = direction.z;

            glm::vec3 result(0.0f);
            // Band 0
            result += Coeffs[0] * 0.282095f;
            // Band 1
            result += Coeffs[1] * 0.488603f * y;
            result += Coeffs[2] * 0.488603f * z;
            result += Coeffs[3] * 0.488603f * x;
            // Band 2
            result += Coeffs[4] * 1.092548f * x * y;
            result += Coeffs[5] * 1.092548f * y * z;
            result += Coeffs[6] * 0.315392f * (3.0f * z * z - 1.0f);
            result += Coeffs[7] * 1.092548f * x * z;
            result += Coeffs[8] * 0.546274f * (x * x - y * y);

            return result;
        }
    };

    struct APVProbe
    {
        glm::vec3 Position = glm::vec3(0.0f);
        SHL2Coefficients SH;
        bool Valid = false;
    };

    struct APVBrick
    {
        glm::vec3 MinBound = glm::vec3(0.0f);
        glm::vec3 MaxBound = glm::vec3(0.0f);
        int Level = 0;
        int ProbeIndices[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }; // 8 corners
        bool HasGeometry = false;
    };

    struct AdaptiveProbeVolumeSettings
    {
        glm::vec3 Origin = glm::vec3(0.0f);
        glm::vec3 Size = glm::vec3(10.0f, 10.0f, 10.0f);
        glm::vec3 ProbeOffset = glm::vec3(0.0f);
        float MinSpacing = 1.0f;
        float MaxSpacing = 9.0f;
        int BakingMode = 0;
        int NumBounces = 2;
        int ProbeResolution = 32;
    };

    struct ProbeInvaliditySettings
    {
        bool Dilation = true;
        bool VirtualOffset = true;
        float ValidityThreshold = 0.75f;
        float SearchDistanceMultiplier = 0.2f;
        float GeometryBias = 0.01f;
        float RayOriginBias = -0.001f;
        int DilationDistance = 3;
    };

    using APVBakeProgressCallback = std::function<void(float, const std::string&)>;

    static constexpr int APV_MAX_PROBES = 512;
    static constexpr int APV_SH_COEFFS = 9;

    class CQ_API AdaptiveProbeVolume
    {
    public:
        AdaptiveProbeVolume(const AdaptiveProbeVolumeSettings& settings = {});
        ~AdaptiveProbeVolume() = default;

        void Bake(Scene* scene);
        void SetProgressCallback(APVBakeProgressCallback callback) { m_ProgressCallback = callback; }

        // Brick hierarchy
        const std::vector<APVBrick>& GetBricks(int level) const { return m_Bricks[level]; }
        int GetNumLevels() const { return m_NumLevels; }

        // Probes
        const std::vector<APVProbe>& GetProbes() const { return m_Probes; }
        int GetTotalProbeCount() const { return (int)m_Probes.size(); }

        // Settings
        const AdaptiveProbeVolumeSettings& GetSettings() const { return m_Settings; }
        void SetSettings(const AdaptiveProbeVolumeSettings& settings) { m_Settings = settings; }
        const ProbeInvaliditySettings& GetInvaliditySettings() const { return m_InvaliditySettings; }
        void SetInvaliditySettings(const ProbeInvaliditySettings& settings) { m_InvaliditySettings = settings; }

        // Status
        bool IsBaked() const { return m_IsBaked; }
        bool IsBaking() const { return m_IsBaking; }
        float GetProgress() const { return m_Progress; }

        // Runtime evaluation - trilinear interpolation
        void EvaluateIrradiance(const glm::vec3& worldPos, const glm::vec3& normal,
                               glm::vec3& outIrradiance) const;

        // Debug
        int GetBrickCount(int level) const { return (int)m_Bricks[level].size(); }

        static std::shared_ptr<AdaptiveProbeVolume> Create(const AdaptiveProbeVolumeSettings& settings = {});

    private:
        void PlaceProbes(Scene* scene);
        int GetNumLevelsFromSpacing() const;

        void BakeProbes(Scene* scene);
        void BakeSingleProbe(Scene* scene, APVProbe& probe);
        void RenderProbeCubemap(Scene* scene, const glm::vec3& position,
                               uint32_t resolution, std::vector<glm::vec3>& outCubemap);
        void CubemapToSH(const std::vector<glm::vec3>& cubemapData, uint32_t resolution,
                        SHL2Coefficients& outSH);

        void ApplyVirtualOffset(Scene* scene);
        void ApplyDilation();
        bool IsProbeValid(const APVProbe& probe) const;

        void ReportProgress(float progress, const std::string& step);

        AdaptiveProbeVolumeSettings m_Settings;
        ProbeInvaliditySettings m_InvaliditySettings;
        APVBakeProgressCallback m_ProgressCallback;

        std::vector<APVBrick> m_Bricks[4]; // Max 4 levels
        std::vector<APVProbe> m_Probes;
        int m_NumLevels = 1;

        bool m_IsBaked = false;
        bool m_IsBaking = false;
        float m_Progress = 0.0f;
    };
}
