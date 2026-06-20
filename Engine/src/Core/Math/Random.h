#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <random>

namespace Conqueror::Math
{
    class CQ_API Random
    {
    public:
        static void SetSeed(uint32_t seed);
        
        // Float [0, 1]
        static float Float();
        
        // Float [min, max]
        static float Float(float min, float max);
        
        // Int [min, max]
        static int Int(int min, int max);
        
        // Vec2 [0, 1]
        static glm::vec2 Vec2();
        
        // Vec2 [min, max]
        static glm::vec2 Vec2(const glm::vec2& min, const glm::vec2& max);
        
        // Vec3 [0, 1]
        static glm::vec3 Vec3();
        
        // Vec3 [min, max]
        static glm::vec3 Vec3(const glm::vec3& min, const glm::vec3& max);
        
        // Unit sphere içinde random nokta
        static glm::vec3 InUnitSphere();
        
        // Unit sphere üzerinde random nokta
        static glm::vec3 OnUnitSphere();

        // Unit circle içinde random nokta
        static glm::vec2 InUnitCircle();

        // Unit circle üzerinde random nokta
        static glm::vec2 OnUnitCircle();

        // Gaussian (normal) dağılım
        static float Gaussian(float mean, float stddev);

    private:
        static thread_local std::mt19937 s_RandomEngine;
        static thread_local std::uniform_real_distribution<float> s_Distribution;
    };
}
