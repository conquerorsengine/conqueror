#include "AntiAliasing.h"

#include "Core/Logging/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>

namespace Conqueror::AntiAliasing
{
    namespace
    {
        using M = AntiAliasingMode;
        using U = UpscalerMode;
        using G = Group;

        // Sıra enum ile aynı (değerler sahne dosyalarında sayı olarak saklanır)
        const ModeInfo s_Modes[] = {
            { M::None,     "None",       "No Anti-Aliasing",                               G::PostProcess,   "No anti-aliasing is applied." },
            { M::FXAA,     "FXAA",       "Fast Approximate Anti-Aliasing",                 G::PostProcess,   "NVIDIA FXAA 3.11 quality preset 39: finds edges in the tonemapped image and blends across them by the estimated sub-pixel offset." },
            { M::SMAA,     "SMAA",       "Subpixel Morphological Anti-Aliasing",           G::PostProcess,   "Jimenez SMAA 1x (Ultra): edge detection, area and search textures for the blend weights, then neighborhood blending." },
            { M::TAA,      "TAA",        "Temporal Anti-Aliasing",                         G::Temporal,      "Sub-pixel jitter plus depth reprojection of previous frames (the Unreal and Unity default)." },
            { M::TAA_FXAA, "TAA + FXAA", "Temporal Anti-Aliasing + FXAA",                  G::Temporal,      "TAA with an extra FXAA pass on fast-moving pixels and on newly revealed areas that have no history yet." },
            { M::MSAA,     "MSAA",       "Multisample Anti-Aliasing",                      G::Multisample,   "Hardware multisampling (2x / 4x / 8x) with a tonemap-weighted (Karis) resolve for HDR." },
            { M::CSAA,     "CSAA",       "Coverage Sampled Anti-Aliasing",                 G::Multisample,   "NVIDIA: more coverage samples than color samples (8x color / 16x coverage)." },
            { M::SSAA,     "SSAA",       "Supersampling Anti-Aliasing",                    G::Supersampling, "Renders the scene at 'SSAA Scale' times the resolution per axis and downsamples it with a Lanczos-2 filter." },
            { M::FSAA,     "FSAA",       "Full Scene Anti-Aliasing",                       G::Supersampling, "2x2 supersampling of the whole scene (the historical name of SSAA), downsampled with a tent filter." },
            { M::EQAA,     "EQAA",       "Enhanced Quality Anti-Aliasing",                 G::Multisample,   "AMD: more coverage samples than stored color samples (for example 8 coverage / 4 color)." },
            { M::MLAA,     "MLAA",       "Morphological Anti-Aliasing",                    G::PostProcess,   "Reshetov / Jimenez GPU MLAA: area-based blending of horizontal and vertical edge patterns." },
            { M::CMAA,     "CMAA",       "Conservative Morphological Anti-Aliasing",       G::PostProcess,   "Intel CMAA2: shape-based smoothing of clear edges only, without blurring the rest of the image." },
            { M::TXAA,     "TXAA",       "Temporal Multisample Anti-Aliasing",             G::Temporal,      "NVIDIA TXAA style: 4x MSAA with a wide tent resolve plus TAA accumulation (softer, more stable edges)." },
            { M::TSAA,     "TSAA",       "Temporal Super Sampling Anti-Aliasing",          G::Temporal,      "Two jitter positions; equal-weight reprojected average of two frames (2x temporal supersampling)." },
            { M::TAAU,     "TAAU",       "Temporal Anti-Aliasing Upsampling",              G::Legacy,        "Moved to Upscaling > TAAU." },
            { M::SMAA_T2X, "SMAA T2x",   "SMAA Temporal 2x",                               G::Temporal,      "SMAA 1x plus a temporal resolve with two sub-sample positions." },
            { M::TSRAA,    "TSRAA",      "Temporal Super Resolution Anti-Aliasing",        G::Temporal,      "TSR at native resolution (anti-aliasing only)." },
            { M::TSR,      "TSR",        "Temporal Super Resolution",                      G::Legacy,        "Moved to Upscaling > TSR." },
            { M::DLSS,     "DLSS",       "Deep Learning Super Sampling",                   G::Legacy,        "Moved to Upscaling > DLSS." },
            { M::DLAA,     "DLAA",       "Deep Learning Anti-Aliasing (NVIDIA)",           G::VendorNative,  "DLSS at native resolution (anti-aliasing only)." },
            { M::FSR,      "FSR",        "FidelityFX Super Resolution",                    G::Legacy,        "Moved to Upscaling > FSR 1." },
            { M::XeSS,     "XeSS",       "Xe Super Sampling",                              G::Legacy,        "Moved to Upscaling > XeSS." },
            { M::PSSR,     "PSSR",       "PlayStation Spectral Super Resolution",          G::Legacy,        "Moved to Upscaling > PSSR." },
            { M::MetalFX,  "MetalFX",    "MetalFX",                                        G::Legacy,        "Moved to Upscaling > MetalFX." },
            { M::NIS,      "NIS",        "NVIDIA Image Scaling",                           G::Legacy,        "Moved to Upscaling > NIS." },
            { M::CAS,      "CAS",        "Contrast Adaptive Sharpening",                   G::Legacy,        "Moved to Sharpening > CAS (and Upscaling > CAS)." },
            { M::FSRAA,    "FSR AA",     "FidelityFX Super Resolution Native AA (AMD)",    G::VendorNative,  "AMD FSR 2.2 at native resolution (anti-aliasing only, temporal)." },
            { M::XeSSAA,   "XeSS AA",    "XeSS Native AA / XeAA (Intel)",                  G::VendorNative,  "XeSS at native resolution (anti-aliasing only)." },
            { M::OGSSAA,   "OGSSAA",     "Ordered Grid Supersampling Anti-Aliasing",       G::Supersampling, "Integer ordered-grid supersampling (2x2 / 3x3 / 4x4), downsampled with a box filter." },
            { M::SGSSAA,   "SGSSAA",     "Sparse Grid Supersampling Anti-Aliasing",        G::Supersampling, "Shades every sample at the sparse-grid positions of 8x MSAA (true supersampling)." },
            { M::RGSS,     "RGSS",       "Rotated Grid Super Sampling",                    G::Supersampling, "Shades every sample at 4x rotated-grid positions." },
            { M::TSSAA,    "TSSAA",      "Temporal Super Sampling Anti-Aliasing",          G::Temporal,      "id Tech 8TX style: 8 jitter positions, standard variance clipping and edge-preserving accumulation." },
            { M::DEAA,     "DEAA",       "Directionally Enhanced Anti-Aliasing",           G::PostProcess,   "Finds the edge direction (Sobel) and smooths only along the edge with short and long taps." },
            { M::GBAA,     "GBAA",       "Geometry Buffer Anti-Aliasing",                  G::PostProcess,   "Humus GBAA: the geometry pass writes the distance to the triangle edge; pixels blend with their neighbor by that coverage." },
            { M::SRAA,     "SRAA",       "Subpixel Reconstruction Anti-Aliasing",          G::Multisample,   "Multisampled depth and normals with single-sample shading; sub-samples are reconstructed by geometric similarity." },
        };
        static_assert(sizeof(s_Modes) / sizeof(s_Modes[0]) == (size_t)AntiAliasingMode::Count, "AA mode table out of sync");

