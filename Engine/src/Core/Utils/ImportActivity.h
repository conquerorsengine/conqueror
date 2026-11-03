#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

// Import Activity (Unity "Import Activity" penceresi): diskten yüklenen / yeniden yüklenen varlıkların kaydı —
// hangi içe aktarıcı, ne kadar sürdü, sonuç. Editörde Window > Import Activity ve Content Browser sağ tık menüsü.
namespace Conqueror::ImportActivity
{
    struct Record
    {
        std::string Path;
        std::string Importer;   // "Texture", "Model", "Material", "Audio"
        std::string Info;       // "2048x2048", "12 meshes, 4 materials"
        double Milliseconds = 0.0;
        double Time = 0.0;      // kayıt anı (uygulama başından saniye)
        bool Reimport = false;
        bool Ok = true;
    };

    CQ_API void Add(const std::string& path, const std::string& importer, double milliseconds, bool ok, const std::string& info = std::string(),
                    bool reimport = false);
    CQ_API const std::vector<Record>& Records();
    CQ_API void Clear();
    // Her eklemede artar (pencere yenilensin)
    CQ_API uint64_t Version();
}
