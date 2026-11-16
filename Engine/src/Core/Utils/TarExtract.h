#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <functional>
#include <string>

// Akışlı tar çıkarıcı (47: Hub'ın indirdiği editör sürümleri ve modüller): .tar, .tar.gz / .tgz, .tar.zst. Büyük arşivler belleğe
// alınmaz; çalıştırma izinleri ve (POSIX'te) sembolik bağlantılar korunur; hedef klasörün dışına çıkan yollar reddedilir.
// GNU uzun ad ('L' / 'K') ve pax ('x') başlıkları desteklenir
namespace Conqueror::TarExtract
{
    // progress: 0..1 (sıkıştırılmış dosyada okunan bayt oranı)
    CQ_API bool Extract(const std::filesystem::path& archive, const std::filesystem::path& destination, std::string& error,
                        const std::function<void(float)>& progress = {});
}