        const UpscalerInfo s_Upscalers[] = {
            { U::None,    "None",    "Native Resolution",                               "Renders at the output resolution (no upscaling).", false },
            { U::FSR1,    "FSR 1",   "AMD FidelityFX Super Resolution 1.0",             "Spatial upscaler: EASU edge-adaptive upscaling plus RCAS sharpening (open source, any GPU).", false },
            { U::CAS,     "CAS",     "AMD FidelityFX Contrast Adaptive Sharpening",     "Spatial upscaler: contrast adaptive sharpening with scaling (open source, any GPU).", false },
            { U::NIS,     "NIS",     "NVIDIA Image Scaling",                            "Spatial upscaler: 6-tap directional scaling plus adaptive sharpening, then FXAA at output resolution to smooth the stair steps left by upscaling (open source, any GPU; at most 2x per axis).", false },
            { U::TAAU,    "TAAU",    "Temporal Anti-Aliasing Upsampling",               "TAA reconstructs the output resolution from jittered low-resolution frames.", true },
            { U::TSR,     "TSR",     "Temporal Super Resolution (Epic Games)",          "Temporal upscaler: reconstructs the output resolution from history (the Unreal Engine 5 default).", true },
            { U::DLSS,    "DLSS",    "Deep Learning Super Sampling (NVIDIA)",           "AI upscaling on NVIDIA RTX tensor cores.", true },
            { U::XeSS,    "XeSS",    "Xe Super Sampling (Intel)",                       "AI upscaling on Intel XMX / DP4a.", true },
            { U::MetalFX, "MetalFX", "MetalFX (Apple)",                                 "Apple Metal spatial / temporal upscaling.", true },
            { U::PSSR,    "PSSR",    "PlayStation Spectral Super Resolution (Sony)",    "PS5 Pro AI upscaling.", true },
            { U::FSR2,    "FSR 2",   "AMD FidelityFX Super Resolution 2.2",             "Temporal upscaler: reconstructs the output resolution from jittered frames, depth and motion vectors (open source, any GPU).", true },
        };
        static_assert(sizeof(s_Upscalers) / sizeof(s_Upscalers[0]) == (size_t)UpscalerMode::Count, "Upscaler table out of sync");


