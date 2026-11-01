#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>
#include <vector>

// Addon (eklenti) sistemi: bir klasör + addon.yaml bildirimi.
//   Katalog : <motor>/Resources/Addons/<klasör>   (motorla gelen, projeye kurulabilir)
//   Kurulu  : <proje>/Packages/<klasör>           (projenin parçası, sürüm kontrolüne girer)
// Addon şunları sağlayabilir: editör betiği (menü öğeleri, ADDONS sekmesi düğmeleri, özel paneller),
// oyun betiği bileşenleri (Add Component > Addons) ve sahneye eklenebilir prefab'lar.
namespace Conqueror
{
    struct AddonInsertItem
    {
        std::string Name;
        std::string Icon = "cube";       // Font Awesome adı
        std::string Prefab;               // addon klasörüne göre
        std::string Tooltip;
    };

    struct AddonManifest
    {
        std::string Id;                   // "conqueror.quick-tools"
        std::string Name;
        std::string Version = "1.0.0";
        std::string Author;
        std::string Description;
        std::string Category = "Tools";
        std::string Icon = "puzzle-piece";
        std::string EditorScript;         // addon klasörüne göre (ör. "Editor/QuickTools.cqs")
        std::vector<std::string> Components;   // oyun betikleri (addon klasörüne göre)
        std::vector<AddonInsertItem> Inserts;
        std::vector<std::string> Tags;
        std::vector<std::string> Dependencies;   // kurulurken önce kurulan addon kimlikleri
        std::filesystem::path Directory;
        bool BuiltIn = false;             // motor kataloğundan
        bool Installed = false;           // projenin Packages klasöründe
    };

    class CQ_API AddonManager
    {
    public:
        // Kataloğu ve projeye kurulu addon'ları yeniden tara (Version artar)
        static void Refresh();
        static const std::vector<AddonManifest>& GetInstalled();
        static const std::vector<AddonManifest>& GetCatalog();
        static const AddonManifest* FindInstalled(const std::string& id);
        static const AddonManifest* FindInCatalog(const std::string& id);

        static bool IsEnabled(const std::string& id);
        static void SetEnabled(const std::string& id, bool enabled);

        // Katalogdan projeye kopyala / projeden kaldır
        static bool Install(const std::string& catalogId, std::string* error = nullptr);
        static bool Uninstall(const std::string& id, std::string* error = nullptr);
        // Packages altında şablon addon oluştur (bildirim + örnek editör betiği); oluşturulan kimliği döner
        static bool CreateNew(const std::string& name, std::string* outId, std::string* error = nullptr);

        static bool LoadManifest(const std::filesystem::path& manifestFile, AddonManifest& out, std::string* error = nullptr);
        static std::filesystem::path GetPackagesDirectory();
        static std::filesystem::path GetCatalogDirectory();
        // Herhangi bir değişiklikte artar (editör addon'ları yeniden yükler)
        static uint32_t GetVersion();
    };
}
