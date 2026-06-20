#include "Random.h"
#include <chrono>

namespace Conqueror::Math
{
    thread_local std::mt19937 Random::s_RandomEngine(std::random_device{}());
    thread_local std::uniform_real_distribution<float> Random::s_Distribution(0.0f, 1.0f);

    void Random::SetSeed(uint32_t seed)
    {
        s_RandomEngine.seed(seed);
    }

    float Random::Float()
    {
        return s_Distribution(s_RandomEngine);
    }

    float Random::Float(float min, float max)
    {
        return min + (max - min) * Float();
    }

    int Random::Int(int min, int max)
    {
        return min + static_cast<int>(Float() * (max - min + 1));
    }

    glm::vec2 Random::Vec2()
    {
        return glm::vec2(Float(), Float());
    }

    glm::vec2 Random::Vec2(const glm::vec2& min, const glm::vec2& max)
    {
        return glm::vec2(
            Float(min.x, max.x),
            Float(min.y, max.y)
        );
    }

    glm::vec3 Random::Vec3()
    {
        return glm::vec3(Float(), Float(), Float());
    }

    glm::vec3 Random::Vec3(const glm::vec3& min, const glm::vec3& max)
    {
        return glm::vec3(
            Float(min.x, max.x),
            Float(min.y, max.y),
            Float(min.z, max.z)
        );
    }

    glm::vec3 Random::InUnitSphere()
    {
        while (true)
        {
            glm::vec3 p = Vec3(glm::vec3(-1.0f), glm::vec3(1.0f));
            if (glm::dot(p, p) < 1.0f)
                return p;
        }
    }

    glm::vec3 Random::OnUnitSphere()
    {
        return glm::normalize(InUnitSphere());
    }

    glm::vec2 Random::InUnitCircle()
    {
        while (true)
        {
            glm::vec2 p = Vec2(glm::vec2(-1.0f), glm::vec2(1.0f));
            if (glm::dot(p, p) < 1.0f)
                return p;
        }
    }

    glm::vec2 Random::OnUnitCircle()
    {
        return glm::normalize(InUnitCircle());
    }

    float Random::Gaussian(float mean, float stddev)
    {
        // Box-Muller transform
        float u1 = Float();
        float u2 = Float();
        if (u1 <= 0.0f) u1 = 0.0001f;
        float z0 = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265359f * u2);
        return mean + z0 * stddev;
    }
}
