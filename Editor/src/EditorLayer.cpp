// glad diğer başlıklardan (GLFW) önce: gpu_probe GL geri okuma çağrıları
#define GLFW_INCLUDE_NONE
#include "Panels/OcclusionPanel.h"
#include "Scene/OcclusionCulling.h"
#include "Scene/XRInteraction.h"
#include "Scene/WorldActors.h"
#include "Scene/SpriteAnimation2D.h"
#include "Scene/Signals.h"
#include "Core/Input/InputActionAssets.h"
#include "Panels/MaterialEditorUI.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scene/MeshRendererMaterial.h"
#include "Assets/SceneTemplate.h"
#include <yaml-cpp/yaml.h>
#include "Assets/VolumeProfileInspector.h"
#include "Assets/ShaderVariantInspector.h"
#include "Assets/AudioAssetsInspector.h"
#include "Assets/SignalReceiverInspector.h"
#include "Assets/AnimationClipInspector.h"
#include "Assets/AssetUI.h"
#include "Scene/AnimationAssets.h"
#include "Scene/Ragdoll.h"
#include "Scene/AudioOcclusion.h"
#include "Core/Assets/SimpleAsset.h"
#include "Assets/SimpleAssetInspector.h"
#include "Scripting/TestRunner.h"
#include "Renderer/SpriteAtlas.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "EditorLayer.h"
#include "Core/Input/InputGlyphs.h"
#include "AISystem/Navigation/NavAgents.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/Audio/AudioEngine.h"
#include "ProjectThumbnail.h"
#include "Renderer/IBL/EnvironmentMap.h"
#include "Renderer/PathTracer.h"
#include "Core/Events/ApplicationEvent.h"
#include "Scene/BlendShapes.h"
#include "Scene/SceneManager.h"
#include "Build/BuildPipeline.h"
#include "Networking/Multiplayer/NetworkSession.h"
#include "Networking/Multiplayer/NetworkComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/NativeDocs.h"
#include "Grid/GridSystem.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "AssetActions.h"
#include "Assets/CubemapInspector.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scene/LevelStreaming.h"
#include "Panels/ImportActivityPanel.h"
#include "Renderer/RHI/Texture.h"
#include "Panels/AssetInspector.h"
#include "Addons/AddonHost.h"
#include "Addons/AddonManager.h"
#include "UndoRedo/SceneSnapshotCommand.h"
#include "UI/IconNames.h"
#include "Core/Input/GameInput.h"
#include "Core/Base/Version.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Time/TimeManager.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/ScriptRuntime.h"
#include <fstream>
#include <sstream>
#include "Panels/GameViewSettings.h"
#include "Core/Audio/AudioMixer.h"
#include "UI/UISystem.h"
#include "Core/Application.h"
#include "Core/Input/Input.h"
#include "Renderer/RHI/Cubemap.h"
#include "Core/KeyCodes.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/AI/AIManager.h"

#include <imnodes.h>
#include "Core/Logging/Log.h"
#include "Core/Debug/DebugDraw.h"
#include "Scene/Components.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Automation/Automation.h"
#include "Core/Recording/Recorder.h"
#include "UI/EditorUI.h"
#include <IconsFontAwesome6.h>
#include <ImGuizmo.h>
#include <cmath>

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
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

        // Konsol değişkenleri (r.*, t.*, stat ...): etkilenen sahne = etkin sahne; "stat" istatistik panelini açar
        ConsoleSystem::SetSceneProvider([this]() { return m_ActiveScene.get(); });
        // Görsel betiklerin Blueprint hata ayıklayıcısı (düğüm izleri yalnızca editörde üretilir)
        Visual::Debugger::SetEnabled(true);
        ConsoleSystem::RegisterBuiltins();
        ConsoleSystem::RegisterCommand("stat", "Statistics overlay: stat fps | unit | none", [this](const std::vector<std::string>& args) {
            const std::string what = args.empty() ? "fps" : args[0];
            m_ShowStats = what != "none";
            const float dt = std::max(1e-6f, TimeManager::GetUnscaledDeltaTime());
            return m_ShowStats ? "Stats on (FPS " + std::to_string((int)std::lround(1.0f / dt)) + ")" : std::string("Stats off");
        });
        
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
            m_LogPanel = std::make_unique<LogPanel>();
            m_ConsolePanel = std::make_unique<ConsolePanel>();
            CQ_CORE_INFO("EditorLayer: Log and Console panels created");

            // Log callback'ini bağla (asenkron log thread'inden çağrılır; LogPanel thread güvenlidir)
            Log::SetConsoleCallback([this](const std::string& message, int level) {
                if (m_LogPanel)
                    m_LogPanel->AddMessageFromLog(message, level);
            });
            CQ_CORE_INFO("EditorLayer: Log callback registered");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create ConsolePanel: {0}", e.what());
        }
        
        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ContentBrowserPanel...");
            m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();
            m_ImportActivityPanel = std::make_unique<ImportActivityPanel>();
            // Unity Project menüsünün sahne / pencere işlemleri
            ContentBrowserPanel::s_FindReferencesInScene = [this](const std::string& path) {
                if (!m_ActiveScene)
                    return 0;
                const std::string rel = SceneSerializer::ToAssetPath(path);
                std::vector<Entity> found;
                m_ActiveScene->m_Registry.view<IDComponent>().each([&](auto id, auto&) {
                    Entity e{ id, m_ActiveScene.get() };
                    const std::string text = SceneSerializer::SerializeEntityToString(e);
                    if (!rel.empty() && text.find(rel) != std::string::npos)
                        found.push_back(e);
                });
                EditorState::Get().SelectMultipleEntities(found);
                if (!found.empty())
                    SelectEntityEverywhere(found.front());
                return (int)found.size();
            };
            ContentBrowserPanel::s_OpenSceneAdditive = [this](const std::string& path) {
                if (!m_ActiveScene || !EditorState::Get().IsEditing())
                    return false;
                // Kaydedilmemiş değişiklik algılaması sahne içeriğini karşılaştırır: yeni kök kendiliğinden "değişti" sayılır
                return LevelStreaming::LoadAdditive(*m_ActiveScene, SceneSerializer::ToAssetPath(path)) != 0xFFFFFFFFu;
            };
            ContentBrowserPanel::s_ReloadModelInScene = [this](const std::string& path) {
                ModelLoader::Invalidate(path);
                ModelLoader::Invalidate(SceneSerializer::ToAssetPath(path));
                std::error_code ec;
                const std::string canonical = std::filesystem::weakly_canonical(path, ec).string();
                std::shared_ptr<Model> reloaded;
                if (m_ActiveScene)
                    m_ActiveScene->m_Registry.view<MeshRendererComponent>().each([&](auto, MeshRendererComponent& mc) {
                        if (mc.ModelPath.empty())
                            return;
                        const std::string resolved = SceneSerializer::ResolveAssetPath(mc.ModelPath);
                        if (std::filesystem::weakly_canonical(resolved, ec).string() != canonical)
                            return;
                        if (!reloaded)
                        {
                            ModelLoader::Invalidate(mc.ModelPath);
                            ModelLoader::Invalidate(resolved);
                            reloaded = ModelLoader::Load(resolved);
                        }
                        if (reloaded)
                            mc.ModelData = reloaded;
                    });
                if (!reloaded)
                    ModelLoader::Load(path);
            };
            ContentBrowserPanel::s_OpenAddonManager = [this](const std::string& id) {
                m_ShowAddonManager = true;
                if (m_AddonManagerPanel)
                    m_AddonManagerPanel->Select(id, false);
            };
            ContentBrowserPanel::s_OpenScriptProject = [this]() {
                const std::string dir = Project::GetActive() ? Project::GetActiveProjectDirectory().string() : std::string();
                if (!dir.empty() && OpenInExternalEditor(dir))
                    return;
                if (m_CodeEditorPanel)
                    m_CodeEditorPanel->GetIsOpen() = true;
            };
            CubemapInspector::s_UseAsSkybox = [this](const std::string& path) {
                if (!m_ActiveScene)
                    return;
                if (auto cube = Cubemap::CreateFromEquirectangular(path, 512); cube && cube->GetRendererID())
                {
                    m_ActiveScene->SetSkybox(cube);
                    m_ActiveScene->InvalidateReflectionProbes();
                }
            };
            ContentBrowserPanel::s_OpenImportActivity = [this](const std::string& filter) {
                m_ShowImportActivity = true;
                if (m_ImportActivityPanel)
                    m_ImportActivityPanel->SetFilter(filter);
            };
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
            m_ViewportPanel->SetSelectionCallback([this](Entity entity, bool additive) {
                if (additive && entity)
                {
                    EditorState::Get().ToggleEntitySelection(entity);
                    Entity primary = EditorState::Get().GetSelectedEntity();
                    auto keep = EditorState::Get().GetSelectedEntities();
                    SelectEntityEverywhere(primary);
                    if (keep.size() > 1)
                        EditorState::Get().SelectMultipleEntities(keep);
                }
                else
                {
                    SelectEntityEverywhere(entity);
                }
            });
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
            m_OcclusionPanel = std::make_unique<OcclusionPanel>();
            m_AudioMixerPanel = std::make_unique<AudioMixerPanel>();
            m_ProfilerPanel = std::make_unique<ProfilerPanel>();
            m_MultiplayerPanel = std::make_unique<MultiplayerPanel>();
            m_MultiplayerPanel->SetRequestPlay([this]() { if (EditorState::Get().IsEditing()) OnPlayButtonPressed(); });
            m_TimelinePanel = std::make_unique<TimelinePanel>();
            m_AnimationWindow = std::make_unique<AnimationWindow>();
            AnimationClipInspector::s_OpenWindow = [this](const std::string& path) {
                if (!m_AnimationWindow)
                    return;
                m_AnimationWindow->OpenClip(path);
                m_ShowAnimation = true;
                m_PendingFocusWindow = "###AnimationWindow";
            };
            AnimationClipInspector::s_ClipChanged = [this]() {
                if (m_AnimationWindow)
                    m_AnimationWindow->InvalidateClip();
            };
            m_BuildSettingsPanel = std::make_unique<BuildSettingsPanel>();
            m_BuildSettingsPanel->SetCurrentSceneProvider([this]() { return m_CurrentScenePath; });
            m_BuildSettingsPanel->SetSaveProject([this]() { if (EditorState::Get().IsEditing()) SaveProject(); });
            m_SpriteEditorPanel = std::make_unique<SpriteEditorPanel>();
            m_InputActionsEditor = std::make_unique<InputActionsEditorPanel>();
            // Input Actions varlığı: Inspector'daki "Edit Asset" ve Content Browser çift tıkı bu pencereyi açar
            InspectorPanel::s_OpenInputActions = [this](const std::string& path) {
                if (m_InputActionsEditor && m_InputActionsEditor->Open(path))
                    m_PendingFocusWindow = "###InputActionsEditor";
            };
            m_TerrainPanel = std::make_unique<TerrainPanel>();
            m_TerrainPanel->SetContext(m_ActiveScene.get());
            m_AddonManagerPanel = std::make_unique<AddonManagerPanel>();
            m_TilePalettePanel = std::make_unique<TilePalettePanel>();
            m_TilePalettePanel->SetContext(m_ActiveScene.get());
            TilePalettePanel::s_OpenPanel = [this]() { m_ShowTilePalette = true; m_PendingFocusWindow = "###TilePalette"; };
            AudioAssetsInspector::SetOpenMixerWindow([this]() { m_ShowAudioMixer = true; });
            if (auto* receiverInfo = ComponentRegistry::Find("SignalReceiverComponent"))
                receiverInfo->ExtraInspector = &SignalReceiverInspector::Draw;
            if (auto* volumeInfo = ComponentRegistry::Find("PostProcessVolumeComponent"))
                volumeInfo->ExtraInspector = &VolumeProfileInspector::DrawVolumeExtra;
            if (auto* tilemapInfo = ComponentRegistry::Find("TilemapComponent"))
                tilemapInfo->ExtraInspector = &TilePalettePanel::DrawTilemapInspector;
            // Blend Shapes: modelin her blend shape'i için kaydırıcı (Unity Skinned Mesh Renderer > BlendShapes)
            if (auto* blendInfo = ComponentRegistry::Find("BlendShapesComponent"))
                blendInfo->ExtraInspector = [](Entity e) {
                    Scene* scene = e.GetScene();
                    const std::vector<std::string>* names = scene ? BlendShapes::Names(*scene, (uint32_t)e) : nullptr;
                    if (!names)
                    {
                        ImGui::TextDisabled("The model has no blend shapes (morph targets).");
                        return;
                    }
                    auto& bs = e.GetComponent<BlendShapesComponent>();
                    static char s_Filter[64] = {};
                    if (names->size() > 12)
                    {
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextWithHint("##bsfilter", ICON_FA_MAGNIFYING_GLASS " Filter blend shapes", s_Filter, sizeof(s_Filter));
                    }
                    std::string filter = s_Filter;
                    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
                    for (const auto& name : *names)
                    {
                        if (!filter.empty())
                        {
                            std::string lower = name;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                            if (lower.find(filter) == std::string::npos)
                                continue;
                        }
                        float value = 0.0f;
                        BlendShapeWeight* slot = nullptr;
                        for (auto& w : bs.Weights)
                            if (w.Name == name) { slot = &w; value = w.Weight; }
                        ImGui::PushID(name.c_str());
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                        if (ImGui::SliderFloat("##w", &value, 0.0f, 1.0f, "%.2f"))
                        {
                            if (slot) slot->Weight = value;
                            else bs.Weights.push_back({ name, value });
                        }
                        ImGui::SameLine();
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::PopID();
                    }
                };
            // Animation bileşeni: Unity "Animations" listesi (AnimationPlay(nesne, "Ad") ile seçilen klipler) + pencere
            if (auto* animInfo = ComponentRegistry::Find("AnimationPlayerComponent"))
                animInfo->ExtraInspector = [this](Entity e) {
                    auto& a = e.GetComponent<AnimationPlayerComponent>();
                    UI::SubHeader("Animations");
                    for (size_t i = 0; i < a.Clips.size(); ++i)
                    {
                        ImGui::PushID((int)i);
                        if (ImGui::SmallButton(ICON_FA_XMARK "##removeclip"))
                        {
                            a.Clips.erase(a.Clips.begin() + (std::ptrdiff_t)i);
                            ImGui::PopID();
                            break;
                        }
                        ImGui::SameLine();
                        std::string clip = a.Clips[i];
                        if (AssetUI::AssetSlot("##animclip", "Animation Clip", { PropertyClips::Extension }, clip))
                            a.Clips[i] = clip;
                        ImGui::PopID();
                    }
                    if (ImGui::Button(ICON_FA_PLUS " Add Clip##AnimComponentAddClip"))
                        a.Clips.emplace_back();
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_CLAPPERBOARD " Open Animation Window##AnimComponentOpen"))
                    {
                        m_ShowAnimation = true;
                        m_PendingFocusWindow = "###AnimationWindow";
                    }
                };
            // Sprite Resolver (Unity 2D Animation): kütüphanedeki kategori / etiket açılır listeleri
            if (auto* resolverInfo = ComponentRegistry::Find("SpriteResolverComponent"))
                resolverInfo->ExtraInspector = [](Entity e) {
                    if (!e.GetScene())
                        return;
                    auto& r = e.GetComponent<SpriteResolverComponent>();
                    const std::string lib = SpriteAnimation2D::FindLibraryAsset(*e.GetScene(), (entt::entity)e);
                    if (lib.empty())
                    {
                        UI::InfoBox("No Sprite Library on this object or its parents.", UI::InfoType::Warning);
                        return;
                    }
                    const auto categories = SpriteAnimation2D::Categories(lib);
                    UI::BeginProperty("Pick Category", "Categories in the Sprite Library Asset");
                    if (ImGui::BeginCombo("##resolverCategory", r.Category.c_str()))
                    {
                        for (const auto& c : categories)
                            if (ImGui::Selectable(c.c_str(), c == r.Category))
                            {
                                r.Category = c;
                                r.LabelIndex = -1;
                                const auto labels = SpriteAnimation2D::Labels(lib, c);
                                if (!labels.empty())
                                    r.Label = labels.front();
                            }
                        ImGui::EndCombo();
                    }
                    UI::EndProperty();
                    const auto labels = SpriteAnimation2D::Labels(lib, r.Category);
                    UI::BeginProperty("Pick Label", "Labels in this category");
                    if (ImGui::BeginCombo("##resolverLabel", r.Label.c_str()))
                    {
                        for (const auto& l : labels)
                            if (ImGui::Selectable(l.c_str(), l == r.Label))
                            {
                                r.Label = l;
                                r.LabelIndex = -1;
                            }
                        ImGui::EndCombo();
                    }
                    UI::EndProperty();
                };
            // Mesh Paint: Vertex Paint bileşeni denetçisinde fırça ayarları, viewport'ta boyama
            MeshPaintTool::Get().SetSceneProvider([this]() { return m_ActiveScene; });
            if (auto* paintInfo = ComponentRegistry::Find("VertexPaintComponent"))
                paintInfo->ExtraInspector = [](Entity e) { MeshPaintTool::Get().DrawInspector(e); };
            // Ek açılmış sahne (Open Scene Additive / Level Streaming): ana sahneye kaydedilmez; Save Level kendi dosyasına yazar
            if (auto* levelInfo = ComponentRegistry::Find("StreamedLevelComponent"))
                levelInfo->ExtraInspector = [this](Entity e) {
                    const auto& level = e.GetComponent<StreamedLevelComponent>();
                    UI::InfoBox(("Objects under this root belong to " + level.Source +
                                 ". Saving this scene does not store them; Save Level writes them back to their own scene file.").c_str(),
                                UI::InfoType::Info);
                    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    ImGui::BeginDisabled(!EditorState::Get().IsEditing());
                    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save Level", ImVec2(w, 0)))
                        SaveStreamedLevel(e);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_XMARK " Unload Level", ImVec2(w, 0)))
                    {
                        EditorState::Get().ClearSelection();
                        m_ActiveScene->DestroyEntity(e);
                    }
                    ImGui::EndDisabled();
                };
            if (auto* prefabInfo = ComponentRegistry::Find("PrefabInstanceComponent"))
                prefabInfo->ExtraInspector = [this](Entity e) {
                    const std::string& prefab = e.GetComponent<PrefabInstanceComponent>().Prefab;
                    std::error_code ec;
                    const auto resolved = PrefabSerializer::Resolve(prefab);
                    const bool exists = std::filesystem::exists(resolved, ec);
                    if (!exists)
                        UI::InfoBox("Prefab asset is missing. Apply recreates it.", UI::InfoType::Warning);
                    const float w = (ImGui::GetContentRegionAvail().x - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
                    ImGui::BeginDisabled(!exists);
                    if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Select", ImVec2(w, 0)) && m_ContentBrowserPanel)
                    {
                        m_ContentBrowserPanel->Reveal(resolved);
                        m_ShowContentBrowser = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    // Unity Overrides gibi: değişiklik yoksa Apply / Revert kapalı (varlık kayıpsa Apply yeniden oluşturur)
                    const bool overrides = PrefabSerializer::HasOverrides(e);
                    ImGui::BeginDisabled(!overrides);
                    if (UI::AccentButton(ICON_FA_UPLOAD " Apply", ImVec2(w, 0)))
                        PrefabSerializer::ApplyInstance(e);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip(overrides ? "Write this instance (and its children) back to the prefab asset" : "This instance has no changes to apply");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!exists || !overrides);
                    if (ImGui::Button(ICON_FA_ROTATE_LEFT " Revert", ImVec2(w, 0)))
                        m_PendingPrefabRevert = e;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_BOX_OPEN " Unpack", ImVec2(w, 0)))
                        e.RemoveComponent<PrefabInstanceComponent>();
                };
            if (m_ViewportPanel)
                m_ViewportPanel->SetToolHook([this](const ViewportToolContext& ctx) {
                    if (MeshPaintTool::Get().OnViewportTool(ctx))
                        return true;
                    if (m_ShowTerrain && m_TerrainPanel && m_TerrainPanel->OnViewportTool(ctx))
                        return true;
                    return m_ShowTilePalette && m_TilePalettePanel && m_TilePalettePanel->OnViewportTool(ctx);
                });
            InspectorPanel::s_SharedScene = [this]() { return m_ActiveScene; };
            InspectorPanel::s_OpenCodeEditor = [this](const std::string& path) {
                if (std::filesystem::path(path).extension() != ".cqgraph" && OpenInExternalEditor(path))
                    return;
                if (std::filesystem::path(path).extension() == ".cqgraph" && m_VisualScriptPanel)
                {
                    m_VisualScriptPanel->Open(path);
                    m_VisualScriptPanel->GetIsOpen() = true;
                }
                else if (m_CodeEditorPanel)
                {
                    m_CodeEditorPanel->OpenFile(path);
                    m_CodeEditorPanel->GetIsOpen() = true;
                }
            };
            InspectorPanel::s_OpenSpriteEditor = [this](const std::string& path) {
                if (m_SpriteEditorPanel && m_SpriteEditorPanel->Open(path))
                    m_ShowSpriteEditor = true;
                else if (m_SpriteEditorPanel)
                    m_ShowSpriteEditor = true; // kaydedilmemiş değişiklik onayı panelde
            };
            m_LightingPanel->SetContext(m_ActiveScene);
            m_LightingPanel->SetSelectionCallback([this](Entity e) { SelectEntityEverywhere(e); });
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
            AI::AIManager::Get().Init();
            m_AIPanel = std::make_unique<AIPanel>();
            m_AIPanel->SetContext(m_ActiveScene);
            m_StatsPanel = std::make_unique<StatsPanel>();
            if (m_ViewportPanel)
                m_ViewportPanel->SetStatsPanel(m_StatsPanel.get(), &m_ShowStats);
            if (m_GamePanel)
                m_GamePanel->SetStatsPanel(m_StatsPanel.get());
            CQ_CORE_INFO("EditorLayer: StatsPanel created");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create StatsPanel: {0}", e.what());
        }

        try
        {
            m_AnimatorPanel = std::make_unique<AnimatorPanel>();
            m_AnimatorPanel->SetContext(m_ActiveScene.get());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("EditorLayer: Failed to create AnimatorPanel: {0}", e.what());
        }

        try
        {
            CQ_CORE_INFO("EditorLayer: Creating ShaderEditorPanel...");
            m_ShaderEditorPanel = std::make_unique<ShaderEditorPanel>();
            m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
            // Sahne şablonu Snapshot: Game görünümü (kamera varsa), yoksa Scene görünümü
            SceneTemplates::SetSnapshotProvider([this](const std::filesystem::path& out) {
                std::shared_ptr<Framebuffer> fb;
                if (m_GamePanel && m_GamePanel->HasCamera() && m_GamePanel->GetViewportSize().x > 1.0f && m_GamePanel->GetViewportSize().y > 1.0f)
                    fb = m_GamePanel->GetFramebuffer();
                if (!fb && m_ViewportPanel && m_ViewportPanel->GetViewportSize().x > 1.0f)
                    fb = m_ViewportPanel->GetFramebuffer();
                if (!fb)
                    return false;
                const auto& spec = fb->GetSpecification();
                return WriteProjectThumbnail(fb->GetColorAttachmentRendererID(0), spec.Width, spec.Height, out, 640, 360);
            });
            // Shader Variant Collection "Add Shaders Used In Scene": sahnedeki malzeme dosyaları ve kod shader'ları
            ShaderVariantInspector::SetSceneProvider([this]() {
                std::vector<std::string> out;
                if (!m_ActiveScene)
                    return out;
                auto add = [&](const std::string& p) {
                    if (p.empty()) return;
                    std::string rel = AssetUI::ToProjectRelative(p);
                    if (std::find(out.begin(), out.end(), rel) == out.end()) out.push_back(rel);
                };
                m_ActiveScene->m_Registry.view<MeshRendererComponent>().each([&](auto, MeshRendererComponent& mr) {
                    add(mr.MaterialPath);
                    if (mr.MaterialInstance) add(mr.MaterialInstance->ShaderPath);
                });
                return out;
            });
            // Nesne seçici "Scene" sekmesi: açık sahnenin kullandığı dosyalar
            ObjectPicker::SetSceneProvider([this]() {
                std::vector<std::string> out;
                if (!m_ActiveScene)
                    return out;
                auto& reg = m_ActiveScene->m_Registry;
                for (auto e : reg.view<MeshRendererComponent>())
                {
                    auto& mr = reg.get<MeshRendererComponent>(e);
                    if (!mr.MaterialPath.empty()) out.push_back(mr.MaterialPath);
                    if (mr.MaterialInstance)
                        for (const auto* t : { &mr.MaterialInstance->AlbedoMap, &mr.MaterialInstance->NormalMap, &mr.MaterialInstance->EmissionMap })
                            if (*t && !(*t)->GetPath().empty()) out.push_back((*t)->GetPath());
                }
                for (auto e : reg.view<SpriteRendererComponent>())
                    if (auto& t = reg.get<SpriteRendererComponent>(e).Texture; t && !t->GetPath().empty())
                        out.push_back(t->GetPath());
                for (auto e : reg.view<BoxColliderComponent>())
                    if (auto& p = reg.get<BoxColliderComponent>(e).PhysicsMaterial; !p.empty()) out.push_back(p);
                for (auto e : reg.view<SphereColliderComponent>())
                    if (auto& p = reg.get<SphereColliderComponent>(e).PhysicsMaterial; !p.empty()) out.push_back(p);
                for (auto e : reg.view<CapsuleColliderComponent>())
                    if (auto& p = reg.get<CapsuleColliderComponent>(e).PhysicsMaterial; !p.empty()) out.push_back(p);
                for (auto& p : out)
                    p = std::filesystem::path(p).lexically_normal().string();
                return out;
            });
            MaterialEditorUI::SetOpenGraphCallback([this](const std::string& materialPath) {
                m_ShowShaderGraph = true;
                if (m_ShaderEditorPanel)
                    m_ShaderEditorPanel->SetMaterialAsset(materialPath);
                ImGui::SetWindowFocus("Shader Graph Editor");
            });
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
            m_CodeEditorPanel->OnFileSaved = [](const std::filesystem::path& path) {
                if (EditorState::Get().IsEditing())
                    return;
                if (auto* rt = Scripting::ScriptRuntimes::ForPath(path.string()))
                    rt->HotReload(path.string(), nullptr);
            };
            CQ_CORE_INFO("EditorLayer: CodeEditorPanel created");
            m_TestRunnerPanel = std::make_unique<TestRunnerPanel>();
            m_TestRunnerPanel->OnEnterPlay = [this]() { if (EditorState::Get().IsEditing()) OnPlayButtonPressed(); };
            m_TestRunnerPanel->OnOpenScript = [](const std::string& path) { if (InspectorPanel::s_OpenCodeEditor) InspectorPanel::s_OpenCodeEditor(path); };
            SimpleAssetInspector::RegisterExtra("PhysicsAsset", [](const std::string& path, SimpleAssets::Data& data) {
                if (ImGui::Button(ICON_FA_PERSON_FALLING "  Generate Bodies From Preview Model"))
                {
                    data.Tables["Bodies"].clear();
                    for (const auto& bone : RagdollBoneNames(data.Str("Preview Model")))
                        data.Tables["Bodies"].push_back({ bone, "0", "0", "0", "0" });
                    if (const auto* def = SimpleAssets::ForKind("PhysicsAsset"))
                        SimpleAssets::Save(path, *def, data);
                }
            });
            SimpleAssetInspector::RegisterExtra("PoseAsset", [](const std::string& path, SimpleAssets::Data& data) {
                if (ImGui::Button(ICON_FA_CAMERA "  Capture Pose") && !data.Str("Pose Name").empty())
                    AnimationAssets::CapturePose(path, data.Str("Model"), data.Str("Clip"), data.Float("Time"), data.Str("Pose Name"));
                if (const auto clips = AnimationAssets::ClipNames(data.Str("Model")); !clips.empty())
                {
                    std::string list;
                    for (const auto& c : clips)
                        list += (list.empty() ? "" : ", ") + c;
                    ImGui::TextDisabled("Clips: %s", list.c_str());
                }
            });
            SimpleAssetInspector::RegisterExtra("Skeleton", [](const std::string&, SimpleAssets::Data& data) {
                const auto bones = AnimationAssets::BoneNames(data.Str("Model"));
                ImGui::Text("%d bones", (int)bones.size());
                for (const auto& b : bones)
                    ImGui::BulletText("%s", b.c_str());
            });
            SimpleAssetInspector::RegisterExtra("SavedSearch", [this](const std::string&, SimpleAssets::Data& data) {
                if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS "  Run Search") && m_ContentBrowserPanel)
                    m_ContentBrowserPanel->SetSearch(data.Str("Query"));
            });
            m_NodeAssetPanel = std::make_unique<NodeAssetPanel>();
            NodeAssetPanel::s_Open = [this](const std::string& path) {
                m_NodeAssetPanel->Open(path);
                m_NodeAssetPanel->GetIsOpen() = true;
            };
            m_VisualScriptPanel = std::make_unique<VisualScriptPanel>();
            m_VisualScriptPanel->OnFileSaved = m_CodeEditorPanel->OnFileSaved;

            if (m_ContentBrowserPanel)
            {
                m_ContentBrowserPanel->SetSpriteEditorCallback([this](const std::filesystem::path& path) {
                    if (InspectorPanel::s_OpenSpriteEditor)
                        InspectorPanel::s_OpenSpriteEditor(path.string());
                });
                m_ContentBrowserPanel->SetFileOpenCallback([this](const std::filesystem::path& path) {
                    std::string ext = path.extension().string();
                    if (ext == ".cqscene")
                    {
                        RequestOpenScene(path);
                    }
                    else if (ext == ".cqprefab" && m_ActiveScene)
                    {
                        // Çift tık: kameranın odak noktasına örnekle
                        glm::vec3 at = m_ViewportPanel ? m_ViewportPanel->GetEditorCamera().GetFocalPoint() : glm::vec3(0.0f);
                        if (Entity e = AssetActions::SpawnFromAsset(m_ActiveScene.get(), path.string(), {}, &at))
                            SelectEntityEverywhere(e);
                    }
                    else if (ext == ".cqcont")
                    {
                        if (m_AnimatorPanel)
                        {
                            m_AnimatorPanel->OpenController(path.string());
                            m_AnimatorPanel->SetOpen(true);
                        }
                    }
                    else if (ext == ".cqgraph" && m_VisualScriptPanel)
                    {
                        m_VisualScriptPanel->Open(path);
                        m_VisualScriptPanel->GetIsOpen() = true;
                    }
                    else if (ext == ".cqsearch" && m_ContentBrowserPanel)
                    {
                        SimpleAssets::Data data;
                        if (SimpleAssets::Load(path.string(), data))
                            m_ContentBrowserPanel->SetSearch(data.Str("Query"));
                    }
                    else if (NodeAssetPanel::IsNodeAsset(path) && m_NodeAssetPanel)
                    {
                        m_NodeAssetPanel->Open(path);
                        m_NodeAssetPanel->GetIsOpen() = true;
                    }
                    else if (ext == InputActionAssets::Extension)
                    {
                        if (InspectorPanel::s_OpenInputActions)
                            InspectorPanel::s_OpenInputActions(path.string());
                    }
                    else if (ext == PropertyClips::Extension)
                    {
                        // Unity: Animation Clip çift tıkı Animation penceresini açar
                        if (AnimationClipInspector::s_OpenWindow)
                            AnimationClipInspector::s_OpenWindow(path.string());
                    }
                    else if (OpenInExternalEditor(path.string()))
                        return;
                    else if (m_CodeEditorPanel)
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
            m_HierarchyPanel->SetFrameCallback([this](Entity) {
                if (m_ViewportPanel)
                    m_ViewportPanel->FrameSelected();
            });
            m_HierarchyPanel->SetSelectionChangedCallback([this](Entity entity) {
                m_InspectorPanel->SetSelectedEntity(entity);
                if (m_ViewportPanel)
                    m_ViewportPanel->SetSelectedEntity(entity);
                if (m_AnimatorPanel)
                    m_AnimatorPanel->SetSelectedEntity(entity);
                if (m_ShaderEditorPanel)
                    m_ShaderEditorPanel->SetSelectedEntity(entity);

                // Entity seçilince animator seçimini temizle
                auto& selInfo = AnimatorPanel::GetSelectionInfo();
                selInfo.HasSelection = false;
                selInfo.IsState = false;
                selInfo.IsTransition = false;
                selInfo.StateName.clear();
                selInfo.TransitionFrom.clear();
                selInfo.TransitionTo.clear();
            });
            
            // Inspector'a scene context'i ver
            m_InspectorPanel->SetContext(m_ActiveScene.get());
        }

        CQ_CORE_INFO("EditorLayer: Initialization complete");

        RegisterAutomationCommands();
        SetupAddonHost();

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
                            // Başlangıç sahnesi olmayan projede de İçerik Tarayıcısı proje klasörünü göstersin
                            if (m_ContentBrowserPanel) m_ContentBrowserPanel->RefreshContext();

                            const auto& startScene = loadedProject->GetConfig().StartScene;
                            m_CurrentScenePath = startScene.empty() ? std::filesystem::path() : projectPath / startScene;
                            CQ_CORE_INFO("[AutoLoad] StartScene path: {0}", m_CurrentScenePath.string());

                            if (!m_CurrentScenePath.empty() && std::filesystem::is_regular_file(m_CurrentScenePath))
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
                                    if (m_AnimatorPanel) m_AnimatorPanel->SetContext(m_ActiveScene.get());
                                    if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
                                    if (m_AIPanel) m_AIPanel->SetContext(m_ActiveScene);
                                    if (m_ContentBrowserPanel) m_ContentBrowserPanel->RefreshContext();
                                }
                                else
                                {
                                    CQ_CORE_ERROR("[AutoLoad] Failed to deserialize scene");
                                }
                            }
                            else if (!m_CurrentScenePath.empty())
                            {
                                CQ_CORE_ERROR("[AutoLoad] StartScene not found: {0}", m_CurrentScenePath.string());
                                m_CurrentScenePath.clear();
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
        // Düzgün kapanış: kurtarma kopyası yalnızca çökmeler içindir
        if (!m_CurrentScenePath.empty())
            RemoveRecoverySnapshot(m_CurrentScenePath);
        // Paneller yok edilmeden önce log geri çağrısını kaldır (kilitli; sonrasında
        // arka plan log thread'i LogPanel'e erişemez)
        Log::SetConsoleCallback(nullptr);
        AI::AIManager::Get().Shutdown();
        // Addon VM'leri paylaşılan CQS yığınını kullanır: statik yıkım sırasından önce kapat
        AddonHost::Get().UnloadAll();
        // Geri alma komutları sahne (GPU kaynakları) tutabilir: GL bağlamı varken bırak
        UndoRedoManager::Get().Clear();
        // Doku denetçisinin önizleme dokusu (statik) da GL bağlamı varken bırakılır
        AssetInspector::Invalidate();
        // ImNodes bağlamları sahibi olan paneller tarafından yok edilir
        DebugDraw::Shutdown();
    }

    void EditorLayer::SyncPanelContexts()
    {
        // Aktif sahne hangi yoldan değişirse değişsin (yükleme, yeni sahne, oynat/durdur,
        // otomatik proje yükleme) tüm paneller aynı sahneyi görmeli; aksi halde silinmiş
        // sahneye işaretçi kalır.
        if (m_ActiveScene.get() != m_PanelSceneContext)
            RefreshAllPanels();
        // Yeni / açılan sahne: eski sahneye ait geri alma komutları anlamsız (ve eski sahneyi canlı tutar)
        if (m_EditorScene.get() != m_UndoSceneOwner)
        {
            if (m_UndoSceneOwner)
                UndoRedoManager::Get().Clear();
            m_UndoSceneOwner = m_EditorScene.get();
        }
    }

    uint64_t EditorLayer::GetSelectedEntityUUID() const
    {
        Entity e = EditorState::Get().GetSelectedEntity();
        Scene* scene = e.GetScene();
        // Yalnızca canlı sahnelere ait seçim okunur (silinmiş sahne işaretçisine dokunma)
        if (!e || !scene || (scene != m_EditorScene.get() && scene != m_RuntimeScene.get()))
            return 0;
        if (!scene->m_Registry.valid(e) || !e.HasComponent<IDComponent>())
            return 0;
        return e.GetComponent<IDComponent>().ID;
    }

    void EditorLayer::RestoreSelectionByUUID(uint64_t uuid)
    {
        Entity found = uuid && m_ActiveScene ? m_ActiveScene->FindEntityByUUID(uuid) : Entity{};
        SelectEntityEverywhere(found);
    }

    // Project Settings > Editor > External Script Editor: betiği dış düzenleyicide aç (boşsa false)
    bool EditorLayer::OpenInExternalEditor(const std::string& path)
    {
        const std::string& cmd = ProjectSettings::Editor().ExternalScriptEditor;
        if (cmd.empty())
            return false;
        std::string quoted = "\"";
        for (char c : path)
        {
            if (c == '"' || c == '\\' || c == '$' || c == '`') quoted += '\\';
            quoted += c;
        }
        quoted += "\"";
#ifdef _WIN32
        const std::string line = "start \"\" " + cmd + " " + quoted;
#else
        const std::string line = cmd + " " + quoted + " >/dev/null 2>&1 &";
#endif
        CQ_CORE_INFO("[Editor] Opening {} with {}", path, cmd);
        return std::system(line.c_str()) == 0;
    }

    void EditorLayer::TrackCreatedEntities()
    {
        // Düzenleme modunda komut kaydedilmeden ortaya çıkan nesneler (Create menüsü, Insert araç çubuğu, sürükle-bırak,
        // eklentiler, otomasyon) geri alınabilir "Create" adımı olur (Unity'de nesne oluşturmak Ctrl+Z ile geri alınır)
        auto& undo = UndoRedoManager::Get();
        Scene* scene = m_ActiveScene.get();
        if (!scene || !EditorState::Get().IsEditing())
        {
            m_TrackScene = nullptr;
            return;
        }
        auto ids = scene->m_Registry.view<IDComponent>();
        const size_t count = ids.size();
        auto rebuild = [&]() {
            m_TrackIDs.clear();
            for (auto e : ids)
                m_TrackIDs.insert((uint64_t)ids.get<IDComponent>(e).ID);
            m_TrackCount = count;
            m_TrackScene = scene;
            m_TrackVersion = undo.GetVersion();
        };
        if (scene != m_TrackScene || undo.GetVersion() != m_TrackVersion || count < m_TrackCount)
        {
            rebuild();
            return;
        }
        if (count == m_TrackCount)
            return;
        std::vector<uint64_t> created;
        for (auto e : ids)
        {
            const uint64_t id = (uint64_t)ids.get<IDComponent>(e).ID;
            if (!m_TrackIDs.count(id))
                created.push_back(id);
        }
        std::unordered_set<uint64_t> createdSet(created.begin(), created.end());
        std::vector<uint64_t> roots;
        std::string label;
        for (uint64_t id : created)
        {
            Entity e = scene->FindEntityByUUID(id);
            Entity parent = e ? e.GetParent() : Entity{};
            if (e && (!parent || !createdSet.count((uint64_t)parent.GetComponent<IDComponent>().ID)))
            {
                roots.push_back(id);
                if (label.empty())
                    label = "Create " + e.GetComponent<TagComponent>().Tag;
            }
        }
        rebuild();
        if (roots.empty())
            return;
        if (roots.size() > 1)
            label += " (+" + std::to_string(roots.size() - 1) + ")";
        std::shared_ptr<Scene> shared = m_ActiveScene;
        undo.Execute(std::make_shared<CreateEntitiesCommand>(shared, std::move(roots), std::move(created), SceneSnapshotCommand::Capture(shared), label,
                                                             [this]() {
                                                                 SelectEntityEverywhere({});
                                                                 RefreshAllPanels();
                                                             }));
        m_TrackVersion = undo.GetVersion();
    }

    namespace
    {
        // camera_fly otomasyonu: editör kamerası her karede sürekli hareket eder (RMB + Shift+W gibi: ivmesiz, anında durur)
        struct AutomationFly
        {
            glm::vec3 LocalVelocity{ 0.0f };   // x sağ, y yukarı, z ileri (m / sn)
            int Frames = 0;
        };
        AutomationFly s_AutomationFly;
        // camera_spin otomasyonu: editör kamerası her karede sabit hızla döner (odak noktası çevresinde ya da yerinde), sonra
        // anında durur
        struct AutomationSpin
        {
            glm::vec2 RateDeg{ 0.0f };   // x yaw, y pitch (derece / sn)
            int Frames = 0;
            bool InPlace = false;        // true: kamera yerinde döner (sağ tıkla bakınmak), false: odak noktası çevresinde
        };
        AutomationSpin s_AutomationSpin;
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        if (EditorState::Get().IsPlaying() && m_ActiveScene && Scripting::TestRunner::HasQueuedPlayMode() &&
            Scripting::TestRunner::UpdatePlayMode(*m_ActiveScene, (float)ts))
            OnStopButtonPressed();
        if (s_AutomationFly.Frames > 0 && m_ViewportPanel)
        {
            auto& cam = m_ViewportPanel->GetEditorCamera();
            const glm::vec3 v = s_AutomationFly.LocalVelocity;
            const glm::vec3 world = cam.GetRightDirection() * v.x + cam.GetUpDirection() * v.y + cam.GetForwardDirection() * v.z;
            // Sabit 1/60 sn adım: başsız test ortamında kare süresi değişken (10-60 FPS); hareket 60 FPS'teki Shift+W ile aynı olsun
            cam.SetFocalPoint(cam.GetFocalPoint() + world * (1.0f / 60.0f));
            --s_AutomationFly.Frames;
        }
        if (s_AutomationSpin.Frames > 0 && m_ViewportPanel)
        {
            auto& cam = m_ViewportPanel->GetEditorCamera();
            const glm::vec3 eye = cam.GetPosition();
            cam.SetYaw(cam.GetYaw() + glm::radians(s_AutomationSpin.RateDeg.x / 60.0f));
            cam.SetPitch(cam.GetPitch() + glm::radians(s_AutomationSpin.RateDeg.y / 60.0f));
            // Yerinde dönme: odak noktası, kamera konumu değişmeyecek şekilde yeni bakış yönüne taşınır
            if (s_AutomationSpin.InPlace)
                cam.SetFocalPoint(cam.GetFocalPoint() + (eye - cam.GetPosition()));
            --s_AutomationSpin.Frames;
        }
        SyncPanelContexts();
        TrackCreatedEntities();
        FeedGameInput();
        {
            // Görsel betik kesme noktası: oyun bu karenin sonunda duraklar, grafik o düğümde açılır
            std::string graph;
            int node = 0;
            if (Visual::Debugger::ConsumeBreak(graph, node) && EditorState::Get().IsPlaying())
            {
                OnPauseButtonPressed();
                if (m_VisualScriptPanel)
                {
                    const std::string path = Visual::Debugger::GetGraphPath(graph);
                    if (!path.empty() && m_VisualScriptPanel->GetPath() != std::filesystem::path(path))
                        m_VisualScriptPanel->Open(path);
                    m_VisualScriptPanel->ShowBreakpointHit(node);
                }
                CQ_CORE_WARN("[Visual Script] Breakpoint hit in {0} (node {1}): play paused", graph, node);
            }
        }
        // Kaydedilmemiş değişiklik temeli: sahne ya da yolu değişince (açma, yeni proje) birkaç kare sonra alınır
        if (m_EditorScene.get() != m_BaselineScene || m_CurrentScenePath != m_BaselinePath)
        {
            m_BaselineScene = m_EditorScene.get();
            m_BaselinePath = m_CurrentScenePath;
            m_SceneBaseline.clear();
            m_SceneBaselineDelay = 3;
        }
        if (m_SceneBaselineDelay > 0 && --m_SceneBaselineDelay == 0)
            CaptureSceneBaseline();
        // Project Settings > Editor > Auto Save: kayıtlı ve değişmiş sahneyi belirli aralıkla kaydet
        if (const int minutes = ProjectSettings::Editor().AutoSaveMinutes; minutes > 0 && EditorState::Get().IsEditing() && !Automation::IsActive())
        {
            m_SceneAutoSaveTimer += ts.GetSeconds();
            if (m_SceneAutoSaveTimer >= (float)minutes * 60.0f)
            {
                m_SceneAutoSaveTimer = 0.0f;
                if (!m_CurrentScenePath.empty() && HasUnsavedChanges())
                {
                    SaveScene();
                    CQ_CORE_INFO("[Editor] Auto-saved {}", m_CurrentScenePath.string());
                }
            }
        }
        // Oyun duraklatılınca ses de durur (Unity gibi); devam edince kaldığı yerden sürer
        if (AudioEngine::IsInitialized())
        {
            AudioEngine::Get().SetPaused(EditorState::Get().IsPaused());
            // Duraklatılmış sahne ses sistemini güncellemez: mikser ölçerleri yine de sönsün
            if (EditorState::Get().IsPaused())
                AudioMixer::Get().Update();
        }
        {
            CQ_PROFILE_SAMPLE("Addons.Update", Editor);
            AddonHost::Get().Update(ts.GetSeconds());
        }
        if (Profiling::FrameProfiler::IsRecording())
        {
            using Profiling::Counter;
            if (m_ActiveScene)
                Profiling::FrameProfiler::SetCounter(Counter::Entities, (double)m_ActiveScene->m_Registry.view<IDComponent>().size());
            Profiling::FrameProfiler::SetCounter(Counter::ScriptObjects, (double)CQS::CQSHeap::Get().GetObjectCount());
        }
        if (m_PendingPrefabRevert)
        {
            Entity target = m_PendingPrefabRevert;
            m_PendingPrefabRevert = {};
            if (m_ActiveScene && target.GetScene() == m_ActiveScene.get() && m_ActiveScene->m_Registry.valid((entt::entity)target))
            {
                SelectEntityEverywhere({});
                if (Entity fresh = PrefabSerializer::RevertInstance(target))
                    SelectEntityEverywhere(fresh);
            }
        }
        AI::AIManager::Get().Update();

        auto& editorState = EditorState::Get();

        // Shader dosyaları diskte değiştiyse yeniden derle (saniyede bir kontrol)
        Renderer3D::CheckShaderHotReload();
        // Diskte değişen / silinen görseller (Unity / Unreal yeniden içe aktarma): sahne, Sprite Renderer ve küçük resimler
        // aynı doku nesnesini kullandığı için hepsi birlikte güncellenir
        {
            static auto s_LastTextureCheck = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - s_LastTextureCheck > std::chrono::milliseconds(500))
            {
                s_LastTextureCheck = now;
                Texture2D::CheckFileChanges();
                MaterialAsset::CheckFileChanges();   // .cqmat dışarıda değiştiyse yerinde yenile
            }
        }
        // Düzenlenen malzeme varlıkları: sürükleme bitince diske
        MaterialEditorUI::FlushPendingSaves();

        // Log paneli "Error Pause": oynarken hata gelirse duraklat
        if (m_LogPanel && m_LogPanel->ConsumeErrorPauseRequest() && editorState.IsPlaying())
            OnPauseButtonPressed();

        // Oyunun imleç kilidi (Unity / Unreal gibi): oynarken imleç gizli ve kilitli. Esc, Shift+F1 ya da pencere
        // odağının kaybı kilidi bırakır, imleç Game görünümünün ortasında çıkar; bırakılmışken oyun fareyi görmez.
        // Game görünümüne herhangi bir fare tuşuyla tıklamak kilidi geri getirir (o tık oyuna gitmez: silah ateşlenmez).
        if (editorState.IsPaused())
        {
            // Duraklatma imleci bırakır ama oyunun isteği kalır: devam edince Game görünümüne tıklamak kilitler
            if (Input::GetAppliedCursorMode() != 0)
                Input::ReleaseCursor();
        }
        else if (!editorState.IsPlaying())
        {
            if (Input::GetAppliedCursorMode() != 0 || Input::IsCursorReleased())
                Input::SetCursorMode(0);
        }
        else if (Input::GetCursorMode() != 0)
        {
            // Odak: yalnızca odaklıyken kaybedildiği anda (Alt+Tab); başsız/odaksız pencerede kilit kalabilir
            const bool focused = glfwGetWindowAttrib(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()), GLFW_FOCUSED) != 0;
            const bool lostFocus = m_WindowWasFocused && !focused;
            m_WindowWasFocused = focused;
            const bool release = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                 (ImGui::IsKeyPressed(ImGuiKey_F1, false) && ImGui::GetIO().KeyShift) || lostFocus;
            if (Input::GetAppliedCursorMode() != 0 && release)
            {
                // ImGui ekran koordinatı -> pencere koordinatı (çoklu viewport'ta ImGui mutlak koordinat kullanır)
                glm::vec2 at(-1.0f);
                if (m_GamePanel)
                {
                    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
                    at = m_GamePanel->GetViewportCenter() - glm::vec2(origin.x, origin.y);
                }
                Input::ReleaseCursor(at.x, at.y);
            }
            else if (Input::IsCursorReleased())
            {
                if (m_GamePanel && m_GamePanel->IsViewportHovered() &&
                    (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                     ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
                {
                    Input::RecaptureCursor();
                    GameInput::ConsumeHeldButtons();
                }
            }
            // Oyun kilit istiyor, kullanıcı bırakmadı ama kilit uygulanmamış (odaksız başlangıç): odak gelince kilitle
            else if (Input::GetAppliedCursorMode() == 0 && focused)
                Input::RecaptureCursor();
        }
        // Fare oyuna kilitliyken editör panelleri fareyi görmez (Hiyerarşi'de seçim, butonlar, gizmo yok);
        // oyun tıklamaları ve klavyeyi Game görünümünün üzerinde olup olmamasından bağımsız alır
        {
            ImGuiIO& io = ImGui::GetIO();
            const bool captured = editorState.IsPlaying() && Input::GetAppliedCursorMode() != 0;
            if (captured)
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            else
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            m_GameMouseCaptured = captured;
        }
        
        // FPS hesapla
        m_FrameTime = ts.GetMilliseconds();
        m_FPS = 1.0f / ts.GetSeconds();
        
        // Otomatik kayıt: düzenleme modunda kurtarma kopyası (Unreal "Autosaves" gibi; sahne dosyasına dokunmaz,
        // oyun modundaki çalışma zamanı durumu asla kaydedilmez)
        if (!m_CurrentScenePath.empty() && editorState.IsEditing())
        {
            m_AutoSaveTimer += ts;
            if (m_AutoSaveTimer >= m_AutoSaveInterval)
            {
                m_AutoSaveTimer = 0.0f;
                WriteRecoverySnapshot();
            }
        }
        
        // Viewport bilgilerini her modda set et (script'ler için)
        if (m_ViewportPanel && m_ActiveScene)
        {
            m_ActiveScene->SetViewportBounds(m_ViewportPanel->GetViewportBounds()[0], m_ViewportPanel->GetViewportBounds()[1]);
            m_ActiveScene->SetViewportHovered(m_ViewportPanel->IsViewportHovered());
            m_ActiveScene->SetEditorViewProjection(m_ViewportPanel->GetEditorCamera().GetViewProjection());
            m_ActiveScene->SetUseEditorCameraForScripts(true);
        }

        // Update active scene
        // NOT: Editör sahne render'ı yalnızca ViewportPanel::OnUpdate içinden yapılır
        // (burada ayrıca OnUpdateEditor çağırmak sahneyi/shadow'u frame başına 2 kez
        // çizip stats'ı bozuyordu). Play modunda OnUpdateRuntime GamePanel'dedir.
        if (editorState.IsSimulating())
        {
            m_ActiveScene->OnUpdateSimulation(ts);
        }

        // Parçacık önizlemesi: editörde yalnızca seçili sistemler simüle edilir (Unity gibi);
        // sahne görünümünde "Realtime" açıksa hepsi oynar
        if (m_ActiveScene)
        {
            const bool editing = EditorState::Get().IsEditing();
            const bool realtime = m_ViewportPanel && m_ViewportPanel->GetViewOptions().Realtime;
            auto view = m_ActiveScene->m_Registry.view<ParticleSystemComponent>();
            for (auto e : view)
                view.get<ParticleSystemComponent>(e).EditorPreview = editing && (realtime || EditorState::Get().IsEntitySelected(Entity{ e, m_ActiveScene.get() }));
        }

        // Update viewport
        if (m_ViewportPanel)
        {
            CQ_PROFILE_SAMPLE("SceneView.Render", Editor);
            CQ_PROFILE_GPU("Scene View");
            m_ViewportPanel->OnUpdate(ts);
        }

        // Bekleyen çok oyunculu başlatma ("Host" Play'e bastıktan sonra)
        if (m_MultiplayerPanel)
            m_MultiplayerPanel->Update(!EditorState::Get().IsEditing());

        // Update game panel (duraklatılmışken "Step" tek kare ilerletir)
        if (m_GamePanel)
        {
            CQ_PROFILE_SAMPLE("GameView", Other);
            CQ_PROFILE_GPU("Game View");
            bool advance = editorState.IsPlaying() || m_StepFrame;
            m_GamePanel->SetPlaying(!editorState.IsEditing());
            m_GamePanel->OnUpdate(m_StepFrame && !editorState.IsPlaying() ? Timestep(1.0f / 60.0f) : ts, advance);
            m_StepFrame = false;
        }

        // Betik istekleri: QuitGame -> Play durur, LoadScene -> oyun içi sahne geçişi (karenin sonunda)
        if (!editorState.IsEditing())
        {
            if (SceneManager::TakeQuitRequest())
                OnStopButtonPressed();
            else if (SceneManager::HasPendingLoad())
                LoadRuntimeScene(SceneManager::TakePendingLoad());
        }
        else if (SceneManager::HasPendingLoad())
        {
            SceneManager::Reset();
        }
        
        // Update stats
        if (m_StatsPanel)
        {
            m_StatsPanel->SetStats(Renderer::GetStats());
            m_StatsPanel->SetFPS(m_FPS, m_FrameTime);
        }
    }

    void EditorLayer::OnImGuiRender()
    {
        GameInput::CaptureImGuiFrameInput();
        SyncPanelContexts();
        if (!m_PendingFocusWindow.empty())
        {
            // "Profiler" gibi görünen adlar "###Profiler" kimliğiyle açılmış olabilir
            std::string name = m_PendingFocusWindow;
            if (!ImGui::FindWindowByName(name.c_str()) && ImGui::FindWindowByName(("###" + name).c_str()))
                name = "###" + name;
            ImGui::SetWindowFocus(name.c_str());
            m_PendingFocusWindow.clear();
        }

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
                    if (m_AnimatorPanel) m_AnimatorPanel->SetContext(m_ActiveScene.get());
                    if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
                    if (m_AIPanel) m_AIPanel->SetContext(m_ActiveScene);
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
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowFileDropEvent>([this](WindowFileDropEvent& drop) {
            if (m_ContentBrowserPanel)
                m_ContentBrowserPanel->QueueDroppedFiles(drop.GetPaths());
            return true;
        });
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
                auto dock_id_top = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 106.0f / std::max(ImGui::GetMainViewport()->Size.y * 0.75f, 400.0f), nullptr, &dockspace_id);
                
                // Sol tarafı ayır (Hierarchy) ve altına Terrain araçları
                auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.28f, nullptr, &dockspace_id);
                auto dock_id_left_bottom = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.42f, nullptr, &dock_id_left);
                m_ToolsDockId = dock_id_left_bottom;

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
                ImGui::DockBuilderDockWindow("###Multiplayer", dock_id_right);
                ImGui::DockBuilderDockWindow("AI Assistant", dock_id_right);
                ImGui::DockBuilderDockWindow("Content Browser", dock_id_down);
                ImGui::DockBuilderDockWindow("Log", dock_id_down);
                ImGui::DockBuilderDockWindow("Console", dock_id_down);
                ImGui::DockBuilderDockWindow("###Timeline", dock_id_down);
                ImGui::DockBuilderDockWindow("###AnimationWindow", dock_id_down);   // Unity: Animation alt bölmede (Project / Console yanında)
                ImGui::DockBuilderDockWindow("Viewport", dockspace_id);
                ImGui::DockBuilderDockWindow("Game", dockspace_id);
                ImGui::DockBuilderDockWindow("Animator", dockspace_id);
                ImGui::DockBuilderDockWindow("###Terrain", dock_id_left_bottom);
                ImGui::DockBuilderDockWindow("###TilePalette", dock_id_left_bottom);
                ImGui::DockBuilderDockWindow("###Audio Mixer", dockspace_id);
                ImGui::DockBuilderDockWindow("###Sprite Editor", dockspace_id);
                ImGui::DockBuilderDockWindow("Shader Graph Editor", dockspace_id);
                ImGui::DockBuilderDockWindow("Code Editor###CodeEditor", dockspace_id);
                ImGui::DockBuilderDockWindow("###VisualScript", dockspace_id);
                ImGui::DockBuilderDockWindow("###NodeAsset", dockspace_id);

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
            AddonsMenu();
            HelpMenu();
            BuildMenu();

            float buttonWidth = 50.0f;
            float rightX = ImGui::GetWindowWidth() - buttonWidth - 10.0f;
            if (rightX > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(rightX);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.9f));
            if (ImGui::Button(" AI ", ImVec2(buttonWidth, 0)))
            {
                m_ShowAIPanel = !m_ShowAIPanel;
            }
            ImGui::PopStyleColor();

            ImGui::EndMenuBar();
        }
    }

    namespace
    {
        // Ribbon düğmesi: üstte ikon, altta etiket (Roblox Studio / Office tarzı)
        // id: etiket değişiyorsa (kayıt süresi gibi) sabit kimlik; yoksa basma/bırakma arasında tıklama kaybolur
        bool RibbonButton(const char* icon, const char* label, const char* tooltip, bool active = false, float width = 0.0f, bool enabled = true,
                          const char* id = nullptr)
        {
            ImFont* font = ImGui::GetFont();
            const float iconSize = 19.0f, labelSize = 12.0f;
            float lw = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, label).x;
            float w = width > 0.0f ? width : std::max(44.0f, lw + 14.0f);
            const float h = 54.0f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(id ? id : icon);
            ImGui::BeginDisabled(!enabled);
            bool pressed = ImGui::InvisibleButton(id ? "##ribbon" : label, ImVec2(w, h));
            bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
            ImGui::EndDisabled();
            ImGui::PopID();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (active)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, held ? 0.55f : 0.38f)), 5.0f);
            else if (hovered && enabled)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, held ? 0.14f : 0.08f)), 5.0f);
            ImU32 col = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            ImVec2 is = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
            dl->AddText(font, iconSize, ImVec2(p.x + (w - is.x) * 0.5f, p.y + 7.0f), active ? ImGui::GetColorU32(ImVec4(0.55f, 0.78f, 1.0f, 1.0f)) : col, icon);
            dl->AddText(font, labelSize, ImVec2(p.x + (w - lw) * 0.5f, p.y + h - labelSize - 7.0f), col, label);
            if (hovered && tooltip && tooltip[0])
                ImGui::SetTooltip("%s", tooltip);
            return pressed && enabled;
        }

        struct RibbonGroup
        {
            float StartX = 0.0f;
            const char* Caption = "";
        };

        RibbonGroup BeginRibbonGroup(const char* caption)
        {
            RibbonGroup g;
            g.StartX = ImGui::GetCursorScreenPos().x;
            g.Caption = caption;
            ImGui::BeginGroup();
            return g;
        }

        void EndRibbonGroup(const RibbonGroup& g, float top)
        {
            ImGui::EndGroup();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float endX = ImGui::GetItemRectMax().x;
            ImFont* font = ImGui::GetFont();
            const float cs = 11.0f;
            float cw = font->CalcTextSizeA(cs, FLT_MAX, 0.0f, g.Caption).x;
            dl->AddText(font, cs, ImVec2(g.StartX + (endX - g.StartX - cw) * 0.5f, top + 57.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), g.Caption);
            ImGui::SameLine(0.0f, 7.0f);
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(p.x - 4.0f, top + 4.0f), ImVec2(p.x - 4.0f, top + 68.0f), ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::SameLine(0.0f, 5.0f);
        }
    }

    void EditorLayer::RenderToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
        ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        auto& editorState = EditorState::Get();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ---- Sekme şeridi
        static const char* tabs[] = { "HOME", "CREATE", "PLAY", "VIEW", "ADDONS" };
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            for (int i = 0; i < 5; ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, 2.0f);
                bool active = m_RibbonTab == i;
                ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton(tabs[i], ImVec2(ts.x + 26.0f, 22.0f)))
                    m_RibbonTab = i;
                bool hovered = ImGui::IsItemHovered();
                if (active)
                {
                    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 26.0f, p.y + 22.0f), ImGui::GetColorU32(ImGuiCol_TabSelected), 4.0f, ImDrawFlags_RoundCornersTop);
                    dl->AddLine(ImVec2(p.x + 4, p.y + 21.0f), ImVec2(p.x + ts.x + 22.0f, p.y + 21.0f), ImGui::GetColorU32(UI::Colors::Accent), 2.0f);
                }
                else if (hovered)
                    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 26.0f, p.y + 22.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)), 4.0f, ImDrawFlags_RoundCornersTop);
                dl->AddText(ImVec2(p.x + 13.0f, p.y + (22.0f - ts.y) * 0.5f), ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), tabs[i]);
            }
            // Sağ: sahne adı
            std::string sceneName = m_CurrentScenePath.empty() ? std::string("Untitled") : m_CurrentScenePath.stem().string();
            ImVec2 ns = ImGui::CalcTextSize(sceneName.c_str());
            dl->AddText(ImVec2(p0.x + ImGui::GetContentRegionAvail().x - ns.x - 8.0f, p0.y + 4.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), sceneName.c_str());
            dl->AddLine(ImVec2(p0.x - 6.0f, p0.y + 22.0f), ImVec2(p0.x + ImGui::GetWindowWidth(), p0.y + 22.0f), ImGui::GetColorU32(ImGuiCol_Separator));
        }

        // ---- Ribbon içeriği
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 3.0f));
        const float top = ImGui::GetCursorScreenPos().y;
        const float playGroupW = 5 * 46.0f + 40.0f;
        ImGui::BeginChild("##ribbon", ImVec2(ImGui::GetContentRegionAvail().x - playGroupW, 74.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        const int gizmo = m_ViewportPanel ? m_ViewportPanel->GetGizmoType() : 7;
        auto setGizmo = [&](int t) { if (m_ViewportPanel) m_ViewportPanel->SetGizmoType(t); };
        Entity selected = editorState.GetSelectedEntity();

        auto toolsGroup = [&]() {
            auto g = BeginRibbonGroup("Tools");
            if (RibbonButton(ICON_FA_ARROW_POINTER, "Select", "Select / view tool (Q)", gizmo == -1)) setGizmo(-1);
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_UP_DOWN_LEFT_RIGHT, "Move", "Move (W)", gizmo == ImGuizmo::TRANSLATE)) setGizmo(ImGuizmo::TRANSLATE);
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_ROTATE, "Rotate", "Rotate (E)", gizmo == ImGuizmo::ROTATE)) setGizmo(ImGuizmo::ROTATE);
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Scale", "Scale (R)", gizmo == ImGuizmo::SCALE)) setGizmo(ImGuizmo::SCALE);
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_VECTOR_SQUARE, "Rect", "Rect tool (T): drag a 2D object to move it, drag its handles to resize it", gizmo == ViewportPanel::kGizmoRect)) setGizmo(ViewportPanel::kGizmoRect);
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_CUBES, "Transform", "Universal transform (Y)", gizmo == ImGuizmo::UNIVERSAL)) setGizmo(ImGuizmo::UNIVERSAL);
            EndRibbonGroup(g, top);
        };
        auto snapGroup = [&]() {
            auto g = BeginRibbonGroup("Snap & Space");
            bool world = editorState.GetGizmoMode() == GizmoMode::World;
            if (RibbonButton(world ? ICON_FA_GLOBE : ICON_FA_CUBE, world ? "Global" : "Local", "Gizmo orientation (X)", false, 52.0f))
                editorState.ToggleGizmoMode();
            ImGui::SameLine();
            bool snap = editorState.IsSnapEnabled();
            if (RibbonButton(ICON_FA_MAGNET, "Snap", "Grid snapping (hold Ctrl to toggle while dragging)", snap))
                editorState.SetSnapEnabled(!snap);
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 8));
            float snapValue = editorState.GetSnapValue();
            ImGui::SetNextItemWidth(64.0f);
            if (ImGui::DragFloat("##snapvalue", &snapValue, 0.05f, 0.01f, 100.0f, "%.2f m"))
                editorState.SetSnapValue(std::max(snapValue, 0.01f));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Move snap step in metres: with Snap on, the Move tool moves objects in steps of this size.\n"
                                  "It does not change the object's size. Rotation snaps to 15 degrees.");
            ImGui::TextDisabled(" Snap step");
            ImGui::EndGroup();
            EndRibbonGroup(g, top);
        };
        auto insertButton = [&](const char* icon, const char* label, auto create) {
            if (RibbonButton(icon, label, nullptr))
            {
                Entity e = create();
                if (e) SelectEntityEverywhere(e);
            }
        };
        auto primitive = [&](const char* name, MeshType type) {
            return [this, name, type]() {
                Entity e = m_ActiveScene->CreateEntity(name);
                auto& mr = e.AddComponent<MeshRendererComponent>();
                mr.Type = type;
                AssetActions::AddPrimitiveCollider(e);
                return e;
            };
        };

        switch (m_RibbonTab)
        {
            case 0: // HOME
            {
                auto g = BeginRibbonGroup("Clipboard");
                if (RibbonButton(ICON_FA_PASTE, "Paste", "Paste (Ctrl+V)", false, 0.0f, editorState.HasClipboardData())) EditPaste();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_COPY, "Copy", "Copy (Ctrl+C)", false, 0.0f, (bool)selected)) EditCopy();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_SCISSORS, "Cut", "Cut (Ctrl+X)", false, 0.0f, (bool)selected)) EditCut();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_CLONE, "Duplicate", "Duplicate (Ctrl+D)", false, 0.0f, (bool)selected)) EditDuplicate();
                EndRibbonGroup(g, top);
                toolsGroup();
                snapGroup();
                g = BeginRibbonGroup("Insert");
                if (RibbonButton(ICON_FA_SQUARE_PLUS, "Insert", "Insert a GameObject")) ImGui::OpenPopup("##RibbonInsert");
                if (ImGui::BeginPopup("##RibbonInsert"))
                {
                    Entity created = HierarchyPanel::DrawCreateMenuItems(m_ActiveScene.get(), Entity{});
                    if (created) SelectEntityEverywhere(created);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                // Shape: temel şekil ekler (Roblox'taki "Part"); okuyla şekil seçilir
                if (RibbonButton(ICON_FA_SHAPES, "Shape", "Insert a basic shape (cube, sphere, cylinder, plane)"))
                    ImGui::OpenPopup("##RibbonShape");
                if (ImGui::BeginPopup("##RibbonShape"))
                {
                    struct ShapeItem { const char* Icon; const char* Name; MeshType Type; };
                    const ShapeItem shapes[] = { { ICON_FA_CUBE, "Cube", MeshType::Cube }, { ICON_FA_CIRCLE, "Sphere", MeshType::Sphere },
                                                 { ICON_FA_DATABASE, "Cylinder", MeshType::Cylinder }, { ICON_FA_SQUARE, "Plane", MeshType::Plane } };
                    for (const auto& item : shapes)
                    {
                        std::string label = std::string(item.Icon) + "  " + item.Name;
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            Entity e = primitive(item.Name, item.Type)();
                            if (e) SelectEntityEverywhere(e);
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                insertButton(ICON_FA_LIGHTBULB, "Light", [this]() { Entity e = m_ActiveScene->CreateEntity("Point Light"); e.AddComponent<PointLightComponent>(); e.GetComponent<TransformComponent>().Position = { 0, 3, 0 }; return e; });
                ImGui::SameLine();
                insertButton(ICON_FA_MOUNTAIN_SUN, "Terrain", [this]() { m_ShowTerrain = true; return TerrainPanel::CreateTerrain(m_ActiveScene.get()); });
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Edit");
                if (RibbonButton(ICON_FA_CROSSHAIRS, "Focus", "Frame selected (F)", false, 0.0f, (bool)selected) && m_ViewportPanel) m_ViewportPanel->FrameSelected();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_TRASH, "Delete", "Delete (Del)", false, 0.0f, (bool)selected)) EditDelete();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_ROTATE_LEFT, "Undo", "Undo (Ctrl+Z)", false, 0.0f, UndoRedoManager::Get().CanUndo())) PerformUndo();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_ROTATE_RIGHT, "Redo", "Redo (Ctrl+Y)", false, 0.0f, UndoRedoManager::Get().CanRedo())) PerformRedo();
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Scene");
                if (RibbonButton(ICON_FA_FILE, "New", "New scene (Ctrl+N)")) RequestNewScene();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_FOLDER_OPEN, "Open", "Open scene (Ctrl+O)")) OpenScene();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_FLOPPY_DISK, "Save", "Save scene (Ctrl+S)")) SaveScene();
                EndRibbonGroup(g, top);
                break;
            }
            case 1: // CREATE
            {
                toolsGroup();
                auto g = BeginRibbonGroup("Parts");
                insertButton(ICON_FA_CUBE, "Block", primitive("Block", MeshType::Cube));
                ImGui::SameLine();
                insertButton(ICON_FA_CIRCLE, "Sphere", primitive("Sphere", MeshType::Sphere));
                ImGui::SameLine();
                insertButton(ICON_FA_DATABASE, "Cylinder", primitive("Cylinder", MeshType::Cylinder));
                ImGui::SameLine();
                insertButton(ICON_FA_SQUARE, "Plane", primitive("Plane", MeshType::Plane));
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Physics");
                bool hasRb = selected && selected.HasComponent<RigidbodyComponent>();
                bool anchored = hasRb && selected.GetComponent<RigidbodyComponent>().Type == RigidbodyComponent::BodyType::Static;
                if (RibbonButton(ICON_FA_ANCHOR, "Anchor", "Make the selection static (anchored)", anchored, 0.0f, (bool)selected))
                {
                    if (!selected.HasComponent<RigidbodyComponent>()) selected.AddComponent<RigidbodyComponent>();
                    if (!selected.HasComponent<BoxColliderComponent>() && !selected.HasComponent<SphereColliderComponent>() && !selected.HasComponent<CapsuleColliderComponent>())
                        selected.AddComponent<BoxColliderComponent>();
                    auto& rb = selected.GetComponent<RigidbodyComponent>();
                    rb.Type = anchored ? RigidbodyComponent::BodyType::Dynamic : RigidbodyComponent::BodyType::Static;
                }
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_WEIGHT_HANGING, "Physics", "Add a dynamic rigidbody + collider", hasRb && !anchored, 0.0f, (bool)selected))
                {
                    if (!selected.HasComponent<RigidbodyComponent>()) selected.AddComponent<RigidbodyComponent>().Type = RigidbodyComponent::BodyType::Dynamic;
                    if (!selected.HasComponent<BoxColliderComponent>() && !selected.HasComponent<SphereColliderComponent>())
                    {
                        if (selected.HasComponent<MeshRendererComponent>() && selected.GetComponent<MeshRendererComponent>().Type == MeshType::Sphere)
                            selected.AddComponent<SphereColliderComponent>();
                        else
                            selected.AddComponent<BoxColliderComponent>();
                    }
                }
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Effects");
                for (const char* preset : { "Fire", "Smoke", "Sparks", "Explosion", "Rain" })
                {
                    static const char* icons[] = { ICON_FA_FIRE, ICON_FA_SMOG, ICON_FA_BOLT, ICON_FA_BURST, ICON_FA_CLOUD_RAIN };
                    static const char* names[] = { "Fire", "Smoke", "Sparks", "Explosion", "Rain" };
                    int idx = 0;
                    for (int k = 0; k < 5; ++k) if (std::string(names[k]) == preset) idx = k;
                    if (idx > 0) ImGui::SameLine();
                    insertButton(icons[idx], preset, [this, preset]() {
                        Entity e = m_ActiveScene->CreateEntity(preset);
                        ParticleSystem::ApplyPreset(e.AddComponent<ParticleSystemComponent>(), preset);
                        return e;
                    });
                }
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Environment");
                insertButton(ICON_FA_MOUNTAIN_SUN, "Terrain", [this]() { m_ShowTerrain = true; return TerrainPanel::CreateTerrain(m_ActiveScene.get()); });
                ImGui::SameLine();
                insertButton(ICON_FA_GLOBE, "Reflection", [this]() { Entity e = m_ActiveScene->CreateEntity("Reflection Probe"); e.AddComponent<ReflectionProbeComponent>(); return e; });
                ImGui::SameLine();
                insertButton(ICON_FA_CHESS_BOARD, "Hex Board", [this]() {
                    m_ShowTilePalette = true;
                    m_PendingFocusWindow = "###TilePalette";
                    return TilePalettePanel::CreateTilemap(m_ActiveScene.get(), GridLayout::HexagonPointyTop, GridPlane::XZ, true);
                });
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_TABLE_CELLS, "Tiles", "Open the Tile Palette", m_ShowTilePalette)) { m_ShowTilePalette = true; m_PendingFocusWindow = "###TilePalette"; }
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_SUN, "Lighting", "Open the Lighting window", m_ShowLighting)) { m_ShowLighting = true; m_PendingFocusWindow = "Lighting"; }
                EndRibbonGroup(g, top);
                break;
            }
            case 2: // PLAY
            {
                auto g = BeginRibbonGroup("Emulation");
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0, 6));
                auto& gv = GameViewSettings::Get();
                ImGui::SetNextItemWidth(190.0f);
                if (ImGui::BeginCombo("##emu", (std::string(ICON_FA_MOBILE_SCREEN "  ") + gv.Current().Name).c_str()))
                {
                    for (int i = 0; i < (int)gv.Presets.size(); ++i)
                        if (ImGui::Selectable(gv.Presets[(size_t)i].Name.c_str(), gv.Selected == i))
                            gv.Selected = i;
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Maximize On Play", &gv.MaximizeOnPlay);
                ImGui::EndGroup();
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Audio");
                if (RibbonButton(gv.MuteAudio ? ICON_FA_VOLUME_XMARK : ICON_FA_VOLUME_HIGH, gv.MuteAudio ? "Unmute" : "Mute", "Mute game audio", gv.MuteAudio)) gv.MuteAudio = !gv.MuteAudio;
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_SLIDERS, "Mixer", "Audio Mixer", m_ShowAudioMixer)) m_ShowAudioMixer = !m_ShowAudioMixer;
                EndRibbonGroup(g, top);
                g = BeginRibbonGroup("Capture");
                if (RibbonButton(ICON_FA_CAMERA, "Screenshot", "Save a screenshot of the Game view") && m_GamePanel) m_GamePanel->RequestScreenshot();
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_CHART_SIMPLE, "Stats", "Statistics overlay", m_ShowStats)) m_ShowStats = !m_ShowStats;
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_GAUGE_HIGH, "Profiler", "CPU / GPU frame profiler (Ctrl+7)", m_ShowProfiler))
                {
                    m_ShowProfiler = !m_ShowProfiler;
                    if (m_ShowProfiler) m_PendingFocusWindow = "###Profiler";
                }
                EndRibbonGroup(g, top);
                break;
            }
            case 3: // VIEW
            {
                auto g = BeginRibbonGroup("Windows");
                struct W { const char* Icon; const char* Name; bool* Flag; };
                W wins[] = {
                    { ICON_FA_SITEMAP, "Hierarchy", &m_ShowHierarchy }, { ICON_FA_SLIDERS, "Inspector", &m_ShowInspector },
                    { ICON_FA_FOLDER_TREE, "Assets", &m_ShowContentBrowser }, { ICON_FA_LIST, "Log", &m_ShowLog },
                    { ICON_FA_TERMINAL, "Console", &m_ShowConsole }, { ICON_FA_SUN, "Lighting", &m_ShowLighting },
                    { ICON_FA_MUSIC, "Mixer", &m_ShowAudioMixer }, { ICON_FA_OBJECT_UNGROUP, "Sprites", &m_ShowSpriteEditor },
                    { ICON_FA_MOUNTAIN_SUN, "Terrain", &m_ShowTerrain }, { ICON_FA_TABLE_CELLS, "Tiles", &m_ShowTilePalette },
                    { ICON_FA_GAMEPAD, "Game", &m_ShowGame }, { ICON_FA_GAUGE_HIGH, "Profiler", &m_ShowProfiler },
                    { ICON_FA_TOWER_BROADCAST, "Network", &m_ShowMultiplayer }, { ICON_FA_FILM, "Timeline", &m_ShowTimeline },
                    { ICON_FA_CLAPPERBOARD, "Animation", &m_ShowAnimation },
                };
                for (int i = 0; i < (int)(sizeof(wins) / sizeof(wins[0])); ++i)
                {
                    if (i > 0) ImGui::SameLine();
                    if (RibbonButton(wins[i].Icon, wins[i].Name, nullptr, *wins[i].Flag)) *wins[i].Flag = !*wins[i].Flag;
                }
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_PERSON_RUNNING, "Animator", nullptr, m_AnimatorPanel && m_AnimatorPanel->IsOpen()) && m_AnimatorPanel)
                    m_AnimatorPanel->SetOpen(!m_AnimatorPanel->IsOpen());
                ImGui::SameLine();
                if (m_CodeEditorPanel && RibbonButton(ICON_FA_CODE, "Code", nullptr, m_CodeEditorPanel->GetIsOpen()))
                    m_CodeEditorPanel->GetIsOpen() = !m_CodeEditorPanel->GetIsOpen();
                ImGui::SameLine();
                if (m_VisualScriptPanel && RibbonButton(ICON_FA_DIAGRAM_PROJECT, "Visual", "Visual Script editor", m_VisualScriptPanel->GetIsOpen()))
                    m_VisualScriptPanel->GetIsOpen() = !m_VisualScriptPanel->GetIsOpen();
                EndRibbonGroup(g, top);
                if (m_ViewportPanel)
                {
                    g = BeginRibbonGroup("Show");
                    auto& vo = m_ViewportPanel->GetViewOptions();
                    if (RibbonButton(ICON_FA_BORDER_ALL, "Grid", nullptr, vo.Grid)) vo.Grid = !vo.Grid;
                    ImGui::SameLine();
                    if (RibbonButton(ICON_FA_LOCATION_CROSSHAIRS, "Gizmos", nullptr, vo.Gizmos)) vo.Gizmos = !vo.Gizmos;
                    ImGui::SameLine();
                    if (RibbonButton(ICON_FA_CLOCK, "Realtime", "Play all particles and animated materials in the Scene view", vo.Realtime)) vo.Realtime = !vo.Realtime;
                    ImGui::SameLine();
                    bool is2D = m_ViewportPanel->GetEditorCamera().Is2D();
                    if (RibbonButton(is2D ? ICON_FA_SQUARE : ICON_FA_CUBE, is2D ? "2D" : "3D", "Toggle 2D / 3D scene view"))
                        m_ViewportPanel->GetEditorCamera().SetCameraMode(is2D ? EditorCameraMode::Perspective3D : EditorCameraMode::Orthographic2D);
                    EndRibbonGroup(g, top);
                }
                break;
            }
            case 4: // ADDONS
            {
                auto g = BeginRibbonGroup("Manage");
                if (RibbonButton(ICON_FA_PUZZLE_PIECE, "Addons", "Open the Addon Manager", m_ShowAddonManager))
                {
                    m_ShowAddonManager = !m_ShowAddonManager;
                    if (m_ShowAddonManager) m_PendingFocusWindow = "###AddonManager";
                }
                ImGui::SameLine();
                if (RibbonButton(ICON_FA_ARROWS_ROTATE, "Reload", "Reload all addons (after editing their scripts)"))
                    AddonHost::Get().ReloadAll();
                EndRibbonGroup(g, top);
                // Her etkin addon: kendi grubu (araç düğmeleri + prefab ekleme + paneller).
                // Şeride sığmayan gruplar en sondaki "More" düğmesinin menüsüne alt menü olarak toplanır.
                int shown = 0;
                std::vector<AddonHost::Instance*> overflow;
                ImFont* ribbonFont = ImGui::GetFont();
                auto itemWidth = [&](const char* label) { return std::max(44.0f, ribbonFont->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label).x + 14.0f); };
                const float moreWidth = 70.0f;
                const float limitX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - moreWidth;
                for (auto& inst : AddonHost::Get().GetInstances())
                {
                    const auto& m = inst->Manifest;
                    if (inst->Buttons.empty() && m.Inserts.empty() && inst->Panels.empty())
                        continue;
                    float groupW = 16.0f;
                    for (auto& b : inst->Buttons) groupW += itemWidth(b.Label.c_str()) + 8.0f;
                    for (auto& ins : m.Inserts) groupW += itemWidth(ins.Name.c_str()) + 8.0f;
                    for (auto& pnl : inst->Panels) groupW += itemWidth(pnl.Title.c_str()) + 8.0f;
                    groupW = std::max(groupW, ImGui::CalcTextSize(m.Name.c_str()).x + 16.0f);
                    if (!overflow.empty() || ImGui::GetCursorScreenPos().x + groupW > limitX)
                    {
                        overflow.push_back(inst.get());
                        continue;
                    }
                    ImGui::PushID(m.Id.c_str());
                    auto ag = BeginRibbonGroup(m.Name.c_str());
                    int n = 0;
                    for (auto& b : inst->Buttons)
                    {
                        if (n++) ImGui::SameLine();
                        ImGui::PushID(n);
                        if (RibbonButton(UI::IconFromName(b.Icon), b.Label.c_str(), b.Tooltip.empty() ? nullptr : b.Tooltip.c_str()))
                            AddonHost::Get().Run(*inst, b.Method, b.Label);
                        ImGui::PopID();
                    }
                    for (auto& ins : m.Inserts)
                    {
                        if (n++) ImGui::SameLine();
                        ImGui::PushID(n);
                        if (RibbonButton(UI::IconFromName(ins.Icon), ins.Name.c_str(), ins.Tooltip.empty() ? "Insert into the scene" : ins.Tooltip.c_str()))
                            if (Entity e = SpawnAddonInsert(m, ins)) SelectEntityEverywhere(e);
                        ImGui::PopID();
                    }
                    for (auto& p : inst->Panels)
                    {
                        if (n++) ImGui::SameLine();
                        ImGui::PushID(n);
                        if (RibbonButton(ICON_FA_WINDOW_MAXIMIZE, p.Title.c_str(), "Show / hide this addon panel", p.Open))
                        {
                            p.Open = !p.Open;
                            p.Focus = p.Open;
                        }
                        ImGui::PopID();
                    }
                    EndRibbonGroup(ag, top);
                    ImGui::PopID();
                    ++shown;
                }
                if (!overflow.empty())
                {
                    auto mg = BeginRibbonGroup("More");
                    std::string moreLabel = std::to_string(overflow.size()) + " more";
                    if (RibbonButton(ICON_FA_ELLIPSIS, moreLabel.c_str(), "Addons that do not fit on the ribbon"))
                        ImGui::OpenPopup("##AddonOverflow");
                    if (ImGui::BeginPopup("##AddonOverflow"))
                    {
                        for (AddonHost::Instance* inst : overflow)
                        {
                            const auto& m = inst->Manifest;
                            ImGui::PushID(m.Id.c_str());
                            std::string title = std::string(UI::IconFromName(m.Icon)) + "  " + m.Name;
                            if (ImGui::BeginMenu(title.c_str()))
                            {
                                for (auto& b : inst->Buttons)
                                {
                                    std::string item = std::string(UI::IconFromName(b.Icon)) + "  " + b.Label;
                                    if (ImGui::MenuItem(item.c_str()))
                                        AddonHost::Get().Run(*inst, b.Method, b.Label);
                                    if (!b.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.Tooltip.c_str());
                                }
                                for (auto& ins : m.Inserts)
                                {
                                    std::string item = std::string(UI::IconFromName(ins.Icon)) + "  " + ins.Name;
                                    if (ImGui::MenuItem(item.c_str()))
                                        if (Entity e = SpawnAddonInsert(m, ins)) SelectEntityEverywhere(e);
                                    if (!ins.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ins.Tooltip.c_str());
                                }
                                for (auto& pnl : inst->Panels)
                                {
                                    std::string item = std::string(ICON_FA_WINDOW_MAXIMIZE) + "  " + pnl.Title;
                                    if (ImGui::MenuItem(item.c_str(), nullptr, pnl.Open))
                                    {
                                        pnl.Open = !pnl.Open;
                                        pnl.Focus = pnl.Open;
                                    }
                                }
                                ImGui::EndMenu();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndPopup();
                    }
                    EndRibbonGroup(mg, top);
                }
                if (shown == 0 && overflow.empty())
                {
                    auto hint = BeginRibbonGroup("Get Started");
                    ImGui::BeginGroup();
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::TextDisabled("No addon tools yet.");
                    ImGui::TextDisabled("Install addons from the Addon Manager catalog.");
                    ImGui::EndGroup();
                    EndRibbonGroup(hint, top);
                }
                break;
            }
            default: break;
        }
        ImGui::EndChild();

        // ---- Sağ (her sekmede): oynatma + kayıt
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - playGroupW, top));
        {
            const bool playing = editorState.IsPlaying();
            const bool paused = editorState.IsPaused();
            const bool editing = editorState.IsEditing();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(p.x - 4.0f, top + 4.0f), ImVec2(p.x - 4.0f, top + 68.0f), ImGui::GetColorU32(ImGuiCol_Separator));
            if (RibbonButton(editing ? ICON_FA_PLAY : ICON_FA_STOP, editing ? "Play" : "Stop", "Play / Stop (Ctrl+P)", !editing, 46.0f))
            {
                if (editing) OnPlayButtonPressed();
                else OnStopButtonPressed();
            }
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_PAUSE, paused ? "Resume" : "Pause", "Pause / Resume (Ctrl+Shift+P)", paused, 46.0f, !editing))
            {
                if (paused) OnPlayButtonPressed();
                else OnPauseButtonPressed();
            }
            ImGui::SameLine();
            if (RibbonButton(ICON_FA_FORWARD_STEP, "Step", "Step one frame (Ctrl+Alt+P)", false, 46.0f, !editing))
            {
                if (playing) OnPauseButtonPressed();
                m_StepFrame = true;
            }
            ImGui::SameLine();
            const bool recording = Recorder::IsRecording();
            char recLabel[32];
            int secs = (int)Recorder::GetElapsedSeconds();
            snprintf(recLabel, sizeof(recLabel), recording ? "%02d:%02d" : "Record", secs / 60, secs % 60);
            ImGui::PushStyleColor(ImGuiCol_Text, recording ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
            if (RibbonButton(recording ? ICON_FA_SQUARE : ICON_FA_CIRCLE, recLabel, recording ? "Stop recording" : "Record the Game view", recording, 52.0f,
                             true, "RecordButton"))
            {
                if (recording)
                    Recorder::Stop();
                else if (m_GamePanel)
                {
                    glm::vec2 size = m_GamePanel->GetViewportSize();
                    if (!Recorder::Start((uint32_t)size.x, (uint32_t)size.y))
                        CQ_CORE_ERROR("Recording could not start: {0}", Recorder::GetLastError());
                    else
                        m_ShowGame = true;
                }
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            const bool recordingNow = recording;
            (void)recordingNow;
            // Ok simgesi kutunun tam ortasında: glyph'in görünen (mürekkep) sınırlarına göre piksel hassasında
            // ortalanır; metin hizalaması ilerleme genişliğini kullandığından oku ~1 px sola kaçırıyordu
            const float caretWidth = std::max(20.0f, ImGui::CalcTextSize(ICON_FA_CARET_DOWN).x + 12.0f);
            const ImVec2 caretPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##recopts", ImVec2(caretWidth, 54.0f)))
                ImGui::OpenPopup("##RecorderSettings");
            if (ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback((ImWchar)0xf0d7))
            {
                float x = std::round(caretPos.x + caretWidth * 0.5f - (glyph->X0 + glyph->X1) * 0.5f);
                float y = std::round(caretPos.y + 54.0f * 0.5f - (glyph->Y0 + glyph->Y1) * 0.5f);
                ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_CARET_DOWN);
            }
        if (ImGui::BeginPopup("##RecorderSettings"))
            {
                auto& rs = Recorder::Settings();
                ImGui::TextUnformatted(ICON_FA_VIDEO "  Recorder");
                ImGui::Separator();
                UI::SetLabelRatio(0.45f);
                ImGui::BeginDisabled(recording);
                const char* formats[] = { "MP4 (MPEG-4)", "WebM (VP9)", "GIF", "PNG Sequence" };
                UI::ComboEnum("Format", rs.Format, formats, 4);
                const int fpsValues[] = { 24, 30, 60, 120 };
                const char* fpsNames[] = { "24", "30", "60", "120" };
                int fpsIndex = 1;
                for (int i = 0; i < 4; ++i) if (fpsValues[i] == rs.FrameRate) fpsIndex = i;
                if (UI::Combo("Frame Rate", fpsIndex, fpsNames, 4))
                    rs.FrameRate = fpsValues[fpsIndex];
                UI::SliderFloat("Quality", rs.Quality, 0.0f, 1.0f);
                UI::SliderFloat("Resolution Scale", rs.ResolutionScale, 0.25f, 1.0f);
                UI::DragFloat("Max Duration (s)", rs.MaxDuration, 0.5f, 0.0f, 3600.0f, "%.0f", "0 = unlimited");
                UI::Checkbox("Start With Play", rs.StartWithPlay);
                UI::Checkbox("Stop With Play", rs.StopWithPlay);
                UI::Checkbox("Fixed Time Step", rs.FixedTimeStep);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Offline render (like Unreal's Movie Render Queue): the game advances exactly one video frame per\n"
                                      "rendered frame, so the video is smooth even if rendering is slower than real time.\n"
                                      "Sound does not follow (use it for cinematics and timelines).");
                ImGui::EndDisabled();
                UI::SetLabelRatio(0.40f);
                if (!Recorder::IsFFmpegAvailable() && rs.Format != RecordingFormat::PNGSequence)
                    UI::InfoBox("ffmpeg was not found in PATH: recordings fall back to PNG sequences.", UI::InfoType::Warning);
                if (recording)
                    ImGui::Text("Recording: %.1fs, %u frames", Recorder::GetElapsedSeconds(), Recorder::GetFrameCount());
                if (!Recorder::GetLastOutput().empty())
                {
                    ImGui::TextDisabled("Last: %s", Recorder::GetLastOutput().c_str());
                    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Show in Folder"))
                    {
                        std::filesystem::path out(Recorder::GetLastOutput());
                        std::string dir = (std::filesystem::is_directory(out) ? out : out.parent_path()).string();
#ifdef CQ_PLATFORM_LINUX
                        OpenURL(dir);
#elif defined(CQ_PLATFORM_WINDOWS)
                        system(("explorer \"" + dir + "\"").c_str());
#else
                        system(("open \"" + dir + "\"").c_str());
#endif
                    }
                }
                if (!Recorder::GetLastError().empty())
                    ImGui::TextColored(UI::Colors::Warning, "%s", Recorder::GetLastError().c_str());
                ImGui::EndPopup();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::RenderPanels()
    {
        if (m_ShowHierarchy && m_HierarchyPanel)
        {
            m_HierarchyPanel->SetSceneName(m_CurrentScenePath.empty() ? std::string("Untitled") : m_CurrentScenePath.stem().string());
            m_HierarchyPanel->OnImGuiRender();
        }

        if (m_ShowInspector && m_InspectorPanel)
            m_InspectorPanel->OnImGuiRender();

        if (m_ShowLighting && m_LightingPanel)
            m_LightingPanel->OnImGuiRender(&m_ShowLighting);
        if (m_OcclusionPanel)
        {
            m_OcclusionPanel->SetContext(m_ActiveScene.get());
            m_OcclusionPanel->Tick();
            if (m_ShowOcclusion)
                m_OcclusionPanel->OnImGuiRender(&m_ShowOcclusion);
        }

        if (m_ShowAudioMixer && m_AudioMixerPanel)
            m_AudioMixerPanel->OnImGuiRender(&m_ShowAudioMixer);
        if (m_MultiplayerPanel && m_ShowMultiplayer)
            m_MultiplayerPanel->OnImGuiRender(&m_ShowMultiplayer, !EditorState::Get().IsEditing());
        if (m_TimelinePanel)
        {
            if (m_ShowTimeline)
                m_TimelinePanel->OnImGuiRender(&m_ShowTimeline, !EditorState::Get().IsEditing());
            else
                m_TimelinePanel->StopPreview();   // pencere kapanınca önizlenen nesneler geri yüklenir
        }
        if (m_AnimationWindow)
        {
            if (m_ShowAnimation)
                m_AnimationWindow->OnImGuiRender(&m_ShowAnimation, !EditorState::Get().IsEditing());
            else
                m_AnimationWindow->StopPreview();   // Unity: pencere kapanınca canlandırılan değerler geri döner
        }
        if (m_BuildSettingsPanel && (m_ShowBuildSettings || BuildPipeline::Get().IsRunning()))
        {
            m_ShowBuildSettings = true;
            m_BuildSettingsPanel->OnImGuiRender(&m_ShowBuildSettings);
        }
        if (m_ProfilerPanel)
        {
            if (m_ShowProfiler)
                m_ProfilerPanel->OnImGuiRender(&m_ShowProfiler);
            // Unity gibi: yalnızca pencere açıkken kare kaydı (sonraki kareden itibaren)
            m_ProfilerPanel->UpdateRecordingState(m_ShowProfiler);
        }

        if (m_ShowSpriteEditor && m_SpriteEditorPanel)
            m_SpriteEditorPanel->OnImGuiRender(&m_ShowSpriteEditor);

        if (m_ShowTerrain && m_TerrainPanel)
            m_TerrainPanel->OnImGuiRender(&m_ShowTerrain);

        if (m_ShowTilePalette && m_TilePalettePanel)
            m_TilePalettePanel->OnImGuiRender(&m_ShowTilePalette);

        if (m_ShowAddonManager && m_AddonManagerPanel)
            m_AddonManagerPanel->OnImGuiRender(&m_ShowAddonManager);
        if (m_ImportActivityPanel)
            m_ImportActivityPanel->OnImGuiRender(&m_ShowImportActivity);
        AddonHost::Get().DrawPanels();

        if (m_ShowProjectSettings && m_ProjectSettingsPanel)
            m_ProjectSettingsPanel->OnImGuiRender();

        if (m_ShowContentBrowser && m_ContentBrowserPanel)
            m_ContentBrowserPanel->OnImGuiRender();

        if (m_ShowLog && m_LogPanel)
            m_LogPanel->OnImGuiRender(&m_ShowLog);

        if (m_ShowConsole && m_ConsolePanel)
            m_ConsolePanel->OnImGuiRender(&m_ShowConsole);

        // Stats artık Scene/Game görünümlerinde sabit, katlanabilir katman olarak çiziliyor


        if (m_AnimatorPanel)
            m_AnimatorPanel->OnImGuiRender();

        if (m_ShowShaderGraph && m_ShaderEditorPanel)
            m_ShaderEditorPanel->OnImGuiRender();
        ObjectPicker::Draw();

        if (m_CodeEditorPanel && m_CodeEditorPanel->GetIsOpen())
        {
            m_CodeEditorPanel->OnImGuiRender();
        }

        if (m_VisualScriptPanel && m_VisualScriptPanel->GetIsOpen())
            m_VisualScriptPanel->OnImGuiRender();
        if (m_NodeAssetPanel && m_NodeAssetPanel->GetIsOpen())
            m_NodeAssetPanel->OnImGuiRender();
        if (m_TestRunnerPanel && m_TestRunnerPanel->GetIsOpen())
            m_TestRunnerPanel->OnImGuiRender();

        if (m_InputActionsEditor)
            m_InputActionsEditor->OnImGuiRender();

        if (m_ShowAIPanel && m_AIPanel)
        {
            m_AIPanel->SetContext(m_ActiveScene);
            m_AIPanel->OnImGuiRender(&m_ShowAIPanel);
        }

        if (m_EditorScene)
            m_EditorScene->SetShowNavMesh(m_ShowNavigation);
        if (m_ShowNavigation)
        {
            ImGui::Begin("Navigation", &m_ShowNavigation);
            ImGui::TextWrapped("Bakes the walkable area of the geometry inside the NavMesh Surfaces (agent radius, height, max slope and step height "
                               "are set on the surface). Modifiers, Modifier Volumes and Links are applied. Surfaces with Bake On Play bake again "
                               "automatically when the scene changed.");

            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(ICON_FA_MAP " Bake NavMesh##BakeNavMesh", ImVec2(-1, 40)) && m_EditorScene)
                NavAgents::Bake(*m_EditorScene);
            if (ImGui::Button(ICON_FA_TRASH_CAN " Clear##ClearNavMesh", ImVec2(-1, 0)) && m_EditorScene)
                m_EditorScene->GetNavMesh().Clear();

            ImGui::Spacing();
            if (m_EditorScene)
            {
                const auto& nav = m_EditorScene->GetNavMesh();
                size_t walkable = 0;
                for (size_t i = 0; i < nav.GetCellCount(); ++i)
                    walkable += nav.GetCell((int)i).Area != NavArea_NotWalkable;
                ImGui::Text("Walkable cells: %zu", walkable);
                ImGui::Text("Grid: %d x %d (cell %.3f m)", nav.GetWidth(), nav.GetDepth(), nav.GetSettings().CellSize);
                ImGui::Text("Links: %zu", nav.GetLinks().size());
                ImGui::TextDisabled("The baked area is drawn in blue in the Scene view while this window is open.");
            }

            ImGui::End();
        }

        if (m_ShowGame && m_GamePanel)
            m_GamePanel->OnImGuiRender();
        else if (m_GamePanel)
            m_GamePanel->MarkHidden();

        if (m_ShowViewport && m_ViewportPanel)
            m_ViewportPanel->OnImGuiRender();

        ImGui::End(); // DockSpace
    }

    void EditorLayer::OnPlayButtonPressed()
    {
        auto& editorState = EditorState::Get();
        
        if (editorState.IsEditing())
        {
            Visual::Debugger::ClearHits();
            // Zaman çizelgesi önizlemesi oyun kopyasına taşınmasın
            if (m_TimelinePanel)
                m_TimelinePanel->StopPreview();
            if (m_AnimationWindow)
                m_AnimationWindow->StopPreview();
            if (m_LogPanel)
                m_LogPanel->OnPlay();
            // Sprite Atlas: kaynakları değişen atlaslar Play'den önce yeniden paketlenir (Unity gibi)
            SpriteAtlases::PackAllIfStale();
            // Editor scene'i direkt memory'den klonla (YAML yok, disk I/O yok)
            uint64_t selectedUUID = GetSelectedEntityUUID();
            m_RuntimeScene = std::make_shared<Scene>();
            m_RuntimeScene->CloneSceneFrom(m_EditorScene);
            m_RuntimeScene->OnRuntimeStart();
            
            // Active scene'i runtime'a çevir
            m_ActiveScene = m_RuntimeScene;
            SceneManager::Reset();
            SceneManager::SetActiveScenePath(m_CurrentScenePath);
            
            // Tüm panelleri yeni (çalışma zamanı) sahneye bağla; seçim klondaki aynı varlığa taşınır
            RefreshAllPanels();
            RestoreSelectionByUUID(selectedUUID);

            // Unity gibi: Play oyun görünümünü öne getirir (klavye odağı oyuna gider); Stop sahne görünümüne döner
            m_ReturnToViewportOnStop = m_ViewportPanel && m_ViewportPanel->IsTabVisible();
            m_PendingFocusWindow = "Game";

            if (Recorder::Settings().StartWithPlay && !Recorder::IsRecording() && m_GamePanel)
            {
                glm::vec2 size = m_GamePanel->GetViewportSize();
                Recorder::Start((uint32_t)size.x, (uint32_t)size.y);
            }

            editorState.SetSceneState(SceneState::Play);
        }
        else if (editorState.IsPaused())
        {
            editorState.SetSceneState(SceneState::Play);
        }
    }

    void EditorLayer::LoadRuntimeScene(const std::filesystem::path& path)
    {
        // Oyun içi sahne geçişi: editör sahnesi değişmez, Stop'ta ona dönülür
        auto scene = std::make_shared<Scene>();
        SceneSerializer serializer(scene);
        if (!serializer.Deserialize(path))
        {
            CQ_CORE_ERROR("LoadScene: could not load {0}", path.string());
            return;
        }
        SelectEntityEverywhere({});
        if (m_RuntimeScene)
        {
            m_RuntimeScene->OnRuntimeStop(false);
            m_RuntimeScene->StopAllAudio();
        }
        m_RuntimeScene = scene;
        m_ActiveScene = scene;
        RefreshAllPanels();
        scene->OnRuntimeStart(false);
        SceneManager::SetActiveScenePath(path);
        CQ_CORE_INFO("Loaded scene {0}", path.filename().string());
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
        if (Scripting::TestRunner::HasQueuedPlayMode())
            Scripting::TestRunner::CancelPlayMode();
        auto& editorState = EditorState::Get();
        
        if (!editorState.IsEditing())
        {
            // Seçili entity'nin UUID'sini kaydet (çalışma zamanı sahnesi silinmeden önce)
            uint64_t selectedUUID = GetSelectedEntityUUID();
            
            // Runtime scene'i temizle
            if (m_RuntimeScene)
            {
                m_RuntimeScene->OnRuntimeStop();
                m_RuntimeScene->StopAllAudio();
            }
            m_RuntimeScene.reset();
            // Oyunun kilitlediği imleci editöre geri ver
            Input::SetCursorMode(0);
            
            // Editor scene audio'yu da durdur
            if (m_EditorScene)
                m_EditorScene->StopAllAudio();
            
            // Editor scene'e geri dön
            m_ActiveScene = m_EditorScene;
            
            // Tüm panelleri editör sahnesine geri bağla
            RefreshAllPanels();
            if (m_ReturnToViewportOnStop)
                m_PendingFocusWindow = "Viewport";
            m_ReturnToViewportOnStop = false;

            if (Recorder::IsRecording() && Recorder::Settings().StopWithPlay)
                Recorder::Stop();

            // Seçili entity'yi UUID ile yeniden bul
            RestoreSelectionByUUID(selectedUUID);
            
            editorState.SetSceneState(SceneState::Edit);
        }
    }

    void EditorLayer::FileMenu()
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                RequestNewScene();
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

            if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B"))
            {
                m_ShowBuildSettings = true;
                m_PendingFocusWindow = "###BuildSettings";
            }
            if (ImGui::MenuItem("Build And Run", nullptr, false, m_BuildSettingsPanel && !BuildPipeline::Get().IsRunning()))
            {
                m_ShowBuildSettings = true;
                std::string err;
                if (!m_BuildSettingsPanel->StartBuild(true, &err))
                    CQ_CORE_ERROR("Build: {0}", err);
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
            Entity created = HierarchyPanel::DrawCreateMenuItems(m_ActiveScene.get(), Entity{});
            if (created)
                SelectEntityEverywhere(created);

            // Addon prefab'ları
            bool anyInsert = false;
            for (auto& inst : AddonHost::Get().GetInstances())
                anyInsert |= !inst->Manifest.Inserts.empty();
            if (anyInsert && ImGui::BeginMenu(ICON_FA_PUZZLE_PIECE "  Addons"))
            {
                for (auto& inst : AddonHost::Get().GetInstances())
                {
                    const auto& m = inst->Manifest;
                    if (m.Inserts.empty() || !ImGui::BeginMenu(m.Name.c_str()))
                        continue;
                    for (auto& ins : m.Inserts)
                    {
                        std::string label = std::string(UI::IconFromName(ins.Icon)) + "  " + ins.Name;
                        if (ImGui::MenuItem(label.c_str()))
                            if (Entity e = SpawnAddonInsert(m, ins)) SelectEntityEverywhere(e);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            Entity selected = EditorState::Get().GetSelectedEntity();
            if (ImGui::MenuItem("Frame Selected", "F", false, (bool)selected) && m_ViewportPanel)
                m_ViewportPanel->FrameSelected();
            if (ImGui::BeginMenu("Create Child", (bool)selected))
            {
                Entity child = HierarchyPanel::DrawCreateMenuItems(m_ActiveScene.get(), selected);
                if (child)
                    SelectEntityEverywhere(child);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }

    void EditorLayer::FeedGameInput()
    {
        // Klavye yalnızca oyun görünümü odaktayken, yeni tıklamalar fare üzerindeyken oyuna gider (Unity gibi)
        const bool playing = !EditorState::Get().IsEditing();
        GameInput::FeedFromImGui(playing && m_GamePanel && (m_GamePanel->IsViewportFocused() || m_GameMouseCaptured),
                                 playing && m_GamePanel && (m_GamePanel->IsViewportHovered() || m_GameMouseCaptured));
    }

    void EditorLayer::AddonsMenu()
    {
        if (ImGui::BeginMenu("Addons"))
        {
            AddonHost::Get().DrawMenuItems();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_PUZZLE_PIECE "  Addon Manager...", nullptr, m_ShowAddonManager))
            {
                m_ShowAddonManager = true;
                m_PendingFocusWindow = "###AddonManager";
            }
            if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Reload Addons"))
                AddonHost::Get().ReloadAll();
            ImGui::EndMenu();
        }
    }

    Entity EditorLayer::SpawnAddonInsert(const AddonManifest& addon, const AddonInsertItem& item)
    {
        if (!m_ActiveScene)
            return {};
        glm::vec3 at = m_ViewportPanel ? m_ViewportPanel->GetEditorCamera().GetFocalPoint() : glm::vec3(0.0f);
        Entity created;
        auto action = [&]() { created = AssetActions::SpawnFromAsset(m_ActiveScene.get(), (addon.Directory / item.Prefab).string(), {}, &at); };
        if (auto& undo = m_AddonUndoable)
            undo(addon.Name + ": " + item.Name, action);
        else
            action();
        return created;
    }

    void EditorLayer::SetupAddonHost()
    {
        AddonHost::Hooks hooks;
        hooks.GetScene = [this]() { return m_ActiveScene; };
        hooks.GetSelection = []() { return EditorState::Get().GetSelectedEntities(); };
        hooks.SetSelection = [this](const std::vector<Entity>& sel) {
            if (sel.empty()) { SelectEntityEverywhere({}); return; }
            SelectEntityEverywhere(sel[0]);
            if (sel.size() > 1)
            {
                EditorState::Get().SelectMultipleEntities(sel);
                UpdateHierarchySelection();
            }
        };
        hooks.GetCameraTarget = [this]() { return m_ViewportPanel ? m_ViewportPanel->GetEditorCamera().GetFocalPoint() : glm::vec3(0.0f); };
        hooks.GetCameraPosition = [this]() { return m_ViewportPanel ? m_ViewportPanel->GetEditorCamera().GetPosition() : glm::vec3(0.0f); };
        hooks.Focus = [this](Entity e) {
            SelectEntityEverywhere(e);
            if (m_ViewportPanel) m_ViewportPanel->FrameSelected();
        };
        hooks.IsPlaying = []() { return !EditorState::Get().IsEditing(); };
        hooks.ToolsDockId = [this]() { return m_ToolsDockId; };
        m_AddonUndoable = [this](const std::string& label, const std::function<void()>& action) {
            std::shared_ptr<Scene> scene = m_ActiveScene;
            if (!scene || !EditorState::Get().IsEditing())
            {
                action();
                return;
            }
            std::string before = SceneSnapshotCommand::Capture(scene);
            action();
            if (scene != m_ActiveScene)
                return;
            std::string after = SceneSnapshotCommand::Capture(scene);
            if (after == before)
                return;
            UndoRedoManager::Get().Execute(std::make_shared<SceneSnapshotCommand>(scene, std::move(before), std::move(after), label, [this]() {
                SelectEntityEverywhere({});
                RefreshAllPanels();
            }));
        };
        hooks.Undoable = m_AddonUndoable;
        AddonHost::Get().SetHooks(std::move(hooks));
        if (m_AddonManagerPanel)
            m_AddonManagerPanel->OpenFile = [this](const std::filesystem::path& path) {
                if (m_CodeEditorPanel)
                {
                    m_CodeEditorPanel->OpenFile(path);
                    m_CodeEditorPanel->GetIsOpen() = true;
                }
            };
    }

    void EditorLayer::WindowMenu()
    {
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
            ImGui::MenuItem("Lighting", nullptr, &m_ShowLighting);
            ImGui::MenuItem("Occlusion Culling", nullptr, &m_ShowOcclusion);
            ImGui::MenuItem("Audio Mixer", nullptr, &m_ShowAudioMixer);
            ImGui::MenuItem("Profiler", "Ctrl+7", &m_ShowProfiler);
            ImGui::MenuItem("Multiplayer", nullptr, &m_ShowMultiplayer);
            ImGui::MenuItem("Timeline", nullptr, &m_ShowTimeline);
            ImGui::MenuItem("Animation", "Ctrl+6", &m_ShowAnimation);
            ImGui::MenuItem("Sprite Editor", nullptr, &m_ShowSpriteEditor);
            ImGui::MenuItem("Terrain", nullptr, &m_ShowTerrain);
            ImGui::MenuItem("Tile Palette", nullptr, &m_ShowTilePalette);
            ImGui::MenuItem("Addon Manager", nullptr, &m_ShowAddonManager);
            ImGui::MenuItem("Import Activity", nullptr, &m_ShowImportActivity);
            ImGui::MenuItem("Project Settings", nullptr, &m_ShowProjectSettings);
            ImGui::MenuItem("Log", nullptr, &m_ShowLog);
            ImGui::MenuItem("Console (Terminal)", nullptr, &m_ShowConsole);
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContentBrowser);
            if (ImGui::MenuItem("Animator"))
            {
                if (m_AnimatorPanel) m_AnimatorPanel->SetOpen(true);
            }
            ImGui::MenuItem("Shader Graph", nullptr, &m_ShowShaderGraph);
            ImGui::MenuItem("Navigation", nullptr, &m_ShowNavigation);
            if (m_CodeEditorPanel)
                ImGui::MenuItem("Code Editor", nullptr, &m_CodeEditorPanel->GetIsOpen());
            if (m_VisualScriptPanel)
                ImGui::MenuItem("Visual Script", nullptr, &m_VisualScriptPanel->GetIsOpen());
            if (m_NodeAssetPanel)
                ImGui::MenuItem("Graph Editor", nullptr, &m_NodeAssetPanel->GetIsOpen());
            if (m_TestRunnerPanel)
                ImGui::MenuItem("Test Runner", nullptr, &m_TestRunnerPanel->GetIsOpen());
            ImGui::MenuItem("AI Assistant", nullptr, &m_ShowAIPanel);
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
            ImGui::OpenPopup("About Conqueror's Engine");
            m_ShowAbout = false;
        }

        DrawRecoveryPopup();
        DrawUnsavedChangesPopup();
        DrawNewSceneDialog();
        DrawMessagePopup();

        // Konum her kare BeginPopupModal'dan hemen önce verilir: araya giren kapalı popup'lar
        // (kurtarma / kaydedilmemiş değişiklik) SetNextWindow* verisini tüketiyordu ve pencere üstte açılıyordu
        // Genişlik GPU satırına göre (uzun sürücü adları taşmasın), ekranın %90'ını geçmez
        {
            const std::string gfx = std::string(RendererAPI::GetBackendName()) + "  |  " + RendererAPI::GetDeviceDescription();
            const float maxW = ImGui::GetMainViewport()->Size.x * 0.9f;
            const float w = std::clamp(ImGui::CalcTextSize(gfx.c_str()).x + ImGui::GetStyle().WindowPadding.x * 2.0f + 8.0f, 500.0f, std::max(500.0f, maxW));
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
        }
        if (ImGui::BeginPopupModal("About Conqueror's Engine", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize("Conqueror's Engine").x) * 0.5f);
            ImGui::Text("Conqueror's Engine");
            {
                char version[64];
                std::snprintf(version, sizeof(version), "Version %s", EngineVersion);
                ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(version).x) * 0.5f);
                ImGui::TextDisabled("%s", version);
                const std::string gfx = std::string(RendererAPI::GetBackendName()) + "  |  " + RendererAPI::GetDeviceDescription();
                ImGui::SetCursorPosX(std::max(8.0f, (ImGui::GetWindowSize().x - ImGui::CalcTextSize(gfx.c_str()).x) * 0.5f));
                ImGui::TextDisabled("%s", gfx.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", gfx.c_str());
            }
            ImGui::Separator();
            ImGui::TextWrapped("Hi, I'm Fazli. I started developing Conqueror's Engine when I was 14, and now at 15 I've decided to publish it under my own company, TurcoLabs. I hope you like it. Thank you very much <3");
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

            ImGui::Spacing();
            {
                const char* license = "\xC2\xA9 2026 TurcoLabs  |  Mozilla Public License 2.0";
                ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(license).x) * 0.5f);
                ImGui::TextDisabled("%s", license);
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
            const bool running = BuildPipeline::Get().IsRunning();
            if (ImGui::MenuItem(ICON_FA_BOXES_PACKING "  Build Settings...", "Ctrl+Shift+B"))
            {
                m_ShowBuildSettings = true;
                m_PendingFocusWindow = "###BuildSettings";
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_HAMMER "  Build", nullptr, false, !running && m_BuildSettingsPanel))
            {
                m_ShowBuildSettings = true;
                std::string err;
                if (!m_BuildSettingsPanel->StartBuild(false, &err))
                    CQ_CORE_ERROR("Build: {0}", err);
            }
            if (ImGui::MenuItem(ICON_FA_PLAY "  Build And Run", nullptr, false, !running && m_BuildSettingsPanel))
            {
                m_ShowBuildSettings = true;
                std::string err;
                if (!m_BuildSettingsPanel->StartBuild(true, &err))
                    CQ_CORE_ERROR("Build: {0}", err);
            }
            if (running)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Building... %d%%", (int)(BuildPipeline::Get().GetProgress() * 100.0f));
            }
            ImGui::EndMenu();
        }
    }

    void EditorLayer::NewScene()
    {
        m_ActiveScene = std::make_shared<Scene>();
        m_EditorScene = m_ActiveScene;
        m_CurrentScenePath.clear();
        
        // Panelleri güncelle; önceki sahneye ait seçim bırakılır
        RefreshAllPanels();
        SelectEntityEverywhere({});

        CQ_CORE_INFO("New scene created");
    }

    void EditorLayer::OpenScene()
    {
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Scene Files", "cqscene" } };
        nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
        if (result == NFD_OKAY)
        {
            std::filesystem::path path = outPath;
            NFD_FreePathU8(outPath);
            RequestOpenScene(path);
        }
    }

    bool EditorLayer::OpenScene(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
        {
            CQ_CORE_ERROR("Scene not found: {0}", path.string());
            return false;
        }

        // Oynatma sırasında sahne değişirse önce durdur
        if (!EditorState::Get().IsEditing())
            OnStopButtonPressed();

        auto scene = std::make_shared<Scene>();
        SceneSerializer serializer(scene);
        if (m_ViewportPanel)
            serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
        if (!serializer.Deserialize(path.string()))
        {
            CQ_CORE_ERROR("Failed to open scene: {0}", path.string());
            return false;
        }

        m_ActiveScene = scene;
        m_EditorScene = m_ActiveScene;
        m_CurrentScenePath = path;
        m_AutoSaveTimer = 0.0f;
        if (m_ViewportPanel)
            m_ViewportPanel->GetPostProcessStack().ResetHistory();
        EditorState::Get().ClearSelection();
        RefreshAllPanels();
        SelectEntityEverywhere({});
        CQ_CORE_INFO("Scene opened: {0}", path.string());
        CheckRecoverySnapshot(path);
        return true;
    }

    void EditorLayer::SaveScene()
    {
        // Oyun modunda sahne kopyası çalışır: kaydetmek çalışma zamanı durumunu sahne dosyasına yazardı (Unity gibi engelle)
        if (!EditorState::Get().IsEditing())
        {
            CQ_CORE_WARN("Stop Play mode before saving the scene (changes made while playing are not saved).");
            return;
        }
        // Animation penceresi önizlemesi: dosyaya özgün değerler yazılır, önizleme sonra sürer
        struct PreviewGuard
        {
            AnimationWindow* W;
            explicit PreviewGuard(AnimationWindow* w) : W(w) { if (W) W->BeginSceneSave(); }
            ~PreviewGuard() { if (W) W->EndSceneSave(); }
        } previewGuard(m_AnimationWindow.get());
        if (m_CurrentScenePath.empty())
        {
            SaveSceneAs();
        }
        else
        {
            SceneSerializer serializer(m_EditorScene);
            
            // Viewport kamerasını serializer'a ver
            if (m_ViewportPanel)
            {
                serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
            }
            
            serializer.Serialize(m_CurrentScenePath);
            CaptureSceneBaseline();
            RemoveRecoverySnapshot(m_CurrentScenePath);
            m_AutoSaveTimer = 0.0f;
            CQ_CORE_INFO("Scene saved: {0}", m_CurrentScenePath.string());
            WriteThumbnail();
        }
    }

    std::filesystem::path EditorLayer::GetRecoveryPath(const std::filesystem::path& scenePath) const
    {
        auto projectDir = Project::GetActiveProjectDirectory();
        if (projectDir.empty() || scenePath.empty())
            return {};
        // Aynı adlı sahneler çakışmasın: göreli yol klasörleri '_' ile birleştirilir
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(scenePath, projectDir, ec);
        std::string key = ec || rel.empty() ? scenePath.filename().string() : rel.generic_string();
        for (char& c : key)
            if (c == '/' || c == '\\' || c == ':') c = '_';
        return projectDir / "Library" / "Recovery" / key;
    }

    void EditorLayer::WriteRecoverySnapshot()
    {
        if (!m_EditorScene || m_CurrentScenePath.empty())
            return;
        const auto recovery = GetRecoveryPath(m_CurrentScenePath);
        if (recovery.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(recovery.parent_path(), ec);
        SceneSerializer serializer(m_EditorScene);
        if (m_ViewportPanel)
            serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
        serializer.Serialize(recovery.string());
        // Kaydedilmemiş değişiklik yoksa kopyayı tutma
        auto read = [](const std::filesystem::path& p) {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        if (read(recovery) == read(m_CurrentScenePath))
            std::filesystem::remove(recovery, ec);
        else
            CQ_CORE_INFO("Auto-save: unsaved changes of {0} written to {1}", m_CurrentScenePath.filename().string(), recovery.string());
    }

    void EditorLayer::RemoveRecoverySnapshot(const std::filesystem::path& scenePath)
    {
        const auto recovery = GetRecoveryPath(scenePath);
        std::error_code ec;
        if (!recovery.empty())
            std::filesystem::remove(recovery, ec);
    }

    // Ek açılmış sahnenin nesnelerini kendi .cqscene dosyasına yaz (ortam ayarları dosyadakilerle kalır)
    bool EditorLayer::SaveStreamedLevel(Entity root)
    {
        if (!root || !root.HasComponent<StreamedLevelComponent>())
            return false;
        const std::string file = SceneSerializer::ResolveAssetPath(root.GetComponent<StreamedLevelComponent>().Source);
        auto temp = std::make_shared<Scene>();
        {
            SceneSerializer loader(temp);
            if (!loader.Deserialize(file))
            {
                CQ_CORE_ERROR("Save Level: cannot read {0}", file);
                return false;
            }
        }
        temp->m_Registry.clear();
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        std::vector<Entity> copies;
        for (Entity child : root.GetChildren())
            copies.push_back(temp->DuplicateEntityTree(child, "", Entity{}, &uuidMap));
        for (Entity c : copies)
            temp->RemapEntityReferences(c, uuidMap);
        temp->UpdateWorldTransforms();
        SceneSerializer(temp).Serialize(file);
        LevelStreaming::ClearCache();
        CQ_CORE_INFO("Saved level {0} ({1} root objects)", file, copies.size());
        return true;
    }

    namespace
    {
        // Sahne dosyasının karşılaştırılacak metni: sahne görünümü kamerası atlanır, Hierarchy çiftleri ebeveyne göre sıralanır
        std::string NormalizeSceneFile(const std::filesystem::path& p)
        {
            std::ifstream f(p, std::ios::binary);
            std::string out, line;
            bool skipping = false, hierarchy = false;
            // Hierarchy bölümü: (çocuk, ebeveyn) çiftleri ebeveyne göre (kardeş sırası korunarak) sıralanır; eski dosyalar
            // çiftleri karma tablo sırasıyla yazıyordu, ilişkiler aynıyken sıra farkı değişiklik sayılmasın
            std::vector<std::pair<std::string, std::string>> pairs;   // (ebeveyn, çocuk)
            auto flushPairs = [&]() {
                std::stable_sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& [parent, child] : pairs)
                    out += "  - Child: " + child + "\n    Parent: " + parent + "\n";
                pairs.clear();
            };
            std::string pendingChild;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '-')
                {
                    if (hierarchy)
                        flushPairs();
                    // Sahne görünümü kamerası değişiklik sayılmaz
                    skipping = line.rfind("EditorCamera:", 0) == 0;
                    hierarchy = line.rfind("Hierarchy:", 0) == 0;
                }
                if (skipping)
                    continue;
                if (hierarchy)
                {
                    if (const size_t c = line.find("- Child: "); c != std::string::npos)
                        pendingChild = line.substr(c + 9);
                    else if (const size_t q = line.find("Parent: "); q != std::string::npos)
                        pairs.push_back({ line.substr(q + 8), pendingChild });
                    else
                        out += line + "\n";
                    continue;
                }
                out += line + "\n";
            }
            if (hierarchy)
                flushPairs();
            return out;
        }
    }

    std::string EditorLayer::NormalizedSceneText()
    {
        std::error_code ec;
        const auto projectDir = Project::GetActiveProjectDirectory();
        const std::filesystem::path probe = (projectDir.empty() ? std::filesystem::temp_directory_path(ec) : projectDir / "Library") / ".unsaved_check.cqscene";
        std::filesystem::create_directories(probe.parent_path(), ec);
        // Oyun modunda da düzenleyici sahnesi karşılaştırılır (oyun kopyası değil)
        SceneSerializer serializer(m_EditorScene);
        serializer.Serialize(probe);
        std::string text = NormalizeSceneFile(probe);
        std::filesystem::remove(probe, ec);
        return text;
    }

    void EditorLayer::CaptureSceneBaseline()
    {
        m_BaselineScene = m_EditorScene.get();
        m_BaselinePath = m_CurrentScenePath;
        m_SceneBaselineDelay = 0;
        m_SceneBaseline = (m_EditorScene && !m_CurrentScenePath.empty()) ? NormalizedSceneText() : std::string();
    }

    bool EditorLayer::HasUnsavedChanges()
    {
        if (!m_EditorScene)
            return false;
        // Adsız sahne: yalnızca kullanıcı bir şey düzenlediyse
        if (m_CurrentScenePath.empty())
            return UndoRedoManager::Get().CanUndo();
        std::error_code ec;
        if (!std::filesystem::exists(m_CurrentScenePath, ec))
            return true;
        // Sahne yeni açıldı: temel birkaç kare içinde alınacak, bu arada düzenleme olamaz
        if (m_SceneBaselineDelay > 0)
            return false;
        // Karşılaştırma açılıştaki / son kayıttaki sahneyle yapılır, diskteki dosyayla değil: eski sürümün yazdığı dosyada olmayan
        // yeni alanlar, otomatik taşınan eski bileşenler (Model -> Mesh Renderer) ve betik alanlarının farklı sırası hiçbir şey
        // değiştirilmese de fark çıkarıyor, çıkarken "Don't Save" sorusu geliyordu
        const std::string now = NormalizedSceneText();
        return now != (m_SceneBaseline.empty() ? NormalizeSceneFile(m_CurrentScenePath) : m_SceneBaseline);
    }

    void EditorLayer::RequestNewScene()
    {
        // Unity File > New Scene: şablon seçme penceresi (otomasyonda doğrudan yerleşik Basic)
        if (Automation::IsActive())
        {
            CreateNewScene("builtin:basic");
            return;
        }
        m_OpenNewSceneDialog = true;
    }

    void EditorLayer::ConfirmNewScene(const std::string& choice)
    {
        if (!Automation::IsActive() && HasUnsavedChanges())
        {
            m_PendingNewSceneChoice = choice;
            m_PendingAction = PendingSceneAction::NewScene;
            m_OpenUnsavedPopup = true;
            return;
        }
        CreateNewScene(choice);
    }

    bool EditorLayer::CreateNewScene(const std::string& choice)
    {
        if (!EditorState::Get().IsEditing())
            OnStopButtonPressed();
        if (choice == "builtin:empty")
        {
            NewScene();
            return true;
        }
        if (choice.rfind("builtin:", 0) == 0)
        {
            NewScene();
            ApplyNewSceneTemplate();
            return true;
        }
        // Proje şablonu: şablon sahnesinin kopyası, adsız (ilk kayıtta Save As)
        SceneTemplates::SceneTemplate t;
        const std::filesystem::path scenePath = SceneTemplates::Load(choice, t) ? SceneTemplates::Resolve(t.ScenePath) : std::filesystem::path();
        std::error_code ec;
        if (scenePath.empty() || !std::filesystem::exists(scenePath, ec))
        {
            CQ_CORE_ERROR("Scene template '{0}' has no valid scene; created a Basic scene instead.", choice);
            NewScene();
            ApplyNewSceneTemplate();
            return false;
        }
        auto scene = std::make_shared<Scene>();
        SceneSerializer serializer(scene);
        if (m_ViewportPanel)
            serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
        if (!serializer.Deserialize(scenePath.string()))
        {
            CQ_CORE_ERROR("Scene template scene could not be loaded: {0}", scenePath.string());
            return false;
        }
        m_ActiveScene = scene;
        m_EditorScene = m_ActiveScene;
        m_CurrentScenePath.clear();
        EditorState::Get().ClearSelection();
        RefreshAllPanels();
        SelectEntityEverywhere({});
        CQ_CORE_INFO("New scene from template '{0}'", t.Name);
        return true;
    }

    // EditorState::ShowMessage iletisi (Unity'nin "Can't add script" gibi uyarı pencereleri)
    void EditorLayer::DrawMessagePopup()
    {
        static std::string s_Title, s_Text;
        if (std::string title, text; EditorState::Get().TakeMessage(title, text))
        {
            s_Title = title;
            s_Text = text;
            ImGui::OpenPopup("###EditorMessage");
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal((s_Title + "###EditorMessage").c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
            return;
        ImGui::TextColored(UI::Colors::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", s_Text.c_str());
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 110.0f);
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void EditorLayer::DrawNewSceneDialog()
    {
        struct Item { std::string Choice, Title, Description, Thumb; bool Pinned = false; bool BuiltIn = false; };
        static std::vector<Item> items;
        static std::map<std::string, std::shared_ptr<Texture2D>>* thumbs = new std::map<std::string, std::shared_ptr<Texture2D>>();
        if (m_OpenNewSceneDialog)
        {
            m_OpenNewSceneDialog = false;
            items.clear();
            items.push_back({ "builtin:basic", "Basic (Built-in)", "A scene with a Main Camera and a Directional Light (2D projects: an orthographic camera).", "", false, true });
            items.push_back({ "builtin:empty", "Empty (Built-in)", "A completely empty scene.", "", false, true });
            for (const auto& f : SceneTemplates::FindAll())
            {
                SceneTemplates::SceneTemplate t;
                if (SceneTemplates::Load(f, t))
                    items.push_back({ f.string(), t.Name, t.Description, SceneTemplates::Resolve(t.ThumbnailPath).string(), t.Pinned, false });
            }
            std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Pinned && !b.Pinned; });
            if (std::none_of(items.begin(), items.end(), [this](const Item& i) { return i.Choice == m_NewSceneChoice; }))
                m_NewSceneChoice = "builtin:basic";
            ImGui::OpenPopup("New Scene");
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f, 460.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_NoSavedSettings))
            return;
        const float footer = ImGui::GetFrameHeightWithSpacing() + 8.0f;
        ImGui::BeginChild("##templates", ImVec2(260.0f, -footer), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("Scene Templates in Project");
        ImGui::Separator();
        const Item* selected = nullptr;
        bool create = false;
        for (const auto& it : items)
        {
            const bool sel = it.Choice == m_NewSceneChoice;
            std::string label = std::string(it.Pinned ? ICON_FA_THUMBTACK "  " : ICON_FA_CLAPPERBOARD "  ") + it.Title + "##" + it.Choice;
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_NewSceneChoice = it.Choice;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    create = true;
            }
            if (it.Choice == m_NewSceneChoice)
                selected = &it;
        }
        if (items.size() == 2)
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Create > Scene Template in the Content Browser (or right-click a scene > Create Scene Template From Scene) to add your own.");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##details", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
        if (selected)
        {
            std::error_code ec;
            if (!selected->Thumb.empty() && std::filesystem::exists(selected->Thumb, ec))
            {
                auto& tex = (*thumbs)[selected->Thumb];
                if (!tex)
                    tex = Texture2D::Create(selected->Thumb);
                if (tex && tex->GetRendererID() && tex->GetWidth() > 0)
                {
                    const float w = ImGui::GetContentRegionAvail().x;
                    const float h = std::min(w * (float)tex->GetHeight() / (float)tex->GetWidth(), 260.0f);
                    ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(), ImVec2(h * (float)tex->GetWidth() / (float)tex->GetHeight(), h),
                                 ImVec2(0, 1), ImVec2(1, 0));
                }
            }
            else
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 200.0f), IM_COL32(40, 42, 48, 255), 4.0f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + w * 0.5f - 20.0f, p.y + 80.0f));
                ImGui::SetWindowFontScale(2.5f);
                ImGui::TextDisabled(ICON_FA_CLAPPERBOARD);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 208.0f));
            }
            ImGui::Spacing();
            ImGui::TextUnformatted(selected->Title.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", selected->Description.empty() ? "No description." : selected->Description.c_str());
            if (!selected->BuiltIn)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", SceneTemplates::ToProjectRelative(selected->Choice).c_str());
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2.0f * 120.0f - 2.0f * ImGui::GetStyle().ItemSpacing.x - 8.0f);
        if (ImGui::Button("Create", ImVec2(120, 0)) || create || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        {
            const std::string choice = m_NewSceneChoice;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ConfirmNewScene(choice);
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Project Settings > Editor / Graphics: yeni sahne şablonu (Unity: Main Camera + Directional Light)
    void EditorLayer::ApplyNewSceneTemplate()
    {
        if (!m_ActiveScene)
            return;
        const auto& editor = ProjectSettings::Editor();
        const auto& graphics = ProjectSettings::Graphics();
        const bool is2D = editor.DefaultBehaviorMode == 1;
        auto& rs = m_ActiveScene->GetRenderSettings();
        rs.Post.Tonemap = (Tonemapper)std::clamp(graphics.DefaultTonemapper, 0, 4);
        rs.Environment.Sky = graphics.DefaultSky == 1 ? SkyMode::SolidColor : SkyMode::Auto;
        if (editor.NewSceneWithCameraAndLight)
        {
            Entity camera = m_ActiveScene->CreateEntity("Main Camera");
            auto& cc = camera.AddComponent<CameraComponent>();
            cc.Primary = true;
            camera.AddComponent<AudioListenerComponent>();
            auto& tc = camera.GetComponent<TransformComponent>();
            if (is2D)
            {
                cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
                cc.Camera.SetOrthographicSize(10.0f);
                tc.Position = { 0.0f, 0.0f, 0.0f };
            }
            else
            {
                tc.Position = { 0.0f, 1.0f, 10.0f };
                Entity light = m_ActiveScene->CreateEntity("Directional Light");
                auto& dl = light.AddComponent<DirectionalLightComponent>();
                dl.Direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
                dl.Intensity = 1.0f;
                light.GetComponent<TransformComponent>().Position = { 0.0f, 3.0f, 0.0f };
            }
        }
        if (m_ViewportPanel)
            m_ViewportPanel->GetEditorCamera().SetCameraMode(is2D ? EditorCameraMode::Orthographic2D : EditorCameraMode::Perspective3D);
        RefreshAllPanels();
    }

    void EditorLayer::RequestOpenScene(const std::filesystem::path& path)
    {
        if (!Automation::IsActive() && path != m_CurrentScenePath && HasUnsavedChanges())
        {
            m_PendingAction = PendingSceneAction::OpenScene;
            m_PendingOpenScene = path;
            m_OpenUnsavedPopup = true;
            return;
        }
        OpenScene(path);
    }

    bool EditorLayer::OnCloseRequested()
    {
        if (m_CloseConfirmed || Automation::IsActive() || !HasUnsavedChanges())
            return true;
        m_PendingAction = PendingSceneAction::Close;
        m_OpenUnsavedPopup = true;
        return false;
    }

    void EditorLayer::DrawUnsavedChangesPopup()
    {
        if (m_OpenUnsavedPopup)
        {
            ImGui::OpenPopup("Unsaved Changes");
            m_OpenUnsavedPopup = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const std::string name = m_CurrentScenePath.empty() ? std::string("Untitled") : m_CurrentScenePath.filename().string();
        ImGui::Text("Do you want to save the changes you made to %s?", name.c_str());
        ImGui::TextDisabled("Your changes will be lost if you don't save them.");
        ImGui::Spacing();

        auto proceed = [this]() {
            const PendingSceneAction action = m_PendingAction;
            const std::filesystem::path pending = m_PendingOpenScene;
            m_PendingAction = PendingSceneAction::None;
            m_PendingOpenScene.clear();
            if (action == PendingSceneAction::Close)
            {
                m_CloseConfirmed = true;
                Application::Get().Close(true);
                return;
            }
            if (!EditorState::Get().IsEditing())
                OnStopButtonPressed();
            if (action == PendingSceneAction::NewScene)
                CreateNewScene(m_PendingNewSceneChoice);
            else if (action == PendingSceneAction::OpenScene)
                OpenScene(pending);
        };

        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            if (!EditorState::Get().IsEditing())
                OnStopButtonPressed();
            SaveScene();
            // "Farklı Kaydet" iptal edildiyse devam etme
            if (!HasUnsavedChanges())
                proceed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(120, 0)))
        {
            if (!m_CurrentScenePath.empty())
                RemoveRecoverySnapshot(m_CurrentScenePath);
            proceed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_PendingAction = PendingSceneAction::None;
            m_PendingOpenScene.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void EditorLayer::CheckRecoverySnapshot(const std::filesystem::path& scenePath)
    {
        const auto recovery = GetRecoveryPath(scenePath);
        std::error_code ec;
        if (recovery.empty() || !std::filesystem::exists(recovery, ec))
            return;
        // Yalnızca sahne dosyasından daha yeni kopyalar (önceki oturum kaydetmeden çöktü)
        auto recoveryTime = std::filesystem::last_write_time(recovery, ec);
        auto sceneTime = std::filesystem::last_write_time(scenePath, ec);
        if (ec || recoveryTime <= sceneTime)
        {
            std::filesystem::remove(recovery, ec);
            return;
        }
        m_RecoveryCandidate = recovery;
        m_OpenRecoveryPopup = true;
        CQ_CORE_WARN("Unsaved changes from a previous session were found for {0}", scenePath.filename().string());
    }

    void EditorLayer::DrawRecoveryPopup()
    {
        if (m_OpenRecoveryPopup)
        {
            ImGui::OpenPopup("Recover Unsaved Changes");
            m_OpenRecoveryPopup = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Recover Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("The editor closed unexpectedly while this scene had unsaved changes.");
            ImGui::Text("Scene: %s", m_CurrentScenePath.filename().string().c_str());
            ImGui::TextDisabled("Restore loads the auto-saved copy; save the scene afterwards to keep it.");
            ImGui::Spacing();
            if (ImGui::Button("Restore", ImVec2(140, 0)))
            {
                auto scene = std::make_shared<Scene>();
                SceneSerializer serializer(scene);
                if (m_ViewportPanel)
                    serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
                if (EditorState::Get().IsEditing() && serializer.Deserialize(m_RecoveryCandidate.string()))
                {
                    m_ActiveScene = scene;
                    m_EditorScene = scene;
                    // Geri yüklenen kopya kaydedilmemiş sayılır: temel, dosyadan açılan sahneninki olarak kalır
                    m_BaselineScene = scene.get();
                    m_SceneBaselineDelay = 0;
                    EditorState::Get().ClearSelection();
                    RefreshAllPanels();
                    SelectEntityEverywhere({});
                    CQ_CORE_INFO("Recovered unsaved changes from {0}", m_RecoveryCandidate.string());
                }
                else
                    CQ_CORE_ERROR("Could not restore {0}", m_RecoveryCandidate.string());
                m_RecoveryCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(140, 0)))
            {
                std::error_code ec;
                std::filesystem::remove(m_RecoveryCandidate, ec);
                m_RecoveryCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorLayer::WriteThumbnail()
    {
        // Hub proje kartı için: oyun kamerasının son karesi (yoksa sahne görünümü)
        auto projectDir = Project::GetActiveProjectDirectory();
        if (projectDir.empty())
            return;
        std::shared_ptr<Framebuffer> fb;
        if (m_GamePanel && m_GamePanel->HasCamera() && m_GamePanel->GetViewportSize().x > 1.0f && m_GamePanel->GetViewportSize().y > 1.0f)
            fb = m_GamePanel->GetFramebuffer();
        else if (m_ViewportPanel && m_ViewportPanel->GetViewportSize().x > 1.0f && m_ViewportPanel->GetViewportSize().y > 1.0f)
            fb = m_ViewportPanel->GetFramebuffer();
        if (!fb)
            return;
        const auto& spec = fb->GetSpecification();
        WriteProjectThumbnail(fb->GetColorAttachmentRendererID(0), spec.Width, spec.Height, projectDir / "Library" / "thumbnail.png");
    }

    void EditorLayer::SaveSceneAs()
    {
        if (!EditorState::Get().IsEditing())
        {
            CQ_CORE_WARN("Stop Play mode before saving the scene (changes made while playing are not saved).");
            return;
        }
        struct PreviewGuard
        {
            AnimationWindow* W;
            explicit PreviewGuard(AnimationWindow* w) : W(w) { if (W) W->BeginSceneSave(); }
            ~PreviewGuard() { if (W) W->EndSceneSave(); }
        } previewGuard(m_AnimationWindow.get());
        nfdu8char_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Scene Files", "cqscene" } };
        nfdresult_t result = NFD_SaveDialogU8(&outPath, filters, 1, nullptr, "Scene.cqscene");
        
        if (result == NFD_OKAY)
        {
            m_CurrentScenePath = outPath;
            
            SceneSerializer serializer(m_EditorScene);
            
            // Viewport kamerasını serializer'a ver
            if (m_ViewportPanel)
            {
                serializer.SetEditorCamera(&m_ViewportPanel->GetEditorCamera());
            }
            
            serializer.Serialize(m_CurrentScenePath);
            CaptureSceneBaseline();
            WriteThumbnail();
            
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

    void EditorLayer::SelectEntityEverywhere(Entity entity)
    {
        if (entity)
            EditorState::Get().SelectEntity(entity);
        else
            EditorState::Get().ClearSelection();
        if (m_HierarchyPanel) m_HierarchyPanel->SetSelectedEntity(entity);
        if (m_InspectorPanel) m_InspectorPanel->SetSelectedEntity(entity);
        if (m_ViewportPanel) m_ViewportPanel->SetSelectedEntity(entity);
        if (m_AnimatorPanel) m_AnimatorPanel->SetSelectedEntity(entity);
        if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetSelectedEntity(entity);
    }

    bool* EditorLayer::FindPanelVisibilityFlag(const std::string& name)
    {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (n == "hierarchy") return &m_ShowHierarchy;
        if (n == "inspector") return &m_ShowInspector;
        if (n == "log") return &m_ShowLog;
        if (n == "console" || n == "terminal") return &m_ShowConsole;
        if (n == "contentbrowser" || n == "content browser" || n == "assets") return &m_ShowContentBrowser;
        if (n == "viewport" || n == "scene") return &m_ShowViewport;
        if (n == "game") return &m_ShowGame;
        if (n == "lighting") return &m_ShowLighting;
        if (n == "occlusion" || n == "occlusionculling" || n == "occlusion culling") return &m_ShowOcclusion;
        if (n == "audiomixer" || n == "audio mixer" || n == "mixer") return &m_ShowAudioMixer;
        if (n == "profiler") return &m_ShowProfiler;
        if (n == "build" || n == "buildsettings" || n == "build settings") return &m_ShowBuildSettings;
        if (n == "multiplayer" || n == "network") return &m_ShowMultiplayer;
        if (n == "timeline") return &m_ShowTimeline;
        if (n == "animation") return &m_ShowAnimation;
        if (n == "spriteeditor" || n == "sprite editor") return &m_ShowSpriteEditor;
        if (n == "terrain") return &m_ShowTerrain;
        if (n == "tilepalette" || n == "tile palette" || n == "tiles") return &m_ShowTilePalette;
        if (n == "addons" || n == "addonmanager" || n == "addon manager") return &m_ShowAddonManager;
        if (n == "projectsettings" || n == "project settings") return &m_ShowProjectSettings;
        if (n == "stats") return &m_ShowStats;
        if (n == "shader" || n == "shadergraph") return &m_ShowShaderGraph;
        if (n == "navigation") return &m_ShowNavigation;
        if (n == "ai") return &m_ShowAIPanel;
        return nullptr;
    }

    void EditorLayer::RegisterAutomationCommands()
    {
        using Args = Automation::CommandArgs;

        auto EditorEntityName = [](Entity e) { return e.GetComponent<TagComponent>().Tag; };
        Automation::RegisterCommand("select", [this, EditorEntityName](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ActiveScene) { err = "select <entity name> [more names...] | select --all <name> | select --none"; return false; }
            if (a[1] == "--none") { SelectEntityEverywhere({}); return true; }
            // Çoklu seçim: "select A B C" ya da aynı adlı tüm nesneler "select --all Box"
            if (a.size() > 2)
            {
                std::vector<Entity> list;
                const bool all = a[1] == "--all";
                for (size_t i = all ? 2 : 1; i < a.size(); ++i)
                    m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) {
                        if (tag.Tag == a[i] && (all || std::none_of(list.begin(), list.end(), [&](Entity e) { return EditorEntityName(e) == a[i]; })))
                            list.push_back(Entity{ id, m_ActiveScene.get() });
                    });
                if (list.empty()) { err = "no entities matched"; return false; }
                SelectEntityEverywhere(list.front());
                if (list.size() > 1)
                {
                    EditorState::Get().SelectMultipleEntities(list);
                    UpdateHierarchySelection();
                }
                return true;
            }
            Entity found{};
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) {
                if (!found && tag.Tag == a[1])
                    found = Entity{ id, m_ActiveScene.get() };
            });
            if (!found) { err = "entity '" + a[1] + "' not found"; return false; }
            SelectEntityEverywhere(found);
            return true;
        });
        Automation::RegisterCommand("play", [this](const Args&, std::string&) { OnPlayButtonPressed(); return true; });
        Automation::RegisterCommand("shading", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ViewportPanel) { err = "shading <shaded|wireframe|unlit|normals|lighting|metalrough|ao|pathtracing>"; return false; }
            static const char* names[] = { "shaded", "wireframe", "unlit", "normals", "lighting", "metalrough", "ao", "pathtracing" };
            static_assert(sizeof(names) / sizeof(names[0]) == (size_t)ViewportShading::Count, "shading names");
            for (int i = 0; i < (int)ViewportShading::Count; ++i)
                if (a[1] == names[i]) { m_ViewportPanel->SetShading((ViewportShading)i); return true; }
            err = "unknown shading mode " + a[1];
            return false;
        });
        // pathtracer expect_samples <n|>n|<n> | reset | bounces <n> | max <n> | spp <n>
        Automation::RegisterCommand("pathtracer", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "pathtracer expect_samples|reset|bounces|max|spp"; return false; }
            auto& st = PathTracer::GetSettings();
            if (a[1] == "reset") { PathTracer::Reset(); return true; }
            if (a.size() < 3) { err = "missing value"; return false; }
            if (a[1] == "bounces") { st.MaxBounces = std::stoi(a[2]); return true; }
            if (a[1] == "max") { st.MaxSamples = std::stoi(a[2]); return true; }
            if (a[1] == "spp") { st.SamplesPerFrame = std::stoi(a[2]); return true; }
            if (a[1] == "expect_samples")
            {
                const int n = PathTracer::GetSampleCount();
                Automation::Report("PATHTRACER samples = " + std::to_string(n) + ", triangles = " + std::to_string(PathTracer::GetTriangleCount()));
                const bool ok = a[2][0] == '>' ? n > std::stoi(a[2].substr(1)) : a[2][0] == '<' ? n < std::stoi(a[2].substr(1)) : n == std::stoi(a[2]);
                if (!ok) { err = "samples " + std::to_string(n); return false; }
                return true;
            }
            err = "unknown pathtracer command " + a[1];
            return false;
        });
        Automation::RegisterCommand("viewport_drag2d", [this](const Args& a, std::string& err) {
            // viewport_drag2d <wx0> <wy0> <wx1> <wy1>: 2D görünümde dünya koordinatlarıyla sürükleme
            if (a.size() < 5 || !m_ViewportPanel) { err = "viewport_drag2d x0 y0 x1 y1"; return false; }
            const glm::vec2 p0 = m_ViewportPanel->WorldToScreen2D({ (float)std::atof(a[1].c_str()), (float)std::atof(a[2].c_str()) });
            const glm::vec2 p1 = m_ViewportPanel->WorldToScreen2D({ (float)std::atof(a[3].c_str()), (float)std::atof(a[4].c_str()) });
            Automation::QueueMouseDrag(p0.x, p0.y, p1.x, p1.y, 16);
            return true;
        });
        Automation::RegisterCommand("viewport_click", [this](const Args& a, std::string& err) {
            if (a.size() < 3 || !m_ViewportPanel) { err = "viewport_click <nx 0..1> <ny 0..1>"; return false; }
            const glm::vec2* b = m_ViewportPanel->GetViewportBounds();
            ImVec2 origin = ImGui::GetMainViewport()->Pos;
            float x = b[0].x - origin.x + (b[1].x - b[0].x) * (float)std::atof(a[1].c_str());
            float y = b[0].y - origin.y + (b[1].y - b[0].y) * (float)std::atof(a[2].c_str());
            return Automation::ExecuteCommandLine("click " + std::to_string((int)x) + " " + std::to_string((int)y), err);
        });
        Automation::RegisterCommand("mixer_tone", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "mixer_tone <group> [freq] [seconds]"; return false; }
            auto& mixer = AudioMixer::Get();
            if (!mixer.FindGroup(a[1])) { err = "no mixer group " + a[1]; return false; }
            mixer.PlayTestTone(a[1], a.size() > 2 ? (float)std::atof(a[2].c_str()) : 440.0f, a.size() > 3 ? (float)std::atof(a[3].c_str()) : 1.0f);
            return true;
        });
        Automation::RegisterCommand("mixer_set", [](const Args& a, std::string& err) {
            // mixer_set <group> volume|mute|solo|effect <value>
            if (a.size() < 4) { err = "mixer_set <group> <volume|mute|solo|effect> <value>"; return false; }
            auto& mixer = AudioMixer::Get();
            MixerGroup* g = mixer.FindGroup(a[1]);
            if (!g) { err = "no mixer group " + a[1]; return false; }
            if (a[2] == "volume") g->VolumeDb = (float)std::atof(a[3].c_str());
            else if (a[2] == "mute") g->Mute = a[3] == "on" || a[3] == "1";
            else if (a[2] == "solo") g->Solo = a[3] == "on" || a[3] == "1";
            else if (a[2] == "effect")
            {
                for (int t = 0; t < (int)MixerEffectType::Count; ++t)
                    if (a[3] == MixerEffect::GetTypeName((MixerEffectType)t))
                    {
                        g->Effects.push_back(MixerEffect::CreateDefault((MixerEffectType)t));
                        mixer.RebuildChain(a[1]);
                        return true;
                    }
                err = "unknown effect " + a[3];
                return false;
            }
            else { err = "unknown property " + a[2]; return false; }
            return true;
        });
        // audio expect_clip <varlık> <dosya soneki> | expect_plays <varlık> <en az n>  (Audio Random Container)
        Automation::RegisterCommand("audio", [this](const Args& a, std::string& err) {
            if (a.size() < 4 || !m_ActiveScene) { err = "audio expect_clip|expect_plays <entity> <value>"; return false; }
            Entity e{};
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) {
                if (!e && tag.Tag == a[2]) e = Entity{ id, m_ActiveScene.get() };
            });
            if (!e || !e.HasComponent<AudioSourceComponent>()) { err = "no audio source on " + a[2]; return false; }
            const auto& s = e.GetComponent<AudioSourceComponent>();
            Automation::Report("AUDIO " + a[2] + " clip=" + s.ContainerClip + " plays=" + std::to_string(s.ContainerPlays));
            if (a[1] == "expect_clip")
            {
                const bool ok = s.ContainerClip.size() >= a[3].size() && s.ContainerClip.compare(s.ContainerClip.size() - a[3].size(), a[3].size(), a[3]) == 0;
                if (!ok) { err = "clip is " + s.ContainerClip; return false; }
                return true;
            }
            if (a[1] == "expect_plays")
            {
                if (s.ContainerPlays < std::atoi(a[3].c_str())) { err = "plays " + std::to_string(s.ContainerPlays); return false; }
                return true;
            }
            err = "unknown audio command";
            return false;
        });
        Automation::RegisterCommand("expect_meter", [](const Args& a, std::string& err) {
            // expect_meter <group> <above|below> <dB>
            if (a.size() < 4) { err = "expect_meter <group> <above|below> <dB>"; return false; }
            MixerGroup* g = AudioMixer::Get().FindGroup(a[1]);
            if (!g) { err = "no mixer group " + a[1]; return false; }
            float db = AudioMixer::LinearToDb(std::max(g->RT->DisplayPeakL, g->RT->DisplayPeakR));
            float want = (float)std::atof(a[3].c_str());
            bool ok = a[2] == "above" ? db > want : db < want;
            Automation::Report("METER " + a[1] + " " + std::to_string(db) + " dB");
            if (!ok) { err = "meter " + a[1] + " = " + std::to_string(db) + " dB, expected " + a[2] + " " + a[3]; return false; }
            return true;
        });
        Automation::RegisterCommand("sprite_editor", [this](const Args& a, std::string& err) {
            // sprite_editor open <path> | slice grid <w> <h> | slice count <c> <r> | slice auto | apply
            if (a.size() < 2 || !m_SpriteEditorPanel) { err = "sprite_editor open <path> | slice ... | apply"; return false; }
            auto& se = *m_SpriteEditorPanel;
            if (a[1] == "open" && a.size() > 2) { m_ShowSpriteEditor = true; if (!se.Open(a[2])) { err = "cannot open " + a[2]; return false; } return true; }
            if (a[1] == "slice" && a.size() > 2)
            {
                if (a[2] == "auto") se.SliceAutomatic();
                else if (a[2] == "grid" && a.size() > 4) se.SliceGridBySize({ std::atoi(a[3].c_str()), std::atoi(a[4].c_str()) });
                else if (a[2] == "count" && a.size() > 4) se.SliceGridByCount({ std::atoi(a[3].c_str()), std::atoi(a[4].c_str()) });
                else { err = "slice auto | grid <w> <h> | count <cols> <rows>"; return false; }
                Automation::Report("SPRITES " + std::to_string(se.GetSheet().Sprites.size()));
                return true;
            }
            if (a[1] == "apply") return se.Apply();
            if (a[1] == "expect_count" && a.size() > 2)
            {
                int want = std::atoi(a[2].c_str());
                if ((int)se.GetSheet().Sprites.size() != want) { err = "sprite count " + std::to_string(se.GetSheet().Sprites.size()) + " != " + a[2]; return false; }
                return true;
            }
            auto F = [&](size_t i) { return (float)std::atof(a[i].c_str()); };
            using EM = SpriteEditorPanel::EditorMode;
            using ST = SpriteEditorPanel::SkinTool;
            if (a[1] == "mode" && a.size() > 2)
            {
                static const std::map<std::string, EM> modes = { { "sprite", EM::Sprite }, { "outline", EM::Outline }, { "physics", EM::Physics }, { "secondary", EM::Secondary }, { "skin", EM::Skin } };
                auto it = modes.find(a[2]);
                if (it == modes.end()) { err = "mode sprite|outline|physics|secondary|skin"; return false; }
                se.SetMode(it->second);
                return true;
            }
            if (a[1] == "select" && a.size() > 2) { se.SelectSprite(std::atoi(a[2].c_str())); return true; }
            if (a[1] == "generate") { se.GenerateShapes(a.size() > 2 ? F(2) : 0.5f); return true; }
            if ((a[1] == "click" && a.size() > 3) || (a[1] == "drag" && a.size() > 5))
            {
                const glm::vec2 p0 = se.CanvasPoint({ F(2), F(3) });
                if (a[1] == "click") { Automation::QueueMouseClick(p0.x, p0.y); return true; }
                const glm::vec2 p1 = se.CanvasPoint({ F(4), F(5) });
                Automation::QueueMouseDrag(p0.x, p0.y, p1.x, p1.y, 16);
                return true;
            }
            if (a[1] == "expect_shapes" && a.size() > 2)
            {
                const int selected = se.GetSelectedSprite();
                if (selected < 0 || selected >= (int)se.GetSheet().Sprites.size()) { err = "no selected sprite"; return false; }
                const auto& def = se.GetSheet().Sprites[(size_t)selected];
                const auto& shapes = se.GetMode() == EM::Physics ? def.PhysicsShapes : def.Outlines;
                size_t points = 0;
                for (const auto& shape : shapes) points += shape.size();
                Automation::Report("SHAPES " + std::to_string(shapes.size()) + " POINTS " + std::to_string(points));
                if ((int)shapes.size() < std::atoi(a[2].c_str())) { err = "shapes " + std::to_string(shapes.size()) + " < " + a[2]; return false; }
                if (a.size() > 3 && (int)points != std::atoi(a[3].c_str())) { err = "points " + std::to_string(points) + " != " + a[3]; return false; }
                return true;
            }
            if (a[1] == "secondary" && a.size() > 3) { se.AddSecondaryTexture(a[2], a[3]); return true; }
            if (a[1] == "tool" && a.size() > 2)
            {
                static const std::map<std::string, ST> tools = { { "pose", ST::PreviewPose }, { "edit_bone", ST::EditBone }, { "create_bone", ST::CreateBone }, { "split_bone", ST::SplitBone },
                    { "auto_geometry", ST::AutoGeometry }, { "edit_geometry", ST::EditGeometry }, { "create_vertex", ST::CreateVertex },
                    { "auto_weights", ST::AutoWeights }, { "weight_slider", ST::WeightSlider }, { "weight_brush", ST::WeightBrush } };
                auto it = tools.find(a[2]);
                if (it == tools.end()) { err = "unknown skin tool"; return false; }
                se.SetSkinTool(it->second);
                return true;
            }
            if (a[1] == "bone" && a.size() > 5) { se.SkinAddBone({ F(2), F(3) }, { F(4), F(5) }, a.size() > 6 ? std::atoi(a[6].c_str()) : -1); return true; }
            if (a[1] == "auto_geometry") { se.SkinAutoGeometry(a.size() > 2 ? std::atoi(a[2].c_str()) : 12, 1); return true; }
            if (a[1] == "auto_weights") { se.SkinAutoWeights(); return true; }
            if (a[1] == "pose" && a.size() > 3) { se.SkinSetPose(std::atoi(a[2].c_str()), F(3)); return true; }
            if (a[1] == "expect_skin" && a.size() > 3)
            {
                const int selected = se.GetSelectedSprite();
                if (selected < 0 || selected >= (int)se.GetSheet().Sprites.size()) { err = "no selected sprite"; return false; }
                const auto& skin = se.GetSheet().Sprites[(size_t)selected].Skin;
                Automation::Report("SKIN bones " + std::to_string(skin.Bones.size()) + " vertices " + std::to_string(skin.Vertices.size()) + " triangles " + std::to_string(skin.Indices.size() / 3));
                if ((int)skin.Bones.size() != std::atoi(a[2].c_str())) { err = "bones " + std::to_string(skin.Bones.size()) + " != " + a[2]; return false; }
                if ((int)skin.Vertices.size() < std::atoi(a[3].c_str())) { err = "vertices " + std::to_string(skin.Vertices.size()) + " < " + a[3]; return false; }
                return true;
            }
            err = "unknown sprite_editor command";
            return false;
        });
        Automation::RegisterCommand("terrain", [this](const Args& a, std::string& err) {
            // terrain create [res] | generate [seed] | tool <raise|smooth|flatten|noise|paint|none> | stroke <x> <z> <seconds> [invert] | autopaint
            if (a.size() < 2 || !m_TerrainPanel) { err = "terrain create|generate|tool|stroke|autopaint|expect_height"; return false; }
            SyncPanelContexts();
            auto& tp = *m_TerrainPanel;
            if (a[1] == "create")
            {
                Entity e = TerrainPanel::CreateTerrain(m_ActiveScene.get(), a.size() > 2 ? std::atoi(a[2].c_str()) : 129);
                SelectEntityEverywhere(e);
                return (bool)e;
            }
            Entity e = tp.GetTarget();
            if (!e) { err = "no terrain"; return false; }
            auto& t = e.GetComponent<TerrainComponent>();
            if (a[1] == "generate")
            {
                if (a.size() > 2) tp.GetNoise().Seed = std::atoi(a[2].c_str());
                TerrainSystem::GenerateNoise(t, tp.GetNoise());
                TerrainSystem::AutoPaint(t);
                return true;
            }
            if (a[1] == "autopaint") { TerrainSystem::AutoPaint(t); return true; }
            if (a[1] == "tool" && a.size() > 2)
            {
                static const char* names[] = { "raise", "smooth", "flatten", "noise", "paint" };
                tp.SetTool(TerrainPanel::Tool::None);
                for (int i = 0; i < 5; ++i)
                    if (a[2] == names[i]) tp.SetTool((TerrainPanel::Tool)i);
                return true;
            }
            if (a[1] == "brush" && a.size() > 3)
            {
                tp.GetBrush().Radius = (float)std::atof(a[2].c_str());
                tp.GetBrush().Strength = (float)std::atof(a[3].c_str());
                if (a.size() > 4) tp.GetBrush().Layer = std::atoi(a[4].c_str());
                return true;
            }
            if (a[1] == "stroke" && a.size() > 4)
            {
                float secs = (float)std::atof(a[4].c_str());
                bool invert = a.size() > 5 && a[5] == "invert";
                for (float tt = 0.0f; tt < secs; tt += 1.0f / 60.0f)
                    tp.ApplyStroke(e, { (float)std::atof(a[2].c_str()), (float)std::atof(a[3].c_str()) }, 1.0f / 60.0f, invert);
                return true;
            }
            // terrain brush_asset <yol.cqbrush|none> | layer_asset <i> <yol.cqterrainlayer|none>
            //   | expect_layer <i> <tint r g b | tiling v | texture <son ek>> (varlıktan eşitlenmiş değer)
            if (a[1] == "brush_asset" && a.size() > 2)
            {
                tp.GetBrush().Brush = a[2] == "none" ? std::string() : SceneSerializer::ResolveAssetPath(a[2]);
                return true;
            }
            if (a[1] == "layer_asset" && a.size() > 3)
            {
                const int i = std::atoi(a[2].c_str());
                if (i < 0 || i >= t.LayerCount) { err = "no layer " + a[2]; return false; }
                t.Layers[(size_t)i].Asset = a[3] == "none" ? std::string() : SceneSerializer::ResolveAssetPath(a[3]);
                return true;
            }
            if (a[1] == "expect_layer" && a.size() > 4)
            {
                const int i = std::atoi(a[2].c_str());
                if (i < 0 || i >= t.LayerCount) { err = "no layer " + a[2]; return false; }
                TerrainSystem::UpdateGPU(t);   // varlıktan eşitle
                const auto& l = t.Layers[(size_t)i];
                if (a[3] == "tint" && a.size() > 6)
                {
                    const glm::vec3 want((float)std::atof(a[4].c_str()), (float)std::atof(a[5].c_str()), (float)std::atof(a[6].c_str()));
                    Automation::Report("TERRAIN layer " + a[2] + " tint=" + std::to_string(l.Tint.r) + "," + std::to_string(l.Tint.g) + "," + std::to_string(l.Tint.b));
                    if (glm::length(l.Tint - want) > 0.01f) { err = "tint differs"; return false; }
                    return true;
                }
                if (a[3] == "tiling")
                {
                    Automation::Report("TERRAIN layer " + a[2] + " tiling=" + std::to_string(l.Tiling));
                    if (std::fabs(l.Tiling - (float)std::atof(a[4].c_str())) > 0.01f) { err = "tiling differs"; return false; }
                    return true;
                }
                if (a[3] == "texture")
                {
                    Automation::Report("TERRAIN layer " + a[2] + " texture=" + l.TexturePath);
                    if (l.TexturePath.size() < a[4].size() || l.TexturePath.compare(l.TexturePath.size() - a[4].size(), a[4].size(), a[4]) != 0)
                    { err = "texture " + l.TexturePath; return false; }
                    return true;
                }
            }
            if (a[1] == "expect_height" && a.size() > 5)
            {
                float h = TerrainSystem::GetHeightLocal(t, (float)std::atof(a[2].c_str()), (float)std::atof(a[3].c_str()));
                float want = (float)std::atof(a[5].c_str());
                Automation::Report("TERRAIN height=" + std::to_string(h));
                bool ok = a[4] == "above" ? h > want : h < want;
                if (!ok) { err = "height " + std::to_string(h) + " not " + a[4] + " " + a[5]; return false; }
                return true;
            }
            err = "unknown terrain command";
            return false;
        });
        Automation::RegisterCommand("tile", [this](const Args& a, std::string& err) {
            // tile create <rect|hex|hexflat|iso> [xy|xz] [3d] | tool <paint|box|fill|erase|picker|none> | brush <size> [tile]
            //      | apply <x> <y> [<x2> <y2>] [invert] | set <x> <y> <tile> | expect <x> <y> <tile|-1> | expect_count <n>
            //      | expect_selected <tile> | expect_hover <x> <y> | expect_path <x0> <y0> <x1> <y1> <cells|none>
            //      | expect_distance <x0> <y0> <x1> <y1> <d> | drag <x0> <y0> <x1> <y1> [steps] | click <x> <y> | hover <x> <y>
            //      | undo | redo
            if (a.size() < 2 || !m_TilePalettePanel) { err = "tile create|tool|brush|apply|set|expect|expect_count|expect_path|drag|click|undo|redo"; return false; }
            SyncPanelContexts();
            auto& tp = *m_TilePalettePanel;
            auto I = [&](size_t i) { return i < a.size() ? std::atoi(a[i].c_str()) : 0; };
            if (a[1] == "create")
            {
                const std::string kind = a.size() > 2 ? a[2] : "rect";
                GridLayout layout = kind == "hex" ? GridLayout::HexagonPointyTop : kind == "hexflat" ? GridLayout::HexagonFlatTop
                                  : kind == "iso" ? GridLayout::Isometric : GridLayout::Rectangle;
                GridPlane plane = a.size() > 3 && a[3] == "xz" ? GridPlane::XZ : GridPlane::XY;
                Entity e = TilePalettePanel::CreateTilemap(m_ActiveScene.get(), layout, plane, a.size() > 4 && a[4] == "3d");
                m_ShowTilePalette = true;
                SelectEntityEverywhere(e);
                return (bool)e;
            }
            // tile palette <path.cqpalette> | pick_asset <i> (palet varlığının i. karosunu seç; haritaya ekler)
            if (a[1] == "palette" && a.size() > 2)
            {
                std::string p = a[2];
                for (size_t i = 3; i < a.size(); ++i) p += " " + a[i];
                tp.SetPaletteAsset(p);
                m_ShowTilePalette = true;
                return true;
            }
            if (a[1] == "pick_asset")
            {
                if (!tp.SelectPaletteAssetTile(I(2))) { err = "cannot pick palette tile " + a[2]; return false; }
                return true;
            }
            Entity e = tp.GetTarget();
            if (!e) { err = "no tilemap"; return false; }
            auto& map = e.GetComponent<TilemapComponent>();
            const GridComponent& g = GridSystem::GridOf(e);
            // tile expect_texture <x> <y> <dosya-adı-soneki> [rotation]: Rule / Animated Tile çözümlemesi
            if (a[1] == "expect_texture" && a.size() > 4)
            {
                GridSystem::SyncTileAssets(map);
                int rot = 0;
                const std::string tex = GridSystem::ResolveTileTexture(map, { I(2), I(3) }, rot, ImGui::GetTime());
                Automation::Report("TILE " + a[2] + "," + a[3] + " texture=" + tex + " rot=" + std::to_string(rot));
                const bool texOk = tex.size() >= a[4].size() && tex.compare(tex.size() - a[4].size(), a[4].size(), a[4]) == 0;
                const bool rotOk = a.size() < 6 || std::atoi(a[5].c_str()) == rot;
                if (!texOk || !rotOk) { err = "texture " + tex + " rot " + std::to_string(rot); return false; }
                return true;
            }
            // Hücre merkezinin viewport ekran konumu
            auto cellScreen = [&](int x, int y, ImVec2& out) {
                if (!m_ViewportPanel) return false;
                const glm::vec2* b = m_ViewportPanel->GetViewportBounds();
                glm::vec2 size = b[1] - b[0];
                if (size.x <= 0.0f || size.y <= 0.0f) return false;
                glm::vec4 c = m_ViewportPanel->GetEditorCamera().GetViewProjection() * glm::vec4(GridSystem::CellToWorld(e, { x, y }), 1.0f);
                if (c.w <= 1e-4f) return false;
                glm::vec3 ndc = glm::vec3(c) / c.w;
                out = ImVec2(b[0].x + (ndc.x * 0.5f + 0.5f) * size.x, b[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
                return true;
            };
            if (a[1] == "tool" && a.size() > 2)
            {
                static const char* names[] = { "paint", "box", "fill", "erase", "picker" };
                tp.SetTool(TilePalettePanel::Tool::None);
                for (int i = 0; i < 5; ++i)
                    if (a[2] == names[i]) tp.SetTool((TilePalettePanel::Tool)i);
                return true;
            }
            if (a[1] == "brush" && a.size() > 2)
            {
                tp.SetBrushSize(I(2));
                if (a.size() > 3) tp.SetSelectedTile(I(3));
                return true;
            }
            if (a[1] == "apply" && a.size() > 3)
            {
                const bool invert = a.back() == "invert";
                glm::ivec2 p0(I(2), I(3));
                glm::ivec2 p1 = a.size() > 5 && a[4] != "invert" ? glm::ivec2(I(4), I(5)) : p0;
                tp.ApplyTool(e, p0, p1, invert);
                return true;
            }
            if (a[1] == "set" && a.size() > 4)
            {
                GridSystem::SetTile(map, { I(2), I(3) }, I(4));
                return true;
            }
            // tile palette <i> autotile <0|1|2> [terrain] | palette <i> texture <path> | template <1|2> <path> | expect_autotile <x> <y> <index>
            if (a[1] == "palette" && a.size() > 4)
            {
                const int i = I(2);
                if (i < 0 || i >= (int)map.Palette.size()) { err = "no palette entry " + a[2]; return false; }
                auto& def = map.Palette[(size_t)i];
                if (a[3] == "autotile") { def.Autotile = I(4); if (a.size() > 5) def.Terrain = I(5); }
                else if (a[3] == "texture") def.Texture = a[4];
                else { err = "palette <i> autotile|texture"; return false; }
                map.Version++;
                return true;
            }
            if (a[1] == "template" && a.size() > 3)
            {
                const std::string path = Project::GetActiveProjectDirectory().empty() ? a[3] : (Project::GetActiveProjectDirectory() / a[3]).string();
                if (!GridSystem::WriteAutotileTemplate(I(2), 32, path)) { err = "template write failed"; return false; }
                Texture2D::ReloadFromDisk(path);
                return true;
            }
            if (a[1] == "expect_autotile" && a.size() > 4)
            {
                const int have = GridSystem::AutotileIndex(map, { I(2), I(3) });
                if (have != I(4)) { err = "autotile (" + a[2] + "," + a[3] + ") = " + std::to_string(have) + ", expected " + a[4]; return false; }
                return true;
            }
            if (a[1] == "expect" && a.size() > 4)
            {
                int t = GridSystem::GetTile(map, { I(2), I(3) });
                if (t != I(4)) { err = "tile (" + a[2] + "," + a[3] + ") = " + std::to_string(t) + ", expected " + a[4]; return false; }
                return true;
            }
            if (a[1] == "expect_count" && a.size() > 2)
            {
                if ((int)map.Cells.size() != I(2)) { err = "tile count " + std::to_string(map.Cells.size()) + " != " + a[2]; return false; }
                return true;
            }
            if (a[1] == "expect_selected" && a.size() > 2)
            {
                if (tp.GetSelectedTile() != I(2)) { err = "selected tile " + std::to_string(tp.GetSelectedTile()) + " != " + a[2]; return false; }
                return true;
            }
            if (a[1] == "expect_hover" && a.size() > 3)
            {
                glm::ivec2 c;
                if (!tp.GetHoverCell(c)) { err = "no hover cell"; return false; }
                if (c != glm::ivec2(I(2), I(3))) { err = "hover cell " + std::to_string(c.x) + "," + std::to_string(c.y) + " != " + a[2] + "," + a[3]; return false; }
                return true;
            }
            if (a[1] == "expect_path" && a.size() > 6)
            {
                auto path = GridSystem::FindPath(g, map, { I(2), I(3) }, { I(4), I(5) });
                std::string cells;
                for (auto& c : path) cells += " " + std::to_string(c.x) + "," + std::to_string(c.y);
                Automation::Report("TILE path(" + std::to_string(path.size()) + "):" + cells);
                bool ok = a[6] == "none" ? path.empty() : (int)path.size() == I(6);
                if (!ok) { err = "path length " + std::to_string(path.size()) + " != " + a[6]; return false; }
                // Ardışık hücreler komşu olmalı
                for (size_t i = 1; i < path.size(); ++i)
                    if (GridSystem::Distance(g, path[i - 1], path[i]) != 1) { err = "path step " + std::to_string(i) + " is not adjacent"; return false; }
                return true;
            }
            if (a[1] == "expect_distance" && a.size() > 6)
            {
                int d = GridSystem::Distance(g, { I(2), I(3) }, { I(4), I(5) });
                if (d != I(6)) { err = "distance " + std::to_string(d) + " != " + a[6]; return false; }
                return true;
            }
            if ((a[1] == "drag" && a.size() > 5) || ((a[1] == "click" || a[1] == "hover") && a.size() > 3))
            {
                ImVec2 s0, s1;
                if (!cellScreen(I(2), I(3), s0)) { err = "cell not visible in the viewport"; return false; }
                if (a[1] == "click") { Automation::QueueMouseClick(s0.x, s0.y); return true; }
                if (a[1] == "hover") { Automation::QueueMouseMove(s0.x, s0.y); return true; }
                if (!cellScreen(I(4), I(5), s1)) { err = "cell not visible in the viewport"; return false; }
                Automation::QueueMouseDrag(s0.x, s0.y, s1.x, s1.y, a.size() > 6 ? I(6) : 16);
                return true;
            }
            if (a[1] == "undo") { tp.Undo(); return true; }
            if (a[1] == "redo") { tp.Redo(); return true; }
            err = "unknown tile command";
            return false;
        });
        Automation::RegisterCommand("prefab", [this](const Args& a, std::string& err) {
            // prefab save <entity> <path> | spawn <path> [x y z] | apply <entity> | revert <entity> | unpack <entity>
            //        | expect_instance <entity> <path-suffix|none> | expect_children <entity> <n> | drop <path> <viewport-x> <viewport-y>
            if (a.size() < 3 || !m_ActiveScene) { err = "prefab save|spawn|apply|revert|unpack|expect_instance|expect_children|drop"; return false; }
            auto find = [this](const std::string& name) {
                Entity found{};
                m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) {
                    if (!found && tag.Tag == name) found = Entity{ id, m_ActiveScene.get() };
                });
                return found;
            };
            if (a[1] == "spawn")
            {
                glm::vec3 at(0.0f);
                if (a.size() >= 6) at = { std::stof(a[3]), std::stof(a[4]), std::stof(a[5]) };
                Entity e = AssetActions::SpawnFromAsset(m_ActiveScene.get(), a[2], {}, &at);
                if (!e) { err = "spawn failed: " + a[2]; return false; }
                SelectEntityEverywhere(e);
                return true;
            }
            if (a[1] == "drop" && a.size() >= 5 && m_ViewportPanel)
            {
                // Viewport bırakma noktası hesabını (UI dışı) dener
                glm::vec3 at = m_ViewportPanel->DropPointOnGround({ std::stof(a[3]), std::stof(a[4]) });
                Automation::Report("PREFAB drop at " + std::to_string(at.x) + " " + std::to_string(at.y) + " " + std::to_string(at.z));
                Entity e = AssetActions::SpawnFromAsset(m_ActiveScene.get(), a[2], {}, &at);
                if (!e) { err = "drop failed"; return false; }
                SelectEntityEverywhere(e);
                return true;
            }
            Entity e = find(a[2]);
            if (!e) { err = "no entity " + a[2]; return false; }
            if (a[1] == "save" && a.size() > 3)
            {
                if (!PrefabSerializer::Serialize(e, PrefabSerializer::Resolve(a[3]))) { err = "save failed"; return false; }
                if (!e.HasComponent<PrefabInstanceComponent>()) e.AddComponent<PrefabInstanceComponent>();
                e.GetComponent<PrefabInstanceComponent>().Prefab = PrefabSerializer::ToProjectPath(PrefabSerializer::Resolve(a[3]));
                return true;
            }
            if (a[1] == "apply") { if (!PrefabSerializer::ApplyInstance(e)) { err = "apply failed"; return false; } return true; }
            if (a[1] == "expect_overrides" && a.size() > 3)
            {
                const bool want = a[3] == "true" || a[3] == "1";
                const bool got = PrefabSerializer::HasOverrides(e);
                Automation::Report(std::string("OVERRIDES ") + (got ? "true" : "false"));
                if (got != want) { err = std::string("overrides: ") + (got ? "true" : "false"); return false; }
                return true;
            }
            if (a[1] == "revert")
            {
                SelectEntityEverywhere({});
                Entity fresh = PrefabSerializer::RevertInstance(e);
                if (!fresh) { err = "revert failed"; return false; }
                SelectEntityEverywhere(fresh);
                return true;
            }
            if (a[1] == "unpack") { if (e.HasComponent<PrefabInstanceComponent>()) e.RemoveComponent<PrefabInstanceComponent>(); return true; }
            if (a[1] == "expect_instance" && a.size() > 3)
            {
                std::string path = PrefabSerializer::GetPrefabPath(e);
                Automation::Report("PREFAB " + a[2] + " -> " + (path.empty() ? "(none)" : path));
                bool ok = a[3] == "none" ? path.empty()
                                         : path.size() >= a[3].size() && path.compare(path.size() - a[3].size(), a[3].size(), a[3]) == 0;
                if (!ok) { err = "prefab path '" + path + "'"; return false; }
                return true;
            }
            if (a[1] == "expect_children" && a.size() > 3)
            {
                // Tüm alt ağaçtaki varlık sayısı (kök hariç)
                std::function<int(Entity)> count = [&](Entity x) { int n = 0; for (Entity c : x.GetChildren()) n += 1 + count(c); return n; };
                int n = count(e);
                if (n != std::atoi(a[3].c_str())) { err = "descendants " + std::to_string(n) + " != " + a[3]; return false; }
                return true;
            }
            err = "unknown prefab command";
            return false;
        });
        Automation::RegisterCommand("texture", [this](const Args& a, std::string& err) {
            // texture select <path> | set <path> <key> <value> [<key> <value>...] (+ uygula)
            //         | expect <path> <mips|width|height|format|filter|memory_kb_below> <value>
            if (a.size() < 3) { err = "texture select|set|expect <path> ..."; return false; }
            std::filesystem::path path = a[2];
            if (path.is_relative() && Project::GetActive()) path = Project::GetActiveProjectDirectory() / path;
            if (!std::filesystem::exists(path)) { err = "no texture " + path.string(); return false; }
            const std::string p = path.string();
            if (a[1] == "select")
            {
                EditorState::Get().SelectAsset(p);
                m_ShowInspector = true;
                return true;
            }
            if (a[1] == "set")
            {
                TextureImportSettings s;
                TextureImportSettings::Load(p, s);
                for (size_t i = 3; i + 1 < a.size(); i += 2)
                {
                    const std::string& k = a[i];
                    const std::string& v = a[i + 1];
                    if (k == "type") s = TextureImportSettings::DefaultsFor(v == "normal" ? TextureType::NormalMap : v == "sprite" ? TextureType::Sprite
                                                                          : v == "ui" ? TextureType::UI : TextureType::Default);
                    else if (k == "filter") s.Filter = v == "point" ? TextureFilter::Point : v == "bilinear" ? TextureFilter::Bilinear : TextureFilter::Trilinear;
                    else if (k == "wrap") s.Wrap = v == "clamp" ? TextureWrap::Clamp : v == "mirror" ? TextureWrap::Mirror : TextureWrap::Repeat;
                    else if (k == "mipmaps") s.Mipmaps = v == "true" || v == "1";
                    else if (k == "aniso") s.Anisotropy = std::atoi(v.c_str());
                    else if (k == "maxsize") s.MaxSize = std::atoi(v.c_str());
                    else if (k == "normal") s.NormalFromGrayscale = v == "true" || v == "1";
                    else if (k == "bump") s.Bumpiness = (float)std::atof(v.c_str());
                    else if (k == "spritemode") s.SpriteMode = v == "multiple" ? SpriteImportMode::Multiple : v == "polygon" ? SpriteImportMode::Polygon : SpriteImportMode::Single;
                    else if (k == "sides") s.PolygonSides = std::atoi(v.c_str());
                    else if (k == "mesh") s.TightMesh = v != "fullrect";
                    else if (k == "extrude") s.ExtrudeEdges = std::atoi(v.c_str());
                    else if (k == "physics") s.GeneratePhysicsShape = v == "true" || v == "1";
                    else if (k == "shape") s.Shape = v == "cube" ? TextureShape::Cube : TextureShape::Texture2D;
                    else if (k == "resize") s.Resize = v == "bilinear" ? TextureResizeAlgorithm::Bilinear : TextureResizeAlgorithm::Mitchell;
                    else if (k == "format") s.Format = v == "rgba32" ? TextureFormatChoice::RGBA32 : v == "rgb24" ? TextureFormatChoice::RGB24 : v == "r8" ? TextureFormatChoice::R8 : TextureFormatChoice::Automatic;
                    else if (k == "compression") s.Compression = v == "low" ? TextureCompression::LowQuality : v == "normal" ? TextureCompression::NormalQuality : v == "high" ? TextureCompression::HighQuality : TextureCompression::None;
                    else if (k == "crunch") s.UseCrunch = v == "true" || v == "1";
                    else if (k == "pc_maxsize") { s.Platforms[0].Override = true; s.Platforms[0].MaxSize = std::atoi(v.c_str()); }
                    else { err = "unknown key " + k; return false; }
                }
                if (!s.Save(p)) { err = "cannot write meta"; return false; }
                AssetInspector::ApplyImportSideEffects(p, s);
                Texture2D::ReloadFromDisk(p);
                AssetInspector::Invalidate();
                return true;
            }
            if (a[1] == "expect" && a.size() > 4)
            {
                auto tex = Texture2D::Create(p);
                if (!tex) { err = "texture failed to load"; return false; }
                std::string got;
                const std::string& k = a[3];
                if (k == "mips") got = std::to_string(tex->GetMipLevels());
                else if (k == "width") got = std::to_string(tex->GetWidth());
                else if (k == "height") got = std::to_string(tex->GetHeight());
                else if (k == "format") got = tex->GetFormatName();
                else if (k == "filter") got = TextureImportSettings::FilterName(tex->GetImportSettings().Filter);
                else if (k == "memory_kb_below")
                {
                    uint64_t kb = tex->GetMemoryBytes() / 1024;
                    Automation::Report("TEXTURE " + a[2] + " memory_kb=" + std::to_string(kb));
                    if (kb >= (uint64_t)std::atoll(a[4].c_str())) { err = "memory " + std::to_string(kb) + " KB"; return false; }
                    return true;
                }
                else { err = "unknown property " + k; return false; }
                Automation::Report("TEXTURE " + a[2] + " " + k + "=" + got);
                if (got != a[4]) { err = k + " is " + got; return false; }
                return true;
            }
            err = "unknown texture command";
            return false;
        });
        Automation::RegisterCommand("open_scene", [this](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "open_scene <path>"; return false; }
            std::filesystem::path path = a[1];
            if (path.is_relative() && Project::GetActive()) path = Project::GetActiveProjectDirectory() / path;
            if (!std::filesystem::exists(path)) { err = "no scene " + path.string(); return false; }
            if (!OpenScene(path)) { err = "failed to open " + path.string(); return false; }
            return true;
        });
        Automation::RegisterCommand("undo", [this](const Args&, std::string&) { PerformUndo(); return true; });
        Automation::RegisterCommand("redo", [this](const Args&, std::string&) { PerformRedo(); return true; });
        Automation::RegisterCommand("expect_entities", [this](const Args& a, std::string& err) {
            // expect_entities <n> [name-prefix]: sahnedeki (önekle eşleşen) varlık sayısı
            if (a.size() < 2 || !m_ActiveScene) { err = "expect_entities <n> [prefix]"; return false; }
            int n = 0;
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto, auto& tag) {
                if (a.size() < 3 || tag.Tag.rfind(a[2], 0) == 0) ++n;
            });
            Automation::Report("ENTITIES " + std::to_string(n));
            if (n != std::atoi(a[1].c_str())) { err = "entity count " + std::to_string(n) + " != " + a[1]; return false; }
            return true;
        });
        Automation::RegisterCommand("addon", [this](const Args& a, std::string& err) {
            // addon list | reload | install|uninstall|enable|disable <id> | create <name> | invoke <id> <method>
            //       | button <id> <label> | insert <id> <name> | open_panel <id> <title>
            //       | expect_loaded <id> | expect_error <id> <none|substring> | expect_counts <id> <menu> <buttons> <panels>
            if (a.size() < 2) { err = "addon list|reload|install|uninstall|uninstall_all|enable|disable|create|invoke|button|insert|open_panel|expect_*"; return false; }
            auto& host = AddonHost::Get();
            const std::string& cmd = a[1];
            if (cmd == "list")
            {
                for (auto& m : AddonManager::GetCatalog()) Automation::Report("ADDON catalog " + m.Id + " " + m.Version);
                for (auto& m : AddonManager::GetInstalled())
                    Automation::Report("ADDON installed " + m.Id + (AddonManager::IsEnabled(m.Id) ? " enabled" : " disabled"));
                return true;
            }
            if (cmd == "reload") { host.ReloadAll(); return true; }
            if (cmd == "uninstall_all")
            {
                // addon uninstall_all [korunacak kimlikler...]: testleri önceden kurulu addon'lardan bağımsız kılar
                std::vector<std::string> ids;
                for (auto& m : AddonManager::GetInstalled())
                    if (std::find(a.begin() + 2, a.end(), m.Id) == a.end()) ids.push_back(m.Id);
                std::string error;
                for (auto& id : ids)
                    if (!AddonManager::Uninstall(id, &error)) { err = error; return false; }
                host.ReloadAll();
                Automation::Report("ADDON uninstalled " + std::to_string(ids.size()));
                return true;
            }
            if (a.size() < 3) { err = "missing id"; return false; }
            const std::string& id = a[2];
            std::string error;
            if (cmd == "install") { if (!AddonManager::Install(id, &error)) { err = error; return false; } host.ReloadAll(); return true; }
            if (cmd == "uninstall") { if (!AddonManager::Uninstall(id, &error)) { err = error; return false; } host.ReloadAll(); return true; }
            if (cmd == "enable" || cmd == "disable") { AddonManager::SetEnabled(id, cmd == "enable"); host.ReloadAll(); return true; }
            if (cmd == "create")
            {
                std::string name;
                for (size_t i = 2; i < a.size(); ++i) name += (i > 2 ? " " : "") + a[i];
                std::string newId;
                if (!AddonManager::CreateNew(name, &newId, &error)) { err = error; return false; }
                Automation::Report("ADDON created " + newId);
                host.ReloadAll();
                return true;
            }
            AddonHost::Instance* inst = host.FindInstance(id);
            if (cmd == "expect_unloaded")
            {
                if (inst && inst->Loaded) { err = id + " is still loaded"; return false; }
                return true;
            }
            if (cmd == "expect_loaded")
            {
                if (!inst || !inst->Loaded) { err = id + " not loaded" + (inst ? ": " + inst->Error : std::string()); return false; }
                return true;
            }
            if (!inst) { err = "addon " + id + " is not enabled/installed"; return false; }
            if (cmd == "expect_error" && a.size() > 3)
            {
                Automation::Report("ADDON " + id + " error='" + inst->Error + "'");
                bool ok = a[3] == "none" ? inst->Error.empty() : inst->Error.find(a[3]) != std::string::npos;
                if (!ok) { err = "error is '" + inst->Error + "'"; return false; }
                return true;
            }
            if (cmd == "expect_counts" && a.size() > 5)
            {
                int menu = (int)inst->Menu.size(), buttons = (int)inst->Buttons.size(), panels = (int)inst->Panels.size();
                Automation::Report("ADDON " + id + " menu=" + std::to_string(menu) + " buttons=" + std::to_string(buttons) + " panels=" + std::to_string(panels));
                if (menu != std::atoi(a[3].c_str()) || buttons != std::atoi(a[4].c_str()) || panels != std::atoi(a[5].c_str()))
                {
                    err = "counts differ";
                    return false;
                }
                return true;
            }
            if (cmd == "invoke" && a.size() > 3)
            {
                if (!host.Invoke(id, a[3])) { err = "invoke failed: " + inst->Error; return false; }
                return true;
            }
            // addon set <id> <field> <int|float|bool|string> <value> | addon expect <id> <field> <value | >N | <N>
            if ((cmd == "set" && a.size() > 5) || (cmd == "expect" && a.size() > 4))
            {
                if (!inst->Loaded || !inst->VM) { err = "addon not loaded"; return false; }
                if (cmd == "set")
                {
                    CQS::Value v;
                    if (a[4] == "int") v = CQS::Value::MakeInt(std::atoll(a[5].c_str()));
                    else if (a[4] == "float") v = CQS::Value::MakeFloat(std::atof(a[5].c_str()));
                    else if (a[4] == "bool") v = CQS::Value::MakeBool(a[5] == "true");
                    else v = inst->VM->MakeStringValue(std::string(a[5]));
                    if (!inst->VM->SetProperty(inst->Object, a[3], v, nullptr)) { inst->VM->TakeException(); err = "no field " + a[3]; return false; }
                    return true;
                }
                CQS::Value slot = inst->Object;
                if (!inst->VM->GetProperty(&slot, a[3], nullptr)) { inst->VM->TakeException(); err = "no field " + a[3]; return false; }
                std::string got = slot.ToString();
                Automation::Report("ADDON " + id + "." + a[3] + " = " + got);
                if (!a[4].empty() && (a[4][0] == '>' || a[4][0] == '<'))
                {
                    double want = std::atof(a[4].c_str() + 1);
                    bool ok = a[4][0] == '>' ? slot.ToNumber() > want : slot.ToNumber() < want;
                    if (!ok) { err = "got " + got; return false; }
                    return true;
                }
                if (got != a[4]) { err = "got " + got; return false; }
                return true;
            }
            if (cmd == "button" && a.size() > 3)
            {
                for (auto& b : inst->Buttons)
                    if (b.Label == a[3]) { host.Run(*inst, b.Method, b.Label); return inst->Error.empty(); }
                err = "no button " + a[3];
                return false;
            }
            if (cmd == "insert" && a.size() > 3)
            {
                for (auto& ins : inst->Manifest.Inserts)
                    if (ins.Name == a[3])
                    {
                        Entity e = SpawnAddonInsert(inst->Manifest, ins);
                        if (!e) { err = "insert failed"; return false; }
                        SelectEntityEverywhere(e);
                        return true;
                    }
                err = "no insert " + a[3];
                return false;
            }
            if (cmd == "open_panel" && a.size() > 3)
            {
                std::string title;
                for (size_t i = 3; i < a.size(); ++i) title += (i > 3 ? " " : "") + a[i];
                if (!host.OpenPanel(id, title)) { err = "no panel " + title; return false; }
                return true;
            }
            err = "unknown addon command";
            return false;
        });
        Automation::RegisterCommand("create_primitive", [this](const Args& a, std::string& err) {
            // create_primitive <cube|sphere|plane|cylinder> <name> <x> <y> <z> [dynamic|static]
            if (a.size() < 6 || !m_ActiveScene) { err = "create_primitive <type> <name> <x> <y> <z> [dynamic|static]"; return false; }
            Entity e = m_ActiveScene->CreateEntity(a[2]);
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = a[1] == "sphere" ? MeshType::Sphere : a[1] == "plane" ? MeshType::Plane : a[1] == "cylinder" ? MeshType::Cylinder : MeshType::Cube;
            e.GetComponent<TransformComponent>().Position = { std::stof(a[3]), std::stof(a[4]), std::stof(a[5]) };
            if (a.size() > 6)
            {
                if (mr.Type == MeshType::Sphere) e.AddComponent<SphereColliderComponent>();
                else e.AddComponent<BoxColliderComponent>();
                auto& rb = e.AddComponent<RigidbodyComponent>();
                rb.Type = a[6] == "dynamic" ? RigidbodyComponent::BodyType::Dynamic : RigidbodyComponent::BodyType::Static;
            }
            return true;
        });
        auto findByName = [this](const std::string& name) {
            Entity found{};
            if (m_ActiveScene)
                m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) {
                    if (!found && tag.Tag == name) found = Entity{ id, m_ActiveScene.get() };
                });
            return found;
        };
        Automation::RegisterCommand("comp", [this, findByName](const Args& a, std::string& err) {
            // comp add|remove <entity> <component> | comp set <entity> <component> <field> <value...>
            // comp expect <entity> <component> <field> <value | >N | <N>
            if (a.size() < 4 || !m_ActiveScene) { err = "comp add|remove|set|expect <entity> <component> ..."; return false; }
            Entity e = findByName(a[2]);
            if (!e) { err = "no entity " + a[2]; return false; }
            ComponentInfo* info = ComponentRegistry::Find(a[3]);
            if (!info) { err = "unknown component " + a[3]; return false; }
            if (a[1] == "add") { ComponentRegistry::AddWithRequirements(*info, e); return true; }
            if (a[1] == "remove") { if (info->Has(e)) info->Remove(e); return true; }
            if (a[1] == "expect_has" && a.size() > 4)
            {
                bool want = a[4] == "true" || a[4] == "1";
                if (info->Has(e) != want) { err = a[2] + (want ? " has no " : " still has ") + info->Name; return false; }
                return true;
            }
            if (!info->Has(e)) { err = a[2] + " has no " + info->Name; return false; }
            if (a.size() < 5) { err = "missing field"; return false; }
            // comp action <varlık> <bileşen> <düğme etiketi>: Inspector düğmesine (Action) basar
            if (a[1] == "action")
            {
                std::string label;
                for (size_t i = 4; i < a.size(); ++i) label += (i > 4 ? " " : "") + a[i];
                for (const auto& field : info->Fields)
                    if (field.Action && field.Label == label) { field.Action(e, info->Get(e)); return true; }
                err = "no action " + label;
                return false;
            }
            const ComponentField* f = ComponentRegistry::FindField(*info, a[4]);
            if (!f) { err = "unknown field " + a[4]; return false; }
            std::string value;
            for (size_t i = 5; i < a.size(); ++i) value += (i > 5 ? " " : "") + a[i];
            if (a[1] == "set")
            {
                // Varlık referansı adla: @Ad
                if (f->Kind == FieldKind::EntityRef && !value.empty() && value[0] == '@')
                {
                    Entity ref = findByName(value.substr(1));
                    if (!ref) { err = "no entity " + value.substr(1); return false; }
                    value = std::to_string((uint64_t)ref.GetComponent<IDComponent>().ID);
                }
                if (!ComponentRegistry::FieldFromString(*f, info->Get(e), value)) { err = "bad value '" + value + "'"; return false; }
                if (auto* w = m_ActiveScene->GetPhysicsWorld3D()) w->MarkDirty(e);
                return true;
            }
            if (a[1] == "expect")
            {
                if (f->Kind == FieldKind::EntityRef && !value.empty() && value[0] == '@')
                {
                    Entity ref = findByName(value.substr(1));
                    if (!ref) { err = "no entity " + value.substr(1); return false; }
                    value = std::to_string((uint64_t)ref.GetComponent<IDComponent>().ID);
                }
                std::string got = ComponentRegistry::FieldToString(*f, info->Get(e));
                Automation::Report("COMP " + a[2] + "." + info->ScriptName + "." + f->Name + " = " + got);
                if (!value.empty() && (value[0] == '>' || value[0] == '<'))
                {
                    double want = std::atof(value.c_str() + 1), have = std::atof(got.c_str());
                    if (value[0] == '>' ? !(have > want) : !(have < want)) { err = "got " + got; return false; }
                    return true;
                }
                // "~x y z": bileşen bazında yaklaşık karşılaştırma (0.01)
                if (!value.empty() && value[0] == '~')
                {
                    std::istringstream want(value.substr(1)), have(got);
                    std::string gotNorm = got;
                    std::replace(gotNorm.begin(), gotNorm.end(), ',', ' ');
                    have.str(gotNorm);
                    float w, h;
                    int n = 0;
                    while (want >> w)
                    {
                        if (!(have >> h) || std::fabs(w - h) > 0.01f) { err = "got " + got; return false; }
                        ++n;
                    }
                    if (n == 0) { err = "bad expected value"; return false; }
                    return true;
                }
                // Varlık yolları mutlak çözülür: beklenen yol sonek olarak da kabul edilir
                // Varlık yolları ayraçtan bağımsız karşılaştırılır (Windows'ta tam yol '\\' içerir)
                if (f->Kind == FieldKind::Asset)
                    std::replace(got.begin(), got.end(), '\\', '/');
                bool assetSuffix = f->Kind == FieldKind::Asset && !value.empty() && got.size() > value.size() &&
                                   got.compare(got.size() - value.size(), value.size(), value) == 0 && got[got.size() - value.size() - 1] == '/';
                if (got != value && !assetSuffix) { err = "got " + got; return false; }
                return true;
            }
            err = "unknown comp command";
            return false;
        });
        Automation::RegisterCommand("entity", [this, findByName](const Args& a, std::string& err) {
            // entity create <name> [x y z] | parent <child> <parent> | scale <name> x y z | rotate <name> x y z | move <name> x y z | tag <name> <tag>
            if (a.size() < 3 || !m_ActiveScene) { err = "entity create|parent|scale|rotate|move ..."; return false; }
            if (a[1] == "create")
            {
                Entity e = m_ActiveScene->CreateEntity(a[2]);
                if (a.size() >= 6) e.GetComponent<TransformComponent>().Position = { std::stof(a[3]), std::stof(a[4]), std::stof(a[5]) };
                return true;
            }
            Entity e = findByName(a[2]);
            if (!e) { err = "no entity " + a[2]; return false; }
            if (a[1] == "tag" && a.size() > 3)
            {
                e.GetComponent<TagComponent>().GameTag = a[3];
                return true;
            }
            if (a[1] == "parent" && a.size() > 3)
            {
                Entity p = findByName(a[3]);
                if (!p) { err = "no entity " + a[3]; return false; }
                m_ActiveScene->SetEntityParent(e, p);
                return true;
            }
            if (a[1] == "rename" && a.size() > 3)
            {
                e.GetComponent<TagComponent>().Tag = a[3];
                return true;
            }
            if (a[1] == "duplicate")
            {
                Entity copy = m_ActiveScene->DuplicateEntity(e, a.size() > 3 ? a[3] : std::string());
                if (!copy) { err = "duplicate failed"; return false; }
                SelectEntityEverywhere(copy);
                return true;
            }
            // entity static <name> <true|false>: Inspector'daki Static (pişirmeye katılır)
            if (a[1] == "static" && a.size() > 3)
            {
                Entity e = findByName(a[2]);
                if (!e) { err = "entity '" + a[2] + "' not found"; return false; }
                e.GetComponent<TagComponent>().IsStatic = a[3] == "true" || a[3] == "1";
                return true;
            }
            if (a[1] == "delete")
            {
                SelectEntityEverywhere({});
                m_ActiveScene->DestroyEntity(e);
                return true;
            }
            if (a[1] == "expect_active" && a.size() > 3)
            {
                const bool want = a[3] == "true" || a[3] == "1";
                const bool active = m_ActiveScene->IsEntityActive((entt::entity)e);
                if (active != want) { err = a[2] + (active ? " is active" : " is inactive"); return false; }
                return true;
            }
            if (a.size() < 6) { err = "need x y z"; return false; }
            glm::vec3 v(std::stof(a[3]), std::stof(a[4]), std::stof(a[5]));
            auto& tc = e.GetComponent<TransformComponent>();
            if (a[1] == "scale") tc.Scale = v;
            else if (a[1] == "rotate") tc.Rotation = glm::radians(v);
            else if (a[1] == "move") tc.Position = v;
            else { err = "unknown entity command"; return false; }
            return true;
        });
        Automation::RegisterCommand("projsettings", [this](const Args& a, std::string& err) {
            // projsettings set <key> <value> | expect <key> <value> | order <Class> <n> | save | reload
            // anahtarlar: quality.level, quality.fps, quality.aa, audio.volume, audio.doppler, nav.cost.<i>, nav.name.<i>,
            //             editor.mode, editor.newscene, graphics.tonemapper, input.custom
            if (a.size() < 2) { err = "projsettings set|expect|order|save|reload|reset|newscene ..."; return false; }
            auto& q = ProjectSettings::Quality();
            auto& au = ProjectSettings::Audio();
            auto& nav = ProjectSettings::Navigation();
            auto& ed = ProjectSettings::Editor();
            auto get = [&](const std::string& key, std::string& out) -> bool {
                char buf[64];
                if (key == "quality.level") out = std::to_string(q.Level);
                else if (key == "quality.fps") out = std::to_string(q.TargetFrameRate);
                else if (key == "quality.aa") out = std::to_string(QualitySettings::GetPreset().AntiAliasing);
                else if (key == "audio.volume") { std::snprintf(buf, sizeof(buf), "%g", au.GlobalVolume); out = buf; }
                else if (key == "audio.doppler") { std::snprintf(buf, sizeof(buf), "%g", au.DopplerFactor); out = buf; }
                else if (key.rfind("nav.cost.", 0) == 0) { std::snprintf(buf, sizeof(buf), "%g", nav.Areas[(size_t)std::clamp(std::stoi(key.substr(9)), 0, 31)].Cost); out = buf; }
                else if (key.rfind("nav.name.", 0) == 0) out = nav.Areas[(size_t)std::clamp(std::stoi(key.substr(9)), 0, 31)].Name;
                else if (key == "editor.mode") out = std::to_string(ed.DefaultBehaviorMode);
                else if (key == "editor.newscene") out = ed.NewSceneWithCameraAndLight ? "1" : "0";
                else if (key == "graphics.tonemapper") out = std::to_string(ProjectSettings::Graphics().DefaultTonemapper);
                else if (key == "input.custom") out = ProjectSettings::CustomInput() ? "1" : "0";
                else return false;
                return true;
            };
            if (a[1] == "set" && a.size() > 3)
            {
                const std::string& k = a[2];
                const std::string& v = a[3];
                if (k == "quality.level") q.Level = std::stoi(v);
                else if (k == "quality.fps") q.TargetFrameRate = std::stoi(v);
                else if (k == "audio.volume") au.GlobalVolume = std::stof(v);
                else if (k == "audio.doppler") au.DopplerFactor = std::stof(v);
                else if (k.rfind("nav.cost.", 0) == 0) nav.Areas[(size_t)std::clamp(std::stoi(k.substr(9)), 0, 31)].Cost = std::stof(v);
                else if (k.rfind("nav.name.", 0) == 0) nav.Areas[(size_t)std::clamp(std::stoi(k.substr(9)), 0, 31)].Name = v;
                else if (k == "editor.mode") ed.DefaultBehaviorMode = std::stoi(v);
                else if (k == "editor.newscene") ed.NewSceneWithCameraAndLight = v == "1" || v == "true";
                else if (k == "graphics.tonemapper") ProjectSettings::Graphics().DefaultTonemapper = std::stoi(v);
                else if (k == "input.custom") ProjectSettings::CustomInput() = v == "1" || v == "true";
                else { err = "unknown key " + k; return false; }
                ProjectSettings::Apply();
                return true;
            }
            if (a[1] == "expect" && a.size() > 3)
            {
                std::string have;
                if (!get(a[2], have)) { err = "unknown key " + a[2]; return false; }
                Automation::Report("PROJSETTING " + a[2] + " = " + have);
                if (have != a[3]) { err = "got " + have; return false; }
                return true;
            }
            if (a[1] == "order" && a.size() > 3)
            {
                auto& order = ProjectSettings::ScriptOrder();
                order.erase(std::remove_if(order.begin(), order.end(), [&](const ScriptOrderEntry& e) { return e.ClassName == a[2]; }), order.end());
                order.push_back({ a[2], std::stoi(a[3]) });
                ProjectSettings::InvalidateScriptOrderCache();
                return true;
            }
            if (a[1] == "save")
            {
                auto project = Project::GetActive();
                if (!project) { err = "no project"; return false; }
                ProjectSerializer serializer(project);
                return serializer.Serialize(Project::GetActiveProjectFile());
            }
            if (a[1] == "reload")
            {
                auto project = Project::GetActive();
                if (!project) { err = "no project"; return false; }
                ProjectSettings::ResetToDefaults();
                ProjectSerializer serializer(project);
                return serializer.Deserialize(Project::GetActiveProjectFile());
            }
            if (a[1] == "newscene")
            {
                RequestNewScene();
                return true;
            }
            if (a[1] == "reset")
            {
                ProjectSettings::ResetToDefaults();
                ProjectSettings::Apply();
                return true;
            }
            err = "unknown projsettings command";
            return false;
        });
        Automation::RegisterCommand("settings_page", [this](const Args& a, std::string& err) {
            // settings_page <page name...>  Project Settings sayfasını açar
            if (a.size() < 2 || !m_ProjectSettingsPanel) { err = "settings_page <name>"; return false; }
            std::string page;
            for (size_t i = 1; i < a.size(); ++i) page += (i > 1 ? " " : "") + a[i];
            m_ShowProjectSettings = true;
            m_ProjectSettingsPanel->SelectPage(page);
            m_PendingFocusWindow = "Project Settings";
            Automation::WaitFrames(2);
            return true;
        });
        Automation::RegisterCommand("phys", [this, findByName](const Args& a, std::string& err) {
            // phys backend <jolt|mujoco> | expect_backend <name> | setting <key> <value>
            // phys raycast ox oy oz dx dy dz <expectedEntity|none> | overlap ox oy oz r <count>
            if (a.size() < 2) { err = "phys backend|expect_backend|setting|raycast|overlap"; return false; }
            auto& settings = PhysicsSettings::Get();
            if (a[1] == "backend" && a.size() > 2)
            {
                settings.Backend = a[2] == "mujoco" ? PhysicsBackend3D::MuJoCo : PhysicsBackend3D::Jolt;
                return true;
            }
            if (a[1] == "setting" && a.size() > 3)
            {
                float v = std::stof(a[3]);
                if (a[2] == "FixedTimestep") settings.FixedTimestep = v;
                else if (a[2] == "Interpolation") settings.Interpolation = v != 0.0f;
                else if (a[2] == "GravityY") settings.Gravity.y = v;
                else { err = "unknown setting"; return false; }
                return true;
            }
            auto* world = m_ActiveScene ? m_ActiveScene->GetPhysicsWorld3D() : nullptr;
            if (a[1] == "expect_backend" && a.size() > 2)
            {
                std::string name = world ? world->GetBackendName() : "None";
                Automation::Report("PHYS BACKEND " + name + " bodies=" + std::to_string(world ? world->GetBodyCount() : 0));
                if (name != a[2]) { err = "backend is " + name; return false; }
                return true;
            }
            if (a[1] == "raycast2d" && a.size() > 6)
            {
                auto* w2 = m_ActiveScene->GetPhysicsWorld2D();
                RaycastHit2D hit;
                bool any = w2 && w2->Raycast({ std::stof(a[2]), std::stof(a[3]) }, { std::stof(a[4]), std::stof(a[5]) }, 1000.0f, hit);
                std::string name = any && m_ActiveScene->m_Registry.valid((entt::entity)hit.Entity)
                    ? Entity{ (entt::entity)hit.Entity, m_ActiveScene.get() }.GetComponent<TagComponent>().Tag : std::string("none");
                Automation::Report("RAYCAST2D hit=" + name + " dist=" + std::to_string(hit.Distance));
                if (name != a[6]) { err = "hit " + name; return false; }
                return true;
            }
            if (!world) { err = "no physics world"; return false; }
            if (a[1] == "raycast" && a.size() > 8)
            {
                Physics3D::RaycastHit hit;
                bool any = world->Raycast({ std::stof(a[2]), std::stof(a[3]), std::stof(a[4]) }, { std::stof(a[5]), std::stof(a[6]), std::stof(a[7]) }, 1000.0f, hit);
                std::string name = "none";
                if (any)
                {
                    Entity h{ (entt::entity)hit.Entity, m_ActiveScene.get() };
                    name = m_ActiveScene->m_Registry.valid((entt::entity)hit.Entity) ? h.GetComponent<TagComponent>().Tag : "?";
                }
                Automation::Report("RAYCAST hit=" + name + " dist=" + std::to_string(hit.Distance) + " normal=" + std::to_string(hit.Normal.x) + "," +
                                   std::to_string(hit.Normal.y) + "," + std::to_string(hit.Normal.z));
                if (name != a[8]) { err = "hit " + name; return false; }
                return true;
            }
            if (a[1] == "overlap" && a.size() > 6)
            {
                std::vector<uint32_t> ids;
                world->OverlapSphere({ std::stof(a[2]), std::stof(a[3]), std::stof(a[4]) }, std::stof(a[5]), ids);
                std::string names;
                for (uint32_t id : ids)
                    names += " " + (m_ActiveScene->m_Registry.valid((entt::entity)id) ? Entity{ (entt::entity)id, m_ActiveScene.get() }.GetComponent<TagComponent>().Tag : std::string("?"));
                Automation::Report("OVERLAP " + std::to_string(ids.size()) + names);
                if ((int)ids.size() != std::atoi(a[6].c_str())) { err = "overlap " + std::to_string(ids.size()); return false; }
                return true;
            }
            err = "unknown phys command";
            return false;
        });
        Automation::RegisterCommand("expect_position", [this](const Args& a, std::string& err) {
            // expect_position <name> <x|y|z> <above|below> <value>  (oyun modunda aktif sahne)
            if (a.size() < 5 || !m_ActiveScene) { err = "expect_position <name> <axis> <above|below> <value>"; return false; }
            Entity found{};
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) { if (!found && tag.Tag == a[1]) found = Entity{ id, m_ActiveScene.get() }; });
            if (!found) { err = "entity not found"; return false; }
            glm::vec3 p = glm::vec3(found.GetComponent<TransformComponent>().WorldTransform[3]);
            int axis = a[2] == "x" ? 0 : a[2] == "y" ? 1 : 2;
            float want = std::stof(a[4]);
            Automation::Report("POSITION " + a[1] + " " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
            bool ok = a[3] == "above" ? p[axis] > want : p[axis] < want;
            if (!ok) { err = a[1] + "." + a[2] + "=" + std::to_string(p[axis]) + " not " + a[3] + " " + a[4]; return false; }
            return true;
        });
        Automation::RegisterCommand("particles", [this](const Args& a, std::string& err) {
            // particles create <preset> <name> [x y z] | expect_count <name> <above|below> <n>
            if (a.size() < 3 || !m_ActiveScene) { err = "particles create <preset> <name> | expect_count <name> <above|below> <n>"; return false; }
            if (a[1] == "create")
            {
                Entity e = m_ActiveScene->CreateEntity(a.size() > 3 ? a[3] : a[2]);
                auto& ps = e.AddComponent<ParticleSystemComponent>();
                if (!ParticleSystem::ApplyPreset(ps, a[2])) { err = "unknown preset"; return false; }
                if (a.size() > 6) e.GetComponent<TransformComponent>().Position = { std::stof(a[4]), std::stof(a[5]), std::stof(a[6]) };
                SelectEntityEverywhere(e);
                return true;
            }
            if (a[1] == "expect_count" && a.size() > 4)
            {
                Entity found{};
                m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) { if (!found && tag.Tag == a[2]) found = Entity{ id, m_ActiveScene.get() }; });
                if (!found || !found.HasComponent<ParticleSystemComponent>()) { err = "no particle system " + a[2]; return false; }
                size_t n = found.GetComponent<ParticleSystemComponent>().RT->Particles.size();
                Automation::Report("PARTICLES " + a[2] + " " + std::to_string(n));
                int want = std::atoi(a[4].c_str());
                bool ok = a[3] == "above" ? (int)n > want : (int)n < want;
                if (!ok) { err = "count " + std::to_string(n); return false; }
                return true;
            }
            err = "unknown particles command";
            return false;
        });
        Automation::RegisterCommand("script", [this](const Args& a, std::string& err) {
            // script add <entity> <path>                 betik bileşeni ekle (sınıf: dosyadaki ilk script)
            // script set <entity> <field> <int|float|bool|string> <value>   Inspector alan değeri (oyun modunda canlı değer de)
            // script expect <entity> <field> <value>     oyun modunda canlı alan değeri (metin karşılaştırma)
            // script expect_pos <entity> <axis x|y|z> <above|below> <value>
            // <entity> yerine "Varlık@Sınıf": aynı varlıktaki birden çok betikten adı verileni seç
            // script new <dil kimliği> <yol>              "New Script" şablonunu yaz (Content Browser ile aynı)
            if (a.size() > 3 && a[1] == "new")
            {
                auto* runtime = Scripting::ScriptRuntimes::ForLanguage(a[2]);
                if (!runtime) { err = "no script language '" + a[2] + "'"; return false; }
                std::filesystem::path path = a[3];
                if (path.is_relative() && Project::GetActive()) path = Project::GetActiveProjectDirectory() / path;
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                std::ofstream(path, std::ios::binary) << runtime->GetNewScriptTemplate(path.stem().string());
                return std::filesystem::exists(path, ec);
            }
            if (a.size() < 3 || !m_ActiveScene) { err = "script new|add|set|expect|expect_pos ..."; return false; }
            std::string entityName = a[2], className;
            if (auto at = entityName.rfind('@'); at != std::string::npos && at > 0)
            {
                className = entityName.substr(at + 1);
                entityName = entityName.substr(0, at);
            }
            Entity e{};
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) { if (!e && tag.Tag == entityName) e = Entity{ id, m_ActiveScene.get() }; });
            if (!e) { err = "no entity " + entityName; return false; }
            if (a[1] == "add" && a.size() > 3)
            {
                // Inspector'a bırakmakla aynı kural: Data Asset sınıfı nesneye eklenmez (uyarı penceresi açılır)
                if (AssetActions::RefuseDataScript(a[3])) { err = "'" + a[3] + "' defines only Data Asset classes"; return false; }
                auto& sc = e.HasComponent<ConquerorScriptComponent>() ? e.GetComponent<ConquerorScriptComponent>() : e.AddComponent<ConquerorScriptComponent>();
                ConquerorScriptData data;
                data.ScriptPath = a[3];
                // Proje göreli yol: sınıf bilgisi için mutlak yola çöz
                std::string probe = a[3];
                if (std::filesystem::path(probe).is_relative() && Project::GetActive())
                {
                    auto candidate = Project::GetActiveProjectDirectory() / probe;
                    if (std::filesystem::exists(candidate)) probe = candidate.string();
                }
                std::vector<std::string> classes;
                if (auto* rt = Scripting::ScriptRuntimes::ForPath(probe))
                    rt->GetClasses(probe, classes, nullptr);
                if (!classes.empty()) data.ClassName = classes.front();
                sc.Scripts.push_back(data);
                SelectEntityEverywhere(e);
                return true;
            }
            if (!e.HasComponent<ConquerorScriptComponent>() || e.GetComponent<ConquerorScriptComponent>().Scripts.empty()) { err = "no script on " + a[2]; return false; }
            auto& scripts = e.GetComponent<ConquerorScriptComponent>().Scripts;
            // Sınıf adı ya da (sınıf adı kaydedilmemişse) dosya adı
            auto found = std::find_if(scripts.begin(), scripts.end(), [&](const auto& s) {
                return className.empty() || s.ClassName == className || std::filesystem::path(s.ScriptPath).stem().string() == className;
            });
            if (found == scripts.end()) { err = "no script class " + className + " on " + entityName; return false; }
            auto& script = *found;
            if (a[1] == "set" && a.size() > 5)
            {
                ScriptFieldOverride v;
                if (a[4] == "int") { v.Type = ScriptFieldOverride::Kind::Int; v.Int = std::atoll(a[5].c_str()); }
                else if (a[4] == "float") { v.Type = ScriptFieldOverride::Kind::Float; v.Float = std::atof(a[5].c_str()); }
                else if (a[4] == "bool") { v.Type = ScriptFieldOverride::Kind::Bool; v.Bool = a[5] == "true"; }
                else { v.Type = ScriptFieldOverride::Kind::String; v.String = a[5]; }
                script.FieldOverrides[a[3]] = v;
                // Oyun modunda betik çalışıyorsa canlı değer de değişir (yalnız başlangıç değeri değil)
                if (script.Instance && script.Runtime)
                {
                    Scripting::ScriptValue sv;
                    if (a[4] == "int") sv = Scripting::ScriptValue::MakeInt(std::atoll(a[5].c_str()));
                    else if (a[4] == "float") sv = Scripting::ScriptValue::MakeFloat(std::atof(a[5].c_str()));
                    else if (a[4] == "bool") sv = Scripting::ScriptValue::MakeBool(a[5] == "true");
                    else sv = Scripting::ScriptValue::MakeString(a[5]);
                    if (!static_cast<Scripting::ScriptRuntime*>(script.Runtime)->SetField(script.Instance, a[3], sv)) { err = "no live field " + a[3]; return false; }
                }
                return true;
            }
            if (a[1] == "expect" && a.size() > 4)
            {
                Scripting::ScriptValue sv;
                std::string got;
                auto* rt = static_cast<Scripting::ScriptRuntime*>(script.Runtime);
                if (!script.Instance || !rt || !rt->GetField(script.Instance, a[3], sv, &got)) { err = "script not running or no field " + a[3]; return false; }
                CQS::Value v = Scripting::ScriptRuntimes::ToValue(sv);
                Automation::Report("SCRIPT " + a[2] + "." + a[3] + " = " + got);
                if (!a[4].empty() && (a[4][0] == '>' || a[4][0] == '<'))
                {
                    double want = std::atof(a[4].c_str() + 1);
                    bool ok = a[4][0] == '>' ? v.ToNumber() > want : v.ToNumber() < want;
                    if (!ok) { err = "got " + got; return false; }
                    return true;
                }
                if (got != a[4]) { err = "got " + got; return false; }
                return true;
            }
            if (a[1] == "call" && a.size() > 3)
            {
                // script call <entity> <method> [args...]: sayısal argümanlar sayı, diğerleri metin
                auto* rt = static_cast<Scripting::ScriptRuntime*>(script.Runtime);
                if (!script.Instance || !rt) { err = "script not running"; return false; }
                std::vector<CQS::Value> args;
                for (size_t i = 4; i < a.size(); ++i)
                {
                    char* end = nullptr;
                    double num = std::strtod(a[i].c_str(), &end);
                    if (end && *end == '\0' && !a[i].empty())
                        args.push_back(a[i].find('.') == std::string::npos ? CQS::Value::MakeInt((int64_t)num) : CQS::Value::MakeFloat(num));
                    else
                        args.push_back(CQS::Value::MakeString(a[i]));
                }
                if (!rt->Invoke(script.Instance, a[3], args.empty() ? nullptr : args.data(), (int)args.size()))
                {
                    err = "call failed: " + a[3];
                    return false;
                }
                return true;
            }
            if (a[1] == "expect_pos" && a.size() > 5)
            {
                glm::vec3 p = e.GetComponent<TransformComponent>().Position;
                float val = a[3] == "x" ? p.x : (a[3] == "y" ? p.y : p.z);
                Automation::Report("POS " + a[2] + "." + a[3] + " = " + std::to_string(val));
                float want = std::stof(a[5]);
                bool ok = a[4] == "above" ? val > want : val < want;
                if (!ok) { err = "value " + std::to_string(val); return false; }
                return true;
            }
            err = "unknown script command";
            return false;
        });
        Automation::RegisterCommand("cvar", [](const Args& a, std::string& err) {
            // cvar <konsol satırı...> | cvar expect <ad> <değer | >N | <N>
            if (a.size() < 2) { err = "cvar <line> | cvar expect <name> <value>"; return false; }
            if (a[1] == "expect" && a.size() > 3)
            {
                ConsoleVariable* v = ConsoleSystem::FindVariable(a[2]);
                if (!v || !v->Get) { err = "no cvar " + a[2]; return false; }
                std::string got = v->Get();
                Automation::Report("CVAR " + a[2] + " = " + got);
                if (!a[3].empty() && (a[3][0] == '>' || a[3][0] == '<'))
                {
                    double have = std::atof(got.c_str()), want = std::atof(a[3].c_str() + 1);
                    if (a[3][0] == '>' ? have > want : have < want) return true;
                    err = "got " + got;
                    return false;
                }
                if (got != a[3]) { err = "got " + got; return false; }
                return true;
            }
            std::string line;
            for (size_t i = 1; i < a.size(); ++i) line += (i > 1 ? " " : "") + a[i];
            std::string output;
            bool ok = ConsoleSystem::Execute(line, output);
            Automation::Report("CVAR> " + output);
            if (!ok) err = output;
            return ok;
        });
        Automation::RegisterCommand("script_errors", [](const Args& a, std::string& err) {
            // script_errors mark | expect <n>: işaretten bu yana bildirilen ConquerorScript hata sayısı
            static uint64_t s_Mark = 0;
            if (a.size() >= 2 && a[1] == "mark") { s_Mark = CQS::CQSEngine::GetErrorCount(); return true; }
            if (a.size() >= 3 && a[1] == "expect")
            {
                uint64_t got = CQS::CQSEngine::GetErrorCount() - s_Mark;
                Automation::Report("SCRIPT_ERRORS " + std::to_string(got));
                if (got != (uint64_t)std::atoll(a[2].c_str()))
                {
                    err = "got " + std::to_string(got) + " (last: " + CQS::CQSEngine::GetLastError() + ")";
                    return false;
                }
                return true;
            }
            err = "script_errors mark | expect <n>";
            return false;
        });
        Automation::RegisterCommand("code", [this](const Args& a, std::string& err) {
            // code open <path> | type <text> | expect_completion <label> | accept [i] | expect_text <substring>
            // code expect_problems <n> | save
            if (a.size() < 2 || !m_CodeEditorPanel) { err = "code open|type|expect_completion|accept|expect_text|expect_problems|save"; return false; }
            auto unescape = [](std::string t) {
                std::string out;
                for (size_t i = 0; i < t.size(); ++i)
                {
                    if (t[i] == '\\' && i + 1 < t.size())
                    {
                        char c = t[++i];
                        out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 's' ? ' ' : c;
                    }
                    else out += t[i];
                }
                return out;
            };
            auto join = [&](size_t from) { std::string r; for (size_t i = from; i < a.size(); ++i) r += (i > from ? " " : "") + a[i]; return r; };
            if (a[1] == "open" && a.size() > 2) { m_CodeEditorPanel->OpenFile(a[2]); return true; }
            if (a[1] == "type") return m_CodeEditorPanel->TypeText(unescape(join(2)));
            if (a[1] == "accept") return m_CodeEditorPanel->AcceptCompletion(a.size() > 2 ? std::atoi(a[2].c_str()) : -1);
            if (a[1] == "save") { m_CodeEditorPanel->SaveFile(); return true; }
            if (a[1] == "replace" && a.size() > 3)
            {
                int n = m_CodeEditorPanel->ReplaceAllText(unescape(a[2]), unescape(a[3]));
                Automation::Report("REPLACED " + std::to_string(n));
                if (n == 0) { err = "nothing replaced"; return false; }
                return true;
            }
            if (a[1] == "expect_completion" && a.size() > 2)
            {
                auto labels = m_CodeEditorPanel->GetCompletionLabels();
                std::string shown;
                for (size_t i = 0; i < labels.size() && i < 8; ++i) shown += (i ? ", " : "") + labels[i];
                Automation::Report("COMPLETIONS [" + shown + "]");
                if (std::find(labels.begin(), labels.end(), a[2]) == labels.end()) { err = "'" + a[2] + "' not in completions"; return false; }
                return true;
            }
            if (a[1] == "expect_text" && a.size() > 2)
            {
                std::string want = unescape(join(2));
                if (m_CodeEditorPanel->GetActiveText().find(want) == std::string::npos) { err = "text does not contain: " + want; return false; }
                return true;
            }
            if (a[1] == "expect_problems" && a.size() > 2)
            {
                int n = m_CodeEditorPanel->GetDiagnosticCount();
                Automation::Report("PROBLEMS " + std::to_string(n));
                if (n != std::atoi(a[2].c_str())) { err = "problems " + std::to_string(n); return false; }
                return true;
            }
            err = "unknown code command";
            return false;
        });
        Automation::RegisterCommand("vs", [this](const Args& a, std::string& err) {
            // vs new <path> | open <path> | node <alias> <type> [x y] | link <a> <pin> <b> <pin>
            // vs default <alias> <pin> <value> | prop <alias> <key> <value> | var <name> <type> <default> [private]
            // vs save | expect_errors <n> | expect_code <substring> | remove <alias> | undo | redo
            static std::map<std::string, int> s_Alias;
            if (a.size() < 2 || !m_VisualScriptPanel) { err = "vs new|open|node|link|default|prop|var|save|expect_errors|expect_code"; return false; }
            auto unescape = [](std::string t) {
                std::string out;
                for (size_t i = 0; i < t.size(); ++i)
                {
                    if (t[i] == '\\' && i + 1 < t.size())
                    {
                        char c = t[++i];
                        out += c == 'n' ? '\n' : c == 's' ? ' ' : c;
                    }
                    else out += t[i];
                }
                return out;
            };
            auto& panel = *m_VisualScriptPanel;
            auto& graph = panel.GetGraph();
            auto node = [&](const std::string& alias) {
                auto it = s_Alias.find(alias);
                return it != s_Alias.end() ? it->second : std::atoi(alias.c_str());
            };
            if ((a[1] == "new" || a[1] == "open") && a.size() > 2)
            {
                std::filesystem::path path = a[2];
                if (path.is_relative() && Project::GetActive()) path = Project::GetActiveProjectDirectory() / path;
                if (a[1] == "new")
                {
                    Visual::Graph g;
                    g.Name = path.stem().string();
                    if (!g.Save(path.string())) { err = "cannot write " + path.string(); return false; }
                }
                s_Alias.clear();
                panel.Open(path);
                panel.GetIsOpen() = true;
                if (panel.GetPath() != path) { err = "cannot open " + path.string(); return false; }
                return true;
            }
            if (a[1] == "node" && a.size() > 3)
            {
                float x = a.size() > 5 ? std::stof(a[4]) : 40.0f + 240.0f * (float)(graph.Nodes.size() % 4);
                float y = a.size() > 5 ? std::stof(a[5]) : 40.0f + 160.0f * (float)(graph.Nodes.size() / 4);
                int id = graph.AddNode(a[3], x, y);
                if (!id) { err = "unknown node type " + a[3]; return false; }
                s_Alias[a[2]] = id;
                panel.MarkEdited();
                return true;
            }
            if (a[1] == "link" && a.size() > 5)
            {
                if (!graph.Connect(node(a[2]), a[3], node(a[4]), a[5], &err)) return false;
                panel.MarkEdited();
                return true;
            }
            if ((a[1] == "default" || a[1] == "prop") && a.size() > 4)
            {
                Visual::Node* n = graph.FindNode(node(a[2]));
                if (!n) { err = "no node " + a[2]; return false; }
                (a[1] == "default" ? n->InputDefaults : n->Properties)[a[3]] = unescape(a[4]);
                panel.MarkEdited();
                return true;
            }
            if (a[1] == "var" && a.size() > 4)
            {
                Visual::Variable v;
                v.Name = a[2];
                v.Type = Visual::PinTypeFromName(a[3]);
                v.Default = unescape(a[4]);
                v.Public = !(a.size() > 5 && a[5] == "private");
                if (Visual::Variable* existing = graph.FindVariable(v.Name)) *existing = v;
                else graph.Variables.push_back(v);
                panel.MarkEdited();
                return true;
            }
            if (a[1] == "remove" && a.size() > 2) { graph.RemoveNode(node(a[2])); panel.MarkEdited(); return true; }
            if ((a[1] == "expect_nodes" || a[1] == "expect_links") && a.size() > 2)
            {
                bool nodes = a[1] == "expect_nodes";
                int n = (int)(nodes ? graph.Nodes.size() : graph.Links.size());
                Automation::Report(std::string("VS ") + (nodes ? "NODES " : "LINKS ") + std::to_string(n));
                if (n != std::atoi(a[2].c_str())) { err = "count " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "save") { if (!panel.Save()) { err = "save failed"; return false; } return true; }
            // vs breakpoint <alias> on|off | expect_hits <alias> <n|>n> | expect_paused true|false
            if (a[1] == "breakpoint" && a.size() > 3)
            {
                Visual::Debugger::SetBreakpoint(panel.DebugKey(), node(a[2]), a[3] == "on");
                return true;
            }
            if (a[1] == "expect_hits" && a.size() > 3)
            {
                const uint64_t n = Visual::Debugger::HitCount(panel.DebugKey(), node(a[2]));
                Automation::Report("VS HITS " + a[2] + " = " + std::to_string(n));
                const bool ok = a[3][0] == '>' ? n > (uint64_t)std::stoll(a[3].substr(1)) : n == (uint64_t)std::stoll(a[3]);
                if (!ok) { err = "hits " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "expect_paused" && a.size() > 2)
            {
                const bool paused = EditorState::Get().IsPaused();
                Automation::Report(std::string("VS PAUSED ") + (paused ? "true" : "false"));
                if (paused != (a[2] == "true")) { err = paused ? "paused" : "not paused"; return false; }
                return true;
            }
            if (a[1] == "expect_errors" && a.size() > 2)
            {
                std::string code = panel.GetGeneratedCode();
                int n = panel.GetErrorCount();
                Automation::Report("VS ERRORS " + std::to_string(n));
                if (n != std::atoi(a[2].c_str())) { err = "errors " + std::to_string(n) + "\n" + code; return false; }
                return true;
            }
            if (a[1] == "expect_code" && a.size() > 2)
            {
                std::string want;
                for (size_t i = 2; i < a.size(); ++i) want += (i > 2 ? " " : "") + a[i];
                want = unescape(want);
                std::string code = panel.GetGeneratedCode();
                if (code.find(want) == std::string::npos) { err = "generated code does not contain: " + want + "\n" + code; return false; }
                return true;
            }
            err = "unknown vs command";
            return false;
        });
        Automation::RegisterCommand("cqs", [](const Args& a, std::string& err) {
            // cqs <kod...>   etkin betik VM'inde değerlendir, sonucu raporla
            std::string code;
            for (size_t i = 1; i < a.size(); ++i) code += (i > 1 ? " " : "") + a[i];
            std::string out;
            bool ok = CQS::CQSEngine::Evaluate(code, out);
            Automation::Report("CQS " + code + " => " + out);
            if (!ok) err = out;
            return ok;
        });
        Automation::RegisterCommand("run_cqs_file", [this](const Args& a, std::string& err) {
            // run_cqs_file <yol>: düzenleyici sahnesinde (oyun dışında) bir CQS dosyası çalıştır — sahne kurulum betikleri
            if (a.size() < 2 || !m_ActiveScene) { err = "run_cqs_file <path>"; return false; }
            std::ifstream f(a[1], std::ios::binary);
            if (!f) { err = "cannot read " + a[1]; return false; }
            std::string code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            Scene* previous = ScriptContext::GetActiveScene();
            ScriptContext::SetActiveScene(m_ActiveScene.get());
            std::string out;
            bool ok = false;
            {
                ScriptContext::EntityScope scope(ScriptContext::NullEntity);
                ok = CQS::CQSEngine::Evaluate(code, out);
            }
            ScriptContext::SetActiveScene(previous);
            Automation::Report("RUN " + a[1] + (ok ? std::string(" ok") : " failed: " + out));
            if (!ok) err = out;
            return ok;
        });
        Automation::RegisterCommand("game_pixel_world", [this](const Args& a, std::string& err) {
            // game_pixel_world <x> <y> [z] [<r|g|b|a> <v | >v | <v> ...]: birincil kameradan dünya noktasının Game görünümündeki rengi (0..255)
            if (a.size() < 3 || !m_ActiveScene || !m_GamePanel || !m_GamePanel->GetFramebuffer()) { err = "game_pixel_world x y [channel cond]..."; return false; }
            Entity cam = m_ActiveScene->GetPrimaryCameraEntity();
            if (!cam) { err = "no primary camera"; return false; }
            const glm::mat4 vp = cam.GetComponent<CameraComponent>().Camera.GetProjection() * glm::inverse(cam.GetComponent<TransformComponent>().WorldTransform);
            size_t first = 3;
            float z = 0.0f;
            if (a.size() > 3 && (std::isdigit((unsigned char)a[3][0]) || a[3][0] == '-' || a[3][0] == '.'))
            {
                z = std::stof(a[3]);
                first = 4;
            }
            glm::vec4 clip = vp * glm::vec4(std::stof(a[1]), std::stof(a[2]), z, 1.0f);
            if (clip.w <= 0.0f) { err = "point behind camera"; return false; }
            auto fb = m_GamePanel->GetFramebuffer();
            const auto& spec = fb->GetSpecification();
            const int px = (int)((clip.x / clip.w * 0.5f + 0.5f) * (float)spec.Width), py = (int)((clip.y / clip.w * 0.5f + 0.5f) * (float)spec.Height);
            if (px < 0 || py < 0 || px >= (int)spec.Width || py >= (int)spec.Height) { err = "point outside the view"; return false; }
            uint8_t rgba[4] = {};
            glGetTextureSubImage(fb->GetColorAttachmentRendererID(0), 0, px, py, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(rgba), rgba);
            Automation::Report("PIXEL (" + a[1] + "," + a[2] + ") = " + std::to_string(rgba[0]) + " " + std::to_string(rgba[1]) + " " + std::to_string(rgba[2]) + " " + std::to_string(rgba[3]));
            for (size_t i = first; i + 1 < a.size(); i += 2)
            {
                const int ch = a[i] == "r" ? 0 : a[i] == "g" ? 1 : a[i] == "b" ? 2 : 3;
                const std::string& cond = a[i + 1];
                const int have = rgba[ch];
                bool ok;
                if (cond[0] == '>') ok = have > std::stoi(cond.substr(1));
                else if (cond[0] == '<') ok = have < std::stoi(cond.substr(1));
                else ok = std::abs(have - std::stoi(cond)) <= 6;
                if (!ok) { err = a[i] + " is " + std::to_string(have); return false; }
            }
            return true;
        });
        Automation::RegisterCommand("game_region", [this](const Args& a, std::string& err) {
            // game_region <u0> <v0> <u1> <v1> [<kanal> <min|max|mean> <v | >v | <v>]...: Game görünümünde bölge istatistiği
            // (u, v 0..1, sol alt köşe 0,0). Kanal: r g b a lum, red/green/blue = o kanalın diğerlerinden baskınlığı (r - max(g, b)).
            if (a.size() < 5 || !m_GamePanel || !m_GamePanel->GetFramebuffer()) { err = "game_region u0 v0 u1 v1 [channel stat cond]..."; return false; }
            auto fb = m_GamePanel->GetFramebuffer();
            const auto& spec = fb->GetSpecification();
            auto toPx = [](const std::string& v, uint32_t size) { return std::clamp((int)(std::stof(v) * (float)size), 0, (int)size - 1); };
            const int x0 = toPx(a[1], spec.Width), y0 = toPx(a[2], spec.Height), x1 = toPx(a[3], spec.Width), y1 = toPx(a[4], spec.Height);
            const int w = std::max(1, std::abs(x1 - x0) + 1), h = std::max(1, std::abs(y1 - y0) + 1);
            std::vector<uint8_t> px((size_t)w * h * 4);
            glGetTextureSubImage(fb->GetColorAttachmentRendererID(0), 0, std::min(x0, x1), std::min(y0, y1), 0, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                 (GLsizei)px.size(), px.data());
            auto value = [&](size_t i, const std::string& ch) -> float {
                const float r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], al = px[i * 4 + 3];
                if (ch == "r") return r;
                if (ch == "g") return g;
                if (ch == "b") return b;
                if (ch == "a") return al;
                if (ch == "red") return r - std::max(g, b);
                if (ch == "green") return g - std::max(r, b);
                if (ch == "blue") return b - std::max(r, g);
                return 0.299f * r + 0.587f * g + 0.114f * b;
            };
            std::string report = "REGION " + std::to_string(w) + "x" + std::to_string(h);
            for (size_t k = 5; k + 2 < a.size(); k += 3)
            {
                const std::string& ch = a[k];
                const std::string& stat = a[k + 1];
                const std::string& cond = a[k + 2];
                float mn = 1e9f, mx = -1e9f, sum = 0.0f;
                const size_t n = (size_t)w * h;
                for (size_t i = 0; i < n; ++i)
                {
                    const float v = value(i, ch);
                    mn = std::min(mn, v);
                    mx = std::max(mx, v);
                    sum += v;
                }
                const float have = stat == "min" ? mn : stat == "max" ? mx : sum / (float)n;
                report += " " + ch + "." + stat + "=" + std::to_string((int)std::lround(have));
                bool ok;
                if (cond[0] == '>') ok = have > std::stof(cond.substr(1));
                else if (cond[0] == '<') ok = have < std::stof(cond.substr(1));
                else ok = std::abs(have - std::stof(cond)) <= 6.0f;
                if (!ok)
                {
                    Automation::Report(report);
                    err = ch + "." + stat + " is " + std::to_string(have);
                    return false;
                }
            }
            Automation::Report(report);
            return true;
        });
        Automation::RegisterCommand("gpu_probe", [this](const Args& a, std::string& err) {
            // gpu_probe <brdf|viewport|sh|prefiltered>: GPU verisini GL API'si üzerinden geri okur (her arka uçta çalışır)
            if (a.size() < 2) { err = "gpu_probe <brdf|viewport|sh|prefiltered>"; return false; }
            auto stats = [](const std::vector<float>& v, int comps, const std::string& what) {
                double mn[4] = { 1e30, 1e30, 1e30, 1e30 }, mx[4] = { -1e30, -1e30, -1e30, -1e30 }, sum[4] = {};
                size_t nan = 0, n = v.size() / (size_t)comps;
                for (size_t i = 0; i < n; ++i)
                    for (int c = 0; c < comps; ++c)
                    {
                        const float f = v[i * (size_t)comps + (size_t)c];
                        if (!std::isfinite(f)) { ++nan; continue; }
                        mn[c] = std::min(mn[c], (double)f); mx[c] = std::max(mx[c], (double)f); sum[c] += f;
                    }
                std::string r = "PROBE " + what + " texels=" + std::to_string(n) + " nonfinite=" + std::to_string(nan);
                char buf[160];
                for (int c = 0; c < comps; ++c)
                {
                    std::snprintf(buf, sizeof(buf), " c%d[min=%.4g max=%.4g mean=%.4g]", c, mn[c], mx[c], n ? sum[c] / (double)n : 0.0);
                    r += buf;
                }
                Automation::Report(r);
            };
            if (a[1] == "brdf")
            {
                const uint32_t id = EnvironmentMap::GetBRDFLUT();
                std::vector<float> data(256 * 256 * 2);
                glGetTextureImage(id, 0, GL_RG, GL_FLOAT, (GLsizei)(data.size() * sizeof(float)), data.data());
                stats(data, 2, "brdf");
                return true;
            }
            if (a[1] == "viewport" && m_ViewportPanel && m_ViewportPanel->GetFramebuffer())
            {
                auto fb = m_ViewportPanel->GetFramebuffer();
                const auto& spec = fb->GetSpecification();
                std::vector<float> data((size_t)spec.Width * spec.Height * 4);
                glGetTextureImage(fb->GetColorAttachmentRendererID(0), 0, GL_RGBA, GL_FLOAT, (GLsizei)(data.size() * sizeof(float)), data.data());
                stats(data, 4, "viewport");
                return true;
            }
            if (a[1] == "skybox")
            {
                // gpu_probe skybox [düzey]: sahne gökyüzü küpünün tüm yüzleri
                const auto& env = Renderer3D::GetEnvironment();
                if (!env.Skybox) { err = "no skybox"; return false; }
                const int level = a.size() > 2 ? std::atoi(a[2].c_str()) : 0;
                const uint32_t size = std::max(1u, env.Skybox->GetWidth() >> level);
                std::vector<float> all, face((size_t)size * size * 4);
                glBindTexture(GL_TEXTURE_CUBE_MAP, env.Skybox->GetRendererID());
                for (int f = 0; f < 6; ++f)
                {
                    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, level, GL_RGBA, GL_FLOAT, face.data());
                    all.insert(all.end(), face.begin(), face.end());
                }
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                stats(all, 4, "skybox_l" + std::to_string(level));
                return true;
            }
            if (a[1] == "sh" || a[1] == "prefiltered")
            {
                auto env = Renderer3D::GetActiveEnvironmentMap();
                if (!env) { err = "no environment map"; return false; }
                if (a[1] == "sh")
                {
                    std::vector<float> data;
                    for (const auto& c : env->GetIrradianceSH()) { data.push_back(c.x); data.push_back(c.y); data.push_back(c.z); }
                    stats(data, 3, "sh");
                    return true;
                }
                // Önfiltrelenmiş küpün +X yüzü, düzey 0
                GLint size = 0;
                glBindTexture(GL_TEXTURE_CUBE_MAP, env->GetPrefilteredID());
                size = 128;
                std::vector<float> face((size_t)size * size * 4 * 6);
                glGetTextureImage(env->GetPrefilteredID(), 0, GL_RGBA, GL_FLOAT, (GLsizei)(face.size() * sizeof(float)), face.data());
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                stats(face, 4, "prefiltered");
                return true;
            }
            err = "unknown probe " + a[1];
            return false;
        });
        Automation::RegisterCommand("shader_dump", [](const Args& a, std::string& err) {
            // shader_dump <stage 0|1> <source|glsl|spirv|hlsl|msl> <path>   (PBR shader)
            if (a.size() < 4) { err = "shader_dump <stage> <view> <path>"; return false; }
            auto shader = Renderer3D::GetPBRShader();
            if (!shader) { err = "no PBR shader"; return false; }
            std::string code = shader->GetStageCode(std::atoi(a[1].c_str()), a[2]);
            std::ofstream(a[3]) << code;
            Automation::Report("SHADER " + a[2] + " bytes=" + std::to_string(code.size()) + (shader->IsCompiledFromSPIRV() ? " spirv" : " direct"));
            return !code.empty() && code.rfind("//", 0) != 0;
        });
        Automation::RegisterCommand("ui", [this](const Args& a, std::string& err) {
            // ui create <Canvas|Panel|Image|Text|Button|Toggle|Slider|InputField|Dropdown|ScrollView|LayoutGroup> [name] [x y]
            // ui click <name>   (Game görünümünde gerçek fare tıklaması)
            // ui drag <name> dx dy   (öğenin ortasından ekran pikseli kadar sürükle)
            // ui expect_rect <name> <x|y|w|h> <v | >v | <v>   ui expect_value <name> <v | >v | <v>   ui expect_selected <name|none>
            if (a.size() < 3 || !m_ActiveScene) { err = "ui create|click|drag|dump|expect_rect|expect_value|expect_selected ..."; return false; }
            auto find = [&](const std::string& name) {
                Entity found{};
                m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& tag) { if (!found && tag.Tag == name) found = Entity{ id, m_ActiveScene.get() }; });
                return found;
            };
            // ui osk_click <tuş etiketi>  (ekran klavyesinde gerçek fare tıklaması)   ui osk_expect <open|closed>
            if (a[1] == "osk_click")
            {
                glm::vec4 r;
                if (!m_GamePanel || !UISystem::GetOnScreenKeyRect(a[2], r)) { err = "on-screen key not visible: " + a[2]; return false; }
                glm::vec2 p = m_GamePanel->RenderToScreen(glm::vec2(r.x + r.z * 0.5f, r.y + r.w * 0.5f));
                ImVec2 origin = ImGui::GetMainViewport()->Pos;
                return Automation::ExecuteCommandLine("click " + std::to_string((int)(p.x - origin.x)) + " " + std::to_string((int)(p.y - origin.y)), err);
            }
            if (a[1] == "expect_glyph_style")
            {
                const std::string style = InputGlyphs::StyleName(InputGlyphs::ActiveStyle());
                Automation::Report("UI glyph style " + style);
                if (style != a[2]) { err = "glyph style is " + style; return false; }
                return true;
            }
            if (a[1] == "osk_expect")
            {
                const bool open = UISystem::IsOnScreenKeyboardOpen();
                Automation::Report(std::string("UI osk ") + (open ? "open" : "closed"));
                if ((a[2] == "open") != open) { err = std::string("on-screen keyboard is ") + (open ? "open" : "closed"); return false; }
                return true;
            }
            auto compare = [&](float have, const std::string& want, const std::string& what) {
                Automation::Report("UI " + what + " = " + std::to_string(have));
                if (!want.empty() && (want[0] == '>' || want[0] == '<'))
                {
                    float w = std::stof(want.substr(1));
                    if (want[0] == '>' ? !(have > w) : !(have < w)) { err = what + " is " + std::to_string(have); return false; }
                    return true;
                }
                if (std::fabs(have - std::stof(want)) > 1.0f) { err = what + " is " + std::to_string(have); return false; }
                return true;
            };
            if (a[1] == "expect_selected")
            {
                Entity sel = UISystem::GetSelected(*m_ActiveScene);
                std::string name = sel ? sel.GetComponent<TagComponent>().Tag : std::string("none");
                Automation::Report("UI selected = " + name);
                if (name != a[2]) { err = "selected is " + name; return false; }
                return true;
            }
            if (a[1] == "create")
            {
                Entity e = a[2] == "Canvas" ? UISystem::CreateCanvas(*m_ActiveScene) : UISystem::CreateElement(*m_ActiveScene, a[2], Entity{});
                if (!e) { err = "unknown UI type"; return false; }
                if (a.size() > 3) e.GetComponent<TagComponent>().Tag = a[3];
                if (a.size() > 5 && e.HasComponent<RectTransformComponent>())
                    e.GetComponent<RectTransformComponent>().AnchoredPosition = { std::stof(a[4]), std::stof(a[5]) };
                return true;
            }
            Entity e = find(a[2]);
            if (!e) { err = "no entity " + a[2]; return false; }
            if (a[1] == "expect_rect" && a.size() > 4)
            {
                if (!e.HasComponent<RectTransformComponent>()) { err = "not UI"; return false; }
                const glm::vec4 r = e.GetComponent<RectTransformComponent>().ComputedRect;
                const float v = a[3] == "x" ? r.x : a[3] == "y" ? r.y : a[3] == "w" ? r.z : r.w;
                return compare(v, a[4], a[2] + "." + a[3]);
            }
            if (a[1] == "expect_value" && a.size() > 3)
            {
                float v = 0.0f;
                if (e.HasComponent<UIScrollbarComponent>()) v = e.GetComponent<UIScrollbarComponent>().Value;
                else if (e.HasComponent<UIScrollRectComponent>()) v = e.GetComponent<UIScrollRectComponent>().NormalizedPosition.y;
                else if (e.HasComponent<UISliderComponent>()) v = e.GetComponent<UISliderComponent>().Value;
                else { err = "no value"; return false; }
                // değerler 0..1: 1 piksel toleransı yerine 0.02
                if (!a[3].empty() && a[3][0] != '>' && a[3][0] != '<')
                {
                    Automation::Report("UI " + a[2] + ".value = " + std::to_string(v));
                    if (std::fabs(v - std::stof(a[3])) > 0.02f) { err = "value is " + std::to_string(v); return false; }
                    return true;
                }
                return compare(v, a[3], a[2] + ".value");
            }
            if (a[1] == "expect_pixel" && a.size() > 4)
            {
                // ui expect_pixel <name> <dx> <dy> [<r|g|b|a> <v | >v | <v> ...]: öğe merkezinden (canvas pikseli) kaydırılmış noktanın rengi
                if (!m_GamePanel || !m_GamePanel->GetFramebuffer() || !e.HasComponent<RectTransformComponent>()) { err = "no game panel / not UI"; return false; }
                Entity canvas = e.GetParent();
                while (canvas && !canvas.HasComponent<CanvasComponent>()) canvas = canvas.GetParent();
                if (!canvas) { err = "no canvas"; return false; }
                glm::vec2 screen = m_GamePanel->GetViewportSize();
                float scale = UISystem::GetCanvasScaleFactor(canvas, screen);
                auto& rt = e.GetComponent<RectTransformComponent>();
                glm::vec2 local(rt.ComputedRect.z * 0.5f + std::stof(a[3]), rt.ComputedRect.w * 0.5f + std::stof(a[4]));
                glm::vec2 c = glm::vec2(rt.CanvasMatrix * glm::vec4(local, 0, 1)) * scale;
                auto fb = m_GamePanel->GetFramebuffer();
                const auto& spec = fb->GetSpecification();
                int px = (int)c.x, py = (int)c.y;
                if (px < 0 || py < 0 || px >= (int)spec.Width || py >= (int)spec.Height) { err = "point outside the view"; return false; }
                uint8_t rgba[4] = {};
                glGetTextureSubImage(fb->GetColorAttachmentRendererID(0), 0, px, py, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(rgba), rgba);
                Automation::Report("UIPIXEL " + a[2] + " = " + std::to_string(rgba[0]) + " " + std::to_string(rgba[1]) + " " + std::to_string(rgba[2]) + " " + std::to_string(rgba[3]));
                for (size_t i = 5; i + 1 < a.size(); i += 2)
                {
                    const int ch = a[i] == "r" ? 0 : a[i] == "g" ? 1 : a[i] == "b" ? 2 : 3;
                    const std::string& cond = a[i + 1];
                    const int have = rgba[ch];
                    bool ok = cond[0] == '>' ? have > std::stoi(cond.substr(1)) : cond[0] == '<' ? have < std::stoi(cond.substr(1)) : std::abs(have - std::stoi(cond)) <= 6;
                    if (!ok) { err = a[i] + " is " + std::to_string(have); return false; }
                }
                return true;
            }
            if (a[1] == "click" || (a[1] == "drag" && a.size() > 4))
            {
                if (!m_GamePanel || !e.HasComponent<RectTransformComponent>()) { err = "no game panel / not UI"; return false; }
                Entity canvas = e.GetParent();
                while (canvas && !canvas.HasComponent<CanvasComponent>()) canvas = canvas.GetParent();
                if (!canvas) { err = "no canvas"; return false; }
                glm::vec2 screen = m_GamePanel->GetViewportSize();
                float scale = UISystem::GetCanvasScaleFactor(canvas, screen);
                auto& rt = e.GetComponent<RectTransformComponent>();
                glm::vec2 c = glm::vec2(rt.CanvasMatrix * glm::vec4(rt.ComputedRect.z * 0.5f, rt.ComputedRect.w * 0.5f, 0, 1)) * scale;
                glm::vec2 p = m_GamePanel->RenderToScreen(c);
                ImVec2 origin = ImGui::GetMainViewport()->Pos;
                const int px = (int)(p.x - origin.x), py = (int)(p.y - origin.y);
                if (a[1] == "drag")
                    return Automation::ExecuteCommandLine("drag " + std::to_string(px) + " " + std::to_string(py) + " " +
                                                          std::to_string(px + std::stoi(a[3])) + " " + std::to_string(py + std::stoi(a[4])) + " 20", err);
                return Automation::ExecuteCommandLine("click " + std::to_string(px) + " " + std::to_string(py), err);
            }
            if (a[1] == "dump")
            {
                std::function<void(Entity, int)> dump = [&](Entity x, int d) {
                    std::string line(d * 2, ' ');
                    line += x.GetComponent<TagComponent>().Tag;
                    if (x.HasComponent<RectTransformComponent>())
                    {
                        auto& rt = x.GetComponent<RectTransformComponent>();
                        char buf[128];
                        snprintf(buf, sizeof(buf), " rect(%.0f,%.0f %.0fx%.0f)", rt.ComputedRect.x, rt.ComputedRect.y, rt.ComputedRect.z, rt.ComputedRect.w);
                        line += buf;
                    }
                    Automation::Report("UIDUMP " + line);
                    for (auto c : x.GetChildren()) dump(c, d + 1);
                };
                dump(e, 0);
                return true;
            }
            if (a[1] == "expect_clicks" && a.size() > 3)
            {
                if (!e.HasComponent<UIButtonComponent>()) { err = "not a button"; return false; }
                uint32_t n = e.GetComponent<UIButtonComponent>().ClickCount;
                Automation::Report("UI clicks " + a[2] + " = " + std::to_string(n));
                if ((int)n != std::atoi(a[3].c_str())) { err = "clicks " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "expect_toggle" && a.size() > 3)
            {
                bool on = e.HasComponent<UIToggleComponent>() && e.GetComponent<UIToggleComponent>().IsOn;
                if (on != (a[3] == "on")) { err = std::string("toggle is ") + (on ? "on" : "off"); return false; }
                return true;
            }
            err = "unknown ui command";
            return false;
        });
        Automation::RegisterCommand("frame_selected", [this](const Args&, std::string&) {
            if (m_ViewportPanel) m_ViewportPanel->FrameSelected();
            return true;
        });
        Automation::RegisterCommand("material", [this, findByName](const Args& a, std::string& err) {
            // material assign <entity> <path.cqmat|path.cqphysmat>   (Content Browser'dan nesneye bırakmakla aynı)
            // material expect <entity> <path|none>
            if (a.size() < 4 && !(a.size() == 3 && a[1] == "expect")) { err = "material assign|expect <entity> <path>"; return false; }
            Entity e = findByName(a[2]);
            if (!e) { err = "entity '" + a[2] + "' not found"; return false; }
            std::string path = a.size() > 3 ? a[3] : std::string();
            for (size_t i = 4; i < a.size(); ++i) path += " " + a[i];
            if (!path.empty() && path != "none" && std::filesystem::path(path).is_relative() && Project::GetActive())
                path = (Project::GetActiveProjectDirectory() / path).string();
            if (a[1] == "assign")
            {
                if (!AssetActions::ApplyToEntity(e, path)) { err = "cannot apply " + path; return false; }
                return true;
            }
            if (a[1] == "expect")
            {
                std::string have = e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>().MaterialPath : std::string();
                bool ok = (path == "none" || path.empty()) ? have.empty()
                        : std::filesystem::weakly_canonical(have) == std::filesystem::weakly_canonical(path);
                if (!ok) err = "material is '" + have + "'";
                return ok;
            }
            err = "unknown material command";
            return false;
        });
        // lighting generate | expect_done | asset <path.cqlighting|none> | default_params <path.cqlmparams|none> | expect_value <alan> <değer>
        // xr buttons <0 sol|1 sağ> <grip> <trigger> <teleport> (-1 serbest) | xr look <yaw> <pitch>: XR Device Simulator'ı sür
        Automation::RegisterCommand("xr", [this](const Args& a, std::string& err) {
            if (a.size() >= 6 && a[1] == "buttons")
            {
                XRInteraction::SetSimulatedButtons(std::stoi(a[2]), std::stoi(a[3]), std::stoi(a[4]), std::stoi(a[5]));
                return true;
            }
            if (a.size() >= 4 && a[1] == "look" && m_ActiveScene)
            {
                for (auto e : m_ActiveScene->m_Registry.view<XROriginComponent>())
                {
                    auto& o = m_ActiveScene->m_Registry.get<XROriginComponent>(e);
                    o.HeadYaw = std::stof(a[2]);
                    o.HeadPitch = std::stof(a[3]);
                    return true;
                }
                err = "no XR Origin";
                return false;
            }
            err = "xr buttons <hand> <grip> <trigger> <teleport> | xr look <yaw> <pitch>";
            return false;
        });
        // occlusion bake [hücre boyu] | clear | expect_hidden <varlık> <true|false> | expect_cells <n|>n> | visualize on|off
        Automation::RegisterCommand("occlusion", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_OcclusionPanel || !m_ActiveScene) { err = "occlusion bake|clear|expect_hidden|expect_cells|visualize"; return false; }
            m_OcclusionPanel->SetContext(m_ActiveScene.get());
            if (a[1] == "bake")
            {
                if (a.size() > 2) m_OcclusionPanel->Settings().CellSize = std::stof(a[2]);
                if (!m_OcclusionPanel->StartBake(err)) return false;
                // Otomasyonda bitene kadar burada (GL bağlamı bu iş parçacığında)
                while (m_OcclusionPanel->IsBaking()) m_OcclusionPanel->Tick();
                Automation::Report("OCCLUSION " + m_OcclusionPanel->Status());
                return m_ActiveScene->GetOcclusionData() != nullptr;
            }
            if (a[1] == "clear") { m_OcclusionPanel->Clear(); return true; }
            if (a[1] == "visualize" && a.size() > 2) { m_ActiveScene->SetOcclusionVisualize(a[2] == "on" || a[2] == "true"); return true; }
            if (a[1] == "expect_cells" && a.size() > 2)
            {
                auto d = m_ActiveScene->GetOcclusionData();
                const int n = d ? (int)d->Cells.size() : 0;
                Automation::Report("OCCLUSION cells = " + std::to_string(n));
                const bool ok = a[2][0] == '>' ? n > std::stoi(a[2].substr(1)) : n == std::stoi(a[2]);
                if (!ok) { err = "cells " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "expect_hidden" && a.size() > 3)
            {
                Entity e;
                for (auto h : m_ActiveScene->m_Registry.view<TagComponent>())
                    if (m_ActiveScene->m_Registry.get<TagComponent>(h).Tag == a[2]) { e = Entity{ h, m_ActiveScene.get() }; break; }
                if (!e) { err = "no entity " + a[2]; return false; }
                // Ana kameranın konumundan hesapla
                std::unordered_set<uint32_t> hidden;
                Entity cam = m_ActiveScene->GetPrimaryCameraEntity();
                if (!cam) { err = "no primary camera"; return false; }
                OcclusionCulling::ComputeHidden(*m_ActiveScene, glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]), hidden);
                WorldActors::ComputeCullHidden(*m_ActiveScene, glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]), hidden);
                const bool got = hidden.count((uint32_t)(entt::entity)e) > 0, want = a[3] == "true";
                Automation::Report(std::string("OCCLUSION ") + a[2] + " hidden = " + (got ? "true" : "false") + " (hidden " + std::to_string(hidden.size()) + ")");
                if (got != want) { err = std::string("hidden is ") + (got ? "true" : "false"); return false; }
                return true;
            }
            err = "unknown occlusion command";
            return false;
        });
        Automation::RegisterCommand("lighting", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_LightingPanel || !m_ActiveScene) { err = "lighting generate|expect_done|asset|default_params|expect_value"; return false; }
            auto& b = m_ActiveScene->GetRenderSettings().Bake;
            auto abs = [](std::string p) {
                if (p == "none") return std::string();
                std::filesystem::path x(p);
                return (x.is_relative() && Project::GetActive() ? Project::GetActiveProjectDirectory() / x : x).string();
            };
            auto rest = [&](size_t from) { std::string p = a[from]; for (size_t i = from + 1; i < a.size(); ++i) p += " " + a[i]; return p; };
            if (a[1] == "generate") { m_LightingPanel->SetContext(m_ActiveScene); m_LightingPanel->GenerateLighting(); return true; }
            if (a[1] == "expect_done") { if (m_LightingPanel->IsBaking()) { err = "still baking"; return false; } return true; }
            // lighting expect_probes <n | >n>: pişirilmiş ışık probu sayısı (Adaptive Probe Volume + Light Probe Group)
            if (a[1] == "expect_probes" && a.size() > 2)
            {
                auto apv = m_ActiveScene->GetAdaptiveProbeVolume();
                const int n = apv && apv->IsBaked() ? apv->GetTotalProbeCount() : 0;
                // Ortalama L00 (ışık düzeyi) ve geçerli prob sayısı: pişirmenin gerçekten ışık yakaladığını gösterir
                glm::vec3 l00(0.0f);
                int valid = 0;
                for (int i = 0; i < n; ++i)
                {
                    l00 += apv->GetProbes()[i].SH.Coeffs[0] / (float)n;
                    valid += apv->GetProbes()[i].Valid ? 1 : 0;
                }
                char buf[160];
                std::snprintf(buf, sizeof(buf), "LIGHTING probes = %d (valid %d, mean L00 %.3f %.3f %.3f)", n, valid, l00.x, l00.y, l00.z);
                Automation::Report(buf);
                const bool ok = a[2][0] == '>' ? n > std::stoi(a[2].substr(1)) : n == std::stoi(a[2]);
                if (!ok) { err = "probes " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "asset" && a.size() > 2)
            {
                b.SettingsAsset = abs(rest(2));
                if (!b.SettingsAsset.empty() && !SceneSerializer::LoadLightingSettings(b.SettingsAsset, b)) { err = "cannot read " + b.SettingsAsset; return false; }
                b.SettingsAsset = abs(rest(2));
                return true;
            }
            if (a[1] == "default_params" && a.size() > 2) { b.DefaultLightmapParameters = abs(rest(2)); return true; }
            if (a[1] == "expect_value" && a.size() > 3)
            {
                YAML::Node n = YAML::Load(SceneSerializer::LightingSettingsToString(b));
                const std::string have = n[a[2]] ? n[a[2]].as<std::string>() : std::string("<missing>");
                Automation::Report("LIGHTING " + a[2] + " = " + have);
                if (have != a[3]) { err = a[2] + " is " + have; return false; }
                return true;
            }
            err = "unknown lighting command";
            return false;
        });
        // select_asset <path>: varlığı seç (Inspector'da açılır; Content Browser'da tıklamakla aynı)
        Automation::RegisterCommand("select_asset", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "select_asset <path>"; return false; }
            std::string p = a[1];
            for (size_t i = 2; i < a.size(); ++i) p += " " + a[i];
            std::filesystem::path x(p);
            if (x.is_relative() && Project::GetActive()) x = Project::GetActiveProjectDirectory() / x;
            if (!std::filesystem::exists(x)) { err = "no asset " + x.string(); return false; }
            EditorState::Get().SelectAsset(x.string());
            return true;
        });
        // testrunner open | run_edit [metot] | run_play [metot] | expect <geçen> <kalan>
        Automation::RegisterCommand("testrunner", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_TestRunnerPanel) { err = "testrunner open|run_edit|run_play|expect"; return false; }
            if (a[1] == "open") { m_TestRunnerPanel->GetIsOpen() = true; return true; }
            if (a[1] == "run_edit") { Scripting::TestRunner::RunEditMode(a.size() > 2 ? a[2] : ""); return true; }
            if (a[1] == "run_play")
            {
                Scripting::TestRunner::QueuePlayMode(a.size() > 2 ? a[2] : "");
                if (m_TestRunnerPanel->OnEnterPlay) m_TestRunnerPanel->OnEnterPlay();
                return true;
            }
            if (a[1] == "expect" && a.size() > 3)
            {
                const int p = Scripting::TestRunner::CountWithStatus(Scripting::TestRunner::Status::Passed);
                const int f = Scripting::TestRunner::CountWithStatus(Scripting::TestRunner::Status::Failed);
                std::string details;
                for (const auto& t : Scripting::TestRunner::Tests())
                    for (const auto& m : t.Messages)
                        details += " [" + t.Method + ": " + m + "]";
                Automation::Report("TESTS passed=" + std::to_string(p) + " failed=" + std::to_string(f) + details);
                if (p != std::atoi(a[2].c_str()) || f != std::atoi(a[3].c_str())) { err = "passed " + std::to_string(p) + " failed " + std::to_string(f); return false; }
                return true;
            }
            err = "unknown testrunner command";
            return false;
        });
        // nodeasset open <yol> | connect <düğüm> <pin> <düğüm> <pin> | prop <düğüm> <ad> <değer> | expect_nodes <n> | save
        Automation::RegisterCommand("nodeasset", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_NodeAssetPanel) { err = "nodeasset open|prop|save|expect_nodes ..."; return false; }
            auto& panel = *m_NodeAssetPanel;
            if (a[1] == "open" && a.size() >= 3)
            {
                panel.Open(AssetUI::Resolve(a[2]));
                panel.GetIsOpen() = true;
                if (panel.GetPath().empty()) { err = "cannot open " + a[2]; return false; }
                return true;
            }
            if (a[1] == "prop" && a.size() >= 5)
            {
                auto* n = panel.GetFile().Find(std::atoi(a[2].c_str()));
                if (!n) { err = "no node " + a[2]; return false; }
                n->Props[a[3]] = a[4];
                panel.MarkEdited();
                return true;
            }
            if (a[1] == "save")
                return panel.Save();
            if (a[1] == "expect_nodes" && a.size() >= 3)
            {
                if ((int)panel.GetFile().Nodes.size() != std::atoi(a[2].c_str())) { err = "nodes: " + std::to_string(panel.GetFile().Nodes.size()); return false; }
                return true;
            }
            err = "unknown nodeasset command";
            return false;
        });
        // editor_utility <yol>: Editor Utility betiğini düzenleyici sahnesinde çalıştır
        Automation::RegisterCommand("editor_utility", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ActiveScene) { err = "editor_utility <path>"; return false; }
            return Scripting::TestRunner::RunEditorUtility(AssetUI::Resolve(a[1]).string(), m_ActiveScene.get(), &err);
        });
        // pose_capture <yol.cqpose>: Inspector'daki Capture Pose ile aynı (varlığın Model / Clip / Time / Pose Name alanlarıyla)
        Automation::RegisterCommand("pose_capture", [](const Args& a, std::string& err) {
            SimpleAssets::Data data;
            if (a.size() < 2 || !SimpleAssets::Load(a[1], data)) { err = "pose_capture <path.cqpose>"; return false; }
            if (!AnimationAssets::CapturePose(a[1], data.Str("Model"), data.Str("Clip"), data.Float("Time"), data.Str("Pose Name"))) { err = "capture failed"; return false; }
            return true;
        });
        // psinfo <varlık>: Particle System'in çözülen doku sayfası ayarları
        Automation::RegisterCommand("psinfo", [this, findByName](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "psinfo <entity>"; return false; }
            Entity e = findByName(a[1]);
            if (!e || !e.HasComponent<ParticleSystemComponent>()) { err = "no particle system"; return false; }
            const auto& ps = e.GetComponent<ParticleSystemComponent>();
            Automation::Report("PS sheet=" + std::to_string((int)ps.TextureSheetEnabled) + " tiles=" + std::to_string(ps.Tiles.x) + "x" + std::to_string(ps.Tiles.y) + " cycles=" + std::to_string(ps.AnimationCycles) + " texture=" + ps.TexturePath);
            return true;
        });
        // soundinfo <varlık>: Audio Source'un çözülen ses varlığı değerleri
        Automation::RegisterCommand("soundinfo", [this, findByName](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ActiveScene) { err = "soundinfo <entity>"; return false; }
            Entity e = findByName(a[1]);
            if (!e || !e.HasComponent<AudioSourceComponent>()) { err = "no audio source"; return false; }
            const auto& c = e.GetComponent<AudioSourceComponent>();
            std::string line = "SOUND clip=" + c.ContainerClip + " vol=" + std::to_string(c.ContainerVolume) + " pitch=" + std::to_string(c.ContainerPitch) +
                               " max=" + std::to_string(c.MaxDistance) + " min=" + std::to_string(c.MinDistance) + " playing=" + std::to_string((int)c.IsPlaying);
            if (e.HasComponent<SoundSettingsComponent>())
            {
                const auto& ss = e.GetComponent<SoundSettingsComponent>();
                line += " gain=" + std::to_string(ss.Gain) + " classPitch=" + std::to_string(ss.Pitch);
            }
            if (e.HasComponent<SoundConcurrencyComponent>())
                line += " conMax=" + std::to_string(e.GetComponent<SoundConcurrencyComponent>().MaxCount);
            if (e.HasComponent<AudioReverbFilterComponent>())
                line += " reverbDecay=" + std::to_string(e.GetComponent<AudioReverbFilterComponent>().DecayTime);
            Automation::Report(line);
            return true;
        });
        // search_set <metin> | search_expect <metin>: Content Browser arama kutusu
        Automation::RegisterCommand("search_set", [this](const Args& a, std::string&) { if (m_ContentBrowserPanel) m_ContentBrowserPanel->SetSearch(a.size() > 1 ? a[1] : ""); return true; });
        Automation::RegisterCommand("search_expect", [this](const Args& a, std::string& err) {
            const std::string got = m_ContentBrowserPanel ? m_ContentBrowserPanel->GetSearch() : "";
            const std::string want = a.size() > 1 ? a[1] : "";
            if (got != want) { err = "search is '" + got + "'"; return false; }
            return true;
        });
        // open_asset <yol>: Content Browser çift tıklamasıyla aynı
        Automation::RegisterCommand("open_asset", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ContentBrowserPanel) { err = "open_asset <path>"; return false; }
            m_ContentBrowserPanel->OpenFile(AssetUI::Resolve(a[1]));
            return true;
        });
        // create_asset <tür>: Content Browser sağ tık menüsündeki girişle aynı kod (geçerli klasöre); yolu raporlar
        Automation::RegisterCommand("create_asset", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ContentBrowserPanel) { err = "create_asset <kind>"; return false; }
            const std::filesystem::path path = m_ContentBrowserPanel->CreateAssetOfKind(a[1]);
            if (path.empty()) { err = "could not create " + a[1]; return false; }
            Automation::Report("CREATED " + path.string());
            return true;
        });
        // content copy|cut <yol> | paste <klasör> | refs <yol> | deps <yol> | scene_refs <yol> | expect_results <N>
        //   | export <çıktı.cqpackage> <yollar...> | import <paket> | reimport <yol> | reimport_all | properties <yol>
        //   | extract <model> | additive <sahne> | exists <yol> <0|1> | select <yol>
        // Content Browser sağ tık menüsündeki işlemlerin kendisi (dosya pencereleri olmadan); yollar proje göreli olabilir
        Automation::RegisterCommand("content", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ContentBrowserPanel) { err = "content <command> ..."; return false; }
            auto& cb = *m_ContentBrowserPanel;
            auto abs = [](const std::string& p) {
                std::filesystem::path x(p);
                return (x.is_relative() && Project::GetActive() ? Project::GetActiveProjectDirectory() / x : x);
            };
            const std::string& op = a[1];
            if ((op == "copy" || op == "cut") && a.size() > 2) { cb.CopyAssets({ abs(a[2]) }, op == "cut"); return true; }
            if (op == "paste" && a.size() > 2)
            {
                if (!cb.PasteInto(abs(a[2]))) { err = "paste failed"; return false; }
                return true;
            }
            if ((op == "refs" || op == "deps") && a.size() > 2)
            {
                const auto p = abs(a[2]);
                auto list = op == "refs" ? cb.FindReferencesInProject(p) : cb.CollectDependencies(p);
                std::string names;
                for (const auto& f : list)
                    names += " " + f.filename().string();
                Automation::Report((op == "refs" ? "REFS " : "DEPS ") + std::to_string(list.size()) + ":" + names);
                cb.ShowResults((op == "refs" ? "References to " : "Dependencies of ") + p.filename().string(), list);
                return true;
            }
            if (op == "expect_results" && a.size() > 2)
            {
                const int want = std::atoi(a[2].c_str());
                if ((int)cb.GetResults().size() != want) { err = "results " + std::to_string(cb.GetResults().size()) + " != " + a[2]; return false; }
                return true;
            }
            if (op == "scene_refs" && a.size() > 2)
            {
                const int n = ContentBrowserPanel::s_FindReferencesInScene ? ContentBrowserPanel::s_FindReferencesInScene(abs(a[2]).string()) : -1;
                Automation::Report("SCENE_REFS " + std::to_string(n));
                if (a.size() > 3 && n != std::atoi(a[3].c_str())) { err = "scene refs " + std::to_string(n); return false; }
                return true;
            }
            if (op == "export" && a.size() > 3)
            {
                std::vector<std::filesystem::path> files;
                for (size_t i = 3; i < a.size(); ++i)
                {
                    const auto p = abs(a[i]);
                    std::error_code ec;
                    if (std::filesystem::is_directory(p, ec))
                    {
                        for (std::filesystem::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
                            if (it->is_regular_file(ec) && it->path().extension() != ".meta")
                                files.push_back(it->path());
                    }
                    else
                        for (const auto& d : cb.CollectDependencies(p))
                            files.push_back(d);
                }
                std::string error;
                if (!cb.ExportPackage(files, abs(a[2]), &error)) { err = error; return false; }
                Automation::Report("EXPORTED " + std::to_string(files.size()));
                return true;
            }
            if (op == "import" && a.size() > 2)
            {
                std::string error;
                if (!cb.BeginImportPackage(abs(a[2]), &error)) { err = error; return false; }
                if (a.size() > 3 && a[3] == "dialog")
                    return true;   // pencere açık kalsın (ekran görüntüsü)
                Automation::Report("IMPORTED " + std::to_string(cb.CommitImportPackage()));
                return true;
            }
            if (op == "reimport" && a.size() > 2) { cb.Reimport(abs(a[2])); return true; }
            if (op == "reimport_all") { cb.ReimportAll(); return true; }
            if (op == "properties" && a.size() > 2) { cb.OpenProperties(abs(a[2])); return true; }
            if (op == "extract" && a.size() > 2)
            {
                if (!cb.ExtractModelMaterials(abs(a[2]))) { err = "extract failed"; return false; }
                return true;
            }
            if (op == "additive" && a.size() > 2)
            {
                if (!ContentBrowserPanel::s_OpenSceneAdditive || !ContentBrowserPanel::s_OpenSceneAdditive(abs(a[2]).string())) { err = "additive failed"; return false; }
                return true;
            }
            if (op == "exists" && a.size() > 3)
            {
                std::error_code ec;
                const bool e = std::filesystem::exists(abs(a[2]), ec);
                Automation::Report("EXISTS " + a[2] + " = " + (e ? "1" : "0"));
                if (e != (a[3] == "1")) { err = a[2] + (e ? " exists" : " is missing"); return false; }
                return true;
            }
            if (op == "select" && a.size() > 2) { cb.SelectAndReveal(abs(a[2])); return true; }
            err = "unknown content command " + op;
            return false;
        });
        // inputactions open <yol> | add_map [ad] | add_action <harita> [ad] | add_binding <harita> <eylem> <yol>
        //   | add_composite <harita> <eylem> <2DVector|1DAxis|OneModifier|TwoModifiers> [parça yolları...]
        //   | kind <harita> <eylem> <Value|Button|PassThrough> [Any|Button|Axis|Vector2]
        //   | interactions|processors <harita> <eylem> <bağlama|-1> <dizge|none> | path <harita> <eylem> <bağlama> <yol>
        //   | select <harita> [eylem] [bağlama] | rename <ad> | delete | duplicate | undo | redo | save | autosave <0|1>
        //   | scheme <ad|all> | add_scheme <ad> <aygıtlar...> | expect_dirty <0|1> | projectwide <yol|none>
        //   | expect <dosya> <harita> <eylem> <bindings N | kind K | interactions S | path B P | groups B G>
        // Harita / eylem: ad ya da sıra numarası. Pencerenin düğmeleriyle aynı düzenleme işlemleri.
        Automation::RegisterCommand("inputactions", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_InputActionsEditor) { err = "inputactions <command> ..."; return false; }
            auto& ed = *m_InputActionsEditor;
            auto abs = [](const std::string& p) {
                std::filesystem::path x(p);
                return (x.is_relative() && Project::GetActive() ? Project::GetActiveProjectDirectory() / x : x).string();
            };
            auto isNum = [](const std::string& t) { return !t.empty() && (std::isdigit((unsigned char)t[0]) || (t[0] == '-' && t.size() > 1)); };
            auto mapIndex = [&](const InputActionAssetData& d, const std::string& m) {
                if (isNum(m)) return std::atoi(m.c_str());
                for (size_t i = 0; i < d.Maps.size(); ++i) if (d.Maps[i].Name == m) return (int)i;
                return -1;
            };
            auto actionIndex = [&](const InputActionAssetData& d, int mi, const std::string& n) {
                if (mi < 0 || mi >= (int)d.Maps.size()) return -1;
                if (isNum(n)) return std::atoi(n.c_str());
                const auto& acts = d.Maps[(size_t)mi].Actions;
                for (size_t i = 0; i < acts.size(); ++i) if (acts[i].Name == n) return (int)i;
                return -1;
            };
            auto rest = [&](size_t from) { std::string r; for (size_t i = from; i < a.size(); ++i) r += (i > from ? " " : "") + a[i]; return r; };
            const std::string& c = a[1];
            if (c == "open" && a.size() > 2)
            {
                if (!ed.Open(abs(rest(2)))) { err = "cannot open " + rest(2); return false; }
                m_PendingFocusWindow = "###InputActionsEditor";
                return true;
            }
            if (c == "add_map") return ed.AddMap(a.size() > 2 ? rest(2) : std::string()) >= 0;
            if (c == "add_action" && a.size() > 2)
            {
                const int mi = mapIndex(ed.Data(), a[2]);
                if (ed.AddAction(mi, a.size() > 3 ? rest(3) : std::string()) < 0) { err = "no map " + a[2]; return false; }
                return true;
            }
            if ((c == "add_binding" || c == "add_composite" || c == "kind" || c == "interactions" || c == "processors" || c == "path") && a.size() > 3)
            {
                const int mi = mapIndex(ed.Data(), a[2]);
                const int ai = actionIndex(ed.Data(), mi, a[3]);
                if (ai < 0) { err = "no action " + a[3] + " in " + a[2]; return false; }
                if (c == "add_binding") return a.size() > 4 && ed.AddBinding(mi, ai, a[4]) >= 0;
                if (c == "add_composite" && a.size() > 4)
                {
                    std::vector<std::string> parts(a.begin() + 5, a.end());
                    if (ed.AddComposite(mi, ai, a[4], parts) < 0) { err = "bad composite " + a[4]; return false; }
                    return true;
                }
                if (c == "kind" && a.size() > 4)
                {
                    const std::string& k = a[4];
                    InputActionKind kind = k == "Value" ? InputActionKind::Value : k == "PassThrough" ? InputActionKind::PassThrough : InputActionKind::Button;
                    const std::string ctl = a.size() > 5 ? a[5] : std::string("Any");
                    InputControlKind control = ctl == "Button" ? InputControlKind::Button : ctl == "Axis" ? InputControlKind::Axis
                                             : ctl == "Vector2" ? InputControlKind::Vector2 : InputControlKind::Any;
                    return ed.SetActionKind(mi, ai, kind, control);
                }
                if ((c == "interactions" || c == "processors") && a.size() > 5)
                {
                    const std::string spec = rest(5) == "none" ? std::string() : rest(5);
                    const int bi = std::atoi(a[4].c_str());
                    return c == "interactions" ? ed.SetInteractions(mi, ai, bi, spec) : ed.SetProcessors(mi, ai, bi, spec);
                }
                if (c == "path" && a.size() > 5)
                    return ed.SetBindingPath(mi, ai, std::atoi(a[4].c_str()), a[5]);
            }
            if (c == "select" && a.size() > 2)
            {
                const int mi = mapIndex(ed.Data(), a[2]);
                const int ai = a.size() > 3 ? actionIndex(ed.Data(), mi, a[3]) : -1;
                ed.Select(mi, ai, a.size() > 4 ? std::atoi(a[4].c_str()) : -1);
                return true;
            }
            if (c == "rename" && a.size() > 2) return ed.RenameSelection(rest(2));
            if (c == "delete") return ed.DeleteSelection();
            if (c == "duplicate") return ed.DuplicateSelection();
            if (c == "undo") { if (!ed.Undo()) { err = "nothing to undo"; return false; } return true; }
            if (c == "redo") { if (!ed.Redo()) { err = "nothing to redo"; return false; } return true; }
            if (c == "save") return ed.Save();
            if (c == "autosave" && a.size() > 2) { ed.SetAutoSave(a[2] == "1"); return true; }
            if (c == "scheme" && a.size() > 2) { ed.SetScheme(a[2] == "all" ? std::string() : rest(2)); return true; }
            if (c == "add_scheme" && a.size() > 3) return ed.AddScheme(a[2], std::vector<std::string>(a.begin() + 3, a.end()));
            if (c == "expect_dirty" && a.size() > 2)
            {
                if (ed.IsDirty() != (a[2] == "1")) { err = std::string("dirty is ") + (ed.IsDirty() ? "1" : "0"); return false; }
                return true;
            }
            if (c == "projectwide" && a.size() > 2)
            {
                ProjectSettings::ProjectWideActions() = a[2] == "none" ? std::string() : rest(2);
                ProjectSettings::Apply();
                return true;
            }
            if (c == "expect" && a.size() > 6)
            {
                InputActionAssetData d;
                if (!InputActionAssets::Load(abs(a[2]), d)) { err = "cannot read " + a[2]; return false; }
                const int mi = mapIndex(d, a[3]);
                const int ai = actionIndex(d, mi, a[4]);
                if (ai < 0 || ai >= (int)d.Maps[(size_t)mi].Actions.size()) { err = "no action " + a[4]; return false; }
                const auto& act = d.Maps[(size_t)mi].Actions[(size_t)ai];
                std::string have;
                const std::string& what = a[5];
                if (what == "bindings") have = std::to_string(act.Bindings.size());
                else if (what == "kind") have = InputActionAssets::KindName(act.Kind) + std::string("/") + InputActionAssets::ControlName(act.Control);
                else if (what == "interactions") have = act.Interactions.empty() ? "none" : act.Interactions;
                else if ((what == "path" || what == "groups" || what == "processors" || what == "binteractions") && a.size() > 7)
                {
                    const int bi = std::atoi(a[6].c_str());
                    if (bi < 0 || bi >= (int)act.Bindings.size()) { err = "no binding " + a[6]; return false; }
                    const auto& b = act.Bindings[(size_t)bi];
                    if (what == "path") have = b.Path.empty() ? "none" : b.Path;
                    else if (what == "processors") have = b.Processors.empty() ? "none" : b.Processors;
                    else if (what == "binteractions") have = b.Interactions.empty() ? "none" : b.Interactions;
                    else { for (size_t g = 0; g < b.Groups.size(); ++g) have += (g ? ";" : "") + b.Groups[g]; if (have.empty()) have = "none"; }
                    const std::string want = rest(7);
                    Automation::Report("INPUTACTIONS " + what + " = " + have);
                    if (have != want) { err = what + " is " + have + ", expected " + want; return false; }
                    return true;
                }
                const std::string want = rest(6);
                Automation::Report("INPUTACTIONS " + what + " = " + have);
                if (have != want) { err = what + " is " + have + ", expected " + want; return false; }
                return true;
            }
            err = "unknown inputactions command " + c;
            return false;
        });
        // sprite_atlas pack <atlas> | pack_all | expect <doku> <in|out>: görselin atlastan çizilip çizilmediği
        Automation::RegisterCommand("sprite_atlas", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "sprite_atlas pack <atlas>|pack_all|expect <texture> <in|out>"; return false; }
            auto abs = [](const std::string& p) {
                std::filesystem::path x(p);
                return (x.is_relative() && Project::GetActive() ? Project::GetActiveProjectDirectory() / x : x).string();
            };
            if (a[1] == "pack" && a.size() > 2)
            {
                std::string error;
                if (!SpriteAtlases::Pack(abs(a[2]), &error)) { err = error; return false; }
                return true;
            }
            if (a[1] == "pack_all") { Automation::Report("ATLAS packed " + std::to_string(SpriteAtlases::PackAllIfStale())); return true; }
            if (a[1] == "expect" && a.size() > 3)
            {
                SpriteAtlases::Lookup l;
                const bool in = SpriteAtlases::Find(abs(a[2]), l);
                Automation::Report(std::string("ATLAS ") + a[2] + (in ? " in atlas at " + std::to_string((int)l.Offset.x) + "," + std::to_string((int)l.Offset.y) : " not in an atlas"));
                if (in != (a[3] == "in")) { err = in ? "texture is in an atlas" : "texture is not in an atlas"; return false; }
                return true;
            }
            err = "unknown sprite_atlas command";
            return false;
        });
        // new_scene_from_template <şablon.cqscenetemplate | builtin:basic | builtin:empty>  (File > New Scene penceresinde Create)
        Automation::RegisterCommand("new_scene_from_template", [this](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "new_scene_from_template <path|builtin:basic|builtin:empty>"; return false; }
            std::string choice = a[1];
            for (size_t i = 2; i < a.size(); ++i) choice += " " + a[i];
            if (choice.rfind("builtin:", 0) != 0)
                choice = SceneTemplates::Resolve(choice).string();
            if (!CreateNewScene(choice)) { err = "cannot create a scene from " + choice; return false; }
            return true;
        });
        // new_scene_dialog: File > New Scene penceresini aç (otomasyonda da)
        Automation::RegisterCommand("new_scene_dialog", [this](const Args&, std::string&) { m_OpenNewSceneDialog = true; return true; });
        // material_bake [klasör=Assets/Materials]: sahnedeki gömülü (Instance) malzemeleri içerik bazında tekilleştirip
        // .cqmat dosyalarına yazar ve Mesh Renderer'lara atar (eski sahne / şablon taşıma)
        Automation::RegisterCommand("material_bake", [this](const Args& a, std::string& err) {
            if (!m_ActiveScene || !Project::GetActive()) { err = "no scene/project"; return false; }
            std::filesystem::path dir = a.size() > 1 ? std::filesystem::path(a[1]) : std::filesystem::path("Assets/Materials");
            if (dir.is_relative()) dir = Project::GetActiveProjectDirectory() / dir;
            int created = 0;
            const int baked = MeshRendererMaterial::BakeInstances(*m_ActiveScene, dir, &created);
            CQ_CORE_INFO("material_bake: {} renderers, {} new material files in {}", baked, created, dir.string());
            return true;
        });
        Automation::RegisterCommand("view_option", [this](const Args& a, std::string& err) {
            if (a.size() < 3 || !m_ViewportPanel) { err = "view_option <grid|gizmos|skybox|fog|shadows|post|sprites|realtime> <on|off>"; return false; }
            bool on = a[2] != "off" && a[2] != "0";
            auto& o = m_ViewportPanel->GetViewOptions();
            if (a[1] == "grid") o.Grid = on;
            else if (a[1] == "gizmos") o.Gizmos = on;
            else if (a[1] == "skybox") o.Skybox = on;
            else if (a[1] == "fog") o.Fog = on;
            else if (a[1] == "shadows") o.Shadows = on;
            else if (a[1] == "post") o.PostEffects = on;
            else if (a[1] == "sprites") o.Sprites2D = on;
            else if (a[1] == "ui") o.UI = on;
            else if (a[1] == "colliders") o.Colliders = on;
            else if (a[1] == "all_colliders") o.AllColliders = on;
            else if (a[1] == "realtime") o.Realtime = on;
            else { err = "unknown view option " + a[1]; return false; }
            return true;
        });
        Automation::RegisterCommand("resolution", [](const Args& a, std::string& err) {
            auto& gv = GameViewSettings::Get();
            if (a.size() < 2) { err = "resolution <preset index>"; return false; }
            int idx = std::atoi(a[1].c_str());
            if (idx < 0 || idx >= (int)gv.Presets.size()) { err = "preset index out of range"; return false; }
            gv.Selected = idx;
            return true;
        });
        Automation::RegisterCommand("expect_selected", [this](const Args& a, std::string& err) {
            Entity e = EditorState::Get().GetSelectedEntity();
            std::string name = (e && e.HasComponent<TagComponent>()) ? e.GetComponent<TagComponent>().Tag : std::string("<none>");
            std::string want = a.size() > 1 ? a[1] : std::string("<none>");
            if (name != want) { err = "selected '" + name + "' expected '" + want + "'"; return false; }
            return true;
        });
        Automation::RegisterCommand("pause", [this](const Args&, std::string&) { OnPauseButtonPressed(); return true; });
        // timeline create <entity> | select <entity> | add_track <type> [bindEntity] | key <track> <time> | clip <track> <start> <dur> [asset]
        //          | signal <track> <time> <method> | property <track> <component> <field> | preview <time> | stop_preview | expect_tracks <n>
        //          | expect_keys <track> <n> | undo | redo
        // signal reaction <alıcı> <yol.cqsignal> <metot> [hedef|self] [argüman] | send <alıcı> <yol.cqsignal> [beklenen tepki sayısı]
        Automation::RegisterCommand("signal", [this, findByName](const Args& a, std::string& err) {
            if (a.size() < 4 || !m_ActiveScene) { err = "signal reaction|send <entity> <path.cqsignal> ..."; return false; }
            Entity e = findByName(a[2]);
            if (!e) { err = "no entity " + a[2]; return false; }
            if (a[1] == "reaction" && a.size() > 4)
            {
                auto& rc = e.HasComponent<SignalReceiverComponent>() ? e.GetComponent<SignalReceiverComponent>() : e.AddComponent<SignalReceiverComponent>();
                SignalReaction r;
                r.Signal = a[3];
                r.Method = a[4];
                if (a.size() > 5 && a[5] != "self")
                {
                    Entity target = findByName(a[5]);
                    if (!target) { err = "no entity " + a[5]; return false; }
                    r.Target = (uint64_t)target.GetComponent<IDComponent>().ID;
                }
                if (a.size() > 6) r.Argument = a[6];
                rc.Reactions.push_back(r);
                return true;
            }
            if (a[1] == "send")
            {
                const int n = SignalAssets::Send(*m_ActiveScene, (uint32_t)(entt::entity)e, a[3]);
                Automation::Report("SIGNAL reactions=" + std::to_string(n));
                if (a.size() > 4 && n != std::atoi(a[4].c_str())) { err = "reactions " + std::to_string(n); return false; }
                return true;
            }
            err = "unknown signal command";
            return false;
        });
        Automation::RegisterCommand("animation", [this, findByName](const Args& a, std::string& err) {
            // animation open | create <nesne> | select_clip <yol> | add_property <yol|.> <Bileşen|Transform> <Alan> | key <eğri|-1> <t>
            //   | event <t> <metod> [metin] | time <t> | preview on|off | record on|off | play on|off | view dopesheet|curves
            //   | expect_curves <n> | expect_keys <eğri> <n> | expect_key <eğri> <i> <t> [x y z w] | expect_events <n>
            //   | expect_event <i> <t> <metod> | expect_clip <ad> | expect_length <s> | expect_state preview|record|none
            //   | expect_value <nesne> <Bileşen|Transform> <Alan> <x> [y z w]
            if (a.size() < 2 || !m_AnimationWindow) { err = "animation <open|create|select_clip|add_property|key|event|time|preview|record|play|view|expect_*>"; return false; }
            auto& w = *m_AnimationWindow;
            const std::string& sub = a[1];
            auto on = [&](size_t i) { return a.size() > i && (a[i] == "on" || a[i] == "1" || a[i] == "true"); };
            auto approx = [](float x, float y, float tol) { return std::abs(x - y) <= tol * std::max(1.0f, std::abs(y)); };
            if (sub == "open")
            {
                m_ShowAnimation = true;
                m_PendingFocusWindow = "###AnimationWindow";
                Automation::WaitFrames(2);
                return true;
            }
            if (sub == "create" && a.size() > 2)
            {
                Entity e = findByName(a[2]);
                if (!e) { err = "no entity " + a[2]; return false; }
                m_ShowAnimation = true;
                SelectEntityEverywhere(e);
                return w.CreateClip(e, &err);
            }
            if (sub == "select_clip" && a.size() > 2) return w.SelectClip(a[2], &err);
            if (sub == "add_property" && a.size() > 4) return w.AddProperty(a[2] == "." ? std::string() : a[2], a[3], a[4], &err);
            if (sub == "key" && a.size() > 3) return w.AddKeyAt(std::stoi(a[2]), std::stof(a[3]), &err);
            if (sub == "event" && a.size() > 3) return w.AddEventAt(std::stof(a[2]), a[3], a.size() > 4 ? a[4] : std::string(), &err);
            if (sub == "time" && a.size() > 2) { w.SetTime(std::stof(a[2])); Automation::WaitFrames(2); return true; }
            if (sub == "preview" && a.size() > 2) { w.SetPreviewing(on(2)); Automation::WaitFrames(2); return true; }
            if (sub == "record" && a.size() > 2) { w.SetRecording(on(2)); Automation::WaitFrames(2); return true; }
            if (sub == "play" && a.size() > 2) { w.SetPlaying(on(2)); return true; }
            if (sub == "view" && a.size() > 2) { w.SetCurvesView(a[2] == "curves"); Automation::WaitFrames(2); return true; }
            // box <eğri|-1> <t0> <t1>: dopesheet satırında t1'den t0'a gerçek fare sürüklemesi (kutu seçimi)
            if (sub == "box" && a.size() > 4)
            {
                ImVec2 p0, p1;
                if (!w.LanePoint(std::stoi(a[2]), std::stof(a[4]), p0) || !w.LanePoint(std::stoi(a[2]), std::stof(a[3]), p1)) { err = "row not visible"; return false; }
                Automation::QueueMouseDrag(p0.x, p0.y - 3.0f, p1.x, p1.y + 3.0f, 12, 0);
                return true;
            }
            const PropertyClip& clip = w.GetClip();
            if (sub == "expect_curves" && a.size() > 2)
            {
                if ((int)clip.Curves.size() != std::stoi(a[2])) { err = "clip has " + std::to_string(clip.Curves.size()) + " curves"; return false; }
                return true;
            }
            if (sub == "expect_keys" && a.size() > 3)
            {
                const int c = std::stoi(a[2]);
                if (c < 0 || c >= (int)clip.Curves.size()) { err = "no curve " + a[2]; return false; }
                const int n = (int)clip.Curves[(size_t)c].Channel.Keys.size();
                if (n != std::stoi(a[3])) { err = "curve " + a[2] + " has " + std::to_string(n) + " keys"; return false; }
                return true;
            }
            if (sub == "expect_key" && a.size() > 4)
            {
                const int c = std::stoi(a[2]), i = std::stoi(a[3]);
                if (c < 0 || c >= (int)clip.Curves.size() || i < 0 || i >= (int)clip.Curves[(size_t)c].Channel.Keys.size()) { err = "no key " + a[2] + "/" + a[3]; return false; }
                const TimelineKey& k = clip.Curves[(size_t)c].Channel.Keys[(size_t)i];
                const float half = 0.5f / std::max(1.0f, clip.FrameRate);
                if (std::abs(k.Time - std::stof(a[4])) > half) { err = "key time " + std::to_string(k.Time); return false; }
                for (size_t j = 5; j < a.size() && j < 9; ++j)
                    if (!approx(k.Value[(int)(j - 5)], std::stof(a[j]), 2e-3f))
                    {
                        err = "key value[" + std::to_string(j - 5) + "] = " + std::to_string(k.Value[(int)(j - 5)]);
                        return false;
                    }
                return true;
            }
            if (sub == "expect_events" && a.size() > 2)
            {
                if ((int)clip.Events.size() != std::stoi(a[2])) { err = "clip has " + std::to_string(clip.Events.size()) + " events"; return false; }
                return true;
            }
            if (sub == "expect_event" && a.size() > 4)
            {
                const int i = std::stoi(a[2]);
                if (i < 0 || i >= (int)clip.Events.size()) { err = "no event " + a[2]; return false; }
                const auto& e = clip.Events[(size_t)i];
                const float half = 0.5f / std::max(1.0f, clip.FrameRate);
                if (std::abs(e.Time - std::stof(a[3])) > half || e.Function != a[4])
                {
                    err = "event " + a[2] + " is " + e.Function + " at " + std::to_string(e.Time);
                    return false;
                }
                return true;
            }
            if (sub == "expect_clip" && a.size() > 2)
            {
                const std::string stem = std::filesystem::path(w.GetClipPath()).stem().string();
                if (stem != a[2]) { err = "clip is '" + stem + "'"; return false; }
                return true;
            }
            if (sub == "expect_length" && a.size() > 2)
            {
                if (!approx(clip.Length, std::stof(a[2]), 1e-3f)) { err = "length " + std::to_string(clip.Length); return false; }
                return true;
            }
            if (sub == "expect_state" && a.size() > 2)
            {
                const std::string state = w.IsRecording() ? "record" : w.IsPreviewing() ? "preview" : "none";
                if (state != a[2]) { err = "state is " + state; return false; }
                return true;
            }
            if (sub == "expect_value" && a.size() > 5)
            {
                Entity e = findByName(a[2]);
                if (!e) { err = "no entity " + a[2]; return false; }
                glm::vec4 v(0.0f);
                if (!PropertyClips::ReadValue(e, a[3], a[4], v)) { err = "no property " + a[3] + "." + a[4]; return false; }
                for (size_t j = 5; j < a.size() && j < 9; ++j)
                    if (!approx(v[(int)(j - 5)], std::stof(a[j]), 2e-3f))
                    {
                        err = a[4] + "[" + std::to_string(j - 5) + "] = " + std::to_string(v[(int)(j - 5)]);
                        return false;
                    }
                return true;
            }
            err = "unknown animation command " + sub;
            return false;
        });
        Automation::RegisterCommand("timeline", [this, findByName](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_TimelinePanel) { err = "timeline <create|select|add_track|key|clip|signal|property|preview|stop_preview|expect_tracks|expect_keys|undo|redo>"; return false; }
            const std::string& sub = a[1];
            auto& tl = *m_TimelinePanel;
            if ((sub == "create" || sub == "select") && a.size() > 2)
            {
                Entity e = findByName(a[2]);
                if (!e) { err = "no entity " + a[2]; return false; }
                m_ShowTimeline = true;
                if (sub == "create") return tl.CreateTimelineFor(e, &err);
                if (!tl.SelectDirector(e)) { err = a[2] + " has no Playable Director"; return false; }
                return true;
            }
            if (sub == "add_track" && a.size() > 2)
            {
                for (int i = 0; i < (int)TimelineTrackType::Count; ++i)
                    if (a[2] == TimelineAsset::TrackTypeName((TimelineTrackType)i))
                        return tl.AddTrack((TimelineTrackType)i, a.size() > 3 ? a[3] : std::string(), &err);
                err = "unknown track type " + a[2];
                return false;
            }
            if (sub == "key" && a.size() > 3) return tl.AddKeyAt(std::stoi(a[2]), std::stof(a[3]), &err);
            if (sub == "clip" && a.size() > 4) return tl.AddClipAt(std::stoi(a[2]), std::stof(a[3]), std::stof(a[4]), a.size() > 5 ? a[5] : std::string(), &err);
            if (sub == "signal" && a.size() > 4) return tl.AddMarkerAt(std::stoi(a[2]), std::stof(a[3]), a[4], &err);
            if (sub == "property" && a.size() > 4) return tl.SetPropertyTarget(std::stoi(a[2]), a[3], a[4], &err);
            if (sub == "preview" && a.size() > 2) { tl.SetPreviewTime(std::stod(a[2])); Automation::WaitFrames(2); return true; }
            if (sub == "stop_preview") { tl.StopPreview(); Automation::WaitFrames(1); return true; }
            if (sub == "undo") { tl.Undo(); return true; }
            if (sub == "redo") { tl.Redo(); return true; }
            auto asset = tl.GetAsset();
            if (!asset) { err = "no timeline selected"; return false; }
            if (sub == "expect_tracks" && a.size() > 2)
            {
                if ((int)asset->Tracks.size() != std::stoi(a[2])) { err = std::to_string(asset->Tracks.size()) + " tracks"; return false; }
                return true;
            }
            if (sub == "expect_keys" && a.size() > 3)
            {
                const int ti = std::stoi(a[2]);
                if (ti < 0 || ti >= (int)asset->Tracks.size()) { err = "no such track"; return false; }
                size_t n = 0;
                for (const auto& ch : asset->Tracks[(size_t)ti].Channels)
                    n = std::max(n, ch.Keys.size());
                if ((int)n != std::stoi(a[3])) { err = std::to_string(n) + " keys"; return false; }
                return true;
            }
            err = "unknown timeline command " + sub;
            return false;
        });
        // build open | add_scene <path> | clear_scenes | output <dir> | platform <name|host> | start [run] | all (every ready platform) |
        //     results | expect_done | expect_success | report
        Automation::RegisterCommand("build", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_BuildSettingsPanel) { err = "build <open|add_scene|clear_scenes|output|platform|start|all|results|expect_done|expect_success|report>"; return false; }
            auto project = Project::GetActive();
            const std::string& sub = a[1];
            if (sub == "open") { m_ShowBuildSettings = true; m_PendingFocusWindow = "###BuildSettings"; Automation::WaitFrames(2); return true; }
            if (!project) { err = "no project"; return false; }
            auto& build = project->GetConfig().Build;
            if (sub == "clear_scenes") { build.Scenes.clear(); return true; }
            if (sub == "add_scene" && a.size() > 2) { build.Scenes.push_back({ a[2], true }); return true; }
            if (sub == "output" && a.size() > 2) { build.OutputDirectory = a[2]; return true; }
            if (sub == "platform" && a.size() > 2)
            {
                // build platform <Linux|Windows|macOS|Android|Android Automotive|host>
                std::string name = a[2];
                for (size_t i = 3; i < a.size(); ++i)
                    name += " " + a[i];
                project->GetConfig().TargetPlatform = name == "host" ? std::string() : name;
                return true;
            }
            if (sub == "all")
            {
                // build all: modülü hazır tüm platformlar (Build All Ready Platforms)
                if (!m_BuildSettingsPanel->StartBuildAll(&err)) return false;
                m_ShowBuildSettings = true;
                return true;
            }
            if (sub == "results")
            {
                for (const auto& [platform, r] : BuildPipeline::Get().GetAllResults())
                    Automation::Report("BUILD_ALL " + platform + " " + (r.Success ? "ok" : "failed") + " exe=" + r.Executable.string() + " " + r.Error);
                return true;
            }
            if (sub == "start")
            {
                const bool run = a.size() > 2 && a[2] == "run";
                if (!m_BuildSettingsPanel->StartBuild(run, &err)) return false;
                m_ShowBuildSettings = true;
                return true;
            }
            auto& pipeline = BuildPipeline::Get();
            if (sub == "expect_done")
            {
                if (pipeline.IsRunning() || !pipeline.HasResult()) { err = "building: " + pipeline.GetStep(); return false; }
                return true;
            }
            if (sub == "expect_success" || sub == "report")
            {
                if (!pipeline.HasResult()) { err = "no build result"; return false; }
                const BuildReport r = pipeline.GetResult();
                char buf[512];
                std::snprintf(buf, sizeof(buf), "BUILD %s files=%zu bytes=%llu time=%.2fs exe=%s %s", r.Success ? "ok" : "failed", r.Files,
                              (unsigned long long)r.Bytes, r.Seconds, r.Executable.string().c_str(), r.Error.c_str());
                Automation::Report(buf);
                if (sub == "expect_success" && !r.Success) { err = r.Error; return false; }
                return true;
            }
            err = "unknown build command " + sub;
            return false;
        });
        // net host|server [port] | client <addr> [port] | stop | expect_connected | expect_mode <m> | expect_clients <n> |
        //     expect_objects <n> | expect_position <owner> <x|y|z> <above|below> <v> | expect_var <owner> <name> <equals|above|below> <v> |
        //     spawn <prefab> x y z [owner] | report
        Automation::RegisterCommand("net", [this](const Args& a, std::string& err) {
            auto& s = Net::NetworkSession::Get();
            if (a.size() < 2) { err = "net <host|server|client|stop|expect_*|spawn|report>"; return false; }
            const std::string& sub = a[1];
            Scene* scene = m_ActiveScene.get();
            if (sub == "host" || sub == "server")
            {
                if (EditorState::Get().IsEditing()) { err = "press play first"; return false; }
                const int port = a.size() > 2 ? std::stoi(a[2]) : 7777;
                if (!s.StartServer(scene, (uint16_t)port, sub == "host")) { err = s.GetLastError(); return false; }
                return true;
            }
            if (sub == "client" && a.size() > 2)
            {
                if (EditorState::Get().IsEditing()) { err = "press play first"; return false; }
                const int port = a.size() > 3 ? std::stoi(a[3]) : 7777;
                if (!s.StartClient(scene, a[2], (uint16_t)port)) { err = s.GetLastError(); return false; }
                return true;
            }
            if (sub == "stop") { s.Shutdown(); return true; }
            // wait_until ile: sunucu henüz açık değilse bağlantıyı tekrar dener; bağlanınca başarılı
            if (sub == "connect_retry" && a.size() > 2)
            {
                if (EditorState::Get().IsEditing()) { err = "press play first"; return false; }
                if (s.IsConnected() && s.GetMode() == Net::NetMode::Client) return true;
                if (s.GetMode() == Net::NetMode::Offline)
                    s.StartClient(scene, a[2], (uint16_t)(a.size() > 3 ? std::stoi(a[3]) : 7777));
                err = s.GetStatusText();
                return false;
            }
            if (sub == "expect_connected")
            {
                if (!s.IsConnected()) { err = "not connected: " + s.GetStatusText(); return false; }
                Automation::Report("NET " + s.GetStatusText());
                return true;
            }
            if (sub == "expect_mode" && a.size() > 2)
            {
                static const char* names[] = { "offline", "server", "host", "client" };
                const std::string cur = names[(int)s.GetMode()];
                if (cur != a[2]) { err = "mode is " + cur; return false; }
                return true;
            }
            if (sub == "expect_clients" && a.size() > 2)
            {
                const size_t n = (size_t)std::stoul(a[2]);
                if (s.GetClients().size() != n) { err = std::to_string(s.GetClients().size()) + " clients"; return false; }
                return true;
            }
            if (sub == "expect_objects" && a.size() > 2)
            {
                const size_t n = (size_t)std::stoul(a[2]);
                if (s.GetObjectCount() != n) { err = std::to_string(s.GetObjectCount()) + " network objects"; return false; }
                return true;
            }
            // Sahibe göre (oyuncu) ağ nesnesi: ilk eşleşen dinamik nesne
            auto findOwned = [&](uint32_t owner) -> Entity {
                if (!scene) return {};
                auto view = scene->m_Registry.view<NetworkObjectComponent>();
                for (auto e : view)
                {
                    const auto& no = view.get<NetworkObjectComponent>(e);
                    if (no.Spawned && no.OwnerClientId == owner && !no.Prefab.empty())
                        return Entity{ e, scene };
                }
                return {};
            };
            auto compare = [&](double value, const std::string& op, double target) {
                if (op == "above") return value > target;
                if (op == "below") return value < target;
                return std::abs(value - target) < 1e-4;
            };
            if (sub == "expect_position" && a.size() > 5)
            {
                Entity e = findOwned((uint32_t)std::stoul(a[2]));
                if (!e) { err = "no object owned by client " + a[2]; return false; }
                const glm::vec3 p = glm::vec3(e.GetComponent<TransformComponent>().WorldTransform[3]);
                const int axis = a[3] == "x" ? 0 : a[3] == "y" ? 1 : 2;
                Automation::Report("NETPOS owner " + a[2] + " " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
                if (!compare(p[axis], a[4], std::stod(a[5]))) { err = a[3] + "=" + std::to_string(p[axis]); return false; }
                return true;
            }
            if (sub == "expect_var" && a.size() > 5)
            {
                Entity e = findOwned((uint32_t)std::stoul(a[2]));
                if (!e) { err = "no object owned by client " + a[2]; return false; }
                Net::NetValue v;
                if (!s.GetVar((uint32_t)(entt::entity)e, a[3], v)) { err = "variable " + a[3] + " not set"; return false; }
                Automation::Report("NETVAR owner " + a[2] + " " + a[3] + " = " + (v.Kind == Net::NetValue::Type::String ? v.Text : std::to_string(v.Number)));
                if (!compare(v.Number, a[4], std::stod(a[5]))) { err = a[3] + "=" + std::to_string(v.Number); return false; }
                return true;
            }
            if (sub == "spawn" && a.size() > 5)
            {
                const uint32_t owner = a.size() > 6 ? (uint32_t)std::stoul(a[6]) : 0;
                if (s.Spawn(a[2], { std::stof(a[3]), std::stof(a[4]), std::stof(a[5]) }, owner) == UINT32_MAX) { err = s.GetLastError(); return false; }
                return true;
            }
            if (sub == "report")
            {
                const auto& st = s.GetStats();
                char buf[256];
                std::snprintf(buf, sizeof(buf), "NET %s objects=%zu clients=%zu sent=%llu recv=%llu rtt=%.1f", s.GetStatusText().c_str(), s.GetObjectCount(),
                              s.GetClients().size(), (unsigned long long)st.BytesSent, (unsigned long long)st.BytesReceived, st.RTTMs);
                Automation::Report(buf);
                return true;
            }
            err = "unknown net command " + sub;
            return false;
        });
        // profiler open|close|record on|off|view hierarchy|timeline|gpu|select latest|<offset>|expect_frames <n>|
        //          expect_sample <name>|expect_gpu|expect_category <name> <minMs>|export <path>|report
        // docs_dump <klasör>: belge üreteci için canlı kayıtları JSON olarak yaz (Tools/Docs/gen_docs.py okur)
        Automation::RegisterCommand("docs_dump", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "docs_dump <dir>"; return false; }
            const std::filesystem::path dir = a[1];
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto esc = [](const std::string& s) {
                std::string o;
                for (char ch : s)
                {
                    switch (ch)
                    {
                        case '"': o += "\\\""; break;
                        case '\\': o += "\\\\"; break;
                        case '\n': o += "\\n"; break;
                        case '\t': o += "\\t"; break;
                        default:
                            if ((unsigned char)ch < 0x20) o += ' ';
                            else o += ch;
                    }
                }
                return "\"" + o + "\"";
            };
            static const char* kinds[] = { "Bool", "Int", "Float", "Angle", "Vec2", "Vec3", "Vec4", "Color3", "Color4", "String", "Enum", "EntityRef", "LayerMask",
                                           "Asset", "Vec2List", "Vec3List" };
            // Bileşenler (varsayılan değerler geçici sahnedeki boş varlıktan)
            {
                auto scene = std::make_shared<Scene>();
                std::ofstream out(dir / "components.json");
                out << "[\n";
                bool first = true;
                for (auto& info : ComponentRegistry::All())
                {
                    Entity e = scene->CreateEntity("DocsProbe");
                    void* comp = nullptr;
                    if (info.Add && info.Get)
                    {
                        if (!(info.Has && info.Has(e)))
                            info.Add(e);
                        comp = info.Get(e);
                    }
                    out << (first ? "" : ",\n") << "{\"name\":" << esc(info.Name) << ",\"type\":" << esc(info.TypeName) << ",\"script\":" << esc(info.ScriptName)
                        << ",\"category\":" << esc(info.Category) << ",\"description\":" << esc(info.Description) << ",\"addMenu\":" << (info.AllowInAddMenu ? "true" : "false")
                        << ",\"requires\":[";
                    for (size_t i = 0; i < info.Requires.size(); ++i)
                        out << (i ? "," : "") << esc(info.Requires[i]);
                    out << "],\"fields\":[";
                    for (size_t i = 0; i < info.Fields.size(); ++i)
                    {
                        const auto& f = info.Fields[i];
                        const int k = (int)f.Kind;
                        out << (i ? "," : "") << "{\"name\":" << esc(f.Name) << ",\"label\":" << esc(f.Label) << ",\"kind\":" << esc(k >= 0 && k < 16 ? kinds[k] : "?")
                            << ",\"tooltip\":" << esc(f.Tooltip) << ",\"header\":" << esc(f.Header) << ",\"default\":"
                            << esc(comp && f.Kind != FieldKind::Vec2List && f.Kind != FieldKind::Vec3List ? ComponentRegistry::FieldToString(f, comp) : std::string()) << ",\"options\":[";
                        for (size_t j = 0; j < f.Options.size(); ++j)
                            out << (j ? "," : "") << esc(f.Options[j]);
                        out << "]}";
                    }
                    out << "]}";
                    first = false;
                    scene->DestroyEntity(e);
                }
                out << "\n]\n";
            }
            // Konsol değişkenleri ve komutları
            {
                std::ofstream out(dir / "console.json");
                out << "{\"variables\":[\n";
                bool first = true;
                for (const auto& name : ConsoleSystem::GetVariableNames())
                    if (ConsoleVariable* v = ConsoleSystem::FindVariable(name))
                    {
                        out << (first ? "" : ",\n") << "{\"name\":" << esc(v->Name) << ",\"description\":" << esc(v->Description) << ",\"value\":" << esc(v->Get ? v->Get() : "")
                            << "}";
                        first = false;
                    }
                out << "\n],\"commands\":[\n";
                first = true;
                for (const auto& name : ConsoleSystem::GetCommandNames())
                {
                    out << (first ? "" : ",\n") << "{\"name\":" << esc(name) << ",\"description\":" << esc(ConsoleSystem::GetCommandDescription(name)) << "}";
                    first = false;
                }
                out << "\n]}\n";
            }
            // Betik API'si (nativebridge)
            {
                std::ofstream out(dir / "natives.json");
                out << "[\n";
                bool first = true;
                for (const auto& [name, d] : Scripting::GetNativeDocs())
                {
                    out << (first ? "" : ",\n") << "{\"name\":" << esc(name) << ",\"signature\":" << esc(d.Signature) << ",\"description\":" << esc(d.Description)
                        << ",\"category\":" << esc(d.Category) << "}";
                    first = false;
                }
                out << "\n]\n";
            }
            // Otomasyon komutları
            {
                std::ofstream out(dir / "automation.json");
                out << "[";
                bool first = true;
                for (const auto& name : Automation::GetCommandNames())
                {
                    out << (first ? "" : ",") << esc(name);
                    first = false;
                }
                out << "]\n";
            }
            Automation::Report("DOCS dumped to " + dir.string());
            return true;
        });
        // render_stats: son çizilen görünümün işleme sayaçları (profiler penceresi açmadan; ekran görüntüsünü kapatmaz)
        Automation::RegisterCommand("render_stats", [](const Args&, std::string&) {
            const RendererStats& rs = Renderer::GetStats();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "RENDER draws=%u meshes=%u culled=%u shadowDraws=%u tris=%u transparent=%u",
                          rs.DrawCalls, rs.MeshCount, rs.CulledObjects, rs.ShadowDrawCalls, rs.Triangles, rs.TransparentDraws);
            Automation::Report(buf);
            return true;
        });
        Automation::RegisterCommand("profiler", [this](const Args& a, std::string& err) {
            using namespace Profiling;
            if (a.size() < 2 || !m_ProfilerPanel) { err = "profiler <open|close|record|view|select|expect_frames|expect_sample|expect_gpu|expect_category|export|report>"; return false; }
            const std::string& sub = a[1];
            const auto& frames = FrameProfiler::GetFrames();
            if (sub == "open") { m_ShowProfiler = true; m_PendingFocusWindow = "###Profiler"; Automation::WaitFrames(2); return true; }
            if (sub == "close") { m_ShowProfiler = false; Automation::WaitFrames(2); return true; }
            if (sub == "record" && a.size() > 2) { m_ProfilerPanel->SetRecordEnabled(a[2] == "on"); return true; }
            if (sub == "view" && a.size() > 2)
            {
                m_ProfilerPanel->SetView(a[2] == "timeline" ? ProfilerPanel::View::Timeline : a[2] == "gpu" ? ProfilerPanel::View::GPU : ProfilerPanel::View::Hierarchy);
                Automation::WaitFrames(2);
                return true;
            }
            if (sub == "select" && a.size() > 2)
            {
                if (a[2] == "latest") { m_ProfilerPanel->SelectLatest(); return true; }
                // en yeni kareden geriye doğru ofset
                long off = std::stol(a[2]);
                if (frames.empty() || off < 0 || off >= (long)frames.size()) { err = "no such frame"; return false; }
                return m_ProfilerPanel->SelectFrame(frames[frames.size() - 1 - (size_t)off].Index);
            }
            if (sub == "expect_frames" && a.size() > 2)
            {
                size_t n = (size_t)std::stoul(a[2]);
                Automation::Report("PROFILER frames=" + std::to_string(frames.size()));
                if (frames.size() < n) { err = "only " + std::to_string(frames.size()) + " frames recorded"; return false; }
                return true;
            }
            // Son birkaç karede ada göre örnek ara (oyun durumu her karede aynı olmayabilir)
            if (sub == "expect_sample" && a.size() > 2)
            {
                const size_t look = std::min<size_t>(frames.size(), 30);
                for (size_t i = 0; i < look; ++i)
                {
                    const auto& f = frames[frames.size() - 1 - i];
                    for (const auto& s : f.Samples)
                        if (a[2] == s.Name)
                        {
                            Automation::Report("PROFILER sample " + a[2] + " " + std::to_string((double)s.DurationNs / 1.0e6) + " ms (frame " + std::to_string(f.Index) + ")");
                            return true;
                        }
                }
                err = "sample '" + a[2] + "' not found in the last " + std::to_string(look) + " frames";
                return false;
            }
            if (sub == "expect_gpu")
            {
                for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                    if (it->GPUMs >= 0.0)
                    {
                        std::string passes;
                        for (const auto& g : it->GPUSamples)
                            passes += std::string(" ") + g.Name + "=" + std::to_string(g.DurationMs);
                        Automation::Report("PROFILER gpu frame " + std::to_string(it->Index) + " " + std::to_string(it->GPUMs) + " ms" + passes);
                        return true;
                    }
                err = "no GPU timings resolved";
                return false;
            }
            if (sub == "expect_category" && a.size() > 3)
            {
                const float minMs = std::stof(a[3]);
                for (size_t c = 0; c < (size_t)Category::Count; ++c)
                {
                    if (a[2] != FrameProfiler::CategoryName((Category)c))
                        continue;
                    float best = 0.0f;
                    for (size_t i = 0; i < std::min<size_t>(frames.size(), 30); ++i)
                        best = std::max(best, frames[frames.size() - 1 - i].CategoryMs[c]);
                    Automation::Report("PROFILER category " + a[2] + " max " + std::to_string(best) + " ms");
                    if (best < minMs) { err = a[2] + " below " + a[3] + " ms"; return false; }
                    return true;
                }
                err = "unknown category " + a[2];
                return false;
            }
            if (sub == "export" && a.size() > 2)
            {
                if (!FrameProfiler::ExportChromeTrace(a[2])) { err = "export failed"; return false; }
                return true;
            }
            if (sub == "report")
            {
                if (frames.empty()) { err = "no frames"; return false; }
                const auto& f = frames.back();
                char buf[320];
                const RendererStats& rs = Renderer::GetStats();
                std::snprintf(buf, sizeof(buf), "PROFILER frame %llu cpu=%.2fms gpu=%.2fms samples=%zu draws=%.0f tris=%.0f meshes=%u culled=%u shadowDraws=%u",
                              (unsigned long long)f.Index, f.CPUMs, f.GPUMs, f.Samples.size(), f.Counters[(size_t)Counter::DrawCalls], f.Counters[(size_t)Counter::Triangles],
                              rs.MeshCount, rs.CulledObjects, rs.ShadowDrawCalls);
                Automation::Report(buf);
                return true;
            }
            err = "unknown profiler command " + sub;
            return false;
        });
        Automation::RegisterCommand("stop", [this](const Args&, std::string&) { OnStopButtonPressed(); return true; });
        // content_drop <yol...>: dosya yöneticisinden bırakma (işletim sistemi olayıyla aynı yol)
        Automation::RegisterCommand("content_drop", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ContentBrowserPanel) { err = "content_drop <path...>"; return false; }
            m_ContentBrowserPanel->QueueDroppedFiles(std::vector<std::string>(a.begin() + 1, a.end()));
            Automation::WaitFrames(2);
            return true;
        });
        // expect_state edit|play|pause: oynatma durumu
        Automation::RegisterCommand("expect_state", [](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "expect_state edit|play|pause"; return false; }
            auto& st = EditorState::Get();
            const char* now = st.IsEditing() ? "edit" : st.IsPaused() ? "pause" : "play";
            Automation::Report(std::string("STATE ") + now);
            if (a[1] != now) { err = std::string("state is ") + now; return false; }
            return true;
        });
        // window_float <ad> <x> <y> <g> <y>: paneli yuvadan çıkarır; window_size <ad> <g> <y>: yüzen pencereyi boyutlandırır
        Automation::RegisterCommand("window_float", [](const Args& a, std::string& err) {
            if (a.size() < 6) { err = "window_float <name> <x> <y> <w> <h>"; return false; }
            ImGuiWindow* w = nullptr;
            for (ImGuiWindow* c : GImGui->Windows)
                if (!std::strchr(c->Name, '/') && std::strncmp(c->Name, a[1].c_str(), a[1].size()) == 0) { w = c; break; }
            if (!w) { err = "no window " + a[1]; return false; }
            ImGui::DockContextQueueUndockWindow(GImGui, w);
            ImGui::SetWindowPos(w, ImVec2(std::stof(a[2]), std::stof(a[3])));
            ImGui::SetWindowSize(w, ImVec2(std::stof(a[4]), std::stof(a[5])));
            Automation::WaitFrames(3);
            return true;
        });
        Automation::RegisterCommand("window_size", [](const Args& a, std::string& err) {
            if (a.size() < 4) { err = "window_size <name> <w> <h>"; return false; }
            for (ImGuiWindow* c : GImGui->Windows)
                if (!std::strchr(c->Name, '/') && std::strncmp(c->Name, a[1].c_str(), a[1].size()) == 0)
                {
                    ImGui::SetWindowSize(c, ImVec2(std::stof(a[2]), std::stof(a[3])));
                    return true;
                }
            err = "no window " + a[1];
            return false;
        });
        // viewport_camera mark | expect_moved <en az mesafe>: sahne görünümü kamerası hareket etti mi
        Automation::RegisterCommand("viewport_camera", [this](const Args& a, std::string& err) {
            static glm::vec3 s_Mark(0.0f);
            if (a.size() < 2 || !m_ViewportPanel) { err = "viewport_camera mark|expect_moved <distance>"; return false; }
            const glm::vec3 p = m_ViewportPanel->GetEditorCamera().GetPosition();
            if (a[1] == "mark") { s_Mark = p; return true; }
            if (a[1] == "expect_moved" && a.size() > 2)
            {
                const float d = glm::distance(p, s_Mark);
                Automation::Report("VIEWPORT CAMERA moved " + std::to_string(d));
                if (d < std::stof(a[2])) { err = "moved only " + std::to_string(d); return false; }
                return true;
            }
            err = "unknown viewport_camera command";
            return false;
        });
        Automation::RegisterCommand("focus", [this](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "focus <window name>"; return false; }
            m_PendingFocusWindow = a[1];
            Automation::WaitFrames(2);
            return true;
        });
        Automation::RegisterCommand("panel", [this](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "panel <name> [on|off]"; return false; }
            std::string n = a[1];
            bool on = a.size() < 3 || (a[2] != "off" && a[2] != "0");
            std::string lower = n;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (lower == "animator") { if (m_AnimatorPanel) m_AnimatorPanel->SetOpen(on); return true; }
            if (lower == "codeeditor" || lower == "code editor") { if (m_CodeEditorPanel) m_CodeEditorPanel->GetIsOpen() = on; return true; }
            if (lower == "visualscript" || lower == "visual script") { if (m_VisualScriptPanel) m_VisualScriptPanel->GetIsOpen() = on; return true; }
            bool* flag = FindPanelVisibilityFlag(n);
            if (!flag) { err = "unknown panel '" + n + "'"; return false; }
            *flag = on;
            Automation::WaitFrames(2);
            return true;
        });
        Automation::RegisterCommand("new_scene", [this](const Args&, std::string&) { NewScene(); return true; });
        // record_start [mp4|webm|gif|png] [fps=<n>] [quality=<0..1>] [fixed] [out=<dir>]: Game görünümünü kaydet
        // (fixed: çevrimdışı sabit adım, her kare bir video karesi)
        Automation::RegisterCommand("record_start", [this](const Args& a, std::string& err) {
            if (!m_GamePanel) { err = "no game panel"; return false; }
            auto& rs = Recorder::Settings();
            rs.FixedTimeStep = false;
            for (size_t i = 1; i < a.size(); ++i)
            {
                const std::string& f = a[i];
                if (f == "mp4" || f == "webm" || f == "gif" || f == "png")
                    rs.Format = f == "webm" ? RecordingFormat::WebM : f == "gif" ? RecordingFormat::GIF : f == "png" ? RecordingFormat::PNGSequence : RecordingFormat::MP4;
                else if (f == "fixed")
                    rs.FixedTimeStep = true;
                else if (f.rfind("fps=", 0) == 0)
                    rs.FrameRate = std::clamp(std::atoi(f.c_str() + 4), 1, 240);
                else if (f.rfind("quality=", 0) == 0)
                    rs.Quality = std::clamp((float)std::atof(f.c_str() + 8), 0.0f, 1.0f);
                else if (f.rfind("out=", 0) == 0)
                    rs.OutputDirectory = f.substr(4);
                else { err = "unknown record_start option " + f; return false; }
            }
            glm::vec2 size = m_GamePanel->GetViewportSize();
            if (!Recorder::Start((uint32_t)size.x, (uint32_t)size.y)) { err = Recorder::GetLastError(); return false; }
            return true;
        });
        Automation::RegisterCommand("record_stop", [](const Args&, std::string&) { Recorder::Stop(); Automation::Report("RECORDED " + Recorder::GetLastOutput()); return true; });
        Automation::RegisterCommand("animator_open", [this](const Args& a, std::string& err) {
            if (!m_AnimatorPanel || a.size() < 2) { err = "animator_open <controller path>"; return false; }
            std::filesystem::path p(a[1]);
            if (p.is_relative() && Project::GetActive())
                p = Project::GetActiveProjectDirectory() / p;
            m_AnimatorPanel->OpenController(p.string());
            m_AnimatorPanel->SetOpen(true);
            Automation::WaitFrames(3);
            return m_AnimatorPanel->GetController() != nullptr;
        });
        Automation::RegisterCommand("animator_select", [this](const Args& a, std::string& err) {
            if (!m_AnimatorPanel || a.size() < 2) { err = "animator_select <node>"; return false; }
            m_AnimatorPanel->SelectNode(a[1]);
            return true;
        });
        // anim_param <entity> <param> <value>  (aktif sahnedeki çalışma zamanı değeri)
        Automation::RegisterCommand("anim_param", [this](const Args& a, std::string& err) {
            if (a.size() < 4 || !m_ActiveScene) { err = "anim_param <entity> <param> <value>"; return false; }
            Entity e = m_ActiveScene->FindEntityByTag(a[1]);
            if (!e) m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& t) { if (!e && t.Tag == a[1]) e = Entity{ id, m_ActiveScene.get() }; });
            if (!e || !e.HasComponent<AnimationComponent>()) { err = "no AnimationComponent on '" + a[1] + "'"; return false; }
            auto& ac = e.GetComponent<AnimationComponent>();
            for (auto& p : ac.Parameters)
            {
                if (p.Name != a[2]) continue;
                float v = std::stof(a[3]);
                p.FloatValue = v; p.IntValue = (int)v; p.BoolValue = v > 0.5f;
                return true;
            }
            err = "parameter '" + a[2] + "' not found";
            return false;
        });
        Automation::RegisterCommand("anim_expect", [this](const Args& a, std::string& err) {
            // anim_expect <entity> <param> <value | >v | <v>: Animation parametresinin değeri (bool 0/1)
            if (a.size() < 4 || !m_ActiveScene) { err = "anim_expect <entity> <param> <value>"; return false; }
            Entity e{};
            m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto id, auto& t) { if (!e && t.Tag == a[1]) e = Entity{ id, m_ActiveScene.get() }; });
            if (!e || !e.HasComponent<AnimationComponent>()) { err = "no AnimationComponent on '" + a[1] + "'"; return false; }
            for (auto& p : e.GetComponent<AnimationComponent>().Parameters)
            {
                if (p.Name != a[2]) continue;
                const float have = p.Type == AnimParameterType::Float ? p.FloatValue : p.Type == AnimParameterType::Int ? (float)p.IntValue : (p.BoolValue ? 1.0f : 0.0f);
                Automation::Report("ANIM " + a[1] + "." + a[2] + " = " + std::to_string(have));
                const std::string& w = a[3];
                bool ok = w[0] == '>' ? have > std::stof(w.substr(1)) : w[0] == '<' ? have < std::stof(w.substr(1)) : std::fabs(have - std::stof(w)) < 1e-3f;
                if (!ok) { err = "value is " + std::to_string(have); return false; }
                return true;
            }
            err = "parameter '" + a[2] + "' not found";
            return false;
        });
        // camera <distance> <pitchDeg> <yawDeg> [fx fy fz]
        Automation::RegisterCommand("camera", [this](const Args& a, std::string& err) {
            if (!m_ViewportPanel || a.size() < 4) { err = "camera <distance> <pitchDeg> <yawDeg> [fx fy fz]"; return false; }
            auto& cam = m_ViewportPanel->GetEditorCamera();
            if (a.size() >= 7)
                cam.SetFocalPoint({ std::stof(a[4]), std::stof(a[5]), std::stof(a[6]) });
            cam.SetPitch(glm::radians(std::stof(a[2])));
            cam.SetYaw(glm::radians(std::stof(a[3])));
            cam.SetDistance(std::stof(a[1]));
            Automation::WaitFrames(2);
            return true;
        });
        // camera_fly <sağ> <yukarı> <ileri> <kare>: editör kamerası verilen hızla (m / sn) her karede sürekli hareket eder, sonra
        // anında durur (sağ tık + Shift+W ile hızlı ileri uçuşun aynısı; camera komutu adım başına 2 kare beklediği için dur-kalk olur)
        Automation::RegisterCommand("camera_fly", [this](const Args& a, std::string& err) {
            if (!m_ViewportPanel || a.size() < 5) { err = "camera_fly <right> <up> <forward> <frames>"; return false; }
            s_AutomationFly.LocalVelocity = { std::stof(a[1]), std::stof(a[2]), std::stof(a[3]) };
            s_AutomationFly.Frames = std::max(0, std::stoi(a[4]));
            // Beklemez: hareket sürerken sonraki komutlar (her karede screenshot) çalışabilsin
            return true;
        });
        // camera_spin <yaw derece/sn> <pitch derece/sn> <kare>: editör kamerası her karede sabit hızla döner, sonra anında durur
        // (sabit 1/60 sn adım: döndürürken TAA'nın davranışını ölçmek için)
        Automation::RegisterCommand("camera_spin", [this](const Args& a, std::string& err) {
            if (!m_ViewportPanel || a.size() < 4) { err = "camera_spin <yawDegPerSec> <pitchDegPerSec> <frames> [inplace]"; return false; }
            s_AutomationSpin.RateDeg = { std::stof(a[1]), std::stof(a[2]) };
            s_AutomationSpin.Frames = std::max(0, std::stoi(a[3]));
            s_AutomationSpin.InPlace = a.size() >= 5 && a[4] == "inplace";
            return true;
        });
        Automation::RegisterCommand("camera_info", [this](const Args&, std::string& err) {
            if (!m_ViewportPanel) { err = "no viewport"; return false; }
            auto& c = m_ViewportPanel->GetEditorCamera();
            char buf[256];
            snprintf(buf, sizeof(buf), "CAMERA mode=%d pos=(%.2f,%.2f,%.2f) focal=(%.2f,%.2f,%.2f) dist=%.2f pitch=%.3f yaw=%.3f ortho=%.2f clip=%.3f..%.1f",
                     (int)c.GetCameraMode(), c.GetPosition().x, c.GetPosition().y, c.GetPosition().z, c.GetFocalPoint().x, c.GetFocalPoint().y,
                     c.GetFocalPoint().z, c.GetDistance(), c.GetPitch(), c.GetYaw(), c.GetOrthographicSize(), c.GetNearClip(), c.GetFarClip());
            Automation::Report(buf);
            return true;
        });
        Automation::RegisterCommand("camera_mode", [this](const Args& a, std::string& err) {
            if (!m_ViewportPanel || a.size() < 2) { err = "camera_mode 2d|3d"; return false; }
            m_ViewportPanel->GetEditorCamera().SetCameraMode(a[1] == "2d" ? EditorCameraMode::Orthographic2D : EditorCameraMode::Perspective3D);
            Automation::WaitFrames(2);
            return true;
        });
        // gizmo_drag <x|y|z> <piksel>: seçili nesnenin taşıma okunu ekranda bulup eksen yönünde sürükler
        // (UI öğeleri yerel eksenlerle). Gizmo davranışının gerçek fareyle testi için.
        Automation::RegisterCommand("gizmo_drag", [this](const Args& a, std::string& err) {
            if (a.size() < 3 || !m_ViewportPanel) { err = "gizmo_drag <x|y|z> <pixels>"; return false; }
            Entity e = EditorState::Get().GetSelectedEntity();
            if (!e || !e.HasComponent<TransformComponent>()) { err = "nothing selected"; return false; }
            const glm::vec2* b = EditorState::Get().GetViewportBounds();
            const glm::vec2 size = b[1] - b[0];
            auto& cam = m_ViewportPanel->GetEditorCamera();
            const glm::mat4 vp = cam.GetViewProjection();
            const glm::mat4& world = e.GetComponent<TransformComponent>().WorldTransform;
            const glm::vec3 p = glm::vec3(world[3]);
            const int axis = a[1] == "y" ? 1 : (a[1] == "z" ? 2 : 0);
            glm::vec3 dir(0.0f);
            dir[axis] = 1.0f;
            if (e.HasComponent<RectTransformComponent>())
                dir = glm::normalize(glm::vec3(world[axis]));
            auto toScreen = [&](const glm::vec3& w) {
                glm::vec4 c = vp * glm::vec4(w, 1.0f);
                glm::vec2 n = glm::vec2(c) / c.w;
                return glm::vec2(b[0].x + (n.x * 0.5f + 0.5f) * size.x, b[0].y + (0.5f - n.y * 0.5f) * size.y);
            };
            const glm::vec2 s0 = toScreen(p);
            glm::vec2 sd = toScreen(p + dir * std::max(cam.GetDistance(), 1.0f) * 0.05f) - s0;
            if (glm::length(sd) < 1e-4f) { err = "axis points at the camera"; return false; }
            sd = glm::normalize(sd);
            const float pixels = std::stof(a[2]);
            const glm::vec2 grab = s0 + sd * 30.0f;   // viewport sınırları zaten mutlak ekran koordinatı
            Automation::QueueMouseDrag(grab.x, grab.y, grab.x + sd.x * pixels, grab.y + sd.y * pixels, 24, 0);
            return true;
        });
        // skybox <equirect hdr/png yolu | none>: Lighting panelindeki gibi gökyüzü yükler
        Automation::RegisterCommand("skybox", [this](const Args& a, std::string& err) {
            if (a.size() < 2 || !m_ActiveScene) { err = "skybox <path|none>"; return false; }
            if (a[1] == "none")
            {
                m_ActiveScene->SetSkybox(nullptr);
                return true;
            }
            std::string path = a[1];
            if (std::filesystem::path(path).is_relative() && Project::GetActive())
            {
                auto candidate = Project::GetActiveProjectDirectory() / path;
                if (std::filesystem::exists(candidate)) path = candidate.string();
            }
            auto cube = Cubemap::CreateFromEquirectangular(path, 1024);
            if (!cube || !cube->GetRendererID()) { err = "cannot load skybox " + path; return false; }
            m_ActiveScene->SetSkybox(cube);
            m_ActiveScene->InvalidateReflectionProbes();
            Automation::WaitFrames(3);
            return true;
        });
        Automation::RegisterCommand("ortho_size", [this](const Args& a, std::string& err) {
            if (!m_ViewportPanel || a.size() < 2) { err = "ortho_size <size>"; return false; }
            m_ViewportPanel->GetEditorCamera().SetOrthographicSize(std::stof(a[1]));
            return true;
        });
        Automation::RegisterCommand("save_scene", [this](const Args&, std::string&) { SaveScene(); return true; });
        // expect_unsaved <true|false>: kaydedilmemiş değişiklik algılaması (kapatma sorusu bunu kullanır)
        Automation::RegisterCommand("expect_unsaved", [this](const Args& a, std::string& err) {
            const bool want = a.size() > 1 && (a[1] == "true" || a[1] == "1");
            const bool got = HasUnsavedChanges();
            Automation::Report(std::string("UNSAVED ") + (got ? "true" : "false"));
            if (got != want) { err = std::string("unsaved changes: ") + (got ? "true" : "false"); return false; }
            return true;
        });
        // save_scene_as <yol>: iletişim kutusu olmadan farklı kaydet
        Automation::RegisterCommand("save_scene_as", [this](const Args& a, std::string& err) {
            if (a.size() < 2) { err = "save_scene_as <path>"; return false; }
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(a[1]).parent_path(), ec);
            m_CurrentScenePath = a[1];
            SaveScene();
            return std::filesystem::exists(m_CurrentScenePath);
        });
        // write_recovery: otomatik kaydı hemen çalıştır (zamanlayıcıyı beklemeden)
        Automation::RegisterCommand("write_recovery", [this](const Args&, std::string&) { WriteRecoverySnapshot(); return true; });
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
                        if (m_AnimatorPanel) m_AnimatorPanel->SetContext(m_ActiveScene.get());
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
            // Mevcut .cqproj dosyasının üzerine (ürün adı dosya adından farklı olabilir)
            std::filesystem::path projectFilePath = Project::GetActiveProjectFile();
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
        // İçerik tarayıcısı odaktaysa kopyala/sil/F2 gibi varlık kısayolları dosyalara uygulanır (panel kendisi işler).
        // Kendi seçimi olan pencereler (Animation: anahtarlar, Timeline: klipler, Animator: durumlar, Sprite Editor, Input
        // Actions) odaktayken Delete / Ctrl+C / Ctrl+D... o pencereye aittir; sahnedeki nesne silinmez (Unity gibi)
        bool panelOwnsKeys = false;
        for (ImGuiWindow* w = GImGui->NavWindow; w && !panelOwnsKeys; w = w->ParentWindow)
            for (const char* id : { "###AnimationWindow", "###Timeline", "###Animator", "###Sprite Editor", "###InputActionsEditor" })
                if (std::strstr(w->Name, id))
                    panelOwnsKeys = true;
        const bool assetFocus = (m_ContentBrowserPanel && m_ContentBrowserPanel->IsFocused()) || panelOwnsKeys;

        // Unity oynatma kısayolları: Ctrl+P Play / Stop, Ctrl+Shift+P Pause / Resume, Ctrl+Alt+P Step (oyun görünümü odaktayken de)
        if (ctrl && !textActive && ImGui::IsKeyPressed(ImGuiKey_P, false))
        {
            auto& state = EditorState::Get();
            const bool editing = state.IsEditing(), paused = state.IsPaused(), playing = state.IsPlaying();
            if (io.KeyAlt)
            {
                if (!editing)
                {
                    if (playing) OnPauseButtonPressed();
                    m_StepFrame = true;
                }
            }
            else if (shift)
            {
                if (!editing)
                {
                    if (paused) OnPlayButtonPressed();
                    else OnPauseButtonPressed();
                }
            }
            else if (editing) OnPlayButtonPressed();
            else OnStopButtonPressed();
            return;
        }
        // Oynarken oyun görünümü odaktaysa tuşlar oyunundur: Ctrl+A / Ctrl+D (çömelip yürümek) sahnedeki nesneleri seçmez, çoğaltmaz
        if (!EditorState::Get().IsEditing() && m_GamePanel && (m_GamePanel->IsViewportFocused() || m_GameMouseCaptured))
            return;

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
        else if (ctrl && !textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            EditCopy();
        }
        else if (ctrl && !textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_X))
        {
            EditCut();
        }
        else if (ctrl && !textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_V))
        {
            EditPaste();
        }
        else if (ctrl && !textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            EditDuplicate();
        }
        else if (ctrl && !textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_A))
        {
            EditSelectAll();
        }
        else if (!textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            auto& state = EditorState::Get();
            if (state.HasSelection())
            {
                if (m_HierarchyPanel)
                    m_HierarchyPanel->StartRename(state.GetSelectedEntity());
            }
        }
        else if (!textActive && !assetFocus && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            auto& state = EditorState::Get();
            if (state.HasSelection())
                EditDelete();
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_N))
        {
            RequestNewScene();
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
        else if (ctrl && shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_B, false))
        {
            // Unity: Ctrl+Shift+B Build Settings
            m_ShowBuildSettings = true;
            m_PendingFocusWindow = "###BuildSettings";
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_6, false))
        {
            // Unity: Ctrl+6 Animation
            m_ShowAnimation = !m_ShowAnimation;
            if (m_ShowAnimation)
                m_PendingFocusWindow = "###AnimationWindow";
        }
        else if (ctrl && !shift && !textActive && ImGui::IsKeyPressed(ImGuiKey_7, false))
        {
            // Unity: Ctrl+7 Profiler
            m_ShowProfiler = !m_ShowProfiler;
            if (m_ShowProfiler)
                m_PendingFocusWindow = "###Profiler";
        }
        else if (!ctrl && !io.KeyAlt && !textActive && m_ViewportPanel && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && EditorState::Get().IsEditing())
        {
            // Unity araç kısayolları (sağ tık ile uçarken WASD/QE kamera içindir)
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_ViewportPanel->SetGizmoType(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_ViewportPanel->SetGizmoType(ImGuizmo::TRANSLATE);
            else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_ViewportPanel->SetGizmoType(ImGuizmo::ROTATE);
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_ViewportPanel->SetGizmoType(ImGuizmo::SCALE);
            else if (ImGui::IsKeyPressed(ImGuiKey_T, false)) m_ViewportPanel->SetGizmoType(ViewportPanel::kGizmoRect);
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) m_ViewportPanel->SetGizmoType(ImGuizmo::UNIVERSAL);
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
        m_PanelSceneContext = m_ActiveScene.get();
        if (m_MultiplayerPanel) m_MultiplayerPanel->SetContext(m_ActiveScene);
        if (m_TimelinePanel) m_TimelinePanel->SetContext(m_ActiveScene);
        if (m_AnimationWindow) m_AnimationWindow->SetContext(m_ActiveScene);
        if (m_HierarchyPanel) m_HierarchyPanel->SetContext(m_ActiveScene);
        if (m_InspectorPanel) m_InspectorPanel->SetContext(m_ActiveScene.get());
        if (m_ViewportPanel) m_ViewportPanel->SetContext(m_ActiveScene);
        if (m_GamePanel) m_GamePanel->SetContext(m_ActiveScene);
        if (m_LightingPanel) m_LightingPanel->SetContext(m_ActiveScene);
        if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->SetContext(m_ActiveScene);
        if (m_AnimatorPanel) m_AnimatorPanel->SetContext(m_ActiveScene.get());
        if (m_ShaderEditorPanel) m_ShaderEditorPanel->SetContext(m_ActiveScene.get());
        if (m_AIPanel) m_AIPanel->SetContext(m_ActiveScene);
        if (m_TerrainPanel) m_TerrainPanel->SetContext(m_ActiveScene.get());
        if (m_TilePalettePanel) m_TilePalettePanel->SetContext(m_ActiveScene.get());
        // Başka sahneye ait seçim (silinmiş olabilir) bırakılır; yalnızca işaretçi karşılaştırılır
        bool stale = false;
        for (auto& e : EditorState::Get().GetSelectedEntities())
            stale |= e.GetScene() != m_ActiveScene.get();
        if (stale)
            SelectEntityEverywhere({});
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

        // Animator seçimini de temizle
        {
            auto& selInfo = AnimatorPanel::GetSelectionInfo();
            selInfo.HasSelection = false;
            selInfo.IsState = false;
            selInfo.IsTransition = false;
            selInfo.StateName.clear();
            selInfo.TransitionFrom.clear();
            selInfo.TransitionTo.clear();
        }

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
