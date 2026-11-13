#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Etkileşimli müzik (Godot 4.3 AudioStreamPlaylist / AudioStreamSynchronized / AudioStreamInteractive; Unity'de yerleşik karşılığı yok,
// FMOD / Wwise gerekir; Unreal'da Quartz + MetaSounds ile elle kurulur). Üç varlık Audio Source'un Clip yuvasına atanır:
//   Playlist      parçalar art arda (karışık sırayla da), bitişte önceki parça Fade Time'da söner (kuyruk), döngü
//   Synchronized  katmanlar aynı anda, örnek hassasiyetinde birlikte; her katmanın sesi betikten değişir (dikey katmanlama)
//   Interactive   klipler + geçiş tablosu: hemen / sonraki vuruşta / sonraki ölçüde / sonda, aynı konumdan ya da baştan, kapalı /
//                 açılma / sönme / çapraz / otomatik geçiş (vuruş cinsinden), dolgu klibi, önceki klibi tutup ona dönme, otomatik ilerleme
// Bir klip başka bir müzik varlığı da olabilir (iç içe: etkileşimli müziğin klibi senkron katmanlı bir parça). Zamanlama miniaudio'nun
// motor saatine göre planlanır (başlama / durma / sönme örnek hassasiyetinde), oyun karesine bağlı değildir.
namespace Conqueror
{
    class Scene;
    struct AudioSourceComponent;
    namespace Scripting { class NativeRegistry; }

    struct MusicClip
    {
        std::string Stream;       // ses dosyası ya da başka bir müzik varlığı (proje göreli)
        float Bpm = 0.0f;         // dakikadaki vuruş (0: bilinmiyor; vuruşa bağlı geçişler 120 sayar)
        int BeatCount = 0;        // > 0: müzikal uzunluk (vuruş); bu noktadan sonrası geçişte sönen kuyruk
        int BarBeats = 4;         // ölçüdeki vuruş
        bool Loop = false;        // dosya kendi sonunda başa döner
        float VolumeDb = 0.0f;    // Synchronized: katmanın başlangıç sesi
    };

    struct MusicInteractiveClip
    {
        std::string Name = "Clip";
        MusicClip Clip;
        int AutoAdvance = 0;      // 0 Disabled, 1 Enabled (Next Clip), 2 Return To Hold
        int NextClip = -1;
    };

    struct MusicTransition
    {
        int From = -1, To = -1;   // klip dizini, -1: Any
        int FromTime = 0;         // 0 Immediate, 1 Next Beat, 2 Next Bar, 3 End
        int ToTime = 1;           // 0 Same Position, 1 Start
        int FadeMode = 4;         // 0 Disabled, 1 Fade In, 2 Fade Out, 3 Cross, 4 Automatic (aynı konuma: çapraz, yoksa sönme)
        float FadeBeats = 1.0f;
        bool UseFiller = false;   // önce dolgu klibi, o bitince hedef
        int FillerClip = -1;
        bool HoldPrevious = false;// önceki klip tutulur: Return To Hold ile kaldığı yerden döner
    };

    struct MusicAsset
    {
        enum class Type { Playlist = 0, Synchronized, Interactive };
        Type Kind = Type::Playlist;
        std::vector<MusicClip> Clips;                    // Playlist: parçalar, Synchronized: katmanlar
        bool Shuffle = false;                            // Playlist
        bool Loop = true;                                // Playlist: son parçadan sonra başa
        float FadeTime = 0.3f;                           // Playlist: biten parçanın sönme süresi (s)
        std::vector<MusicInteractiveClip> InteractiveClips;
        std::vector<MusicTransition> Transitions;
        int InitialClip = 0;
    };

    namespace InteractiveMusic
    {
        constexpr const char* PlaylistExtension = ".cqmusicplaylist";
        constexpr const char* SynchronizedExtension = ".cqmusicsync";
        constexpr const char* InteractiveExtension = ".cqmusicinteractive";

        CQ_API bool IsAsset(const std::string& path);
        CQ_API bool Load(const std::string& path, MusicAsset& out);
        CQ_API bool Save(const std::string& path, const MusicAsset& asset);
        CQ_API const char* TypeName(MusicAsset::Type type);

        // Clip yuvasında müzik varlığı olan Audio Source (Scene::UpdateAudioSystem her kare)
        void UpdateSource(Scene& scene, uint32_t entity, AudioSourceComponent& source, const glm::vec3& position, float gain, bool runtime,
                          uint32_t routingGeneration);
        CQ_API bool Play(Scene& scene, uint32_t entity);
        CQ_API void Stop(Scene& scene, uint32_t entity);
        CQ_API bool IsPlaying(Scene& scene, uint32_t entity);
        // Interactive: klibe geç (ad ya da dizin; Godot switch_to_clip_by_name / switch_to_clip)
        CQ_API bool SwitchToClip(Scene& scene, uint32_t entity, const std::string& clip);
        CQ_API std::string GetCurrentClip(Scene& scene, uint32_t entity);
        // Synchronized: katman sesi (Godot set_sync_stream_volume); iç içe ise çalan klibin katmanları
        CQ_API bool SetLayerVolume(Scene& scene, uint32_t entity, int layer, float volumeDb);
        // Çalan klibin / parçanın konumu (s)
        CQ_API float GetPosition(Scene& scene, uint32_t entity);
        CQ_API void StopAll(Scene& scene);
        // Silinen ya da klibi değişen kaynakların çalarları
        void Prune(Scene& scene);

        // Düzenleyici önizlemesi (sahneden bağımsız, Master'a)
        CQ_API bool PreviewPlay(const std::string& path);
        CQ_API void PreviewStop();
        CQ_API bool PreviewIsPlaying();
        CQ_API bool PreviewSwitch(const std::string& clip);
        CQ_API std::string PreviewCurrentClip();
        CQ_API bool PreviewSetLayerVolume(int layer, float volumeDb);
        CQ_API void UpdatePreview();
        CQ_API void Shutdown();
    }
}
