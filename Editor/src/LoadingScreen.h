#pragma once

#include <filesystem>
#include <string>

// Proje açılırken yükleme penceresi (270, Unity'nin proje açılış ekranı gibi ama kendi görselimizle): pencerenin ortasında bir kart:
// üstte projenin küçük resmi (yoksa motorun amblemi), ilerleme çubuğu, o an yüklenen dosya, proje adı, motor logosu ve sürümü.
// Yükleme kodu LoadingProgress::Report ile ne yaptığını söyler; kart en çok saniyede ~30 kez yeniden çizilir. Küçük projelerde bir an
// görünüp dolar, Archviz gibi büyüklerde yükleme boyunca kalır
namespace Conqueror::Editor::LoadingScreen
{
    void Begin(const std::string& projectName, const std::filesystem::path& projectDir);
    void End();
    bool IsActive();
    // Testler: kaç kez çizildi, son aşama ve yüzde
    int DrawCount();
    float LastFraction();
}
