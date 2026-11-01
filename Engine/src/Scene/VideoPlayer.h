#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

// Unity VideoPlayer karşılığı. Kaynaklar:
//  - Motion-JPEG AVI (.avi; çerçeveler JPEG, ses PCM 16 bit) — bağımlılıksız çözücü
//  - Animasyonlu GIF (.gif), resim dizisi (PNG / JPG dosyalarıyla dolu klasör)
//  - Diğerleri (.mp4, .webm, .mov, .mkv): sistemde ffmpeg varsa bir kez MJPEG AVI'ye dönüştürülüp önbelleklenir
// Çıktı: mesh dokusu (TV ekranı), Raw Image (UI), Sprite Renderer, kameranın arka / ön düzlemi ya da yalnızca betik.
namespace Conqueror
{
    class Scene;
    class Texture2D;
    struct RenderCamera;

    enum class VideoRenderMode : int { MaterialOverride = 0, CameraFarPlane, CameraNearPlane, RawImage, SpriteRenderer, APIOnly };
    enum class VideoAudioOutput : int { None = 0, Direct };

    struct VideoPlayerComponent
    {
        std::string Source;
        bool PlayOnAwake = true;
        bool Loop = true;
        float PlaybackSpeed = 1.0f;
        VideoRenderMode RenderMode = VideoRenderMode::MaterialOverride;
        uint64_t Target = 0;                    // hedef varlık (0 = kendisi)
        bool Emissive = true;                   // Material Override: ekran gibi parlasın
        float CameraAlpha = 1.0f;               // kamera düzlemlerinde saydamlık
        VideoAudioOutput AudioOutput = VideoAudioOutput::Direct;
        float Volume = 1.0f;
        float ImageSequenceFrameRate = 24.0f;   // resim dizisi kare hızı

        // Çalışma zamanı (bilgi)
        bool IsPlaying = false;
        float Time = 0.0f;
        float Length = 0.0f;
        int Frame = -1;
        int FrameCount = 0;
        int Width = 0, Height = 0;
        float FrameRate = 0.0f;
        std::string Status;
    };

    namespace VideoPlayers
    {
        CQ_API void Update(Scene& scene, float dt);
        // Kamera düzlemi modları (oyun görünümü): far = gökyüzünden sonra, near = her şeyin üstünde
        CQ_API void RenderCameraPlanes(Scene& scene, bool farPlane);
        CQ_API void Release(Scene* scene);
        CQ_API void Play(Scene& scene, uint32_t entity);
        CQ_API void Pause(Scene& scene, uint32_t entity);
        CQ_API void Stop(Scene& scene, uint32_t entity);
        CQ_API void Seek(Scene& scene, uint32_t entity, float seconds);
        CQ_API std::shared_ptr<Texture2D> GetTexture(Scene& scene, uint32_t entity);
        CQ_API void Shutdown();
    }

    void RegisterVideoComponents();
}
