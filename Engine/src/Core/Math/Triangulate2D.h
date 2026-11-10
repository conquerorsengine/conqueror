#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

// Basit (kendini kesmeyen) çokgeni kulak kırpma ile üçgenlere böler; saat yönü fark etmez. Üçgenler saat yönünün tersine.
// Sprite Shape dolgusu ve içbükey Polygon Collider 2D için.
namespace Conqueror::Triangulate2D
{
    inline float SignedArea(const std::vector<glm::vec2>& p)
    {
        float a = 0.0f;
        for (size_t i = 0, n = p.size(); i < n; ++i)
        {
            const glm::vec2& u = p[i];
            const glm::vec2& v = p[(i + 1) % n];
            a += u.x * v.y - v.x * u.y;
        }
        return a * 0.5f;
    }

    inline bool IsConvex(const std::vector<glm::vec2>& p)
    {
        const size_t n = p.size();
        if (n < 3)
            return false;
        int sign = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const glm::vec2 a = p[(i + 1) % n] - p[i], b = p[(i + 2) % n] - p[(i + 1) % n];
            const float c = a.x * b.y - a.y * b.x;
            if (std::abs(c) < 1e-9f)
                continue;
            const int s = c > 0.0f ? 1 : -1;
            if (sign == 0) sign = s;
            else if (s != sign) return false;
        }
        return true;
    }

    // out: üçgen köşe indisleri (3'er). Başarısızsa (kendini kesen çokgen) false; o ana kadar bulunanlar kalır
    inline bool Triangulate(const std::vector<glm::vec2>& p, std::vector<uint32_t>& out)
    {
        out.clear();
        const size_t n = p.size();
        if (n < 3)
            return false;
        std::vector<uint32_t> v(n);
        const bool ccw = SignedArea(p) >= 0.0f;
        for (size_t i = 0; i < n; ++i)
            v[i] = (uint32_t)(ccw ? i : n - 1 - i);
        auto cross = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
        auto inside = [&](const glm::vec2& q, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
            return cross(a, b, q) >= 0.0f && cross(b, c, q) >= 0.0f && cross(c, a, q) >= 0.0f;
        };
        size_t guard = 0;
        while (v.size() > 3 && guard++ < n * n + 16)
        {
            bool clipped = false;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const size_t ia = (i + v.size() - 1) % v.size(), ib = i, ic = (i + 1) % v.size();
                const glm::vec2& a = p[v[ia]];
                const glm::vec2& b = p[v[ib]];
                const glm::vec2& c = p[v[ic]];
                if (cross(a, b, c) <= 1e-12f)
                    continue;   // içbükey köşe
                bool ear = true;
                for (size_t j = 0; j < v.size() && ear; ++j)
                {
                    if (j == ia || j == ib || j == ic)
                        continue;
                    const glm::vec2& q = p[v[j]];
                    if (q == a || q == b || q == c)
                        continue;
                    if (inside(q, a, b, c))
                        ear = false;
                }
                if (!ear)
                    continue;
                out.push_back(v[ia]);
                out.push_back(v[ib]);
                out.push_back(v[ic]);
                v.erase(v.begin() + (std::ptrdiff_t)ib);
                clipped = true;
                break;
            }
            if (!clipped)
                return false;
        }
        if (v.size() == 3)
        {
            out.push_back(v[0]);
            out.push_back(v[1]);
            out.push_back(v[2]);
        }
        return true;
    }
}
