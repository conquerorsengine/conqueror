// ARKit kamera görüntüsü dönüşümünün testi (256): Engine/src/Platform/iOS/ARKitImage.h her platformda derlenir, gerçek iPhone olmadan
// sahte bir YCbCr karesiyle renk dönüşümünü ve ekrana döndürmeyi denetler.
//   c++ -std=c++17 -I Engine/src Tools/Tests/native/arkit_image_test.cpp -o arkit_image_test && ./arkit_image_test
// Yön: arka kamera görüntüsü yataydır (sensör telefonun sağına dönük, "landscape right"). ARKit'in dikey (portrait) ekran için
// displayTransform'u x' = y, y' = 1 - x (a=0 b=-1 c=1 d=0 tx=0 ty=1; en-boy oranı aynıyken): görüntünün sağ kenarı ekranın üstüne gelir.
// Yatay (landscape right) ekranda dönüşüm birimdir.
#include "Platform/iOS/ARKitImage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Conqueror;

static int s_Failures = 0;

static void Expect(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++s_Failures;
}

static bool Near(int a, int b, int tol) { return std::abs(a - b) <= tol; }

// Görüntü (ekranda üstten aşağı, soldan sağa) piksel rengi; out alttan yukarı (GL) yazılır
static const uint8_t* Pixel(const std::vector<uint8_t>& out, int W, int H, int x, int yFromTop)
{
    return &out[((size_t)(H - 1 - yFromTop) * (size_t)W + (size_t)x) * 4];
}

int main()
{
    // 1) Renk: BT.601 tam aralık. Gri (128,128,128) ve saf kırmızıya yakın (Y 81, Cb 90, Cr 240 -> ~238, 14, 14)
    {
        const int yw = 4, yh = 2;
        std::vector<uint8_t> Y(yw * yh), C((yw / 2) * (yh / 2) * 2);
        for (auto& v : Y) v = 128;
        for (size_t i = 0; i < C.size(); i += 2) { C[i] = 128; C[i + 1] = 128; }
        ARKitImage::Planes p{ Y.data(), yw, yh, (size_t)yw, C.data(), yw / 2, yh / 2, (size_t)yw };
        std::vector<uint8_t> out(4 * 2 * 4);
        for (int r = 0; r < 2; ++r)
            ARKitImage::ConvertRow(p, {}, 4, 2, r, out.data());
        Expect(out[0] == 128 && out[1] == 128 && out[2] == 128 && out[3] == 255, "gray YCbCr (128, 128, 128) -> RGB (128, 128, 128)");
        for (auto& v : Y) v = 81;
        for (size_t i = 0; i < C.size(); i += 2) { C[i] = 90; C[i + 1] = 240; }
        for (int r = 0; r < 2; ++r)
            ARKitImage::ConvertRow(p, {}, 4, 2, r, out.data());
        Expect(Near(out[0], 238, 2) && Near(out[1], 14, 2) && Near(out[2], 14, 2), "red YCbCr (81, 90, 240) -> RGB (238, 14, 14)");
    }

    // 2) Yön: 8x4 yatay kamera görüntüsü, dört çeyrek dört farklı parlaklıkta (sol üst 40, sağ üst 100, sol alt 160, sağ alt 220)
    const int yw = 8, yh = 4;
    std::vector<uint8_t> Y(yw * yh), C((yw / 2) * (yh / 2) * 2, 128);
    for (int y = 0; y < yh; ++y)
        for (int x = 0; x < yw; ++x)
            Y[y * yw + x] = (uint8_t)((y < yh / 2) ? (x < yw / 2 ? 40 : 100) : (x < yw / 2 ? 160 : 220));
    ARKitImage::Planes p{ Y.data(), yw, yh, (size_t)yw, C.data(), yw / 2, yh / 2, (size_t)yw };
    {
        // Yatay ekran (landscape right): birim dönüşüm, 16x8 çıkış görüntüyle aynı yönde
        const int W = 16, H = 8;
        std::vector<uint8_t> out((size_t)W * H * 4);
        const ARKitImage::Affine toImage = ARKitImage::Invert({});
        for (int r = 0; r < H; ++r)
            ARKitImage::ConvertRow(p, toImage, W, H, r, out.data());
        Expect(Pixel(out, W, H, 1, 1)[0] == 40 && Pixel(out, W, H, W - 2, 1)[0] == 100 && Pixel(out, W, H, 1, H - 2)[0] == 160 &&
               Pixel(out, W, H, W - 2, H - 2)[0] == 220, "landscape: screen corners show the image corners in place");
        // GL dokusu alttan başlar: belleğin ilk satırı ekranın altı (sol alt çeyrek 160)
        Expect(out[0] == 160, "GL texture rows start at the bottom of the screen");
    }
    {
        // Dikey ekran: displayTransform (görüntü -> ekran) x' = y, y' = 1 - x; kod tersini kullanır (ekran -> görüntü)
        const int W = 8, H = 16;
        std::vector<uint8_t> out((size_t)W * H * 4);
        const ARKitImage::Affine display{ 0.0, -1.0, 1.0, 0.0, 0.0, 1.0 };
        const ARKitImage::Affine toImage = ARKitImage::Invert(display);
        for (int r = 0; r < H; ++r)
            ARKitImage::ConvertRow(p, toImage, W, H, r, out.data());
        // Görüntünün sağ üstü (100) ekranın sol üstüne, sol üstü (40) sol altına, sağ altı (220) sağ üstüne, sol altı (160) sağ altına
        const int tl = Pixel(out, W, H, 1, 1)[0], tr = Pixel(out, W, H, W - 2, 1)[0];
        const int bl = Pixel(out, W, H, 1, H - 2)[0], br = Pixel(out, W, H, W - 2, H - 2)[0];
        std::printf("      portrait corners: top-left %d, top-right %d, bottom-left %d, bottom-right %d\n", tl, tr, bl, br);
        Expect(tl == 100 && tr == 220 && bl == 40 && br == 160, "portrait: the image is turned 90 degrees (its right edge at the top of the screen)");
        // Ters dönüşüm gerçekten ters: ekranın her köşesi görüntüye gidip displayTransform ile geri gelir
        const ARKitImage::Affine back = ARKitImage::Invert(toImage);
        Expect(std::abs(back.a - display.a) < 1e-9 && std::abs(back.b - display.b) < 1e-9 && std::abs(back.ty - display.ty) < 1e-9,
               "Invert(Invert(displayTransform)) == displayTransform");
    }
    std::printf("%s (%d failures)\n", s_Failures ? "FAILED" : "OK", s_Failures);
    return s_Failures ? 1 : 0;
}
