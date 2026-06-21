#include "EditorLayer.h"
#include "Core/Application.h"
#include "Core/Input/Input.h"
#include "Core/KeyCodes.h"
#include "Core/Serialization/SceneSerializer.h"

#include <imnodes.h>
#include "Core/Logging/Log.h"
#include "Core/Debug/DebugDraw.h"
#include "Scene/Components.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nfd.h>

#ifdef CQ_PLATFORM_LINUX
#include <unistd.h>
#include <sys/wait.h>

static void OpenURL(const std::string& url)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        setsid();
        execlp("xdg-open", "xdg-open", url.c_str(), nullptr);
        _exit(1);
    }
}
#endif

namespace Conqueror::Editor
{
    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    void EditorLayer::OnAttach()
    {
        CQ_CORE_INFO("EditorLayer: Creating editor scene...");
        
        // Renderer3D is now initialized by Application::Renderer::Init()
        
        // Initialize DebugDraw
        DebugDraw::Init();
        CQ_CORE_INFO("EditorLayer: DebugDraw initialized");
        
        // Create project and editor scene
        auto project = std::make_shared<Project>();
        Project::SetActive(project);

        m_EditorScene = std::make_shared<Scene>();
        m_ActiveScene = m_EditorScene;

        CQ_CORE_INFO("EditorLayer: Initializing panels...");

        try
        {
            // Initialize panels
            m_HierarchyPanel = std::make_unique<HierarchyPanel>(m_ActiveScene);
            CQ_CORE_INFO("EditorLayer: HierarchyPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create HierarchyPanel: {0}", e.what());
        }
        
        try
        {
            m_InspectorPanel = std::make_unique<InspectorPanel>();
            CQ_CORE_INFO("EditorLayer: InspectorPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create InspectorPanel: {0}", e.what());
        }
        
        try
        {
            m_ConsolePanel = std::make_unique<ConsolePanel>();
            CQ_CORE_INFO("EditorLayer: ConsolePanel created");
            
            // Log callback'ini bağla
            Log::SetConsoleCallback([this](const std::string& message, int level) {
                if (m_ConsolePanel)
                {
                    m_ConsolePanel->AddMessageFromLog(message, level);
                }
            });
            CQ_CORE_INFO("EditorLayer: Console callback registered");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ConsolePanel: {0}", e.what());
        }
        
        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ContentBrowserPanel...");
            m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();
            CQ_CORE_INFO("EditorLayer: ContentBrowserPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ContentBrowserPanel: {0}", e.what());
        }
        
        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ViewportPanel...");
            m_ViewportPanel = std::make_unique<ViewportPanel>();
            m_ViewportPanel->SetContext(m_ActiveScene);
            CQ_CORE_INFO("EditorLayer: ViewportPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ViewportPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating GamePanel...");
            m_GamePanel = std::make_unique<GamePanel>();
            m_GamePanel->SetContext(m_ActiveScene);
            CQ_CORE_INFO("EditorLayer: GamePanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create GamePanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating LightingPanel...");
            m_LightingPanel = std::make_unique<LightingPanel>();
            m_LightingPanel->SetContext(m_ActiveScene);
            CQ_CORE_INFO("EditorLayer: LightingPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create LightingPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ProjectSettingsPanel...");
            m_ProjectSettingsPanel = std::make_unique<ProjectSettingsPanel>();
            m_ProjectSettingsPanel->SetContext(m_ActiveScene);
            CQ_CORE_INFO("EditorLayer: ProjectSettingsPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ProjectSettingsPanel: {0}", e.what());
        }

        try
        {
            m_StatsPanel = std::make_unique<StatsPanel>();
            CQ_CORE_INFO("EditorLayer: StatsPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create StatsPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating AudioGraphPanel...");
            m_AudioGraphPanel = std::make_unique<AudioGraphPanel>();
            m_AudioGraphPanel->SetContext(m_ActiveScene.get());
            CQ_CORE_INFO("EditorLayer: AudioGraphPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create AudioGraphPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ShaderEditorPanel...");
            m_ShaderEditorPanel = std::make_unique<ShaderEditorPanel>();
            m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
            CQ_CORE_INFO("EditorLayer: ShaderEditorPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ShaderEditorPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating CodeEditorPanel...");
            m_CodeEditorPanel = std::make_unique<CodeEditorPanel>();
            CQ_CORE_INFO("EditorLayer: CodeEditorPanel created");

            if (m_ContentBrowserPanel)
            {
                m_ContentBrowserPanel->SetFileOpenCallback([this](const std::filesystem::path& path) {
                    if (m_CodeEditorPanel)
                    {
                        m_CodeEditorPanel->OpenFile(path);
                        m_CodeEditorPanel->GetIsOpen() = true;
                    }
                });
            }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create CodeEditorPanel: {0}", e.what());
        }

        // Link hierarchy selection to inspector
        if (m_HierarchyPanel && m_InspectorPanel)
        {
            m_HierarchyPanel->SetSelectionChangedCallback([this](Entity entity) {
                m_InspectorPanel->SetSelectedEntity(entity);
                if (m_ViewportPanel)
                    m_ViewportPanel->SetSelectedEntity(entity);
                if (m_AudioGraphPanel)
                    m_AudioGraphPanel->SetSelectedEntity(entity);
                if (m_ShaderEditorPanel)
                    m_ShaderEditorPanel->SetSelectedEntity(entity);
            });
            
            // Inspector'a scene context'i ver
            m_InspectorPanel->SetContext(m_ActiveScene.get());
        }

        CQ_CORE_INFO("EditorLayer: Initialization complete");

        // Command line'dan proje yolu geldiyse otomatik yükle
        auto& spec = Application::Get().GetSpecification();
        if (spec.CommandLineArgs.Count > 1)
        {
            std::string projectArg = spec.CommandLineArgs[1];
            std::filesystem::path projectPath(projectArg);
            CQ_CORE_INFO("[AutoLoad] Command line project path: {0}", projectPath.string());

            if (std::filesystem::exists(projectPath))
            {
                // Dizinde .cqproj dosyası ara
                for (auto& entry : std::filesystem::directory_iterator(projectPath))
                {
                    if (entry.path().extension() == ".cqproj")
                    {
                        CQ_CORE_INFO("[AutoLoad] Found cqproj: {0}", entry.path().string());
                        auto loadedProject = std::make_shared<Project>();
                        ProjectSerializer projSerializer(loadedProject);
                        if (projSerializer.Deserialize(entry.path()))
                        {
                            loadedProject->SetProjectDirectory(projectPath);
                            Project::SetActive(loadedProject);

                            m_CurrentScenePath = projectPath / loadedProject->GetConfig().StartScene;
                            CQ_CORE_INFO("[AutoLoad] StartScene path: {0}", m_CurrentScenePath.string());

                            if (std::filesystem::exists(m_CurrentScenePath))
                            {
                                m_ActiveScene = std::make_shared<Scene>();
                                m_EditorScene = m_ActiveScene;
                                SceneSerializer sceneSerializer(m_ActiveScene);
                                if (m_ViewportPanel)
                                    sceneSerializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
                                if (sceneSerializer.Deserialize(m_CurrentScenePath))
                                {
                                    CQ_CORE_INFO("[AutoLoad] Scene loaded successfully");

                                    if (m_HierarchyPanel) m_HierarchyPanel->SetContext(m_ActiveScene);
                                    if (m_InspectorPanel) m_InspectorPanel->SetContext(m_ActiveScene.get());
                                    if (m_ViewportPanel) m_ViewportPanel->SetContext(m_ActiveScene);
                                    if (m_GamePanel) m_GamePanel->SetContext(m_ActiveScene);
                                    if (m_LightingPanel) m_LightingPanel->SetContext(m_ActiveScene);
                                    if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->SetContext(m_ActiveScene);
                                    if (m_AudioGraphPanel) m_AudioGraphPanel->SetContext(m_ActiveScene.get());
                                    if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
                                    if (m_ContentBrowserPanel) m_ContentBrowserPanel->RefreshContext();
                                }
                                else
                                {
                                    CQ_CORE_ERROR("[AutoLoad] Failed to deserialize scene");
                                }
                            }
                            else
                            {
                                CQ_CORE_ERROR("[AutoLoad] StartScene not found: {0}", m_CurrentScenePath.string());
                            }
                        }
                        else
                        {
                            CQ_CORE_ERROR("[AutoLoad] Failed to deserialize cqproj: {0}", entry.path().string());
                        }
                        break;
                    }
                }
            }
            else
            {
                CQ_CORE_ERROR("[AutoLoad] Project path does not exist: {0}", projectPath.string());
            }
        }
    }

    void EditorLayer::OnDetach()
    {
        ImNodes::DestroyContext();
        DebugDraw::Shutdown();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        auto& editorState = EditorState::Get();
        
        // FPS hesapla
        m_FrameTime = ts.GetMilliseconds();
        m_FPS = 1.0f / ts.GetSeconds();
        
        // Otomatik kaydetme timer
        if (!m_CurrentScenePath.empty())
        {
            m_AutoSaveTimer += ts;
            if (m_AutoSaveTimer >= m_AutoSaveInterval)
            {
                SaveScene();
                m_AutoSaveTimer = 0.0f;
                CQ_CORE_INFO("Auto-save: Scene saved");
            }
        }
        
        // Update active scene
        if (editorState.IsPlaying())
        {
            m_ActiveScene->OnUpdateRuntime(ts);
        }
        else if (editorState.IsSimulating())
        {
            m_ActiveScene->OnUpdateSimulation(ts);
        }
        else
        {
            // Pass EditorCamera to scene for rendering
            if (m_ViewportPanel)
                m_ActiveScene->OnUpdateEditor(ts, m_ViewportPanel->GetEditorCamera(), m_ViewportPanel->GetViewportBounds());
        }

        // Update viewport
        if (m_ViewportPanel)
            m_ViewportPanel->OnUpdate(ts);

        // Update game panel
        if (m_GamePanel)
            m_GamePanel->OnUpdate(ts, editorState.IsPlaying());
        
        // Update stats
        if (m_StatsPanel)
        {
            m_StatsPanel->SetStats(Renderer::GetStats());
            m_StatsPanel->SetFPS(m_FPS, m_FrameTime);
        }
    }

    void EditorLayer::OnImGuiRender()
    {
        // New Project Popup
        if (m_ShowNewProjectPopup)
        {
            ImGui::OpenPopup("New Project");
            m_ShowNewProjectPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("New Project", NULL, ImGuiWindowFlags_NoResize))
        {
            ImGui::InputText("Project Name", m_NewProjectName, sizeof(m_NewProjectName));
            
            ImGui::InputText("Location", m_NewProjectLocation, sizeof(m_NewProjectLocation));
            ImGui::SameLine();
            if (ImGui::Button("Browse"))
            {
                nfdchar_t* outPath = nullptr;
                if (NFD_PickFolderU8(&outPath, nullptr) == NFD_OKAY)
                {
                    strncpy(m_NewProjectLocation, outPath, sizeof(m_NewProjectLocation) - 1);
                    NFD_FreePathU8(outPath);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                std::string projName(m_NewProjectName);
                std::string projLoc(m_NewProjectLocation);

                if (!projName.empty() && !projLoc.empty())
                {
                    std::filesystem::path projectPath = std::filesystem::path(projLoc) / projName;
                    
                    std::filesystem::create_directories(projectPath);
                    std::filesystem::create_directories(projectPath / "Assets" / "Scenes");
                    std::filesystem::create_directories(projectPath / "Assets" / "Scripts");
                    std::filesystem::create_directories(projectPath / "Assets" / "Textures");
                    std::filesystem::create_directories(projectPath / "Assets" / "Materials");
                    std::filesystem::create_directories(projectPath / "Assets" / "Models");
                    std::filesystem::create_directories(projectPath / "Assets" / "Audio");
                    std::filesystem::create_directories(projectPath / "Packages");

                    std::filesystem::path scenePath = projectPath / "Assets" / "Scenes" / "Scene.cqscene";
                    SceneSerializer sceneSerializer(m_ActiveScene);
                    if (m_ViewportPanel)
                    {
                        sceneSerializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
                    }
                    sceneSerializer.Serialize(scenePath);
                    m_CurrentScenePath = scenePath;

                    auto project = std::make_shared<Project>();
                    project->SetProjectDirectory(projectPath);
                    auto& config = project->GetConfig();
                    config.Name = projName;
                    config.StartScene = "Assets/Scenes/Scene.cqscene";
                    config.AssetDirectory = "Assets";
                    config.ScriptModulePath = "";

                    std::filesystem::path projectFilePath = projectPath / (projName + ".cqproj");
                    ProjectSerializer projectSerializer(project);
                    projectSerializer.Serialize(projectFilePath);

                    Project::SetActive(project);
                    m_EditorScene = m_ActiveScene;
                    
                    // Bağlamları güncelle
                    if (m_HierarchyPanel) m_HierarchyPanel->SetContext(m_ActiveScene);
                    if (m_InspectorPanel) m_InspectorPanel->SetContext(m_ActiveScene.get());
                    if (m_ViewportPanel) m_ViewportPanel->SetContext(m_ActiveScene);
                    if (m_GamePanel) m_GamePanel->SetContext(m_ActiveScene);
                    if (m_LightingPanel) m_LightingPanel->SetContext(m_ActiveScene);
                    if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->SetContext(m_ActiveScene);
                    if (m_AudioGraphPanel) m_AudioGraphPanel->SetContext(m_ActiveScene.get());
                    if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
                    if (m_ContentBrowserPanel) m_ContentBrowserPanel->RefreshContext();

                    CQ_CORE_INFO("New project created at: {0}", projectPath.string());
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        SetupDockspace();
        RenderMenuBar();
        RenderToolbar();
        RenderPanels();
        HandleKeyboardShortcuts();
    }

    void EditorLayer::OnEvent(Event& e)
    {
        if (m_ViewportPanel)
            m_ViewportPanel->OnEvent(e);
    }

    void EditorLayer::SetupDockspace()
    {
        static bool dockspaceOpen = true;
        static bool opt_fullscreen_persistant = true;
        bool opt_fullscreen = opt_fullscreen_persistant;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", &dockspaceOpen, window_flags);
        ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        float minWinSizeX = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;
        
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

            static auto first_time = true;
            if (first_time)
            {
                first_time = false;

                ImGui::DockBuilderRemoveNode(dockspace_id);
                ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

                // Önce sağ tarafı ayır (Inspector, Lighting, etc.)
                auto dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.25f, nullptr, &dockspace_id);
                
                // Alt tarafı ayır (Content Browser, Console)
                auto dock_id_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.25f, nullptr, &dockspace_id);
                
                // Toolbar için en üstte alan ayır (Hierarchy + Viewport'un üstünde)
                auto dock_id_top = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 0.045f, nullptr, &dockspace_id);
                
                // Sol tarafı ayır (Hierarchy)
                auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.28f, nullptr, &dockspace_id);

                // Toolbar dock node'una NoTabBar flag'i ekle
                ImGuiDockNode* toolbar_node = ImGui::DockBuilderGetNode(dock_id_top);
                if (toolbar_node)
                    toolbar_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

                // Dock windows
                ImGui::DockBuilderDockWindow("##toolbar", dock_id_top);
                ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left);
                ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
                ImGui::DockBuilderDockWindow("Lighting", dock_id_right);
                ImGui::DockBuilderDockWindow("Project Settings", dock_id_right);
                ImGui::DockBuilderDockWindow("Content Browser", dock_id_down);
                ImGui::DockBuilderDockWindow("Console", dock_id_down);
                ImGui::DockBuilderDockWindow("Game", dockspace_id);
                ImGui::DockBuilderDockWindow("Viewport", dockspace_id);
                ImGui::DockBuilderDockWindow("Audio Graph Editor", dockspace_id);
                ImGui::DockBuilderDockWindow("Shader Graph Editor", dockspace_id);
                ImGui::DockBuilderDockWindow("Code Editor###CodeEditor", dockspace_id);

                ImGui::DockBuilderFinish(dockspace_id);
            }
        }

        style.WindowMinSize.x = minWinSizeX;
    }

    void EditorLayer::RenderMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            FileMenu();
            EditMenu();
            GameObjectMenu();
            WindowMenu();
            HelpMenu();
            BuildMenu();

            ImGui::EndMenuBar();
        }
    }

    void EditorLayer::RenderToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        auto& colors = ImGui::GetStyle().Colors;
        const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
        const auto& buttonActive = colors[ImGuiCol_ButtonActive];
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

        ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float size = ImGui::GetWindowHeight() - 2.0f;
        ImGui::SetCursorPosY(1.0f);
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 1.5f));

        auto& editorState = EditorState::Get();
        bool hasPlayButton = editorState.IsEditing() || editorState.IsPaused();
        bool hasPauseButton = !editorState.IsEditing();
        bool hasStopButton = !editorState.IsEditing();

        if (hasPlayButton)
        {
            if (ImGui::Button(editorState.IsPaused() ? "Resume" : "Play", ImVec2(size, size)))
            {
                OnPlayButtonPressed();
            }
        }

        if (hasPauseButton)
        {
            ImGui::SameLine();
            if (ImGui::Button("Pause", ImVec2(size, size)))
            {
                OnPauseButtonPressed();
            }
        }

        if (hasStopButton)
        {
            ImGui::SameLine();
            if (ImGui::Button("Stop", ImVec2(size, size)))
            {
                OnStopButtonPressed();
            }
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

    void EditorLayer::RenderPanels()
    {
        if (m_ShowHierarchy && m_HierarchyPanel)
            m_HierarchyPanel->OnImGuiRender();

        if (m_ShowInspector && m_InspectorPanel)
            m_InspectorPanel->OnImGuiRender();

        if (m_ShowLighting && m_LightingPanel)
            m_LightingPanel->OnImGuiRender();

        if (m_ShowProjectSettings && m_ProjectSettingsPanel)
            m_ProjectSettingsPanel->OnImGuiRender();

        if (m_ShowContentBrowser && m_ContentBrowserPanel)
            m_ContentBrowserPanel->OnImGuiRender();

        if (m_ShowConsole && m_ConsolePanel)
            m_ConsolePanel->OnImGuiRender();

        if (m_ShowStats && m_StatsPanel)
            m_StatsPanel->OnImGuiRender();

        if (m_ShowAudioGraph && m_AudioGraphPanel)
            m_AudioGraphPanel->OnImGuiRender();

        if (m_ShowShaderGraph && m_ShaderEditorPanel)
            m_ShaderEditorPanel->OnImGuiRender();

        if (m_CodeEditorPanel && m_CodeEditorPanel->GetIsOpen())
        {
            m_CodeEditorPanel->OnImGuiRender();
        }

        if (m_ShowNavigation)
        {
            ImGui::Begin("Navigation", &m_ShowNavigation);
            ImGui::TextWrapped("Click the button below to bake the NavMesh based on the current scene geometry. This will analyze NavMeshSurface components and walkability parameters.");
            
            ImGui::Separator();
            ImGui::Spacing();
            
            if (ImGui::Button("Bake NavMesh", ImVec2(-1, 40)))
            {
                if (m_EditorScene)
                {
                    NavMeshBuilder::Bake(m_EditorScene.get(), m_EditorScene->GetNavMesh());
                }
            }
            
            ImGui::Spacing();
            if (m_EditorScene)
            {
                ImGui::Text("Baked Triangles: %zu", m_EditorScene->GetNavMesh().GetTriangles().size());
            }

            ImGui::End();
        }

        if (m_ShowGame && m_GamePanel)
            m_GamePanel->OnImGuiRender();

        if (m_ShowViewport && m_ViewportPanel)
            m_ViewportPanel->OnImGuiRender();

        ImGui::End(); // DockSpace
    }

    void EditorLayer::OnPlayButtonPressed()
    {
        auto& editorState = EditorState::Get();
        
        if (editorState.IsEditing())
        {
            // Editor scene'i direkt memory'den klonla (YAML yok, disk I/O yok)
            m_RuntimeScene = std::make_shared<Scene>();
            m_RuntimeScene->CloneSceneFrom(m_EditorScene);
            
            // Active scene'i runtime'a çevir
            m_ActiveScene = m_RuntimeScene;
            
            // Panelleri güncelle
            if (m_HierarchyPanel)
                m_HierarchyPanel->SetContext(m_ActiveScene);
            if (m_InspectorPanel)
                m_InspectorPanel->SetContext(m_ActiveScene.get());
            if (m_ViewportPanel)
                m_ViewportPanel->SetContext(m_ActiveScene);
            if (m_GamePanel)
                m_GamePanel->SetContext(m_ActiveScene);
            if (m_LightingPanel)
                m_LightingPanel->SetContext(m_ActiveScene);
            if (m_ProjectSettingsPanel)
                m_ProjectSettingsPanel->SetContext(m_ActiveScene);
            if (m_AudioGraphPanel)
                m_AudioGraphPanel->SetContext(m_ActiveScene.get());
            if (m_ShaderEditorPanel)
                m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
            
            editorState.SetSceneState(SceneState::Play);
        }
        else if (editorState.IsPaused())
        {
            editorState.SetSceneState(SceneState::Play);
        }
    }

    void EditorLayer::OnPauseButtonPressed()
    {
        auto& editorState = EditorState::Get();
        
        if (editorState.IsPlaying())
        {
            editorState.SetSceneState(SceneState::Pause);
        }
    }

    void EditorLayer::OnStopButtonPressed()
    {
        auto& editorState = EditorState::Get();
        
        if (!editorState.IsEditing())
        {
            // Seçili entity'nin UUID'sini kaydet
            uint64_t selectedUUID = 0;
            if (m_InspectorPanel)
            {
                Entity selectedEntity = m_InspectorPanel->GetSelectedEntity();
                if (selectedEntity && selectedEntity.HasComponent<IDComponent>())
                {
                    selectedUUID = selectedEntity.GetComponent<IDComponent>().ID;
                }
            }
            
            // Runtime scene'i temizle
            if (m_RuntimeScene)
                m_RuntimeScene->StopAllAudio();
            m_RuntimeScene.reset();
            
            // Editor scene audio'yu da durdur
            if (m_EditorScene)
                m_EditorScene->StopAllAudio();
            
            // Editor scene'e geri dön
            m_ActiveScene = m_EditorScene;
            
            // Panelleri güncelle
            if (m_HierarchyPanel)
                m_HierarchyPanel->SetContext(m_ActiveScene);
            if (m_InspectorPanel)
                m_InspectorPanel->SetContext(m_ActiveScene.get());
            if (m_ViewportPanel)
                m_ViewportPanel->SetContext(m_ActiveScene);
            if (m_GamePanel)
                m_GamePanel->SetContext(m_ActiveScene);
            if (m_LightingPanel)
                m_LightingPanel->SetContext(m_ActiveScene);
            if (m_ProjectSettingsPanel)
                m_ProjectSettingsPanel->SetContext(m_ActiveScene);
            if (m_AudioGraphPanel)
                m_AudioGraphPanel->SetContext(m_ActiveScene.get());
            if (m_ShaderEditorPanel)
                m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
            
            // Seçili entity'yi UUID ile yeniden bul
            if (selectedUUID != 0 && m_InspectorPanel)
            {
                Entity newSelectedEntity = m_ActiveScene->FindEntityByUUID(selectedUUID);
                if (newSelectedEntity)
                {
                    m_InspectorPanel->SetSelectedEntity(newSelectedEntity);
                    if (m_ViewportPanel)
                        m_ViewportPanel->SetSelectedEntity(newSelectedEntity);
                    if (m_AudioGraphPanel)
                        m_AudioGraphPanel->SetSelectedEntity(newSelectedEntity);
                    if (m_ShaderEditorPanel)
                        m_ShaderEditorPanel->SetSelectedEntity(newSelectedEntity);
                }
            }
            
            editorState.SetSceneState(SceneState::Edit);
        }
    }

    void EditorLayer::FileMenu()
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                NewScene();
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            {
                OpenScene();
            }

            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                SaveScene();
            }

            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
            {
                SaveSceneAs();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("New Project"))
            {
                NewProject();
            }

            if (ImGui::MenuItem("Open Project..."))
            {
                OpenProject();
            }

            if (ImGui::MenuItem("Save Project"))
            {
                SaveProject();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit"))
            {
                Conqueror::Application::Get().Close();
            }

            ImGui::EndMenu();
        }
    }

    void EditorLayer::EditMenu()
    {
        if (ImGui::BeginMenu("Edit"))
        {
            auto& state = EditorState::Get();

            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, UndoRedoManager::Get().CanUndo()))
            {
                PerformUndo();
            }

            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, UndoRedoManager::Get().CanRedo()))
            {
                PerformRedo();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Cut", "Ctrl+X", false, state.HasSelection()))
            {
                EditCut();
            }

            if (ImGui::MenuItem("Copy", "Ctrl+C", false, state.HasSelection()))
            {
                EditCopy();
            }

            if (ImGui::MenuItem("Paste", "Ctrl+V", false, state.HasClipboardData()))
            {
                EditPaste();
            }

            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, state.HasSelection()))
            {
                EditDuplicate();
            }

            if (ImGui::MenuItem("Delete", "Delete", false, state.HasSelection()))
            {
                EditDelete();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Select All", "Ctrl+A"))
            {
                EditSelectAll();
            }

            ImGui::EndMenu();
        }
    }

    void EditorLayer::GameObjectMenu()
    {
        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                m_ActiveScene->CreateEntity("Empty Entity");
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("2D Object"))
            {
                // Alfabetik sıralı sprite listesi
                const char* spriteNames[] = {
                    "Capsule", "Circle", "Diamond", 
                    "HexagonFlat", "HexagonPoint", "IsometricDiamond",
                    "NineSliced", "Pentagon", "Square", 
                    "Star", "Triangle"
                };

                for (const char* spriteName : spriteNames)
                {
                    if (ImGui::MenuItem(spriteName))
                    {
                        auto entity = m_ActiveScene->CreateEntity(spriteName);
                        auto& sprite = entity.AddComponent<SpriteRendererComponent>();
                        
                        std::string texturePath = "Resources/test/" + std::string(spriteName) + ".png";
                        sprite.Texture = Texture2D::Create(texturePath);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("3D Object"))
            {
                if (ImGui::MenuItem("Import Model..."))
                {
                    auto entity = m_ActiveScene->CreateEntity("Model");
                    entity.AddComponent<ModelComponent>();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Cube"))
                {
                    auto entity = m_ActiveScene->CreateEntity("Cube");
                    auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                    meshRenderer.Type = MeshType::Cube;
                }

                if (ImGui::MenuItem("Sphere"))
                {
                    auto entity = m_ActiveScene->CreateEntity("Sphere");
                    auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                    meshRenderer.Type = MeshType::Sphere;
                }

                if (ImGui::MenuItem("Plane"))
                {
                    auto entity = m_ActiveScene->CreateEntity("Plane");
                    auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                    meshRenderer.Type = MeshType::Plane;
                }

                if (ImGui::MenuItem("Cylinder"))
                {
                    auto entity = m_ActiveScene->CreateEntity("Cylinder");
                    auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                    meshRenderer.Type = MeshType::Cylinder;
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Camera"))
            {
                auto entity = m_ActiveScene->CreateEntity("Camera");
                entity.AddComponent<CameraComponent>();
            }

            if (ImGui::MenuItem("Light"))
            {
                auto entity = m_ActiveScene->CreateEntity("Light");
                // TODO: Add light component
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("UI"))
            {
                if (ImGui::MenuItem("Text"))
                {
                    auto entity = m_ActiveScene->CreateEntity("Text");
                    entity.AddComponent<TextRendererComponent>();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }
    }

    void EditorLayer::WindowMenu()
    {
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
            ImGui::MenuItem("Lighting", nullptr, &m_ShowLighting);
            ImGui::MenuItem("Project Settings", nullptr, &m_ShowProjectSettings);
            ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContentBrowser);
            ImGui::MenuItem("Audio Graph", nullptr, &m_ShowAudioGraph);
            ImGui::MenuItem("Shader Graph", nullptr, &m_ShowShaderGraph);
            ImGui::MenuItem("Navigation", nullptr, &m_ShowNavigation);
            if (m_CodeEditorPanel)
                ImGui::MenuItem("Code Editor", nullptr, &m_CodeEditorPanel->GetIsOpen());
            ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
            ImGui::MenuItem("Game", nullptr, &m_ShowGame);
            ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);

            ImGui::EndMenu();
        }
    }

    void EditorLayer::HelpMenu()
    {
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Documentation"))
            {
#ifdef CQ_PLATFORM_LINUX
                OpenURL("https://conquerorsengine.com/documentations");
#elif defined(CQ_PLATFORM_WINDOWS)
                system("start https://conquerorsengine.com/documentations");
#else
                system("open https://conquerorsengine.com/documentations");
#endif
            }

            if (ImGui::MenuItem("About"))
            {
                m_ShowAbout = true;
            }

            ImGui::EndMenu();
        }

        if (m_ShowAbout)
        {
            ImGui::SetNextWindowPos(ImVec2(
                ImGui::GetIO().DisplaySize.x * 0.5f,
                ImGui::GetIO().DisplaySize.y * 0.5f),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);
            ImGui::OpenPopup("About Conqueror's Engine");
            m_ShowAbout = false;
        }

        if (ImGui::BeginPopupModal("About Conqueror's Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize("Conqueror's Engine").x) * 0.5f);
            ImGui::Text("Conqueror's Engine");
            ImGui::Separator();
            ImGui::TextWrapped("Hi, I'm Fazli. I started developing Conqueror's Engine when I was 14, and now at 15 I've decided to publish it under Vertex and Heuronic companies. I hope you like it. Thank you very much <3");
            ImGui::Spacing();

            float buttonWidth = 160.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);
            if (ImGui::Button("Contact Me", ImVec2(buttonWidth, 0)))
            {
#ifdef CQ_PLATFORM_LINUX
                OpenURL("https://www.linkedin.com/in/fazl%C4%B1-%C3%B6zlemi%C5%9F-3929673b0/");
#elif defined(CQ_PLATFORM_WINDOWS)
                system("start https://www.linkedin.com/in/fazl%C4%B1-%C3%B6zlemi%C5%9F-3929673b0/");
#else
                system("open https://www.linkedin.com/in/fazl%C4%B1-%C3%B6zlemi%C5%9F-3929673b0/");
#endif
            }

            ImGui::Separator();
            float closeWidth = 60.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - closeWidth) * 0.5f);
            if (ImGui::Button("Close", ImVec2(closeWidth, 0)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::BuildMenu()
    {
        if (ImGui::BeginMenu("Build"))
        {
            if (ImGui::MenuItem("Build Project"))
            {
                auto project = Project::GetActive();
                if (!project)
                {
                    CQ_CORE_ERROR("No active project to build!");
                    ImGui::EndMenu();
                    return;
                }

                std::string platform = project->GetConfig().TargetPlatform;
                CQ_CORE_INFO("Starting Build for {0}...", platform);
                SaveProject();

                std::filesystem::path engineRoot = std::filesystem::current_path().parent_path();
                std::string buildScript = "cmake --build \"" + engineRoot.string() + "\" --target ConquerorRuntime -j8";
                int result = system(buildScript.c_str());

                if (result == 0)
                {
                    std::filesystem::path buildDir = project->GetProjectDirectory() / "Builds" / platform;
                    std::filesystem::create_directories(buildDir);

                    std::filesystem::path exePath = std::filesystem::current_path() / "ConquerorRuntime";
                    if (std::filesystem::exists(exePath))
                    {
                        std::string outName = project->GetConfig().Name;
                        std::replace(outName.begin(), outName.end(), ' ', '_');

                        std::filesystem::path destPath = buildDir / outName;
                        std::filesystem::copy_file(exePath, destPath, std::filesystem::copy_options::overwrite_existing);

                        if (platform == "Linux")
                        {
                            std::string chmodCmd = "chmod +x \"" + destPath.string() + "\"";
                            system(chmodCmd.c_str());
                        }

                        std::filesystem::path projectPath = project->GetProjectDirectory();
                        std::filesystem::path buildAssetsPath = buildDir / "Assets";
                        std::filesystem::path buildPackagesPath = buildDir / "Packages";

                        if (std::filesystem::exists(buildAssetsPath)) std::filesystem::remove_all(buildAssetsPath);
                        if (std::filesystem::exists(buildPackagesPath)) std::filesystem::remove_all(buildPackagesPath);

                        if (std::filesystem::exists(projectPath / "Assets"))
                            std::filesystem::copy(projectPath / "Assets", buildAssetsPath, std::filesystem::copy_options::recursive);

                        if (std::filesystem::exists(projectPath / "Packages"))
                            std::filesystem::copy(projectPath / "Packages", buildPackagesPath, std::filesystem::copy_options::recursive);

                        for (const auto& entry : std::filesystem::directory_iterator(projectPath))
                        {
                            if (entry.path().extension() == ".cqproj" || entry.path().extension() == ".cqproject")
                            {
                                std::filesystem::copy_file(entry.path(), buildDir / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
                                break;
                            }
                        }

                        CQ_CORE_INFO("Build successful! Standalone game created at: {0}", buildDir.string());
                    }
                    else
                    {
                        CQ_CORE_ERROR("Build failed: Executable not found at {0}", exePath.string());
                    }
                }
                else
                {
                    CQ_CORE_ERROR("Compilation failed!");
                }
            }

            ImGui::EndMenu();
        }
    }

    void EditorLayer::NewScene()
    {
        m_ActiveScene = std::make_shared<Scene>();
        m_EditorScene = m_ActiveScene;
        m_CurrentScenePath.clear();
        
        // Panelleri güncelle
        if (m_HierarchyPanel)
            m_HierarchyPanel->SetContext(m_ActiveScene);
        if (m_InspectorPanel)
            m_InspectorPanel->SetContext(m_ActiveScene.get());
        if (m_ViewportPanel)
            m_ViewportPanel->SetContext(m_ActiveScene);
        if (m_GamePanel)
            m_GamePanel->SetContext(m_ActiveScene);
        if (m_LightingPanel)
            m_LightingPanel->SetContext(m_ActiveScene);
        if (m_ProjectSettingsPanel)
            m_ProjectSettingsPanel->SetContext(m_ActiveScene);
        if (m_AudioGraphPanel)
            m_AudioGraphPanel->SetContext(m_ActiveScene.get());
        if (m_ShaderEditorPanel)
            m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
        
        CQ_CORE_INFO("New scene created");
    }

    void EditorLayer::OpenScene()
    {
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Scene Files", "cqscene" } };
        nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
        
        if (result == NFD_OKAY)
        {
            // Seçili entity'nin UUID'sini kaydet
            uint64_t selectedUUID = 0;
            if (m_InspectorPanel)
            {
                Entity selectedEntity = m_InspectorPanel->GetSelectedEntity();
                if (selectedEntity && selectedEntity.HasComponent<IDComponent>())
                {
                    selectedUUID = selectedEntity.GetComponent<IDComponent>().ID;
                }
            }
            
            m_ActiveScene = std::make_shared<Scene>();
            m_EditorScene = m_ActiveScene;
            
            SceneSerializer serializer(m_ActiveScene);
            
            // Viewport kamerasını serializer'a ver (yükleme için)
            if (m_ViewportPanel)
            {
                serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
            }
            
            if (serializer.Deserialize(outPath))
            {
                m_CurrentScenePath = outPath;
                
                // Panelleri güncelle
                if (m_HierarchyPanel)
                    m_HierarchyPanel->SetContext(m_ActiveScene);
                if (m_InspectorPanel)
                    m_InspectorPanel->SetContext(m_ActiveScene.get());
                if (m_ViewportPanel)
                    m_ViewportPanel->SetContext(m_ActiveScene);
                if (m_GamePanel)
                    m_GamePanel->SetContext(m_ActiveScene);
                if (m_LightingPanel)
                    m_LightingPanel->SetContext(m_ActiveScene);
                if (m_ProjectSettingsPanel)
                    m_ProjectSettingsPanel->SetContext(m_ActiveScene);
                if (m_AudioGraphPanel)
                    m_AudioGraphPanel->SetContext(m_ActiveScene.get());
                if (m_ShaderEditorPanel)
                    m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
                
                // Seçili entity'yi UUID ile yeni scene'de bul
                if (selectedUUID != 0 && m_InspectorPanel)
                {
                    Entity newSelectedEntity = m_ActiveScene->FindEntityByUUID(selectedUUID);
                    if (newSelectedEntity)
                    {
                        m_InspectorPanel->SetSelectedEntity(newSelectedEntity);
                        if (m_ViewportPanel)
                            m_ViewportPanel->SetSelectedEntity(newSelectedEntity);
                        if (m_AudioGraphPanel)
                            m_AudioGraphPanel->SetSelectedEntity(newSelectedEntity);
                    }
                }
                
                CQ_CORE_INFO("Scene opened: {0}", outPath);
            }
            
            NFD_FreePathU8(outPath);
        }
    }

    void EditorLayer::SaveScene()
    {
        if (m_CurrentScenePath.empty())
        {
            SaveSceneAs();
        }
        else
        {
            SceneSerializer serializer(m_ActiveScene);
            
            // Viewport kamerasını serializer'a ver
            if (m_ViewportPanel)
            {
                serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
            }
            
            serializer.Serialize(m_CurrentScenePath);
            CQ_CORE_INFO("Scene saved: {0}", m_CurrentScenePath.string());
        }
    }

    void EditorLayer::SaveSceneAs()
    {
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Scene Files", "cqscene" } };
        nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 1, nullptr, "Scene.cqscene");
        
        if (result == NFD_OKAY)
        {
            m_CurrentScenePath = outPath;
            
            SceneSerializer serializer(m_ActiveScene);
            
            // Viewport kamerasını serializer'a ver
            if (m_ViewportPanel)
            {
                serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
            }
            
            serializer.Serialize(m_CurrentScenePath);
            
            CQ_CORE_INFO("Scene saved as: {0}", outPath);
            NFD_FreePathU8(outPath);
        }
    }

    EditorCamera* EditorLayer::GetEditorCamera()
    {
        if (m_ViewportPanel)
            return &m_ViewportPanel->GetEditorCamera();
        return nullptr;
    }

    void EditorLayer::NewProject()
    {
        m_ShowNewProjectPopup = true;
    }

    void EditorLayer::OpenProject()
    {
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Project Files", "cqproj,cqproject" } };
        nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
        
        if (result == NFD_OKAY)
        {
            auto project = std::make_shared<Project>();
            ProjectSerializer serializer(project);
            if (serializer.Deserialize(outPath))
            {
                Project::SetActive(project);
                
                // Load the StartScene automatically
                std::filesystem::path projectPath = project->GetProjectDirectory();
                std::filesystem::path startScenePath = projectPath / project->GetConfig().StartScene;
                
                if (std::filesystem::exists(startScenePath))
                {
                    m_ActiveScene = std::make_shared<Scene>();
                    SceneSerializer sceneSerializer(m_ActiveScene);
                    if (m_ViewportPanel)
                    {
                        sceneSerializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
                    }
                    if (sceneSerializer.Deserialize(startScenePath))
                    {
                        m_CurrentScenePath = startScenePath;
                        m_EditorScene = m_ActiveScene;
                        
                        // Update context panels
                        if (m_HierarchyPanel) m_HierarchyPanel->SetContext(m_ActiveScene);
                        if (m_InspectorPanel) m_InspectorPanel->SetContext(m_ActiveScene.get());
                        if (m_ViewportPanel) m_ViewportPanel->SetContext(m_ActiveScene);
                        if (m_GamePanel) m_GamePanel->SetContext(m_ActiveScene);
                        if (m_LightingPanel) m_LightingPanel->SetContext(m_ActiveScene);
                        if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->SetContext(m_ActiveScene);
                        if (m_AudioGraphPanel) m_AudioGraphPanel->SetContext(m_ActiveScene.get());
                        if (m_ContentBrowserPanel) m_ContentBrowserPanel->RefreshContext();
                    }
                }
                
                CQ_CORE_INFO("Project opened: {0}", outPath);
            }
            else
            {
                CQ_CORE_ERROR("Failed to open project: {0}", outPath);
            }
            NFD_FreePathU8(outPath);
        }
    }

    void EditorLayer::SaveProject()
    {
        // Önce açık olan sahneyi kaydet
        SaveScene();

        auto project = Project::GetActive();
        if (!project)
            return;

        // Proje zaten bir dizine sahipse doğrudan üzerine yaz
        std::filesystem::path projectPath = project->GetProjectDirectory();
        if (!projectPath.empty())
        {
            std::string projectName = project->GetConfig().Name;
            std::filesystem::path projectFilePath = projectPath / (projectName + ".cqproj");
            ProjectSerializer serializer(project);
            serializer.Serialize(projectFilePath);
            CQ_CORE_INFO("Project saved: {0}", projectFilePath.string());
        }
        else
        {
            nfdu8char_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = { { "Project Files", "cqproj" } };
            nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 1, nullptr, "MyProject.cqproj");
            
            if (result == NFD_OKAY)
            {
                ProjectSerializer serializer(project);
                serializer.Serialize(outPath);
                CQ_CORE_INFO("Project saved as: {0}", outPath);
                NFD_FreePathU8(outPath);
            }
        }
    }

    void EditorLayer::HandleKeyboardShortcuts()
    {
        auto& io = ImGui::GetIO();
        bool ctrl = io.KeyCtrl;
        bool shift = io.KeyShift;

        bool textActive = io.WantTextInput;

        if (ctrl && !textActive && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            if (UndoRedoManager::Get().CanUndo())
                PerformUndo();
        }
        else if (ctrl && !textActive && ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            if (UndoRedoManager::Get().CanRedo())
                PerformRedo();
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            EditCopy();
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X))
        {
            EditCut();
        }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
        {
            EditPaste();
        }
        else if (ctrl && !textActive && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            EditDuplicate();
        }
        else if (ctrl && !textActive && ImGui::IsKeyPressed(ImGuiKey_A))
        {
            EditSelectAll();
        }
        else if (!textActive && ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            auto& state = EditorState::Get();
            if (state.HasSelection())
            {
                if (m_HierarchyPanel)
                    m_HierarchyPanel->StartRename(state.GetSelectedEntity());
            }
        }
        else if (!textActive && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            auto& state = EditorState::Get();
            if (state.HasSelection())
                EditDelete();
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_N))
        {
            NewScene();
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            OpenScene();
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            SaveScene();
        }
        else if (ctrl && shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveUndoState()
    {
    }

    void EditorLayer::PerformUndo()
    {
        UndoRedoManager::Get().Undo();
        UpdateHierarchySelection();
    }

    void EditorLayer::PerformRedo()
    {
        UndoRedoManager::Get().Redo();
        UpdateHierarchySelection();
    }

    void EditorLayer::RefreshAllPanels()
    {
        if (m_HierarchyPanel) m_HierarchyPanel->SetContext(m_ActiveScene);
        if (m_InspectorPanel) m_InspectorPanel->SetContext(m_ActiveScene.get());
        if (m_ViewportPanel) m_ViewportPanel->SetContext(m_ActiveScene);
        if (m_GamePanel) m_GamePanel->SetContext(m_ActiveScene);
        if (m_LightingPanel) m_LightingPanel->SetContext(m_ActiveScene);
        if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->SetContext(m_ActiveScene);
        if (m_AudioGraphPanel) m_AudioGraphPanel->SetContext(m_ActiveScene.get());
        if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
    }

    void EditorLayer::UpdateHierarchySelection()
    {
        auto& state = EditorState::Get();
        if (m_HierarchyPanel)
        {
            if (state.HasSelection())
                m_HierarchyPanel->SetSelectedEntity(state.GetSelectedEntity());
            else
                m_HierarchyPanel->SetSelectedEntity({});
        }
    }

    void EditorLayer::EditCopy()
    {
        auto& state = EditorState::Get();
        if (!state.HasSelection())
            return;

        state.CopyEntities(state.GetSelectedEntities());
        CQ_CORE_INFO("Copied {0} entity/entities to clipboard", state.GetSelectionCount());
    }

    void EditorLayer::EditCut()
    {
        auto& state = EditorState::Get();
        if (!state.HasSelection())
            return;

        state.CutEntities(state.GetSelectedEntities());
        CQ_CORE_INFO("Cut {0} entity/entities", state.GetSelectionCount());
    }

    void EditorLayer::EditPaste()
    {
        auto& state = EditorState::Get();
        if (!state.HasClipboardData())
            return;

        std::vector<Entity> newEntities;
        auto& clipboard = state.GetClipboardEntities();
        bool wasCut = state.IsCutOperation();

        for (auto& srcEntity : clipboard)
        {
            if (!srcEntity)
                continue;

            auto cmd = std::make_shared<DuplicateEntityCommand>(m_ActiveScene, srcEntity);
            UndoRedoManager::Get().Execute(cmd);
            if (cmd->GetDuplicatedEntity())
                newEntities.push_back(cmd->GetDuplicatedEntity());
        }

        if (wasCut)
        {
            for (auto& srcEntity : clipboard)
            {
                if (srcEntity && srcEntity.GetScene() == m_ActiveScene.get())
                {
                    auto cmd = std::make_shared<DestroyEntityCommand>(m_ActiveScene, srcEntity);
                    UndoRedoManager::Get().Execute(cmd);
                }
            }
            state.FinalizeCut();
        }

        state.ClearClipboard();
        state.SelectMultipleEntities(newEntities);
        UpdateHierarchySelection();

        CQ_CORE_INFO("Pasted {0} entity/entities", newEntities.size());
    }

    void EditorLayer::EditDuplicate()
    {
        auto& state = EditorState::Get();
        if (!state.HasSelection())
            return;

        std::vector<Entity> newEntities;
        for (auto& entity : state.GetSelectedEntities())
        {
            if (!entity)
                continue;
            auto cmd = std::make_shared<DuplicateEntityCommand>(m_ActiveScene, entity);
            UndoRedoManager::Get().Execute(cmd);
            if (cmd->GetDuplicatedEntity())
                newEntities.push_back(cmd->GetDuplicatedEntity());
        }

        state.SelectMultipleEntities(newEntities);
        UpdateHierarchySelection();

        CQ_CORE_INFO("Duplicated {0} entity/entities", newEntities.size());
    }

    void EditorLayer::EditDelete()
    {
        auto& state = EditorState::Get();
        if (!state.HasSelection())
            return;

        size_t count = state.GetSelectionCount();
        for (auto& entity : state.GetSelectedEntities())
        {
            if (entity)
            {
                auto cmd = std::make_shared<DestroyEntityCommand>(m_ActiveScene, entity);
                UndoRedoManager::Get().Execute(cmd);
            }
        }

        state.ClearSelection();
        UpdateHierarchySelection();
        if (m_InspectorPanel)
            m_InspectorPanel->SetSelectedEntity({});

        CQ_CORE_INFO("Deleted {0} entity/entities", count);
    }

    void EditorLayer::EditSelectAll()
    {
        auto& state = EditorState::Get();
        std::vector<Entity> allEntities;

        m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto entityID, [[maybe_unused]] auto& tc)
        {
            Entity entity{ entityID, m_ActiveScene.get() };
            allEntities.push_back(entity);
        });

        state.SelectMultipleEntities(allEntities);

        if (m_HierarchyPanel && !allEntities.empty())
            m_HierarchyPanel->SetSelectedEntity(allEntities[0]);

        CQ_CORE_INFO("Selected all {0} entities", allEntities.size());
    }
}
