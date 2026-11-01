#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    // Bir doku içindeki alt sprite (Unity "Sprite Mode: Multiple" dilimi)
    struct SpriteDef
    {
        std::string Name;
        glm::vec4 Rect{ 0.0f };        // piksel: x, y (sol-alt köken), genişlik, yükseklik
        glm::vec2 Pivot{ 0.5f };       // 0..1 dikdörtgen içinde
        glm::vec4 Border{ 0.0f };      // 9-slice kenarlıkları (piksel): sol, alt, sağ, üst
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

        const SpriteDef* Find(const std::string& name) const;
        SpriteDef* Find(const std::string& name);
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
        // Dikdörtgeni saydam kenarlardan kırp
        static glm::vec4 Trim(const uint8_t* rgba, int width, int height, const glm::vec4& rect, uint8_t alphaThreshold = 1);

    private:
        std::filesystem::file_time_type m_LoadedTime{};
    };
}
