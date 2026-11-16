#pragma once

#include "Core/Base/Base.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Conqueror::AssetBundles
{
    // Unity AssetBundle: varlıklar (ve bağımlılıkları) ayrı bir arşive (.cqbundle) paketlenir; oyun çalışırken LoadAssetBundle ile açılır
    // (indirilebilir içerik, yamalar, DLC). Arşiv: "CQAB" başlığı, dosya tablosu (proje göreli yol, boyut, sıkıştırılmış boyut, CRC32),
    // ardından dosya başına zstd bloğu. Açılan arşiv önbellek klasörüne çözülür (Unity'nin paket önbelleği gibi; aynı CRC'li arşiv yeniden
    // çözülmez) ve o klasör varlık arama köklerine eklenir: sahne, prefab, doku, ses ... yükleyicileri "Assets/..." yolunu orada da bulur.
    // Aynı kap biçimini taşınabilir oyun paketi de (.cqspace, Core/Project/GamePackage.h) kullanır.
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

    // İlerleme geri çağrısı (bitti, toplam); false döndürürse işlem iptal edilir
    using Progress = std::function<bool(size_t done, size_t total)>;
    struct WriteEntry
    {
        std::string Path;           // arşivdeki proje göreli yol
        std::string Source;         // kaynak dosyanın tam yolu; boşsa Data yazılır
        std::string Data;
        // Verilmişse içerik yazım anında üretilir (önceki girdilerin tablo bilgisiyle: ör. içerik özetli bildirim son girdi olarak)
        std::function<std::string(const std::vector<FileEntry>& previous)> Generate;
    };
    // Akışlı yazım: dosyalar tek tek sıkıştırılır (bellekte yalnız o anki dosya), tablo sonda yerine yazılır. written: girdilerin
    // tablo bilgisi (boyut, CRC) — isteğe bağlı
    CQ_API bool WriteEntries(const std::string& bundleFile, const std::vector<WriteEntry>& entries, std::string* error = nullptr,
                             const Progress& progress = {}, std::vector<FileEntry>* written = nullptr);
    // Tek bir girdiyi okur (diğerlerini açmadan; boyut ve CRC doğrulanır)
    CQ_API bool ReadEntry(const std::string& bundleFile, const std::string& path, std::string& data, std::string* error = nullptr);
    // Bütün girdileri klasöre çözer (yol güvenliği + boyut + CRC doğrulaması)
    CQ_API bool Extract(const std::string& bundleFile, const std::string& directory, std::string* error = nullptr,
                        const Progress& progress = {});

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
