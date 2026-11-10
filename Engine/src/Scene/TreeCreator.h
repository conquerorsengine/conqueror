#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    struct Model;

    // Unity Tree Creator (GameObject > 3D Object > Tree): tohumla (seed) üretilen prosedürel ağaç. Gruplar bir hiyerarşi
    // kurar: kökten çıkan dal grubu (gövde) -> dallar -> ince dallar -> yapraklar. Her grubun dağılımı (sıklık, düzen, büyüme
    // açısı / ölçeği), şekli (uzunluk, yarıçap, kırışıklık, güneşe yönelme, gürültü, kök genişlemesi) ve malzemesi vardır.
    // Üretilen ağ (kabuk + yaprak) aynı varlığın Mesh Renderer'ına model olarak verilir: gölge, seçim, ışık hazır gelir
    struct TreeGroup
    {
        enum class Kind : int { Branch = 0, Leaf };
        enum class Distribution : int { Random = 0, Alternate, Opposite, Whorled };
        enum class LeafShape : int { Plane = 0, Cross, TriCross };

        std::string Name;
        Kind Type = Kind::Branch;
        int Parent = -1;                        // üst grup (-1: kök, Unity Tree Root Node)
        // Dağılım (Unity Distribution)
        int Seed = 1;                           // grubun kendi tohumu (Unity Group Seed)
        int Frequency = 1;                      // her üst dal için kaç tane
        Distribution Mode = Distribution::Random;
        int Whorl = 3;                          // Whorled: bir halkadaki sayı
        float Twirl = 0.0f;                     // ardışık dallar / halkalar arası ek dönüş (derece)
        glm::vec2 Range{ 0.0f, 1.0f };          // üst dal boyunca dağılım aralığı (0 kök, 1 uç)
        glm::vec2 GrowthScale{ 1.0f, 1.0f };    // üst dalın kökünde / ucunda ölçek
        glm::vec2 GrowthAngle{ 0.0f, 0.0f };    // üst dalın eksenine göre açı (derece), kökünde / ucunda
        // Şekil: dal
        glm::vec2 Length{ 8.0f, 10.0f };        // metre (Relative Length açıksa üst dalın uzunluğuna oran)
        bool RelativeLength = false;
        float Radius = 0.3f;                    // metre (üst dalın o noktadaki yarıçapını aşmaz)
        float CapTaper = 0.2f;                  // uç yarıçapı / kök yarıçapı
        float Crinkliness = 0.1f;               // dalın rastgele kıvrılması
        float SeekSun = 0.1f;                   // -1 aşağı sarkar .. 1 yukarı yönelir
        float Noise = 0.1f;                     // kabuk yüzeyindeki pürüz
        float FlareRadius = 0.0f;               // kök genişlemesi (yarıçapa oran)
        float FlareHeight = 0.1f;               // genişlemenin yüksekliği (uzunluğa oran)
        // Şekil: yaprak
        glm::vec2 Size{ 0.5f, 0.7f };           // metre
        LeafShape Shape = LeafShape::Plane;
        float PerpendicularAlign = 0.0f;        // yaprak yüzü üst dala dik
        float HorizontalAlign = 0.0f;           // yaprak yüzü yukarı (yatay)
        // Malzeme (.cqmat); boş = varsayılan kabuk / yaprak
        std::string Material;
    };

    struct TreeComponent
    {
        int Seed = 1234;
        float LODQuality = 0.6f;                // dal kesiti ve boy bölümleri
        float AreaSpread = 0.0f;                // kökten birden çok gövde çıkınca dağıldığı yarıçap (metre)
        float GroundOffset = 0.0f;              // gövdenin yere gömülme derinliği (metre)
        std::vector<TreeGroup> Groups;          // sırayla: bir grubun üst grubu her zaman kendinden önce
        int SelectedGroup = 0;                  // editör: Inspector'da düzenlenen grup

        // Çalışma zamanı (serileştirilmez)
        uint64_t BuiltHash = 0;
        std::shared_ptr<Model> Generated;
        uint32_t VertexCount = 0, TriangleCount = 0;

        TreeComponent();
    };

    namespace TreeCreator
    {
        // Gövde + dallar + ince dallar + yapraklar
        CQ_API void MakeDefault(TreeComponent& tree);
        CQ_API TreeGroup MakeGroup(TreeGroup::Kind kind, int parent, const std::string& name);
        CQ_API uint64_t Hash(const TreeComponent& tree);
        CQ_API std::shared_ptr<Model> Generate(const TreeComponent& tree, uint32_t* vertices = nullptr, uint32_t* triangles = nullptr);
        // Ayarı değişen ağaçları yeniden üretir ve Mesh Renderer'a verir (çizimden önce, her karede ucuz)
        CQ_API void Update(Scene& scene);
        // Grubu ve alt gruplarını siler, üst grup indislerini düzeltir
        CQ_API void RemoveGroup(TreeComponent& tree, int index);
        // Grubu (alt grupları olmadan) aynı üst grubun altına kopyalar
        CQ_API int DuplicateGroup(TreeComponent& tree, int index);
    }

    void RegisterTreeComponents();
}
