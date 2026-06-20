#pragma once

#include "RHI/Texture.h"
#include <glm/glm.hpp>

#include <string>
#include <memory>
#include <unordered_map>

namespace Conqueror
{
    struct Glyph
    {
        std::shared_ptr<Texture2D> Texture;
        glm::ivec2 Size;
        glm::ivec2 Bearing;
        uint32_t Advance;
    };

    class Font
    {
    public:
        Font(const std::string& filepath);
        ~Font();

        const Glyph& GetGlyph(char character) const;
        const Glyph& GetGlyph(wchar_t character) const;
        
        static std::shared_ptr<Font> GetDefault();

    private:
        void LoadFont(const std::string& filepath);
        Glyph LoadGlyph(wchar_t character) const;

    private:
        mutable std::unordered_map<wchar_t, Glyph> m_Glyphs;
        
        void* m_FTLibrary = nullptr; // FT_Library
        void* m_Face = nullptr;      // FT_Face
        
        static std::shared_ptr<Font> s_DefaultFont;
    };
}
