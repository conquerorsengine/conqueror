#include "Terrain.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <random>

namespace Conqueror
{
    namespace
    {
        inline int Idx(const TerrainComponent& t, int x, int z) { return z * t.Resolution + x; }

        inline float HeightAt(const TerrainComponent& t, int x, int z)
        {
            x = std::clamp(x, 0, t.Resolution - 1);
            z = std::clamp(z, 0, t.Resolution - 1);
            return t.Heights[(size_t)Idx(t, x, z)];
        }

        // ---------------------------------------------------------- Gürültü (Perlin)
        struct Perlin
        {
            std::array<int, 512> P{};
            explicit Perlin(int seed)
            {
                std::array<int, 256> p;
                std::iota(p.begin(), p.end(), 0);
                std::mt19937 rng((uint32_t)seed);
                std::shuffle(p.begin(), p.end(), rng);
                for (int i = 0; i < 512; ++i)
                    P[(size_t)i] = p[(size_t)(i & 255)];
            }
            static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
            static float Grad(int h, float x, float y)
            {
                switch (h & 7)
                {
                    case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
                    case 4: return x; case 5: return -x; case 6: return y; default: return -y;
                }
            }
            float Noise(float x, float y) const
            {
                int X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255;
                x -= std::floor(x); y -= std::floor(y);
                float u = Fade(x), v = Fade(y);
                int a = P[(size_t)X] + Y, b = P[(size_t)X + 1] + Y;
                float n00 = Grad(P[(size_t)a], x, y), n10 = Grad(P[(size_t)b], x - 1, y);
                float n01 = Grad(P[(size_t)a + 1], x, y - 1), n11 = Grad(P[(size_t)b + 1], x - 1, y - 1);
                float nx0 = n00 + u * (n10 - n00), nx1 = n01 + u * (n11 - n01);
                return (nx0 + v * (nx1 - nx0)) * 0.7071f; // ~[-1,1]
            }
        };

        // ---------------------------------------------------------- base64
        const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string Base64Encode(const uint8_t* data, size_t len)
        {
            std::string out;
            out.reserve(((len + 2) / 3) * 4);
            for (size_t i = 0; i < len; i += 3)
            {
                uint32_t n = (uint32_t)data[i] << 16;
                if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
                if (i + 2 < len) n |= data[i + 2];
                out.push_back(kB64[(n >> 18) & 63]);
                out.push_back(kB64[(n >> 12) & 63]);
                out.push_back(i + 1 < len ? kB64[(n >> 6) & 63] : '=');
                out.push_back(i + 2 < len ? kB64[n & 63] : '=');
            }
            return out;
        }

        std::vector<uint8_t> Base64Decode(const std::string& in)
        {
            static int8_t table[256];
            static bool init = false;
            if (!init)
            {
                std::fill(std::begin(table), std::end(table), (int8_t)-1);
                for (int i = 0; i < 64; ++i) table[(uint8_t)kB64[i]] = (int8_t)i;
                init = true;
            }
            std::vector<uint8_t> out;
            out.reserve(in.size() * 3 / 4);
            uint32_t buf = 0;
            int bits = 0;
            for (char c : in)
            {
                int8_t v = table[(uint8_t)c];
                if (v < 0) continue;
                buf = (buf << 6) | (uint32_t)v;
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back((uint8_t)((buf >> bits) & 0xFF));
                }
            }
            return out;
        }

