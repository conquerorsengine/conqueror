#pragma once

#include "Core/Layer.h"
#include "EditorState.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ViewportPanel.h"
#include "Panels/GamePanel.h"
#include "Panels/LightingPanel.h"
#include "Panels/ProjectSettingsPanel.h"
#include "Panels/StatsPanel.h"
#include "Panels/Audio/AudioGraphPanel.h"
#include "Panels/Animator/AnimatorPanel.h"
#include "Panels/Shader/ShaderEditorPanel.h"
#include "Panels/CodeEditorPanel.h"
#include "UndoRedo/UndoRedoManager.h"
#include "Scene/Scene.h"

#include <memory>

namespace Conqueror::Editor
{
    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnImGuiRender() override;
        void OnEvent(Event& e) override;

    private:
        void SetupDockspace();
        void RenderMenuBar();
        void RenderToolbar();
        void RenderPanels();
        
        void OnPlayButtonPressed();
        void OnPauseButtonPressed();
        void OnStopButtonPressed();

        // Menu callbacks
        void FileMenu();
        void EditMenu();
        void GameObjectMenu();
        void WindowMenu();
        void HelpMenu();
        void BuildMenu();

        // Edit operations
        void HandleKeyboardShortcuts();
        void EditCopy();
        void EditCut();
        void EditPaste();
        void EditDuplicate();
        void EditDelete();
        void EditSelectAll();
        void SaveUndoState();
        void PerformUndo();
        void PerformRedo();
        void RefreshAllPanels();
        void UpdateHierarchySelection();

        // Scene operations
        void NewScene();
        void OpenScene();
        void SaveScene();
        void SaveSceneAs();
        
        // Project operations
        void NewProject();
        void OpenProject();
        void SaveProject();
        
        // Viewport kamera durumunu al
        EditorCamera* GetEditorCamera();

    private:
        
        // Scene
        std::shared_ptr<Scene> m_ActiveScene;
        std::shared_ptr<Scene> m_EditorScene;
        std::shared_ptr<Scene> m_RuntimeScene; // Play mode için kopyalanan scene
        
        // Panels
        std::unique_ptr<HierarchyPanel> m_HierarchyPanel;
        std::unique_ptr<InspectorPanel> m_InspectorPanel;
        std::unique_ptr<ConsolePanel> m_ConsolePanel;
        std::unique_ptr<ContentBrowserPanel> m_ContentBrowserPanel;
        std::unique_ptr<ViewportPanel> m_ViewportPanel;
        std::unique_ptr<GamePanel> m_GamePanel;
        std::unique_ptr<LightingPanel> m_LightingPanel;
        std::unique_ptr<ProjectSettingsPanel> m_ProjectSettingsPanel;
        std::unique_ptr<StatsPanel> m_StatsPanel;
        std::unique_ptr<AudioGraphPanel> m_AudioGraphPanel;
        std::unique_ptr<AnimatorPanel> m_AnimatorPanel;
        std::unique_ptr<ShaderEditorPanel> m_ShaderEditorPanel;
        std::unique_ptr<CodeEditorPanel> m_CodeEditorPanel;

        // Panel visibility
        bool m_ShowHierarchy = true;
        bool m_ShowInspector = true;
        bool m_ShowConsole = true;
        bool m_ShowContentBrowser = true;
        bool m_ShowViewport = true;
        bool m_ShowGame = true;
        bool m_ShowLighting = true;
        bool m_ShowProjectSettings = true;
        bool m_ShowStats = true;
        bool m_ShowAudioGraph = false;
        bool m_ShowAnimator = false;
        bool m_ShowShaderGraph = false;
        bool m_ShowNavigation = false;
        bool m_ShowAbout = false;

        // Scene file path
        std::filesystem::path m_CurrentScenePath;

        // New Project Popup state
        bool m_ShowNewProjectPopup = false;
        char m_NewProjectName[256] = "UntitledProject";
        char m_NewProjectLocation[256] = "";
        
        // Otomatik kaydetme
        float m_AutoSaveTimer = 0.0f;
        const float m_AutoSaveInterval = 300.0f; // 5 dakika (300 saniye)
        
        // FPS tracking
        float m_FPS = 0.0f;
        float m_FrameTime = 0.0f;
    };
}
