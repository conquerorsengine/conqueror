#include "SpriteAtlas.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/TextureSettings.h"

#include <stb_image.h>
#include "stb_image_write.h"
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Conqueror::SpriteAtlases
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        bool IsImage(const fs::path& p)
        {
            const std::string e = Lower(p.extension().string());
            if (Lower(p.filename().string()).find(".cqspriteatlas.") != std::string::npos)
                return false;   // başka bir atlasın paketi
            return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp";
        }

        fs::path Abs(const std::string& p)
        {
            fs::path path(SceneSerializer::ResolveAssetPath(p));
            if (path.is_relative() && Project::GetActive())
                path = Project::GetActiveProjectDirectory() / path;
            return path;
        }

        std::string Rel(const fs::path& p)
        {
            std::error_code ec;
            if (Project::GetActive())
            {
                auto r = fs::relative(p, Project::GetActiveProjectDirectory(), ec);
                if (!ec && !r.empty() && r.native()[0] != '.')
                    return r.generic_string();
            }
            return p.generic_string();
        }

        std::string Key(const fs::path& p)
        {
            std::error_code ec;
            auto c = fs::weakly_canonical(p, ec);
            return (ec ? p : c).lexically_normal().generic_string();
        }

        std::vector<fs::path> Collect(const SpriteAtlasAsset& atlas)
        {
            std::vector<fs::path> out;
            std::error_code ec;
            for (const auto& o : atlas.Objects)
            {
                const fs::path p = Abs(o);
                if (fs::is_directory(p, ec))
                {
                    for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                         it != fs::recursive_directory_iterator(); it.increment(ec))
                        if (!ec && it->is_regular_file(ec) && IsImage(it->path()))
                            out.push_back(it->path());
                }
                else if (fs::exists(p, ec) && IsImage(p))
                    out.push_back(p);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }

        struct Table
        {
            bool Built = false;
            struct Item { std::string AtlasPng; glm::vec2 Offset; glm::vec2 Size; };
            std::unordered_map<std::string, Item> Sources;                       // kaynak anahtarı -> yerleşim
            std::unordered_map<std::string, std::shared_ptr<Texture2D>> Textures;   // atlas png -> doku
            std::unordered_map<std::string, std::string> PathKeys;               // doku yolu -> anahtar (önbellek)
        };
        Table& T()
        {
            static Table* t = new Table();   // GL dokuları bağlam kapanırken Shutdown ile bırakılır
            return *t;
        }

        void Build()
        {
            auto& t = T();
            t.Built = true;
            t.Sources.clear();
            if (!Project::GetActive())
                return;
            std::error_code ec;
            const fs::path assets = Project::GetActiveProjectDirectory() / "Assets";
            for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec || it->path().extension() != Extension)
                    continue;
                SpriteAtlasAsset a;
                if (!Load(it->path().string(), a) || !a.IncludeInBuild || a.Packed.empty() || a.PackedSize.x <= 0)
                    continue;
                const std::string png = PackedTexturePath(it->path().string());
                if (!fs::exists(png, ec))
                    continue;
                for (const auto& e : a.Packed)
                    t.Sources[Key(Abs(e.Source))] = { png, glm::vec2(e.Rect.x, e.Rect.y), glm::vec2(a.PackedSize) };
            }
        }
    }

    std::string PackedTexturePath(const std::string& atlasPath)
    {
        return Abs(atlasPath).string() + ".png";
    }

    bool Load(const std::string& path, SpriteAtlasAsset& out)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(Abs(path).string());
            YAML::Node n = root["SpriteAtlas"];
            if (!n)
                return false;
            out = {};
            if (auto o = n["Objects"]; o && o.IsSequence())
                for (const auto& x : o) out.Objects.push_back(x.as<std::string>());
            if (n["Padding"]) out.Padding = std::clamp(n["Padding"].as<int>(), 0, 32);
            if (n["MaxSize"]) out.MaxSize = std::clamp(n["MaxSize"].as<int>(), 64, 8192);
            if (n["PointFilter"]) out.PointFilter = n["PointFilter"].as<bool>();
            if (n["IncludeInBuild"]) out.IncludeInBuild = n["IncludeInBuild"].as<bool>();
            if (auto s = n["PackedSize"]; s && s.IsSequence() && s.size() == 2) out.PackedSize = { s[0].as<int>(), s[1].as<int>() };
            if (auto p = n["Packed"]; p && p.IsSequence())
                for (const auto& e : p)
                {
                    SpriteAtlasAsset::Entry entry;
                    entry.Source = e["Source"].as<std::string>();
                    if (auto r = e["Rect"]; r && r.IsSequence() && r.size() == 4)
                        entry.Rect = { r[0].as<int>(), r[1].as<int>(), r[2].as<int>(), r[3].as<int>() };
                    out.Packed.push_back(entry);
                }
            return true;
        }
        catch (...) { return false; }
    }

    bool Save(const std::string& path, const SpriteAtlasAsset& a)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "SpriteAtlas" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "Objects" << YAML::Value << YAML::BeginSeq;
        for (const auto& o : a.Objects) e << o;
        e << YAML::EndSeq;
        e << YAML::Key << "Padding" << YAML::Value << a.Padding;
        e << YAML::Key << "MaxSize" << YAML::Value << a.MaxSize;
        e << YAML::Key << "PointFilter" << YAML::Value << a.PointFilter;
        e << YAML::Key << "IncludeInBuild" << YAML::Value << a.IncludeInBuild;
        e << YAML::Key << "PackedSize" << YAML::Value << YAML::Flow << YAML::BeginSeq << a.PackedSize.x << a.PackedSize.y << YAML::EndSeq;
        e << YAML::Key << "Packed" << YAML::Value << YAML::BeginSeq;
        for (const auto& p : a.Packed)
        {
            e << YAML::BeginMap << YAML::Key << "Source" << YAML::Value << p.Source;
            e << YAML::Key << "Rect" << YAML::Value << YAML::Flow << YAML::BeginSeq << p.Rect.x << p.Rect.y << p.Rect.z << p.Rect.w << YAML::EndSeq;
            e << YAML::EndMap;
        }
        e << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
        std::ofstream f(Abs(path), std::ios::trunc);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return true;
    }

    bool Pack(const std::string& path, std::string* error)
    {
        SpriteAtlasAsset atlas;
        if (!Load(path, atlas))
        {
            if (error) *error = "cannot read " + path;
            return false;
        }
        struct Img { fs::path Path; int W = 0, H = 0; std::vector<uint8_t> Px; int X = 0, Y = 0; };
        std::vector<Img> imgs;
        for (const auto& p : Collect(atlas))
        {
            Img img;
            img.Path = p;
            int c = 0;
            stbi_set_flip_vertically_on_load(0);
            stbi_uc* data = stbi_load(p.string().c_str(), &img.W, &img.H, &c, 4);
            if (!data)
                continue;
            img.Px.assign(data, data + (size_t)img.W * img.H * 4);
            stbi_image_free(data);
            imgs.push_back(std::move(img));
        }
        if (imgs.empty())
        {
            if (error) *error = "no images to pack (add sprites or folders to Objects for Packing)";
            return false;
        }
        // Raf (shelf) paketleme: yüksekliğe göre azalan; en küçük uygun 2^n boyut
        std::vector<size_t> order(imgs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return imgs[a].H != imgs[b].H ? imgs[a].H > imgs[b].H : imgs[a].W > imgs[b].W; });
        const int pad = atlas.Padding;
        int W = 0, H = 0;
        bool placed = false;
        for (int size = 64; size <= atlas.MaxSize && !placed; size *= 2)
        {
            for (int hMul = 1; hMul >= 0 && !placed; --hMul)   // önce kare, sonra yarı yükseklik dene değil: kare yeterli
            {
                const int w = size, h = size;
                int x = pad, y = pad, rowH = 0;
                bool ok = true;
                for (size_t i : order)
                {
                    Img& img = imgs[i];
                    if (img.W + 2 * pad > w) { ok = false; break; }
                    if (x + img.W + pad > w) { x = pad; y += rowH + pad; rowH = 0; }
                    if (y + img.H + pad > h) { ok = false; break; }
                    img.X = x; img.Y = y;
                    x += img.W + pad;
                    rowH = std::max(rowH, img.H);
                }
                if (ok)
                {
                    W = w;
                    // Kullanılan yüksekliğe göre kırp (2^n)
                    int used = 0;
                    for (auto& img : imgs) used = std::max(used, img.Y + img.H + pad);
                    H = 1;
                    while (H < used) H *= 2;
                    placed = true;
                }
                break;
            }
        }
        if (!placed)
        {
            if (error) *error = "the sprites do not fit in " + std::to_string(atlas.MaxSize) + " x " + std::to_string(atlas.MaxSize) + " (raise Max Size)";
            return false;
        }
        std::vector<uint8_t> out((size_t)W * H * 4, 0);
        auto put = [&](int x, int y, const uint8_t* c) {
            if (x < 0 || y < 0 || x >= W || y >= H) return;
            std::copy(c, c + 4, &out[((size_t)y * W + x) * 4]);
        };
        const int extrude = pad / 2;
        atlas.Packed.clear();
        for (const auto& img : imgs)
        {
            for (int y = -extrude; y < img.H + extrude; ++y)
                for (int x = -extrude; x < img.W + extrude; ++x)
                {
                    // Kenar taşırma (bilinear süzgeçte komşu sprite'tan renk sızmasın)
                    const int sx = std::clamp(x, 0, img.W - 1), sy = std::clamp(y, 0, img.H - 1);
                    put(img.X + x, img.Y + y, &img.Px[((size_t)sy * img.W + sx) * 4]);
                }
            // Görüntü satırları üstten; doku sol-alttan (yükleyici çevirir)
            atlas.Packed.push_back({ Rel(img.Path), glm::ivec4(img.X, H - img.Y - img.H, img.W, img.H) });
        }
        atlas.PackedSize = { W, H };
        const std::string png = PackedTexturePath(path);
        if (!stbi_write_png(png.c_str(), W, H, 4, out.data(), W * 4))
        {
            if (error) *error = "cannot write " + png;
            return false;
        }
        TextureImportSettings settings = TextureImportSettings::DefaultsFor(TextureType::Sprite);
        settings.Filter = atlas.PointFilter ? TextureFilter::Point : TextureFilter::Bilinear;
        settings.MaxSize = std::max(W, H);
        settings.Save(png);
        Save(path, atlas);
        Invalidate();
        CQ_CORE_INFO("[SpriteAtlas] Packed {0} sprites into {1}x{2}: {3}", imgs.size(), W, H, fs::path(png).filename().string());
        return true;
    }

    int PackAllIfStale()
    {
        if (!Project::GetActive())
            return 0;
        int packed = 0;
        std::error_code ec;
        const fs::path assets = Project::GetActiveProjectDirectory() / "Assets";
        for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec || it->path().extension() != Extension)
                continue;
            SpriteAtlasAsset a;
            if (!Load(it->path().string(), a) || !a.IncludeInBuild)
                continue;
            const fs::path png = PackedTexturePath(it->path().string());
            bool stale = !fs::exists(png, ec) || fs::last_write_time(it->path(), ec) > fs::last_write_time(png, ec);
            const auto pngTime = fs::last_write_time(png, ec);
            std::vector<fs::path> sources = Collect(a);
            if (!stale && sources.size() != a.Packed.size())
                stale = true;
            for (const auto& s : sources)
                if (!stale && fs::last_write_time(s, ec) > pngTime)
                    stale = true;
            if (stale && !sources.empty() && Pack(it->path().string()))
                ++packed;
        }
        return packed;
    }

    bool Find(const std::string& sourceTexturePath, Lookup& out)
    {
        if (sourceTexturePath.empty())
            return false;
        auto& t = T();
        if (!t.Built)
            Build();
        if (t.Sources.empty())
            return false;
        auto k = t.PathKeys.find(sourceTexturePath);
        if (k == t.PathKeys.end())
            k = t.PathKeys.emplace(sourceTexturePath, Key(Abs(sourceTexturePath))).first;
        auto it = t.Sources.find(k->second);
        if (it == t.Sources.end())
            return false;
        auto& tex = t.Textures[it->second.AtlasPng];
        if (!tex)
            tex = Texture2D::Create(it->second.AtlasPng);
        if (!tex || !tex->GetRendererID())
            return false;
        out.Texture = tex;
        out.Offset = it->second.Offset;
        out.Size = it->second.Size;
        return true;
    }

    void Invalidate()
    {
        auto& t = T();
        t.Built = false;
        t.Sources.clear();
        t.Textures.clear();
        t.PathKeys.clear();
    }

    void Shutdown()
    {
        Invalidate();
    }
}
