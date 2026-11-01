#pragma once

#include "Core/Project/Project.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Conqueror::Editor
{
    struct BuildOptions
    {
        std::filesystem::path ProjectDir;
        std::filesystem::path ProjectFile;       // .cqproj
        ProjectConfig Config;                    // derleme anındaki kopya
        std::filesystem::path OutputDir;         // mutlak
        std::filesystem::path EngineBinDir;      // editör çalıştırılabilirinin klasörü (ConquerorRuntime burada)
        bool BuildRuntimeIfMissing = true;
        bool RunAfterBuild = false;
    };

    struct BuildReport
    {
        bool Success = false;
        std::string Error;
        double Seconds = 0.0;
        size_t Files = 0;
        uint64_t Bytes = 0;
        std::filesystem::path Executable;
        std::vector<std::string> Warnings;
    };

    // Unity "Build" / "Build And Run": oynanabilir bağımsız oyun klasörü üretir. Arka plan iş parçacığında çalışır;
    // editör ilerlemeyi ve günlüğü her karede okur.
    class BuildPipeline
    {
    public:
        static BuildPipeline& Get();
        ~BuildPipeline();

        bool Start(const BuildOptions& options);
        // Tek tıkla birden çok platform ("Build All"): sırayla, her biri OutputDir/<Platform> klasörüne. Sonuç her platform için
        // GetAllResults'ta; GetResult toplam (hepsi başarılıysa Success).
        bool StartAll(const BuildOptions& base, const std::vector<std::string>& platforms);
        std::vector<std::pair<std::string, BuildReport>> GetAllResults() const;
        bool IsRunning() const { return m_Running.load(); }
        void Cancel() { m_Cancel = true; }

        float GetProgress() const { return m_Progress.load(); }
        std::string GetStep() const;
        std::vector<std::string> GetLog() const;
        bool HasResult() const { return m_HasResult.load(); }
        BuildReport GetResult() const;

        // Eşzamanlı derleme (otomasyon / komut satırı)
        static BuildReport Run(const BuildOptions& options, const std::function<void(float, const std::string&)>& progress,
                               const std::function<void(const std::string&)>& log, const std::atomic<bool>* cancel = nullptr);

        // Bu makinede derlenebilen platform adı ("Linux", "Windows", "macOS")
        static std::string HostPlatform();

        // Hedef platformlar (konsollar yok): Project Settings ve Build Settings aynı listeyi kullanır
        struct PlatformInfo
        {
            const char* Name;      // TargetPlatform değeri
            const char* Icon;
            const char* Status;    // nasıl / nerede derlendiği, kısa
        };
        static const std::vector<PlatformInfo>& Platforms();
        // Editörün Build düğmesiyle derlenebilir mi (çalışan PC'nin platformu ya da Android / Android Automotive)
        static bool CanBuild(const std::string& platform);
        // Paket / paket kimliği önerisi: com.<şirket>.<ürün> (küçük harf, rakam, _; şirket boşsa conqueror)
        static std::string DefaultIdentifier(const ProjectConfig& config);
        // Editör ikilisinin yanından motor kaynak kökünü bulur (Resources/ ve Engine/src/Shaders)
        static std::filesystem::path FindEngineRoot(const std::filesystem::path& binDir);
        // Derlenmiş oyunu ayrı süreç olarak başlatır
        static bool Launch(const std::filesystem::path& executable, std::string* error = nullptr);

    private:
        BuildPipeline() = default;

        std::thread m_Thread;
        std::vector<std::pair<std::string, BuildReport>> m_AllResults;
        std::atomic<bool> m_Running{ false };
        std::atomic<bool> m_Cancel{ false };
        std::atomic<bool> m_HasResult{ false };
        std::atomic<float> m_Progress{ 0.0f };
        mutable std::mutex m_Mutex;
        std::string m_Step;
        std::vector<std::string> m_Log;
        BuildReport m_Result;
    };
}
