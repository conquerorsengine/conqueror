#include "ClusterMesh.h"
#include "MeshSimplifier.h"
#include "Renderer/RHI/VertexArray.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr float kInf = 1e30f;

        uint32_t Part1By2(uint32_t x)
        {
            x &= 0x3FF;
            x = (x | (x << 16)) & 0x030000FF;
            x = (x | (x << 8)) & 0x0300F00F;
            x = (x | (x << 4)) & 0x030C30C3;
            x = (x | (x << 2)) & 0x09249249;
            return x;
        }

        uint32_t Morton(const glm::vec3& p, const glm::vec3& mn, const glm::vec3& inv)
        {
            const glm::vec3 q = glm::clamp((p - mn) * inv, glm::vec3(0.0f), glm::vec3(1023.0f));
            return Part1By2((uint32_t)q.x) | (Part1By2((uint32_t)q.y) << 1) | (Part1By2((uint32_t)q.z) << 2);
        }

        // Konum kaynaklama anahtarı (aynı konumdaki dikiş köşeleri aynı düğüm)
        struct Welder
        {
            glm::vec3 Min{ 0.0f };
            float Cell = 1e-6f;
            uint64_t Key(const glm::vec3& p) const
            {
                const glm::vec3 q = (p - Min) / Cell;
                return ((uint64_t)std::llround(q.x) & 0x1FFFFF) | (((uint64_t)std::llround(q.y) & 0x1FFFFF) << 21) | (((uint64_t)std::llround(q.z) & 0x1FFFFF) << 42);
            }
        };

        uint64_t EdgeKey(uint64_t a, uint64_t b)
        {
            if (a > b) std::swap(a, b);
            return a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull + (a << 6) + (a >> 2));
        }

        void Sphere(const std::vector<Vertex>& verts, const std::vector<uint32_t>& tris, glm::vec3& center, float& radius)
        {
            glm::vec3 mn(kInf), mx(-kInf);
            for (uint32_t i : tris)
            {
                mn = glm::min(mn, verts[i].Position);
                mx = glm::max(mx, verts[i].Position);
            }
            center = (mn + mx) * 0.5f;
            float r2 = 0.0f;
            for (uint32_t i : tris)
            {
                const glm::vec3 d = verts[i].Position - center;
                r2 = std::max(r2, glm::dot(d, d));
            }
            radius = std::sqrt(r2);
        }

        // Üçgenleri komşuluk üzerinden büyüyen bölgelerle kümelere ayır (en fazla maxTris); her küme üçgen indeks listesi
        std::vector<std::vector<uint32_t>> Clusterize(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx, uint32_t maxTris, const Welder& w)
        {
            const size_t triCount = idx.size() / 3;
            std::vector<std::vector<uint32_t>> clusters;
            if (triCount == 0)
                return clusters;
            std::unordered_map<uint64_t, std::vector<uint32_t>> edgeTris;
            edgeTris.reserve(triCount * 2);
            std::vector<glm::vec3> centroid(triCount);
            glm::vec3 mn(kInf), mx(-kInf);
            for (size_t t = 0; t < triCount; ++t)
            {
                const glm::vec3 a = verts[idx[t * 3]].Position, b = verts[idx[t * 3 + 1]].Position, c = verts[idx[t * 3 + 2]].Position;
                centroid[t] = (a + b + c) / 3.0f;
                mn = glm::min(mn, centroid[t]);
                mx = glm::max(mx, centroid[t]);
                const uint64_t k[3] = { w.Key(a), w.Key(b), w.Key(c) };
                for (int e = 0; e < 3; ++e)
                    edgeTris[EdgeKey(k[e], k[(e + 1) % 3])].push_back((uint32_t)t);
            }
            std::vector<std::vector<uint32_t>> neighbors(triCount);
            for (auto& [key, list] : edgeTris)
                for (size_t i = 0; i < list.size(); ++i)
                    for (size_t j = i + 1; j < list.size(); ++j)
                    {
                        neighbors[list[i]].push_back(list[j]);
                        neighbors[list[j]].push_back(list[i]);
                    }
            const glm::vec3 inv = 1023.0f / glm::max(mx - mn, glm::vec3(1e-9f));
            std::vector<uint32_t> order(triCount);
            std::vector<uint32_t> codes(triCount);
            for (size_t t = 0; t < triCount; ++t)
            {
                order[t] = (uint32_t)t;
                codes[t] = Morton(centroid[t], mn, inv);
            }
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return codes[a] < codes[b]; });
            std::vector<int> owner(triCount, -1);
            using Item = std::pair<float, uint32_t>;
            for (uint32_t seed : order)
            {
                if (owner[seed] >= 0)
                    continue;
                const int id = (int)clusters.size();
                std::vector<uint32_t> cluster;
                std::priority_queue<Item, std::vector<Item>, std::greater<Item>> frontier;
                frontier.push({ 0.0f, seed });
                const glm::vec3 origin = centroid[seed];
                while (!frontier.empty() && cluster.size() < maxTris)
                {
                    const uint32_t t = frontier.top().second;
                    frontier.pop();
                    if (owner[t] >= 0)
                        continue;
                    owner[t] = id;
                    cluster.push_back(t);
                    for (uint32_t n : neighbors[t])
                        if (owner[n] < 0)
                        {
                            const glm::vec3 d = centroid[n] - origin;
                            frontier.push({ glm::dot(d, d), n });
                        }
                }
                clusters.push_back(std::move(cluster));
            }
            // Küçük parçacık kümeleri en çok kenar paylaştığı komşuya kat (sınır uzunluğu ve kilitli köşe sayısı azalır)
            const size_t small = std::max<size_t>(maxTris / 6, 4);
            for (size_t c = 0; c < clusters.size(); ++c)
            {
                if (clusters[c].empty() || clusters[c].size() >= small)
                    continue;
                std::unordered_map<int, int> counts;
                for (uint32_t t : clusters[c])
                    for (uint32_t n : neighbors[t])
                        if (owner[n] >= 0 && owner[n] != (int)c)
                            ++counts[owner[n]];
                int best = -1, bestCount = 0;
                for (auto& [other, count] : counts)
                    if (count > bestCount && clusters[(size_t)other].size() + clusters[c].size() <= maxTris + maxTris / 4)
                    {
                        best = other;
                        bestCount = count;
                    }
                if (best < 0)
                    continue;
                for (uint32_t t : clusters[c])
                    owner[t] = best;
                clusters[(size_t)best].insert(clusters[(size_t)best].end(), clusters[c].begin(), clusters[c].end());
                clusters[c].clear();
            }
            clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [](const std::vector<uint32_t>& c) { return c.empty(); }), clusters.end());
            return clusters;
        }

        struct Work
        {
            std::vector<uint32_t> Tris;       // küresel köşe indeksleri (üçlüler)
            glm::vec3 Center{ 0.0f };
            float Radius = 0.0f;
            float Error = 0.0f;
            glm::vec3 LodCenter{ 0.0f };
            float LodRadius = 0.0f;
            float ParentError = kInf;
            glm::vec3 ParentLodCenter{ 0.0f };
            float ParentLodRadius = 0.0f;
            int Level = 0;
        };
    }

    std::shared_ptr<ClusterMesh> ClusterMesh::Build(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, uint32_t clusterSize)
    {
        auto out = std::make_shared<ClusterMesh>();
        if (vertices.empty() || indices.size() < 3)
            return out;
        clusterSize = std::clamp(clusterSize, 32u, 256u);
        out->m_Vertices = vertices;
        std::vector<Vertex>& verts = out->m_Vertices;
        glm::vec3 mn(kInf), mx(-kInf);
        for (const auto& v : vertices)
        {
            mn = glm::min(mn, v.Position);
            mx = glm::max(mx, v.Position);
        }
        Welder welder;
        welder.Min = mn - glm::vec3(1e-3f);
        welder.Cell = std::max(glm::length(mx - mn), 1e-6f) * 1e-6f;

        // Yaprak kümeler
        std::vector<Work> all;
        {
            auto clusters = Clusterize(verts, indices, clusterSize, welder);
            for (auto& tris : clusters)
            {
                Work wk;
                for (uint32_t t : tris)
                    for (int k = 0; k < 3; ++k)
                        wk.Tris.push_back(indices[(size_t)t * 3 + k]);
                Sphere(verts, wk.Tris, wk.Center, wk.Radius);
                wk.LodCenter = wk.Center;
                wk.LodRadius = wk.Radius;
                all.push_back(std::move(wk));
            }
        }
        out->m_LeafTriangles = (uint32_t)(indices.size() / 3);

        // Üst seviyeler: komşu kümeleri dörtlü grupla, sınırları kilitli yarıya indir, yeniden kümele
        std::vector<uint32_t> current(all.size());
        for (size_t i = 0; i < all.size(); ++i)
            current[i] = (uint32_t)i;
        int level = 0;
        while (current.size() > 1 && level < 24)
        {
            // Küme komşuluğu: paylaşılan (kaynaklanmış) kenar sayısı
            std::unordered_map<uint64_t, std::vector<uint32_t>> edgeClusters;
            for (uint32_t ci : current)
            {
                const auto& tris = all[ci].Tris;
                for (size_t t = 0; t + 2 < tris.size(); t += 3)
                {
                    const uint64_t k[3] = { welder.Key(verts[tris[t]].Position), welder.Key(verts[tris[t + 1]].Position), welder.Key(verts[tris[t + 2]].Position) };
                    for (int e = 0; e < 3; ++e)
                    {
                        auto& list = edgeClusters[EdgeKey(k[e], k[(e + 1) % 3])];
                        if (list.empty() || list.back() != ci)
                            list.push_back(ci);
                    }
                }
            }
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> shared;
            for (auto& [key, list] : edgeClusters)
                for (size_t i = 0; i < list.size(); ++i)
                    for (size_t j = i + 1; j < list.size(); ++j)
                    {
                        ++shared[list[i]][list[j]];
                        ++shared[list[j]][list[i]];
                    }
            // Gruplama (Morton sırasıyla, en çok kenar paylaşan komşular)
            glm::vec3 cmn(kInf), cmx(-kInf);
            for (uint32_t ci : current)
            {
                cmn = glm::min(cmn, all[ci].Center);
                cmx = glm::max(cmx, all[ci].Center);
            }
            const glm::vec3 inv = 1023.0f / glm::max(cmx - cmn, glm::vec3(1e-9f));
            std::vector<uint32_t> order = current;
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return Morton(all[a].Center, cmn, inv) < Morton(all[b].Center, cmn, inv); });
            std::unordered_map<uint32_t, int> groupOf;
            std::vector<std::vector<uint32_t>> groups;
            for (uint32_t ci : order)
            {
                if (groupOf.count(ci))
                    continue;
                std::vector<uint32_t> group{ ci };
                groupOf[ci] = (int)groups.size();
                while (group.size() < 4)
                {
                    uint32_t best = 0;
                    int bestCount = 0;
                    for (uint32_t member : group)
                        for (auto& [n, count] : shared[member])
                            if (!groupOf.count(n) && count > bestCount)
                            {
                                best = n;
                                bestCount = count;
                            }
                    if (bestCount == 0)
                        break;
                    groupOf[best] = (int)groups.size();
                    group.push_back(best);
                }
                groups.push_back(std::move(group));
            }
            // Tek kalan kümeler komşu gruba katılır (en küçük komşu grup, en fazla 6 küme)
            for (size_t g = 0; g < groups.size(); ++g)
            {
                if (groups[g].size() != 1)
                    continue;
                const uint32_t ci = groups[g][0];
                int target = -1;
                size_t targetSize = 1000;
                for (auto& [n, count] : shared[ci])
                {
                    const int ng = groupOf[n];
                    if (ng != (int)g && groups[(size_t)ng].size() < targetSize && groups[(size_t)ng].size() < 6 && !groups[(size_t)ng].empty())
                    {
                        target = ng;
                        targetSize = groups[(size_t)ng].size();
                    }
                }
                if (target < 0)
                    continue;
                groups[(size_t)target].push_back(ci);
                groupOf[ci] = target;
                groups[g].clear();
            }
            groups.erase(std::remove_if(groups.begin(), groups.end(), [](const std::vector<uint32_t>& g) { return g.empty(); }), groups.end());

            std::vector<uint32_t> next;
            for (const auto& group : groups)
            {
                if (group.size() < 2 && current.size() > 1 && groups.size() > 1)
                {
                    // Tek başına kalan küme bu seviyede sadeleşmez: üst seviyeye olduğu gibi taşınır
                    next.push_back(group[0]);
                    continue;
                }
                // Grubun üçgenleri yerel köşe listesine
                std::vector<Vertex> localVerts;
                std::vector<uint32_t> localIdx;
                std::unordered_map<uint32_t, uint32_t> remap;
                float childError = 0.0f;
                glm::vec3 gmn(kInf), gmx(-kInf);
                for (uint32_t ci : group)
                {
                    childError = std::max(childError, all[ci].Error);
                    for (uint32_t v : all[ci].Tris)
                    {
                        auto it = remap.find(v);
                        if (it == remap.end())
                        {
                            it = remap.emplace(v, (uint32_t)localVerts.size()).first;
                            localVerts.push_back(verts[v]);
                            gmn = glm::min(gmn, verts[v].Position);
                            gmx = glm::max(gmx, verts[v].Position);
                        }
                        localIdx.push_back(it->second);
                    }
                }
                const size_t before = localIdx.size() / 3;
                MeshSimplifier::Options opt;
                opt.TargetTriangles = (uint32_t)std::max<size_t>(before / 2, 1);
                opt.LockBorders = true;
                MeshSimplifier::Result r = MeshSimplifier::Simplify(localVerts, localIdx, opt);
                const size_t after = r.Indices.size() / 3;
                if (after == 0 || after > before * 95 / 100)
                    continue;   // daha fazla sadeleşmiyor: bu grubun kümeleri kök kalır
                const float groupDiag = glm::length(gmx - gmn);
                const float groupError = std::max(r.Error * groupDiag, childError) + 1e-9f;
                // Grup küresi: çocuk kürelerinin birleşimi
                glm::vec3 gc(0.0f);
                for (uint32_t ci : group)
                    gc += all[ci].Center;
                gc /= (float)group.size();
                float gr = 0.0f;
                for (uint32_t ci : group)
                    gr = std::max(gr, glm::distance(gc, all[ci].Center) + all[ci].Radius);
                for (uint32_t ci : group)
                {
                    all[ci].ParentError = groupError;
                    all[ci].ParentLodCenter = gc;
                    all[ci].ParentLodRadius = gr;
                }
                // Sadeleşen geometri yeni köşelerle eklenir ve yeniden kümelenir
                const uint32_t base = (uint32_t)verts.size();
                verts.insert(verts.end(), r.Vertices.begin(), r.Vertices.end());
                std::vector<uint32_t> globalIdx(r.Indices.size());
                for (size_t i = 0; i < r.Indices.size(); ++i)
                    globalIdx[i] = base + r.Indices[i];
                std::vector<Vertex> sub(verts.begin() + base, verts.end());
                auto parts = Clusterize(sub, r.Indices, clusterSize, welder);
                for (auto& tris : parts)
                {
                    Work wk;
                    for (uint32_t t : tris)
                        for (int k = 0; k < 3; ++k)
                            wk.Tris.push_back(globalIdx[(size_t)t * 3 + k]);
                    Sphere(verts, wk.Tris, wk.Center, wk.Radius);
                    wk.Error = groupError;
                    wk.LodCenter = gc;
                    wk.LodRadius = gr;
                    wk.Level = level + 1;
                    next.push_back((uint32_t)all.size());
                    all.push_back(std::move(wk));
                }
            }
            if (next.empty() || next.size() >= current.size())
                break;
            current = std::move(next);
            ++level;
        }
        out->m_Levels = level + 1;

        // Birleşik indeks tamponu
        for (const Work& wk : all)
        {
            ClusterInfo ci;
            ci.IndexOffset = (uint32_t)out->m_Indices.size();
            ci.IndexCount = (uint32_t)wk.Tris.size();
            out->m_Indices.insert(out->m_Indices.end(), wk.Tris.begin(), wk.Tris.end());
            ci.Center = wk.Center;
            ci.Radius = wk.Radius;
            ci.LodCenter = wk.LodCenter;
            ci.LodRadius = wk.LodRadius;
            ci.Error = wk.Error;
            ci.ParentError = wk.ParentError;
            ci.ParentLodCenter = wk.ParentLodCenter;
            ci.ParentLodRadius = wk.ParentLodRadius;
            ci.Level = wk.Level;
            out->m_Clusters.push_back(ci);
        }
        return out;
    }

    void ClusterMesh::Upload()
    {
        if (m_VertexArray || m_Vertices.empty())
            return;
        m_VertexArray = VertexArray::Create();
        auto vb = VertexBuffer::Create((float*)m_Vertices.data(), (uint32_t)(m_Vertices.size() * sizeof(Vertex)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float3, "a_Tangent" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float2, "a_TexCoord2" }
        });
        m_VertexArray->AddVertexBuffer(vb);
        m_VertexArray->SetIndexBuffer(IndexBuffer::Create(m_Indices.data(), (uint32_t)m_Indices.size()));
        // CPU kopyası artık gerekmez (seçim yalnızca küme bilgisini kullanır)
        m_Vertices.clear();
        m_Vertices.shrink_to_fit();
        m_Indices.clear();
        m_Indices.shrink_to_fit();
    }

    void ClusterMesh::Select(const glm::vec3& cameraLocal, float worldScale, float pixelScale, float thresholdPixels, bool orthographic,
                             const glm::mat4& world, const glm::vec4* frustum, std::vector<uint32_t>& outClusters) const
    {
        outClusters.clear();
        const float threshold = std::max(thresholdPixels, 0.05f);
        auto projected = [&](float error, const glm::vec3& center, float radius) {
            if (error >= kInf * 0.5f)
                return kInf;
            if (orthographic)
                return error * worldScale * pixelScale;
            const float d = std::max(glm::distance(cameraLocal, center) - radius, 1e-4f);
            return error / d * pixelScale;
        };
        for (uint32_t i = 0; i < (uint32_t)m_Clusters.size(); ++i)
        {
            const ClusterInfo& c = m_Clusters[i];
            if (projected(c.Error, c.LodCenter, c.LodRadius) > threshold)
                continue;   // bu küme yeterince ayrıntılı değil (çocukları çizilir)
            if (projected(c.ParentError, c.ParentLodCenter, c.ParentLodRadius) <= threshold)
                continue;   // üst seviye yeterli
            if (frustum)
            {
                const glm::vec3 wc = glm::vec3(world * glm::vec4(c.Center, 1.0f));
                const float wr = c.Radius * worldScale;
                bool visible = true;
                for (int p = 0; p < 6 && visible; ++p)
                    visible = glm::dot(glm::vec3(frustum[p]), wc) + frustum[p].w >= -wr;
                if (!visible)
                    continue;
            }
            outClusters.push_back(i);
        }
    }

    uint32_t ClusterMesh::Draw(const std::vector<uint32_t>& clusters) const
    {
        if (!m_VertexArray || clusters.empty())
            return 0;
        m_VertexArray->Bind();
        static thread_local std::vector<GLsizei> s_Counts;
        static thread_local std::vector<const void*> s_Offsets;
        s_Counts.clear();
        s_Offsets.clear();
        uint32_t triangles = 0;
        for (uint32_t i : clusters)
        {
            const ClusterInfo& c = m_Clusters[i];
            s_Counts.push_back((GLsizei)c.IndexCount);
            s_Offsets.push_back((const void*)(uintptr_t)(c.IndexOffset * sizeof(uint32_t)));
            triangles += c.IndexCount / 3;
        }
        if (glMultiDrawElements)
            glMultiDrawElements(GL_TRIANGLES, s_Counts.data(), GL_UNSIGNED_INT, s_Offsets.data(), (GLsizei)s_Counts.size());
        else
            for (size_t i = 0; i < s_Counts.size(); ++i)
                glDrawElements(GL_TRIANGLES, s_Counts[i], GL_UNSIGNED_INT, s_Offsets[i]);
        return triangles;
    }
}
