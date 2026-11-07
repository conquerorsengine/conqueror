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
        using G = Group;

        // Sıra enum ile aynı (değerler sahne dosyalarında sayı olarak saklanır)
        const ModeInfo s_Modes[] = {
            { M::None,     "None",      "Kenar yumuşatma yok",                         G::Classic,   "Hiçbir kenar yumuşatma uygulanmaz." },
            { M::FXAA,     "FXAA",      "Fast Approximate Anti-Aliasing",              G::Classic,   "NVIDIA FXAA 3.11 kalite önayarı (39): ton eşlemeli görüntüde kenar arayıp alt piksel kayması kadar karıştırır." },
            { M::SMAA,     "SMAA",      "Subpixel Morphological Anti-Aliasing",        G::Classic,   "Jimenez SMAA 1x (Ultra): kenar algılama, alan / arama dokularıyla karışım ağırlığı, komşu karışımı." },
            { M::TAA,      "TAA",       "Temporal Anti-Aliasing",                      G::Temporal,  "Alt piksel titreşimi + geçmiş karelerin derinlikten yeniden izdüşümü (Unreal / Unity varsayılanı)." },
            { M::TAA_FXAA, "TAA + FXAA","Temporal Anti-Aliasing + FXAA",               G::Temporal,  "TAA; hızlı hareket eden piksellerde ve geçmişi olmayan yeni açılan alanlarda ek FXAA." },
            { M::MSAA,     "MSAA",      "Multisample Anti-Aliasing",                   G::Classic,   "Donanımsal çoklu örnekleme (2x / 4x / 8x), HDR'de ton eşleme ağırlıklı (Karis) çözüm." },
            { M::CSAA,     "CSAA",      "Coverage Sampled Anti-Aliasing",              G::Classic,   "NVIDIA: renk örneklerinden fazla örtü örneği (8x renk / 16x örtü)." },
            { M::SSAA,     "SSAA",      "Supersampling Anti-Aliasing",                 G::Classic,   "Sahne eksen başına 'SSAA Scale' kat çözünürlükte çizilir, Lanczos-2 süzgeciyle küçültülür." },
            { M::FSAA,     "FSAA",      "Full Scene Anti-Aliasing",                    G::Classic,   "Tüm sahnenin 2x2 süper örneklenmesi (SSAA'nın tarihsel adı), çadır (tent) süzgeciyle küçültme." },
            { M::EQAA,     "EQAA",      "Enhanced Quality Anti-Aliasing",              G::Classic,   "AMD: renk depolama örneğinden fazla kapsam örneği (ör. 8 kapsam / 4 renk)." },
            { M::MLAA,     "MLAA",      "Morphological Anti-Aliasing",                 G::Classic,   "Reshetov / Jimenez GPU MLAA: dik (yatay / dikey) kenar desenlerinin alan tabanlı karışımı." },
            { M::CMAA,     "CMAA",      "Conservative Morphological Anti-Aliasing",    G::Classic,   "Intel CMAA2: yalnız belirgin kenarlarda, görüntüyü bulanıklaştırmadan şekil tabanlı yumuşatma." },
            { M::TXAA,     "TXAA",      "Temporal Anti-Aliasing (NVIDIA tarzı)",       G::Temporal,  "4x MSAA + geniş (çadır) MSAA çözümü + TAA birikimi: titreşimli, kenarda daha yumuşak." },
            { M::TSAA,     "TSAA",      "Temporal Super Sampling Anti-Aliasing",       G::Temporal,  "İki titreşim konumu; iki karenin yeniden izdüşümlü eşit ortalaması (2x zamansal süper örnekleme)." },
            { M::TAAU,     "TAAU",      "Temporal Anti-Aliasing Upsampling",           G::Temporal,  "Düşük iç çözünürlükte çizim; TAA tam çözünürlüğe yeniden kurar." },
            { M::SMAA_T2X, "SMAA T2x",  "SMAA Temporal 2x",                            G::Temporal,  "SMAA 1x + iki alt örnek konumlu zamansal çözüm." },
            { M::TSRAA,    "TSRAA",     "Temporal Super Resolution Anti-Aliasing",     G::Temporal,  "TSR yerel çözünürlükte (yalnız kenar yumuşatma)." },
            { M::TSR,      "TSR",       "Temporal Super Resolution (Epic Games)",      G::Upscaling, "Zamansal yükseltme: düşük iç çözünürlük, geçmişten tam çözünürlük." },
            { M::DLSS,     "DLSS",      "Deep Learning Super Sampling (NVIDIA)",       G::Upscaling, "NVIDIA RTX tensör çekirdekleriyle yapay zekâ yükseltme." },
            { M::DLAA,     "DLAA",      "Deep Learning Anti-Aliasing (NVIDIA)",        G::Upscaling, "DLSS yerel çözünürlükte (yalnız kenar yumuşatma)." },
            { M::FSR,      "FSR",       "FidelityFX Super Resolution 1 (AMD)",         G::Upscaling, "TAA'lı düşük iç çözünürlük + AMD FSR 1 EASU kenar uyarlamalı yükseltme + RCAS keskinlik." },
            { M::XeSS,     "XeSS",      "Xe Super Sampling (Intel)",                   G::Upscaling, "Intel XMX / DP4a yapay zekâ yükseltme." },
            { M::PSSR,     "PSSR",      "PlayStation Spectral Super Resolution (Sony)",G::Upscaling, "PS5 Pro yapay zekâ yükseltme." },
            { M::MetalFX,  "MetalFX",   "MetalFX (Apple)",                             G::Upscaling, "Apple Metal uzamsal / zamansal yükseltme." },
            { M::NIS,      "NIS",       "NVIDIA Image Scaling",                        G::Upscaling, "TAA'lı düşük iç çözünürlük + NVIDIA NIS 6 dokunuşlu ölçekleme ve keskinlik (açık kaynak, her GPU)." },
            { M::CAS,      "CAS",       "Contrast Adaptive Sharpening (AMD)",          G::Upscaling, "TAA + AMD FidelityFX CAS: yerel kontrasta göre keskinleştirme (isteğe bağlı yükseltme)." },
            { M::FSRAA,    "FSR AA",    "FidelityFX Super Resolution Native AA (AMD)", G::Upscaling, "FSR 2 yerel çözünürlükte (yalnız kenar yumuşatma)." },
            { M::XeSSAA,   "XeSS AA",   "XeSS Native AA / XeAA (Intel)",               G::Upscaling, "XeSS yerel çözünürlükte (yalnız kenar yumuşatma)." },
            { M::OGSSAA,   "OGSSAA",    "Ordered Grid SSAA",                           G::Special,   "Tam sayı sıralı ızgara (2x2 / 3x3 / 4x4) süper örnekleme, kutu süzgeciyle küçültme." },
            { M::SGSSAA,   "SGSSAA",    "Sparse Grid SSAA",                            G::Special,   "8x MSAA'nın seyrek ızgara örnek konumlarında her örnekte ayrı gölgelendirme (gerçek süper örnekleme)." },
            { M::RGSS,     "RGSS",      "Rotated Grid Super Sampling",                 G::Special,   "4x döndürülmüş ızgara örnek konumlarında her örnekte ayrı gölgelendirme." },
            { M::TSSAA,    "TSSAA",     "Temporal Super Sampling Anti-Aliasing",       G::Special,   "id Tech 8TX tarzı: 8 titreşim konumu, standart varyans kırpması, kenar korumalı birikim." },
            { M::DEAA,     "DEAA",      "Directionally Enhanced Anti-Aliasing",        G::Special,   "Kenarın yönü (Sobel) bulunur; yalnız kenar boyunca kısa ve uzun örneklerle yumuşatılır." },
            { M::GBAA,     "GBAA",      "Geometry Buffer Anti-Aliasing",               G::Special,   "Humus GBAA: geometri geçişinde üçgen kenarına uzaklık yazılır, kapsam kadar komşuyla karıştırılır." },
            { M::SRAA,     "SRAA",      "Subpixel Reconstruction Anti-Aliasing",       G::Special,   "Çok örnekli derinlik / normal, tek örnekli gölgelendirme; alt örnekler geometri benzerliğiyle kurulur." },
        };
        static_assert(sizeof(s_Modes) / sizeof(s_Modes[0]) == (size_t)AntiAliasingMode::Count, "AA mode table out of sync");

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
            case Group::Classic:   return "Klasik";
            case Group::Temporal:  return "Zamansal";
            case Group::Upscaling: return "Modern yükseltme / yeniden yapılandırma";
            case Group::Special:   return "Daha özel teknikler";
        }
        return "";
    }

    Availability GetAvailability(AntiAliasingMode mode)
    {
        const Caps& c = GetCaps();
        switch (mode)
        {
            case M::CSAA:
                if (!c.NVCoverage)
                    return { false, "NVIDIA'ya özel (GL_NV_framebuffer_multisample_coverage); bu GPU'da yok. Seçilirse en yakın karşılığı (AMD'de EQAA, diğerlerinde 8x MSAA) kullanılır." };
                return {};
            case M::EQAA:
                if (!c.AMDAdvancedMS)
                    return { false, "AMD'ye özel (GL_AMD_framebuffer_multisample_advanced); bu GPU'da yok. Seçilirse 8x MSAA kullanılır." };
                return {};
            case M::SGSSAA:
            case M::RGSS:
                if (!c.SampleShading || c.MaxSamples < (mode == M::SGSSAA ? 8 : 4))
                    return { false, "Örnek başına gölgelendirme (OpenGL 4.0) ya da yeterli MSAA örneği yok." };
                return {};
            case M::TXAA:
                if (c.MaxSamples < 4)
                    return { false, "4x MSAA desteklenmiyor." };
                return {};
            case M::DLSS:
            case M::DLAA:
                return { false, c.NVIDIA ? "NVIDIA NGX (DLSS SDK) entegrasyonu yok: NGX yalnız Vulkan / DirectX'te çalışır, motor OpenGL ile çiziyor."
                                         : "NVIDIA RTX ekran kartı ve NGX (DLSS SDK, Vulkan / DirectX) gerekir; bu GPU'da çalışamaz." };
            case M::XeSS:
            case M::XeSSAA:
                return { false, "Intel XeSS SDK yalnız Windows'ta (DirectX 11/12, Vulkan) çalışır; Linux / OpenGL sürümü yok." };
            case M::PSSR:
                return { false, "Yalnız PlayStation 5 Pro'da (Sony'nin gizlilik sözleşmeli SDK'sı) çalışır." };
            case M::MetalFX:
                return { false, "Yalnız Apple Metal'de (macOS / iOS) çalışır." };
            case M::NIS:
            case M::CMAA:
            case M::TAAU:
            case M::SMAA_T2X:
            case M::TSRAA:
            case M::TSR:
            case M::FSRAA:
            case M::GBAA:
            case M::SRAA:
                return { false, "Bu sürümde henüz uygulanmadı (sırada)." };
            default:
                return {};
        }
    }

    AntiAliasingMode ResolveForThisSystem(AntiAliasingMode mode)
    {
        if ((int)mode < 0 || (int)mode >= (int)AntiAliasingMode::Count)
            return AntiAliasingMode::TAA;
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
            case M::DLSS:
            case M::XeSS:
            case M::PSSR:
            case M::MetalFX:
            case M::TSR:
            case M::NIS:
            case M::TAAU:     fallback = M::FSR; break;
            default:          fallback = M::TAA; break;   // DLAA / XeSS AA / FSR AA / TSRAA: yerel çözünürlükte zamansal
        }
        static std::set<int> s_Logged;
        if (s_Logged.insert((int)mode).second)
            CQ_CORE_WARN("AntiAliasing: {0} bu sistemde yok ({1}) -> {2} kullanılıyor", GetInfo(mode).Name, GetAvailability(mode).Reason, GetInfo(fallback).Name);
        return fallback;
    }

    float RenderScale(const PostProcessSettings& settings, AntiAliasingMode resolved)
    {
        switch (resolved)
        {
            case M::SSAA:   return std::clamp(settings.SSAAScale, 1.25f, 4.0f);
            case M::OGSSAA: return std::clamp(std::round(settings.SSAAScale), 2.0f, 4.0f);
            case M::FSAA:   return 2.0f;
            case M::FSR:
            case M::NIS:    return std::clamp(settings.UpscaleRenderScale, 0.5f, 1.0f);
            case M::CAS:    return std::clamp(settings.UpscaleRenderScale, 0.5f, 1.0f) >= 0.999f ? 1.0f : std::clamp(settings.UpscaleRenderScale, 0.5f, 1.0f);
            default:        return 1.0f;
        }
    }
}
