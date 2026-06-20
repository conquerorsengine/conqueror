#pragma once

#include "AssetManager.h"
#include "Core/Assets/AssetSystem/AssetMetadata.h"
#include "Core/Assets/AssetSystem/AssetRegistry.h"

#include <unordered_map>

namespace Conqueror {

    class EditorAssetManager : public AssetManager
    {
    public:
        EditorAssetManager() = default;
        virtual ~EditorAssetManager() = default;

        virtual Ref<Asset> GetAsset(AssetHandle handle) override;
        virtual bool IsAssetHandleValid(AssetHandle handle) const override;
        virtual bool IsAssetLoaded(AssetHandle handle) const override;

        virtual void Shutdown() override;

        virtual AssetManagerType GetType() const override { return AssetManagerType::Editor; }

        AssetRegistry& GetRegistry() { return m_AssetRegistry; }

    private:
        AssetRegistry m_AssetRegistry;
        std::unordered_map<AssetHandle, Ref<Asset>> m_LoadedAssets;
    };
}
