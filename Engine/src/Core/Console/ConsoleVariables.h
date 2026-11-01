#pragma once

#include "Core/Base/Base.h"

#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Unreal tarzı konsol değişkenleri (CVar) ve konsol komutları: "r.Bloom 0", "t.MaxFPS 60", "stat fps", "slomo 0.5".
// r.* değişkenleri sahnenin kayıtlı ayarlarını DEĞİŞTİRMEZ: oturum boyu geçerli bir üst katman (override) olarak
// çizim sırasında uygulanır (Unreal'daki gibi motor genelinde; sahne kirlenmez, kaydedilmez).
// Proje açılışında <proje>/Config/ConsoleVariables.ini okunur ("r.Bloom=0" satırları).
namespace Conqueror
{
    class Scene;
    struct RenderSettings;
    struct PostProcessSettings;

    enum class CVarType : int { Int = 0, Float, Bool, String };

    struct ConsoleVariable
    {
        std::string Name;
        std::string Description;
        CVarType Type = CVarType::Float;
        std::function<std::string()> Get;                       // geçerli değer (metin)
        std::function<bool(const std::string&)> Set;            // metinden ayarla (geçersizse false)
        std::function<void()> Reset;                            // varsayılana dön (override'ı kaldır)
    };

    // Konsol komutu: argümanlar -> çıktı metni
    using ConsoleCommandFn = std::function<std::string(const std::vector<std::string>& args)>;

    // r.* override değerleri: NaN / -1 = geçersiz kılınmamış (sahne ayarı geçerli)
    struct RenderOverrides
    {
        int Shadows = -1, ShadowResolution = -1, ShadowCascades = -1;
        float ShadowDistance = NAN;
        int Fog = -1;
        float FogDensity = NAN;
        int PostProcess = -1, Bloom = -1, SSAO = -1, AntiAliasing = -1, Tonemapper = -1;
        int Vignette = -1, FilmGrain = -1, ChromaticAberration = -1, Sharpen = -1, ColorGrading = -1;
        float BloomIntensity = NAN, Exposure = NAN, Saturation = NAN, Contrast = NAN;
        int SSR = -1, VolumetricFog = -1, VolumetricClouds = -1, DepthOfField = -1, MotionBlur = -1, AutoExposure = -1;
        int SkyAtmosphere = -1, SSGI = -1;
        float SSGIIntensity = NAN;
    };

    class CQ_API ConsoleSystem
    {
    public:
        static void RegisterVariable(ConsoleVariable variable);
        static void RegisterCommand(const std::string& name, const std::string& description, ConsoleCommandFn run);
        static ConsoleVariable* FindVariable(const std::string& name);       // büyük/küçük harf duyarsız
        // Satırın ilk sözcüğü bilinen bir değişken / komut mu (editör konsolu kabuğa göndermeden önce sorar)
        static bool IsKnown(const std::string& line);
        // "r.Bloom 0" | "r.Bloom" (değeri yaz) | "r.Bloom ?" (açıklama) | "stat fps" | "help r." ...
        // Bilinmeyen komutta false döner ve output'a hata yazar.
        static bool Execute(const std::string& line, std::string& output);
        static std::string ExecuteText(const std::string& line);
        // Önekle eşleşen adlar (otomatik tamamlama)
        static std::vector<std::string> Complete(const std::string& prefix, size_t max = 32);
        static std::vector<std::string> GetVariableNames();
        static std::vector<std::string> GetCommandNames();
        static std::string GetCommandDescription(const std::string& name);

        // Ini / betik dosyasını uygula: [Bölüm] başlıkları ve ';' '#' yorumları yok sayılır, "ad=değer" ya da "ad değer"
        static int ApplyFile(const std::filesystem::path& file);
        // Proje açılınca: <proje>/Config/ConsoleVariables.ini
        static void OnProjectLoaded(const std::filesystem::path& projectDirectory);

        // r.* değişkenlerinin etkilediği sahne (editör / oyun katmanı bağlar)
        static void SetSceneProvider(std::function<Scene*()> provider);
        static Scene* GetScene();

        static RenderOverrides& Overrides();
        static void ApplyRenderOverrides(RenderSettings& settings);
        static void ApplyPostOverrides(PostProcessSettings& post);
        static void ClearOverrides();

        // Motorun yerleşik değişken ve komutları (ilk çağrıda bir kez)
        static void RegisterBuiltins();
    };
}
