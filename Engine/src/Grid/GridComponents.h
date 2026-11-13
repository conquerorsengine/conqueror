#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Izgara tabanlı oyun sistemi (Unity Grid + Tilemap, altıgen desteği):
// dikdörtgen, sivri/düz tepeli altıgen ve izometrik yerleşimler; karo paleti; 2D/3D çizim;
// karo çarpıştırıcısı; komşular, mesafe ve A* yol bulma.
namespace Conqueror
{
    enum class GridLayout : int { Rectangle = 0, HexagonPointyTop, HexagonFlatTop, Isometric };
    enum class GridPlane : int { XY = 0, XZ };   // XY: 2D oyunlar; XZ: 3D masa/tahta oyunları

    struct GridComponent
    {
        GridLayout Layout = GridLayout::Rectangle;
        glm::vec2 CellSize{ 1.0f, 1.0f };   // altıgen: genişlik (düz kenarlar arası) / yükseklik (köşeler arası)
        glm::vec2 CellGap{ 0.0f, 0.0f };
        GridPlane Plane = GridPlane::XY;
        bool ShowGridInEditor = true;
        int EditorGridExtent = 12;          // düzenleyicide çizilen hücre yarıçapı
    };

    // Rule Tile kuralı (Unity Rule Tile): 8 komşu deseni eşleşirse çıktı dokusu kullanılır
    struct TileRule
    {
        // Komşular K, KD, D, GD, G, GB, B, KB (saat yönü, kuzeyden): 0 = fark etmez, 1 = bu karo, 2 = bu karo değil
        int Neighbors[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        std::string Texture;   // boş = karonun dokusu
        int Rotation = 0;      // 0 sabit; 1 döndürülmüş (desen 90°'lik dönüşlerle de denenir, doku birlikte döner)
    };

    struct TileDefinition
    {
        std::string Name = "Tile";
        glm::vec4 Color{ 1.0f };
        std::string Texture;
        bool Solid = true;                  // TilemapCollider2D şekli üretir (Unity Collider Type: None değil)
        bool SpriteCollider = false;        // Unity Collider Type Sprite: şekil dokunun opak alanı (yoksa Grid: bütün hücre)
        // Unity Tile Flags
        bool LockColor = false;             // boyarken hücre rengi değişmez (karonun rengi)
        bool LockTransform = false;         // boyarken döndürme / aynalama uygulanmaz
        // Unity Tile.gameObject: her hücrede örneklenen prefab (karo haritasının çocuğu, hücre merkezinde)
        std::string GameObject;
        bool GameObjectRuntimeOnly = true;  // Instantiate GameObject Runtime Only: kapalıyken düzenleyicide de görünür (kaydedilmez)
        bool Walkable = true;               // yol bulma
        int Cost = 1;                       // yol bulma maliyeti (>= 1)
        float Height = 0.25f;               // 3D çizimde prizma yüksekliği
        // Otomatik döşeme (Godot terrain / Unity Rule Tile): doku bir atlas, hücre komşulara göre seçilir.
        // 0 yok; 1 = 16 karo (4 kenar komşu, 4x4 atlas, indis = K1|D2|G4|B8); 2 = 47 karo "blob" (8 komşu, 8x6 atlas)
        int Autotile = 0;
        int Terrain = -1;                   // aynı Terrain değerli karolar birbirine bağlanır (-1: yalnızca aynı karo)
        // Karo varlığı (.cqtile, Unity Tile asset): doluysa yukarıdaki alanlar dosyadan gelir (dosya değişince güncellenir)
        std::string Asset;
        // Rule Tile: ilk eşleşen kuralın dokusu (hiçbiri eşleşmezse Texture)
        std::vector<TileRule> Rules;
        // Animated Tile: kare dokuları ve hız (kare/saniye)
        std::vector<std::string> Frames;
        float FrameRate = 0.0f;
    };

    struct TileCell
    {
        int Tile = 0;                       // Palette indisi
        glm::vec4 Color{ 1.0f };            // hücre başına renk çarpanı
        float Elevation = 0.0f;             // 3D: ek yükseklik
        // Hücre dönüşümü (Unity Tilemap hücre matrisi; [ ] döndür, Shift+[ ] aynala): bit 0-1 saat yönü tersine çeyrek
        // dönüş, bit 2 yatay ayna (önce aynalanır, sonra döner). Dikey ayna = yatay ayna + 180°
        uint8_t Transform = 0;
    };

