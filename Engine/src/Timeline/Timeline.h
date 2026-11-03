#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    // Unity Timeline benzeri sıralama varlığı (.cqtimeline). Parçalar bir sahne varlığına bağlanır
    // (PlayableDirector bağlantıları) ve zamana göre değerlendirilir.
    enum class TimelineTrackType : uint8_t
    {
        Activation = 0,   // klipler süresince varlık etkin
        Transform,        // konum / dönüş / ölçek anahtarları
        Property,         // herhangi bir bileşen alanı (float / vec / renk) anahtarları
        Audio,            // ses klipleri
        Signal,           // işaretçiler: betik metodunu çağırır
        Animation,        // Animator klip indeksini ve zamanını sürer
        Count
    };

    enum class KeyInterpolation : uint8_t { Smooth = 0, Linear, Constant };

    struct TimelineKey
    {
        float Time = 0.0f;
        glm::vec4 Value{ 0.0f };
        KeyInterpolation Interp = KeyInterpolation::Smooth;
    };

    // Dönüşüm parçası: "Position", "Rotation" (derece), "Scale"; özellik parçası: tek kanal ("Value")
    struct TimelineChannel
    {
        std::string Name;
        std::vector<TimelineKey> Keys;   // zamana göre sıralı
    };

    struct TimelineClip
    {
        float Start = 0.0f;
        float Duration = 1.0f;
        std::string Name;
        std::string Asset;               // ses dosyası (proje göreli)
        float Volume = 1.0f;
        float Speed = 1.0f;
        float ClipIn = 0.0f;             // kaynağın içinden başlangıç (s)
        int AnimationIndex = 0;          // Animation parçası: model klip indeksi
        bool Loop = true;                // Animation: klip bitince başa sarar; false: son karede kalır (oturma, açma...)
        float End() const { return Start + Duration; }
    };

    struct TimelineMarker
    {
        float Time = 0.0f;
        std::string Method;              // bağlı varlıktaki betik metodu
        std::string Argument;            // isteğe bağlı metin argümanı
        std::string Signal;              // Signal varlığı (.cqsignal): bağlı nesnenin Signal Receiver'ı tepki verir
    };

    struct TimelineTrack
    {
        std::string Name = "Track";
        TimelineTrackType Type = TimelineTrackType::Transform;
        bool Muted = false;
        bool Locked = false;
        int PostPlayback = 0;            // Activation: 0 olduğu gibi, 1 etkin, 2 devre dışı
        std::string Component;           // Property: bileşen tür adı ("PointLightComponent")
        std::string Field;               // Property: alan adı ("Intensity")
        std::vector<TimelineChannel> Channels;
        std::vector<TimelineClip> Clips;
        std::vector<TimelineMarker> Markers;

        TimelineChannel* FindChannel(const std::string& name);
        const TimelineChannel* FindChannel(const std::string& name) const;
        TimelineChannel& GetOrAddChannel(const std::string& name);
    };

    struct CQ_API TimelineAsset
    {
        float Duration = 5.0f;
        float FrameRate = 60.0f;
        std::vector<TimelineTrack> Tracks;

        float ContentEnd() const;        // son klip / anahtar / işaretçi
        bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
        static std::shared_ptr<TimelineAsset> Load(const std::filesystem::path& path, std::string* error = nullptr);

        static const char* TrackTypeName(TimelineTrackType type);
    };

    // Kanalı t anında değerlendir (anahtar yoksa fallback)
    CQ_API glm::vec4 EvaluateChannel(const TimelineChannel& channel, float t, const glm::vec4& fallback);
    // Anahtar ekle / güncelle (aynı zamanda ±yarım kare içinde varsa üzerine yazar)
    CQ_API void SetKey(TimelineChannel& channel, float time, const glm::vec4& value, float frameRate, KeyInterpolation interp = KeyInterpolation::Smooth);
}
