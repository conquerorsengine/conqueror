#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    enum class TextureType : int { Default = 0, NormalMap, Sprite, UI };
    enum class TextureFilter : int { Point = 0, Bilinear, Trilinear };
    enum class TextureWrap : int { Repeat = 0, Clamp, Mirror };
    enum class TextureShape : int { Texture2D = 0, Cube };
    enum class SpriteImportMode : int { Single = 0, Multiple, Polygon };
    enum class TextureResizeAlgorithm : int { Mitchell = 0, Bilinear };
    enum class TextureFormatChoice : int { Automatic = 0, RGBA32, RGB24, R8 };
    enum class TextureCompression : int { None = 0, LowQuality, NormalQuality, HighQuality };
    enum class TexturePlatform : int { PC = 0, Web, Mobile };
    inline constexpr int kTexturePlatformCount = 3;

    // Platform sekmesi (Unity Default / PC / Web): Override açıksa bu platformda varsayılanın yerine geçer
    struct CQ_API PlatformTextureSettings
    {
        bool Override = false;
        int MaxSize = 4096;
        TextureResizeAlgorithm Resize = TextureResizeAlgorithm::Mitchell;
        TextureFormatChoice Format = TextureFormatChoice::Automatic;
        TextureCompression Compression = TextureCompression::None;
        bool UseCrunch = false;
        int CrunchQuality = 50;
        bool operator==(const PlatformTextureSettings& o) const
        {
            return Override == o.Override && MaxSize == o.MaxSize && Resize == o.Resize && Format == o.Format && Compression == o.Compression &&
                   UseCrunch == o.UseCrunch && CrunchQuality == o.CrunchQuality;
        }
    };

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
        TextureShape Shape = TextureShape::Texture2D;   // Cube: görselden .cqcubemap üretilir
        SpriteImportMode SpriteMode = SpriteImportMode::Single;
        int PolygonSides = 6;            // Sprite Mode: Polygon
        bool TightMesh = true;           // Mesh Type: Tight (false: Full Rect)
        int ExtrudeEdges = 1;            // çizim ağını piksel olarak dışa genişletir (0-32)
        bool GeneratePhysicsShape = true;
        TextureResizeAlgorithm Resize = TextureResizeAlgorithm::Mitchell;
        TextureFormatChoice Format = TextureFormatChoice::Automatic;
        TextureCompression Compression = TextureCompression::None;
        bool UseCrunch = false;          // oyun derlenirken kayıplı boyut küçültme
        int CrunchQuality = 50;
        // Unity Streaming Mipmaps: Texture Streaming açıkken (Quality) doku yalnız kameraların gerektirdiği mip seviyesine
        // kadar GPU'ya yüklenir; bütçe aşılınca önce düşük öncelikliler küçülür
        bool StreamingMipmaps = false;
        int StreamingPriority = 0;       // Mip Map Priority (-128..127)
        // Unity Read/Write: görselin pikselleri oyunda da okunabilir kalır (betik, sprite fizik dış hattı ...). Kapalıyken Crunch'lanan
        // doku oyuna yalnız .crn olarak girer (daha küçük derleme)
        bool ReadWrite = false;
        PlatformTextureSettings Platforms[kTexturePlatformCount];

        bool operator==(const TextureImportSettings& o) const;
        bool operator!=(const TextureImportSettings& o) const { return !(*this == o); }

        // Türün önerdiği varsayılanlar (Sprite/UI: mipmap yok, kenara kıstır)
        static TextureImportSettings DefaultsFor(TextureType type);
        // Çalışılan platform ve o platformun geçerli ayarları (Override açıksa boyut / biçim / sıkıştırma onun)
        static TexturePlatform CurrentPlatform();
        TextureImportSettings ForPlatform(TexturePlatform platform) const;
        static std::string MetaPath(const std::string& texturePath);
        // Yan dosyayı oku (yoksa false ve varsayılanlar)
        static bool Load(const std::string& texturePath, TextureImportSettings& out);
        bool Save(const std::string& texturePath) const;

        static const char* TypeName(TextureType type);
        static const char* FilterName(TextureFilter filter);
        static const char* WrapName(TextureWrap wrap);
    };
}
