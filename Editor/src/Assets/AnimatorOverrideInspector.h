#pragma once

#include <string>

namespace Conqueror::Editor::AnimatorOverrideInspector
{
    // .cqoverride Inspector'ı (Unity Animator Override Controller): temel Animator Controller, klip listesini almak için
    // model, temel controller'daki her klip için yerine geçecek klip (Original -> Override). Animation bileşeninin
    // Controller yuvası bu dosyayı da alır: durumlar, geçişler, parametreler temelden; klipler buradan
    void Draw(const std::string& path);

    // Varsayılan dosya içeriği (Create > Animation > Animator Override Controller)
    std::string DefaultContent();
}
