#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror
{
    // Sahne başına render ayarları. Lighting paneli düzenler, SceneSerializer
    // kaydeder, Renderer3D + PostProcess her karede uygular.

    enum class ShadowQuality : int
    {
        Hard = 0,      // tek örnek
        Soft,          // 16-tap Poisson PCF
        VerySoft       // PCSS benzeri, blocker araması ile değişken yarıçap
    };

    struct ShadowSettings
    {
        bool Enabled = true;
        ShadowQuality Quality = ShadowQuality::Soft;
        int Resolution = 4096;        // atlas genişliği (cascade'ler yan yana)
        float Distance = 80.0f;       // gölge çizim mesafesi
        int CascadeCount = 2;         // 1..4
        float CascadeSplitLambda = 0.75f;
        float Strength = 1.0f;        // 0 = gölge yok, 1 = tam
        float Softness = 1.0f;        // PCF yarıçap çarpanı
        float DepthBias = 0.005f;
        float NormalBias = 0.015f;
        glm::vec3 Color = glm::vec3(0.0f); // gölge rengi (Unity "Realtime Shadow Color")
    };

    enum class FogMode : int
    {
        Linear = 0,
        Exponential,
        ExponentialSquared
    };

    struct FogSettings
    {
        bool Enabled = false;
        FogMode Mode = FogMode::ExponentialSquared;
        glm::vec3 Color = glm::vec3(0.5f, 0.6f, 0.7f);
        float Density = 0.02f;
        float Start = 10.0f;
        float End = 100.0f;
        float HeightFalloff = 0.0f;   // > 0: yükseklik sisi (yukarı çıktıkça incelir)
        float BaseHeight = 0.0f;
    };

    enum class Tonemapper : int
    {
        None = 0,
        Neutral,       // Khronos PBR Neutral: 0.76 altını değiştirmez (2D/UI dostu)
        ACES,
        AgX,
        Reinhard
    };

    enum class AntiAliasingMode : int
    {
        None = 0,
        FXAA,
        SMAA,
        TAA            // Temporal AA: alt piksel titreşimi + geçmiş karelerin yeniden izdüşümü (Unreal varsayılanı)
    };

    // Full Screen Pass (sahnedeki bileşenlerden toplanır)
    struct FullScreenPassEntry
    {
        std::string Shader;
        float Intensity = 1.0f;
        bool AfterPostProcess = false;
    };

    struct PostProcessSettings
    {
        bool Enabled = true;
        std::vector<FullScreenPassEntry> FullScreenPasses;   // serileştirilmez

        Tonemapper Tonemap = Tonemapper::Neutral;
        float Exposure = 0.0f;        // EV (2^Exposure çarpan)

        bool Bloom = true;
        float BloomThreshold = 1.0f;
        float BloomKnee = 0.5f;
        float BloomIntensity = 0.6f;
        float BloomScatter = 0.7f;    // upsample karışımı
        glm::vec3 BloomTint = glm::vec3(1.0f);

        bool AmbientOcclusion = true;
        float AORadius = 0.6f;
        float AOIntensity = 1.0f;
        float AOPower = 1.5f;

        bool ColorGrading = true;
        float Contrast = 1.0f;
        float Saturation = 1.0f;
        float Temperature = 0.0f;     // -1 soğuk .. +1 sıcak
        float Tint = 0.0f;            // -1 yeşil .. +1 magenta
        glm::vec3 Lift = glm::vec3(0.0f);
        glm::vec3 Gamma = glm::vec3(1.0f);
        glm::vec3 Gain = glm::vec3(1.0f);

        bool Vignette = false;
        float VignetteIntensity = 0.3f;
        float VignetteSmoothness = 0.4f;
        glm::vec3 VignetteColor = glm::vec3(0.0f);

        bool ChromaticAberration = false;
        float ChromaticAberrationIntensity = 0.3f;

        bool FilmGrain = false;
        float FilmGrainIntensity = 0.15f;

        bool Sharpen = false;
        float SharpenAmount = 0.3f;

        AntiAliasingMode AntiAliasing = AntiAliasingMode::FXAA;
        float TAAFeedback = 0.9f;         // geçmiş ağırlığı (yüksek = pürüzsüz, hareket izi riski)
        float TAASharpness = 0.25f;

        // Ekran uzayı yansımalar (SSR): parlak zemin / su / metal yüzeyler sahneyi yansıtır
        bool ScreenSpaceReflections = false;
        float SSRIntensity = 1.0f;
        float SSRMaxDistance = 40.0f;     // dünya birimi
        float SSRThickness = 0.5f;        // yüzey kalınlık toleransı
        float SSRMaxRoughness = 0.6f;     // daha pürüzlü yüzeyler yansıtmaz

        // Ekran uzayı küresel aydınlatma (Lumen ekran izleri benzeri): yüzeyler arası tek sıçrama dolaylı ışık
        bool ScreenSpaceGI = false;
        float SSGIIntensity = 1.0f;
        float SSGIRadius = 4.0f;          // ışın uzunluğu (m)
        int SSGIRays = 4;                 // piksel başına ışın (yarım çözünürlük)

        // Hacimsel sis (Unreal Exponential Height Fog + Volumetric Fog): gölgeli güneş huzmeleri
        bool VolumetricFog = false;
        float VolumetricFogDensity = 0.01f;
        float VolumetricFogHeightFalloff = 0.1f;
        float VolumetricFogBaseHeight = 0.0f;
        float VolumetricFogAnisotropy = 0.2f;   // ileri saçılma (Henyey-Greenstein g)
        float VolumetricFogIntensity = 1.0f;    // güneş saçılma çarpanı
        float VolumetricFogDistance = 80.0f;
        glm::vec3 VolumetricFogAlbedo = glm::vec3(1.0f);
        float VolumetricFogAmbient = 0.3f;      // ortam (gökyüzü) ışığının sise katkısı

        // Hacimsel bulutlar (ışın yürütmeli, rüzgârla akar)
        bool VolumetricClouds = false;
        float CloudCoverage = 0.45f;      // 0..1
        float CloudDensity = 1.0f;
        float CloudBottom = 1500.0f;      // m
        float CloudThickness = 1800.0f;   // m
        float CloudScale = 1.0f;          // şekil ölçeği (büyük = küçük bulutlar)
        float CloudWindSpeed = 12.0f;     // m/s
        float CloudWindDirection = 30.0f; // derece
        glm::vec3 CloudColor = glm::vec3(1.0f);

        // Alan derinliği (bokeh): odak uzaklığı dışında kalan bölgeler bulanıklaşır
        bool DepthOfField = false;
        float FocusDistance = 10.0f;      // m
        float Aperture = 2.8f;            // f-stop (küçük = daha fazla bulanıklık)
        float FocalLength = 50.0f;        // mm
        float DOFMaxBlur = 10.0f;         // piksel (1080p'ye göre)

        // Hareket bulanıklığı (kamera hareketi, derinlikten yeniden izdüşüm)
        bool MotionBlur = false;
        float MotionBlurIntensity = 0.5f; // pozlama süresi / kare süresi
        float MotionBlurMax = 32.0f;      // piksel

        // Otomatik pozlama (göz uyumu): ortalama parlaklığa göre pozlama zamanla ayarlanır
        bool AutoExposure = false;
        float AutoExposureMin = -3.0f;    // EV
        float AutoExposureMax = 3.0f;     // EV
        float AutoExposureSpeedUp = 3.0f;
        float AutoExposureSpeedDown = 1.0f;
        float AutoExposureCompensation = 0.0f;  // EV
    };

    enum class SkyMode : int
    {
        Auto = 0,      // skybox varsa o; yoksa 3D içerik varsa prosedürel gökyüzü
        Procedural,
        SolidColor,
        Atmosphere     // fiziksel gökyüzü atmosferi (Rayleigh + Mie + ozon; Unreal Sky Atmosphere)
    };

    struct EnvironmentSettings
    {
        SkyMode Sky = SkyMode::Auto;
        glm::vec3 BackgroundColor = glm::vec3(0.1f);
        bool ImageBasedLighting = true;   // skybox'tan diffuse + specular IBL
        float ReflectionIntensity = 1.0f;
        float SkyboxIntensity = 1.0f;     // ambient/IBL katkısı
        bool SpecularOcclusion = true;

        // Sky Atmosphere (Sky = Atmosphere)
        float AtmosphereRayleigh = 1.0f;  // mavi gökyüzü saçılması
        float AtmosphereMie = 1.0f;       // pus ve güneş halesi
        float AtmosphereMieAnisotropy = 0.8f;
        float AtmosphereOzone = 1.0f;     // alacakaranlık rengi
        float AtmosphereSunDisk = 1.0f;   // güneş diski boyutu
        float AtmosphereIntensity = 1.0f;
        glm::vec3 AtmosphereGroundAlbedo = glm::vec3(0.12f);
    };

    // Lighting penceresindeki pişirme (lightmap / APV) ayarları
    struct LightBakeSettings
    {
        // Lighting Settings varlığı (.cqlighting, Unity Lighting Settings Asset): doluysa bu ayarlar dosyadan gelir ve
        // Lighting penceresindeki değişiklikler dosyaya yazılır (sahneler arasında paylaşılır)
        std::string SettingsAsset;
        // Varsayılan Lightmap Parameters (.cqlmparams); Mesh Renderer kendi parametresini seçebilir
        std::string DefaultLightmapParameters;
        bool RealtimeGI = false;          // gerçek zamanlı lightmap'ler (otomatik güncellenir)
        bool BakedGI = true;
        int LightingMode = 0;             // 0 Shadowmask, 1 Distance Shadowmask, 2 Subtractive, 3 Baked Indirect
        int Lightmapper = 1;              // 0 None, 1 Progressive CPU, 2 Progressive GPU
        bool ImportanceSampling = true;
        int DirectSamples = 32;
        int IndirectSamples = 512;
        int EnvironmentSamples = 256;
        int MaxBounces = 2;
        int Filtering = 0;                // 0 Auto, 1 None
        int Packing = 0;                  // 0 Auto, 1 Manual
        int Resolution = 40;              // texel / birim
        int MaxLightmapSize = 1024;
        bool UseMipmapLimits = true;
        int Compression = 2;              // 0 None, 1 Low, 2 Normal, 3 High
        bool AmbientOcclusion = true;
        int DirectionalMode = 0;          // 0 Directional, 1 Non-Directional
        float AlbedoBoost = 1.0f;
        float IndirectIntensity = 1.0f;
        int RealtimeResolution = 20;
        int RealtimeIndirectSamples = 256;
        int RealtimeMaxBounces = 1;
        bool AutoGenerate = false;        // ışık/statik geometri değişince otomatik pişir
        int BakeOnSceneLoad = 0;          // 0 Never, 1 If missing

        // Adaptive Probe Volume
        bool APVFitToScene = true;
        glm::vec3 APVOrigin = glm::vec3(-10.0f, -1.0f, -10.0f);
        glm::vec3 APVSize = glm::vec3(20.0f, 10.0f, 20.0f);
        glm::vec3 APVProbeOffset = glm::vec3(0.0f);
        float APVMinSpacing = 1.0f;
        float APVMaxSpacing = 9.0f;
        int APVBakingMode = 0;
        bool APVDilation = false;
        bool APVVirtualOffset = true;
        float APVValidityThreshold = 0.75f;
        float APVSearchDistanceMultiplier = 0.2f;
        float APVGeometryBias = 0.01f;
        float APVRayOriginBias = -0.001f;
    };

    struct RenderSettings
    {
        ShadowSettings Shadows;
        FogSettings Fog;
        PostProcessSettings Post;
        EnvironmentSettings Environment;
        LightBakeSettings Bake;
    };
}
