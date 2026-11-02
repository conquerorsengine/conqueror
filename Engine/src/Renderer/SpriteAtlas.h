#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;

    // Sprite Atlas (Unity Sprite Atlas): listedeki görseller (ve klasörlerdeki tüm görseller) tek dokuya paketlenir.
    // Sprite Renderer'lar bu görselleri çizerken atlas dokusunu kullanır (daha az doku değişimi / çizim çağrısı).
    // Paket "<atlas>.cqspriteatlas.png" olarak varlığın yanına yazılır (oyunla birlikte gider), yerleşimler varlık dosyasında.
    struct SpriteAtlasAsset
    {
        std::vector<std::string> Objects;   // görsel ya da klasör yolları (proje göreli)
        int Padding = 4;                    // sprite'lar arası piksel (kenarlar taşırılarak doldurulur)
        int MaxSize = 2048;
        bool PointFilter = false;           // piksel sanatı
        bool IncludeInBuild = true;         // kapalıysa çalışma zamanında atlas kullanılmaz
        // Paketleme sonucu
        struct Entry { std::string Source; glm::ivec4 Rect{ 0 }; };   // proje göreli kaynak, atlas pikseli (x, y sol-alt, w, h)
        std::vector<Entry> Packed;
        glm::ivec2 PackedSize{ 0 };
    };

    namespace SpriteAtlases
    {
        constexpr const char* Extension = ".cqspriteatlas";

        CQ_API bool Load(const std::string& path, SpriteAtlasAsset& out);
        CQ_API bool Save(const std::string& path, const SpriteAtlasAsset& atlas);
        // Görselleri topla, paketle, PNG'yi ve yerleşimleri yaz. Hata olursa false (error doldurulur)
        CQ_API bool Pack(const std::string& path, std::string* error = nullptr);
        // Kaynaklar paketten yeniyse (ya da paket yoksa) yeniden paketle; projedeki tüm atlaslar (Play / Build öncesi)
        CQ_API int PackAllIfStale();
        CQ_API std::string PackedTexturePath(const std::string& atlasPath);

        // Çalışma zamanı: görsel bir atlasa paketlenmişse atlas dokusu ve görselin atlas içindeki dikdörtgeni
        struct Lookup
        {
            std::shared_ptr<Texture2D> Texture;
            glm::vec2 Offset{ 0.0f };   // piksel
            glm::vec2 Size{ 0.0f };     // atlas boyutu
        };
        CQ_API bool Find(const std::string& sourceTexturePath, Lookup& out);
        // Atlas dosyaları değişti (paketleme, silme): tablo yeniden kurulur
        CQ_API void Invalidate();
        CQ_API void Shutdown();
    }
}
