#include "PolygonUnion.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror::PolygonUnion
{
    namespace
    {
        bool IsAxisRect(const std::vector<glm::vec2>& p, glm::vec4& rect)
        {
            if (p.size() != 4)
                return false;
            glm::vec2 mn = p[0], mx = p[0];
            for (auto& q : p) { mn = glm::min(mn, q); mx = glm::max(mx, q); }
            const float eps = 1e-4f * std::max(1.0f, glm::length(mx - mn));
            for (auto& q : p)
                if ((std::fabs(q.x - mn.x) > eps && std::fabs(q.x - mx.x) > eps) || (std::fabs(q.y - mn.y) > eps && std::fabs(q.y - mx.y) > eps))
                    return false;
            if (mx.x - mn.x <= eps || mx.y - mn.y <= eps)
                return false;
            rect = glm::vec4(mn, mx);
            return true;
        }

        // Çift-tek kuralı (içbükey çokgenler de)
        bool Inside(const std::vector<glm::vec2>& poly, const glm::vec2& p)
        {
            bool in = false;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            {
                const glm::vec2& a = poly[i];
                const glm::vec2& b = poly[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                    in = !in;
            }
            return in;
        }

        void Dedupe(std::vector<float>& v)
        {
            std::sort(v.begin(), v.end());
            std::vector<float> out;
            for (float x : v)
                if (out.empty() || x - out.back() > 1e-5f * std::max(1.0f, std::fabs(x)))
                    out.push_back(x);
            v.swap(out);
        }

        float PointLineDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            glm::vec2 ab = b - a;
            float len2 = glm::dot(ab, ab);
            if (len2 < 1e-12f)
                return glm::length(p - a);
            float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
            return glm::length(p - (a + ab * t));
        }

        void DouglasPeucker(const std::vector<glm::vec2>& pts, size_t first, size_t last, float tol, std::vector<char>& keep)
        {
            if (last <= first + 1)
                return;
            float best = -1.0f;
            size_t index = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                float d = PointLineDistance(pts[i], pts[first], pts[last]);
                if (d > best) { best = d; index = i; }
            }
            if (best > tol)
            {
                keep[index] = 1;
                DouglasPeucker(pts, first, index, tol, keep);
                DouglasPeucker(pts, index, last, tol, keep);
            }
        }

        std::vector<glm::vec2> RemoveCollinear(const std::vector<glm::vec2>& loop)
        {
            std::vector<glm::vec2> out;
            const size_t n = loop.size();
            for (size_t i = 0; i < n; ++i)
            {
                const glm::vec2& prev = loop[(i + n - 1) % n];
                const glm::vec2& cur = loop[i];
                const glm::vec2& next = loop[(i + 1) % n];
                glm::vec2 a = cur - prev, b = next - cur;
                float cross = a.x * b.y - a.y * b.x;
                if (std::fabs(cross) > 1e-9f * std::max(1.0f, glm::dot(a, a) + glm::dot(b, b)) || glm::dot(a, b) < 0.0f)
                    out.push_back(cur);
            }
            return out;
        }

        // Kapalı kontur sadeleştirme: en uzak iki noktadan böl, iki yarıya Douglas-Peucker
        std::vector<glm::vec2> SimplifyLoop(const std::vector<glm::vec2>& loop, float tol)
        {
            const size_t n = loop.size();
            if (n < 5 || tol <= 0.0f)
                return loop;
            size_t a = 0, b = 0;
            float best = -1.0f;
            for (size_t i = 0; i < n; ++i)
            {
                float d = glm::length(loop[i] - loop[0]);
                if (d > best) { best = d; b = i; }
            }
            // a = 0, b = en uzak; döngüyü a'dan başlat
            std::vector<glm::vec2> pts(loop.begin(), loop.end());
            pts.push_back(loop[0]);
            std::vector<char> keep(pts.size(), 0);
            keep[a] = keep[b] = keep[pts.size() - 1] = 1;
            DouglasPeucker(pts, a, b, tol, keep);
            DouglasPeucker(pts, b, pts.size() - 1, tol, keep);
            std::vector<glm::vec2> out;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                if (keep[i])
                    out.push_back(pts[i]);
            return out.size() >= 3 ? out : loop;
        }
    }

    float SignedArea(const std::vector<glm::vec2>& loop)
    {
        float a = 0.0f;
        for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
            a += loop[j].x * loop[i].y - loop[i].x * loop[j].y;
        return a * 0.5f;
    }

    Result Build(const std::vector<std::vector<glm::vec2>>& polygons, float cellSize, bool wantBoxes)
    {
        Result result;
        std::vector<const std::vector<glm::vec2>*> polys;
        glm::vec2 mn(1e30f), mx(-1e30f);
        for (auto& p : polygons)
        {
            if (p.size() < 3)
                continue;
            polys.push_back(&p);
            for (auto& q : p) { mn = glm::min(mn, q); mx = glm::max(mx, q); }
        }
        if (polys.empty())
            return result;

        // Izgara: hepsi dikdörtgense kenar koordinatları (kesin), değilse düzgün hücreler
        std::vector<glm::vec4> rects;
        bool allRects = true;
        for (auto* p : polys)
        {
            glm::vec4 r;
            if (IsAxisRect(*p, r)) rects.push_back(r);
            else { allRects = false; break; }
        }
        std::vector<float> xs, ys;
        if (allRects)
        {
            for (auto& r : rects) { xs.push_back(r.x); xs.push_back(r.z); ys.push_back(r.y); ys.push_back(r.w); }
            Dedupe(xs);
            Dedupe(ys);
            result.Exact = true;
        }
        else
        {
            float h = std::max(cellSize, 1e-3f);
            // En fazla ~4 milyon hücre
            const glm::vec2 extent = mx - mn;
            while ((extent.x / h + 2.0f) * (extent.y / h + 2.0f) > 4.0e6f)
                h *= 1.5f;
            const int nx = (int)std::ceil(extent.x / h) + 2, ny = (int)std::ceil(extent.y / h) + 2;
            for (int i = 0; i <= nx; ++i) xs.push_back(mn.x - h + (float)i * h);
            for (int j = 0; j <= ny; ++j) ys.push_back(mn.y - h + (float)j * h);
            cellSize = h;
        }
        const int cx = (int)xs.size() - 1, cy = (int)ys.size() - 1;
        if (cx <= 0 || cy <= 0)
            return result;

        // Doluluk: hücre merkezi herhangi bir kaynak çokgenin içinde mi
        std::vector<char> filled((size_t)cx * (size_t)cy, 0);
        auto at = [&](int i, int j) -> char { return (i < 0 || j < 0 || i >= cx || j >= cy) ? 0 : filled[(size_t)j * (size_t)cx + (size_t)i]; };
        auto findCell = [](const std::vector<float>& v, float x) {
            return (int)(std::upper_bound(v.begin(), v.end(), x) - v.begin()) - 1;
        };
        if (allRects)
        {
            for (auto& r : rects)
            {
                int i0 = findCell(xs, r.x + 1e-6f), i1 = findCell(xs, r.z - 1e-6f);
                int j0 = findCell(ys, r.y + 1e-6f), j1 = findCell(ys, r.w - 1e-6f);
                for (int j = std::max(j0, 0); j <= std::min(j1, cy - 1); ++j)
                    for (int i = std::max(i0, 0); i <= std::min(i1, cx - 1); ++i)
                        filled[(size_t)j * (size_t)cx + (size_t)i] = 1;
            }
        }
        else
        {
            for (auto* p : polys)
            {
                glm::vec2 pmn((*p)[0]), pmx((*p)[0]);
                for (auto& q : *p) { pmn = glm::min(pmn, q); pmx = glm::max(pmx, q); }
                int i0 = std::max(0, findCell(xs, pmn.x)), i1 = std::min(cx - 1, findCell(xs, pmx.x));
                int j0 = std::max(0, findCell(ys, pmn.y)), j1 = std::min(cy - 1, findCell(ys, pmx.y));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        size_t k = (size_t)j * (size_t)cx + (size_t)i;
                        if (!filled[k] && Inside(*p, glm::vec2((xs[(size_t)i] + xs[(size_t)i + 1]) * 0.5f, (ys[(size_t)j] + ys[(size_t)j + 1]) * 0.5f)))
                            filled[k] = 1;
                    }
            }
        }

        // Sınır kenarları: dolu hücre solda kalacak yönde; köşe (i, j) -> anahtar
        struct Edge { int X0, Y0, X1, Y1; bool Used = false; };
        std::vector<Edge> edges;
        std::unordered_multimap<long long, size_t> outgoing;
        auto key = [&](int x, int y) { return (long long)y * (long long)(cx + 1) + x; };
        auto addEdge = [&](int x0, int y0, int x1, int y1) {
            outgoing.emplace(key(x0, y0), edges.size());
            edges.push_back({ x0, y0, x1, y1 });
        };
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i)
            {
                if (!at(i, j))
                    continue;
                if (!at(i, j - 1)) addEdge(i, j, i + 1, j);           // alt: soldan sağa
                if (!at(i + 1, j)) addEdge(i + 1, j, i + 1, j + 1);   // sağ: yukarı
                if (!at(i, j + 1)) addEdge(i + 1, j + 1, i, j + 1);   // üst: sağdan sola
                if (!at(i - 1, j)) addEdge(i, j + 1, i, j);           // sol: aşağı
            }

        // Kenarları konturlara bağla; çapraz değen hücrelerde sola dönüş (konturlar ayrı kalır)
        for (size_t start = 0; start < edges.size(); ++start)
        {
            if (edges[start].Used)
                continue;
            std::vector<glm::vec2> loop;
            size_t cur = start;
            while (true)
            {
                Edge& e = edges[cur];
                e.Used = true;
                loop.emplace_back(xs[(size_t)e.X0], ys[(size_t)e.Y0]);
                if (e.X1 == edges[start].X0 && e.Y1 == edges[start].Y0)
                    break;
                const int dx = e.X1 - e.X0, dy = e.Y1 - e.Y0;
                size_t next = (size_t)-1;
                int bestTurn = -10;
                auto range = outgoing.equal_range(key(e.X1, e.Y1));
                for (auto it = range.first; it != range.second; ++it)
                {
                    const Edge& c = edges[it->second];
                    if (c.Used)
                        continue;
                    const int ndx = c.X1 - c.X0, ndy = c.Y1 - c.Y0;
                    // Dönüş: sol = 1, düz = 0, sağ = -1 (işaretli çapraz çarpım)
                    const int turn = ((dx > 0) - (dx < 0)) * ((ndy > 0) - (ndy < 0)) - ((dy > 0) - (dy < 0)) * ((ndx > 0) - (ndx < 0));
                    if (turn > bestTurn) { bestTurn = turn; next = it->second; }
                }
                if (next == (size_t)-1)
                    break;   // açık kalan (olmamalı)
                cur = next;
            }
            loop = RemoveCollinear(loop);
            if (!result.Exact)
                loop = RemoveCollinear(SimplifyLoop(loop, cellSize * 1.5f));   // basamaklar gerçek kenardan ~1 hücre sapar
            if (loop.size() >= 3 && std::fabs(SignedArea(loop)) > 1e-9f)
                result.Loops.push_back(std::move(loop));
        }

        // Dikdörtgenler: açgözlü birleştirme (önce sağa, sonra yukarı)
        if (wantBoxes)
        {
            std::vector<char> used(filled.size(), 0);
            for (int j = 0; j < cy; ++j)
                for (int i = 0; i < cx; ++i)
                {
                    size_t k = (size_t)j * (size_t)cx + (size_t)i;
                    if (!filled[k] || used[k])
                        continue;
                    int i1 = i;
                    while (i1 + 1 < cx && filled[k + (size_t)(i1 + 1 - i)] && !used[k + (size_t)(i1 + 1 - i)])
                        ++i1;
                    int j1 = j;
                    for (;;)
                    {
                        if (j1 + 1 >= cy) break;
                        bool rowOk = true;
                        for (int x = i; x <= i1 && rowOk; ++x)
                        {
                            size_t kk = (size_t)(j1 + 1) * (size_t)cx + (size_t)x;
                            rowOk = filled[kk] && !used[kk];
                        }
                        if (!rowOk) break;
                        ++j1;
                    }
                    for (int y = j; y <= j1; ++y)
                        for (int x = i; x <= i1; ++x)
                            used[(size_t)y * (size_t)cx + (size_t)x] = 1;
                    result.Boxes.emplace_back(xs[(size_t)i], ys[(size_t)j], xs[(size_t)i1 + 1], ys[(size_t)j1 + 1]);
                }
        }
        return result;
    }
}
