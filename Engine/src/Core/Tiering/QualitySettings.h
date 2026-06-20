#pragma once

#include "Core/Base/Base.h"

namespace Conqueror
{
    enum class QualityLevel
    {
        Low = 0,
        Medium,
        High,
        Ultra,
        Custom
    };

    struct CQ_API QualityPreset
    {
        // Render
        int ShadowResolution = 1024;
        int ShadowCascades = 4;
        float ShadowDistance = 100.0f;
        bool EnableSSAO = true;
        bool EnableBloom = true;
        bool EnableMotionBlur = false;
        int AntiAliasing = 4; // MSAA samples
        
        // Post-process
        bool EnableSMAA = false;
        bool EnableFXAA = true;
        bool EnableVolumetricFog = false;
        bool EnableGodRays = false;
        
        // LOD
        float LODBias = 1.0f;
        int MaxLODLevel = 3;
        
        // Texture
        int TextureQuality = 2; // 0=Low, 1=Medium, 2=High
        int AnisotropicFiltering = 16;
        
        // Physics
        int PhysicsIterations = 8;
        
        // Particles
        int MaxParticles = 10000;
    };

    class CQ_API QualitySettings
    {
    public:
        static void Init();
        
        static void SetQualityLevel(QualityLevel level);
        static QualityLevel GetQualityLevel() { return s_CurrentLevel; }
        
        static void SetCustomPreset(const QualityPreset& preset);
        static const QualityPreset& GetPreset() { return s_CurrentPreset; }
        
        static void ApplyPreset();
        
        // Preset'ler
        static QualityPreset GetLowPreset();
        static QualityPreset GetMediumPreset();
        static QualityPreset GetHighPreset();
        static QualityPreset GetUltraPreset();
        
    private:
        static QualityLevel s_CurrentLevel;
        static QualityPreset s_CurrentPreset;
    };
}
