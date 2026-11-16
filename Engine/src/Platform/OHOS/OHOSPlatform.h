#pragma once

// HarmonyOS (OpenHarmony NDK) platform katmanı. Uygulama bir HAP'tir: ArkTS sayfası (Tools/HarmonyOS şablonu) tam pencere bir
// XComponent yüzeyi açar ve editör / oyuncu .so'sunu NAPI modülü olarak yükler. Motor kendi iş parçacığında masaüstündeki main
// gövdesiyle çalışır; GLFW işlevleri (GLFWOhos.cpp) XComponent olaylarını (yüzey, fare, klavye, dokunma, tekerlek, kare saati)
// ve OHNativeWindow üzerindeki EGL bağlamını sunar. Sistem arayüzü gerektiren işler (belge seçici, imleç biçimi, adres açma,
// pencere başlığı, kapanış) ArkTS tarafına istek olarak gider (setHostCallback).
#if defined(__OHOS__)

#include "Core/Base/Base.h"

#include <string>
#include <vector>

namespace Conqueror::OHOS
{
    using MainFunction = int (*)(int argc, char** argv);

    // Hedefin (editör / oyuncu) NAPI modülünü kaydeder; .so yüklenirken (statik kurucu) çağrılır. moduleName kitaplık adıdır
    // ("ConquerorEditor" -> libConquerorEditor.so; ArkTS: import engine from 'libConquerorEditor.so' ve XComponent libraryname).
    // main motor iş parçacığında çağrılır (masaüstündeki main gövdesi), contentFolder HAP rawfile içeriğinin çıkarılacağı
    // filesDir altı klasör ("editor", "game")
    CQ_API void RegisterModule(const char* moduleName, MainFunction main, const char* contentFolder);

    // Uygulama kum havuzu klasörleri (ArkTS context.filesDir / cacheDir), setup() ile gelir
    CQ_API std::string FilesDirectory();
    CQ_API std::string CacheDirectory();
    // HAP rawfile içeriğinin (cq_files.txt listesi) çıkarıldığı klasör: motor kaynakları (editör) ya da oyun dosyaları (oyuncu)
    CQ_API std::string ContentDirectory();
    // ArkTS start() ile gelen ek argümanlar (ör. açılacak proje klasörü)
    CQ_API const std::vector<std::string>& StartArguments();

    // ArkTS tarafına istek (Index.ets'teki setHostCallback): "cursor", "openUrl", "pick", "title", "quit". Geri çağrı yoksa false
    CQ_API bool PostHostRequest(const std::string& kind, const std::string& payload);

    // Belge seçici (ArkTS DocumentViewPicker). Motor iş parçacığında çağrılır ve kullanıcı seçene / vazgeçene kadar bekler.
    // extensions: ".png" biçiminde uzantılar (boş = tüm dosyalar). Seçilen yollar outPaths'e; vazgeçilirse ya da hata olursa
    // false (hata metni error'a)
    enum class PickerMode
    {
        OpenFile,
        OpenFiles,
        SaveFile,
        PickFolder,
    };
    CQ_API bool ShowPicker(PickerMode mode, const std::vector<std::string>& extensions, const std::string& defaultPath,
                           const std::string& defaultName, std::vector<std::string>& outPaths, std::string& error);

    // Adresi tarayıcıda / ilgili uygulamada açar (ArkTS context.openLink)
    CQ_API bool OpenURL(const std::string& url);
}

#endif
