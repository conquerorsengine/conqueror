#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RenderSettings.h"

// Kenar yumuşatma (anti-aliasing) yöntemleri: ad, grup, açıklama ve bu sistemdeki (GPU / sürücü / işletim sistemi)
// kullanılabilirlik. Yöntemin kendisi PostProcessStack'te uygulanır.
namespace Conqueror::AntiAliasing
{
    enum class Group
    {
        Classic,     // klasik (uzamsal / donanımsal)
        Temporal,    // zamansal
        Upscaling,   // modern yükseltme ve yeniden yapılandırma
        Special      // daha özel teknikler
    };

    struct ModeInfo
    {
        AntiAliasingMode Mode;
        const char* Name;          // kısa ad (arayüz / betik / konsol)
        const char* FullName;      // açılım
        Group Category;
        const char* Description;   // ne yapar (tek cümle)
    };

    struct Availability
    {
        bool Available = true;
        const char* Reason = "";   // kullanılamıyorsa neden (arayüzde gösterilir)
    };

    // GPU yetenekleri (ilk çağrıda GL bağlamından okunur)
    struct Caps
    {
        bool Queried = false;
        bool NVCoverage = false;      // GL_NV_framebuffer_multisample_coverage (CSAA)
        bool AMDAdvancedMS = false;   // GL_AMD_framebuffer_multisample_advanced (EQAA)
        bool SampleShading = false;   // GL 4.0 örnek başına gölgelendirme (SGSSAA / RGSS)
        int MaxSamples = 4;
        bool NVIDIA = false;
        bool AMD = false;
        bool Intel = false;
    };

    CQ_API const Caps& GetCaps();
    CQ_API int GetModeCount();
    CQ_API const ModeInfo& GetInfo(AntiAliasingMode mode);
    CQ_API const char* GetGroupName(Group group);
    CQ_API Availability GetAvailability(AntiAliasingMode mode);

    // Seçilen yöntem bu sistemde çalışamıyorsa en yakın gerçek karşılığı (CSAA -> EQAA / 8x MSAA, DLSS -> TSR yoksa TAA ...).
    // Düşüş sebebi günlüğe yöntem başına bir kez yazılır
    CQ_API AntiAliasingMode ResolveForThisSystem(AntiAliasingMode mode);

    // Yöntemin iç çözünürlük çarpanı (SSAA > 1, yükselticiler < 1, diğerleri 1)
    CQ_API float RenderScale(const PostProcessSettings& settings, AntiAliasingMode resolved);
}
