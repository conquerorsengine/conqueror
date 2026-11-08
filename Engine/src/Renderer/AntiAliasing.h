#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RenderSettings.h"

// Kenar yumuşatma (anti-aliasing) ve yükseltme (upscaling) yöntemleri: ad, kategori, açıklama ve bu sistemdeki
// (GPU / sürücü / işletim sistemi) kullanılabilirlik. Unity / Unreal gibi iki ayrı ayar: Anti-Aliasing yerel çözünürlükte
// çalışan yöntemler, Upscaling düşük iç çözünürlükten çıktıya yükselticiler. Yöntemlerin kendisi PostProcessStack'te.
namespace Conqueror::AntiAliasing
{
    // Anti-Aliasing menüsünün kategorileri (menüde alt menü)
    enum class Group
    {
        PostProcess,     // ton eşlenmiş görüntüde uzamsal yöntemler
        Multisample,     // donanımsal çoklu örnekleme
        Supersampling,   // yerelden yüksek çözünürlükte çizim
        Temporal,        // zamansal
        VendorNative,    // üretici yöntemlerinin yerel çözünürlük kipi (DLAA, FSR AA, XeSS AA)
        Legacy           // eski sürümlerde AA listesindeki yükselticiler: menüde yok, yüklenirken dönüştürülür
    };

    struct ModeInfo
    {
        AntiAliasingMode Mode;
        const char* Name;          // kısa ad (arayüz / betik / konsol)
        const char* FullName;      // açılım
        Group Category;
        const char* Description;   // ne yapar (tek cümle)
    };

    struct UpscalerInfo
    {
        UpscalerMode Mode;
        const char* Name;
        const char* FullName;
        const char* Description;
        bool Temporal;             // zamansal yükseltici (kendi geçmişiyle kenar yumuşatmanın yerini alır)
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

    // ---- Anti-Aliasing
    CQ_API int GetModeCount();
    CQ_API const ModeInfo& GetInfo(AntiAliasingMode mode);
    CQ_API const char* GetGroupName(Group group);
    CQ_API Availability GetAvailability(AntiAliasingMode mode);
    CQ_API bool IsSupersampling(AntiAliasingMode mode);
    // Tek kareye bakan (zamansal birikim kullanmayan) yöntem: FXAA, SMAA, MSAA, süper örnekleme ... (None ve zamansal yöntemler
    // hariç). Bunlarda "Temporal Stability" katmanı yanıp sönen ince ayrıntıları sabitleyebilir
    CQ_API bool IsSingleFrame(AntiAliasingMode mode);
    CQ_API bool IsLegacyUpscaler(AntiAliasingMode mode);

    // Seçilen yöntem bu sistemde çalışamıyorsa en yakın gerçek karşılığı (CSAA -> EQAA / 8x MSAA, DLAA -> TAA ...).
    // Düşüş sebebi günlüğe yöntem başına bir kez yazılır
    CQ_API AntiAliasingMode ResolveForThisSystem(AntiAliasingMode mode);

    // ---- Upscaling
    CQ_API int GetUpscalerCount();
    CQ_API const UpscalerInfo& GetUpscalerInfo(UpscalerMode mode);
    CQ_API Availability GetUpscalerAvailability(UpscalerMode mode);
    CQ_API UpscalerMode ResolveUpscalerForThisSystem(UpscalerMode mode);
    CQ_API const char* GetQualityModeName(UpscaleQualityMode mode);
    CQ_API float GetQualityModeScale(UpscaleQualityMode mode, float customScale);

    // Bu karede uygulanacak plan (eski sahnelerdeki AA listesi yükselticileri ayarı değiştirmeden yorumlanır)
    struct Plan
    {
        AntiAliasingMode AA = AntiAliasingMode::None;
        UpscalerMode Upscaler = UpscalerMode::None;
        bool CASSharpen = false;   // keskinleştirme CAS ile
    };
    CQ_API Plan GetPlan(const PostProcessSettings& settings);

    // Eski sahne / betik / konsol değeri AA listesindeki bir yükselticiyse (FSR, DLSS, TSR, CAS ...) ayarı yeni yapıya çevirir:
    // Anti-Aliasing TAA, yükseltici Upscaling'e (CAS yerel çözünürlükte: keskinleştirme). Değişiklik yaptıysa true
    CQ_API bool MigrateLegacy(PostProcessSettings& settings);

    // İç çözünürlük çarpanı: süper örnekleme > 1, yükseltici < 1, diğerleri 1
    CQ_API float RenderScale(const PostProcessSettings& settings, AntiAliasingMode resolvedAA, UpscalerMode resolvedUpscaler);
}
