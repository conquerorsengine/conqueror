#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // File > Build Settings (Unity benzeri): derlenecek sahneler (sıra + etkinlik), hedef platform,
    // oyuncu ayarları (pencere, tam ekran, simge), çıktı klasörü ve "Build" / "Build And Run".
    class BuildSettingsPanel
    {
    public:
        void OnImGuiRender(bool* open);

        // Editör: açık sahnenin yolu ("Add Open Scenes") ve proje kaydı
        void SetCurrentSceneProvider(std::function<std::filesystem::path()> fn) { m_CurrentScene = std::move(fn); }
        void SetSaveProject(std::function<void()> fn) { m_SaveProject = std::move(fn); }

        // Otomasyon ve menü
        bool StartBuild(bool run, std::string* error = nullptr);
        // Tek tıkla: bu bilgisayarda modülü hazır olan tüm platformlar (Builds/<Platform>)
        bool StartBuildAll(std::string* error = nullptr);
        static std::vector<std::string> ReadyPlatforms();
        static std::filesystem::path DefaultOutputDir();
        static std::filesystem::path EngineBinDir();

    private:
        void DrawScenes();
        void DrawPlatforms();
        void DrawPlayerSettings();
        void DrawBuildSection();
        void MarkDirty() { m_Dirty = true; }
        void SaveIfDirty();

        std::function<std::filesystem::path()> m_CurrentScene;
        std::function<void()> m_SaveProject;
        bool m_Dirty = false;
        bool m_ShowLog = false;
        int m_DragFrom = -1;
        std::string m_Message;
    };
}
