#pragma once

// GL çeviri katmanlarının API'den bağımsız texel dönüşümleri (MtlGL, WebGPU). GL iç biçimi -> texel türü,
// GL kaynak verisi -> sıkı paketlenmiş satırlar, geri okuma, glClearTexImage değeri.

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Conqueror::GLEmu
{
    enum class TexelKind : uint8_t
    {
        RGBA8, RGBA8_SRGB, R8, RG8, RGBA16F, RGBA32F, R16F, RG16F, R32F, RG32F, RG11B10F, RGBA16, R16, RG16,
        R32I, R32UI, RG32I, RGBA32I, R8I, R8UI,
        D16, D32F, D24S8, D32FS8
    };

    struct TexelInfo
    {
        TexelKind Kind = TexelKind::RGBA8;
        int Channels = 4;
        int Bytes = 4;                                   // texel başına bayt (derinlikte yalnızca derinlik düzlemi)
        bool Depth = false, Stencil = false, Integer = false;
    };

    TexelInfo TexelOf(GLenum internalFormat);
    TexelInfo TexelInfoOf(TexelKind kind);
    uint32_t MaxLevels(uint32_t w, uint32_t h);
    int GLChannels(GLenum format);
    int GLTypeSize(GLenum type);

    // GL kaynak verisini texel türünün satırlarına çevirir (sıkı paket: genişlik * Bytes). Dönüşüm gerekmiyorsa data'yı döndürür.
    const uint8_t* ConvertUpload(TexelKind dst, int w, int h, GLenum format, GLenum type, const void* data, int unpackRowLength, int unpackAlignment,
                                 std::vector<uint8_t>& staging, size_t& pitch);
    // Texel satırlarını (src, srcPitch) GL biçimine yazar
    void ConvertReadback(TexelKind src, const uint8_t* data, size_t srcPitch, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    // glClearTexImage verisi -> renk / derinlik + şablon
    void ParseClearValue(bool depth, bool integer, GLenum format, GLenum type, const void* data, float color[4], float& depthValue, uint8_t& stencil);
}
