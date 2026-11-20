#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// ARKit kamera görüntüsü (capturedImage: YCbCr 4:2:0 iki düzlemli, tam aralık, BT.601) -> RGBA8. Saf C++: ARKitSession.mm satırları
// paralel çalıştırır; Tools/Tests/native/arkit_image_test.cpp aynı kodu sahte bir kareyle her platformda dener (256)
namespace Conqueror::ARKitImage
{
    // Görünümün normalleştirilmiş noktası (sol üst köken) -> görüntünün normalleştirilmiş noktası: ARKit'in
    // displayTransformForOrientation:viewportSize: dönüşümünün tersi (CGAffineTransform alanları: x' = a x + c y + tx, y' = b x + d y + ty)
    struct Affine
    {
        double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
    };

    inline Affine Invert(const Affine& m)
    {
        const double det = m.a * m.d - m.b * m.c;
        if (det == 0.0)
            return m;
        Affine r;
        r.a = m.d / det;
        r.b = -m.b / det;
        r.c = -m.c / det;
        r.d = m.a / det;
        r.tx = (m.c * m.ty - m.d * m.tx) / det;
        r.ty = (m.b * m.tx - m.a * m.ty) / det;
        return r;
    }

    struct Planes
    {
        const uint8_t* Y = nullptr;     // parlaklık düzlemi
        int YWidth = 0, YHeight = 0;
        size_t YStride = 0;
        const uint8_t* CbCr = nullptr;  // yarım çözünürlüklü, Cb Cr sırasıyla iç içe
        int CWidth = 0, CHeight = 0;
        size_t CStride = 0;
    };

    // Çıkışın bir satırı (row 0 = görünümün üstü). out W x H x 4; GL dokusu alttan başladığı için satır H - 1 - row'a yazılır
    inline void ConvertRow(const Planes& p, const Affine& toImage, int W, int H, int row, uint8_t* out)
    {
        uint8_t* dst = out + (size_t)(H - 1 - row) * (size_t)W * 4;
        const double vy = ((double)row + 0.5) / (double)H;
        for (int x = 0; x < W; ++x)
        {
            const double vx = ((double)x + 0.5) / (double)W;
            const double ix = toImage.a * vx + toImage.c * vy + toImage.tx, iy = toImage.b * vx + toImage.d * vy + toImage.ty;
            const int sx = std::clamp((int)(ix * p.YWidth), 0, p.YWidth - 1), sy = std::clamp((int)(iy * p.YHeight), 0, p.YHeight - 1);
            const float Y = (float)p.Y[(size_t)sy * p.YStride + (size_t)sx];
            const int cx = std::min(sx / 2, p.CWidth - 1), cy = std::min(sy / 2, p.CHeight - 1);
            const uint8_t* c = p.CbCr + (size_t)cy * p.CStride + (size_t)cx * 2;
            const float Cb = (float)c[0] - 128.0f, Cr = (float)c[1] - 128.0f;
            // BT.601 tam aralık (ARKit'in capturedImage biçimi; Apple'ın AR örneğindeki dönüşüm)
            const float r = Y + 1.402f * Cr, g = Y - 0.3441f * Cb - 0.7141f * Cr, b = Y + 1.772f * Cb;
            dst[x * 4 + 0] = (uint8_t)std::clamp(r, 0.0f, 255.0f);
            dst[x * 4 + 1] = (uint8_t)std::clamp(g, 0.0f, 255.0f);
            dst[x * 4 + 2] = (uint8_t)std::clamp(b, 0.0f, 255.0f);
            dst[x * 4 + 3] = 255;
        }
    }
}
