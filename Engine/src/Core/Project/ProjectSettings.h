#pragma once

#include "Core/Base/Base.h"
#include "Core/Tiering/QualitySettings.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace YAML { class Emitter; class Node; }

// Unity Project Settings sayfalarının proje düzeyindeki ayarları (.cqproj içinde saklanır).
// Fizik ayarları PhysicsSettings'te, oyuncu / derleme ayarları ProjectConfig'te kalır.
namespace Conqueror
{
    struct AudioProjectSettings
    {
        float GlobalVolume = 1.0f;
        float DopplerFactor = 1.0f;        // tüm 3D seslerin Doppler çarpanı
        float RolloffScale = 1.0f;         // mesafeyle azalmanın çarpanı
        bool DisableAudio = false;
    };

    // Unity Quality > Texture Streaming (Mipmap Streaming): içe aktarmada "Streaming Mipmaps" açık dokular GPU'ya yalnız
    // kameraların gerektirdiği mip seviyesine kadar yüklenir (Renderer/TextureStreaming)
    struct TextureStreamingSettings
    {
        bool Enabled = false;
        bool AddAllCameras = true;      // kapalıysa yalnız Streaming Controller'lı kameralar (ve editörün sahne görünümü)
        float MemoryBudgetMB = 512.0f;  // tüm dokular (akışa girmeyenler dahil)
        int RenderersPerFrame = 512;    // bir karede incelenen Mesh Renderer sayısı (fazlası sonraki karelere)
        int MaxLevelReduction = 2;      // bir doku en çok kaç mip seviyesi küçülebilir; yüklenirken de bu seviyeden başlar
        int MaxIORequests = 1024;       // aynı anda bekleyen çözme isteği üst sınırı
    };

    struct QualityProjectSettings
    {
        int Level = (int)QualityLevel::High;   // Low, Medium, High, Ultra, Custom
        QualityPreset Custom;
        int TargetFrameRate = 0;               // 0 = sınırsız (VSync'e bağlı)
        TextureStreamingSettings Streaming;
    };

    struct GraphicsProjectSettings
    {
        int DefaultTonemapper = 1;             // yeni sahneler: 0 Yok, 1 Neutral, 2 ACES, 3 AgX, 4 Reinhard
        int DefaultSky = 0;                    // yeni sahneler: 0 Prosedürel gökyüzü, 1 Düz renk
        // Preloaded Shaders: oyun başında ısıtılan Shader Variant Collection'lar (.cqshadervariants)
        std::vector<std::string> PreloadedShaders;
    };

    struct NavAreaSetting
    {
        std::string Name;
        float Cost = 1.0f;
    };

    struct NavigationProjectSettings
    {
        std::array<NavAreaSetting, 32> Areas;
        NavigationProjectSettings();
    };

    struct EditorProjectSettings
    {
        int DefaultBehaviorMode = 0;           // 0 3D, 1 2D (yeni sahne şablonu ve görünüm)
        bool NewSceneWithCameraAndLight = true;
        int AutoSaveMinutes = 0;               // 0 = kapalı (kayıtlı sahne, değişiklik varsa)
        std::string ExternalScriptEditor;      // boş = yerleşik kod düzenleyici (ör. "code", "kate", "notepad++")
    };

    struct ScriptOrderEntry
    {
        std::string ClassName;
        int Order = 0;
    };

    // Godot Autoload: oyun başlarken (Play / derlenmiş oyun) sahneden önce oluşturulan ve sahne değişince kalan genel nesneler.
    // Path bir prefab (.cqprefab) ya da betik; nesne Name adıyla oluşur, betikler GetAutoload(Name) / FindEntity(Name) ile ulaşır
    struct AutoloadEntry
    {
        std::string Name;
        std::string Path;      // proje göreli
        bool Enabled = true;
    };

    class CQ_API ProjectSettings
    {
    public:
        static AudioProjectSettings& Audio();
        static QualityProjectSettings& Quality();
        static GraphicsProjectSettings& Graphics();
        static NavigationProjectSettings& Navigation();
        static EditorProjectSettings& Editor();
        static std::vector<ScriptOrderEntry>& ScriptOrder();
        static std::vector<AutoloadEntry>& Autoloads();
        // Scripting Define Symbols: Native Script Module kısıtları ve C/C++ betikleri için
        static std::vector<std::string>& ScriptDefines();
        // Projeye özel giriş haritası (.cqproj içinde); kapalıysa motorun varsayılan haritası
        static bool& CustomInput();
        // Project-wide Actions (Unity 6): bir Input Actions varlığı (.cqinputactions, proje göreli). Doluysa betiklerin
        // genel eylem sorguları (InputGetAction, InputActionVector ...) her haritada bu varlıktan okunur.
        static std::string& ProjectWideActions();
        // Tags & Layers: projeye eklenen etiketler (yerleşiklerden sonra)
        static std::vector<std::string>& CustomTags();

        // Betik sınıfının çalışma sırası (Unity Script Execution Order; küçük önce, varsayılan 0)
        static int GetScriptOrder(const std::string& className);
        static void InvalidateScriptOrderCache();

        static void Serialize(YAML::Emitter& out);
        static void Deserialize(const YAML::Node& root);
        static void ResetToDefaults();
        // Ses / kalite / gezinme ayarlarını ilgili sistemlere uygula
        static void Apply();
    };
}
