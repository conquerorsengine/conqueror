#include "Font.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Conqueror
{
    std::shared_ptr<Font> Font::s_DefaultFont = nullptr;

    Font::Font(const std::string& filepath)
    {
        LoadFont(filepath);
    }

    Font::~Font()
    {
        if (m_Face)
        {
            FT_Done_Face((FT_Face)m_Face);
        }
        if (m_FTLibrary)
        {
            FT_Done_FreeType((FT_Library)m_FTLibrary);
        }
    }

    void Font::LoadFont(const std::string& filepath)
    {
        FT_Library ft;
        if (FT_Init_FreeType(&ft))
        {
            CQ_CORE_ERROR("FreeType: Could not init FreeType Library");
            return;
        }

        FT_Face face;
        if (FT_New_Face(ft, filepath.c_str(), 0, &face))
        {
            CQ_CORE_ERROR("FreeType: Failed to load font: {0}", filepath);
            FT_Done_FreeType(ft);
            return;
        }

        // Font boyutu ayarla (48 pixel)
        FT_Set_Pixel_Sizes(face, 0, 48);
        
        // FreeType library ve face'i sakla
        m_FTLibrary = ft;
        m_Face = face;

        CQ_CORE_INFO("Font loaded: {0}", filepath);
    }

    Glyph Font::LoadGlyph(wchar_t character) const
    {
        FT_Face face = (FT_Face)m_Face;
        
        if (FT_Load_Char(face, character, FT_LOAD_RENDER))
        {
            CQ_CORE_WARN("FreeType: Failed to load Glyph '{0}'", (int)character);
            // Fallback: boş glyph
            return Glyph{ nullptr, glm::ivec2(0), glm::ivec2(0), 0 };
        }

        const FT_Bitmap& bitmap = face->glyph->bitmap;
        const int w = (int)bitmap.width, h = (int)bitmap.rows;
        Glyph glyph;
        glyph.Size = glm::ivec2(w, h);
        glyph.Bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
        glyph.Advance = static_cast<uint32_t>(face->glyph->advance.x);

        // Kapsama değeri dört kanala (gölgelendirici .r okur); satır adımı FreeType'ın pitch'i
        std::vector<uint32_t> rgba((size_t)std::max(w, 1) * (size_t)std::max(h, 1), 0u);
        if (bitmap.buffer && w > 0 && h > 0)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const uint32_t value = bitmap.buffer[(size_t)y * (size_t)std::abs(bitmap.pitch) + (size_t)x];
                    rgba[(size_t)y * (size_t)w + (size_t)x] = (value << 24) | (value << 16) | (value << 8) | value;
                }

        if (w > 0 && h > 0)
        {
            if (!m_Atlas)
            {
                m_Atlas = Texture2D::Create(kAtlasSize, kAtlasSize);
                std::vector<uint32_t> clear((size_t)kAtlasSize * kAtlasSize, 0u);
                m_Atlas->SetData(clear.data(), (uint32_t)(clear.size() * sizeof(uint32_t)));
            }
            // Raf yerleşimi, glifler arasında 1 piksel boşluk (çift doğrusal süzme komşuya taşmasın)
            if (m_AtlasX + w + 1 > kAtlasSize)
            {
                m_AtlasX = 1;
                m_AtlasY += m_AtlasRowHeight + 1;
                m_AtlasRowHeight = 0;
            }
            if (m_AtlasY + h + 1 <= kAtlasSize && w + 2 <= kAtlasSize)
            {
                glTextureSubImage2D(m_Atlas->GetRendererID(), 0, m_AtlasX, m_AtlasY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                glyph.Texture = m_Atlas;
                glyph.UV0 = glm::vec2((float)m_AtlasX, (float)m_AtlasY) / (float)kAtlasSize;
                glyph.UV1 = glm::vec2((float)(m_AtlasX + w), (float)(m_AtlasY + h)) / (float)kAtlasSize;
                m_AtlasX += w + 1;
                m_AtlasRowHeight = std::max(m_AtlasRowHeight, h);
                return glyph;
            }
        }

        // Boş glif (boşluk): yalnız ilerleme, doku yok. Atlas doluysa glifin kendi dokusu
        if (w <= 0 || h <= 0)
            return glyph;
        glyph.Texture = Texture2D::Create((uint32_t)w, (uint32_t)h);
        glyph.Texture->SetData(rgba.data(), (uint32_t)((size_t)w * (size_t)h * sizeof(uint32_t)));
        return glyph;
    }

    const Glyph& Font::GetGlyph(char character) const
    {
        return GetGlyph((wchar_t)character);
    }

    const Glyph& Font::GetGlyph(wchar_t character) const
    {
        // Cache'de var mı kontrol et
        auto it = m_Glyphs.find(character);
        if (it != m_Glyphs.end())
            return it->second;
        
        // Yoksa yükle ve cache'e ekle
        Glyph glyph = LoadGlyph(character);
        m_Glyphs[character] = glyph;
        
        return m_Glyphs[character];
    }

    std::shared_ptr<Font> Font::GetDefault()
    {
        if (!s_DefaultFont)
        {
            try
            {
                s_DefaultFont = std::make_shared<Font>("Resources/Fonts/opensans/OpenSans-Regular.ttf");
            }
            catch (...)
            {
                CQ_CORE_ERROR("Failed to load default font, text rendering disabled");
                return nullptr;
            }
        }
        return s_DefaultFont;
    }
}
