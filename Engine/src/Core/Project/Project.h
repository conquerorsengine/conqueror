#pragma once

#include "Core/Base/Base.h"
#include "Core/Assets/AssetManager/AssetManager.h"
#include "Core/Assets/AssetManager/EditorAssetManager.h"
#include "Core/Assets/AssetManager/RuntimeAssetManager.h"

#include <string>
#include <filesystem>
#include <memory>
#include <vector>

namespace Conqueror {

    // Platforma özel oyuncu ayarları (Unity Player Settings platform sekmeleri). Konsollar yok.
    struct MacPlayerSettings
    {
        std::string BundleIdentifier;                 // boşsa com.<şirket>.<ürün>
        std::string BuildNumber = "1";                // CFBundleVersion
        std::string MinimumVersion = "11.0";          // LSMinimumSystemVersion
        std::string Category = "public.app-category.games";
        std::string CameraUsage;                      // NSCameraUsageDescription (kamera kullanan oyunlar)
        std::string MicrophoneUsage;                  // NSMicrophoneUsageDescription
    };
    struct LinuxPlayerSettings
    {
        int DisplayServer = 0;                        // 0 otomatik, 1 Wayland, 2 X11 (CQ_DISPLAY_SERVER ortam değişkeni ezer)
    };
    struct AndroidPlayerSettings
    {
        std::string PackageName;                      // boşsa com.<şirket>.<ürün>
        int VersionCode = 1;
        int Orientation = 0;                          // 0 yatay (sensör), 1 yatay, 2 dikey (sensör), 3 dikey, 4 serbest
        bool ARM64 = true;                            // telefon / tablet / araç
        bool X86_64 = true;                           // emülatör, Chromebook
        std::string Keystore;                         // boşsa hata ayıklama anahtarı (~/.android/debug.keystore)
        std::string KeyAlias;                         // parola projede tutulmaz: CQ_ANDROID_KEYSTORE_PASS
    };
    struct IOSPlayerSettings
    {
        std::string BundleIdentifier;
        std::string BuildNumber = "1";
        int TargetDevice = 0;                         // 0 iPhone + iPad, 1 iPhone, 2 iPad
        std::string MinimumVersion = "15.0";
        int Orientation = 0;                          // 0 yatay, 1 dikey, 2 serbest
        std::string TeamID;                           // imzalama ekibi
        std::string CameraUsage;                      // NSCameraUsageDescription (AR için gerekli)
        bool RequiresARKit = false;
        bool StatusBarHidden = true;
    };
    struct WebPlayerSettings
    {
        int GraphicsAPI = 0;                          // 0 WebGL 2, 1 WebGPU
        int Template = 0;                             // 0 varsayılan sayfa, 1 sade, 2 tam ekran
        int Compression = 2;                          // 0 yok, 1 Gzip, 2 Brotli
        int MemoryMB = 256;                           // başlangıç belleği
        bool Threads = false;                         // SharedArrayBuffer (sunucuda COOP / COEP başlıkları gerekir)
        bool DataCaching = true;                      // oyun verisi tarayıcıda (IndexedDB) önbelleklenir
    };
    struct QNXPlayerSettings
    {
        int SDP = 1;                                  // 0 SDP 7.1, 1 SDP 8.0
        int Architecture = 0;                         // 0 aarch64le, 1 x86_64 (CQ_QNX_ARCH)
        bool FillDisplay = true;                      // pencere ekranı kaplar (false: istenen boyut, CQ_QNX_WINDOWED)
        std::string ScreenAppID = "conqueror";        // QNX Screen pencere kimliği (CQ_APP_ID)
    };

    // Açılış ekranı (Unity Splash Image): oyun açılırken logo(lar) belirip kaybolur; sahne arkada yüklenir
    struct SplashLogoEntry
    {
        std::string Path;                             // proje göreli resim
        float Duration = 2.0f;                        // saniye (belirme + bekleme + kaybolma)
    };
    struct SplashSettings
    {
        // Logoların motor logosuna göre yerleşimi (Unity Draw Mode: All Sequential / Unity Logo Below)
        enum Layout : int { Sequential = 0, EngineBelow, EngineAbove, EngineLeft, EngineRight };
        enum Motion : int { Static = 0, Dolly };      // Dolly: logo yavaşça büyür
        bool Show = true;
        std::string Logo;                             // eski tek logo alanı (okunurken Logos'a taşınır)
        std::vector<SplashLogoEntry> Logos;           // sırayla gösterilen proje logoları
        bool ShowEngineLogo = true;                   // "Conqueror's Engine" logosu
        int DrawMode = Sequential;
        int Animation = Static;
        float Duration = 2.0f;                        // motor logosu (Sequential) ve yeni logoların süresi
        float LogoScale = 1.0f;                       // logo boyutu çarpanı
        float BackgroundR = 0.067f, BackgroundG = 0.071f, BackgroundB = 0.086f;   // sRGB
        std::string BackgroundImage;                  // proje göreli; ekranı kaplar (boş: düz renk)
    };

