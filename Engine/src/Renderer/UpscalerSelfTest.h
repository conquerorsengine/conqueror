#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Upscaler Self-Test: bu bilgisayarda seçilebilen her kenar yumuşatma yöntemini ve yükselticiyi birkaç kare çalıştırır; her biri için
// çalıştı / hata verdi / başka yönteme düştü / kullanılamaz (nedeniyle) yazan bir rapor dosyası üretir. Başka GPU'su olan biri
// (RTX, Intel Arc, RX 7000 ...) raporu gönderirse o karttaki yöntemler gerçekten denenmiş olur.
// Başlatma: Debug > Rendering > Run Upscaler Self-Test, konsolda r.UpscalerSelfTest [dosya], komut satırında --upscaler-self-test[=dosya]
namespace Conqueror::UpscalerSelfTest
{
    // Sunulan son görüntüyü RGBA8 olarak okur (editör: Game / Viewport görünümü; oyun: varsayılan çerçeve arabelleği)
    using ImageReader = std::function<bool(std::vector<uint8_t>& rgba, int& width, int& height)>;
    CQ_API void SetImageReader(ImageReader reader);

    // report boşsa çalışma klasöründe UpscalerSelfTest.txt. quitWhenDone: bitince uygulama kapanır (komut satırı)
    CQ_API bool Start(const std::filesystem::path& report = {}, bool quitWhenDone = false);
    CQ_API void Cancel();
    CQ_API bool IsRunning();
    // Application her karenin sahne çiziminden sonra (arayüzden ve takastan önce) çağırır
    CQ_API void OnFrameEnd();

    CQ_API std::string GetStatus();                     // "AA 12 / 33: MSAA" (çalışırken)
    CQ_API const std::string& GetLastReport();          // son raporun metni
    CQ_API const std::filesystem::path& GetLastReportPath();
}
