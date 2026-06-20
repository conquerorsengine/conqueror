#pragma once

#include "AssetMetadata.h"
#include <unordered_map>

namespace Conqueror {

    class AssetRegistry
    {
    public:
        AssetMetadata& Get(AssetHandle handle);
        const AssetMetadata& Get(AssetHandle handle) const;

        bool Contains(AssetHandle handle) const;
        void Add(AssetHandle handle, const AssetMetadata& metadata);
        void Remove(AssetHandle handle);

        void Clear();

        auto begin() { return m_Registry.begin(); }
        auto end() { return m_Registry.end(); }
        auto begin() const { return m_Registry.begin(); }
        auto end() const { return m_Registry.end(); }

        size_t Count() const { return m_Registry.size(); }

    private:
        std::unordered_map<AssetHandle, AssetMetadata> m_Registry;
    };
}
