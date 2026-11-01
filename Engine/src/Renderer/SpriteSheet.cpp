#include "SpriteSheet.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
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
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;

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
                Sprites.push_back(d);
            }
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
