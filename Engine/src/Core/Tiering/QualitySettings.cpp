#include "QualitySettings.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    QualityLevel QualitySettings::s_CurrentLevel = QualityLevel::High;
    QualityPreset QualitySettings::s_CurrentPreset = {};

    void QualitySettings::Init()
    {
        SetQualityLevel(QualityLevel::High);
        CQ_CORE_INFO("QualitySettings initialized - Level: High");
    }

    void QualitySettings::SetQualityLevel(QualityLevel level)
    {
        s_CurrentLevel = level;
        
        switch (level)
        {
            case QualityLevel::Low:
                s_CurrentPreset = GetLowPreset();
                break;
            case QualityLevel::Medium:
                s_CurrentPreset = GetMediumPreset();
                break;
            case QualityLevel::High:
                s_CurrentPreset = GetHighPreset();
                break;
            case QualityLevel::Ultra:
                s_CurrentPreset = GetUltraPreset();
                break;
            case QualityLevel::Custom:
                // Custom preset kullanıcı tarafından set edilir
                break;
        }
        
        ApplyPreset();
    }

    void QualitySettings::SetCustomPreset(const QualityPreset& preset)
    {
        s_CurrentLevel = QualityLevel::Custom;
        s_CurrentPreset = preset;
        ApplyPreset();
    }

    void QualitySettings::ApplyPreset()
    {
        // Burada render sistemine ayarları uygula
        // Örnek: Renderer::SetShadowResolution(s_CurrentPreset.ShadowResolution);
        CQ_CORE_INFO("Quality preset applied");
    }

    QualityPreset QualitySettings::GetLowPreset()
    {
        QualityPreset preset;
        preset.ShadowResolution = 512;
        preset.ShadowCascades = 2;
        preset.ShadowDistance = 50.0f;
        preset.EnableSSAO = false;
        preset.EnableBloom = false;
        preset.EnableMotionBlur = false;
        preset.AntiAliasing = 0;
        preset.EnableSMAA = false;
        preset.EnableFXAA = false;
        preset.EnableVolumetricFog = false;
        preset.EnableGodRays = false;
        preset.LODBias = 2.0f;
        preset.MaxLODLevel = 2;
        preset.TextureQuality = 0;
        preset.AnisotropicFiltering = 2;
        preset.PhysicsIterations = 4;
        preset.MaxParticles = 1000;
        return preset;
    }

    QualityPreset QualitySettings::GetMediumPreset()
    {
        QualityPreset preset;
        preset.ShadowResolution = 1024;
        preset.ShadowCascades = 3;
        preset.ShadowDistance = 75.0f;
        preset.EnableSSAO = true;
        preset.EnableBloom = true;
        preset.EnableMotionBlur = false;
        preset.AntiAliasing = 2;
        preset.EnableSMAA = false;
        preset.EnableFXAA = true;
        preset.EnableVolumetricFog = false;
        preset.EnableGodRays = false;
        preset.LODBias = 1.5f;
        preset.MaxLODLevel = 3;
        preset.TextureQuality = 1;
        preset.AnisotropicFiltering = 8;
        preset.PhysicsIterations = 6;
        preset.MaxParticles = 5000;
        return preset;
    }

    QualityPreset QualitySettings::GetHighPreset()
    {
        QualityPreset preset;
        preset.ShadowResolution = 2048;
        preset.ShadowCascades = 4;
        preset.ShadowDistance = 100.0f;
        preset.EnableSSAO = true;
        preset.EnableBloom = true;
        preset.EnableMotionBlur = true;
        preset.AntiAliasing = 4;
        preset.EnableSMAA = false;
        preset.EnableFXAA = true;
        preset.EnableVolumetricFog = true;
        preset.EnableGodRays = false;
        preset.LODBias = 1.0f;
        preset.MaxLODLevel = 4;
        preset.TextureQuality = 2;
        preset.AnisotropicFiltering = 16;
        preset.PhysicsIterations = 8;
        preset.MaxParticles = 10000;
        return preset;
    }

    QualityPreset QualitySettings::GetUltraPreset()
    {
        QualityPreset preset;
        preset.ShadowResolution = 4096;
        preset.ShadowCascades = 4;
        preset.ShadowDistance = 150.0f;
        preset.EnableSSAO = true;
        preset.EnableBloom = true;
        preset.EnableMotionBlur = true;
        preset.AntiAliasing = 8;
        preset.EnableSMAA = false;
        preset.EnableFXAA = true;
        preset.EnableVolumetricFog = true;
        preset.EnableGodRays = true;
        preset.LODBias = 0.5f;
        preset.MaxLODLevel = 5;
        preset.TextureQuality = 2;
        preset.AnisotropicFiltering = 16;
        preset.PhysicsIterations = 10;
        preset.MaxParticles = 20000;
        return preset;
    }
}
