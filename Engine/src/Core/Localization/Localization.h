#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace YAML { class Emitter; class Node; }
namespace Conqueror { class Texture2D; }

// Yerelleştirme (Godot TranslationServer, Unity Localization string tabloları): çeviri dosyaları (CSV: ilk satır "keys,en,tr,de",
// sonraki satırlar anahtar ve çevirileri; gettext PO: dil başına msgid -> msgstr, msgctxt bağlamı, msgid_plural + Plural-Forms çoğulları)
// Project Settings > Localization'da listelenir. Oyun sistem diline (ya da Locale ayarına) en yakın dili seçer: "tr_TR" yoksa "tr",
// o da yoksa Fallback. UI Text, Text Renderer ve Dropdown (Auto Translate) anahtar ise çevirisini gösterir; betikler Tr / TrN ile çevirir,
// SetLocale ile dili değiştirir. Remaps: bir dokunun / sesin / yazı tipinin dile göre başka dosyası (Godot Localization > Remaps).
// Pseudolocalization: çevrilmemiş ya da taşan metni bulmak için metinleri aksanlı / uzatılmış / ters gösterir (Godot gibi)
namespace Conqueror
{
    struct LocalizationRemap
    {
        std::string Resource;                                        // proje göreli asıl dosya ("Assets/UI/flag.png")
        std::vector<std::pair<std::string, std::string>> Locales;    // dil -> o dilde kullanılacak dosya (proje göreli)
    };

    struct PseudolocalizationSettings
    {
        bool Enabled = false;
        bool Accents = true;            // harfleri aksanlı benzerleriyle değiştir (yazı tipi kapsamı)
        bool DoubleVowels = false;      // ünlüleri ikile (daha uzun diller)
        bool FakeBiDi = false;          // sağdan sola dilleri taklit et (karakterler ters sırada)
        bool Override = false;          // tüm harfleri '*' yap (çevrilmemiş sabit metinler hemen görünür)
        float ExpansionRatio = 0.0f;    // metni bu oranda uzat (çeviri taşmalarını yakalamak için, ör. 0.3)
        std::string Prefix = "[";
        std::string Suffix = "]";
        bool SkipPlaceholders = true;   // %s, %d, {0} gibi yer tutuculara dokunma
    };

    struct LocalizationSettings
    {
        std::vector<std::string> Translations;   // proje göreli .csv / .po
        std::string Locale;                      // boş: sistem dili
        std::string Fallback = "en";             // çeviri bulunamazsa
        std::string TestLocale;                  // düzenleyicide ve Play'de önizleme dili (boş: Locale kuralı)
        std::vector<std::string> LocaleFilter;   // boş değilse yalnız bu diller seçilebilir
        std::vector<LocalizationRemap> Remaps;
        PseudolocalizationSettings Pseudo;
        std::vector<std::string> POTFiles;       // POT üretiminde taranacak sahne / betikler (boş: Assets altındaki hepsi)
    };

    namespace Localization
    {
        CQ_API LocalizationSettings& Settings();
        // Çeviri dosyalarını (yeniden) yükle ve dili seç
        CQ_API void Reload();
        // Anahtarın bu dildeki çevirisi; yoksa anahtarın kendisi (sahte yerelleştirme açıksa dönüştürülmüş hali)
        CQ_API const std::string& Translate(const std::string& key);
        CQ_API const std::string& Translate(const std::string& key, const std::string& context);
        // Çoğul: n'e göre dilin çoğul kuralıyla (PO Plural-Forms) biçim seçilir; çeviri yoksa n == 1 ? key : pluralKey
        CQ_API const std::string& TranslatePlural(const std::string& key, const std::string& pluralKey, int64_t n, const std::string& context = {});
        // Etkin dil (örn. "tr" ya da "pt_BR"); dili değiştirmek bir sonraki çizimde tüm metinleri günceller
        CQ_API void SetLocale(const std::string& locale);
        CQ_API const std::string& GetLocale();
        CQ_API std::vector<std::string> GetLoadedLocales();
        CQ_API int GetKeyCount();
        CQ_API uint64_t GetVersion();          // dil / çeviriler / ayarlar her değiştiğinde artar
        // İşletim sisteminin dili ("tr_TR", "en_US" ...)
        CQ_API std::string SystemLocale();
        // Sahte yerelleştirme ayarları değişti (önbellek temizlenir)
        CQ_API void PseudolocalizationChanged();
        CQ_API std::string Pseudolocalize(const std::string& text);

        // Remaps: etkinken (oyunda, düzenleyicide Play sırasında) dosya yolu etkin dilin karşılığına çevrilir; yoksa aynen döner
        CQ_API void SetRemapsActive(bool active);
        CQ_API bool RemapsActive();
        CQ_API std::string Remap(const std::string& path);
        // Yüklü dokulardan dile göre karşılığı olanları yerinde yeniden yükler (dil değişince, Play başında / sonunda)
        CQ_API void ApplyTextureRemaps();
        // Yeni yüklenen doku: Remaps etkinse ve dilde karşılığı varsa hemen karşılığından yüklenir
        CQ_API void OnTextureLoaded(Texture2D& texture);

        // POT (çeviri şablonu): sahnelerdeki çevrilecek metinler (UI Text, Text Renderer, Dropdown seçenekleri) ve betiklerdeki
        // Tr("...") / TrN("...", "...", n) çağrıları; files boşsa projenin Assets klasörü taranır. Yazılan kayıt sayısı (-1: hata)
        CQ_API int GeneratePOT(const std::string& outPath, const std::vector<std::string>& files = {});

        // Proje ayarları
        CQ_API void Serialize(YAML::Emitter& out);
        CQ_API void Deserialize(const YAML::Node& node);
        CQ_API void ResetToDefaults();
    }
}
