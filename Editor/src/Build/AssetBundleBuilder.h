#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror::Editor::AssetBundleBuilder
{
    // Unity AssetBundle atamaları (Inspector altındaki "AssetBundle" alanı): proje kökündeki AssetBundles.yaml'da (proje göreli yol ->
    // paket adı). Klasöre verilen ad içindeki tüm dosyalara geçer; dosyanın kendi adı klasörünkünden önce gelir.
    std::string GetBundleName(const std::string& assetPath);      // kendisinden ya da üst klasörden
    std::string GetAssignedName(const std::string& assetPath);    // yalnız kendisine verilen
    void SetBundleName(const std::string& assetPath, const std::string& bundle);   // "" kaldırır
    std::vector<std::string> GetAllBundleNames();
    // Paketin açıkça atanmış dosyaları (klasör içerikleri dahil, proje göreli)
    std::vector<std::string> GetAssetsInBundle(const std::string& bundle);
    // Oyun derlemesinde paketlere atanmış varlıkları dışarıda bırakmak için
    bool IsInAnyBundle(const std::string& relativePath);

    using DependencyFn = std::function<std::vector<std::filesystem::path>(const std::filesystem::path&)>;
    struct Result
    {
        bool Ok = false;
        int Bundles = 0;
        int Files = 0;
        std::string OutputDir;
        std::string Error;
    };
    // Build AssetBundles: <proje>/AssetBundles/<platform>/<ad>.cqbundle + <platform>.manifest. Başka pakete atanmamış bağımlılıklar
    // pakete katılır (Unity'deki örtük ekleme), başka pakettekiler manifeste paket bağımlılığı olarak yazılır
    Result Build(const std::string& platform, const DependencyFn& dependencies);
}
