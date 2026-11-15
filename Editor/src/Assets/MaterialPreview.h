#pragma once

#include <memory>
#include <string>

namespace Conqueror { class Material; }

// Unity Inspector'ın altındaki malzeme önizlemesi (Unreal Material Editor "Viewport", Godot malzeme önizlemesi): malzeme
// bir kürenin (ya da küp / düzlem / silindir) üstünde, güneş ışığı ve gökyüzü yansımasıyla. Görüntü canlıdır: Inspector'da
// değiştirilen değer aynı karede küreye yansır (önizleme sahnesi malzemenin paylaşılan örneğini kullanır). Sürükleyince döner.
namespace Conqueror::Editor::MaterialPreview
{
    // Malzeme varlığının Inspector'ında: "Preview" başlığı + kare görüntü + şekil seçimi
    void Draw(const std::string& materialPath);
    // Nesnenin Inspector'ındaki malzeme başlığında küçük küre (Unity "Lit (Material)" başlığındaki gibi). key: malzeme yolu
    // ya da örnek malzemede "instance:<kimlik>"
    bool Thumbnail(const std::shared_ptr<Material>& material, const std::string& key, float size);
    // Testler / otomasyon: son büyük önizlemenin ortadaki ortalama rengi (0-1, sRGB) ve çizim sayısı
    bool LastStats(float rgb[3], int& renders);
    // Son büyük önizlemenin dokusu (testler PNG'ye yazar)
    unsigned int LastTexture();
}
