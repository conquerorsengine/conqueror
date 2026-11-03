#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

// Paket arşivi (.cqpackage): gzip ile sıkıştırılmış tar (Unity .unitypackage gibi tek dosya; dış araçlarla da açılır).
// Girdi yolları proje köküne göre ("Assets/Materials/Brick.cqmat", "Packages/my.addon/addon.yaml").
namespace Conqueror::Archive
{
    struct Entry
    {
        std::string Path;
        std::vector<uint8_t> Data;
    };

    CQ_API bool WriteTarGz(const std::string& file, const std::vector<Entry>& entries, std::string* error = nullptr);
    CQ_API bool ReadTarGz(const std::string& file, std::vector<Entry>& out, std::string* error = nullptr);
}
