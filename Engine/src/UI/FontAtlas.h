#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    class Texture2D;

    struct SDFGlyph
    {
        glm::vec2 UVMin{ 0.0f }, UVMax{ 0.0f };
        glm::vec2 Size{ 0.0f };      // piksel (temel boyutta, SDF yayılımı dahil)
        glm::vec2 Bearing{ 0.0f };   // sol, üst (temel boyutta)
        float Advance = 0.0f;        // temel boyutta
        bool Valid = false;
    };

    // FreeType SDF glif atlası: glifler istendikçe tek dokuya paketlenir.
    // Metin her boyutta keskin çizilir (shader'da smoothstep) ve Renderer2D ile toplu çizilir.
    class CQ_API FontAtlas
    {
    public:
        static constexpr float BaseSize = 48.0f;

        explicit FontAtlas(const std::string& path);
        ~FontAtlas();

        // Boş yol: varsayılan (OpenSans Regular)
        static std::shared_ptr<FontAtlas> Get(const std::string& path = "");
        static void ShutdownAll();

        bool IsValid() const { return m_Face != nullptr; }
        const SDFGlyph& GetGlyph(uint32_t codepoint);
        float GetKerning(uint32_t left, uint32_t right) const;
        std::shared_ptr<Texture2D> GetTexture();   // gerekirse yükler

        float GetAscender() const { return m_Ascender; }
        float GetDescender() const { return m_Descender; }
        float GetLineHeight() const { return m_LineHeight; }
        const std::string& GetPath() const { return m_Path; }

        // UTF-8 -> kod noktaları
        static void DecodeUTF8(const std::string& text, std::vector<uint32_t>& out);
        // Tek satır genişliği (verilen piksel boyutunda)
        float MeasureWidth(const std::vector<uint32_t>& codepoints, size_t begin, size_t end, float fontSize);

    private:
        bool Rasterize(uint32_t codepoint, SDFGlyph& out);

        std::string m_Path;
        void* m_Library = nullptr;
        void* m_Face = nullptr;
        std::unordered_map<uint32_t, SDFGlyph> m_Glyphs;
        std::vector<uint8_t> m_Pixels;               // RGBA (RGB beyaz, A = mesafe)
        int m_Width = 1024, m_Height = 1024;
        int m_CursorX = 1, m_CursorY = 1, m_RowHeight = 0;
        bool m_Dirty = true;
        std::shared_ptr<Texture2D> m_Texture;
        float m_Ascender = 0.0f, m_Descender = 0.0f, m_LineHeight = 0.0f;
        bool m_HasKerning = false;
    };
}
