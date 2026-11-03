#pragma once

#include <string>

namespace Conqueror::Editor::AvatarMaskInspector
{
    // .cqmask Inspector'ı (Unity Avatar Mask): tıklanabilir insan figürü (bölümler), modelden iskelet alınıp kemik kemik
    // seçim (ağaç, süzgeç), Animator katmanının Mask yuvasında kullanılır.
    void Draw(const std::string& path);
}
