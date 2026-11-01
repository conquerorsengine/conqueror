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
        std::shared_ptr<Texture2D> Texture;     // yazı tipinin glif atlası (paylaşılan); atlasa sığmayan glifin kendi dokusu
        glm::ivec2 Size{};
        glm::ivec2 Bearing{};
        uint32_t Advance{};
        glm::vec2 UV0{ 0.0f }, UV1{ 1.0f };     // dokudaki bölge: sol üst, sağ alt
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
        // Glif atlası: glifler yüklendikçe raf usulü yerleşir (metin tek dokuyla, tek çizim çağrısında çizilir)
        static constexpr int kAtlasSize = 1024;
        mutable std::shared_ptr<Texture2D> m_Atlas;
        mutable int m_AtlasX = 1, m_AtlasY = 1, m_AtlasRowHeight = 0;
        
        void* m_FTLibrary = nullptr; // FT_Library
        void* m_Face = nullptr;      // FT_Face
        
        static std::shared_ptr<Font> s_DefaultFont;
    public:
        static void ReleaseDefault() { s_DefaultFont.reset(); }
    };
}
