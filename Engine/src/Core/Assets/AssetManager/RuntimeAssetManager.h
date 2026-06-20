#pragma once

#include "AssetManager.h"
#include "Core/Assets/Pack/AssetPack.h"

#include <unordered_map>

namespace Conqueror {

    class RuntimeAssetManager : public AssetManager
    {
    public:
        RuntimeAssetManager() = default;
        virtual ~RuntimeAssetManager() = default;

        virtual Ref<Asset> GetAsset(AssetHandle handle) override;
        virtual bool IsAssetHandleValid(AssetHandle handle) const override;
        virtual bool IsAssetLoaded(AssetHandle handle) const override;

        virtual void Shutdown() override;

        virtual AssetManagerType GetType() const override { return AssetManagerType::Runtime; }

        void SetAssetPack(Ref<AssetPack> assetPack) { m_AssetPack = assetPack; }

    private:
        Ref<AssetPack> m_AssetPack;
        std::unordered_map<AssetHandle, Ref<Asset>> m_LoadedAssets;
    };
}
