#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    // Skinning Editor kemiği (piksel, dilim dikdörtgeninin sol-alt köşesine göre)
    struct SpriteBone
    {
        std::string Name;
        int Parent = -1;
        glm::vec2 Position{ 0.0f };
        glm::vec2 Tip{ 0.0f };
    };

    // Skinning Editor verisi: kemikler, ağ ve köşe başına en çok 4 kemik ağırlığı
    struct SpriteSkinData
    {
        std::vector<SpriteBone> Bones;
        std::vector<glm::vec2> Vertices;       // piksel, dilime göre
        std::vector<uint32_t> Indices;         // üçgenler
        std::vector<glm::ivec4> BoneIndex;     // -1: yok
        std::vector<glm::vec4> BoneWeight;
    };

    // Bir doku içindeki alt sprite (Unity "Sprite Mode: Multiple" dilimi)
    struct SpriteDef
    {
        std::string Name;
        glm::vec4 Rect{ 0.0f };        // piksel: x, y (sol-alt köken), genişlik, yükseklik
        glm::vec2 Pivot{ 0.5f };       // 0..1 dikdörtgen içinde
        glm::vec4 Border{ 0.0f };      // 9-slice kenarlıkları (piksel): sol, alt, sağ, üst
        // Custom Outline (çizim ağı) ve Custom Physics Shape (Polygon Collider 2D): çokgenler, piksel, dilime göre
        std::vector<std::vector<glm::vec2>> Outlines;
        std::vector<std::vector<glm::vec2>> PhysicsShapes;
        SpriteSkinData Skin;
        // Outlines'ın üçgenleri (3'er köşe, piksel); yüklemede üretilir, kaydedilmez
        std::vector<glm::vec2> OutlineTriangles;
    };

    // "<doku>.cqsprite" meta dosyası: dilimler, birim başına piksel, filtre.
    class CQ_API SpriteSheet
    {
    public:
        enum class Filter : int { Bilinear = 0, Point };

        std::string TexturePath;
        float PixelsPerUnit = 100.0f;
        Filter FilterMode = Filter::Bilinear;
        std::vector<SpriteDef> Sprites;
        // Secondary Textures (Unity Sprite Editor): ad (_NormalMap, _MaskTex ...) ve proje göreli yol
        struct SecondaryTexture { std::string Name; std::string Path; };
        std::vector<SecondaryTexture> SecondaryTextures;
        bool FullRectMesh = false;     // Mesh Type: Full Rect (Custom Outline yok sayılır)
        int ExtrudeEdges = 0;          // çizim ağı bu kadar piksel dışa genişler (bilineer kenar kesilmesin)

        const SpriteDef* Find(const std::string& name) const;
        SpriteDef* Find(const std::string& name);
        // Sprite Renderer'ın dilimi: ad boşsa (Sprite Mode: Single) tüm dokuyu kaplayan tek dilim (Custom Outline / Physics Shape / Skin onda)
        const SpriteDef* FindForSprite(const std::string& name, const glm::vec2& textureSize) const;
        std::string MakeUniqueName(const std::string& base) const;

        bool Save() const;
        bool Load();

        static std::filesystem::path MetaPathFor(const std::string& texturePath);
        // Önbellekli erişim; meta dosyası değişince yeniden yüklenir (yoksa boş sayfa döner)
        static std::shared_ptr<SpriteSheet> Get(const std::string& texturePath);
        static void Invalidate(const std::string& texturePath);

        // ---- Dilimleme yardımcıları (RGBA8, satır 0 = üst; dönüş: piksel x,y(sol-alt),w,h)
        static std::vector<glm::vec4> SliceGrid(int texWidth, int texHeight, glm::ivec2 cellSize, glm::ivec2 offset, glm::ivec2 padding,
                                                const uint8_t* rgba = nullptr, bool skipEmpty = true);
        static std::vector<glm::vec4> SliceAutomatic(const uint8_t* rgba, int width, int height, int minSize = 4, uint8_t alphaThreshold = 1);
        // Dilimin saydam olmayan bölgelerinin dış çizgileri (piksel, dilime göre). tolerance 0..1: 1 en ayrıntılı (Unity Outline Tolerance)
        static std::vector<std::vector<glm::vec2>> GenerateOutline(const uint8_t* rgba, int width, int height, const glm::vec4& rect, float tolerance,
                                                                   uint8_t alphaThreshold = 1);
        // Outlines -> OutlineTriangles
        static void BuildOutlineTriangles(SpriteDef& sprite, int extrude = 0);
        // Dikdörtgeni saydam kenarlardan kırp
        static glm::vec4 Trim(const uint8_t* rgba, int width, int height, const glm::vec4& rect, uint8_t alphaThreshold = 1);

    private:
        std::filesystem::file_time_type m_LoadedTime{};
    };
}
