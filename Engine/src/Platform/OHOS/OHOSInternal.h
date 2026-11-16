#pragma once

// Platform/OHOS iç arayüzü: NAPI modülü (OHOSModule.cpp) ile GLFW katmanı (GLFWOhos.cpp) arasında. Motorun geri kalanı
// OHOSPlatform.h'yi kullanır.
#if defined(__OHOS__)

#include <string>

struct OH_NativeXComponent;

namespace Conqueror::OHOS::Detail
{
    // XComponent yüklendi (NAPI Init'te dışa aktarılan OH_NATIVE_XCOMPONENT_OBJ): yüzey, girdi ve kare geri çağrıları kaydedilir.
    // ArkTS ana iş parçacığında çağrılır
    void AttachXComponent(OH_NativeXComponent* component);
    // Yazılım / donanım giriş yönteminden metin (UTF-8): karakter olayları olarak motora gider
    void InsertText(const std::string& utf8);
    // Pencere kapatılmak isteniyor (geri hareketi, pencerenin kapat düğmesi): motor kendi kapanış akışını yürütür
    void RequestClose();
    // Uygulama sonlanıyor (EntryAbility.onDestroy): motor döngüsü biter, yüzey beklenmez
    void RequestDestroy();
    // Motor iş parçacığı bitti (main döndü)
    void EngineFinished();
}

#endif
