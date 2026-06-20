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

    private:
        static void ProcessNode(void* node, const void* scene, std::shared_ptr<Model> model);
        static std::shared_ptr<Mesh> ProcessStaticMesh(void* mesh, const void* scene);
        static std::shared_ptr<SkinnedMesh> ProcessSkinnedMesh(void* mesh, std::shared_ptr<Model> model);
        static std::shared_ptr<Material> ProcessMaterial(void* material, const void* scene, const std::string& directory);

        static inline std::unordered_map<std::string, std::weak_ptr<Model>> s_Cache;
    };
}
