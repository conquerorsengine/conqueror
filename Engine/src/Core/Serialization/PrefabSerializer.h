#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include <filesystem>
#include <string>

namespace Conqueror
{
    // Prefab (.cqprefab): bir varlık alt ağacının diskteki şablonu (Unity Prefab).
    // Dosya, tek köklü bir sahne olarak SceneSerializer biçiminde saklanır. Örnekleme önbellekli şablon
    // sahnesinden yapılır (dosya değişince yeniden yüklenir); kök, kaynağını PrefabInstanceComponent'te tutar.
    class CQ_API PrefabSerializer
    {
    public:
        static constexpr const char* Extension = ".cqprefab";

        // Alt ağacı prefab dosyasına yaz (kök konumu korunur)
        static bool Serialize(Entity root, const std::filesystem::path& filepath);
        // Prefab'ı sahneye örnekle; parent verilirse onun altına. Kök varlığı döner (başarısızsa boş)
        static Entity Deserialize(const std::shared_ptr<Scene>& scene, const std::filesystem::path& filepath, Entity parent = {});
        static Entity Instantiate(Scene& scene, const std::string& path, Entity parent = {});

        // Örneği kaynağına geri yaz (Apply) / kaynaktan yeniden oluştur (Revert; konum, ebeveyn ve isim korunur)
        static bool ApplyInstance(Entity instanceRoot);
        static Entity RevertInstance(Entity instanceRoot);
        // Örneğin kaynak prefab yolu (örnek değilse boş); en yakın prefab kökü
        static std::string GetPrefabPath(Entity entity);
        static Entity FindInstanceRoot(Entity entity);

        static bool IsPrefabPath(const std::string& path);

        // Prefab Variant (Unity): tabandan türeyen, yalnız farkları saklayan prefab. Taban değişince variant da değişir;
        // variant örneğine Apply yalnız farkları yazar. Variant'ın tabanı da bir variant olabilir.
        static bool CreateVariant(const std::filesystem::path& basePath, const std::filesystem::path& variantPath);
        static bool IsVariant(const std::filesystem::path& path);
        static std::string GetVariantBase(const std::filesystem::path& path);   // proje göreli (variant değilse boş)
        // Proje göreli yol -> mutlak (bulunamazsa olduğu gibi)
        static std::filesystem::path Resolve(const std::string& path);
        // Mutlak yol -> proje göreli (proje dışındaysa mutlak)
        static std::string ToProjectPath(const std::filesystem::path& path);
        static void ClearCache();
    };
}
