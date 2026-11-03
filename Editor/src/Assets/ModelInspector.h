#pragma once

#include <string>

namespace Conqueror::Editor::ModelInspector
{
    // Model dosyası (.fbx .gltf .glb .obj ...) Inspector'ı (Unity Model Import Settings): ağ / malzeme / animasyon bilgisi,
    // Materials: "Extract Materials" ve her malzeme için eşleme yuvası (Remapped Materials, <model>.meta).
    bool IsModel(const std::string& path);
    void Draw(const std::string& path);
}
