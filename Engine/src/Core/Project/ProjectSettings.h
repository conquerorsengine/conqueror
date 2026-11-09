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

    struct QualityProjectSettings
    {
        int Level = (int)QualityLevel::High;   // Low, Medium, High, Ultra, Custom
        QualityPreset Custom;
        int TargetFrameRate = 0;               // 0 = sınırsız (VSync'e bağlı)
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

    class CQ_API ProjectSettings
    {
    public:
        static AudioProjectSettings& Audio();
        static QualityProjectSettings& Quality();
        static GraphicsProjectSettings& Graphics();
        static NavigationProjectSettings& Navigation();
        static EditorProjectSettings& Editor();
        static std::vector<ScriptOrderEntry>& ScriptOrder();
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
