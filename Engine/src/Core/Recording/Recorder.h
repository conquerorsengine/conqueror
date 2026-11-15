#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>

namespace Conqueror
{
    enum class RecordingFormat : int
    {
        MP4 = 0,       // MPEG-4 (ffmpeg)
        WebM,          // VP9 (ffmpeg)
        GIF,           // paletli GIF (ffmpeg)
        PNGSequence    // ffmpeg gerekmez
    };

    struct RecorderSettings
    {
        RecordingFormat Format = RecordingFormat::MP4;
        int FrameRate = 30;
        float Quality = 0.85f;          // 0..1
        float ResolutionScale = 1.0f;   // 0.25..1 (yalnızca ffmpeg biçimlerinde)
        // Tam çıkış çözünürlüğü (Unity Recorder "Output Resolution"): ikisi de > 0 ise ResolutionScale yerine bu boyut; oran farklıysa
        // görüntü sığdırılır, kenarlar siyah (letterbox / pillarbox)
        int OutputWidth = 0;
        int OutputHeight = 0;
        // Oyunun sesi (hoparlöre giden karıştırılmış ses) videoya eklenir (MP4: AAC, WebM: Opus); PNG dizisinde audio.wav, GIF'te ses yok.
        // Fixed Time Step açıkken ses gerçek zamanda aktığından kaydedilmez
        bool RecordAudio = true;
        float MaxDuration = 0.0f;       // saniye, 0 = sınırsız
        bool StartWithPlay = false;
        bool StopWithPlay = true;
        // Çevrimdışı kayıt (Unreal Movie Render Queue / Unity Recorder "Fixed"): oyun her karede tam 1 / FrameRate
        // ilerler ve her kare bir kez yazılır; görüntü makine hızından bağımsız akıcıdır (ses gerçek zamanla eşleşmez)
        bool FixedTimeStep = false;
        std::string OutputDirectory;    // boş: <proje>/Recordings
    };

    // Oyun görünümünü (veya herhangi bir GL dokusunu) sabit kare hızında kaydeder.
    // Kodlama/disk yazma arka plan thread'inde yapılır.
    class CQ_API Recorder
    {
    public:
        static RecorderSettings& Settings();

        static bool Start(uint32_t width, uint32_t height);
        static void Stop();
        static bool IsRecording();

        // Her karede çağrılır: gerekirse dokuyu okuyup kuyruğa ekler
        static void SubmitFrame(uint32_t textureID, uint32_t width, uint32_t height, float deltaSeconds);

        static float GetElapsedSeconds();
        static uint32_t GetFrameCount();
        static const std::string& GetCurrentOutput();
        static const std::string& GetLastOutput();
        static const std::string& GetLastError();
        static bool IsFFmpegAvailable();
        // Kullanılan ffmpeg: CQ_FFMPEG, motorla gelen Tools/ffmpeg, kullanıcı klasöründeki indirilmiş kopya ya da PATH
        static std::string FFmpegPath();
        // Kayıt sırasında yakalanan ses (saniye), testler / arayüz
        static float GetAudioSeconds();
    };
}
