#pragma once

// iOS / iPadOS oyuncusu: UIKit uygulaması (Runtime/src/iOSMain.mm) motoru application:didFinishLaunching içinde kurar; pencere ve
// girdi GLFWiOS.mm'deki GLFW işlevleriyle (UIWindow + CAMetalLayer'lı görünüm), kareler ekran tazelemesinde CADisplayLink ile.
#include <string>

namespace Conqueror
{
    class Application;
}

namespace Conqueror::iOS
{
    // Oyun dosyaları uygulama paketinde (<App>.app/game, salt okunur); motor proje klasörüne önbellek yazdığı için ilk açılışta
    // (ya da paket sürümü değişince) Library/Application Support/game'e kopyalanır. Kopyalanan klasör döner (boşsa hata)
    std::string PrepareGameFolder();
    // Yazılabilir klasörler (uygulama kum havuzu)
    std::string LibraryDirectory();
    std::string CachesDirectory();

    // Ana döngü: ekran tazelemesinde Application::RunFrame (UIKit ana iş parçacığı); uygulama arka plana geçince duraklar
    // (iOS arka planda GPU işine izin vermez). Oyun kapanınca süreç biter
    void RunFrameLoop(Application* app);

    // Oyun görünümünün CAMetalLayer'ı (Metal arka ucu çizer)
    void* MetalLayer();
    // Nokta başına piksel (Retina: 2 / 3)
    float ContentScale();
    // Adresi Safari'de / ilgili uygulamada açar (http, https, mailto ...)
    void OpenURL(const std::string& url);
}