        Caps s_Caps;

        bool HasExtension(const char* name)
        {
            GLint n = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            for (GLint i = 0; i < n; ++i)
            {
                const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
                if (e && std::strcmp(e, name) == 0)
                    return true;
            }
            return false;
        }

        const char* DLSSReason()
        {
            return GetCaps().NVIDIA ? "NVIDIA NGX (DLSS SDK) is not integrated: NGX runs only on Vulkan / DirectX and the engine renders with OpenGL."
                                    : "Requires an NVIDIA RTX GPU and NGX (DLSS SDK, Vulkan / DirectX); it cannot run on this GPU.";
        }
        constexpr const char* kXeSSReason = "The Intel XeSS SDK runs only on Windows (DirectX 11/12, Vulkan); there is no Linux / OpenGL version.";
        constexpr const char* kMetalFXReason = "Runs only on Apple Metal (macOS / iOS).";
        constexpr const char* kPSSRReason = "Runs only on PlayStation 5 Pro (Sony's NDA SDK).";
        constexpr const char* kTSRReason = "Epic's TSR source is only available under the Unreal Engine EULA and cannot be shipped in another engine.";

        // AA listesindeki eski yükselticinin Upscaling karşılığı
        UpscalerMode LegacyUpscaler(AntiAliasingMode mode)
        {
            switch (mode)
            {
                case M::FSR:     return U::FSR1;
                case M::NIS:     return U::NIS;
                case M::TAAU:    return U::TAAU;
                case M::TSR:     return U::TSR;
                case M::DLSS:    return U::DLSS;
                case M::XeSS:    return U::XeSS;
                case M::PSSR:    return U::PSSR;
                case M::MetalFX: return U::MetalFX;
                default:         return U::None;
            }
        }
    }

    const Caps& GetCaps()
    {
        if (s_Caps.Queried)
            return s_Caps;
        s_Caps.Queried = true;
        const char* vendor = (const char*)glGetString(GL_VENDOR);
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        std::string v = std::string(vendor ? vendor : "") + " " + (renderer ? renderer : "");
        std::string lower = v;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        s_Caps.NVIDIA = lower.find("nvidia") != std::string::npos;
        s_Caps.AMD = lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos || lower.find("ati ") != std::string::npos;
        s_Caps.Intel = lower.find("intel") != std::string::npos;
        s_Caps.NVCoverage = HasExtension("GL_NV_framebuffer_multisample_coverage");
        s_Caps.AMDAdvancedMS = HasExtension("GL_AMD_framebuffer_multisample_advanced") && glNamedRenderbufferStorageMultisampleAdvancedAMD != nullptr;
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        s_Caps.SampleShading = (major > 4 || (major == 4 && minor >= 0)) && glMinSampleShading != nullptr;
        GLint maxSamples = 4;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        s_Caps.MaxSamples = std::max(1, (int)maxSamples);
        CQ_CORE_INFO("AntiAliasing: {0} | CSAA {1}, EQAA {2}, sample shading {3}, max samples {4}", v, s_Caps.NVCoverage, s_Caps.AMDAdvancedMS,
                     s_Caps.SampleShading, s_Caps.MaxSamples);
        return s_Caps;
    }

    int GetModeCount() { return (int)AntiAliasingMode::Count; }

    const ModeInfo& GetInfo(AntiAliasingMode mode)
    {
        int i = std::clamp((int)mode, 0, (int)AntiAliasingMode::Count - 1);
        return s_Modes[i];
    }

    const char* GetGroupName(Group group)
    {
        switch (group)
        {
            case Group::PostProcess:   return "Post-Process";
            case Group::Multisample:   return "Multisample";
            case Group::Supersampling: return "Supersampling";
            case Group::Temporal:      return "Temporal";
            case Group::VendorNative:  return "Vendor Native AA";
            case Group::Legacy:        return "Legacy";
        }
        return "";
    }

    bool IsSupersampling(AntiAliasingMode mode)
    {
        return mode == M::SSAA || mode == M::FSAA || mode == M::OGSSAA;
    }

    bool IsSingleFrame(AntiAliasingMode mode)
    {
        switch (mode)
        {
            case M::FXAA: case M::SMAA: case M::MSAA: case M::CSAA: case M::SSAA: case M::FSAA: case M::EQAA: case M::MLAA:
            case M::CMAA: case M::OGSSAA: case M::SGSSAA: case M::RGSS: case M::DEAA: case M::GBAA: case M::SRAA:
                return true;
            default:
                return false;
        }
    }

