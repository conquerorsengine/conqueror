#include "SpriteSheet.h"
#include "Core/Math/Triangulate2D.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Conqueror
{
    namespace fs = std::filesystem;

    namespace
    {
        struct CacheEntry
        {
            std::shared_ptr<SpriteSheet> Sheet;
            fs::file_time_type MetaTime{};
            bool HadMeta = false;
            std::chrono::steady_clock::time_point LastCheck{};
        };
        std::unordered_map<std::string, CacheEntry>& Cache()
        {
            static std::unordered_map<std::string, CacheEntry> s_Cache;
            return s_Cache;
        }
        std::mutex& CacheMutex()
        {
            static std::mutex s_Mutex;
            return s_Mutex;
        }

        inline uint8_t AlphaAt(const uint8_t* rgba, int w, int h, int x, int yBottom)
        {
            // yBottom: sol-alt köken; görüntü belleğinde satır 0 = üst
            int row = h - 1 - yBottom;
            return rgba[((size_t)row * w + x) * 4 + 3];
        }
    }

    const SpriteDef* SpriteSheet::Find(const std::string& name) const
    {
        for (const auto& s : Sprites)
            if (s.Name == name)
                return &s;
        return nullptr;
    }

    SpriteDef* SpriteSheet::Find(const std::string& name)
    {
        for (auto& s : Sprites)
            if (s.Name == name)
                return &s;
        return nullptr;
    }

    const SpriteDef* SpriteSheet::FindForSprite(const std::string& name, const glm::vec2& textureSize) const
    {
        if (!name.empty())
            return Find(name);
        if (Sprites.size() != 1)
            return nullptr;
        const glm::vec4& r = Sprites[0].Rect;
        return r.x == 0.0f && r.y == 0.0f && r.z == textureSize.x && r.w == textureSize.y ? &Sprites[0] : nullptr;
    }

    std::string SpriteSheet::MakeUniqueName(const std::string& base) const
    {
        if (!Find(base))
            return base;
        for (int i = 1; ; ++i)
        {
            std::string n = base + "_" + std::to_string(i);
            if (!Find(n))
                return n;
        }
    }

    namespace
    {
        void WritePolygons(YAML::Emitter& out, const char* key, const std::vector<std::vector<glm::vec2>>& polys)
        {
            if (polys.empty())
                return;
            out << YAML::Key << key << YAML::Value << YAML::BeginSeq;
            for (const auto& poly : polys)
            {
                std::vector<float> flat;
                for (const auto& p : poly) { flat.push_back(p.x); flat.push_back(p.y); }
                out << YAML::Flow << flat;
            }
            out << YAML::EndSeq;
        }

        void ReadPolygons(const YAML::Node& node, std::vector<std::vector<glm::vec2>>& out)
        {
            out.clear();
            if (!node || !node.IsSequence())
                return;
            for (const auto& poly : node)
            {
                std::vector<glm::vec2> pts;
                for (std::size_t i = 0; i + 1 < poly.size(); i += 2)
                    pts.push_back({ poly[i].as<float>(), poly[i + 1].as<float>() });
                if (pts.size() >= 3)
                    out.push_back(std::move(pts));
            }
        }

        // Kapalı çokgeni Douglas-Peucker ile sadeleştirir
        void SimplifyRange(const std::vector<glm::vec2>& p, size_t a, size_t b, float eps, std::vector<char>& keep)
        {
            if (b <= a + 1)
                return;
            const glm::vec2 u = p[b] - p[a];
            const float len = glm::length(u);
            float worst = -1.0f;
            size_t idx = a;
            for (size_t i = a + 1; i < b; ++i)
            {
                const glm::vec2 w = p[i] - p[a];
                const float d = len > 1e-6f ? std::abs(u.x * w.y - u.y * w.x) / len : glm::length(w);
                if (d > worst) { worst = d; idx = i; }
            }
            if (worst > eps)
            {
                keep[idx] = 1;
                SimplifyRange(p, a, idx, eps, keep);
                SimplifyRange(p, idx, b, eps, keep);
            }
        }

        std::vector<glm::vec2> SimplifyLoop(const std::vector<glm::vec2>& loop, float eps)
        {
            if (loop.size() < 4)
                return loop;
            // Başlangıç noktasından en uzak noktayla ikiye böl
            size_t far = 0;
            float best = -1.0f;
            for (size_t i = 1; i < loop.size(); ++i)
            {
                const float d = glm::distance(loop[0], loop[i]);
                if (d > best) { best = d; far = i; }
            }
            std::vector<glm::vec2> open = loop;
            open.push_back(loop[0]);
            std::vector<char> keep(open.size(), 0);
            keep[0] = keep[far] = keep[open.size() - 1] = 1;
            SimplifyRange(open, 0, far, eps, keep);
            SimplifyRange(open, far, open.size() - 1, eps, keep);
            std::vector<glm::vec2> out;
            for (size_t i = 0; i + 1 < open.size(); ++i)
                if (keep[i])
                    out.push_back(open[i]);
            return out.size() >= 3 ? out : loop;
        }
    }

    void SpriteSheet::BuildOutlineTriangles(SpriteDef& sprite, int extrude)
    {
        sprite.OutlineTriangles.clear();
        std::vector<uint32_t> tris;
        const glm::vec2 size(std::max(sprite.Rect.z, 1.0f), std::max(sprite.Rect.w, 1.0f));
        for (const auto& source : sprite.Outlines)
        {
            std::vector<glm::vec2> poly = source;
            if (extrude > 0 && source.size() >= 3)
            {
                // Her köşe, iki kenarının dışa bakan normallerinin ortalaması boyunca kayar (dilimin dışına taşmaz)
                const float side = Triangulate2D::SignedArea(source) >= 0.0f ? 1.0f : -1.0f;
                const size_t n = source.size();
                for (size_t i = 0; i < n; ++i)
                {
                    const glm::vec2 a = source[(i + n - 1) % n], b = source[i], c = source[(i + 1) % n];
                    const glm::vec2 e0 = b - a, e1 = c - b;
                    if (glm::dot(e0, e0) < 1e-6f || glm::dot(e1, e1) < 1e-6f)
                        continue;
                    const glm::vec2 n0 = glm::normalize(glm::vec2(e0.y, -e0.x)) * side, n1 = glm::normalize(glm::vec2(e1.y, -e1.x)) * side;
                    glm::vec2 m = n0 + n1;
                    const float len = glm::length(m);
                    if (len < 1e-4f)
                        continue;
                    m /= len;
                    const float cosHalf = std::max(glm::dot(m, n0), 0.35f);
                    poly[i] = glm::clamp(b + m * ((float)extrude / cosHalf), glm::vec2(0.0f), size);
                }
                if (!Triangulate2D::Triangulate(poly, tris))
                    poly = source;
            }
            if (!Triangulate2D::Triangulate(poly, tris))
                continue;
            for (uint32_t i : tris)
                sprite.OutlineTriangles.push_back(poly[i]);
        }
    }

    std::vector<std::vector<glm::vec2>> SpriteSheet::GenerateOutline(const uint8_t* rgba, int width, int height, const glm::vec4& rect, float tolerance,
                                                                     uint8_t alphaThreshold)
    {
        std::vector<std::vector<glm::vec2>> result;
        if (!rgba || width <= 0 || height <= 0)
            return result;
        const int x0 = std::clamp((int)rect.x, 0, width), y0 = std::clamp((int)rect.y, 0, height);
        const int w = std::min((int)rect.z, width - x0), h = std::min((int)rect.w, height - y0);
        if (w <= 0 || h <= 0)
            return result;
        std::vector<uint8_t> solid((size_t)w * h, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                solid[(size_t)y * w + x] = AlphaAt(rgba, width, height, x0 + x, y0 + y) >= alphaThreshold ? 1 : 0;
        auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && solid[(size_t)y * w + x] != 0; };
        // Dolu pikselin solunda kalan yönlü kenarlar (köşe ızgarasında); uç noktadan sonraki kenarlar
        struct Edge { int X0, Y0, X1, Y1; bool Used; };
        std::vector<Edge> edges;
        std::unordered_map<uint64_t, std::vector<int>> from;
        auto key = [](int x, int y) { return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y; };
        auto add = [&](int x0e, int y0e, int x1e, int y1e) {
            from[key(x0e, y0e)].push_back((int)edges.size());
            edges.push_back({ x0e, y0e, x1e, y1e, false });
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                if (!at(x, y))
                    continue;
                if (!at(x, y - 1)) add(x, y, x + 1, y);             // alt
                if (!at(x + 1, y)) add(x + 1, y, x + 1, y + 1);     // sağ
                if (!at(x, y + 1)) add(x + 1, y + 1, x, y + 1);     // üst
                if (!at(x - 1, y)) add(x, y + 1, x, y);             // sol
            }
        const float eps = glm::mix(std::max(w, h) * 0.08f, 0.5f, std::clamp(tolerance, 0.0f, 1.0f));
        for (size_t i = 0; i < edges.size(); ++i)
        {
            if (edges[i].Used)
                continue;
            std::vector<glm::vec2> loop;
            int cur = (int)i;
            size_t guard = 0;
            while (cur >= 0 && !edges[(size_t)cur].Used && guard++ <= edges.size())
            {
                Edge& e = edges[(size_t)cur];
                e.Used = true;
                loop.push_back({ (float)e.X0, (float)e.Y0 });
                // Sonraki kenar: bir köşede ikiden çok kenar varsa en sola dönen (komşu adalar ayrı kalsın)
                const glm::ivec2 dir(e.X1 - e.X0, e.Y1 - e.Y0);
                int next = -1, bestTurn = -2;
                auto it = from.find(key(e.X1, e.Y1));
                if (it != from.end())
                    for (int c : it->second)
                    {
                        if (edges[(size_t)c].Used)
                            continue;
                        const glm::ivec2 d(edges[(size_t)c].X1 - edges[(size_t)c].X0, edges[(size_t)c].Y1 - edges[(size_t)c].Y0);
                        const int turn = dir.x * d.y - dir.y * d.x;
                        if (turn > bestTurn) { bestTurn = turn; next = c; }
                    }
                cur = next;
            }
            // Dış sınırlar saat yönünün tersi (delikler atlanır)
            if (loop.size() >= 3 && Triangulate2D::SignedArea(loop) > 0.0f)
                result.push_back(SimplifyLoop(loop, eps));
        }
        return result;
    }

    fs::path SpriteSheet::MetaPathFor(const std::string& texturePath)
    {
        return fs::path(texturePath + ".cqsprite");
    }

    bool SpriteSheet::Save() const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "SpriteSheet" << YAML::Value << 1;
        out << YAML::Key << "PixelsPerUnit" << YAML::Value << PixelsPerUnit;
        out << YAML::Key << "Filter" << YAML::Value << (FilterMode == Filter::Point ? "Point" : "Bilinear");
        out << YAML::Key << "Sprites" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : Sprites)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << s.Name;
            out << YAML::Key << "Rect" << YAML::Value << YAML::Flow << YAML::BeginSeq << s.Rect.x << s.Rect.y << s.Rect.z << s.Rect.w << YAML::EndSeq;
            out << YAML::Key << "Pivot" << YAML::Value << YAML::Flow << YAML::BeginSeq << s.Pivot.x << s.Pivot.y << YAML::EndSeq;
            out << YAML::Key << "Border" << YAML::Value << YAML::Flow << YAML::BeginSeq << s.Border.x << s.Border.y << s.Border.z << s.Border.w << YAML::EndSeq;
            WritePolygons(out, "Outlines", s.Outlines);
            WritePolygons(out, "PhysicsShapes", s.PhysicsShapes);
            if (!s.Skin.Bones.empty() || !s.Skin.Vertices.empty())
            {
                out << YAML::Key << "Skin" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "Bones" << YAML::Value << YAML::BeginSeq;
                for (const auto& b : s.Skin.Bones)
                {
                    out << YAML::Flow << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << b.Name << YAML::Key << "Parent" << YAML::Value << b.Parent;
                    out << YAML::Key << "Pos" << YAML::Value << YAML::Flow << YAML::BeginSeq << b.Position.x << b.Position.y << YAML::EndSeq;
                    out << YAML::Key << "Tip" << YAML::Value << YAML::Flow << YAML::BeginSeq << b.Tip.x << b.Tip.y << YAML::EndSeq << YAML::EndMap;
                }
                out << YAML::EndSeq;
                std::vector<float> flat;
                for (const auto& v : s.Skin.Vertices) { flat.push_back(v.x); flat.push_back(v.y); }
                out << YAML::Key << "Vertices" << YAML::Value << YAML::Flow << flat;
                out << YAML::Key << "Indices" << YAML::Value << YAML::Flow << s.Skin.Indices;
                std::vector<int> bi;
                std::vector<float> bw;
                for (size_t i = 0; i < s.Skin.BoneIndex.size(); ++i)
                    for (int k = 0; k < 4; ++k)
                    {
                        bi.push_back(s.Skin.BoneIndex[i][k]);
                        bw.push_back(s.Skin.BoneWeight[i][k]);
                    }
                out << YAML::Key << "BoneIndex" << YAML::Value << YAML::Flow << bi;
                out << YAML::Key << "BoneWeight" << YAML::Value << YAML::Flow << bw;
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        if (FullRectMesh)
            out << YAML::Key << "MeshType" << YAML::Value << "FullRect";
        if (ExtrudeEdges > 0)
            out << YAML::Key << "ExtrudeEdges" << YAML::Value << ExtrudeEdges;
        if (!SecondaryTextures.empty())
        {
            out << YAML::Key << "SecondaryTextures" << YAML::Value << YAML::BeginSeq;
            for (const auto& t : SecondaryTextures)
                out << YAML::Flow << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << t.Name << YAML::Key << "Path" << YAML::Value << t.Path << YAML::EndMap;
            out << YAML::EndSeq;
        }
        out << YAML::EndMap;

        fs::path path = MetaPathFor(TexturePath);
        fs::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f)
            {
                CQ_CORE_ERROR("SpriteSheet: cannot write {0}", tmp.string());
                return false;
            }
            f << out.c_str();
        }
        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec)
        {
            CQ_CORE_ERROR("SpriteSheet: save failed {0}", ec.message());
            return false;
        }
        Invalidate(TexturePath);
        return true;
    }

    bool SpriteSheet::Load()
    {
        fs::path path = MetaPathFor(TexturePath);
        std::error_code ec;
        if (!fs::exists(path, ec))
            return false;
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            PixelsPerUnit = root["PixelsPerUnit"].as<float>(100.0f);
            FilterMode = root["Filter"].as<std::string>("Bilinear") == "Point" ? Filter::Point : Filter::Bilinear;
            Sprites.clear();
            for (const auto& n : root["Sprites"])
            {
                SpriteDef d;
                d.Name = n["Name"].as<std::string>("Sprite");
                if (auto r = n["Rect"]; r && r.size() == 4) d.Rect = { r[0].as<float>(), r[1].as<float>(), r[2].as<float>(), r[3].as<float>() };
                if (auto p = n["Pivot"]; p && p.size() == 2) d.Pivot = { p[0].as<float>(), p[1].as<float>() };
                if (auto b = n["Border"]; b && b.size() == 4) d.Border = { b[0].as<float>(), b[1].as<float>(), b[2].as<float>(), b[3].as<float>() };
                ReadPolygons(n["Outlines"], d.Outlines);
                ReadPolygons(n["PhysicsShapes"], d.PhysicsShapes);
                if (auto skin = n["Skin"])
                {
                    if (auto bones = skin["Bones"]; bones && bones.IsSequence())
                        for (const auto& b : bones)
                        {
                            SpriteBone bone;
                            bone.Name = b["Name"].as<std::string>("Bone");
                            bone.Parent = b["Parent"].as<int>(-1);
                            if (auto v = b["Pos"]; v && v.size() == 2) bone.Position = { v[0].as<float>(), v[1].as<float>() };
                            if (auto v = b["Tip"]; v && v.size() == 2) bone.Tip = { v[0].as<float>(), v[1].as<float>() };
                            d.Skin.Bones.push_back(bone);
                        }
                    if (auto v = skin["Vertices"]; v && v.IsSequence())
                        for (std::size_t i = 0; i + 1 < v.size(); i += 2)
                            d.Skin.Vertices.push_back({ v[i].as<float>(), v[i + 1].as<float>() });
                    if (auto v = skin["Indices"]; v && v.IsSequence())
                        for (std::size_t i = 0; i < v.size(); ++i)
                            d.Skin.Indices.push_back(v[i].as<uint32_t>());
                    auto bi = skin["BoneIndex"];
                    auto bw = skin["BoneWeight"];
                    if (bi && bw && bi.IsSequence() && bw.IsSequence() && bi.size() == bw.size())
                        for (std::size_t i = 0; i + 3 < bi.size(); i += 4)
                        {
                            d.Skin.BoneIndex.push_back({ bi[i].as<int>(), bi[i + 1].as<int>(), bi[i + 2].as<int>(), bi[i + 3].as<int>() });
                            d.Skin.BoneWeight.push_back({ bw[i].as<float>(), bw[i + 1].as<float>(), bw[i + 2].as<float>(), bw[i + 3].as<float>() });
                        }
                }
                BuildOutlineTriangles(d, root["ExtrudeEdges"].as<int>(0));
                Sprites.push_back(d);
            }
            FullRectMesh = root["MeshType"].as<std::string>("") == "FullRect";
            ExtrudeEdges = root["ExtrudeEdges"].as<int>(0);
            SecondaryTextures.clear();
            if (auto list = root["SecondaryTextures"]; list && list.IsSequence())
                for (const auto& n : list)
                    SecondaryTextures.push_back({ n["Name"].as<std::string>(""), n["Path"].as<std::string>("") });
            m_LoadedTime = fs::last_write_time(path, ec);
            return true;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("SpriteSheet: failed to parse {0}: {1}", path.string(), e.what());
            return false;
        }
    }

    std::shared_ptr<SpriteSheet> SpriteSheet::Get(const std::string& texturePath)
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        auto& entry = Cache()[texturePath];
        auto now = std::chrono::steady_clock::now();
        // Dosya değişikliğini en fazla 0.5 sn'de bir kontrol et
        if (entry.Sheet && now - entry.LastCheck < std::chrono::milliseconds(500))
            return entry.Sheet;
        entry.LastCheck = now;

        std::error_code ec;
        fs::path meta = MetaPathFor(texturePath);
        bool hasMeta = fs::exists(meta, ec);
        fs::file_time_type t = hasMeta ? fs::last_write_time(meta, ec) : fs::file_time_type{};
        if (!entry.Sheet || hasMeta != entry.HadMeta || t != entry.MetaTime)
        {
            auto sheet = std::make_shared<SpriteSheet>();
            sheet->TexturePath = texturePath;
            if (hasMeta)
                sheet->Load();
            entry.Sheet = sheet;
            entry.HadMeta = hasMeta;
            entry.MetaTime = t;
        }
        return entry.Sheet;
    }

    void SpriteSheet::Invalidate(const std::string& texturePath)
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        Cache().erase(texturePath);
    }

    // ------------------------------------------------------------------ Dilimleme

    std::vector<glm::vec4> SpriteSheet::SliceGrid(int texWidth, int texHeight, glm::ivec2 cellSize, glm::ivec2 offset, glm::ivec2 padding,
                                                  const uint8_t* rgba, bool skipEmpty)
    {
        std::vector<glm::vec4> rects;
        if (cellSize.x <= 0 || cellSize.y <= 0)
            return rects;
        // Unity gibi sol-üstten başlayarak satır satır (ad sırası okuma yönüyle eşleşir)
        for (int top = offset.y; top + cellSize.y <= texHeight; top += cellSize.y + padding.y)
        {
            for (int x = offset.x; x + cellSize.x <= texWidth; x += cellSize.x + padding.x)
            {
                int yBottom = texHeight - top - cellSize.y;
                glm::vec4 r((float)x, (float)yBottom, (float)cellSize.x, (float)cellSize.y);
                if (rgba && skipEmpty)
                {
                    bool any = false;
                    for (int yy = 0; yy < cellSize.y && !any; ++yy)
                        for (int xx = 0; xx < cellSize.x; ++xx)
                            if (AlphaAt(rgba, texWidth, texHeight, x + xx, yBottom + yy) > 0) { any = true; break; }
                    if (!any)
                        continue;
                }
                rects.push_back(r);
            }
        }
        return rects;
    }

    std::vector<glm::vec4> SpriteSheet::SliceAutomatic(const uint8_t* rgba, int width, int height, int minSize, uint8_t alphaThreshold)
    {
        std::vector<glm::vec4> rects;
        if (!rgba || width <= 0 || height <= 0)
            return rects;

        // Opak piksel adaları (8-komşu flood fill), sınırlayıcı kutular
        std::vector<uint8_t> visited((size_t)width * height, 0);
        std::vector<std::pair<int, int>> stack;
        auto opaque = [&](int x, int y) { return rgba[((size_t)y * width + x) * 4 + 3] >= alphaThreshold; };

        struct Box { int MinX, MinY, MaxX, MaxY; };
        std::vector<Box> boxes;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                size_t idx = (size_t)y * width + x;
                if (visited[idx] || !opaque(x, y))
                    continue;
                Box b{ x, y, x, y };
                stack.clear();
                stack.push_back({ x, y });
                visited[idx] = 1;
                while (!stack.empty())
                {
                    auto [cx, cy] = stack.back();
                    stack.pop_back();
                    b.MinX = std::min(b.MinX, cx); b.MaxX = std::max(b.MaxX, cx);
                    b.MinY = std::min(b.MinY, cy); b.MaxY = std::max(b.MaxY, cy);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            int nx = cx + dx, ny = cy + dy;
                            if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                                continue;
                            size_t ni = (size_t)ny * width + nx;
                            if (!visited[ni] && opaque(nx, ny))
                            {
                                visited[ni] = 1;
                                stack.push_back({ nx, ny });
                            }
                        }
                }
                boxes.push_back(b);
            }
        }

        // Çakışan/iç içe kutuları birleştir (ör. "i" harfinin noktası değil, ama gölgeler)
        bool merged = true;
        while (merged)
        {
            merged = false;
            for (size_t i = 0; i < boxes.size() && !merged; ++i)
                for (size_t j = i + 1; j < boxes.size(); ++j)
                {
                    const Box& a = boxes[i];
                    const Box& c = boxes[j];
                    if (a.MinX <= c.MaxX && c.MinX <= a.MaxX && a.MinY <= c.MaxY && c.MinY <= a.MaxY)
                    {
                        Box m{ std::min(a.MinX, c.MinX), std::min(a.MinY, c.MinY), std::max(a.MaxX, c.MaxX), std::max(a.MaxY, c.MaxY) };
                        boxes[i] = m;
                        boxes.erase(boxes.begin() + (long)j);
                        merged = true;
                        break;
                    }
                }
        }

        // Okuma sırası: yukarıdan aşağı, soldan sağa
        std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
            if (std::abs(a.MinY - b.MinY) > 4) return a.MinY < b.MinY;
            return a.MinX < b.MinX;
        });
        for (const auto& b : boxes)
        {
            int w = b.MaxX - b.MinX + 1, h = b.MaxY - b.MinY + 1;
            if (w < minSize && h < minSize)
                continue;
            rects.push_back(glm::vec4((float)b.MinX, (float)(height - 1 - b.MaxY), (float)w, (float)h));
        }
        return rects;
    }

    glm::vec4 SpriteSheet::Trim(const uint8_t* rgba, int width, int height, const glm::vec4& rect, uint8_t alphaThreshold)
    {
        if (!rgba)
            return rect;
        int x0 = std::max(0, (int)rect.x), y0 = std::max(0, (int)rect.y);
        int x1 = std::min(width, (int)(rect.x + rect.z)), y1 = std::min(height, (int)(rect.y + rect.w));
        int minX = x1, minY = y1, maxX = x0 - 1, maxY = y0 - 1;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (AlphaAt(rgba, width, height, x, y) >= alphaThreshold)
                {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
        if (maxX < minX || maxY < minY)
            return rect;
        return glm::vec4((float)minX, (float)minY, (float)(maxX - minX + 1), (float)(maxY - minY + 1));
    }
}
