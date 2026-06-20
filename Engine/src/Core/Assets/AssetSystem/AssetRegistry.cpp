#include "AssetRegistry.h"
#include "Core/Logging/Log.h"

namespace Conqueror {

    AssetMetadata& AssetRegistry::Get(AssetHandle handle)
    {
        CQ_CORE_ASSERT(Contains(handle), "Asset handle not found in registry!");
        return m_Registry[handle];
    }

    const AssetMetadata& AssetRegistry::Get(AssetHandle handle) const
    {
        CQ_CORE_ASSERT(Contains(handle), "Asset handle not found in registry!");
        return m_Registry.at(handle);
    }

    bool AssetRegistry::Contains(AssetHandle handle) const
    {
        return m_Registry.find(handle) != m_Registry.end();
    }

    void AssetRegistry::Add(AssetHandle handle, const AssetMetadata& metadata)
    {
        m_Registry[handle] = metadata;
    }

    void AssetRegistry::Remove(AssetHandle handle)
    {
        m_Registry.erase(handle);
    }

    void AssetRegistry::Clear()
    {
        m_Registry.clear();
    }
}
