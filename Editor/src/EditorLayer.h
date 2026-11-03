#pragma once
#include <unordered_set>

#include "Core/Layer.h"
#include "EditorState.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/LogPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ViewportPanel.h"
#include "Panels/GamePanel.h"
#include "Panels/LightingPanel.h"
#include "Panels/Audio/AudioMixerPanel.h"
#include "Panels/ProfilerPanel.h"
#include "Panels/MultiplayerPanel.h"
#include "Panels/BuildSettingsPanel.h"
#include "Panels/TimelinePanel.h"
#include "Panels/Animation/AnimationWindow.h"
#include "Panels/SpriteEditorPanel.h"
#include "Panels/InputActionsEditorPanel.h"
#include "Panels/TerrainPanel.h"
#include "Panels/MeshPaintTool.h"
#include "Panels/TilePalettePanel.h"
#include "Panels/AddonManagerPanel.h"
#include "Addons/AddonManager.h"
#include "Panels/ProjectSettingsPanel.h"
#include "Panels/StatsPanel.h"
#include "Panels/Animator/AnimatorPanel.h"
#include "Panels/Shader/ShaderEditorPanel.h"
#include "Panels/VisualScript/VisualScriptPanel.h"
#include "Panels/CodeEditorPanel.h"
#include "Panels/AIPanel.h"
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
        void AddonsMenu();
        // Oyun betiklerinin girdisi: Game görünümü odaktayken ImGui'den (otomasyon dahil) beslenir
        void FeedGameInput();
        void SetupAddonHost();
        // Addon "Inserts" prefab'ını kameranın baktığı yere ekle
        Entity SpawnAddonInsert(const AddonManifest& addon, const AddonInsertItem& item);

        // Edit operations
        void HandleKeyboardShortcuts();
        void EditCopy();
        void EditCut();
        void EditPaste();
        void EditDuplicate();
        void EditDelete();
        void EditSelectAll();
        void SaveUndoState();
        void TrackCreatedEntities();
        Scene* m_TrackScene = nullptr;
        size_t m_TrackCount = 0;
        uint64_t m_TrackVersion = 0;
        std::unordered_set<uint64_t> m_TrackIDs;
        void PerformUndo();
        void PerformRedo();
        void RefreshAllPanels();
        // Oynat/durdur sahne değişiminde seçimi UUID ile korur
        uint64_t GetSelectedEntityUUID() const;
        void RestoreSelectionByUUID(uint64_t uuid);
        void UpdateHierarchySelection();

        // Scene operations
        void NewScene();
        void ApplyNewSceneTemplate();
        // File > New Scene penceresi (yerleşik şablonlar + projedeki .cqscenetemplate dosyaları)
        void DrawNewSceneDialog();
        void DrawMessagePopup();
        void ConfirmNewScene(const std::string& choice);
        bool CreateNewScene(const std::string& choice);   // "builtin:basic" | "builtin:empty" | şablon dosyası yolu
        bool OpenInExternalEditor(const std::string& path);
        void OpenScene();
        bool OpenScene(const std::filesystem::path& path);
        void SaveScene();
        void SaveSceneAs();
        // Çökme kurtarma: otomatik kayıt <proje>/Library/Recovery altına yazar (gerçek sahne dosyasının üzerine değil)
        std::filesystem::path GetRecoveryPath(const std::filesystem::path& scenePath) const;
        void WriteRecoverySnapshot();
        void RemoveRecoverySnapshot(const std::filesystem::path& scenePath);
        void CheckRecoverySnapshot(const std::filesystem::path& scenePath);
        void DrawRecoveryPopup();
        // Kaydedilmemiş değişiklikler: kapatma / yeni sahne / başka sahne açma öncesi sor
        bool HasUnsavedChanges();
        bool SaveStreamedLevel(Entity root);
        void RequestNewScene();
        void RequestOpenScene(const std::filesystem::path& path);
        void DrawUnsavedChangesPopup();
        bool OnCloseRequested() override;
        void LoadRuntimeScene(const std::filesystem::path& path);
        void WriteThumbnail();
        
        // Project operations
        void NewProject();
        void OpenProject();
        void SaveProject();
        
        // Viewport kamera durumunu al
        EditorCamera* GetEditorCamera();

        // Seçimi tüm panellere uygula (hiyerarşi, inspector, viewport, animator ...)
        void SelectEntityEverywhere(Entity entity);
        void SyncPanelContexts();
        Scene* m_PanelSceneContext = nullptr;
        // Geri alma geçmişinin ait olduğu düzenleyici sahnesi (sahne değişince geçmiş temizlenir)
        Scene* m_UndoSceneOwner = nullptr;
        // Varsayılan yerleşimin araç bölgesi (Hiyerarşi altı): addon panelleri buraya sekme olarak yerleşir
        unsigned int m_ToolsDockId = 0;
        int m_RibbonTab = 0;

        // UI otomasyon komutları (select, play, stop, focus, panel ...)
        void RegisterAutomationCommands();
        bool* FindPanelVisibilityFlag(const std::string& name);
        std::string m_PendingFocusWindow;
        bool m_ReturnToViewportOnStop = false;
        // Prefab Revert varlığı yeniden oluşturur: denetçi çizimi bittikten sonra (sonraki karede) uygulanır
        Entity m_PendingPrefabRevert;
        // Sahne anlık görüntüsüyle geri alınabilir eylem (addon'lar, toplu düzenlemeler)
        std::function<void(const std::string&, const std::function<void()>&)> m_AddonUndoable;
        bool m_StepFrame = false;

    private:
        
        // Scene
        std::shared_ptr<Scene> m_ActiveScene;
        std::shared_ptr<Scene> m_EditorScene;
        std::shared_ptr<Scene> m_RuntimeScene; // Play mode için kopyalanan scene
        
        // Panels
        std::unique_ptr<HierarchyPanel> m_HierarchyPanel;
        std::unique_ptr<InspectorPanel> m_InspectorPanel;
        std::unique_ptr<LogPanel> m_LogPanel;
        std::unique_ptr<ConsolePanel> m_ConsolePanel;   // terminal
        std::unique_ptr<ContentBrowserPanel> m_ContentBrowserPanel;
        std::unique_ptr<ViewportPanel> m_ViewportPanel;
        std::unique_ptr<GamePanel> m_GamePanel;
        std::unique_ptr<LightingPanel> m_LightingPanel;
        std::unique_ptr<AudioMixerPanel> m_AudioMixerPanel;
        std::unique_ptr<ProfilerPanel> m_ProfilerPanel;
        std::unique_ptr<MultiplayerPanel> m_MultiplayerPanel;
        std::unique_ptr<BuildSettingsPanel> m_BuildSettingsPanel;
        std::unique_ptr<TimelinePanel> m_TimelinePanel;
        std::unique_ptr<AnimationWindow> m_AnimationWindow;   // Window > Animation (Unity Animation penceresi)
        std::unique_ptr<SpriteEditorPanel> m_SpriteEditorPanel;
        std::unique_ptr<InputActionsEditorPanel> m_InputActionsEditor;
        std::unique_ptr<TerrainPanel> m_TerrainPanel;
        std::unique_ptr<TilePalettePanel> m_TilePalettePanel;
        std::unique_ptr<AddonManagerPanel> m_AddonManagerPanel;
        std::unique_ptr<class ImportActivityPanel> m_ImportActivityPanel;
        bool m_ShowImportActivity = false;
        std::unique_ptr<ProjectSettingsPanel> m_ProjectSettingsPanel;
        std::unique_ptr<StatsPanel> m_StatsPanel;
        std::unique_ptr<AnimatorPanel> m_AnimatorPanel;
        std::unique_ptr<ShaderEditorPanel> m_ShaderEditorPanel;
        std::unique_ptr<VisualScriptPanel> m_VisualScriptPanel;
        std::unique_ptr<CodeEditorPanel> m_CodeEditorPanel;
        std::unique_ptr<AIPanel> m_AIPanel;

        // Panel visibility
        bool m_ShowHierarchy = true;
        bool m_ShowInspector = true;
        bool m_ShowLog = true;
        bool m_ShowConsole = true;
        bool m_ShowContentBrowser = true;
        bool m_ShowViewport = true;
        bool m_ShowGame = true;
        bool m_ShowLighting = true;
        bool m_ShowAudioMixer = false;
        bool m_ShowProfiler = false;
        bool m_ShowMultiplayer = false;
        bool m_ShowBuildSettings = false;
        bool m_ShowTimeline = false;
        bool m_ShowAnimation = false;
        bool m_ShowSpriteEditor = false;
        bool m_ShowTerrain = true;
        bool m_ShowTilePalette = false;
        bool m_ShowAddonManager = false;
        bool m_ShowProjectSettings = true;
        bool m_ShowStats = true;
        bool m_ShowAnimator = false;
        bool m_ShowShaderGraph = false;
        bool m_ShowNavigation = false;
        bool m_ShowAbout = false;
        bool m_ShowAIPanel = false;

        // Scene file path
        std::filesystem::path m_CurrentScenePath;

        // New Project Popup state
        bool m_ShowNewProjectPopup = false;
        char m_NewProjectName[256] = "UntitledProject";
        char m_NewProjectLocation[256] = "";
        
        // Otomatik kaydetme
        float m_AutoSaveTimer = 0.0f;
        float m_SceneAutoSaveTimer = 0.0f;   // Project Settings > Editor > Auto Save
        std::filesystem::path m_RecoveryCandidate;   // açılan sahne için bulunan kurtarma kopyası
        bool m_OpenRecoveryPopup = false;
        enum class PendingSceneAction { None, Close, NewScene, OpenScene };
        PendingSceneAction m_PendingAction = PendingSceneAction::None;
        std::filesystem::path m_PendingOpenScene;
        bool m_OpenUnsavedPopup = false;
        bool m_OpenNewSceneDialog = false;
        std::string m_NewSceneChoice = "builtin:basic";
        std::string m_PendingNewSceneChoice = "builtin:basic";
        bool m_CloseConfirmed = false;
        const float m_AutoSaveInterval = 300.0f; // 5 dakika (300 saniye)
        
        // FPS tracking
        float m_FPS = 0.0f;
        float m_FrameTime = 0.0f;
        bool m_GameMouseCaptured = false;   // oyun fareyi kilitledi: paneller fareyi görmez
        bool m_WindowWasFocused = false;
};
}
