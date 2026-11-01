#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct ma_node_base ma_node_base;
typedef struct ma_sound ma_sound;

namespace Conqueror
{
    class AudioNode;

    // Mikser efekt türleri (her grup kendi zincirine sahiptir)
    enum class MixerEffectType : int
    {
        LowPass = 0,
        HighPass,
        Echo,
        Reverb,
        Chorus,
        Distortion,
        Compressor,
        Limiter,
        Gain,
        Count
    };

    struct CQ_API MixerEffect
    {
        MixerEffectType Type = MixerEffectType::LowPass;
        bool Bypass = false;
        float Params[5] = { 0, 0, 0, 0, 0 };

        // Çalışma zamanı düğümü (AudioMixer yönetir)
        std::shared_ptr<AudioNode> Runtime;

        static const char* GetTypeName(MixerEffectType type);
        static int GetParamCount(MixerEffectType type);
        static const char* GetParamName(MixerEffectType type, int index);
        static void GetParamRange(MixerEffectType type, int index, float& min, float& max, const char*& format, bool& logarithmic);
        static MixerEffect CreateDefault(MixerEffectType type);
    };

    // Grup başına ölçüm (ses iş parçacığı yazar, UI okur)
    struct MixerMeterData
    {
        std::atomic<float> PeakL{ 0.0f }, PeakR{ 0.0f };
        std::atomic<float> RmsL{ 0.0f }, RmsR{ 0.0f };
    };

    struct CQ_API MixerGroup
    {
        std::string Name;
        std::string Parent;                 // boş: Master'a (ya da Master ise uç noktaya)
        float VolumeDb = 0.0f;              // -80 .. +20
        float Pan = 0.0f;                   // -1 .. 1
        float Pitch = 1.0f;
        bool Mute = false;
        bool Solo = false;
        bool BypassEffects = false;
        glm::vec3 Color = glm::vec3(0.35f, 0.62f, 0.95f);
        std::vector<MixerEffect> Effects;

        // Ducking (yan zincir): kaynak grup bu eşiği aşınca bu grup kısılır
        std::string DuckSource;
        float DuckThresholdDb = -30.0f;
        float DuckAmountDb = -12.0f;
        float DuckAttack = 0.05f;           // s
        float DuckRelease = 0.4f;           // s

        // ---- Çalışma zamanı (kopyalanmaz)
        struct Runtime
        {
            ma_sound* Group = nullptr;              // ma_sound_group
            std::shared_ptr<AudioNode> Meter;       // ölçüm düğümü (zincirin sonu)
            std::shared_ptr<MixerMeterData> MeterData = std::make_shared<MixerMeterData>();
            float DisplayPeakL = 0.0f, DisplayPeakR = 0.0f;
            float DisplayRmsL = 0.0f, DisplayRmsR = 0.0f;
            float HoldL = 0.0f, HoldR = 0.0f, HoldTimer = 0.0f;
            float DuckGainDb = 0.0f;
            float SnapshotFromDb = 0.0f, SnapshotToDb = 0.0f;
            bool Clipped = false;
        };
        std::shared_ptr<Runtime> RT = std::make_shared<Runtime>();
    };

    struct CQ_API MixerSnapshot
    {
        std::string Name;
        std::unordered_map<std::string, float> VolumesDb;
        std::unordered_map<std::string, bool> Mutes;
    };

    // Unity Audio Mixer / FMOD bus sistemi karşılığı.
    // Ses kaynakları adla bir gruba yönlenir; gruplar ağaç oluşturur ve Master'da birleşir.
    class CQ_API AudioMixer
    {
    public:
        static AudioMixer& Get();

        void Init();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Kare başına (birden çok çağrıyı tek kareye indirger)
        void Update();

        // ---- Gruplar
        std::vector<MixerGroup>& GetGroups() { return m_Groups; }
        MixerGroup* FindGroup(const std::string& name);
        int FindGroupIndex(const std::string& name) const;
        MixerGroup& GetMaster() { return m_Groups[0]; }
        MixerGroup* AddGroup(const std::string& name, const std::string& parent = "Master");
        bool RemoveGroup(const std::string& name);
        bool RenameGroup(const std::string& oldName, const std::string& newName);
        bool SetGroupParent(const std::string& name, const std::string& parent);
        bool IsDescendant(const std::string& group, const std::string& ancestor) const;
        std::string MakeUniqueName(const std::string& base) const;

        // Efekt zinciri değişti -> düğümleri yeniden bağla
        void RebuildChain(const std::string& group);
        void UpdateEffectParams(const std::string& group);

        // ---- Yönlendirme: bir düğümü grubun girişine bağla (grup yoksa Master)
        void Connect(AudioNode* node, const std::string& group);
        ma_node_base* GetGroupInput(const std::string& group);
        // Grup yapısı değiştiğinde artar; kaynaklar yeniden bağlanmalı
        uint32_t GetRoutingGeneration() const { return m_RoutingGeneration; }

        // ---- Oyun oturumu: betiklerin değiştirdiği grup ses/sessiz ayarları oyun bitince geri alınır
        void BeginPlaySession();
        void EndPlaySession();

        // ---- Snapshot'lar
        std::vector<MixerSnapshot>& GetSnapshots() { return m_Snapshots; }
        void CaptureSnapshot(const std::string& name);
        void ApplySnapshot(const std::string& name, float transitionSeconds = 0.5f);

        // ---- Test tonu (editörde gruba sinüs gönder)
        void PlayTestTone(const std::string& group, float frequency = 440.0f, float seconds = 1.0f, float amplitude = 0.25f);
        void StopTestTones();

        // ---- Kayıt
        bool Save(const std::filesystem::path& path) const;
        bool Load(const std::filesystem::path& path);
        // Proje değiştiğinde ProjectSettings/AudioMixer.yaml yüklenir
        void EnsureProjectLoaded(const std::filesystem::path& projectDirectory);
        const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        bool SaveProject() const { return !m_FilePath.empty() && Save(m_FilePath); }
        void ResetToDefault();

        static float DbToLinear(float db) { return db <= -79.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }
        static float LinearToDb(float lin) { return lin <= 0.0001f ? -80.0f : 20.0f * std::log10(lin); }

    private:
        AudioMixer() = default;
        void CreateRuntime(MixerGroup& group);
        void DestroyRuntime(MixerGroup& group);
        void WireGroup(MixerGroup& group);
        void RebuildAll();
        bool IsAudible(const MixerGroup& group, bool anySolo) const;

        std::vector<MixerGroup> m_Groups;
        std::vector<MixerSnapshot> m_Snapshots;
        std::filesystem::path m_FilePath;
        std::filesystem::path m_ProjectDirectory;
        bool m_Initialized = false;
        uint32_t m_RoutingGeneration = 1;
        double m_LastUpdate = 0.0;
        float m_SnapshotT = 1.0f, m_SnapshotDuration = 0.0f;

        struct TestTone;
        std::vector<std::shared_ptr<TestTone>> m_TestTones;

        struct SavedGroupState { std::string Name; float VolumeDb; bool Mute; };
        std::vector<SavedGroupState> m_PlaySessionState;
        bool m_InPlaySession = false;
    };
}
