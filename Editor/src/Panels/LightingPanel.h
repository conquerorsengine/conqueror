#pragma once

#include "Scene/Scene.h"
#include "Renderer/Lightmap/LightmapBaker.h"
#include "Renderer/Probe/ReflectionProbe.h"
#include "Renderer/Probe/AdaptiveProbeVolume.h"
#include "Renderer/RHI/Texture.h"

#include <memory>
#include <string>
#include <chrono>

namespace Conqueror::Editor
{
    class LightingPanel
    {
    public:
        LightingPanel() = default;
        ~LightingPanel() = default;

        void OnImGuiRender();
        void SetContext(std::shared_ptr<Scene> scene) { m_Context = scene; }

    private:
        // Tab rendering
        void RenderSceneTab();
        void RenderAdaptiveProbeVolumesTab();
        void RenderEnvironmentTab();
        void RenderRealtimeLightmapsTab();
        void RenderBakedLightmapsTab();

        // Helper
        void RenderGPUBakingSection();

        std::shared_ptr<Scene> m_Context;

        // Active tab
        int m_ActiveTab = 0; // 0=Scene, 1=APV, 2=Environment, 3=RealtimeLM, 4=BakedLM

        // Scene tab settings
        int m_LightmapperIndex = 1; // 0=None, 1=Progressive CPU, 2=Progressive GPU
        bool m_ImportanceSampling = true;
        int m_DirectSamples = 32;
        int m_IndirectSamples = 512;
        int m_EnvironmentSamples = 256;
        int m_LightProbeSampleMultiplier = 4;
        int m_MaxBounces = 2;
        int m_FilteringIndex = 0; // 0=Auto, 1=None
        int m_PackingIndex = 0; // 0=Auto, 1=Manual
        int m_LightmapResolution = 40;
        int m_MaxLightmapSizeIndex = 1; // 0=512, 1=1024, 2=2048, 3=4096
        bool m_UseMipmapLimits = true;
        int m_LightmapCompressionIndex = 2; // 0=None, 1=Low, 2=Medium, 3=High
        bool m_AmbientOcclusion = true;
        int m_DirectionalModeIndex = 0; // 0=Directional, 1=Non-Directional
        float m_AlbedoBoost = 1.0f;
        float m_IndirectIntensity = 1.0f;

        // Realtime Lighting
        bool m_RealtimeGI = false;

        // Mixed Lighting
        bool m_BakedGI = false;
        int m_LightingModeIndex = 0; // 0=Shadowmask, 1=DistanceShadowmask, 2=Subtractive

        // APV tab settings
        int m_APBakingModeIndex = 0;
        float m_APProbeOffset[3] = { 0.0f, 0.0f, 0.0f };
        float m_APMinSpacing = 1.0f;
        float m_APMaxSpacing = 9.0f;
        bool m_APDilation = false;
        bool m_APVirtualOffset = true;
        float m_APValidityThreshold = 0.75f;
        float m_APSearchDistanceMultiplier = 0.2f;
        float m_APGeometryBias = 0.01f;
        float m_APRayOriginBias = -0.001f;

        // Environment tab settings
        int m_EnvironmentReflectionsSourceIndex = 0;
        int m_ReflectionsResolution = 128;
        int m_ReflectionsCompressionIndex = 0;
        float m_ReflectionsIntensityMultiplier = 1.0f;
        int m_ReflectionsBounces = 1;

        // Skybox resolution
        int m_SkyboxResolutionIndex = 4;

        // GPU Baking
        int m_GPUBakingProfileIndex = 0;
        int m_BakeOnSceneLoadIndex = 0;

        // Baking state
        bool m_IsBaking = false;
        float m_BakeProgress = 0.0f;
        int m_BakeMode = 0; // 0=Baked, 1=Realtime
        std::string m_BakeStep;

        // Realtime lightmap state
        bool m_RealtimeBaked = false;
        int m_RealtimeAtlasWidth = 0;
        int m_RealtimeAtlasHeight = 0;
        int m_RealtimeTexelCount = 0;
        int m_RealtimeObjectCount = 0;
        float m_RealtimeBakeTime = 0.0f;
        std::shared_ptr<Texture2D> m_RealtimeLightmapTexture;

        // Baked lightmap state
        bool m_BakedLightmapBaked = false;
        int m_BakedAtlasWidth = 0;
        int m_BakedAtlasHeight = 0;
        int m_BakedTexelCount = 0;
        int m_BakedObjectCount = 0;
        int m_BakedProbeCount = 0;
        float m_BakedBakeTime = 0.0f;
        std::shared_ptr<Texture2D> m_BakedLightmapTexture;
    };
}
