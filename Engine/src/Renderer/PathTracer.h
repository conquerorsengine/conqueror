#pragma once

#include "Core/Base/Base.h"

#include <cstdint>

// Path Tracer (Unreal Path Tracing görünüm kipi): sahnenin üçgenleri CPU'da BVH'ye dizilir, GPU'da compute shader ile ışın izlenir.
// Her karede piksel başına bir örnek eklenir; kamera ya da sahne değişince birikim baştan başlar. Doğrudan ışık: güneş ve nokta
// ışıklar, spot ve alan ışıkları (gölge ışınıyla), gökyüzü motorun ortam ışığı ayarı, malzeme: Lambert + GGX (renk / metal / pürüz / ışıma).
namespace Conqueror
{
    class Scene;
    struct RenderCamera;

    namespace PathTracer
    {
        struct Settings
        {
            int MaxBounces = 5;
            int SamplesPerFrame = 1;
            int MaxSamples = 4096;            // bu sayıya ulaşınca durur
            float MaxPathIntensity = 24.0f;   // dolaylı ışık örneği üst sınırı (Unreal Max Path Intensity): ateşböceği noktaları
        };

        CQ_API Settings& GetSettings();
        // target: RGBA16F doku (boyutu dokudan okunur); birikmiş ortalama doğrusal HDR renk yazılır. Işıklar ve ortam bu karede
        // Renderer3D'ye verilenlerdir (sahne önce raster ile çizilmiş olmalı). camera: titreşimsiz (TAA'sız) kamera.
        CQ_API bool Render(Scene& scene, const RenderCamera& camera, uint32_t target);
        CQ_API void Reset();
        CQ_API int GetSampleCount();
        CQ_API int GetTriangleCount();
    }
}
