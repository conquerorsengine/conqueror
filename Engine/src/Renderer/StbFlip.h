#pragma once

#include <stb_image.h>

namespace Conqueror
{
    // stb_image'in "dikey çevir" ayarı (OpenGL'de satır 0 görüntünün altı). Ayar eskiden programın tamamında ortaktı: bir doku yüklemesi
    // açık bırakınca, ardından gelen ve çevrilmemiş görüntü bekleyen yükleme (ya da tersi) ters görüntü alıyordu; örneğin aynı fotoğraf
    // dosya yöneticisinden ikinci kez bırakılınca Content Browser küçük resmi baş aşağı çıkıyordu (278). Her yükleme ayarı yalnız kendi iş
    // parçacığında ve yalnız bu kapsam boyunca açar / kapatır; kapsam bitince her iş parçacığında "çevirme" (varsayılan) geri gelir
    struct StbFlipScope
    {
        explicit StbFlipScope(bool flip) { stbi_set_flip_vertically_on_load_thread(flip ? 1 : 0); }
        ~StbFlipScope() { stbi_set_flip_vertically_on_load_thread(0); }
        StbFlipScope(const StbFlipScope&) = delete;
        StbFlipScope& operator=(const StbFlipScope&) = delete;
    };
}
