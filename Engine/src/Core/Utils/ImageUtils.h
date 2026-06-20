#pragma once
#include "Core/Base/Base.h"
#include <vector>
#include <cstdint>

namespace Conqueror::Utils {
    class CQ_API ImageUtils {
    public:
        struct Image {
            int Width = 0;
            int Height = 0;
            std::vector<uint8_t> Pixels; // RGBA format
        };

        static Image CreateImage(int width, int height);
        static bool SetPixel(Image& img, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        static bool GetPixel(const Image& img, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a);
        static void DrawLine(Image& img, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        static void DrawCircle(Image& img, int xc, int yc, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        static void BoxBlur(Image& img, int radius);
    };
}
