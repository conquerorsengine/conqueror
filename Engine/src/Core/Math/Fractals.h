#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <string>

namespace Conqueror::Math {
    class CQ_API Fractals {
    public:
        // Mandelbrot Set
        // c = x + iy noktası için maksimum iterasyona ulaşıp ulaşmadığını döndürür
        static int Mandelbrot(float x, float y, int maxIterations);

        // Julia Set
        // z = x + iy noktası ve sabit c = cx + icy için
        static int Julia(float x, float y, float cx, float cy, int maxIterations);

        // 3D Mandelbulb (Mesafe Kestirim Fonksiyonu)
        static float MandelbulbDistance(const CQVec3& pos, int maxIterations, float power = 8.0f);

        // L-System (Basit kural tabanlı sistem değerlendiricisi)
        // rules: örn "F->F+F-F-F+F"
        static std::string EvaluateLSystem(const std::string& axiom, const std::string& ruleMatch, const std::string& ruleReplace, int iterations);
    };
}
