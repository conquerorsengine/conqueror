#pragma once

#include "Core/Assets/SimpleAsset.h"

#include <functional>
#include <string>

namespace Conqueror::Editor::SimpleAssetInspector
{
    // Tabloyla tanımlı varlığın Inspector'ı (alanlar SimpleAssets tanımından)
    bool IsSimpleAsset(const std::string& path);
    void Draw(const std::string& path);
    // Türe özel ek arayüz (düğmeler, önizleme); alanların altında çizilir
    using Extra = std::function<void(const std::string& path, SimpleAssets::Data& data)>;
    void RegisterExtra(const std::string& kind, Extra extra);
    // Dosyayı varsayılan değerlerle oluşturur
    bool CreateDefault(const SimpleAssets::Def& def, const std::string& path);
}
