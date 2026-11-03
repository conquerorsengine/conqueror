#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Curve varlığı (.cqcurve): Unreal Curve Float / Curve Vector / Curve Linear Color, Unity AnimationCurve karşılığı.
//  - Her kanal bir anahtar dizisi; anahtar: zaman, değer, giriş / çıkış teğeti ve ara değerleme (Auto, Linear, Constant,
//    Free / kırık teğet). Auto teğetler komşu anahtarlardan (Catmull-Rom, tepe ve çukurda düz) hesaplanır.
//  - Ön / son sonsuzluk: Clamp (son değeri tut), Loop, PingPong, Linear (son eğimle devam).
//  - Oyunda: CurveEvaluate("Assets/Curves/Jump.cqcurve", t) -> sayı (Float), {x, y, z} (Vector), {r, g, b, a} (Color).
namespace Conqueror
{
    enum class CurveType : int { Float = 0, Vector, Color };
    enum class CurveKeyMode : int { Auto = 0, Linear, Constant, Free };
    enum class CurveInfinity : int { Clamp = 0, Loop, PingPong, Linear };

    struct CurveKey
    {
        float Time = 0.0f;
        float Value = 0.0f;
        float InTangent = 0.0f;    // eğim (değer / saniye); Free modda kullanıcının
        float OutTangent = 0.0f;
        CurveKeyMode Mode = CurveKeyMode::Auto;
    };

    struct CurveChannel
    {
        std::string Name;
        std::vector<CurveKey> Keys;

        float Evaluate(float time, CurveInfinity pre, CurveInfinity post) const;
        // Auto / Linear anahtarların teğetlerini yeniden hesapla, zamana göre sırala
        void Recalculate();
        // Anahtar ekle (aynı zamanda varsa değerini değiştirir); eklenen anahtarın dizini
        int AddKey(float time, float value, CurveKeyMode mode = CurveKeyMode::Auto);
    };

    struct CurveAsset
    {
        CurveType Type = CurveType::Float;
        std::vector<CurveChannel> Channels;
        CurveInfinity PreInfinity = CurveInfinity::Clamp;
        CurveInfinity PostInfinity = CurveInfinity::Clamp;

        // Türün varsayılanı: Float 0->1 doğrusal, Vector üç kanal, Color beyazdan beyaza (RGBA)
        static CurveAsset Default(CurveType type);
        // Bütün kanallar; Float -> x, Vector -> xyz, Color -> rgba
        glm::vec4 Evaluate(float time) const;
        // Anahtarların zaman ve değer aralığı (görünüm için)
        void Range(float& minTime, float& maxTime, float& minValue, float& maxValue) const;
        void Recalculate();
    };

    namespace CurveAssets
    {
        inline constexpr const char* Extension = ".cqcurve";

        CQ_API bool Save(const std::string& path, const CurveAsset& curve);
        CQ_API bool Load(const std::string& path, CurveAsset& out, std::string* error = nullptr);
        // Önbellekli (dosya değişince yeniden okunur); yoksa nullptr
        CQ_API const CurveAsset* Get(const std::string& path);
        CQ_API void Invalidate(const std::string& path);
        CQ_API const char* TypeName(CurveType type);
        CQ_API const char* ModeName(CurveKeyMode mode);
        CQ_API const char* InfinityName(CurveInfinity mode);
    }
}
