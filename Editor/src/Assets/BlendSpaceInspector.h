#pragma once

#include <string>

namespace Conqueror::Editor::BlendSpaceInspector
{
    // .cqblendspace Inspector'ı (Unreal Blend Space / Blend Space 1D düzenleyicisi): eksen adları ve aralıkları, ızgara
    // üzerinde örnekler (klip noktaları: sürükle, çift tıkla ekle, sağ tık sil), önizleme noktası ve o noktadaki klip
    // ağırlıkları, örnek tablosu. Animator'da bir Blend Tree durumunun "Blend Space" alanına konur.
    void Draw(const std::string& path);
}
