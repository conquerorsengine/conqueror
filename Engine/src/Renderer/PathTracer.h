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
        // depth: bu karenin raster sahne derinliği (0: gürültü giderme yok). 299: az örnekte (kamera / sahne değişince birikim baştan
        // başlar, hareket ederken 1 örnek) sonuç kenar korumalı à-trous süzgeciyle (SVGF'nin uzamsal kısmı gibi) temizlenir; örnek
        // arttıkça süzgeç küçülür, 128 örnekten sonra birikmiş görüntüye dokunulmaz (Blender'ın Viewport gürültü gidermesi gibi)
        CQ_API bool Render(Scene& scene, const RenderCamera& camera, uint32_t target, uint32_t depth = 0);
        CQ_API void Reset();
        CQ_API int GetSampleCount();
        CQ_API int GetTriangleCount();
    }
}
