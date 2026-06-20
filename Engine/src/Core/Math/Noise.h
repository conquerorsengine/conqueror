#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror::Math
{
    // Perlin Noise (classic gradient noise)
    class CQ_API PerlinNoise
    {
    public:
        PerlinNoise(uint32_t seed = 0);
        
        float Noise1D(float x) const;
        float Noise2D(float x, float y) const;
        float Noise3D(float x, float y, float z) const;
        
        // Fractal Brownian Motion (layered noise)
        float FBM1D(float x, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const;
        float FBM2D(float x, float y, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const;
        float FBM3D(float x, float y, float z, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const;
        
        // Turbulence (absolute value of noise)
        float Turbulence2D(float x, float y, int octaves = 4) const;
        float Turbulence3D(float x, float y, float z, int octaves = 4) const;
        
    private:
        static constexpr int PERMUTATION_SIZE = 512;
        int m_Permutation[PERMUTATION_SIZE];
        
        float Fade(float t) const;
        float Lerp(float t, float a, float b) const;
        float Grad(int hash, float x, float y, float z) const;
    };

    // Simplex Noise (improved Perlin, less directional artifacts)
    class CQ_API SimplexNoise
    {
    public:
        SimplexNoise(uint32_t seed = 0);
        
        float Noise2D(float x, float y) const;
        float Noise3D(float x, float y, float z) const;
        float Noise4D(float x, float y, float z, float w) const;
        
        float FBM2D(float x, float y, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const;
        float FBM3D(float x, float y, float z, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const;
        
    private:
        static constexpr int PERMUTATION_SIZE = 512;
        int m_Permutation[PERMUTATION_SIZE];
        
        struct Grad3 { float x, y, z; };
        static const Grad3 s_Grad3[];
        
        int FastFloor(float x) const;
        float Dot(const Grad3& g, float x, float y, float z) const;
    };

    // Worley Noise (cellular/voronoi noise)
    class CQ_API WorleyNoise
    {
    public:
        enum class DistanceFunction
        {
            Euclidean,
            Manhattan,
            Chebyshev,
            Minkowski
        };
        
        WorleyNoise(uint32_t seed = 0);
        
        // Returns distance to nearest feature point
        float Noise2D(float x, float y, DistanceFunction func = DistanceFunction::Euclidean) const;
        float Noise3D(float x, float y, float z, DistanceFunction func = DistanceFunction::Euclidean) const;
        
        // Returns distances to N nearest points
        void NoiseN2D(float x, float y, float* distances, int count, DistanceFunction func = DistanceFunction::Euclidean) const;
        void NoiseN3D(float x, float y, float z, float* distances, int count, DistanceFunction func = DistanceFunction::Euclidean) const;
        
        // Cell-based patterns
        float Cells2D(float x, float y) const; // F1
        float CellBorders2D(float x, float y) const; // F2 - F1
        
    private:
        uint32_t m_Seed;
        
        glm::vec2 Hash2D(int x, int y) const;
        glm::vec3 Hash3D(int x, int y, int z) const;
        
        float Distance(const glm::vec2& a, const glm::vec2& b, DistanceFunction func) const;
        float Distance(const glm::vec3& a, const glm::vec3& b, DistanceFunction func) const;
    };

    // Value Noise (simpler than Perlin, grid-based)
    class CQ_API ValueNoise
    {
    public:
        ValueNoise(uint32_t seed = 0);
        
        float Noise2D(float x, float y) const;
        float Noise3D(float x, float y, float z) const;
        
        float FBM2D(float x, float y, int octaves = 4) const;
        float FBM3D(float x, float y, float z, int octaves = 4) const;
        
    private:
        static constexpr int PERMUTATION_SIZE = 512;
        int m_Permutation[PERMUTATION_SIZE];
        
        float Hash(int x, int y) const;
        float Hash(int x, int y, int z) const;
        float SmoothStep(float t) const;
    };

    // White Noise (pure random)
    class CQ_API WhiteNoise
    {
    public:
        WhiteNoise(uint32_t seed = 0);
        
        float Noise1D(float x) const;
        float Noise2D(float x, float y) const;
        float Noise3D(float x, float y, float z) const;
        
    private:
        uint32_t m_Seed;
        uint32_t Hash(uint32_t x) const;
    };
}
