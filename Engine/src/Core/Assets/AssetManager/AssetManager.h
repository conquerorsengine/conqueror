#pragma once

#include "Core/Assets/AssetSystem/Asset.h"
#include <memory>

namespace Conqueror {

    enum class AssetManagerType
    {
        Editor, Runtime
    };

    class AssetManager
    {
    public:
        virtual ~AssetManager() = default;

        virtual Ref<Asset> GetAsset(AssetHandle handle) = 0;
        virtual bool IsAssetHandleValid(AssetHandle handle) const = 0;
        virtual bool IsAssetLoaded(AssetHandle handle) const = 0;
        
        virtual void Shutdown() = 0;
        
        virtual AssetManagerType GetType() const = 0;
    };
}
