#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    enum class TextureType : int { Default = 0, NormalMap, Sprite, UI };
    enum class TextureFilter : int { Point = 0, Bilinear, Trilinear };
    enum class TextureWrap : int { Repeat = 0, Clamp, Mirror };

    // Doku içe aktarma ayarları (Unity Texture Import Settings). "<doku>.meta" yan dosyasında saklanır;
    // Texture2D::Create okur, editör Apply ile yazar ve dokuyu yerinde yeniden yükler.
    struct CQ_API TextureImportSettings
    {
        TextureType Type = TextureType::Default;
        TextureFilter Filter = TextureFilter::Trilinear;
        TextureWrap Wrap = TextureWrap::Repeat;
        bool Mipmaps = true;
        int Anisotropy = 8;              // 1 (kapalı) - 16
        int MaxSize = 4096;              // daha büyük dokular en-boy oranı korunarak küçültülür
        bool NormalFromGrayscale = false; // NormalMap türü: yükseklik (gri) haritasından normal üret
        float Bumpiness = 2.0f;
        float PixelsPerUnit = 100.0f;    // Sprite türü için varsayılan

        bool operator==(const TextureImportSettings& o) const;
        bool operator!=(const TextureImportSettings& o) const { return !(*this == o); }

        // Türün önerdiği varsayılanlar (Sprite/UI: mipmap yok, kenara kıstır)
        static TextureImportSettings DefaultsFor(TextureType type);
        static std::string MetaPath(const std::string& texturePath);
        // Yan dosyayı oku (yoksa false ve varsayılanlar)
        static bool Load(const std::string& texturePath, TextureImportSettings& out);
        bool Save(const std::string& texturePath) const;

        static const char* TypeName(TextureType type);
        static const char* FilterName(TextureFilter filter);
        static const char* WrapName(TextureWrap wrap);
    };
}
