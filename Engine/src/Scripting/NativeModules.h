#pragma once

#include "Core/Assets/SimpleAsset.h"
#include "Core/Base/Base.h"

#include <filesystem>
#include <string>
#include <vector>

// Native Script Module (.cqmodule, Unity Assembly Definition karşılığı): dosyanın bulunduğu klasördeki C / C++ betikleri
// o modülün tanımları, başvuruları ve platform kısıtlarıyla derlenir. Modül dışlanırsa betikleri derlemeye girmez.
namespace Conqueror::NativeModules
{
    struct Module
    {
        std::string Name;
        std::filesystem::path Folder;
        std::string Text;                   // dosya içeriği (değişince yeniden derlenir)
        SimpleAssets::Data Data;
        bool Active = true;
        std::string Why;                    // neden dışlandı
    };

    // Projedeki modüller; başvurular ve platform / tanım kısıtları çözülmüş halde
    CQ_API std::vector<Module> Resolve();
    CQ_API std::string Fingerprint(const std::vector<Module>& modules);
    // Kaynağı içeren en yakın modül (yoksa nullptr)
    CQ_API const Module* ForSource(const std::vector<Module>& modules, const std::filesystem::path& source);
    // Bu modülün derleyici bayrakları (-D, -I)
    CQ_API std::string CompilerFlags(const Module& module);
    // Derlenecek birimler: modülsüz kaynaklar olduğu gibi, modüllüler tanımlarıyla sarmalanır, dışlananlar çıkar
    CQ_API std::vector<std::filesystem::path> ApplyModules(const std::vector<Module>& modules, const std::vector<std::filesystem::path>& sources,
                                                           const std::filesystem::path& outDir, const std::string& extension,
                                                           std::vector<std::filesystem::path>& includes);
}
