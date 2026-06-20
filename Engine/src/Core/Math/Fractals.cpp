#include "Fractals.h"
#include <cmath>

namespace Conqueror::Math {

    int Fractals::Mandelbrot(float x, float y, int maxIterations) {
        float zx = 0.0f;
        float zy = 0.0f;
        int iter = 0;
        while (zx * zx + zy * zy < 4.0f && iter < maxIterations) {
            float tmp = zx * zx - zy * zy + x;
            zy = 2.0f * zx * zy + y;
            zx = tmp;
            iter++;
        }
        return iter;
    }

    int Fractals::Julia(float x, float y, float cx, float cy, int maxIterations) {
        float zx = x;
        float zy = y;
        int iter = 0;
        while (zx * zx + zy * zy < 4.0f && iter < maxIterations) {
            float tmp = zx * zx - zy * zy + cx;
            zy = 2.0f * zx * zy + cy;
            zx = tmp;
            iter++;
        }
        return iter;
    }

    float Fractals::MandelbulbDistance(const CQVec3& pos, int maxIterations, float power) {
        CQVec3 z = pos;
        float dr = 1.0f;
        float r = 0.0f;

        for (int i = 0; i < maxIterations; ++i) {
            r = Vec3Length(z);
            if (r > 2.0f) break;

            // Kutupsal koordinatlara dönüştür
            float theta = std::acos(z.z / r);
            float phi = std::atan2(z.y, z.x);

            dr = std::pow(r, power - 1.0f) * power * dr + 1.0f;

            // Ölçekle ve döndür
            float zr = std::pow(r, power);
            theta = theta * power;
            phi = phi * power;

            // Geri kartezyene dönüştür
            z.x = zr * std::sin(theta) * std::cos(phi);
            z.y = zr * std::sin(theta) * std::sin(phi);
            z.z = zr * std::cos(theta);

            z = Vec3Add(z, pos);
        }

        return 0.5f * std::log(r) * r / dr;
    }

    std::string Fractals::EvaluateLSystem(const std::string& axiom, const std::string& ruleMatch, const std::string& ruleReplace, int iterations) {
        std::string current = axiom;
        for (int i = 0; i < iterations; ++i) {
            std::string next = "";
            for (char c : current) {
                if (std::string(1, c) == ruleMatch) {
                    next += ruleReplace;
                } else {
                    next += c;
                }
            }
            current = next;
        }
        return current;
    }

}
