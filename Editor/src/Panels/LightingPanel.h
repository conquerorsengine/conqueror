#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Lightmap/LightmapBaker.h"
#include "Renderer/Probe/AdaptiveProbeVolume.h"
#include "Renderer/RHI/Texture.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Conqueror::Editor
{
    // Unity "Lighting" penceresinin karşılığı:
    //  Scene        : GI modları, lightmapper, örnek sayıları, çözünürlük, otomatik üretim
    //  Environment  : gökyüzü (skybox / prosedürel / düz renk), güneş, ortam ışığı, yansımalar, sis, hale/parlama
    //  Shadows      : kalite, çözünürlük, mesafe, cascade'ler (önizlemeli), bias
    //  Post FX      : ton eşleme, pozlama, bloom, SSAO, renk derecelendirme, vinyet, CA, gren, AA (+ hazır ayarlar)
    //  Lightmaps    : pişirilmiş / gerçek zamanlı lightmap durumu ve önizleme
    //  Probes       : Adaptive Probe Volume + yansıma probları
    //  Light Explorer: sahnedeki tüm ışıkların tablo halinde düzenlenmesi
    class LightingPanel
    {
    public:
        LightingPanel() = default;
        ~LightingPanel();

        void OnImGuiRender(bool* open = nullptr);
        void SetContext(std::shared_ptr<Scene> scene);
        void SetSelectionCallback(std::function<void(Entity)> callback) { m_SelectCallback = std::move(callback); }

        bool IsBaking() const { return m_Bake.Active; }
        // Otomasyon / menü erişimi
        void GenerateLighting();
        void ClearBakedData();
        void SelectTab(int tab) { m_RequestedTab = tab; }

    private:
        // Bake Probe Volume düğmesi: hacim ızgarası her zaman (bileşen yoksa Lighting ayarlarındaki hacim)
        bool m_APVVolumeRequested = false;
        std::shared_ptr<struct LightBakeSettings> m_LightingAssetBefore;   // Lighting Settings varlığı: kare başındaki ayarlar
        bool m_PendingLightingSave = false;
        enum class BakeKind { None, BakedLightmap, RealtimeLightmap, APV };

        struct BakeJob
        {
            bool Active = false;
            BakeKind Kind = BakeKind::None;
            std::thread Worker;
            std::atomic<bool> Done{ true };
            std::atomic<float> Progress{ 0.0f };
            std::mutex StepMutex;
            std::string Step;
            std::shared_ptr<Scene> Snapshot;           // bake thread'i yalnızca bu kopyayı okur
            std::shared_ptr<LightmapBaker> Baker;
            std::shared_ptr<AdaptiveProbeVolume> APV;
            std::chrono::steady_clock::time_point Start;
            bool Silent = false;                       // otomatik (arka plan) bake
        };

        struct BakeResultInfo
        {
            bool Valid = false;
            int AtlasWidth = 0;
            int AtlasHeight = 0;
            int Texels = 0;
            int Objects = 0;
            float Seconds = 0.0f;
            std::string When;
        };

        // Sekmeler
        void DrawToolbar();
        void DrawSceneTab();
        void DrawEnvironmentTab();
        void DrawShadowsTab();
        void DrawPostTab();
        void DrawLightmapsTab();
        void DrawProbesTab();
        void DrawLightExplorerTab();

        // Yardımcılar
        void DrawSkyboxPicker();
        void DrawTexturePicker(const char* label, std::shared_ptr<Texture2D> texture, const std::function<void(std::shared_ptr<Texture2D>)>& setter);
        void DrawCascadePreview();
        void ApplyPostPreset(int preset);

        // Bake
        void StartBake(BakeKind kind, bool silent = false);
        void PollBake();
        void CancelBake();
        void FinishBake();
        void JoinWorker();
        void UpdateAutoGenerate();
        uint64_t ComputeLightingHash() const;
        std::shared_ptr<Scene> CreateSnapshot() const;
        LightmapSettings MakeLightmapSettings(bool realtime) const;

        std::shared_ptr<Scene> m_Context;
        std::function<void(Entity)> m_SelectCallback;
        int m_RequestedTab = -1;

        BakeJob m_Bake;
        BakeResultInfo m_BakedInfo;
        BakeResultInfo m_RealtimeInfo;
        BakeResultInfo m_APVInfo;
        int m_APVProbeCount = 0;
        int m_APVLevels = 0;
        std::shared_ptr<Texture2D> m_RealtimeLightmap;

        // Otomatik üretim
        uint64_t m_LastLightingHash = 0;
        bool m_AutoPending = false;
        std::chrono::steady_clock::time_point m_AutoChangeTime{};

        // Skybox
        int m_SkyboxResolutionIndex = 2; // 0 512, 1 1024, 2 2048, 3 4096, 4 kaynak
        std::vector<std::string> m_SkyboxCandidates;
        std::chrono::steady_clock::time_point m_LastSkyboxScan{};

        // Light Explorer filtresi
        char m_LightFilter[64] = {};
        std::string m_StatusMessage;
        std::chrono::steady_clock::time_point m_StatusTime{};
    };
}