    inline int TileTurns(uint8_t t) { return t & 3; }
    inline bool TileMirrored(uint8_t t) { return (t & 4) != 0; }
    inline uint8_t MakeTileTransform(int turns, bool mirrored) { return (uint8_t)((((turns % 4) + 4) % 4) | (mirrored ? 4 : 0)); }
    // Görünen karoya yeni bir işlem (Unity'deki gibi fırçanın şimdiki hali üzerinden). Doku koordinatı uv = R^k(M^m(p)):
    // görüntüyü saat yönü tersine döndürmek k'yi artırır (aynalıyken azaltır: M·R = R⁻¹·M), yatay aynalamak yalnız ayna bitini,
    // dikey aynalamak (= 180° · yatay ayna) ayna bitini değiştirip k'ye 2 ekler
    inline uint8_t RotateTileTransform(uint8_t t, int quarterTurnsCCW)
    {
        return MakeTileTransform(TileTurns(t) + (TileMirrored(t) ? -quarterTurnsCCW : quarterTurnsCCW), TileMirrored(t));
    }
    inline uint8_t FlipTileTransformX(uint8_t t) { return MakeTileTransform(TileTurns(t), !TileMirrored(t)); }
    inline uint8_t FlipTileTransformY(uint8_t t) { return MakeTileTransform(TileTurns(t) + 2, !TileMirrored(t)); }
    // Hücre içi (0..1) doku koordinatına dönüşüm
    inline glm::vec2 ApplyTileTransform(uint8_t t, glm::vec2 uv)
    {
        if (TileMirrored(t))
            uv.x = 1.0f - uv.x;
        for (int k = 0; k < TileTurns(t); ++k)
            uv = glm::vec2(uv.y, 1.0f - uv.x);
        return uv;
    }

    struct TilemapComponent
    {
        std::vector<TileDefinition> Palette;
        std::unordered_map<uint64_t, TileCell> Cells;  // anahtar: GridKey(x, y)
        glm::vec4 Tint{ 1.0f };
        int OrderInLayer = 0;
        bool Render3D = false;              // hücreleri prizma/küp olarak çiz (3D tahta)
        bool Outline = true;                // 2D: hücre kenar çizgileri
        uint32_t Version = 0;               // düzenleme sayacı (çarpıştırıcı/önbellek yenileme)

        TilemapComponent()
        {
            TileDefinition grass;
            grass.Name = "Grass";
            grass.Color = { 0.36f, 0.72f, 0.33f, 1.0f };
            grass.Solid = false;
            TileDefinition water;
            water.Name = "Water";
            water.Color = { 0.22f, 0.48f, 0.86f, 1.0f };
            water.Solid = false;
            water.Walkable = false;
            water.Height = 0.12f;
            TileDefinition stone;
            stone.Name = "Stone";
            stone.Color = { 0.55f, 0.55f, 0.6f, 1.0f };
            stone.Cost = 2;
            stone.Height = 0.4f;
            TileDefinition wall;
            wall.Name = "Wall";
            wall.Color = { 0.35f, 0.3f, 0.28f, 1.0f };
            wall.Walkable = false;
            wall.Height = 1.0f;
            Palette = { grass, water, stone, wall };
        }
    };

    // Unity Tile.gameObject örneği: karo haritasının çocuğu. Düzenleyici önizlemesi (GameObjectRuntimeOnly kapalı) sahneye kaydedilmez
    struct TileObjectComponent
    {
        uint64_t Tilemap = 0;          // karo haritasının UUID'si
        int CellX = 0, CellY = 0;
        bool EditorPreview = false;
    };

    // Ters hücre dönüşümü: görünen hücre konumu (0..1) <- doku koordinatı
    inline glm::vec2 InverseTileTransform(uint8_t t, glm::vec2 uv)
    {
        for (int k = 0; k < TileTurns(t); ++k)
            uv = glm::vec2(1.0f - uv.y, uv.x);
        if (TileMirrored(t))
            uv.x = 1.0f - uv.x;
        return uv;
    }

    struct TilemapCollider2DComponent
    {
        float Friction = 0.4f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        std::string PhysicsMaterial;   // .cqphysmat2d
    };

    inline uint64_t GridKey(int x, int y)
    {
        return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    }
    inline glm::ivec2 GridKeyToCell(uint64_t key)
    {
        return { (int)(int32_t)(uint32_t)(key >> 32), (int)(int32_t)(uint32_t)(key & 0xFFFFFFFFu) };
    }
}
