#include "Noise.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math
{
    // ============================================================================
    // PerlinNoise Implementation
    // ============================================================================
    
    PerlinNoise::PerlinNoise(uint32_t seed)
    {
        // Standard permutation table
        int p[] = {
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
            8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
            35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
            134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
            55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
            18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
            250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
            189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
            172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
            228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
            107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
            138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
        };
        
        // Seed'e göre permutation table'ı karıştır
        for (int i = 0; i < 256; i++)
        {
            m_Permutation[i] = p[i];
            m_Permutation[256 + i] = p[i];
        }
        
        if (seed != 0)
        {
            // Basit seed-based shuffle
            for (int i = 0; i < 256; i++)
            {
                int j = (seed + i * 17) % 256;
                std::swap(m_Permutation[i], m_Permutation[j]);
                m_Permutation[256 + i] = m_Permutation[i];
            }
        }
    }
    
    float PerlinNoise::Fade(float t) const
    {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
    
    float PerlinNoise::Lerp(float t, float a, float b) const
    {
        return a + t * (b - a);
    }
    
    float PerlinNoise::Grad(int hash, float x, float y, float z) const
    {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
    
    float PerlinNoise::Noise1D(float x) const
    {
        return Noise3D(x, 0.0f, 0.0f);
    }
    
    float PerlinNoise::Noise2D(float x, float y) const
    {
        return Noise3D(x, y, 0.0f);
    }
    
    float PerlinNoise::Noise3D(float x, float y, float z) const
    {
        int X = (int)std::floor(x) & 255;
        int Y = (int)std::floor(y) & 255;
        int Z = (int)std::floor(z) & 255;
        
        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);
        
        float u = Fade(x);
        float v = Fade(y);
        float w = Fade(z);
        
        int A = m_Permutation[X] + Y;
        int AA = m_Permutation[A] + Z;
        int AB = m_Permutation[A + 1] + Z;
        int B = m_Permutation[X + 1] + Y;
        int BA = m_Permutation[B] + Z;
        int BB = m_Permutation[B + 1] + Z;
        
        return Lerp(w, Lerp(v, Lerp(u, Grad(m_Permutation[AA], x, y, z),
                                        Grad(m_Permutation[BA], x - 1, y, z)),
                               Lerp(u, Grad(m_Permutation[AB], x, y - 1, z),
                                        Grad(m_Permutation[BB], x - 1, y - 1, z))),
                       Lerp(v, Lerp(u, Grad(m_Permutation[AA + 1], x, y, z - 1),
                                        Grad(m_Permutation[BA + 1], x - 1, y, z - 1)),
                               Lerp(u, Grad(m_Permutation[AB + 1], x, y - 1, z - 1),
                                        Grad(m_Permutation[BB + 1], x - 1, y - 1, z - 1))));
    }
    
    float PerlinNoise::FBM1D(float x, int octaves, float lacunarity, float gain) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise1D(x * frequency) * amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        
        return sum;
    }
    
    float PerlinNoise::FBM2D(float x, float y, int octaves, float lacunarity, float gain) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise2D(x * frequency, y * frequency) * amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        
        return sum;
    }
    
    float PerlinNoise::FBM3D(float x, float y, float z, int octaves, float lacunarity, float gain) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        
        return sum;
    }
    
    float PerlinNoise::Turbulence2D(float x, float y, int octaves) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += std::abs(Noise2D(x * frequency, y * frequency)) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        return sum;
    }
    
    float PerlinNoise::Turbulence3D(float x, float y, float z, int octaves) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += std::abs(Noise3D(x * frequency, y * frequency, z * frequency)) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        return sum;
    }
    
    // ============================================================================
    // SimplexNoise Implementation
    // ============================================================================
    
    const SimplexNoise::Grad3 SimplexNoise::s_Grad3[] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    };
    
    SimplexNoise::SimplexNoise(uint32_t seed)
    {
        int p[] = {
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
            8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
            35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
            134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
            55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
            18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
            250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
            189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
            172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
            228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
            107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
            138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
        };
        
        for (int i = 0; i < 256; i++)
        {
            m_Permutation[i] = p[i];
            m_Permutation[256 + i] = p[i];
        }
        
        if (seed != 0)
        {
            for (int i = 0; i < 256; i++)
            {
                int j = (seed + i * 17) % 256;
                std::swap(m_Permutation[i], m_Permutation[j]);
                m_Permutation[256 + i] = m_Permutation[i];
            }
        }
    }
    
    int SimplexNoise::FastFloor(float x) const
    {
        return x > 0 ? (int)x : (int)x - 1;
    }
    
    float SimplexNoise::Dot(const Grad3& g, float x, float y, float z) const
    {
        return g.x * x + g.y * y + g.z * z;
    }
    
    float SimplexNoise::Noise2D(float x, float y) const
    {
        const float F2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
        const float G2 = (3.0f - std::sqrt(3.0f)) / 6.0f;
        
        float s = (x + y) * F2;
        int i = FastFloor(x + s);
        int j = FastFloor(y + s);
        
        float t = (i + j) * G2;
        float X0 = i - t;
        float Y0 = j - t;
        float x0 = x - X0;
        float y0 = y - Y0;
        
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else { i1 = 0; j1 = 1; }
        
        float x1 = x0 - i1 + G2;
        float y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;
        
        int ii = i & 255;
        int jj = j & 255;
        int gi0 = m_Permutation[ii + m_Permutation[jj]] % 12;
        int gi1 = m_Permutation[ii + i1 + m_Permutation[jj + j1]] % 12;
        int gi2 = m_Permutation[ii + 1 + m_Permutation[jj + 1]] % 12;
        
        float n0, n1, n2;
        
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 < 0) n0 = 0.0f;
        else {
            t0 *= t0;
            n0 = t0 * t0 * Dot(s_Grad3[gi0], x0, y0, 0);
        }
        
        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 < 0) n1 = 0.0f;
        else {
            t1 *= t1;
            n1 = t1 * t1 * Dot(s_Grad3[gi1], x1, y1, 0);
        }
        
        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 < 0) n2 = 0.0f;
        else {
            t2 *= t2;
            n2 = t2 * t2 * Dot(s_Grad3[gi2], x2, y2, 0);
        }
        
        return 70.0f * (n0 + n1 + n2);
    }
    
    float SimplexNoise::Noise3D(float x, float y, float z) const
    {
        // Basit 3D simplex noise implementation
        return Noise2D(x, y) * 0.5f + Noise2D(y, z) * 0.5f;
    }
    
    float SimplexNoise::Noise4D(float x, float y, float z, float w) const
    {
        // Basit 4D simplex noise implementation
        return Noise3D(x, y, z) * 0.5f + Noise3D(y, z, w) * 0.5f;
    }
    
    float SimplexNoise::FBM2D(float x, float y, int octaves, float lacunarity, float gain) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise2D(x * frequency, y * frequency) * amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        
        return sum;
    }
    
    float SimplexNoise::FBM3D(float x, float y, float z, int octaves, float lacunarity, float gain) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        
        return sum;
    }
    
    // ============================================================================
    // WorleyNoise Implementation
    // ============================================================================
    
    WorleyNoise::WorleyNoise(uint32_t seed)
        : m_Seed(seed)
    {
    }
    
    glm::vec2 WorleyNoise::Hash2D(int x, int y) const
    {
        uint32_t n = m_Seed + x * 374761393 + y * 668265263;
        n = (n ^ (n >> 13)) * 1274126177;
        n = n ^ (n >> 16);
        
        float fx = (n & 0xFFFF) / 65535.0f;
        float fy = ((n >> 16) & 0xFFFF) / 65535.0f;
        
        return glm::vec2(fx, fy);
    }
    
    glm::vec3 WorleyNoise::Hash3D(int x, int y, int z) const
    {
        uint32_t n = m_Seed + x * 374761393 + y * 668265263 + z * 1274126177;
        n = (n ^ (n >> 13)) * 1274126177;
        n = n ^ (n >> 16);
        
        float fx = (n & 0x3FF) / 1023.0f;
        float fy = ((n >> 10) & 0x3FF) / 1023.0f;
        float fz = ((n >> 20) & 0x3FF) / 1023.0f;
        
        return glm::vec3(fx, fy, fz);
    }
    
    float WorleyNoise::Distance(const glm::vec2& a, const glm::vec2& b, DistanceFunction func) const
    {
        glm::vec2 d = b - a;
        
        switch (func)
        {
            case DistanceFunction::Euclidean:
                return glm::length(d);
            case DistanceFunction::Manhattan:
                return std::abs(d.x) + std::abs(d.y);
            case DistanceFunction::Chebyshev:
                return std::max(std::abs(d.x), std::abs(d.y));
            case DistanceFunction::Minkowski:
                return std::pow(std::pow(std::abs(d.x), 4.0f) + std::pow(std::abs(d.y), 4.0f), 0.25f);
        }
        
        return glm::length(d);
    }
    
    float WorleyNoise::Distance(const glm::vec3& a, const glm::vec3& b, DistanceFunction func) const
    {
        glm::vec3 d = b - a;
        
        switch (func)
        {
            case DistanceFunction::Euclidean:
                return glm::length(d);
            case DistanceFunction::Manhattan:
                return std::abs(d.x) + std::abs(d.y) + std::abs(d.z);
            case DistanceFunction::Chebyshev:
                return std::max(std::max(std::abs(d.x), std::abs(d.y)), std::abs(d.z));
            case DistanceFunction::Minkowski:
                return std::pow(std::pow(std::abs(d.x), 4.0f) + std::pow(std::abs(d.y), 4.0f) + std::pow(std::abs(d.z), 4.0f), 0.25f);
        }
        
        return glm::length(d);
    }
    
    float WorleyNoise::Noise2D(float x, float y, DistanceFunction func) const
    {
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        
        float minDist = 10000.0f;
        
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int cx = xi + dx;
                int cy = yi + dy;
                
                glm::vec2 cellPoint = glm::vec2(cx, cy) + Hash2D(cx, cy);
                float dist = Distance(glm::vec2(x, y), cellPoint, func);
                
                minDist = std::min(minDist, dist);
            }
        }
        
        return minDist;
    }
    
    float WorleyNoise::Noise3D(float x, float y, float z, DistanceFunction func) const
    {
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        int zi = (int)std::floor(z);
        
        float minDist = 10000.0f;
        
        for (int dz = -1; dz <= 1; dz++)
        {
            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    int cx = xi + dx;
                    int cy = yi + dy;
                    int cz = zi + dz;
                    
                    glm::vec3 cellPoint = glm::vec3(cx, cy, cz) + Hash3D(cx, cy, cz);
                    float dist = Distance(glm::vec3(x, y, z), cellPoint, func);
                    
                    minDist = std::min(minDist, dist);
                }
            }
        }
        
        return minDist;
    }
    
    void WorleyNoise::NoiseN2D(float x, float y, float* distances, int count, DistanceFunction func) const
    {
        for (int i = 0; i < count; i++)
            distances[i] = 10000.0f;
        
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int cx = xi + dx;
                int cy = yi + dy;
                
                glm::vec2 cellPoint = glm::vec2(cx, cy) + Hash2D(cx, cy);
                float dist = Distance(glm::vec2(x, y), cellPoint, func);
                
                for (int i = 0; i < count; i++)
                {
                    if (dist < distances[i])
                    {
                        for (int j = count - 1; j > i; j--)
                            distances[j] = distances[j - 1];
                        distances[i] = dist;
                        break;
                    }
                }
            }
        }
    }
    
    void WorleyNoise::NoiseN3D(float x, float y, float z, float* distances, int count, DistanceFunction func) const
    {
        for (int i = 0; i < count; i++)
            distances[i] = 10000.0f;
        
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        int zi = (int)std::floor(z);
        
        for (int dz = -1; dz <= 1; dz++)
        {
            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    int cx = xi + dx;
                    int cy = yi + dy;
                    int cz = zi + dz;
                    
                    glm::vec3 cellPoint = glm::vec3(cx, cy, cz) + Hash3D(cx, cy, cz);
                    float dist = Distance(glm::vec3(x, y, z), cellPoint, func);
                    
                    for (int i = 0; i < count; i++)
                    {
                        if (dist < distances[i])
                        {
                            for (int j = count - 1; j > i; j--)
                                distances[j] = distances[j - 1];
                            distances[i] = dist;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    float WorleyNoise::Cells2D(float x, float y) const
    {
        return Noise2D(x, y);
    }
    
    float WorleyNoise::CellBorders2D(float x, float y) const
    {
        float distances[2];
        NoiseN2D(x, y, distances, 2);
        return distances[1] - distances[0];
    }
    
    // ============================================================================
    // ValueNoise Implementation
    // ============================================================================
    
    ValueNoise::ValueNoise(uint32_t seed)
    {
        int p[] = {
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
            8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
            35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
            134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
            55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
            18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
            250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
            189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
            172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
            228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
            107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
            138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
        };
        
        for (int i = 0; i < 256; i++)
        {
            m_Permutation[i] = p[i];
            m_Permutation[256 + i] = p[i];
        }
        
        if (seed != 0)
        {
            for (int i = 0; i < 256; i++)
            {
                int j = (seed + i * 17) % 256;
                std::swap(m_Permutation[i], m_Permutation[j]);
                m_Permutation[256 + i] = m_Permutation[i];
            }
        }
    }
    
    float ValueNoise::Hash(int x, int y) const
    {
        int n = x + y * 57;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }
    
    float ValueNoise::Hash(int x, int y, int z) const
    {
        int n = x + y * 57 + z * 997;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }
    
    float ValueNoise::SmoothStep(float t) const
    {
        return t * t * (3.0f - 2.0f * t);
    }
    
    float ValueNoise::Noise2D(float x, float y) const
    {
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        
        float xf = x - xi;
        float yf = y - yi;
        
        float u = SmoothStep(xf);
        float v = SmoothStep(yf);
        
        float v00 = Hash(xi, yi);
        float v10 = Hash(xi + 1, yi);
        float v01 = Hash(xi, yi + 1);
        float v11 = Hash(xi + 1, yi + 1);
        
        float x0 = v00 * (1.0f - u) + v10 * u;
        float x1 = v01 * (1.0f - u) + v11 * u;
        
        return x0 * (1.0f - v) + x1 * v;
    }
    
    float ValueNoise::Noise3D(float x, float y, float z) const
    {
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        int zi = (int)std::floor(z);
        
        float xf = x - xi;
        float yf = y - yi;
        float zf = z - zi;
        
        float u = SmoothStep(xf);
        float v = SmoothStep(yf);
        float w = SmoothStep(zf);
        
        float v000 = Hash(xi, yi, zi);
        float v100 = Hash(xi + 1, yi, zi);
        float v010 = Hash(xi, yi + 1, zi);
        float v110 = Hash(xi + 1, yi + 1, zi);
        float v001 = Hash(xi, yi, zi + 1);
        float v101 = Hash(xi + 1, yi, zi + 1);
        float v011 = Hash(xi, yi + 1, zi + 1);
        float v111 = Hash(xi + 1, yi + 1, zi + 1);
        
        float x00 = v000 * (1.0f - u) + v100 * u;
        float x10 = v010 * (1.0f - u) + v110 * u;
        float x01 = v001 * (1.0f - u) + v101 * u;
        float x11 = v011 * (1.0f - u) + v111 * u;
        
        float y0 = x00 * (1.0f - v) + x10 * v;
        float y1 = x01 * (1.0f - v) + x11 * v;
        
        return y0 * (1.0f - w) + y1 * w;
    }
    
    float ValueNoise::FBM2D(float x, float y, int octaves) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise2D(x * frequency, y * frequency) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        return sum;
    }
    
    float ValueNoise::FBM3D(float x, float y, float z, int octaves) const
    {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; i++)
        {
            sum += Noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        return sum;
    }
    
    // ============================================================================
    // WhiteNoise Implementation
    // ============================================================================
    
    WhiteNoise::WhiteNoise(uint32_t seed)
        : m_Seed(seed)
    {
    }
    
    uint32_t WhiteNoise::Hash(uint32_t x) const
    {
        x += m_Seed;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return x;
    }
    
    float WhiteNoise::Noise1D(float x) const
    {
        uint32_t xi = (uint32_t)(x * 1000.0f);
        return (Hash(xi) & 0xFFFF) / 65535.0f;
    }
    
    float WhiteNoise::Noise2D(float x, float y) const
    {
        uint32_t xi = (uint32_t)(x * 1000.0f);
        uint32_t yi = (uint32_t)(y * 1000.0f);
        return (Hash(xi + yi * 374761393) & 0xFFFF) / 65535.0f;
    }
    
    float WhiteNoise::Noise3D(float x, float y, float z) const
    {
        uint32_t xi = (uint32_t)(x * 1000.0f);
        uint32_t yi = (uint32_t)(y * 1000.0f);
        uint32_t zi = (uint32_t)(z * 1000.0f);
        return (Hash(xi + yi * 374761393 + zi * 668265263) & 0xFFFF) / 65535.0f;
    }
}
