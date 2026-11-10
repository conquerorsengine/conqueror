#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "Material.h"
#include "Core/Animation/Skeleton.h"
#include "Core/Animation/AnimationClip.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Conqueror
{
    struct Model
    {
        std::vector<std::shared_ptr<Mesh>> Meshes;
        std::vector<std::shared_ptr<SkinnedMesh>> SkinnedMeshes;
        std::vector<std::shared_ptr<Material>> Materials;
        std::string Directory;

        bool IsSkinned = false;
        std::shared_ptr<Skeleton> SkeletonData;
        std::vector<std::shared_ptr<AnimationClip>> Animations;
    };

    class CQ_API ModelLoader
    {
    public:
        static std::shared_ptr<Model> Load(const std::string& path);
        static void ClearCache() { s_Cache.clear(); }
        // Reimport: dosya bir sonraki Load'da diskten yeniden okunur
        static void Invalidate(const std::string& path) { s_Cache.erase(path); }

        // Unity "Extract Materials" / "Extract Material SubAsset": gömülü dokular dosyaya, her malzeme bir .cqmat'a yazılır;
        // model içe aktarma ayarı (<model>.meta, ModelImport > MaterialRemap) bundan sonra modelin bu .cqmat'ları
        // kullanmasını sağlar (bütün örnekler; .cqmat düzenlenince model de değişir). Oluşturulan dosyalar created'a eklenir.
        static bool ExtractMaterials(const std::string& path, const std::string& outDirectory, std::vector<std::string>& created,
                                     std::string* error = nullptr);
        // Modeldeki malzeme adları (dosya sırasıyla, tekrarsız)
        static std::vector<std::string> MaterialNames(const std::string& path);
        // Malzeme eşlemesi (<model>.meta): ad -> proje göreli .cqmat yolu; boş yol eşlemeyi kaldırır
        static std::vector<std::pair<std::string, std::string>> ReadMaterialRemap(const std::string& path);
        static bool WriteMaterialRemap(const std::string& path, const std::vector<std::pair<std::string, std::string>>& remap);

    private:
        // parentTransform: düğüm dönüşümleri (kök hariç) statik ağlara uygulanır (ör. glTF'de ayrı düğümdeki tekerlekler)
        static void ProcessNode(void* node, const void* scene, std::shared_ptr<Model> model, const glm::mat4& parentTransform, bool isRoot);
        static std::shared_ptr<Mesh> ProcessStaticMesh(void* mesh, const void* scene, const glm::mat4& transform);
        // rigidBone: kemiksiz (yalnız blend shape'li) ağ, tüm köşeleri bu düğüm kemiğine bağlanır (düğüm animasyonu da işler)
        static std::shared_ptr<SkinnedMesh> ProcessSkinnedMesh(void* mesh, std::shared_ptr<Model> model, const std::string& rigidBone = "");
        static std::shared_ptr<Material> ProcessMaterial(void* material, const void* scene, const std::string& directory);

        static inline std::unordered_map<std::string, std::weak_ptr<Model>> s_Cache;
    };
}
