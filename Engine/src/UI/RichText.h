#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror { class Texture2D; }

// Zengin metin (Godot RichTextLabel BBCode + TextMeshPro etiketleri): [b] [i] [u] [s] [color=#ff8800|red]
// [size=24] / [font_size=24] [wave amp=.. freq=..] [shake rate=.. level=..] [rainbow freq=..] [lb] [rb];
// aynıları <b> <i> <u> <s> <color=...> <size=...> biçiminde de yazılabilir. Tanınmayan etiket metin olarak kalır.
// [glyph=a|key_space] / <glyph=Jump>: oyuncunun aygıtına göre tuş simgesi (InputGlyphs; kumandada A, klavyede Space).
namespace Conqueror::RichText
{
    enum StyleFlags : uint32_t
    {
        Bold = 1u << 0,
        Italic = 1u << 1,
        Underline = 1u << 2,
        Strike = 1u << 3,
        Wave = 1u << 4,
        Shake = 1u << 5,
        Rainbow = 1u << 6,
    };

    struct Style
    {
        glm::vec4 Color{ 1.0f };
        bool HasColor = false;           // false: bileşenin rengi
        float Size = 0.0f;               // 0: bileşenin boyutu; piksel
        uint32_t Flags = 0;
        float WaveAmplitude = 8.0f;      // piksel (Godot amp=20 ~ 1/2.5 satır)
        float WaveFrequency = 5.0f;
        float ShakeLevel = 3.0f;         // piksel
        float ShakeRate = 20.0f;
        float RainbowFrequency = 0.5f;
        const glm::vec4* Gradient = nullptr;   // <gradient=Ad>: sol alt, sağ alt, sağ üst, sol üst
    };

    // Color Gradient varlığı (.cqcolorgradient) köşe renkleri: ad (dosya adı) ya da yol; bulunamazsa nullptr
    CQ_API const glm::vec4* FindGradient(const std::string& nameOrPath);
    // Sprite Asset (<sprite name=X>) dokusu ve ölçeği
    CQ_API std::shared_ptr<Conqueror::Texture2D> SpriteTexture(const std::string& name, float* scale = nullptr);

    // Tuş simgeleri metinde özel kod noktası olarak taşınır (Unicode özel kullanım alanı, düzlem 16)
    CQ_API uint32_t GlyphCodepoint(const std::string& spec);
    CQ_API bool IsGlyph(uint32_t codepoint);
    CQ_API std::string GlyphSpec(uint32_t codepoint);

    // Metni kod noktalarına ve kod noktası başına stile çözer
    CQ_API void Parse(const std::string& text, std::vector<uint32_t>& codepoints, std::vector<Style>& styles);
    // Etiketleri atılmış düz metin (ölçüm / erişilebilirlik / arama)
    CQ_API std::string Strip(const std::string& text);
    // "#rgb", "#rrggbb", "#rrggbbaa" ya da ad (red, green, ...) -> renk
    CQ_API bool ParseColor(const std::string& value, glm::vec4& out);
}
