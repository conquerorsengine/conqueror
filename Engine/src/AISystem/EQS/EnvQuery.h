#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Environment Query System (Unreal EQS): bir üreteç aday noktalar / varlıklar çıkarır, testler her adayı
// süzer ya da puanlar (her testin ham değerleri 0..1'e normalleştirilir, ağırlıkla toplanır), en iyiler döner.
// Örnek: "oyuncuyu görmeyen, bana yakın, yolla ulaşılabilir siper noktası".
namespace Conqueror
{
    class Scene;

    enum class EnvGeneratorType : int { Grid = 0, Ring, Entities };
    enum class EnvTestType : int { Distance = 0, Visibility, Dot, PathLength, Height };
    enum class EnvTestMode : int { Score = 0, Filter, FilterAndScore };

    // Bağlam: sorgulayan, bir varlık ya da sabit nokta
    struct EnvContext
    {
        enum class Kind : int { Querier = 0, Entity, Point } Type = Kind::Querier;
        uint32_t EntityId = 0xFFFFFFFFu;
        glm::vec3 Point{ 0.0f };
    };

    struct EnvGenerator
    {
        EnvGeneratorType Type = EnvGeneratorType::Grid;
        EnvContext Center;                // ızgara / halka merkezi
        float Radius = 10.0f;             // ızgara yarı genişliği / halka dış yarıçapı
        float Spacing = 2.0f;             // ızgara aralığı
        int Rings = 3;
        int PointsPerRing = 12;
        std::string Tag;                  // Entities: oyun etiketi (GameTag) ya da gameplay tag
        bool ProjectToNavMesh = true;     // noktaları gezinme ağına oturt (ağ yoksa zemine ışınla)
    };

    struct EnvTest
    {
        EnvTestType Type = EnvTestType::Distance;
        EnvTestMode Mode = EnvTestMode::Score;
        EnvContext Context;               // mesafe / görünürlük / yön ölçülen bağlam
        float FilterMin = -1e30f, FilterMax = 1e30f;   // ham değer aralığı (Filter)
        bool Visible = true;              // Visibility: görünür olanlar mı istenir
        float Weight = 1.0f;              // < 0: düşük değer tercih edilir (ör. yakın)
        float EyeHeight = 1.6f;           // görünürlük ışını yükseklikleri
        float ItemHeight = 0.6f;
    };

    struct EnvQueryItem
    {
        glm::vec3 Position{ 0.0f };
        uint32_t Entity = 0xFFFFFFFFu;
        float Score = 0.0f;
        bool Valid = true;
    };

    struct EnvQueryDesc
    {
        EnvGenerator Generator;
        std::vector<EnvTest> Tests;
        int MaxResults = 5;
    };

    namespace EnvQuery
    {
        // Sorguyu çalıştır: sonuçlar puana göre azalan, yalnızca geçerliler (en fazla MaxResults); tümü 'all' içine
        CQ_API std::vector<EnvQueryItem> Run(Scene& scene, uint32_t querier, const EnvQueryDesc& desc, std::vector<EnvQueryItem>* all = nullptr);
        // .cqeqs (YAML) sorgu dosyası
        CQ_API bool Load(const std::string& path, EnvQueryDesc& out, std::string* error = nullptr);
        CQ_API bool Save(const std::string& path, const EnvQueryDesc& desc);
        // Son sorgunun hata ayıklama çizimi (yeşil = iyi, kırmızı = süzülen), seconds süreyle
        CQ_API void DebugDraw(const std::vector<EnvQueryItem>& items, float seconds);
    }
}
