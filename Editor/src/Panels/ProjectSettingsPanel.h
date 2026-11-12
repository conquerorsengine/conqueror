#pragma once

#include <memory>
#include <string>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Unity tarzı Project Settings: sol gezinme + sayfa (General, Player, Quality, Graphics, Audio, Input, Physics, Physics 2D,
    // Navigation, Time, Tags & Layers, Editor, Script Execution Order, Scripting)
    class ProjectSettingsPanel
    {
    public:
        ProjectSettingsPanel() = default;
        ~ProjectSettingsPanel() = default;

        void OnImGuiRender();
        void SetContext(std::shared_ptr<Scene> context) { m_Context = context; }

        // Otomasyon
        void SelectPage(const std::string& page) { m_Page = page; }
        bool Save();
        // Proje dosyasını (.cqproj, proje ayarlarıyla) yaz: panel dışından da (Input Actions > Project-wide)
        static bool SaveProjectFile();

    private:
        void DrawGeneral();
        void DrawPhysics();
        void DrawPhysics2D();
        void DrawTime();
        void DrawTagsAndLayers();
        void DrawScripting();
        void DrawPlayer();
        void DrawPlatformSettings();
        void DrawQuality();
        void DrawGraphics();
        void DrawAudio();
        void DrawInput();
        void DrawNavigation();
        void DrawLocalization();
        void DrawEditorSettings();
        void DrawScriptOrder();
        void DrawCollisionMatrix();
        void ProbeMuJoCo(bool force);

        std::shared_ptr<Scene> m_Context;
        std::string m_Page = "Physics";
        bool m_Dirty = false;
        char m_Search[64] = {};

        // MuJoCo kitaplık algılama önbelleği
        bool m_MuJoCoProbed = false;
        bool m_MuJoCoFound = false;
        std::string m_MuJoCoInfo;
        std::string m_MuJoCoVersion;
        std::string m_ProbedPath;
        char m_NewLayer[64] = {};
        char m_NewTag[64] = {};
        // Input sayfası
        std::string m_InputContext;
        std::string m_ListenAction;
        char m_NewInputName[64] = {};
        char m_NewActionName[64] = {};
        // Script Execution Order
        char m_NewScriptOrder[96] = {};
    };
}
