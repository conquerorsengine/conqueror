#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror
{
    // Per-scene render settings. Edited in the Lighting panel, serialized
    // by SceneSerializer, and applied each frame by Renderer3D + PostProcess.

    enum class ShadowQuality : int
    {
        Hard = 0,      // single tap
        Soft,          // 16-tap Poisson PCF
        VerySoft       // PCSS-like, variable radius with blocker search
    };

    struct ShadowSettings
    {
        bool Enabled = true;
        ShadowQuality Quality = ShadowQuality::Soft;
        int Resolution = 4096;        // atlas width (cascades side-by-side)
        float Distance = 80.0f;       // shadow render distance
        int CascadeCount = 2;         // 1..4
        float CascadeSplitLambda = 0.75f;
        float Strength = 1.0f;        // 0 = no shadow, 1 = full shadow
        float Softness = 1.0f;        // PCF radius multiplier
        float DepthBias = 0.005f;
        float NormalBias = 0.015f;
        glm::vec3 Color = glm::vec3(0.0f); // shadow color (Unity "Realtime Shadow Color")
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
        float HeightFalloff = 0.0f;   // > 0: height fog (thins out higher up)
        float BaseHeight = 0.0f;
    };

    enum class Tonemapper : int
    {
        None = 0,
        Neutral,       // Khronos PBR Neutral: preserves colors below 0.76 (2D/UI friendly)
        ACES,
        AgX,
        Reinhard
    };

    // Anti-aliasing modes. Values are serialized as integers into scene files: ALWAYS append new modes at the end.
    // Name, category, description, and availability for each mode are in Renderer/AntiAliasing.h.
    enum class AntiAliasingMode : int
    {
        None = 0,
        FXAA,           // Fast Approximate AA (NVIDIA FXAA 3.11 quality preset)
        SMAA,           // Subpixel Morphological AA (Jimenez, 1x: edge + area texture + neighborhood blending)
        TAA,            // Temporal AA: subpixel jitter + reprojection of history frames (Unreal default)
        TAA_FXAA,       // TAA + FXAA: lightweight FXAA final pass over temporal reconstruction
        MSAA,           // Hardware Multisample AA (selectable 2x/4x/8x)
        CSAA,           // Coverage Sampled AA (NVIDIA): separate coverage samples (NV_framebuffer_multisample_coverage)
        SSAA,           // Supersampling: scene rendered at SSAAScale times resolution per axis, downsampled with Lanczos
        FSAA,           // Full Scene AA: 2x2 supersampling of the entire scene (historical SSAA name), tent filter
        EQAA,           // Enhanced Quality AA (AMD): coverage samples > color samples (AMD_framebuffer_multisample_advanced)
        MLAA,           // Morphological AA (Reshetov / Jimenez GPU MLAA): orthogonal edge patterns only
        CMAA,           // Conservative Morphological AA (Intel CMAA2)
        TXAA,           // MSAA + temporal filter (NVIDIA TXAA style): wide MSAA resolve + TAA accumulation
        TSAA,           // Temporal Super Sampling AA: two jitter positions, reprojected average of two frames
        TAAU,           // TAA Upsampling: low internal resolution reconstructed to full resolution via TAA
        SMAA_T2X,       // SMAA 1x + 2-subsample temporal resolve
        TSRAA,          // Temporal Super Resolution at native resolution (as AA)
        TSR,            // Temporal Super Resolution (Epic): temporal upscaler
        DLSS,           // NVIDIA DLSS (RTX + NGX)
        DLAA,           // NVIDIA DLAA (DLSS at native resolution)
        FSR,            // AMD FidelityFX Super Resolution 1 (EASU upscale + RCAS sharpening)
        XeSS,           // Intel XeSS
        PSSR,           // Sony PlayStation Spectral Super Resolution (PS5 Pro)
        MetalFX,        // Apple MetalFX
        NIS,            // NVIDIA Image Scaling (upscaling + sharpening)
        CAS,            // AMD FidelityFX Contrast Adaptive Sharpening
        FSRAA,          // FSR at native resolution (Native AA)
        XeSSAA,         // XeSS at native resolution (XeAA)
        OGSSAA,         // Ordered Grid SSAA: integer grid (2x2 / 3x3 / 4x4), box filter
        SGSSAA,         // Sparse Grid SSAA: 8x MSAA sparse sample positions + shading per sample
        RGSS,           // Rotated Grid Super Sampling: 4x rotated grid + shading per sample
        TSSAA,          // Temporal Super Sampling AA (id Tech 8TX): 8 jitter positions, standard clipping
        DEAA,           // Directionally Enhanced AA: long / short smoothing along edge direction
        GBAA,           // Geometry Buffer AA (Humus): edge distance from geometry
        SRAA,           // Subpixel Reconstruction AA (NVIDIA): multisample geometry, single-sample shading
        Count
    };
    // Note: TAAU, TSR, DLSS, FSR, XeSS, PSSR, MetalFX, NIS, and CAS remain in the enum for backwards compatibility;
    // they are hidden from the Anti-Aliasing UI dropdown (like Unity / Unreal, upscalers live in a dedicated "Upscaling" section).
    // During load, they are migrated to TAA + the corresponding upscaler (CAS -> sharpening): AntiAliasing::MigrateLegacy

    // Upscaler mode (Unity: Render Scale + Upscaling Filter, Unreal: Screen Percentage + temporal / spatial upscaler).
    // Values are serialized as numbers into scene files: ALWAYS append new modes at the end.
    enum class UpscalerMode : int
    {
        None = 0,       // native resolution
        FSR1,           // AMD FidelityFX Super Resolution 1.0 (EASU upscale + RCAS sharpening)
        CAS,            // AMD FidelityFX Contrast Adaptive Sharpening scaling
        NIS,            // NVIDIA Image Scaling
        TAAU,           // Temporal AA Upsampling
        TSR,            // Temporal Super Resolution (Epic)
        DLSS,           // NVIDIA DLSS
        XeSS,           // Intel XeSS
        MetalFX,        // Apple MetalFX
        PSSR,           // Sony PlayStation Spectral Super Resolution
        FSR2,           // AMD FidelityFX Super Resolution 2.2 (temporal)
        Bilinear,       // Unity URP Upscaling Filter: bilinear
        NearestNeighbor,// Unity URP Upscaling Filter: nearest neighbor (pixel art)
        Lanczos,        // Unreal r.Upscale.Quality / Godot: Lanczos-2 (ringing suppression)
        SGSR,           // Qualcomm Snapdragon Game Super Resolution 1 (spatial, open source)
        SGSR2,          // Qualcomm Snapdragon Game Super Resolution 2 (temporal, open source, fragment-only)
        Count
    };

    // Upscaler quality preset -> internal resolution scale (DLSS / FSR / XeSS naming conventions)
    enum class UpscaleQualityMode : int
    {
        UltraQuality = 0,   // 77%
        Quality,            // 67%
        Balanced,           // 59%
        Performance,        // 50%
        UltraPerformance,   // 33%
        Custom,             // Render Scale slider
        Count
    };

    // Sharpening method
    enum class SharpenMethod : int
    {
        EdgeAware = 0,   // engine edge-aware sharpening
        CAS,             // AMD FidelityFX Contrast Adaptive Sharpening
        Count
    };

    // Full Screen Pass (gathered from components in the scene)
    struct FullScreenPassEntry
    {
        std::string Shader;
        float Intensity = 1.0f;
        bool AfterPostProcess = false;
    };

    struct PostProcessSettings
    {
        bool Enabled = true;
        std::vector<FullScreenPassEntry> FullScreenPasses;   // not serialized

        Tonemapper Tonemap = Tonemapper::Neutral;
        float Exposure = 0.0f;        // EV (2^Exposure multiplier)

        bool Bloom = true;
        float BloomThreshold = 1.0f;
        float BloomKnee = 0.5f;
        float BloomIntensity = 0.6f;
        float BloomScatter = 0.7f;    // upsample blend ratio
        glm::vec3 BloomTint = glm::vec3(1.0f);

        bool AmbientOcclusion = true;
        float AORadius = 0.6f;
        float AOIntensity = 1.0f;
        float AOPower = 1.5f;

        bool ColorGrading = true;
        float Contrast = 1.0f;
        float Saturation = 1.0f;
        float Temperature = 0.0f;     // -1 cool .. +1 warm
        float Tint = 0.0f;            // -1 green .. +1 magenta
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
        SharpenMethod SharpenMode = SharpenMethod::EdgeAware;

        AntiAliasingMode AntiAliasing = AntiAliasingMode::FXAA;
        int MSAASamples = 4;              // MSAA sample count (2, 4, or 8)
        float TAAFeedback = 0.9f;         // history weight (higher = smoother, risk of ghosting)
        float TAASharpness = 0.25f;
        // Temporal stability for single-frame AA techniques (FXAA, SMAA, MSAA ...): fine details that flicker during movement
        // (inter-cube gaps, distant stair edges, thin lines) are blended with previous frames; static image remains identical
        bool TemporalStability = true;
        float SSAAScale = 2.0f;           // SSAA / OGSSAA: super-sampling multiplier per axis (1.5 .. 4; OGSSAA rounds to integer)
        UpscalerMode Upscaler = UpscalerMode::None;
        UpscaleQualityMode UpscaleQuality = UpscaleQualityMode::Quality;
        float UpscaleRenderScale = 0.67f; // Internal resolution ratio in Custom quality mode (0.33 .. 1)
        float UpscaleSharpness = 0.5f;    // FSR (RCAS) / NIS / CAS upscaling sharpness (0 .. 1)
        int EQAASamples = 8;              // EQAA coverage samples (4 / 8 / 16); color storage samples are half

        // Screen Space Reflections (SSR): glossy ground / water / metal surfaces reflect the scene
        bool ScreenSpaceReflections = false;
        float SSRIntensity = 1.0f;
        float SSRMaxDistance = 40.0f;     // world units
        float SSRThickness = 0.5f;        // surface thickness tolerance
        float SSRMaxRoughness = 0.6f;     // rougher surfaces do not reflect

        // Ray Traced GI (Lumen-style dynamic global illumination; Renderer/RayTracing/RayTracedGI.h): light probes around
        // the camera trace rays into the scene (sky, direct light, emissive surfaces, multi-bounce); replaces ambient light
        bool RayTracedGI = false;
        float RTGIIntensity = 1.0f;        // indirect light multiplier (Unreal Indirect Lighting Intensity)
        float RTGIProbeSpacing = 2.0f;     // probe spacing (m): smaller = higher detail, more probes
        float RTGIViewDistance = 32.0f;    // horizontal half-extent of probe volume (m); ambient light beyond this
        int RTGIRaysPerProbe = 128;        // rays per probe (Final Gather Quality)
        float RTGIUpdateSpeed = 1.0f;      // adaptation speed to lighting changes (Scene Lighting Update Speed)
        float RTGISkyLeaking = 0.0f;       // fraction of sky light leaking into enclosed spaces (Skylight Leaking)
        float RTGIDiffuseBoost = 1.0f;     // bounce surface color multiplier (Diffuse Color Boost)

        // Screen space global illumination (similar to Lumen screen traces): single-bounce indirect light between surfaces
        bool ScreenSpaceGI = false;
        float SSGIIntensity = 1.0f;
        float SSGIRadius = 4.0f;          // ray length (m)
        int SSGIRays = 4;                 // rays per pixel (half resolution)

        // Volumetric fog (Unreal Exponential Height Fog + Volumetric Fog): shadowed sun shafts
        bool VolumetricFog = false;
        float VolumetricFogDensity = 0.01f;
        float VolumetricFogHeightFalloff = 0.1f;
        float VolumetricFogBaseHeight = 0.0f;
        float VolumetricFogAnisotropy = 0.2f;   // forward scattering (Henyey-Greenstein g)
        float VolumetricFogIntensity = 1.0f;    // sun scattering multiplier
        float VolumetricFogDistance = 80.0f;
        glm::vec3 VolumetricFogAlbedo = glm::vec3(1.0f);
        float VolumetricFogAmbient = 0.3f;      // ambient (sky) light contribution to fog

        // Volumetric clouds (raymarched, wind-driven)
        bool VolumetricClouds = false;
        float CloudCoverage = 0.45f;      // 0..1
        float CloudDensity = 1.0f;
        float CloudBottom = 1500.0f;      // m
        float CloudThickness = 1800.0f;   // m
        float CloudScale = 1.0f;          // shape scale (larger = smaller clouds)
        float CloudWindSpeed = 12.0f;     // m/s
        float CloudWindDirection = 30.0f; // degrees
        glm::vec3 CloudColor = glm::vec3(1.0f);

        // Depth of field (bokeh): regions outside focus distance become blurred
        bool DepthOfField = false;
        float FocusDistance = 10.0f;      // m
        float Aperture = 2.8f;            // f-stop (smaller = more blur)
        float FocalLength = 50.0f;        // mm
        float DOFMaxBlur = 10.0f;         // pixels (relative to 1080p)

        // Motion blur (camera movement, reprojection from depth)
        bool MotionBlur = false;
        float MotionBlurIntensity = 0.5f; // exposure time / frame duration
        float MotionBlurMax = 32.0f;      // pixels

        // Auto exposure (eye adaptation): adjusts exposure over time based on average scene luminance
        bool AutoExposure = false;
        float AutoExposureMin = -3.0f;    // EV
        float AutoExposureMax = 3.0f;     // EV
        float AutoExposureSpeedUp = 3.0f;
        float AutoExposureSpeedDown = 1.0f;
        float AutoExposureCompensation = 0.0f;  // EV
    };

    enum class SkyMode : int
    {
        Auto = 0,      // uses skybox if present; otherwise procedural sky if 3D content exists
        Procedural,
        SolidColor,
        Atmosphere     // physical sky atmosphere (Rayleigh + Mie + ozone; Unreal Sky Atmosphere)
    };

    struct EnvironmentSettings
    {
        SkyMode Sky = SkyMode::Auto;
        glm::vec3 BackgroundColor = glm::vec3(0.1f);
        bool ImageBasedLighting = true;   // diffuse + specular IBL from skybox
        float ReflectionIntensity = 1.0f;
        float SkyboxIntensity = 1.0f;     // ambient / IBL contribution
        bool SpecularOcclusion = true;

        // Sky Atmosphere (Sky = Atmosphere)
        float AtmosphereRayleigh = 1.0f;  // blue sky scattering
        float AtmosphereMie = 1.0f;       // haze and sun halo
        float AtmosphereMieAnisotropy = 0.8f;
        float AtmosphereOzone = 1.0f;     // twilight absorption color
        float AtmosphereSunDisk = 1.0f;   // sun disk size
        float AtmosphereIntensity = 1.0f;
        glm::vec3 AtmosphereGroundAlbedo = glm::vec3(0.12f);
    };

    // Baking (lightmap / APV) settings in the Lighting window
    struct LightBakeSettings
    {
        // Lighting Settings asset (.cqlighting, Unity Lighting Settings Asset): if populated, settings are loaded
        // from file and any changes made in the Lighting window are saved back to file (shared across scenes)
        std::string SettingsAsset;
        // Default Lightmap Parameters (.cqlmparams); Mesh Renderer can override with its own parameters
        std::string DefaultLightmapParameters;
        bool RealtimeGI = false;          // realtime lightmaps (updated automatically)
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
        int Resolution = 40;              // texels / unit
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
        bool AutoGenerate = false;        // auto-rebake when lights / static geometry change
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
