#include "FontAtlas.h"
#include "Core/Logging/Log.h"
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
        if (m_Face) FT_Done_Face((FT_Face)m_Face);
        if (m_Library) FT_Done_FreeType((FT_Library)m_Library);
    }

    std::shared_ptr<FontAtlas> FontAtlas::Get(const std::string& path)
    {
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
