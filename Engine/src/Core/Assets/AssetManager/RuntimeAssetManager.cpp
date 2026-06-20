#include "RuntimeAssetManager.h"

namespace Conqueror {

    Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle handle)
    {
        if (IsAssetLoaded(handle))
            return m_LoadedAssets[handle];

        // TODO: Load from AssetPack
        return nullptr;
    }

    bool RuntimeAssetManager::IsAssetHandleValid(AssetHandle handle) const
    {
        // Check pack
        return false;
    }

    bool RuntimeAssetManager::IsAssetLoaded(AssetHandle handle) const
    {
        return m_LoadedAssets.find(handle) != m_LoadedAssets.end();
    }

    void RuntimeAssetManager::Shutdown()
    {
        m_LoadedAssets.clear();
        m_AssetPack = nullptr;
    }
}
