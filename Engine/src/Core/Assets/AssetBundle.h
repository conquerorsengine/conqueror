#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <utility>
#include <vector>

namespace Conqueror::AssetBundles
{
    // Unity AssetBundle: varlıklar (ve bağımlılıkları) ayrı bir arşive (.cqbundle) paketlenir; oyun çalışırken LoadAssetBundle ile açılır
    // (indirilebilir içerik, yamalar, DLC). Arşiv: "CQAB" başlığı, dosya tablosu (proje göreli yol, boyut, sıkıştırılmış boyut, CRC32),
    // ardından dosya başına zstd bloğu. Açılan arşiv önbellek klasörüne çözülür (Unity'nin paket önbelleği gibi; aynı CRC'li arşiv yeniden
    // çözülmez) ve o klasör varlık arama köklerine eklenir: sahne, prefab, doku, ses ... yükleyicileri "Assets/..." yolunu orada da bulur.
    constexpr const char* Extension = ".cqbundle";

    struct FileEntry
    {
        std::string Path;           // proje göreli ("Assets/Textures/a.png")
        uint64_t Size = 0;
        uint64_t CompressedSize = 0;
        uint32_t Crc = 0;
    };

    // files: (arşivdeki proje göreli yol, kaynak dosyanın tam yolu)
    CQ_API bool Write(const std::string& bundleFile, const std::vector<std::pair<std::string, std::string>>& files, std::string* error = nullptr);
    CQ_API bool ReadTable(const std::string& bundleFile, std::vector<FileEntry>& entries, std::string* error = nullptr);
    // CRC32 (arşiv dosyasının tamamı; manifest ile doğrulama)
    CQ_API uint32_t FileCrc(const std::string& path);

    // Açar ve bağlar; tanıtıcı (> 0) ya da 0. Aynı arşiv ikinci kez açılırsa aynı tanıtıcı döner
    CQ_API int Load(const std::string& bundleFile, std::string* error = nullptr);
    CQ_API bool Unload(int handle);
    CQ_API void UnloadAll();
    CQ_API bool Contains(int handle, const std::string& assetPath);
    CQ_API std::vector<std::string> GetAssetNames(int handle);
    CQ_API std::vector<std::string> GetLoadedBundles();   // açık arşivlerin adları
    // Açık arşivlerde proje göreli yol: çözülmüş dosyanın tam yolu, yoksa boş
    CQ_API std::string ResolveMounted(const std::string& relativePath);
}
