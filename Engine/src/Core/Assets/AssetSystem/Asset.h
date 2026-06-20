#pragma once

#include "Core/Base/Base.h"

namespace Conqueror {

    using AssetHandle = uint64_t;

    enum class AssetType : uint16_t
    {
        None = 0,
        Scene,
        Texture2D,
        Audio,
        Script
    };

    class Asset
    {
    public:
        AssetHandle Handle = 0;

        virtual ~Asset() = default;
        
        static AssetType GetStaticType() { return AssetType::None; }
        virtual AssetType GetAssetType() const { return AssetType::None; }

        bool IsValid() const { return Handle != 0; }
    };
}
