#pragma once

#include "UI/UISystem.h"

#include "Core/Application.h"
#include "Core/Layer.h"
#include "Core/Project/GamePackage.h"
#include "Scene/Scene.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Project/SplashLayout.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror::Runtime
{
    class RuntimeLayer : public Layer
    {
    public:
        // projectPath: oyun klasörü (bağımsız derleme); packagePath: taşınabilir oyun paketi (.cqspace). İkisi de boşsa oyuncunun açılış
        // ekranı ("Open a .cqspace" + son açılan oyunlar)
        RuntimeLayer(const std::filesystem::path& projectPath, const std::filesystem::path& packagePath = {});
        virtual ~RuntimeLayer();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(Timestep ts) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Event& e) override;

    private:
        bool LoadProject();
        void SwitchScene(const std::filesystem::path& path);
        // Açılış ekranı (Player Settings > Splash Image): logolar belirip kaybolur, ilk sahne logo tam görünürken yüklenir
        void SetupSplash();
        void DrawSplash();
        // Proje yüklenemedi: bağımsız derlemede oyun kapanır (eski davranış), paketten açılan oyunda hata ekranı
        void ProjectLoadFailed();
        void StartProject();

        // Taşınabilir oyun paketi: arka planda açılır / çözülür, bitince proje gibi yüklenir
        void BeginOpenPackage(const std::filesystem::path& package);
        void FinishOpenPackage();
        void ShowError(const std::string& title, const std::string& message);
        void ShowLauncher();
        void OpenFileDialog();
        void PollOpenRequests();
        void DrawLauncher();
        void DrawUnpacking();
        void DrawError();
        void ClearScreen();

    private:
        enum class Mode { Project, Unpacking, Launcher, Error };

        std::filesystem::path m_ProjectPath;
        std::shared_ptr<Scene> m_ActiveScene;
        UIInputState m_UIInput;
        bool m_PrevMouseDown = false;
        bool m_WindowWasFocused = false;

        SplashSettings m_SplashSettings;
        std::vector<Splash::Step> m_SplashSteps;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_SplashTextures;   // proje göreli yol -> doku ("" motor logosu)
        std::shared_ptr<Texture2D> m_SplashBackgroundImage;
        bool m_SplashActive = false;
        bool m_ProjectLoaded = false;
        float m_SplashTime = 0.0f;
        float m_SplashTotal = 0.0f;
        glm::vec3 m_SplashBackground{ 0.067f, 0.071f, 0.086f };

        Mode m_Mode = Mode::Project;
        std::string m_LoadError;                       // LoadProject'in son hatası (hata ekranında gösterilir)
        std::filesystem::path m_PackagePath;          // açık (ya da açılan) .cqspace
        bool m_FromLauncher = false;                  // paket açılış ekranından açıldı: pencere başlığı / boyutu burada uygulanır
        GamePackage::Manifest m_Manifest;
        std::thread m_UnpackThread;
        std::atomic<bool> m_UnpackDone{ false };
        std::atomic<bool> m_UnpackCancel{ false };
        std::atomic<float> m_UnpackProgress{ 0.0f };
        bool m_UnpackOk = false;
        std::string m_UnpackError;
        std::filesystem::path m_UnpackedDir;
        std::string m_ErrorTitle;
        std::string m_ErrorText;
        std::vector<GamePackage::RecentGame> m_Recent;
        std::vector<std::filesystem::path> m_FoundGames;   // oyun klasörlerinde bulunan paketler (Android: uygulamanın Games klasörü)
        char m_PathInput[1024] = {};
        std::string m_LauncherMessage;
        // Dosya iletişim kutusu (Linux: zenity / kdialog alt süreci) kareleri durdurmasın: ayrı iş parçacığında; durum paylaşımlı
        // (oyuncu kapanırken iletişim kutusu açık kalırsa iş parçacığı katmandan bağımsız biter)
        struct DialogState
        {
            std::mutex Mutex;
            bool Running = false;
            bool Done = false;
            std::string Result;
            std::string Error;
        };
        std::shared_ptr<DialogState> m_Dialog = std::make_shared<DialogState>();
    };
}
