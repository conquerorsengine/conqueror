#pragma once

#include <filesystem>
#include <string>

// Proje açılırken yükleme penceresi (270, Unity'nin proje açılış ekranı gibi ama kendi görselimizle): ayrı, başlık çubuğu ve düğmeleri
// olmayan küçük bir pencere (kart): üstte projenin küçük resmi (yoksa motorun amblemi), ilerleme çubuğu, o an yüklenen dosya, proje
// adı, motor logosu ve sürümü. Editörün ana penceresi yükleme boyunca gizlidir; ilk oturmuş karesi gösterilince açılış penceresi kapanır.
// Yükleme kodu LoadingProgress::Report ile ne yaptığını söyler; kart en çok saniyede ~30 kez yeniden çizilir
namespace Conqueror::Editor::LoadingScreen
{
    void Begin(const std::string& projectName, const std::filesystem::path& projectDir);
    void End();                // yükleme bitti: kart %100'de kalır
    void Finish();             // editörün ilk karesi ekranda: açılış penceresi kapanır
    bool IsActive();
    // Testler: kaç kez çizildi, son aşama ve yüzde
    int DrawCount();
    float LastFraction();
    bool HadSplashWindow();
    bool SplashDecorated();
    bool SplashOpen();
    bool MainVisibleWhileLoading();
    int SplashWidth();
    int SplashHeight();
}