    bool IsPostProcess(AntiAliasingMode mode)
    {
        return mode == M::FXAA || mode == M::SMAA || mode == M::MLAA || mode == M::CMAA || mode == M::DEAA;
    }

    bool IsLegacyUpscaler(AntiAliasingMode mode)
    {
        return GetInfo(mode).Category == Group::Legacy;
    }

    Availability GetAvailability(AntiAliasingMode mode)
    {
        const Caps& c = GetCaps();
        switch (mode)
        {
            case M::CSAA:
                if (!c.NVCoverage)
                    return { false, "NVIDIA only (GL_NV_framebuffer_multisample_coverage), not available on this GPU. If selected, the closest equivalent is used (EQAA on AMD, 8x MSAA elsewhere)." };
                return {};
            case M::EQAA:
                if (!c.AMDAdvancedMS)
                    return { false, "AMD only (GL_AMD_framebuffer_multisample_advanced), not available on this GPU. If selected, 8x MSAA is used." };
                return {};
            case M::SGSSAA:
            case M::RGSS:
                if (!c.SampleShading || c.MaxSamples < (mode == M::SGSSAA ? 8 : 4))
                    return { false, "Requires per-sample shading (OpenGL 4.0) and enough MSAA samples." };
                return {};
            case M::TXAA:
                if (c.MaxSamples < 4)
                    return { false, "4x MSAA is not supported." };
                return {};
            case M::DLAA:
                return { false, DLSSReason() };
            case M::XeSSAA:
                return { false, kXeSSReason };
            case M::TSRAA:
                return { false, kTSRReason };
            case M::SRAA:
                if (c.MaxSamples < 4)
                    return { false, "4x MSAA textures are not supported." };
                return {};
            default:
                if (IsLegacyUpscaler(mode))
                    return mode == M::CAS ? Availability{} : GetUpscalerAvailability(LegacyUpscaler(mode));
                return {};
        }
    }

    AntiAliasingMode ResolveForThisSystem(AntiAliasingMode mode)
    {
        if ((int)mode < 0 || (int)mode >= (int)AntiAliasingMode::Count)
            return AntiAliasingMode::TAA;
        if (IsLegacyUpscaler(mode))
            return AntiAliasingMode::TAA;   // eski AA listesi yükselticisi: kenar yumuşatma TAA, yükseltici GetPlan'da
        if (GetAvailability(mode).Available)
            return mode;
        AntiAliasingMode fallback = AntiAliasingMode::TAA;
        switch (mode)
        {
            case M::CSAA:     fallback = GetCaps().AMDAdvancedMS ? M::EQAA : M::MSAA; break;
            case M::EQAA:     fallback = M::MSAA; break;
            case M::SGSSAA:
            case M::RGSS:     fallback = M::MSAA; break;
            case M::TXAA:     fallback = M::TAA; break;
            case M::CMAA:
            case M::SMAA_T2X:
            case M::GBAA:
            case M::SRAA:     fallback = M::SMAA; break;
            default:          fallback = M::TAA; break;   // DLAA / XeSS AA / FSR AA / TSRAA: yerel çözünürlükte zamansal
        }
        static std::set<int> s_Logged;
        if (s_Logged.insert((int)mode).second)
            CQ_CORE_WARN("AntiAliasing: {0} is not available on this system ({1}); using {2}", GetInfo(mode).Name, GetAvailability(mode).Reason, GetInfo(fallback).Name);
        return fallback;
    }

    int GetUpscalerCount() { return (int)UpscalerMode::Count; }

    const UpscalerInfo& GetUpscalerInfo(UpscalerMode mode)
    {
        int i = std::clamp((int)mode, 0, (int)UpscalerMode::Count - 1);
        return s_Upscalers[i];
    }

    Availability GetUpscalerAvailability(UpscalerMode mode)
    {
        switch (mode)
        {
            case U::DLSS:    return { false, DLSSReason() };
            case U::XeSS:    return { false, kXeSSReason };
            case U::MetalFX: return { false, kMetalFXReason };
            case U::PSSR:    return { false, kPSSRReason };
            case U::TSR:     return { false, kTSRReason };
            default:         return {};
        }
    }

