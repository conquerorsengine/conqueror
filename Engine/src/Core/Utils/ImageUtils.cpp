#include "ImageUtils.h"
#include <cmath>

namespace Conqueror::Utils {

    ImageUtils::Image ImageUtils::CreateImage(int width, int height) {
        Image img;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return img;
        img.Width = width;
        img.Height = height;
        img.Pixels.resize(width * height * 4, 0);
        return img;
    }

    bool ImageUtils::SetPixel(Image& img, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || x >= img.Width || y < 0 || y >= img.Height) return false;
        int idx = (y * img.Width + x) * 4;
        img.Pixels[idx] = r;
        img.Pixels[idx + 1] = g;
        img.Pixels[idx + 2] = b;
        img.Pixels[idx + 3] = a;
        return true;
    }

    bool ImageUtils::GetPixel(const Image& img, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
        if (x < 0 || x >= img.Width || y < 0 || y >= img.Height) return false;
        int idx = (y * img.Width + x) * 4;
        r = img.Pixels[idx];
        g = img.Pixels[idx + 1];
        b = img.Pixels[idx + 2];
        a = img.Pixels[idx + 3];
        return true;
    }

    void ImageUtils::DrawLine(Image& img, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        
        while (true) {
            SetPixel(img, x0, y0, r, g, b, a);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void ImageUtils::DrawCircle(Image& img, int xc, int yc, int radius, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
        int x = 0, y = radius;
        int d = 3 - 2 * radius;
        
        auto drawPoints = [&](int cx, int cy, int px, int py) {
            SetPixel(img, cx+px, cy+py, cr, cg, cb, ca);
            SetPixel(img, cx-px, cy+py, cr, cg, cb, ca);
            SetPixel(img, cx+px, cy-py, cr, cg, cb, ca);
            SetPixel(img, cx-px, cy-py, cr, cg, cb, ca);
            SetPixel(img, cx+py, cy+px, cr, cg, cb, ca);
            SetPixel(img, cx-py, cy+px, cr, cg, cb, ca);
            SetPixel(img, cx+py, cy-px, cr, cg, cb, ca);
            SetPixel(img, cx-py, cy-px, cr, cg, cb, ca);
        };
        
        drawPoints(xc, yc, x, y);
        while (y >= x) {
            x++;
            if (d > 0) { y--; d = d + 4 * (x - y) + 10; }
            else d = d + 4 * x + 6;
            drawPoints(xc, yc, x, y);
        }
    }

    void ImageUtils::BoxBlur(Image& img, int radius) {
        if (radius <= 0 || img.Pixels.empty()) return;
        std::vector<uint8_t> newPixels = img.Pixels;
        
        for (int y = 0; y < img.Height; y++) {
            for (int x = 0; x < img.Width; x++) {
                int r=0, g=0, b=0, a=0, count=0;
                for (int ky = -radius; ky <= radius; ky++) {
                    for (int kx = -radius; kx <= radius; kx++) {
                        int nx = x + kx, ny = y + ky;
                        if (nx >= 0 && nx < img.Width && ny >= 0 && ny < img.Height) {
                            int idx = (ny * img.Width + nx) * 4;
                            r += img.Pixels[idx];
                            g += img.Pixels[idx+1];
                            b += img.Pixels[idx+2];
                            a += img.Pixels[idx+3];
                            count++;
                        }
                    }
                }
                int outIdx = (y * img.Width + x) * 4;
                newPixels[outIdx] = r / count;
                newPixels[outIdx+1] = g / count;
                newPixels[outIdx+2] = b / count;
                newPixels[outIdx+3] = a / count;
            }
        }
        img.Pixels = std::move(newPixels);
    }
}
