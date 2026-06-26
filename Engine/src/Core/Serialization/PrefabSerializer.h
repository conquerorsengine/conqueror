#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include <filesystem>

namespace Conqueror
{
    class CQ_API PrefabSerializer
    {
    public:
        static bool Serialize(Entity root, const std::filesystem::path& filepath);
        static Entity Deserialize(const std::shared_ptr<Scene>& scene, const std::filesystem::path& filepath, Entity parent = {});
    };
}