    UpscalerMode ResolveUpscalerForThisSystem(UpscalerMode mode)
    {
        if ((int)mode < 0 || (int)mode >= (int)UpscalerMode::Count)
            return UpscalerMode::None;
        if (GetUpscalerAvailability(mode).Available)
            return mode;
        // Çalışamayan yükseltici: her GPU'da çalışan uzamsal FSR 1 (aynı iç çözünürlükle)
        static std::set<int> s_Logged;
        if (s_Logged.insert((int)mode).second)
            CQ_CORE_WARN("AntiAliasing: upscaler {0} is not available on this system ({1}); using {2}", GetUpscalerInfo(mode).Name,
                         GetUpscalerAvailability(mode).Reason, GetUpscalerInfo(UpscalerMode::FSR1).Name);
        return UpscalerMode::FSR1;
    }

    const char* GetQualityModeName(UpscaleQualityMode mode)
    {
        switch (mode)
        {
            case UpscaleQualityMode::UltraQuality:     return "Ultra Quality (77%)";
            case UpscaleQualityMode::Quality:          return "Quality (67%)";
            case UpscaleQualityMode::Balanced:         return "Balanced (59%)";
            case UpscaleQualityMode::Performance:      return "Performance (50%)";
            case UpscaleQualityMode::UltraPerformance: return "Ultra Performance (33%)";
            case UpscaleQualityMode::Custom:           return "Custom";
            default:                                   return "";
        }
    }

    float GetQualityModeScale(UpscaleQualityMode mode, float customScale)
    {
        switch (mode)
        {
            case UpscaleQualityMode::UltraQuality:     return 1.0f / 1.3f;
            case UpscaleQualityMode::Quality:          return 1.0f / 1.5f;
            case UpscaleQualityMode::Balanced:         return 1.0f / 1.7f;
            case UpscaleQualityMode::Performance:      return 0.5f;
            case UpscaleQualityMode::UltraPerformance: return 1.0f / 3.0f;
            default:                                   return std::clamp(customScale, 0.33f, 1.0f);
        }
    }

    Plan GetPlan(const PostProcessSettings& settings)
    {
        Plan plan;
        plan.AA = settings.AntiAliasing;
        plan.Upscaler = settings.Upscaler;
        plan.CASSharpen = settings.Sharpen && settings.SharpenMode == SharpenMethod::CAS;
        if (IsLegacyUpscaler(settings.AntiAliasing))
        {
            plan.AA = AntiAliasingMode::TAA;
            if (settings.AntiAliasing == M::CAS)
            {
                if (settings.UpscaleRenderScale < 0.999f)
                    plan.Upscaler = U::CAS;
                else
                    plan.CASSharpen = true;
            }
            else
                plan.Upscaler = LegacyUpscaler(settings.AntiAliasing);
        }
        return plan;
    }

    bool MigrateLegacy(PostProcessSettings& settings)
    {
        if (!IsLegacyUpscaler(settings.AntiAliasing))
            return false;
        const AntiAliasingMode old = settings.AntiAliasing;
        settings.AntiAliasing = AntiAliasingMode::TAA;
        if (old == M::CAS)
        {
            if (settings.UpscaleRenderScale < 0.999f)
            {
                settings.Upscaler = U::CAS;
                settings.UpscaleQuality = UpscaleQualityMode::Custom;
            }
            else
            {
                settings.Sharpen = true;
                settings.SharpenMode = SharpenMethod::CAS;
                settings.SharpenAmount = settings.UpscaleSharpness;
            }
        }
        else
        {
            settings.Upscaler = LegacyUpscaler(old);
            settings.UpscaleQuality = UpscaleQualityMode::Custom;   // eski sürümde iç çözünürlük doğrudan UpscaleRenderScale idi
        }
        return true;
    }

    float RenderScale(const PostProcessSettings& settings, AntiAliasingMode resolvedAA, UpscalerMode resolvedUpscaler)
    {
        // Süper örnekleme yerelden yüksek çözünürlükte çizer: yükseltici bu sırada kullanılmaz
        switch (resolvedAA)
        {
            case M::SSAA:   return std::clamp(settings.SSAAScale, 1.25f, 4.0f);
            case M::OGSSAA: return std::clamp(std::round(settings.SSAAScale), 2.0f, 4.0f);
            case M::FSAA:   return 2.0f;
            default:        break;
        }
        if (resolvedUpscaler == U::None)
            return 1.0f;
        const UpscaleQualityMode quality = IsLegacyUpscaler(settings.AntiAliasing) ? UpscaleQualityMode::Custom : settings.UpscaleQuality;
        const float scale = GetQualityModeScale(quality, settings.UpscaleRenderScale);
        // NIS en çok 2x yükseltir (NVScalerUpdateConfig 0.5 .. 1 oranını kabul eder)
        return resolvedUpscaler == U::NIS ? std::max(scale, 0.5f) : scale;
    }
}
