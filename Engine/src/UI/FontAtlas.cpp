#include "FontAtlas.h"
#include "Core/Logging/Log.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/FileSystem.h"
#include "Renderer/RHI/Texture.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Conqueror
{
    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<FontAtlas>>& Cache()
        {
            static std::unordered_map<std::string, std::shared_ptr<FontAtlas>> s_Cache;
            return s_Cache;
        }

        std::string DefaultFontPath()
        {
            const char* candidates[] = { "Resources/Fonts/opensans/OpenSans-Regular.ttf", "../Resources/Fonts/opensans/OpenSans-Regular.ttf" };
            for (const char* c : candidates)
                if (std::filesystem::exists(c))
                    return c;
            return FileSystem::ResolveResourcePath("Resources/Fonts/opensans/OpenSans-Regular.ttf");
        }

        const SDFGlyph s_Empty{};
    }

    FontAtlas::FontAtlas(const std::string& path)
        : m_Path(path)
    {
        InitFace(path);
    }

    FontAtlas::FontAtlas(const std::string& path, const Options& options)
        : m_Path(path), m_LineSpacing(options.LineSpacing)
    {
        InitFace(path);
        if (!m_Face)
            return;
        m_LineHeight *= m_LineSpacing;
        FT_Library lib = (FT_Library)m_Library;
        for (const auto& fallback : options.Fallbacks)
        {
            FT_Face face = nullptr;
            if (FT_New_Face(lib, SceneSerializer::ResolveAssetPath(fallback).c_str(), 0, &face) != 0)
            {
                CQ_CORE_WARN("FontAtlas: cannot load fallback font {0}", fallback);
                continue;
            }
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)BaseSize);
            m_Fallbacks.push_back(face);
        }
        std::vector<uint32_t> cps;
        DecodeUTF8(options.Characters, cps);
        for (uint32_t c : cps)
            GetGlyph(c);
        // Statik atlas: başlangıçta üretilen ASCII yalnız Characters boşsa kalır
        if (options.Static && !cps.empty())
            for (auto it = m_Glyphs.begin(); it != m_Glyphs.end();)
            {
                if (std::find(cps.begin(), cps.end(), it->first) == cps.end())
                    it = m_Glyphs.erase(it);
                else
                    ++it;
            }
        m_Static = options.Static;
    }

    FontAtlas::FontAtlas(const BitmapOptions& bitmap)
        : m_Path(bitmap.Texture)
    {
        m_LineSpacing = bitmap.LineSpacing;
        m_Bitmap = Texture2D::Create(SceneSerializer::ResolveAssetPath(bitmap.Texture));
        if (!m_Bitmap || m_Bitmap->GetWidth() == 0)
        {
            CQ_CORE_ERROR("FontAtlas: Custom Font texture not found: {0}", bitmap.Texture);
            m_Bitmap = nullptr;
            return;
        }
        const int columns = std::max(1, bitmap.Columns), rows = std::max(1, bitmap.Rows);
        const float cellW = (float)m_Bitmap->GetWidth() / (float)columns, cellH = (float)m_Bitmap->GetHeight() / (float)rows;
        const float width = BaseSize * cellW / cellH;
        m_Ascender = BaseSize * 0.8f;
        m_Descender = -BaseSize * 0.2f;
        m_LineHeight = BaseSize * m_LineSpacing;
        std::vector<uint32_t> cps;
        DecodeUTF8(bitmap.Characters, cps);
        for (size_t i = 0; i < cps.size() && i < (size_t)columns * rows; ++i)
        {
            const int col = (int)i % columns, row = (int)i / columns;
            SDFGlyph g;
            // Dokular alttan yukarı: ilk satır üstte
            g.UVMin = { (float)col / columns, 1.0f - (float)(row + 1) / rows };
            g.UVMax = { (float)(col + 1) / columns, 1.0f - (float)row / rows };
            float ratio = 1.0f;
            for (const auto& [c, w] : bitmap.Widths)
                if (c == cps[i])
                    ratio = w;
            g.Size = { width, BaseSize };
            g.Bearing = { 0.0f, m_Ascender };
            g.Advance = width * ratio + bitmap.Spacing * BaseSize;
            g.Valid = true;
            m_Glyphs[cps[i]] = g;
        }
        m_Static = true;
        m_Dirty = false;
    }

    void FontAtlas::InitFace(const std::string& path)
    {

        FT_Library lib = nullptr;
        if (FT_Init_FreeType(&lib) != 0)
        {
            CQ_CORE_ERROR("FontAtlas: FreeType init failed");
            return;
        }
        m_Library = lib;
        FT_Face face = nullptr;
        if (FT_New_Face(lib, path.c_str(), 0, &face) != 0)
        {
            CQ_CORE_ERROR("FontAtlas: cannot load font {0}", path);
            return;
        }
        m_Face = face;
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)BaseSize);
        // SDF yayılımı (piksel)
        FT_Int spread = 6;
        FT_Property_Set(lib, "sdf", "spread", &spread);
        m_Ascender = face->size->metrics.ascender / 64.0f;
        m_Descender = face->size->metrics.descender / 64.0f;
        m_LineHeight = face->size->metrics.height / 64.0f;
        m_HasKerning = FT_HAS_KERNING(face);
        m_Pixels.assign((size_t)m_Width * m_Height * 4, 0);
        for (size_t i = 0; i < m_Pixels.size(); i += 4)
        {
            m_Pixels[i] = m_Pixels[i + 1] = m_Pixels[i + 2] = 255;
        }
        // Sık kullanılan glifleri önceden yükle (ASCII + Türkçe)
        for (uint32_t c = 32; c < 127; ++c)
            GetGlyph(c);
        for (uint32_t c : { 0x00E7u, 0x00C7u, 0x011Fu, 0x011Eu, 0x0131u, 0x0130u, 0x00F6u, 0x00D6u, 0x015Fu, 0x015Eu, 0x00FCu, 0x00DCu })
            GetGlyph(c);
    }

    FontAtlas::~FontAtlas()
    {
        for (void* face : m_Fallbacks)
            FT_Done_Face((FT_Face)face);
        if (m_Face) FT_Done_Face((FT_Face)m_Face);
        if (m_Library) FT_Done_FreeType((FT_Library)m_Library);
    }

    namespace
    {
        // Varlıktan yazı tipi; dosya değişince yeniden üretilir (eskisi önbellekte kalır: çizimde kullanılıyor olabilir)
        std::shared_ptr<FontAtlas> GetFromAsset(const std::string& path, bool bitmap)
        {
            std::error_code ec;
            const std::string file = SceneSerializer::ResolveAssetPath(path);
            const auto time = std::filesystem::last_write_time(file, ec);
            const std::string key = file + "#" + std::to_string(ec ? 0 : (long long)time.time_since_epoch().count());
            auto& cache = Cache();
            if (auto it = cache.find(key); it != cache.end())
                return it->second;
            SimpleAssets::Data data;
            std::shared_ptr<FontAtlas> atlas;
            if (SimpleAssets::Load(file, data))
            {
                if (bitmap)
                {
                    FontAtlas::BitmapOptions o;
                    o.Texture = data.Str("Texture");
                    o.Characters = data.Str("Characters");
                    o.Columns = data.Int("Columns", 16);
                    o.Rows = data.Int("Rows", 16);
                    o.Spacing = data.Float("Spacing");
                    o.LineSpacing = data.Float("Line Spacing", 1.0f);
                    for (const auto& row : data.Table("Character Widths"))
                    {
                        std::vector<uint32_t> cps;
                        FontAtlas::DecodeUTF8(SimpleAssets::Cell(row, 0), cps);
                        if (!cps.empty())
                            o.Widths.emplace_back(cps.front(), SimpleAssets::CellFloat(row, 1, 1.0f));
                    }
                    atlas = std::make_shared<FontAtlas>(o);
                }
                else
                {
                    FontAtlas::Options o;
                    o.Static = data.Str("Atlas Population") == "Static";
                    o.Characters = data.Str("Characters");
                    o.LineSpacing = data.Float("Line Spacing", 1.0f);
                    for (const auto& row : data.Table("Fallback Fonts"))
                        if (const std::string f = SimpleAssets::Cell(row, 0); !f.empty())
                            o.Fallbacks.push_back(f);
                    atlas = std::make_shared<FontAtlas>(SceneSerializer::ResolveAssetPath(data.Str("Source Font")), o);
                }
            }
            if (!atlas || !atlas->IsValid())
                return FontAtlas::Get("");
            cache[key] = atlas;
            return atlas;
        }
    }

    std::shared_ptr<FontAtlas> FontAtlas::Get(const std::string& path)
    {
        const std::string ext = std::filesystem::path(path).extension().string();
        if (ext == ".cqfontasset" || ext == ".cqcustomfont")
            return GetFromAsset(path, ext == ".cqcustomfont");
        std::string key = path.empty() ? DefaultFontPath() : path;
        auto& cache = Cache();
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        auto atlas = std::make_shared<FontAtlas>(key);
        if (!atlas->IsValid() && !path.empty())
            return Get(""); // geçersiz yazı tipi: varsayılana düş
        cache[key] = atlas;
        return atlas;
    }

    void FontAtlas::ShutdownAll()
    {
        Cache().clear();
    }

    bool FontAtlas::Rasterize(uint32_t codepoint, SDFGlyph& out)
    {
        FT_Face face = (FT_Face)m_Face;
        if (!face)
            return false;
        FT_UInt index = FT_Get_Char_Index(face, codepoint);
        // Eksik karakter: yedek yazı tipleri (Font Asset Fallback listesi)
        if (index == 0)
            for (void* fallback : m_Fallbacks)
                if (FT_UInt i = FT_Get_Char_Index((FT_Face)fallback, codepoint); i != 0)
                {
                    face = (FT_Face)fallback;
                    index = i;
                    break;
                }
        if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) != 0)
            return false;
        out.Advance = face->glyph->advance.x / 64.0f;
        if (face->glyph->outline.n_points == 0)
        {
            out.Valid = true; // boşluk vb.
            return true;
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0)
        {
            // SDF desteklenmiyorsa normal kenar yumuşatma
            if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
                return false;
        }
        const FT_Bitmap& bmp = face->glyph->bitmap;
        int w = (int)bmp.width, h = (int)bmp.rows;
        if (m_CursorX + w + 1 >= m_Width)
        {
            m_CursorX = 1;
            m_CursorY += m_RowHeight + 1;
            m_RowHeight = 0;
        }
        if (m_CursorY + h + 1 >= m_Height)
        {
            if (m_Height >= 4096)
            {
                CQ_CORE_WARN("FontAtlas: atlas full ({0})", m_Path);
                return false;
            }
            // Atlası iki katına çıkar (yükseklik), mevcut UV'leri ölçekle
            int newH = m_Height * 2;
            m_Pixels.resize((size_t)m_Width * newH * 4, 0);
            for (size_t i = (size_t)m_Width * m_Height * 4; i < m_Pixels.size(); i += 4)
                m_Pixels[i] = m_Pixels[i + 1] = m_Pixels[i + 2] = 255;
            float s = (float)m_Height / newH;
            for (auto& [cp, g] : m_Glyphs)
            {
                g.UVMin.y *= s;
                g.UVMax.y *= s;
            }
            m_Height = newH;
            m_Texture.reset();
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint8_t v = bmp.buffer[y * bmp.pitch + x];
                // Doku satırı 0 = alt (yükleyici kuralıyla aynı): glifi dikey çevirerek yaz
                size_t dst = ((size_t)(m_CursorY + (h - 1 - y)) * m_Width + (m_CursorX + x)) * 4;
                m_Pixels[dst + 3] = v;
            }
        out.Size = glm::vec2((float)w, (float)h);
        out.Bearing = glm::vec2((float)face->glyph->bitmap_left, (float)face->glyph->bitmap_top);
        out.UVMin = glm::vec2((float)m_CursorX / m_Width, (float)m_CursorY / m_Height);
        out.UVMax = glm::vec2((float)(m_CursorX + w) / m_Width, (float)(m_CursorY + h) / m_Height);
        out.Valid = true;
        m_CursorX += w + 1;
        m_RowHeight = std::max(m_RowHeight, h);
        m_Dirty = true;
        return true;
    }

    const SDFGlyph& FontAtlas::GetGlyph(uint32_t codepoint)
    {
        auto it = m_Glyphs.find(codepoint);
        if (it != m_Glyphs.end())
            return it->second;
        // Statik atlas (Font Asset / Custom Font): listede olmayan karakter üretilmez
        if (m_Static)
        {
            if (codepoint != '?' && m_Glyphs.count('?'))
                return m_Glyphs.at('?');
            return s_Empty;
        }
        SDFGlyph g;
        if (!Rasterize(codepoint, g))
        {
            if (codepoint != '?')
                return GetGlyph('?');
            return s_Empty;
        }
        return m_Glyphs.emplace(codepoint, g).first->second;
    }

    float FontAtlas::GetKerning(uint32_t left, uint32_t right) const
    {
        if (!m_HasKerning || !m_Face)
            return 0.0f;
        FT_Face face = (FT_Face)m_Face;
        FT_Vector k;
        if (FT_Get_Kerning(face, FT_Get_Char_Index(face, left), FT_Get_Char_Index(face, right), FT_KERNING_DEFAULT, &k) != 0)
            return 0.0f;
        return k.x / 64.0f;
    }

    std::shared_ptr<Texture2D> FontAtlas::GetTexture()
    {
        if (m_Bitmap)
            return m_Bitmap;
        if (!m_Texture || m_Texture->GetHeight() != (uint32_t)m_Height)
        {
            m_Texture = Texture2D::Create((uint32_t)m_Width, (uint32_t)m_Height);
            m_Dirty = true;
        }
        if (m_Dirty)
        {
            m_Texture->SetData(m_Pixels.data(), (uint32_t)m_Pixels.size());
            m_Dirty = false;
        }
        return m_Texture;
    }

    void FontAtlas::DecodeUTF8(const std::string& text, std::vector<uint32_t>& out)
    {
        out.clear();
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();)
        {
            unsigned char c = (unsigned char)text[i];
            uint32_t cp = 0;
            int len = 1;
            if (c < 0x80) cp = c;
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else { ++i; continue; }
            if (i + (size_t)len > text.size())
                break;
            for (int k = 1; k < len; ++k)
                cp = (cp << 6) | ((unsigned char)text[i + (size_t)k] & 0x3F);
            out.push_back(cp);
            i += (size_t)len;
        }
    }

    float FontAtlas::MeasureWidth(const std::vector<uint32_t>& cps, size_t begin, size_t end, float fontSize)
    {
        float scale = fontSize / BaseSize;
        float w = 0.0f;
        for (size_t i = begin; i < end && i < cps.size(); ++i)
        {
            w += GetGlyph(cps[i]).Advance * scale;
            if (i + 1 < end)
                w += GetKerning(cps[i], cps[i + 1]) * scale;
        }
        return w;
    }
}
