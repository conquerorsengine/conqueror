#include "EditorAssetManager.h"
#include "Core/Logging/Log.h"

namespace Conqueror {

    Ref<Asset> EditorAssetManager::GetAsset(AssetHandle handle)
    {
        if (!IsAssetHandleValid(handle))
            return nullptr;

        if (IsAssetLoaded(handle))
            return m_LoadedAssets[handle];

        // TODO: Load asset from disk using Importer
        return nullptr;
    }

    bool EditorAssetManager::IsAssetHandleValid(AssetHandle handle) const
    {
        return m_AssetRegistry.Contains(handle);
    }

    bool EditorAssetManager::IsAssetLoaded(AssetHandle handle) const
    {
        return m_LoadedAssets.find(handle) != m_LoadedAssets.end();
    }

    void EditorAssetManager::Shutdown()
    {
        m_LoadedAssets.clear();
        m_AssetRegistry.Clear();
    }
}
