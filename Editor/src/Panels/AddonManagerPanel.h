#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace Conqueror::Editor
{
    // Addon Yöneticisi (Unity Package Manager / Blender Add-ons): kurulu ve katalog addon'ları, ayrıntılar,
    // etkinleştir / kur / kaldır / güncelle, yeni addon oluştur, betiği düzenle, klasörü aç.
    class AddonManagerPanel
    {
    public:
        void OnImGuiRender(bool* open);
        // Kod düzenleyicide dosya aç (EditorLayer bağlar)
        std::function<void(const std::filesystem::path&)> OpenFile;

        void Select(const std::string& id, bool catalog) { m_SelectedId = id; m_ShowCatalog = catalog; }

    private:
        void DrawList();
        void DrawDetails();

        bool m_ShowCatalog = false;
        std::string m_SelectedId;
        std::string m_Search;
        std::string m_NewName = "My Addon";
        std::string m_ConfirmUninstall;
        std::string m_LastError;
    };
}