        Vertex BuildVertex(const TerrainComponent& t, int x, int z)
        {
            const int r = t.Resolution;
            const float sx = t.Width / (float)(r - 1), sz = t.Length / (float)(r - 1);
            Vertex v{};
            float h = HeightAt(t, x, z) * t.Height;
            v.Position = glm::vec3(x * sx, h, z * sz);
            // Merkezi fark normal
            float hl = HeightAt(t, x - 1, z) * t.Height, hr = HeightAt(t, x + 1, z) * t.Height;
            float hd = HeightAt(t, x, z - 1) * t.Height, hu = HeightAt(t, x, z + 1) * t.Height;
            float dx = (x > 0 && x < r - 1) ? 2.0f * sx : sx;
            float dz = (z > 0 && z < r - 1) ? 2.0f * sz : sz;
            glm::vec3 tx(dx, hr - hl, 0.0f), tz(0.0f, hu - hd, dz);
            v.Normal = glm::normalize(glm::cross(tz, tx));
            v.Tangent = glm::normalize(tx);
            v.TexCoords = glm::vec2((float)x / (r - 1), (float)z / (r - 1));
            v.TexCoords2 = v.TexCoords;
            return v;
        }
    }

    // ------------------------------------------------------------------ Veri

    void TerrainSystem::Initialize(TerrainComponent& t, int resolution, float flatHeight01)
    {
        t.Resolution = std::clamp(resolution, 17, 2049);
        t.Heights.assign((size_t)t.Resolution * t.Resolution, std::clamp(flatHeight01, 0.0f, 1.0f));
        t.SplatResolution = std::clamp(t.SplatResolution, 16, 2048);
        t.Splat.assign((size_t)t.SplatResolution * t.SplatResolution * 4, 0);
        for (size_t i = 0; i < t.Splat.size(); i += 4)
            t.Splat[i] = 255; // katman 0
        t.MeshDirty = true;
        t.SplatDirty = true;
        t.DataVersion++;
    }

    void TerrainSystem::EnsureData(TerrainComponent& t)
    {
        t.Resolution = std::clamp(t.Resolution, 17, 2049);
        if (t.Heights.size() != (size_t)t.Resolution * t.Resolution)
        {
            t.Heights.assign((size_t)t.Resolution * t.Resolution, 0.0f);
            t.MeshDirty = true;
        }
        t.SplatResolution = std::clamp(t.SplatResolution, 16, 2048);
        if (t.Splat.size() != (size_t)t.SplatResolution * t.SplatResolution * 4)
        {
            t.Splat.assign((size_t)t.SplatResolution * t.SplatResolution * 4, 0);
            for (size_t i = 0; i < t.Splat.size(); i += 4)
                t.Splat[i] = 255;
            t.SplatDirty = true;
        }
        t.LayerCount = std::clamp(t.LayerCount, 1, TerrainComponent::MaxLayers);
    }

    void TerrainSystem::Resample(TerrainComponent& t, int newResolution)
    {
        EnsureData(t);
        newResolution = std::clamp(newResolution, 17, 2049);
        if (newResolution == t.Resolution)
            return;
        std::vector<float> h((size_t)newResolution * newResolution);
        for (int z = 0; z < newResolution; ++z)
            for (int x = 0; x < newResolution; ++x)
                h[(size_t)z * newResolution + x] = SampleHeight01(t, (float)x / (newResolution - 1), (float)z / (newResolution - 1));
        t.Heights = std::move(h);
        t.Resolution = newResolution;
        t.RuntimeMesh.reset();
        t.MeshDirty = true;
        t.DataVersion++;
    }

    float TerrainSystem::SampleHeight01(const TerrainComponent& t, float u, float v)
    {
        if (t.Heights.empty())
            return 0.0f;
        const int r = t.Resolution;
        float fx = std::clamp(u, 0.0f, 1.0f) * (r - 1), fz = std::clamp(v, 0.0f, 1.0f) * (r - 1);
        int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
        int x1 = std::min(x0 + 1, r - 1), z1 = std::min(z0 + 1, r - 1);
        float tx = fx - x0, tz = fz - z0;
        float h00 = HeightAt(t, x0, z0), h10 = HeightAt(t, x1, z0), h01 = HeightAt(t, x0, z1), h11 = HeightAt(t, x1, z1);
        return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
    }

    float TerrainSystem::GetHeightLocal(const TerrainComponent& t, float x, float z)
    {
        return SampleHeight01(t, x / std::max(t.Width, 1e-4f), z / std::max(t.Length, 1e-4f)) * t.Height;
    }

    glm::vec3 TerrainSystem::GetNormalLocal(const TerrainComponent& t, float x, float z)
    {
        float e = std::max(t.Width, t.Length) / std::max(1, t.Resolution - 1);
        float hl = GetHeightLocal(t, x - e, z), hr = GetHeightLocal(t, x + e, z);
        float hd = GetHeightLocal(t, x, z - e), hu = GetHeightLocal(t, x, z + e);
        return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
    }

    void TerrainSystem::GetLocalBounds(const TerrainComponent& t, glm::vec3& outMin, glm::vec3& outMax)
    {
        float mn = 1.0f, mx = 0.0f;
        for (float h : t.Heights) { mn = std::min(mn, h); mx = std::max(mx, h); }
        if (t.Heights.empty()) { mn = 0.0f; mx = 0.0f; }
        outMin = glm::vec3(0.0f, mn * t.Height, 0.0f);
        outMax = glm::vec3(t.Width, std::max(mx * t.Height, mn * t.Height + 0.01f), t.Length);
    }

    bool TerrainSystem::Raycast(const TerrainComponent& t, const glm::mat4& world, const glm::vec3& origin, const glm::vec3& direction,
                                glm::vec3& outHitWorld, float maxDistance)
    {
        if (t.Heights.empty())
            return false;
        glm::mat4 inv = glm::inverse(world);
        glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
        glm::vec3 d = glm::vec3(inv * glm::vec4(direction, 0.0f));
        float dl = glm::length(d);
        if (dl < 1e-8f)
            return false;
        d /= dl;

        // Sınırlayıcı kutuya giriş/çıkış
        glm::vec3 bmin, bmax;
        GetLocalBounds(t, bmin, bmax);
        bmin.y -= 0.01f; bmax.y += 0.01f;
        float tmin = 0.0f, tmax = maxDistance;
        for (int a = 0; a < 3; ++a)
        {
            if (std::fabs(d[a]) < 1e-8f)
            {
                if (o[a] < bmin[a] || o[a] > bmax[a])
                    return false;
                continue;
            }
            float t1 = (bmin[a] - o[a]) / d[a], t2 = (bmax[a] - o[a]) / d[a];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }

        // Hücre boyutunun yarısı adımla ilerle, işaret değişince ikili arama
        const float cell = std::min(t.Width, t.Length) / std::max(1, t.Resolution - 1);
        const float step = std::max(cell * 0.5f, 1e-3f);
        auto above = [&](float s) { glm::vec3 p = o + d * s; return p.y - GetHeightLocal(t, p.x, p.z); };
        float prevS = tmin, prevF = above(tmin);
        if (prevF < 0.0f)
        {
            outHitWorld = glm::vec3(world * glm::vec4(o + d * tmin, 1.0f));
            return true;
        }
        for (float s = tmin + step; s <= tmax + step; s += step)
        {
            float cs = std::min(s, tmax);
            float f = above(cs);
            if (f <= 0.0f)
            {
                float lo = prevS, hi = cs;
                for (int i = 0; i < 20; ++i)
                {
                    float mid = (lo + hi) * 0.5f;
                    if (above(mid) > 0.0f) lo = mid; else hi = mid;
                }
                outHitWorld = glm::vec3(world * glm::vec4(o + d * hi, 1.0f));
                return true;
            }
            prevS = cs;
            prevF = f;
            if (cs >= tmax)
                break;
        }
        return false;
    }

    // ------------------------------------------------------------------ GPU

    void TerrainSystem::MarkHeightsDirty(TerrainComponent& t, int x0, int z0, int x1, int z1)
    {
        if (t.DirtyRegion.z < t.DirtyRegion.x)
            t.DirtyRegion = glm::ivec4(x0, z0, x1, z1);
        else
            t.DirtyRegion = glm::ivec4(std::min(t.DirtyRegion.x, x0), std::min(t.DirtyRegion.y, z0), std::max(t.DirtyRegion.z, x1), std::max(t.DirtyRegion.w, z1));
        t.DataVersion++;
    }

    void TerrainSystem::ReleaseGPU(TerrainComponent& t)
    {
        t.RuntimeMesh.reset();
        t.RuntimeSplat.reset();
        t.MeshDirty = true;
        t.SplatDirty = true;
    }

    void TerrainSystem::UpdateGPU(TerrainComponent& t)
    {
        EnsureData(t);
        const int r = t.Resolution;

        if (!t.RuntimeMesh || t.MeshDirty || t.RuntimeMesh->GetVertexCount() != (uint32_t)(r * r))
        {
            std::vector<Vertex> vertices((size_t)r * r);
            for (int z = 0; z < r; ++z)
                for (int x = 0; x < r; ++x)
                    vertices[(size_t)Idx(t, x, z)] = BuildVertex(t, x, z);
            std::vector<uint32_t> indices;
            indices.reserve((size_t)(r - 1) * (r - 1) * 6);
            for (int z = 0; z < r - 1; ++z)
                for (int x = 0; x < r - 1; ++x)
                {
                    uint32_t i = (uint32_t)Idx(t, x, z);
                    indices.insert(indices.end(), { i, i + (uint32_t)r, i + 1, i + 1, i + (uint32_t)r, i + (uint32_t)r + 1 });
                }
            t.RuntimeMesh = std::make_shared<Mesh>(vertices, indices);
            t.MeshDirty = false;
            t.DirtyRegion = glm::ivec4(0, 0, -1, -1);
        }
        else if (t.DirtyRegion.z >= t.DirtyRegion.x)
        {
            // Değişen satırlar (+1 kenar normaller için)
            int z0 = std::max(0, t.DirtyRegion.y - 1), z1 = std::min(r - 1, t.DirtyRegion.w + 1);
            int x0 = std::max(0, t.DirtyRegion.x - 1), x1 = std::min(r - 1, t.DirtyRegion.z + 1);
            for (int z = z0; z <= z1; ++z)
            {
                std::vector<Vertex> row((size_t)(x1 - x0 + 1));
                for (int x = x0; x <= x1; ++x)
                    row[(size_t)(x - x0)] = BuildVertex(t, x, z);
                t.RuntimeMesh->UpdateVertices(row.data(), (uint32_t)Idx(t, x0, z), (uint32_t)row.size());
            }
            t.DirtyRegion = glm::ivec4(0, 0, -1, -1);
        }

        if (!t.RuntimeSplat || t.SplatDirty || t.RuntimeSplat->GetWidth() != (uint32_t)t.SplatResolution)
        {
            if (!t.RuntimeSplat || t.RuntimeSplat->GetWidth() != (uint32_t)t.SplatResolution)
                t.RuntimeSplat = Texture2D::Create((uint32_t)t.SplatResolution, (uint32_t)t.SplatResolution);
            t.RuntimeSplat->SetData(t.Splat.data(), (uint32_t)t.Splat.size());
            t.SplatDirty = false;
        }

        for (int i = 0; i < t.LayerCount; ++i)
        {
            auto& layer = t.Layers[(size_t)i];
            if (!layer.TexturePath.empty() && (!layer.Albedo || layer.Albedo->GetPath() != layer.TexturePath))
                layer.Albedo = Texture2D::Create(layer.TexturePath);
            else if (layer.TexturePath.empty())
                layer.Albedo.reset();
        }
    }

    void TerrainSystem::Render(TerrainComponent& t, const glm::mat4& transform)
    {
        UpdateGPU(t);
        if (!t.RuntimeMesh)
            return;
        TerrainDrawParams params;
        params.Splat = t.RuntimeSplat;
        params.LayerCount = t.LayerCount;
        params.ReceiveShadows = t.ReceiveShadows;
        params.Size = glm::vec2(t.Width, t.Length);
        for (int i = 0; i < TerrainComponent::MaxLayers; ++i)
        {
            const auto& l = t.Layers[(size_t)i];
            params.Layers[(size_t)i] = l.Albedo;
            params.Tint[(size_t)i] = glm::vec4(l.Tint, l.Albedo ? 1.0f : 0.0f);
            params.Params[(size_t)i] = glm::vec4(l.Tiling, l.Roughness, l.Metallic, 0.0f);
        }
        Renderer3D::DrawTerrain(t.RuntimeMesh, transform, params);
    }

    // ------------------------------------------------------------------ Fırçalar

    void TerrainSystem::ApplyBrush(TerrainComponent& t, TerrainBrushOp op, const TerrainBrush& brush, const glm::vec2& c, float dt)
    {
        EnsureData(t);
        const float radius = std::max(brush.Radius, 0.01f);
        const float strength = std::clamp(brush.Strength, 0.0f, 1.0f);

        auto weightAt = [&](float dist) {
            float x = std::clamp(dist / radius, 0.0f, 1.0f);
            if (x >= 1.0f) return 0.0f;
            float hard = 1.0f - std::clamp(brush.Falloff, 0.0f, 1.0f);
            if (x <= hard) return 1.0f;
            float s = (x - hard) / std::max(1e-4f, 1.0f - hard);
            return 0.5f * (1.0f + std::cos(s * 3.14159265f)); // kosinüs düşüşü
        };

        if (op == TerrainBrushOp::Paint)
        {
            const int sr = t.SplatResolution;
            const float sx = t.Width / sr, sz = t.Length / sr;
            int x0 = std::max(0, (int)std::floor((c.x - radius) / sx)), x1 = std::min(sr - 1, (int)std::ceil((c.x + radius) / sx));
            int z0 = std::max(0, (int)std::floor((c.y - radius) / sz)), z1 = std::min(sr - 1, (int)std::ceil((c.y + radius) / sz));
            const int layer = std::clamp(brush.Layer, 0, TerrainComponent::MaxLayers - 1);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    glm::vec2 p((x + 0.5f) * sx, (z + 0.5f) * sz);
                    float w = weightAt(glm::length(p - c)) * strength * std::min(1.0f, dt * 6.0f);
                    if (w <= 0.0f)
                        continue;
                    uint8_t* px = &t.Splat[((size_t)z * sr + x) * 4];
                    float wts[4] = { px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f, px[3] / 255.0f };
                    for (int k = 0; k < 4; ++k)
                        wts[k] = (k == layer) ? wts[k] + (1.0f - wts[k]) * w : wts[k] * (1.0f - w);
                    float sum = wts[0] + wts[1] + wts[2] + wts[3];
                    for (int k = 0; k < 4; ++k)
                        px[k] = (uint8_t)std::lround(std::clamp(wts[k] / std::max(sum, 1e-4f), 0.0f, 1.0f) * 255.0f);
                }
            t.SplatDirty = true;
            t.DataVersion++;
            return;
        }

        const int r = t.Resolution;
        const float sx = t.Width / (r - 1), sz = t.Length / (r - 1);
        int x0 = std::max(0, (int)std::floor((c.x - radius) / sx)), x1 = std::min(r - 1, (int)std::ceil((c.x + radius) / sx));
        int z0 = std::max(0, (int)std::floor((c.y - radius) / sz)), z1 = std::min(r - 1, (int)std::ceil((c.y + radius) / sz));
        if (x0 > x1 || z0 > z1)
            return;

        // Saniye başına yaklaşık %50 yükseklik (Strength=1)
        const float rate = strength * dt * 0.5f;
        std::vector<float> src;
        if (op == TerrainBrushOp::Smooth)
            src = t.Heights;
        Perlin noise(1234);

        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                float w = weightAt(glm::length(glm::vec2(x * sx, z * sz) - c));
                if (w <= 0.0f)
                    continue;
                float& h = t.Heights[(size_t)Idx(t, x, z)];
                switch (op)
                {
                    case TerrainBrushOp::Raise: h += rate * w; break;
                    case TerrainBrushOp::Lower: h -= rate * w; break;
                    case TerrainBrushOp::Flatten: h += (brush.TargetHeight - h) * std::min(1.0f, strength * dt * 8.0f) * w; break;
                    case TerrainBrushOp::Smooth:
                    {
                        float avg = 0.0f;
                        int n = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                int xx = std::clamp(x + dx, 0, r - 1), zz = std::clamp(z + dz, 0, r - 1);
                                avg += src[(size_t)Idx(t, xx, zz)];
                                ++n;
                            }
                        avg /= n;
                        h += (avg - h) * std::min(1.0f, strength * dt * 10.0f) * w;
                        break;
                    }
                    case TerrainBrushOp::Noise:
                    {
                        float n = noise.Noise(x * sx * brush.NoiseScale, z * sz * brush.NoiseScale);
                        h += n * rate * w;
                        break;
                    }
                    default: break;
                }
                h = std::clamp(h, 0.0f, 1.0f);
            }
        }
        MarkHeightsDirty(t, x0, z0, x1, z1);
        t.ColliderVersion++;
    }

    // ------------------------------------------------------------------ Üretim

    void TerrainSystem::GenerateNoise(TerrainComponent& t, const TerrainNoiseSettings& s)
    {
        EnsureData(t);
        Perlin perlin(s.Seed);
        const int r = t.Resolution;
        const float offX = (float)((s.Seed * 7919) % 1000), offZ = (float)((s.Seed * 104729) % 1000);
        for (int z = 0; z < r; ++z)
        {
            for (int x = 0; x < r; ++x)
            {
                float u = (float)x / (r - 1), v = (float)z / (r - 1);
                float amp = 1.0f, freq = s.Frequency, sum = 0.0f, norm = 0.0f;
                for (int o = 0; o < std::clamp(s.Octaves, 1, 12); ++o)
                {
                    float n = perlin.Noise(u * freq + offX, v * freq + offZ);
                    if (s.Ridged)
                    {
                        n = 1.0f - std::fabs(n);
                        n *= n;
                    }
                    else
                    {
                        n = n * 0.5f + 0.5f;
                    }
                    sum += n * amp;
                    norm += amp;
                    amp *= s.Persistence;
                    freq *= s.Lacunarity;
                }
                float h = s.BaseHeight + (sum / std::max(norm, 1e-4f)) * s.Amplitude;
                if (s.IslandFalloff > 0.0f)
                {
                    float dx = u * 2.0f - 1.0f, dz = v * 2.0f - 1.0f;
                    float d = std::sqrt(dx * dx + dz * dz) / 1.4142f;
                    h *= 1.0f - std::pow(std::clamp(d, 0.0f, 1.0f), 2.0f) * s.IslandFalloff;
                }
                t.Heights[(size_t)Idx(t, x, z)] = std::clamp(h, 0.0f, 1.0f);
            }
        }
        if (s.ErosionIterations > 0)
            ThermalErosion(t, s.ErosionIterations);
        t.MeshDirty = true;
        t.ColliderVersion++;
        t.DataVersion++;
    }

    void TerrainSystem::Flatten(TerrainComponent& t, float height01)
    {
        EnsureData(t);
        std::fill(t.Heights.begin(), t.Heights.end(), std::clamp(height01, 0.0f, 1.0f));
        t.MeshDirty = true;
        t.ColliderVersion++;
        t.DataVersion++;
    }

    void TerrainSystem::SmoothAll(TerrainComponent& t, int iterations)
    {
        EnsureData(t);
        const int r = t.Resolution;
        for (int it = 0; it < iterations; ++it)
        {
            std::vector<float> src = t.Heights;
            for (int z = 0; z < r; ++z)
                for (int x = 0; x < r; ++x)
                {
                    float sum = 0.0f;
                    int n = 0;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            int xx = x + dx, zz = z + dz;
                            if (xx < 0 || zz < 0 || xx >= r || zz >= r) continue;
                            sum += src[(size_t)zz * r + xx];
                            ++n;
                        }
                    t.Heights[(size_t)z * r + x] = sum / n;
                }
        }
        t.MeshDirty = true;
        t.ColliderVersion++;
        t.DataVersion++;
    }

    void TerrainSystem::ThermalErosion(TerrainComponent& t, int iterations, float talus)
    {
        const int r = t.Resolution;
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int it = 0; it < iterations; ++it)
        {
            std::vector<float> delta(t.Heights.size(), 0.0f);
            for (int z = 1; z < r - 1; ++z)
                for (int x = 1; x < r - 1; ++x)
                {
                    float h = t.Heights[(size_t)z * r + x];
                    float maxDiff = 0.0f, total = 0.0f;
                    float diffs[4];
                    for (int k = 0; k < 4; ++k)
                    {
                        float d = h - t.Heights[(size_t)(z + dz[k]) * r + x + dx[k]];
                        diffs[k] = d;
                        if (d > talus) { total += d; maxDiff = std::max(maxDiff, d); }
                    }
                    if (total <= 0.0f)
                        continue;
                    float move = 0.5f * (maxDiff - talus);
                    for (int k = 0; k < 4; ++k)
                        if (diffs[k] > talus)
                        {
                            float m = move * diffs[k] / total;
                            delta[(size_t)z * r + x] -= m;
                            delta[(size_t)(z + dz[k]) * r + x + dx[k]] += m;
                        }
                }
            for (size_t i = 0; i < t.Heights.size(); ++i)
                t.Heights[i] = std::clamp(t.Heights[i] + delta[i], 0.0f, 1.0f);
        }
        t.MeshDirty = true;
        t.ColliderVersion++;
    }

    void TerrainSystem::AutoPaint(TerrainComponent& t)
    {
        EnsureData(t);
        const int sr = t.SplatResolution;
        float mn = 1.0f, mx = 0.0f;
        for (float h : t.Heights) { mn = std::min(mn, h); mx = std::max(mx, h); }
        const float range = std::max(mx - mn, 1e-4f);
        for (int z = 0; z < sr; ++z)
        {
            for (int x = 0; x < sr; ++x)
            {
                float lx = (x + 0.5f) / sr * t.Width, lz = (z + 0.5f) / sr * t.Length;
                float h01 = (GetHeightLocal(t, lx, lz) / std::max(t.Height, 1e-4f) - mn) / range;
                float slope = 1.0f - GetNormalLocal(t, lx, lz).y; // 0 düz .. 1 dikey
                float w[4];
                w[1] = std::clamp((slope - 0.12f) / 0.15f, 0.0f, 1.0f);                       // kaya
                w[2] = t.LayerCount > 2 ? std::clamp((h01 - 0.72f) / 0.12f, 0.0f, 1.0f) * (1.0f - w[1] * 0.6f) : 0.0f; // kar
                w[3] = t.LayerCount > 3 ? std::clamp((0.12f - h01) / 0.08f, 0.0f, 1.0f) * (1.0f - w[1]) : 0.0f;       // kum
                if (t.LayerCount < 2) w[1] = 0.0f;
                w[0] = std::max(0.0f, 1.0f - w[1] - w[2] - w[3]);
                float sum = w[0] + w[1] + w[2] + w[3];
                uint8_t* px = &t.Splat[((size_t)z * sr + x) * 4];
                for (int k = 0; k < 4; ++k)
                    px[k] = (uint8_t)std::lround(w[k] / sum * 255.0f);
            }
        }
        t.SplatDirty = true;
        t.DataVersion++;
    }

    void TerrainSystem::FillLayer(TerrainComponent& t, int layer)
    {
        EnsureData(t);
        layer = std::clamp(layer, 0, 3);
        for (size_t i = 0; i < t.Splat.size(); i += 4)
            for (int k = 0; k < 4; ++k)
                t.Splat[i + (size_t)k] = k == layer ? 255 : 0;
        t.SplatDirty = true;
        t.DataVersion++;
    }

    // ------------------------------------------------------------------ İçe/dışa aktarma

    bool TerrainSystem::ImportHeightmap(TerrainComponent& t, const std::filesystem::path& path)
    {
        EnsureData(t);
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::vector<float> src;
        int w = 0, h = 0;
        if (ext == ".raw" || ext == ".r16")
        {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f)
                return false;
            size_t bytes = (size_t)f.tellg();
            f.seekg(0);
            size_t count = bytes / 2;
            int side = (int)std::lround(std::sqrt((double)count));
            if ((size_t)side * side != count)
            {
                CQ_CORE_ERROR("Terrain: RAW heightmap must be square 16-bit ({0} bytes)", bytes);
                return false;
            }
            std::vector<uint16_t> data(count);
            f.read((char*)data.data(), (std::streamsize)bytes);
            src.resize(count);
            for (size_t i = 0; i < count; ++i)
                src[i] = data[i] / 65535.0f;
            w = h = side;
        }
        else
        {
            int ch = 0;
            stbi_set_flip_vertically_on_load(0);
            if (stbi_is_16_bit(path.string().c_str()))
            {
                stbi_us* data = stbi_load_16(path.string().c_str(), &w, &h, &ch, 1);
                if (!data) return false;
                src.resize((size_t)w * h);
                for (size_t i = 0; i < src.size(); ++i) src[i] = data[i] / 65535.0f;
                stbi_image_free(data);
            }
            else
            {
                stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &ch, 1);
                if (!data) return false;
                src.resize((size_t)w * h);
                for (size_t i = 0; i < src.size(); ++i) src[i] = data[i] / 255.0f;
                stbi_image_free(data);
            }
        }
        // Terrain çözünürlüğüne bilinear örnekle (görüntü satırı 0 = üst = +z sonu)
        const int r = t.Resolution;
        for (int z = 0; z < r; ++z)
            for (int x = 0; x < r; ++x)
            {
                float fx = (float)x / (r - 1) * (w - 1), fy = (1.0f - (float)z / (r - 1)) * (h - 1);
                int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
                float tx = fx - x0, ty = fy - y0;
                float a = src[(size_t)y0 * w + x0], b = src[(size_t)y0 * w + x1], c = src[(size_t)y1 * w + x0], d = src[(size_t)y1 * w + x1];
                t.Heights[(size_t)z * r + x] = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
            }
        t.MeshDirty = true;
        t.ColliderVersion++;
        t.DataVersion++;
        return true;
    }

    bool TerrainSystem::ExportHeightmap(const TerrainComponent& t, const std::filesystem::path& path)
    {
        const int r = t.Resolution;
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".raw" || ext == ".r16")
        {
            std::vector<uint16_t> data((size_t)r * r);
            for (int z = 0; z < r; ++z)
                for (int x = 0; x < r; ++x)
                    data[(size_t)(r - 1 - z) * r + x] = (uint16_t)std::lround(std::clamp(t.Heights[(size_t)z * r + x], 0.0f, 1.0f) * 65535.0f);
            std::ofstream f(path, std::ios::binary);
            f.write((const char*)data.data(), (std::streamsize)(data.size() * 2));
            return (bool)f;
        }
        std::vector<uint8_t> img((size_t)r * r);
        for (int z = 0; z < r; ++z)
            for (int x = 0; x < r; ++x)
                img[(size_t)(r - 1 - z) * r + x] = (uint8_t)std::lround(std::clamp(t.Heights[(size_t)z * r + x], 0.0f, 1.0f) * 255.0f);
        return stbi_write_png(path.string().c_str(), r, r, 1, img.data(), r) != 0;
    }

    // ------------------------------------------------------------------ Serileştirme

    std::string TerrainSystem::EncodeHeights(const TerrainComponent& t)
    {
        std::vector<uint16_t> q(t.Heights.size());
        for (size_t i = 0; i < q.size(); ++i)
            q[i] = (uint16_t)std::lround(std::clamp(t.Heights[i], 0.0f, 1.0f) * 65535.0f);
        return Base64Encode((const uint8_t*)q.data(), q.size() * 2);
    }

    bool TerrainSystem::DecodeHeights(TerrainComponent& t, const std::string& data)
    {
        auto bytes = Base64Decode(data);
        size_t count = bytes.size() / 2;
        if (count != (size_t)t.Resolution * t.Resolution)
            return false;
        t.Heights.resize(count);
        const uint16_t* q = (const uint16_t*)bytes.data();
        for (size_t i = 0; i < count; ++i)
            t.Heights[i] = q[i] / 65535.0f;
        t.MeshDirty = true;
        return true;
    }

    std::string TerrainSystem::EncodeSplat(const TerrainComponent& t)
    {
        return Base64Encode(t.Splat.data(), t.Splat.size());
    }

    bool TerrainSystem::DecodeSplat(TerrainComponent& t, const std::string& data)
    {
        auto bytes = Base64Decode(data);
        if (bytes.size() != (size_t)t.SplatResolution * t.SplatResolution * 4)
            return false;
        t.Splat = std::move(bytes);
        t.SplatDirty = true;
        return true;
    }
}
