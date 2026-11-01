#include "NavMesh.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Terrain/TerrainComponent.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Logging/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr uint8_t kNullArea = 0xFF;
        constexpr int kMaxHeight = 0x7FFFFFFF;
        // Komşu yönleri: 0 +x, 1 +z, 2 -x, 3 -z
        constexpr int kDirX[4] = { 1, 0, -1, 0 };
        constexpr int kDirZ[4] = { 0, 1, 0, -1 };

        struct Span
        {
            int Min, Max;
            uint8_t Area;
            float Top;      // gerçek üst yükseklik (zemin hücresi bunu kullanır; Max yuvarlanmış)
        };

        bool Passable(uint8_t area, uint32_t mask, const std::array<float, kNavMaxAreas>& cost)
        {
            return area < kNavMaxAreas && area != NavArea_NotWalkable && ((mask >> area) & 1u) && cost[area] > 0.0f;
        }
    }

    // ===================================================================== NavMesh

    NavMesh::NavMesh()
    {
        m_AreaCost.fill(1.0f);
        m_AreaCost[NavArea_NotWalkable] = 0.0f;
        m_AreaCost[NavArea_Jump] = 2.0f;
    }

    void NavMesh::Clear()
    {
        m_Cells.clear();
        m_ColumnCells.clear();
        m_ColumnStart.clear();
        m_Links.clear();
        m_LinksFrom.clear();
        m_Blocked.clear();
        m_LegacyTriangles.clear();
        m_Width = m_Depth = 0;
        ++m_Version;
    }

    void NavMesh::SetGrid(const glm::vec3& origin, int width, int depth, const NavBuildSettings& settings)
    {
        m_Origin = origin;
        m_Width = width;
        m_Depth = depth;
        m_Settings = settings;
        m_ColumnCells.assign((size_t)width * (size_t)depth, {});
    }

    int NavMesh::AddCell(int x, int z, const Cell& cell)
    {
        int index = (int)m_Cells.size();
        Cell c = cell;
        c.Column = z * m_Width + x;
        m_Cells.push_back(c);
        m_ColumnCells[(size_t)c.Column].push_back(index);
        return index;
    }

    void NavMesh::FinishColumns()
    {
        // Hücreler sütun sırasıyla eklendi: başlangıç dizini tablosu
        m_ColumnStart.assign((size_t)m_Width * (size_t)m_Depth + 1, 0);
        int running = 0;
        for (size_t col = 0; col < m_ColumnCells.size(); ++col)
        {
            m_ColumnStart[col] = running;
            running += (int)m_ColumnCells[col].size();
        }
        m_ColumnStart.back() = running;
        m_ColumnCells.clear();
        m_ColumnCells.shrink_to_fit();
        m_LinksFrom.assign(m_Cells.size(), {});
        m_Blocked.assign(m_Cells.size(), 0);
        ++m_Version;
    }

    int NavMesh::ColumnStart(int x, int z) const
    {
        if (x < 0 || z < 0 || x >= m_Width || z >= m_Depth || m_ColumnStart.empty())
            return 0;
        return m_ColumnStart[(size_t)(z * m_Width + x)];
    }

    int NavMesh::ColumnCount(int x, int z) const
    {
        if (x < 0 || z < 0 || x >= m_Width || z >= m_Depth || m_ColumnStart.empty())
            return 0;
        const size_t col = (size_t)(z * m_Width + x);
        return m_ColumnStart[col + 1] - m_ColumnStart[col];
    }

    glm::vec3 NavMesh::CellPosition(int cell) const
    {
        const Cell& c = m_Cells[(size_t)cell];
        const int x = c.Column % std::max(m_Width, 1), z = c.Column / std::max(m_Width, 1);
        return glm::vec3(m_Origin.x + ((float)x + 0.5f) * m_Settings.CellSize, c.Y, m_Origin.z + ((float)z + 0.5f) * m_Settings.CellSize);
    }

    int NavMesh::FindNearestCell(const glm::vec3& p, float maxDistance) const
    {
        if (m_Cells.empty())
            return -1;
        const float cs = m_Settings.CellSize;
        const int cx = (int)std::floor((p.x - m_Origin.x) / cs), cz = (int)std::floor((p.z - m_Origin.z) / cs);
        const int r = std::max(1, (int)std::ceil(maxDistance / cs));
        int best = -1;
        float bestD = maxDistance * maxDistance;
        for (int ring = 0; ring <= r; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                        continue;
                    const int x = cx + dx, z = cz + dz;
                    const int start = ColumnStart(x, z), count = ColumnCount(x, z);
                    for (int i = start; i < start + count; ++i)
                    {
                        const Cell& c = m_Cells[(size_t)i];
                        if (c.Area >= kNavMaxAreas || c.Area == NavArea_NotWalkable || IsBlocked(i))
                            continue;
                        glm::vec3 d = CellPosition(i) - p;
                        // Dikey fark (kat seçimi) ağırlıklı: yakın kat tercih edilir
                        float dist2 = d.x * d.x + d.z * d.z + d.y * d.y * 4.0f;
                        if (dist2 < bestD) { bestD = dist2; best = i; }
                    }
                }
            // Bulunan en yakın hücre bu halkadan daha yakınsa dur
            if (best >= 0 && (float)(ring * ring) * cs * cs > bestD)
                break;
        }
        return best;
    }

    bool NavMesh::SamplePosition(const glm::vec3& position, float maxDistance, glm::vec3& outPosition) const
    {
        int c = FindNearestCell(position, maxDistance);
        if (c < 0)
            return false;
        outPosition = CellPosition(c);
        // Hücre içindeki gerçek x/z korunur (sınır içinde)
        const float cs = m_Settings.CellSize;
        glm::vec3 center = outPosition;
        outPosition.x = std::clamp(position.x, center.x - cs * 0.5f, center.x + cs * 0.5f);
        outPosition.z = std::clamp(position.z, center.z - cs * 0.5f, center.z + cs * 0.5f);
        // Yükseklik komşu hücrelerle doğrusal yumuşatılır (rampalarda basamaklanma olmaz)
        const float fx = (outPosition.x - center.x) / cs, fz = (outPosition.z - center.z) / cs;
        const Cell& cell = m_Cells[(size_t)c];
        const int nx = cell.Neighbors[fx >= 0.0f ? 0 : 2], nz = cell.Neighbors[fz >= 0.0f ? 1 : 3];
        const float yx = nx >= 0 ? m_Cells[(size_t)nx].Y : cell.Y, yz = nz >= 0 ? m_Cells[(size_t)nz].Y : cell.Y;
        outPosition.y = cell.Y + std::fabs(fx) * (yx - cell.Y) + std::fabs(fz) * (yz - cell.Y);
        return true;
    }

    bool NavMesh::GetHeight(const glm::vec3& position, float& outY) const
    {
        int c = FindNearestCell(position, m_Settings.CellSize * 2.0f);
        if (c < 0)
            return false;
        outY = m_Cells[(size_t)c].Y;
        return true;
    }

    int NavMesh::NeighborDiagonal(int cell, int dirA, int dirB) const
    {
        // İki yoldan da (A sonra B, B sonra A) aynı köşe hücresine varılmalı (köşe kesme yok)
        int a = m_Cells[(size_t)cell].Neighbors[dirA];
        int b = m_Cells[(size_t)cell].Neighbors[dirB];
        if (a < 0 || b < 0)
            return -1;
        int ab = m_Cells[(size_t)a].Neighbors[dirB];
        int ba = m_Cells[(size_t)b].Neighbors[dirA];
        return ab >= 0 && ab == ba ? ab : -1;
    }

    bool NavMesh::LineOfSight(int fromCell, int toCell, uint32_t mask) const
    {
        const Cell& a = m_Cells[(size_t)fromCell];
        const Cell& b = m_Cells[(size_t)toCell];
        const uint8_t area = a.Area;
        int x0 = a.Column % m_Width, z0 = a.Column / m_Width;
        const int x1 = b.Column % m_Width, z1 = b.Column / m_Width;
        const int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0);
        const int sx = x1 > x0 ? 1 : -1, sz = z1 > z0 ? 1 : -1;
        const int dirX = sx > 0 ? 0 : 2, dirZ = sz > 0 ? 1 : 3;
        int cur = fromCell;
        // Hücre merkezleri arası doğru: tam sayı DDA (her adım bir komşu bağlantısıyla)
        int err = dx - dz;
        for (int steps = dx + dz; steps > 0; --steps)
        {
            const int e2 = 2 * err;
            int next;
            if (e2 > -dz && e2 < dx)
            {
                // Köşeden geçiş: iki yol da açık olmalı
                next = NeighborDiagonal(cur, dirX, dirZ);
                err += -dz + dx;
                --steps;
            }
            else if (e2 > -dz)
            {
                next = m_Cells[(size_t)cur].Neighbors[dirX];
                err -= dz;
            }
            else
            {
                next = m_Cells[(size_t)cur].Neighbors[dirZ];
                err += dx;
            }
            if (next < 0)
                return false;
            const Cell& n = m_Cells[(size_t)next];
            if (n.Area != area || !Passable(n.Area, mask, m_AreaCost) || IsBlocked(next))
                return false;
            cur = next;
        }
        return cur == toCell;
    }

    bool NavMesh::FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<NavPathPoint>& outPath, uint32_t areaMask, bool* outComplete) const
    {
        outPath.clear();
        if (outComplete) *outComplete = false;
        const int startCell = FindNearestCell(start, 3.0f);
        const int goalCell = FindNearestCell(end, 6.0f);
        if (startCell < 0 || goalCell < 0)
            return false;

        // A* (yeniden kullanılan arama tamponları; yalnızca dokunulan hücreler sıfırlanır)
        static thread_local std::vector<float> g;
        static thread_local std::vector<int> parent, parentLink;
        static thread_local std::vector<uint8_t> state;   // 0 yok, 1 açık, 2 kapalı
        static thread_local std::vector<int> touched;
        const size_t n = m_Cells.size();
        if (g.size() < n)
        {
            g.assign(n, std::numeric_limits<float>::infinity());
            parent.assign(n, -1);
            parentLink.assign(n, -1);
            state.assign(n, 0);
        }
        for (int t : touched)
        {
            g[(size_t)t] = std::numeric_limits<float>::infinity();
            parent[(size_t)t] = -1;
            parentLink[(size_t)t] = -1;
            state[(size_t)t] = 0;
        }
        touched.clear();

        const glm::vec3 goalPos = CellPosition(goalCell);
        float minCost = 1.0f;
        for (int a = 0; a < kNavMaxAreas; ++a)
            if (a != NavArea_NotWalkable && m_AreaCost[(size_t)a] > 0.0f)
                minCost = std::min(minCost, m_AreaCost[(size_t)a]);
        auto heuristic = [&](int c) { return glm::length(CellPosition(c) - goalPos) * minCost; };

        using Node = std::pair<float, int>;
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        g[(size_t)startCell] = 0.0f;
        state[(size_t)startCell] = 1;
        touched.push_back(startCell);
        open.push({ heuristic(startCell), startCell });
        int bestCell = startCell;
        float bestH = heuristic(startCell);
        int expanded = 0;
        const int maxExpanded = 2000000;

        auto relax = [&](int from, int to, float cost, int link) {
            const float ng = g[(size_t)from] + cost;
            if (ng < g[(size_t)to])
            {
                if (state[(size_t)to] == 0)
                    touched.push_back(to);
                g[(size_t)to] = ng;
                parent[(size_t)to] = from;
                parentLink[(size_t)to] = link;
                state[(size_t)to] = 1;
                open.push({ ng + heuristic(to), to });
            }
        };

        while (!open.empty() && expanded < maxExpanded)
        {
            auto [f, cur] = open.top();
            open.pop();
            if (state[(size_t)cur] == 2)
                continue;
            state[(size_t)cur] = 2;
            ++expanded;
            const float h = f - g[(size_t)cur];
            if (h < bestH) { bestH = h; bestCell = cur; }
            if (cur == goalCell)
            {
                bestCell = goalCell;
                break;
            }
            const glm::vec3 cp = CellPosition(cur);
            const Cell& c = m_Cells[(size_t)cur];
            for (int d = 0; d < 4; ++d)
            {
                const int nb = c.Neighbors[d];
                if (nb < 0 || state[(size_t)nb] == 2 || IsBlocked(nb) || !Passable(m_Cells[(size_t)nb].Area, areaMask, m_AreaCost))
                    continue;
                relax(cur, nb, glm::length(CellPosition(nb) - cp) * m_AreaCost[m_Cells[(size_t)nb].Area], -1);
            }
            for (int d = 0; d < 4; ++d)
            {
                const int nb = NeighborDiagonal(cur, d, (d + 1) % 4);
                if (nb < 0 || state[(size_t)nb] == 2 || IsBlocked(nb) || !Passable(m_Cells[(size_t)nb].Area, areaMask, m_AreaCost))
                    continue;
                // Köşe hücreleri de geçilebilir olmalı
                const int a = c.Neighbors[d], b = c.Neighbors[(d + 1) % 4];
                if (IsBlocked(a) || IsBlocked(b) || !Passable(m_Cells[(size_t)a].Area, areaMask, m_AreaCost) || !Passable(m_Cells[(size_t)b].Area, areaMask, m_AreaCost))
                    continue;
                relax(cur, nb, glm::length(CellPosition(nb) - cp) * m_AreaCost[m_Cells[(size_t)nb].Area], -1);
            }
            if (!m_LinksFrom.empty())
                for (int entry : m_LinksFrom[(size_t)cur])
                {
                    const Link& l = m_Links[(size_t)(entry / 2)];
                    if (!((areaMask >> l.Area) & 1u))
                        continue;
                    const int to = (entry & 1) ? l.StartCell : l.EndCell;
                    if (to < 0 || state[(size_t)to] == 2 || IsBlocked(to))
                        continue;
                    relax(cur, to, l.Cost, entry);
                }
        }

        const bool complete = bestCell == goalCell;
        if (outComplete) *outComplete = complete;

        // Hücre zinciri (+ hangi adım bağlantıyla)
        std::vector<int> cells, links;
        for (int c = bestCell; c >= 0; c = parent[(size_t)c])
        {
            cells.push_back(c);
            links.push_back(parentLink[(size_t)c]);
        }
        std::reverse(cells.begin(), cells.end());
        std::reverse(links.begin(), links.end());

        // Görüş hattı yumuşatma (bağlantılarda bölünür)
        glm::vec3 startPos;
        SamplePosition(start, 3.0f, startPos);
        outPath.push_back({ startPos, -1 });
        size_t segStart = 0;
        for (size_t i = 1; i <= cells.size(); ++i)
        {
            const bool linkNext = i < cells.size() && links[i] >= 0;
            if (i == cells.size() || linkNext)
            {
                // Bölüm: cells[segStart .. i-1]
                size_t anchor = segStart;
                while (anchor < i - 1)
                {
                    size_t j = i - 1;
                    while (j > anchor + 1 && !LineOfSight(cells[anchor], cells[j], areaMask))
                        --j;
                    outPath.push_back({ CellPosition(cells[j]), -1 });
                    anchor = j;
                }
                if (linkNext)
                {
                    const Link& l = m_Links[(size_t)(links[i] / 2)];
                    const bool reverse = links[i] & 1;
                    // Bağlantının başına git, sonra bağlantı ile karşıya geç
                    outPath.push_back({ reverse ? l.End : l.Start, -1 });
                    outPath.push_back({ reverse ? l.Start : l.End, links[i] / 2 });
                    segStart = i;
                }
            }
        }
        // Son nokta: gerçek hedef (tam yol ise hücresinin içinde)
        if (complete)
        {
            glm::vec3 endPos;
            if (SamplePosition(end, 6.0f, endPos))
            {
                if (outPath.size() > 1 && glm::length(outPath.back().Position - endPos) < m_Settings.CellSize * 1.5f && outPath.back().LinkIndex < 0)
                    outPath.back().Position = endPos;
                else
                    outPath.push_back({ endPos, -1 });
            }
        }
        // Yinelenen noktaları temizle
        std::vector<NavPathPoint> cleaned;
        for (auto& p : outPath)
            if (cleaned.empty() || p.LinkIndex >= 0 || glm::length(p.Position - cleaned.back().Position) > 1e-3f)
                cleaned.push_back(p);
        outPath.swap(cleaned);
        return outPath.size() >= 1;
    }

    bool NavMesh::Raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& outHit, uint32_t areaMask) const
    {
        int cur = FindNearestCell(from, 2.0f);
        if (cur < 0)
        {
            outHit = from;
            return false;
        }
        const float cs = m_Settings.CellSize;
        glm::vec2 a(from.x, from.z), b(to.x, to.z);
        const float len = glm::length(b - a);
        const int steps = std::max(1, (int)std::ceil(len / (cs * 0.5f)));
        glm::vec3 last = CellPosition(cur);
        for (int s = 1; s <= steps; ++s)
        {
            glm::vec2 p = a + (b - a) * ((float)s / (float)steps);
            const int x = (int)std::floor((p.x - m_Origin.x) / cs), z = (int)std::floor((p.y - m_Origin.z) / cs);
            const Cell& c = m_Cells[(size_t)cur];
            const int cxp = c.Column % m_Width, czp = c.Column / m_Width;
            if (x == cxp && z == czp)
                continue;
            int next = -1;
            if (std::abs(x - cxp) + std::abs(z - czp) == 1)
            {
                for (int d = 0; d < 4; ++d)
                    if (kDirX[d] == x - cxp && kDirZ[d] == z - czp) next = c.Neighbors[d];
            }
            else
            {
                const int dX = x > cxp ? 0 : 2, dZ = z > czp ? 1 : 3;
                next = NeighborDiagonal(cur, dX, dZ);
            }
            if (next < 0 || IsBlocked(next) || !Passable(m_Cells[(size_t)next].Area, areaMask, m_AreaCost))
            {
                outHit = glm::vec3(last.x, last.y, last.z);
                return false;
            }
            cur = next;
            last = glm::vec3(p.x, m_Cells[(size_t)cur].Y, p.y);
        }
        outHit = last;
        return true;
    }

    void NavMesh::ClearBlocked()
    {
        if (std::any_of(m_Blocked.begin(), m_Blocked.end(), [](uint8_t b) { return b != 0; }))
        {
            std::fill(m_Blocked.begin(), m_Blocked.end(), 0);
            ++m_BlockedVersion;
        }
    }

    void NavMesh::BlockBox(const glm::mat4& transform, const glm::vec3& center, const glm::vec3& size, float expand)
    {
        if (m_Cells.empty())
            return;
        const glm::mat4 inv = glm::inverse(transform);
        const glm::vec3 half = glm::abs(size) * 0.5f;
        // Dünya AABB'si
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 corner = center + glm::vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
            glm::vec3 w = glm::vec3(transform * glm::vec4(corner, 1.0f));
            mn = glm::min(mn, w);
            mx = glm::max(mx, w);
        }
        mn -= glm::vec3(expand, 0.0f, expand);
        mx += glm::vec3(expand, 0.0f, expand);
        const float cs = m_Settings.CellSize;
        const glm::vec3 scale(glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2])));
        const glm::vec3 localExpand = glm::vec3(expand) / glm::max(scale, glm::vec3(1e-4f));
        bool changed = false;
        for (int z = std::max(0, (int)std::floor((mn.z - m_Origin.z) / cs)); z <= std::min(m_Depth - 1, (int)std::floor((mx.z - m_Origin.z) / cs)); ++z)
            for (int x = std::max(0, (int)std::floor((mn.x - m_Origin.x) / cs)); x <= std::min(m_Width - 1, (int)std::floor((mx.x - m_Origin.x) / cs)); ++x)
                for (int i = ColumnStart(x, z), e = i + ColumnCount(x, z); i < e; ++i)
                {
                    glm::vec3 p = glm::vec3(inv * glm::vec4(CellPosition(i), 1.0f)) - center;
                    // Zemin kutunun altında birazdan (ajan boyu) üstüne kadar
                    if (std::fabs(p.x) <= half.x + localExpand.x && std::fabs(p.z) <= half.z + localExpand.z &&
                        p.y >= -half.y - m_Settings.AgentHeight / std::max(scale.y, 1e-4f) && p.y <= half.y + 0.05f)
                    {
                        if (!m_Blocked[(size_t)i]) changed = true;
                        m_Blocked[(size_t)i] = 1;
                    }
                }
        if (changed)
            ++m_BlockedVersion;
    }

    void NavMesh::AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, int areaType)
    {
        NavTriangle t;
        t.Vertices[0] = v0; t.Vertices[1] = v1; t.Vertices[2] = v2;
        t.Normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        t.Center = (v0 + v1 + v2) / 3.0f;
        t.Area = glm::length(glm::cross(v1 - v0, v2 - v0)) * 0.5f;
        t.AreaType = areaType;
        m_LegacyTriangles.push_back(t);
    }

    int NavMesh::FindClosestTriangle(const glm::vec3& position) const
    {
        return FindNearestCell(position, 3.0f);
    }

    // ===================================================================== Pişirme

    namespace
    {
        struct SourceTri
        {
            glm::vec3 V[3];
            uint8_t Area;
        };

        // Sutherland-Hodgman: "axis" ekseninde x = value ile ikiye böl
        void DividePoly(const std::vector<glm::vec3>& in, std::vector<glm::vec3>& below, std::vector<glm::vec3>& above, float value, int axis)
        {
            below.clear();
            above.clear();
            const size_t n = in.size();
            for (size_t i = 0, j = n - 1; i < n; j = i, ++i)
            {
                const float di = value - in[i][axis], dj = value - in[j][axis];
                const bool ina = dj >= 0.0f, inb = di >= 0.0f;
                if (ina != inb)
                {
                    const float s = dj / (dj - di);
                    glm::vec3 v = in[j] + (in[i] - in[j]) * s;
                    below.push_back(v);
                    above.push_back(v);
                    if (di > 0.0f) below.push_back(in[i]);
                    else if (di < 0.0f) above.push_back(in[i]);
                }
                else
                {
                    if (di >= 0.0f)
                    {
                        below.push_back(in[i]);
                        if (di != 0.0f) continue;
                    }
                    above.push_back(in[i]);
                }
            }
        }

        void AddSpan(std::vector<Span>& column, int smin, int smax, uint8_t area, int mergeThreshold, float top)
        {
            Span s{ smin, smax, area, top };
            for (size_t i = 0; i < column.size();)
            {
                Span& c = column[i];
                if (c.Min > s.Max || c.Max < s.Min)
                {
                    ++i;
                    continue;
                }
                // Çakışma: birleştir; üstleri tırmanma eşiğindeyse yürünebilir alan önceliklidir
                if (std::abs(s.Max - c.Max) <= mergeThreshold)
                {
                    if (s.Area == kNullArea) s.Area = c.Area;
                    else if (c.Area != kNullArea) s.Area = std::max(s.Area, c.Area);
                }
                else if (c.Max > s.Max)
                    s.Area = c.Area;
                if (c.Max > s.Max || (c.Max == s.Max && c.Top > s.Top))
                    s.Top = c.Top;
                s.Min = std::min(s.Min, c.Min);
                s.Max = std::max(s.Max, c.Max);
                column.erase(column.begin() + (long)i);
            }
            auto pos = std::lower_bound(column.begin(), column.end(), s, [](const Span& a, const Span& b) { return a.Min < b.Min; });
            column.insert(pos, s);
        }

        uint64_t HashMix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
        uint64_t HashFloat(uint64_t h, float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            return HashMix(h, bits);
        }

        // Pişirmeye dahil edilecek varlıklar ve alanları
        struct BakeSource
        {
            entt::entity E;
            uint8_t Area;
        };

        bool CollectSources(Scene* scene, std::vector<BakeSource>& out, NavBuildSettings& settings)
        {
            auto& reg = scene->m_Registry;
            auto surfaces = reg.view<NavMeshSurfaceComponent>();
            entt::entity firstSurface = entt::null;
            for (auto s : surfaces)
                if (scene->IsEntityActive(s)) { firstSurface = s; break; }
            if (firstSurface == entt::null)
                return false;
            const auto& surface = reg.get<NavMeshSurfaceComponent>(firstSurface);
            settings.AgentRadius = std::max(0.0f, surface.AgentRadius);
            settings.AgentHeight = std::max(0.1f, surface.AgentHeight);
            settings.MaxSlope = std::clamp(surface.MaxSlope, 0.0f, 89.0f);
            settings.StepHeight = std::max(0.0f, surface.StepHeight);
            settings.CellSize = surface.OverrideVoxelSize ? std::max(0.02f, surface.VoxelSize) : std::max(0.05f, settings.AgentRadius / 3.0f);
            settings.CellHeight = std::max(0.02f, settings.CellSize * 0.5f);

            // Kapsam: Children modundaki yüzeylerin alt ağaçları, All modunda tüm sahne
            bool all = false;
            std::vector<entt::entity> roots;
            for (auto s : surfaces)
            {
                if (!scene->IsEntityActive(s)) continue;
                if (reg.get<NavMeshSurfaceComponent>(s).CollectObjects == NavCollectObjects::All) all = true;
                else roots.push_back(s);
            }
            auto inScope = [&](Entity e) {
                if (all) return true;
                for (Entity p = e; p; p = p.GetParent())
                    for (auto r : roots)
                        if ((entt::entity)p == r) return true;
                return false;
            };
            const uint8_t defaultArea = (uint8_t)std::clamp(surface.AreaType, 0, kNavMaxAreas - 1);

            auto consider = [&](entt::entity id) {
                Entity e{ id, scene };
                if (!scene->IsEntityActive(id) || !inScope(e))
                    return;
                if (reg.all_of<NavMeshAgentComponent>(id))
                    return;
                if (auto* rb = reg.try_get<RigidbodyComponent>(id); rb && rb->Type == RigidbodyComponent::BodyType::Dynamic)
                    return;
                uint8_t area = defaultArea;
                // En yakın Modifier (kendisi ya da alt öğelere uygulayan üstü)
                for (Entity p = e; p; p = p.GetParent())
                {
                    if (auto* m = reg.try_get<NavMeshModifierComponent>((entt::entity)p))
                    {
                        if ((entt::entity)p != id && !m->ApplyToChildren)
                            continue;
                        if (m->IgnoreFromBuild)
                            return;
                        if (m->OverrideArea)
                            area = (uint8_t)std::clamp(m->Area, 0, kNavMaxAreas - 1);
                        break;
                    }
                }
                out.push_back({ id, area });
            };
            std::vector<entt::entity> ids;
            for (auto e : reg.view<MeshRendererComponent>()) ids.push_back(e);
            for (auto e : reg.view<ModelComponent>()) ids.push_back(e);
            for (auto e : reg.view<TerrainComponent>()) ids.push_back(e);
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            for (auto e : ids)
                consider(e);
            return true;
        }

        void AppendMesh(const std::shared_ptr<Mesh>& mesh, const glm::mat4& world, uint8_t area, std::vector<SourceTri>& out)
        {
            if (!mesh)
                return;
            const auto& v = mesh->GetVertices();
            const auto& idx = mesh->GetIndices();
            for (size_t i = 0; i + 2 < idx.size(); i += 3)
            {
                if (idx[i] >= v.size() || idx[i + 1] >= v.size() || idx[i + 2] >= v.size())
                    continue;
                SourceTri t;
                t.V[0] = glm::vec3(world * glm::vec4(v[idx[i]].Position, 1.0f));
                t.V[1] = glm::vec3(world * glm::vec4(v[idx[i + 1]].Position, 1.0f));
                t.V[2] = glm::vec3(world * glm::vec4(v[idx[i + 2]].Position, 1.0f));
                t.Area = area;
                out.push_back(t);
            }
        }
    }

    uint64_t NavMeshBuilder::ComputeSceneSignature(Scene* scene)
    {
        uint64_t h = 1469598103934665603ull;
        if (!scene)
            return h;
        NavBuildSettings settings;
        std::vector<BakeSource> sources;
        if (!CollectSources(scene, sources, settings))
            return h;
        auto& reg = scene->m_Registry;
        h = HashFloat(h, settings.AgentRadius); h = HashFloat(h, settings.AgentHeight); h = HashFloat(h, settings.MaxSlope);
        h = HashFloat(h, settings.StepHeight); h = HashFloat(h, settings.CellSize);
        for (auto& s : sources)
        {
            h = HashMix(h, (uint64_t)(uint32_t)s.E + 7);
            h = HashMix(h, s.Area);
            const glm::mat4& w = reg.get<TransformComponent>(s.E).WorldTransform;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    h = HashFloat(h, w[c][r]);
            if (auto* mr = reg.try_get<MeshRendererComponent>(s.E)) h = HashMix(h, (uint64_t)mr->Type + 11);
            if (auto* mc = reg.try_get<ModelComponent>(s.E)) h = HashMix(h, (uint64_t)(uintptr_t)mc->ModelData.get());
            if (auto* t = reg.try_get<TerrainComponent>(s.E))
            {
                h = HashFloat(h, t->Width); h = HashFloat(h, t->Length); h = HashFloat(h, t->Height); h = HashMix(h, (uint64_t)t->Resolution);
                for (size_t i = 0; i < t->Heights.size(); i += 97) h = HashFloat(h, t->Heights[i]);
            }
        }
        for (auto e : reg.view<NavMeshModifierVolumeComponent>())
        {
            auto& v = reg.get<NavMeshModifierVolumeComponent>(e);
            h = HashMix(h, (uint64_t)v.Area + 13);
            h = HashFloat(h, v.Size.x); h = HashFloat(h, v.Size.y); h = HashFloat(h, v.Size.z);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            for (int c = 0; c < 4; ++c) h = HashFloat(h, w[3][c] + w[0][c] + w[2][c]);
        }
        for (auto e : reg.view<NavMeshLinkComponent>())
        {
            auto& l = reg.get<NavMeshLinkComponent>(e);
            h = HashFloat(h, l.StartPoint.x + l.StartPoint.y * 3.0f + l.StartPoint.z * 7.0f);
            h = HashFloat(h, l.EndPoint.x + l.EndPoint.y * 3.0f + l.EndPoint.z * 7.0f);
            h = HashMix(h, (uint64_t)l.Area * 2 + l.Bidirectional);
            h = HashFloat(h, l.CostModifier);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            for (int c = 0; c < 3; ++c) h = HashFloat(h, w[3][c]);
        }
        return h;
    }

    void NavMeshBuilder::Bake(Scene* scene, NavMesh& nav)
    {
        auto t0 = std::chrono::steady_clock::now();
        nav.Clear();
        if (!scene)
            return;
        NavBuildSettings settings;
        std::vector<BakeSource> sources;
        if (!CollectSources(scene, sources, settings))
            return;
        auto& reg = scene->m_Registry;

        // ---- 1. Kaynak üçgenler (dünya uzayı)
        std::vector<SourceTri> tris;
        for (auto& s : sources)
        {
            const glm::mat4& world = reg.get<TransformComponent>(s.E).WorldTransform;
            if (auto* mr = reg.try_get<MeshRendererComponent>(s.E))
            {
                std::shared_ptr<Mesh> mesh = mr->Type == MeshType::Sphere ? Renderer3D::GetSphereMesh() : mr->Type == MeshType::Plane ? Renderer3D::GetPlaneMesh()
                                           : mr->Type == MeshType::Cylinder ? Renderer3D::GetCylinderMesh() : Renderer3D::GetCubeMesh();
                AppendMesh(mesh, world, s.Area, tris);
            }
            if (auto* mc = reg.try_get<ModelComponent>(s.E); mc && mc->ModelData)
                for (auto& m : mc->ModelData->Meshes)
                    AppendMesh(m, world, s.Area, tris);
            if (auto* t = reg.try_get<TerrainComponent>(s.E); t && t->Resolution > 1 && (int)t->Heights.size() >= t->Resolution * t->Resolution)
            {
                const int r = t->Resolution;
                auto vert = [&](int x, int z) {
                    glm::vec3 local((float)x / (float)(r - 1) * t->Width, t->Heights[(size_t)z * (size_t)r + (size_t)x] * t->Height, (float)z / (float)(r - 1) * t->Length);
                    return glm::vec3(world * glm::vec4(local, 1.0f));
                };
                for (int z = 0; z + 1 < r; ++z)
                    for (int x = 0; x + 1 < r; ++x)
                    {
                        glm::vec3 a = vert(x, z), b = vert(x + 1, z), c = vert(x + 1, z + 1), d = vert(x, z + 1);
                        tris.push_back({ { a, d, c }, s.Area });
                        tris.push_back({ { a, c, b }, s.Area });
                    }
            }
        }
        if (tris.empty())
        {
            CQ_CORE_WARN("[NavMesh] Bake: no geometry inside the NavMesh Surface scope");
            return;
        }

        // ---- 2. Izgara
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for (auto& t : tris)
            for (auto& v : t.V) { bmin = glm::min(bmin, v); bmax = glm::max(bmax, v); }
        float cs = settings.CellSize;
        const float pad = settings.AgentRadius + cs * 2.0f;
        bmin -= glm::vec3(pad, 0.0f, pad);
        bmax += glm::vec3(pad, settings.AgentHeight, pad);
        while (((bmax.x - bmin.x) / cs) * ((bmax.z - bmin.z) / cs) > 6.0e6f)
            cs *= 1.25f;   // çok büyük dünya: hücre büyür
        settings.CellSize = cs;
        settings.CellHeight = std::max(settings.CellHeight, cs * 0.25f);
        const float ch = settings.CellHeight;
        const int width = std::max(1, (int)std::ceil((bmax.x - bmin.x) / cs));
        const int depth = std::max(1, (int)std::ceil((bmax.z - bmin.z) / cs));
        const int walkableHeight = std::max(1, (int)std::ceil(settings.AgentHeight / ch));
        const int walkableClimb = std::max(0, (int)std::floor(settings.StepHeight / ch));
        const float walkableCos = std::cos(glm::radians(settings.MaxSlope));

        // ---- 3. Rasterleştirme (Recast)
        std::vector<std::vector<Span>> columns((size_t)width * (size_t)depth);
        std::vector<glm::vec3> poly, row, rest, cell, rest2;
        for (auto& t : tris)
        {
            glm::vec3 n = glm::cross(t.V[1] - t.V[0], t.V[2] - t.V[0]);
            float nl = glm::length(n);
            if (nl < 1e-12f)
                continue;
            n /= nl;
            // Yürünebilir: eğimi uygun yüzey. Ters sargılı modeller için normalin işareti dikkate alınmaz;
            // aşağı bakan yüzeyler aynı katı cismin sütun aralığında kaldığından zemin oluşturmaz.
            const uint8_t area = (std::fabs(n.y) >= walkableCos && t.Area != NavArea_NotWalkable) ? t.Area : kNullArea;
            glm::vec3 tmin = glm::min(glm::min(t.V[0], t.V[1]), t.V[2]), tmax = glm::max(glm::max(t.V[0], t.V[1]), t.V[2]);
            int z0 = std::clamp((int)std::floor((tmin.z - bmin.z) / cs), 0, depth - 1);
            int z1 = std::clamp((int)std::floor((tmax.z - bmin.z) / cs), 0, depth - 1);
            poly.assign(t.V, t.V + 3);
            for (int z = z0; z <= z1 && !poly.empty(); ++z)
            {
                const float cz = bmin.z + (float)(z + 1) * cs;
                DividePoly(poly, row, rest, cz, 2);
                poly.swap(rest);
                if (row.size() < 3)
                    continue;
                float minX = row[0].x, maxX = row[0].x;
                for (auto& v : row) { minX = std::min(minX, v.x); maxX = std::max(maxX, v.x); }
                int x0 = std::clamp((int)std::floor((minX - bmin.x) / cs), 0, width - 1);
                int x1 = std::clamp((int)std::floor((maxX - bmin.x) / cs), 0, width - 1);
                std::vector<glm::vec3> rowPoly = row;
                for (int x = x0; x <= x1 && !rowPoly.empty(); ++x)
                {
                    const float cx = bmin.x + (float)(x + 1) * cs;
                    DividePoly(rowPoly, cell, rest2, cx, 0);
                    rowPoly.swap(rest2);
                    if (cell.size() < 3)
                        continue;
                    float ymin = cell[0].y, ymax = cell[0].y;
                    for (auto& v : cell) { ymin = std::min(ymin, v.y); ymax = std::max(ymax, v.y); }
                    int smin = (int)std::floor((ymin - bmin.y) / ch), smax = (int)std::ceil((ymax - bmin.y) / ch);
                    smin = std::max(smin, 0);
                    smax = std::max(smax, smin + 1);
                    AddSpan(columns[(size_t)z * (size_t)width + (size_t)x], smin, smax, area, walkableClimb, ymax);
                }
            }
        }

        auto columnAt = [&](int x, int z) -> std::vector<Span>* {
            if (x < 0 || z < 0 || x >= width || z >= depth) return nullptr;
            return &columns[(size_t)z * (size_t)width + (size_t)x];
        };

        // ---- 4. Filtreler
        // a) Alçak engeller (kaldırım, basamak kenarı): yürünebilir yüzeyin hemen üstündeki kısa çıkıntılar yürünür
        for (auto& col : columns)
        {
            bool prevWalkable = false;
            uint8_t prevArea = kNullArea;
            int prevMax = 0;
            for (auto& s : col)
            {
                const bool walkable = s.Area != kNullArea;
                if (!walkable && prevWalkable && std::abs(s.Max - prevMax) <= walkableClimb)
                    s.Area = prevArea;
                prevWalkable = walkable;
                prevArea = s.Area;
                prevMax = s.Max;
            }
        }
        // b) Uçurum kenarları (komşuya düşüş tırmanma sınırından büyük)
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
            {
                auto& col = *columnAt(x, z);
                for (size_t si = 0; si < col.size(); ++si)
                {
                    Span& s = col[si];
                    if (s.Area == kNullArea)
                        continue;
                    const int bot = s.Max;
                    const int top = si + 1 < col.size() ? col[si + 1].Min : kMaxHeight;
                    int minh = kMaxHeight, asmin = s.Max, asmax = s.Max;
                    for (int d = 0; d < 4; ++d)
                    {
                        auto* ncol = columnAt(x + kDirX[d], z + kDirZ[d]);
                        if (!ncol)
                        {
                            minh = std::min(minh, -walkableClimb - bot);
                            continue;
                        }
                        int nbot = -walkableClimb;
                        int ntop = ncol->empty() ? kMaxHeight : (*ncol)[0].Min;
                        if (std::min(top, ntop) - std::max(bot, nbot) > walkableHeight)
                            minh = std::min(minh, nbot - bot);
                        for (size_t ni = 0; ni < ncol->size(); ++ni)
                        {
                            nbot = (*ncol)[ni].Max;
                            ntop = ni + 1 < ncol->size() ? (*ncol)[ni + 1].Min : kMaxHeight;
                            if (std::min(top, ntop) - std::max(bot, nbot) > walkableHeight)
                            {
                                minh = std::min(minh, nbot - bot);
                                if (std::abs(nbot - bot) <= walkableClimb)
                                {
                                    asmin = std::min(asmin, nbot);
                                    asmax = std::max(asmax, nbot);
                                }
                            }
                        }
                    }
                    if (minh < -walkableClimb)
                        s.Area = kNullArea;
                    else if (asmax - asmin > walkableClimb)
                        s.Area = kNullArea;
                }
            }
        // c) Tavan boşluğu ajan boyundan az
        for (auto& col : columns)
            for (size_t si = 0; si < col.size(); ++si)
            {
                const int top = si + 1 < col.size() ? col[si + 1].Min : kMaxHeight;
                if (top - col[si].Max <= walkableHeight)
                    col[si].Area = kNullArea;
            }

        // ---- 5. Zemin hücreleri
        nav.SetGrid(glm::vec3(bmin.x, bmin.y, bmin.z), width, depth, settings);
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
            {
                auto& col = *columnAt(x, z);
                for (size_t si = 0; si < col.size(); ++si)
                {
                    if (col[si].Area == kNullArea)
                        continue;
                    NavMesh::Cell c;
                    c.Y = col[si].Top;
                    const int top = si + 1 < col.size() ? col[si + 1].Min : kMaxHeight;
                    c.Clearance = top == kMaxHeight ? 1e6f : (float)(top - col[si].Max) * ch;
                    c.Area = col[si].Area;
                    nav.AddCell(x, z, c);
                }
            }
        columns.clear();
        columns.shrink_to_fit();
        nav.FinishColumns();
        auto& cells = nav.MutableCells();

        // ---- 6. Komşuluk (tırmanılabilir ve ajanın sığdığı geçişler)
        const float climb = (float)walkableClimb * ch + 1e-4f;
        for (size_t i = 0; i < cells.size(); ++i)
        {
            auto& c = cells[i];
            const int x = c.Column % width, z = c.Column / width;
            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + kDirX[d], nz = z + kDirZ[d];
                int best = -1;
                float bestDy = 1e30f;
                for (int j = nav.ColumnStart(nx, nz), e = j + nav.ColumnCount(nx, nz); j < e; ++j)
                {
                    const auto& n = cells[(size_t)j];
                    const float bot = std::max(c.Y, n.Y), top = std::min(c.Y + c.Clearance, n.Y + n.Clearance);
                    const float dy = std::fabs(n.Y - c.Y);
                    if (top - bot >= settings.AgentHeight && dy <= climb && dy < bestDy)
                    {
                        bestDy = dy;
                        best = j;
                    }
                }
                c.Neighbors[d] = best;
            }
        }

        // ---- 7. Ajan yarıçapı kadar aşındırma (kenarlardan uzaklık; 2 dik / 3 çapraz)
        const int radiusCells = (int)std::ceil(settings.AgentRadius / cs);
        if (radiusCells > 0)
        {
            std::vector<int> dist(cells.size(), std::numeric_limits<int>::max());
            using QN = std::pair<int, int>;
            std::priority_queue<QN, std::vector<QN>, std::greater<QN>> q;
            for (size_t i = 0; i < cells.size(); ++i)
            {
                bool border = false;
                for (int d = 0; d < 4; ++d)
                    if (cells[i].Neighbors[d] < 0 || cells[(size_t)cells[i].Neighbors[d]].Area == NavArea_NotWalkable)
                        border = true;
                if (border || cells[i].Area == NavArea_NotWalkable)
                {
                    dist[i] = 0;
                    q.push({ 0, (int)i });
                }
            }
            const int limit = radiusCells * 2;
            while (!q.empty())
            {
                auto [dcur, i] = q.top();
                q.pop();
                if (dcur != dist[(size_t)i] || dcur >= limit)
                    continue;
                for (int d = 0; d < 4; ++d)
                {
                    const int n = cells[(size_t)i].Neighbors[d];
                    if (n >= 0 && dcur + 2 < dist[(size_t)n]) { dist[(size_t)n] = dcur + 2; q.push({ dcur + 2, n }); }
                    const int dn = nav.NeighborDiagonal(i, d, (d + 1) % 4);
                    if (dn >= 0 && dcur + 3 < dist[(size_t)dn]) { dist[(size_t)dn] = dcur + 3; q.push({ dcur + 3, dn }); }
                }
            }
            for (size_t i = 0; i < cells.size(); ++i)
                if (dist[i] < limit)
                    cells[i].Area = NavArea_NotWalkable;
        }

        // ---- 8. Modifier Volume'lar
        for (auto e : reg.view<NavMeshModifierVolumeComponent>())
        {
            if (!scene->IsEntityActive(e))
                continue;
            const auto& v = reg.get<NavMeshModifierVolumeComponent>(e);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            const glm::mat4 inv = glm::inverse(w);
            const glm::vec3 half = glm::abs(v.Size) * 0.5f;
            const uint8_t area = (uint8_t)std::clamp(v.Area, 0, kNavMaxAreas - 1);
            for (size_t i = 0; i < cells.size(); ++i)
            {
                glm::vec3 p = glm::vec3(inv * glm::vec4(nav.CellPosition((int)i), 1.0f)) - v.Center;
                if (std::fabs(p.x) <= half.x && std::fabs(p.y) <= half.y && std::fabs(p.z) <= half.z && cells[i].Area != NavArea_NotWalkable)
                    cells[i].Area = area;
            }
        }

        // ---- 9. NavMesh Link'ler
        auto& links = nav.MutableLinks();
        for (auto e : reg.view<NavMeshLinkComponent>())
        {
            if (!scene->IsEntityActive(e))
                continue;
            const auto& l = reg.get<NavMeshLinkComponent>(e);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            NavMesh::Link link;
            link.Start = glm::vec3(w * glm::vec4(l.StartPoint, 1.0f));
            link.End = glm::vec3(w * glm::vec4(l.EndPoint, 1.0f));
            link.StartCell = nav.FindNearestCell(link.Start, std::max(1.0f, settings.AgentRadius * 3.0f));
            link.EndCell = nav.FindNearestCell(link.End, std::max(1.0f, settings.AgentRadius * 3.0f));
            if (link.StartCell < 0 || link.EndCell < 0)
            {
                CQ_CORE_WARN("[NavMesh] Link '{}' endpoints are not on the NavMesh", reg.get<TagComponent>(e).Tag);
                continue;
            }
            link.Start = nav.CellPosition(link.StartCell);
            link.End = nav.CellPosition(link.EndCell);
            link.Bidirectional = l.Bidirectional;
            link.Area = (uint8_t)std::clamp(l.Area, 0, kNavMaxAreas - 1);
            const float len = glm::length(link.End - link.Start);
            link.Cost = len * (l.CostModifier >= 0.0f ? l.CostModifier : nav.GetAreaCost(link.Area));
            const int index = (int)links.size();
            links.push_back(link);
            nav.m_LinksFrom[(size_t)link.StartCell].push_back(index * 2);
            if (link.Bidirectional)
                nav.m_LinksFrom[(size_t)link.EndCell].push_back(index * 2 + 1);
        }

        size_t walkable = 0;
        for (auto& c : cells)
            walkable += c.Area != NavArea_NotWalkable;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CQ_CORE_INFO("[NavMesh] Baked {} walkable cells ({} x {} grid, cell {:.3f} m, {} links) in {:.1f} ms", walkable, width, depth, cs, links.size(), ms);
    }
}
