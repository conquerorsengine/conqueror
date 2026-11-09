#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>

// Light Profile (Unreal IES Texture): ışığın açıya göre şiddeti. .ies dosyası ya da Light Profile varlığı (.cqlightprofile).
// Gölgelendiriciye 8 örnek (2 vec4) gider: spot ışıkta eksen -> dış koni açısı, nokta ışıkta aşağı yön -> yukarı yön.
namespace Conqueror
{
    class Scene;

    struct LightProfileComponent
    {
        std::string Profile;     // .cqlightprofile ya da .ies
    };

    namespace LightProfiles
    {
        // maxAngleDegrees: örneklerin yayıldığı açı (0..maxAngle). false: profil okunamadı
        CQ_API bool Sample(const std::string& path, float maxAngleDegrees, glm::vec4& out0, glm::vec4& out1);
        void RegisterComponents();
    }
}
