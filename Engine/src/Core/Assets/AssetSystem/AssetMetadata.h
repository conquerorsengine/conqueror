#pragma once

#include "Asset.h"
#include <filesystem>

namespace Conqueror {

    struct AssetMetadata
    {
        AssetHandle Handle = 0;
        AssetType Type = AssetType::None;
        std::filesystem::path FilePath;
        bool IsDataLoaded = false;
        bool IsValid = false;
    };
}
