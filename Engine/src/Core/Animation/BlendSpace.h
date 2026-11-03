#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Conqueror
{
    // Blend Space (Unreal Blend Space / Blend Space 1D): eksenler ve eksenler üzerinde yerleştirilmiş klipler (.cqblendspace).
    // Animator durumu "Blend Space" alanında bu dosyayı kullanır: durumun blend parametreleri eksenleri sürer, klipler
    // en yakın örneklerin ağırlıklarıyla karışır (1D doğrusal, 2D serbest kartezyen).
    struct BlendSpaceAxis
    {
        std::string Name = "Speed";
        float Min = 0.0f;
        float Max = 1.0f;
        int Divisions = 4;   // editörde ızgara
    };

    struct BlendSpaceSample
    {
        std::string Clip;    // modeldeki klip adı
        glm::vec2 Position{ 0.0f };
    };

    struct BlendSpaceAsset
    {
        bool Is2D = true;
        BlendSpaceAxis AxisX, AxisY;
        std::string ClipsFrom;   // klip listesi için model (editör)
        std::vector<BlendSpaceSample> Samples;
    };

    namespace BlendSpaces
    {
        CQ_API bool Load(const std::string& path, BlendSpaceAsset& out, std::string* error = nullptr);
        CQ_API std::string ToText(const BlendSpaceAsset& asset);
        CQ_API bool Save(const std::string& path, const BlendSpaceAsset& asset);
        CQ_API BlendSpaceAsset Default(bool is2D);
    }
}
