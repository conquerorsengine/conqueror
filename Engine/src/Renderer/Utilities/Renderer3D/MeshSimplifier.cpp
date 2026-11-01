#include "MeshSimplifier.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Conqueror::MeshSimplifier
{
    namespace
    {
        // Simetrik 4x4 quadric (10 katsayı) + yalnızca yüz düzlemlerinin alan toplamı (hata normalleştirme)
        struct Quadric
        {
            double M[10] = {};
            double Area = 0.0;

            void AddPlane(const glm::dvec3& n, double d, double w)
            {
                M[0] += w * n.x * n.x; M[1] += w * n.x * n.y; M[2] += w * n.x * n.z; M[3] += w * n.x * d;
                M[4] += w * n.y * n.y; M[5] += w * n.y * n.z; M[6] += w * n.y * d;
                M[7] += w * n.z * n.z; M[8] += w * n.z * d;
                M[9] += w * d * d;
            }

            Quadric& operator+=(const Quadric& o)
            {
                for (int i = 0; i < 10; ++i)
                    M[i] += o.M[i];
                Area += o.Area;
                return *this;
            }

            double Evaluate(const glm::dvec3& v) const
            {
                const double x = v.x, y = v.y, z = v.z;
                return M[0] * x * x + 2.0 * M[1] * x * y + 2.0 * M[2] * x * z + 2.0 * M[3] * x
                     + M[4] * y * y + 2.0 * M[5] * y * z + 2.0 * M[6] * y
                     + M[7] * z * z + 2.0 * M[8] * z + M[9];
            }
        };

        struct Candidate
        {
            double Cost;
            uint32_t From, To;
            uint32_t VersionFrom, VersionTo;
            bool operator>(const Candidate& o) const { return Cost > o.Cost; }
        };

        struct EdgeInfo
        {
            uint32_t Count = 0;
            uint32_t Triangle = 0;
            uint32_t VertexA = 0, VertexB = 0;   // ilk üçgendeki öznitelik köşeleri (konum sırasına göre)
            bool Seam = false;
        };

        uint64_t EdgeKey(uint32_t a, uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return ((uint64_t)a << 32) | b;
        }
    }

    Result Simplify(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const Options& options)
    {
        Result result;
        const size_t triCount = indices.size() / 3;
        if (triCount == 0 || vertices.empty())
        {
            result.Vertices = vertices;
            result.Indices = indices;
            return result;
        }

        // 1) Konum kaynaklama: aynı konumdaki dikiş köşeleri tek düğüm
        glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);
        for (const auto& v : vertices)
        {
            bmin = glm::min(bmin, v.Position);
            bmax = glm::max(bmax, v.Position);
        }
        const double diag = std::max((double)glm::length(bmax - bmin), 1e-9);
        const double cell = diag * 1e-6;
        std::unordered_map<uint64_t, uint32_t> posMap;
        posMap.reserve(vertices.size());
        std::vector<uint32_t> posOf(vertices.size());
        std::vector<glm::dvec3> positions;
        positions.reserve(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const glm::dvec3 q = (glm::dvec3(vertices[i].Position) - glm::dvec3(bmin)) / cell;
            const uint64_t key = ((uint64_t)std::llround(q.x) & 0xFFFFFull) | (((uint64_t)std::llround(q.y) & 0xFFFFFull) << 20)
                               | (((uint64_t)std::llround(q.z) & 0xFFFFFull) << 40);
            auto [it, inserted] = posMap.emplace(key, (uint32_t)positions.size());
            if (inserted)
                positions.push_back(glm::dvec3(vertices[i].Position));
            posOf[i] = it->second;
        }
        const size_t posCount = positions.size();

        // 2) Üçgenler ve geliş listeleri
        std::vector<uint32_t> tri(indices.begin(), indices.begin() + (long)(triCount * 3));
        std::vector<uint8_t> triAlive(triCount, 0);
        std::vector<std::vector<uint32_t>> incident(posCount);
        size_t alive = 0;
        for (size_t t = 0; t < triCount; ++t)
        {
            const uint32_t p0 = posOf[tri[t * 3]], p1 = posOf[tri[t * 3 + 1]], p2 = posOf[tri[t * 3 + 2]];
            if (p0 == p1 || p1 == p2 || p0 == p2)
                continue;
            triAlive[t] = 1;
            ++alive;
            incident[p0].push_back((uint32_t)t);
            incident[p1].push_back((uint32_t)t);
            incident[p2].push_back((uint32_t)t);
        }
        const size_t target = options.TargetTriangles > 0 ? (size_t)options.TargetTriangles
                                                          : std::max<size_t>(1, (size_t)std::llround((double)alive * std::clamp(options.TargetRatio, 0.0f, 1.0f)));

        auto corner = [&](uint32_t t, int c) { return glm::dvec3(positions[posOf[tri[t * 3 + c]]]); };
        auto faceNormal = [&](const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) { return glm::cross(b - a, c - a); };

        // 3) Quadric'ler: alan ağırlıklı yüz düzlemleri (+ özgün yüz normalleri: birikerek ters dönmeyi önler)
        std::vector<Quadric> Q(posCount);
        std::vector<glm::dvec3> originalNormal(triCount, glm::dvec3(0.0));
        for (size_t t = 0; t < triCount; ++t)
        {
            if (!triAlive[t])
                continue;
            const glm::dvec3 a = corner((uint32_t)t, 0), b = corner((uint32_t)t, 1), c = corner((uint32_t)t, 2);
            glm::dvec3 n = faceNormal(a, b, c);
            const double len = glm::length(n);
            if (len < 1e-30)
                continue;
            originalNormal[t] = n / len;
            const double area = len * 0.5;
            n /= len;
            const double d = -glm::dot(n, a);
            for (int k = 0; k < 3; ++k)
            {
                Quadric& q = Q[posOf[tri[t * 3 + k]]];
                q.AddPlane(n, d, area);
                q.Area += area;
            }
        }

        // 4) Kenarlar: açık kenar / dikiş ceza düzlemleri
        std::unordered_map<uint64_t, EdgeInfo> edges;
        edges.reserve(alive * 2);
        for (size_t t = 0; t < triCount; ++t)
        {
            if (!triAlive[t])
                continue;
            for (int k = 0; k < 3; ++k)
            {
                const uint32_t va = tri[t * 3 + k], vb = tri[t * 3 + (k + 1) % 3];
                uint32_t pa = posOf[va], pb = posOf[vb];
                uint32_t a = va, b = vb;
                if (pa > pb)
                {
                    std::swap(pa, pb);
                    std::swap(a, b);
                }
                EdgeInfo& e = edges[EdgeKey(pa, pb)];
                if (e.Count == 0)
                {
                    e.Triangle = (uint32_t)t;
                    e.VertexA = a;
                    e.VertexB = b;
                }
                else if (e.VertexA != a || e.VertexB != b)
                    e.Seam = true;
                ++e.Count;
            }
        }
        std::vector<uint8_t> boundary(posCount, 0);
        for (auto& [key, e] : edges)
        {
            const bool isBoundary = e.Count == 1;
            if (!isBoundary && !e.Seam)
                continue;
            const uint32_t pa = (uint32_t)(key >> 32), pb = (uint32_t)(key & 0xFFFFFFFFu);
            const glm::dvec3 A = positions[pa], B = positions[pb];
            const uint32_t t = e.Triangle;
            glm::dvec3 fn = faceNormal(corner(t, 0), corner(t, 1), corner(t, 2));
            const double fl = glm::length(fn);
            const glm::dvec3 dir = B - A;
            const double el = glm::length(dir);
            if (fl < 1e-30 || el < 1e-30)
                continue;
            glm::dvec3 n = glm::cross(dir / el, fn / fl);
            const double nl = glm::length(n);
            if (nl < 1e-12)
                continue;
            n /= nl;
            const double d = -glm::dot(n, A);
            const double weight = (isBoundary ? 8.0 : std::max((double)options.SeamWeight, 0.0)) * el * el;
            Q[pa].AddPlane(n, d, weight);
            Q[pb].AddPlane(n, d, weight);
            if (isBoundary)
                boundary[pa] = boundary[pb] = 1;
        }

        // 5) Aday kenarlar (en düşük maliyet önce; eski sürümlü girdiler atlanır)
        std::vector<uint32_t> version(posCount, 0);
        std::vector<uint8_t> posAlive(posCount, 1);
        const double kInf = std::numeric_limits<double>::infinity();
        auto collapseCost = [&](uint32_t from, uint32_t to) -> double {
            if (options.LockBorders && boundary[from])
                return kInf;
            Quadric q = Q[from];
            q += Q[to];
            return std::max(q.Evaluate(positions[to]), 0.0) / std::max(q.Area, 1e-30);
        };
        std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
        auto pushEdge = [&](uint32_t a, uint32_t b) {
            if (a == b)
                return;
            const double ab = collapseCost(a, b), ba = collapseCost(b, a);
            if (ab == kInf && ba == kInf)
                return;
            if (ab <= ba)
                heap.push({ ab, a, b, version[a], version[b] });
            else
                heap.push({ ba, b, a, version[b], version[a] });
        };
        for (auto& [key, e] : edges)
            pushEdge((uint32_t)(key >> 32), (uint32_t)(key & 0xFFFFFFFFu));
        edges.clear();

        std::vector<uint32_t> scratchA, scratchB, attribsB;
        auto gatherNeighbors = [&](uint32_t p, std::vector<uint32_t>& out) {
            out.clear();
            auto& list = incident[p];
            size_t w = 0;
            for (size_t i = 0; i < list.size(); ++i)
            {
                const uint32_t t = list[i];
                if (!triAlive[t])
                    continue;
                list[w++] = t;   // ölü üçgenleri sıkıştır
                for (int c = 0; c < 3; ++c)
                {
                    const uint32_t q = posOf[tri[t * 3 + c]];
                    if (q != p)
                        out.push_back(q);
                }
            }
            list.resize(w);
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        };

        const double maxError = options.MaxError >= 1e29f ? kInf : std::pow((double)options.MaxError * diag, 2.0);
        double worst = 0.0;
        while (alive > target && !heap.empty())
        {
            const Candidate cand = heap.top();
            heap.pop();
            const uint32_t a = cand.From, b = cand.To;
            if (!posAlive[a] || !posAlive[b] || version[a] != cand.VersionFrom || version[b] != cand.VersionTo)
                continue;
            if (cand.Cost > maxError)
                break;

            // Bağlantı koşulu: ortak komşu sayısı <= 2 (manifold korunur), kenar hâlâ var
            gatherNeighbors(a, scratchA);
            gatherNeighbors(b, scratchB);
            if (!std::binary_search(scratchA.begin(), scratchA.end(), b))
                continue;
            size_t common = 0;
            for (uint32_t n : scratchA)
                if (std::binary_search(scratchB.begin(), scratchB.end(), n))
                    ++common;
            if (common > 2)
                continue;

            // Ters dönme / bozulma denetimi: a'yı b'ye taşıyınca a'nın (b'yi içermeyen) üçgenleri
            bool ok = true;
            for (uint32_t t : incident[a])
            {
                if (!triAlive[t])
                    continue;
                glm::dvec3 p[3];
                bool hasB = false;
                for (int c = 0; c < 3; ++c)
                {
                    const uint32_t q = posOf[tri[t * 3 + c]];
                    if (q == b) hasB = true;
                    p[c] = positions[q];
                }
                if (hasB)
                    continue;
                const glm::dvec3 n0 = faceNormal(p[0], p[1], p[2]);
                for (int c = 0; c < 3; ++c)
                    if (posOf[tri[t * 3 + c]] == a)
                        p[c] = positions[b];
                const glm::dvec3 n1 = faceNormal(p[0], p[1], p[2]);
                const double l0 = glm::length(n0), l1 = glm::length(n1);
                if (l1 < 1e-14 * diag * diag || glm::dot(n0, n1) < 0.25 * l0 * l1 || glm::dot(originalNormal[t], n1) < 0.0)
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;

            // b'nin öznitelik köşeleri (dikişlerde birden fazla)
            attribsB.clear();
            for (uint32_t t : incident[b])
                if (triAlive[t])
                    for (int c = 0; c < 3; ++c)
                        if (posOf[tri[t * 3 + c]] == b)
                            attribsB.push_back(tri[t * 3 + c]);
            std::sort(attribsB.begin(), attribsB.end());
            attribsB.erase(std::unique(attribsB.begin(), attribsB.end()), attribsB.end());
            if (attribsB.empty())
                continue;
            auto bestMatch = [&](uint32_t va) {
                uint32_t best = attribsB[0];
                float bestScore = FLT_MAX;
                const Vertex& A = vertices[va];
                for (uint32_t vb : attribsB)
                {
                    const Vertex& B = vertices[vb];
                    const glm::vec2 duv = A.TexCoords - B.TexCoords;
                    const float score = glm::dot(duv, duv) * 4.0f + (1.0f - glm::dot(A.Normal, B.Normal));
                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = vb;
                    }
                }
                return best;
            };

            // Çöktür: kenarı içeren üçgenler silinir, diğerleri b'ye bağlanır
            for (uint32_t t : incident[a])
            {
                if (!triAlive[t])
                    continue;
                bool hasB = false;
                for (int c = 0; c < 3; ++c)
                    if (posOf[tri[t * 3 + c]] == b) hasB = true;
                if (hasB)
                {
                    triAlive[t] = 0;
                    --alive;
                    continue;
                }
                for (int c = 0; c < 3; ++c)
                    if (posOf[tri[t * 3 + c]] == a)
                        tri[t * 3 + c] = bestMatch(tri[t * 3 + c]);
                incident[b].push_back(t);
            }
            incident[a].clear();
            posAlive[a] = 0;
            Q[b] += Q[a];
            ++version[b];
            worst = std::max(worst, cand.Cost);
            gatherNeighbors(b, scratchB);
            for (uint32_t n : scratchB)
                pushEdge(b, n);
        }

        // 6) Çıktı: yaşayan üçgenler ve kullanılan köşeler (sıkıştırılmış)
        std::vector<uint32_t> remap(vertices.size(), 0xFFFFFFFFu);
        result.Indices.reserve(alive * 3);
        for (size_t t = 0; t < triCount; ++t)
        {
            if (!triAlive[t])
                continue;
            for (int c = 0; c < 3; ++c)
            {
                const uint32_t v = tri[t * 3 + c];
                if (remap[v] == 0xFFFFFFFFu)
                {
                    remap[v] = (uint32_t)result.Vertices.size();
                    result.Vertices.push_back(vertices[v]);
                }
                result.Indices.push_back(remap[v]);
            }
        }
        result.Error = (float)(std::sqrt(worst) / diag);
        return result;
    }
}
