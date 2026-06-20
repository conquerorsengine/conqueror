#include "Font.h"
#include "Core/Logging/Log.h"

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

        std::shared_ptr<Texture2D> texture = Texture2D::Create(
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows
        );

        if (face->glyph->bitmap.buffer && face->glyph->bitmap.width > 0 && face->glyph->bitmap.rows > 0)
        {
            uint32_t pixelCount = face->glyph->bitmap.width * face->glyph->bitmap.rows;
            uint32_t* rgbaData = new uint32_t[pixelCount];
            
            for (uint32_t i = 0; i < pixelCount; i++)
            {
                uint8_t value = face->glyph->bitmap.buffer[i];
                rgbaData[i] = (value << 24) | (value << 16) | (value << 8) | value;
            }
            
            texture->SetData(rgbaData, pixelCount * sizeof(uint32_t));
            delete[] rgbaData;
        }

        Glyph glyph = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<uint32_t>(face->glyph->advance.x)
        };

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
