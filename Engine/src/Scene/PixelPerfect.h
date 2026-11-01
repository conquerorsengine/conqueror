#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>

// Unity 2D Pixel Perfect Camera: piksel sanatı tam sayı katlarla, titremeden gösterilir.
//  - Kamera boyutu: bir doku pikseli = tam sayı (zoom) ekran pikseli
//  - Upscale Render Texture: sahne referans çözünürlüğe yakın düşük çözünürlükte çizilir, en yakın komşu ile büyütülür
//  - Crop Frame X/Y: referans çözünürlük dışı siyah çerçeve (letterbox / pillarbox)
//  - Pixel Snapping: sprite ve kamera konumları piksel ızgarasına oturtulur
namespace Conqueror
{
    class Scene;
    class Entity;

    struct PixelPerfectCameraComponent
    {
        int AssetsPPU = 100;
        glm::vec2 ReferenceResolution{ 320.0f, 180.0f };
        bool UpscaleRenderTexture = false;
        bool PixelSnapping = false;
        bool CropFrameX = false;
        bool CropFrameY = false;
        bool StretchFill = false;          // iki eksen kırpılırken ekranı doldur (tam sayı olmayan ölçek)
        // Çalışma zamanı
        int CurrentZoom = 1;
    };

    namespace PixelPerfect
    {
        struct View
        {
            bool Active = false;
            uint32_t RenderWidth = 0, RenderHeight = 0;   // sahnenin çizildiği çözünürlük
            glm::ivec4 Output{ 0 };                       // son görüntünün yerleşeceği dikdörtgen (x, y, w, h)
            bool PointSample = false;
            int Zoom = 1;
        };

        // Birincil kamerada Pixel Perfect Camera varsa çözünürlükleri hesaplar ve kameranın boyutunu ayarlar
        CQ_API View Resolve(Scene& scene, uint32_t screenWidth, uint32_t screenHeight);
        // Sprite / kamera konumu ızgarası (dünya birimi başına piksel; 0 = kapalı)
        CQ_API float SnapGrid(Scene& scene, Entity camera);
    }

    void RegisterPixelPerfectComponents();
}
