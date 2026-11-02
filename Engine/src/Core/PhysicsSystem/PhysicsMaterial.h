#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    // Fizik malzemesi (Unity Physic Material / Unreal Physical Material): sürtünme ve sekme bir .cqphysmat dosyasında;
    // çarpıştırıcının "Material" yuvasına atanır, aynı dosyayı kullanan tüm nesneler aynı davranır.
    struct PhysicsMaterialAsset
    {
        float Friction = 0.6f;     // 0 = buz, 1 = lastik
        float Bounciness = 0.0f;   // 0 = sekmez, 1 = enerji kaybetmeden seker
    };

    namespace PhysicsMaterials
    {
        constexpr const char* Extension = ".cqphysmat";
        // Physics Material 2D (Unity PhysicsMaterial2D): aynı alanlar, 2D çarpıştırıcılar ve Rigidbody 2D için
        constexpr const char* Extension2D = ".cqphysmat2d";
        CQ_API bool IsFile(const std::string& path);
        CQ_API bool IsFile2D(const std::string& path);
        // Önbellekli; dosya değişince yeniden okunur. Bulunamazsa false
        CQ_API bool Load(const std::string& path, PhysicsMaterialAsset& out);
        CQ_API bool Save(const PhysicsMaterialAsset& material, const std::string& path);
    }
}