    // Derlenmiş oyunun penceresi (Unity: Player Settings)
    struct PlayerSettings
    {
        int Width = 1280;
        int Height = 720;
        int FullscreenMode = 0;          // 0 pencere, 1 tam ekran pencere (kenarlıksız), 2 özel tam ekran
        bool Resizable = true;
        bool VSync = true;
        bool RunInBackground = true;     // odak kaybında oyun durmaz (çok oyunculu için gerekli)
        std::string Icon;                // proje göreli .png
        std::string GraphicsAPI = "Auto";   // "Auto" / "OpenGL" / "Vulkan" / "Direct3D11" / "Direct3D12" / "Metal" (yeniden başlatma gerekir)

        SplashSettings Splash;
        MacPlayerSettings Mac;
        LinuxPlayerSettings Linux;          // Linux ve Steam Deck
        LinuxPlayerSettings BSD;            // FreeBSD / OpenBSD / NetBSD
        AndroidPlayerSettings Android;      // Android ve Android Automotive
        IOSPlayerSettings IOS;
        WebPlayerSettings Web;
        QNXPlayerSettings QNX;
    };

    // Unity: Build Settings
    struct BuildSceneEntry
    {
        std::string Path;                // proje göreli .cqscene
        bool Enabled = true;
    };

    struct BuildSettings
    {
        std::vector<BuildSceneEntry> Scenes;   // ilk etkin sahne başlangıç sahnesidir (indeks 0)
        std::string OutputDirectory;           // proje göreli; boşsa Builds/<Platform>
        bool DevelopmentBuild = false;         // günlük dosyası + konsol çıktısı
        // Development Build seçenekleri (Unity Script Debugging / Wait For Managed Debugger, Unreal DebugGame): betikler
        // optimizasyonsuz ve hata ayıklama bilgisiyle; oyun başlarken bir hata ayıklayıcının bağlanmasını bekler
        bool ScriptDebugging = false;
        bool WaitForDebugger = false;
    };

    struct ProjectConfig
    {
        std::string Name = "Untitled";
        std::string Organization = "MyCompany";
        std::string Version = "1.0.0";
        std::string TargetPlatform;          // "Linux", "Windows", "macOS"; boş = editörün çalıştığı platform (proje taşınabilir)
        
        std::string StartScene;
        std::string AssetDirectory;
        std::string ScriptModulePath;

        PlayerSettings Player;
        BuildSettings Build;

        // Derlenecek sahneler (etkin olanlar, sırayla); liste boşsa başlangıç sahnesi
        std::vector<std::string> GetBuildScenes() const
        {
            std::vector<std::string> scenes;
            for (const auto& s : Build.Scenes)
                if (s.Enabled && !s.Path.empty())
                    scenes.push_back(s.Path);
            if (scenes.empty() && !StartScene.empty())
                scenes.push_back(StartScene);
            return scenes;
        }
    };

    class CQ_API Project
    {
    public:
        Project();
        ~Project();

        const ProjectConfig& GetConfig() const { return m_Config; }
        ProjectConfig& GetConfig() { return m_Config; }

        static Ref<Project> GetActive() { return s_ActiveProject; }
        static void SetActive(Ref<Project> project);
        static void SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack = nullptr);
        
        static void ReloadScriptEngine();

        std::filesystem::path GetProjectDirectory() const { return m_ProjectDirectory; }
        void SetProjectDirectory(const std::filesystem::path& path) { m_ProjectDirectory = path; }

        static std::filesystem::path GetActiveProjectDirectory()
        {
            if (s_ActiveProject)
                return s_ActiveProject->m_ProjectDirectory;
            return "";
        }

        // Etkin projenin .cqproj dosyası (klasördeki mevcut dosya; yoksa <Ad>.cqproj)
        static std::filesystem::path GetActiveProjectFile();

        static std::filesystem::path GetAssetDirectory()
        {
            if (s_ActiveProject)
                return GetActiveProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory;
            return "";
        }

        static Ref<AssetManager> GetAssetManager() { return s_AssetManager; }
        static Ref<RuntimeAssetManager> GetRuntimeAssetManager() { return std::static_pointer_cast<RuntimeAssetManager>(s_AssetManager); }

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;

        inline static Ref<Project> s_ActiveProject;
        inline static Ref<AssetManager> s_AssetManager;
    };
}
