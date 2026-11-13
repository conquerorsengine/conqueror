#include "HierarchyPanel.h"
#include "Core/Math/Math.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "AISystem/Components/NavComponents.h"
#include "Renderer/Renderer2D.h"
#include "EditorState.h"
#include "UndoRedo/UndoRedoManager.h"
#include "UI/EditorUI.h"
#include "TerrainPanel.h"
#include "TilePalettePanel.h"
#include "AssetActions.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Project/Project.h"
#include "UI/UISystem.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Conqueror::Editor
{
    namespace
    {
        constexpr ImVec4 kColLight   = ImVec4(1.00f, 0.82f, 0.30f, 1.0f);
        constexpr ImVec4 kColCamera  = ImVec4(0.45f, 0.75f, 1.00f, 1.0f);
        constexpr ImVec4 kColMesh    = ImVec4(0.62f, 0.70f, 0.85f, 1.0f);
        constexpr ImVec4 kCol2D      = ImVec4(0.55f, 0.85f, 0.65f, 1.0f);
        constexpr ImVec4 kColUI      = ImVec4(0.35f, 0.85f, 0.85f, 1.0f);
        constexpr ImVec4 kColPhysics = ImVec4(0.55f, 0.90f, 0.40f, 1.0f);
        constexpr ImVec4 kColAudio   = ImVec4(1.00f, 0.60f, 0.30f, 1.0f);
        constexpr ImVec4 kColScript  = ImVec4(0.75f, 0.55f, 1.00f, 1.0f);
        constexpr ImVec4 kColAnim    = ImVec4(1.00f, 0.50f, 0.65f, 1.0f);
        constexpr ImVec4 kColProbe   = ImVec4(0.65f, 0.85f, 1.00f, 1.0f);
        constexpr ImVec4 kColNav     = ImVec4(0.40f, 0.80f, 0.75f, 1.0f);
        constexpr ImVec4 kColEmpty   = ImVec4(0.60f, 0.62f, 0.66f, 1.0f);

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        Entity FindOrCreateCanvas(Scene* scene)
        {
            auto canvasView = scene->m_Registry.view<CanvasComponent>();
            if (!canvasView.empty())
                return Entity{ *canvasView.begin(), scene };
            Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>();
            canvas.AddComponent<CanvasScalerComponent>();
            canvas.AddComponent<GraphicRaycasterComponent>();
            return canvas;
        }

        void MakeUI(Entity e)
        {
            if (e.HasComponent<LayerComponent>())
                e.GetComponent<LayerComponent>().Layer = 5; // UI katmanı
        }
    }

    // ------------------------------------------------------------------ İkonlar

    HierarchyPanel::ComponentIcon HierarchyPanel::GetPrimaryIcon(Entity entity)
    {
        if (entity.HasComponent<CameraComponent>()) return { ICON_FA_VIDEO, kColCamera, "Camera" };
        if (entity.HasComponent<DirectionalLightComponent>()) return { ICON_FA_SUN, kColLight, "Directional Light" };
        if (entity.HasComponent<PointLightComponent>()) return { ICON_FA_LIGHTBULB, kColLight, "Point Light" };
        if (entity.HasComponent<SpotLightComponent>()) return { ICON_FA_LIGHTBULB, kColLight, "Spot Light" };
        if (entity.HasComponent<CanvasComponent>()) return { ICON_FA_TABLE_CELLS_LARGE, kColUI, "Canvas" };
        if (entity.HasComponent<ButtonComponent>()) return { ICON_FA_HAND_POINTER, kColUI, "Button" };
        if (entity.HasComponent<TextRendererComponent>()) return { ICON_FA_FONT, kColUI, "Text" };
        if (entity.HasComponent<TerrainComponent>()) return { ICON_FA_MOUNTAIN_SUN, kCol2D, "Terrain" };
        if (entity.HasComponent<ParticleSystemComponent>()) return { ICON_FA_FIRE, kColAudio, "Particle System" };
        if (entity.HasComponent<MeshRendererComponent>())
            return { entity.GetComponent<MeshRendererComponent>().IsModel() ? ICON_FA_CUBES : ICON_FA_CUBE, kColMesh, "Mesh Renderer" };
        if (entity.HasComponent<SpriteRendererComponent>()) return { ICON_FA_SHAPES, kCol2D, "Sprite Renderer" };
        if (entity.HasComponent<AudioSourceComponent>()) return { ICON_FA_VOLUME_HIGH, kColAudio, "Audio Source" };
        if (entity.HasComponent<AudioListenerComponent>()) return { ICON_FA_EAR_LISTEN, kColAudio, "Audio Listener" };
        if (entity.HasComponent<ReflectionProbeComponent>()) return { ICON_FA_GLOBE, kColProbe, "Reflection Probe" };
        if (entity.HasComponent<AdaptiveProbeVolumeComponent>()) return { ICON_FA_BRAILLE, kColProbe, "Probe Volume" };
        if (entity.HasComponent<NavMeshSurfaceComponent>()) return { ICON_FA_ROUTE, kColNav, "NavMesh Surface" };
        if (entity.HasComponent<CharacterControllerComponent>()) return { ICON_FA_PERSON_WALKING, kColPhysics, "Character Controller" };
        return { entity.HasChildren() ? ICON_FA_LAYER_GROUP : ICON_FA_CIRCLE_DOT, kColEmpty, "Empty" };
    }

    void HierarchyPanel::GetComponentIcons(Entity entity, std::vector<ComponentIcon>& out)
    {
        out.clear();
        const char* primary = GetPrimaryIcon(entity).Name;
        auto add = [&](bool has, ComponentIcon icon) {
            if (has && std::strcmp(icon.Name, primary) != 0)
                out.push_back(icon);
        };
        add(entity.HasComponent<MeshRendererComponent>(), { ICON_FA_CUBE, kColMesh, "Mesh Renderer" });
        add(entity.HasComponent<SpriteRendererComponent>(), { ICON_FA_SHAPES, kCol2D, "Sprite Renderer" });
        add(entity.HasComponent<AnimationComponent>() || entity.HasComponent<AnimatorComponent>(), { ICON_FA_PERSON_RUNNING, kColAnim, "Animator" });
        add(entity.HasComponent<RigidbodyComponent>(), { ICON_FA_WEIGHT_HANGING, kColPhysics, "Rigidbody" });
        add(entity.HasComponent<RigidBody2DComponent>(), { ICON_FA_WEIGHT_HANGING, kColPhysics, "Rigidbody 2D" });
        add(entity.HasComponent<BoxColliderComponent>() || entity.HasComponent<SphereColliderComponent>() ||
            entity.HasComponent<CapsuleColliderComponent>() || entity.HasComponent<MeshColliderComponent>(),
            { ICON_FA_VECTOR_SQUARE, kColPhysics, "Collider" });
        add(entity.HasComponent<BoxCollider2DComponent>() || entity.HasComponent<CircleCollider2DComponent>(),
            { ICON_FA_VECTOR_SQUARE, kColPhysics, "Collider 2D" });
        add(entity.HasComponent<NativeScriptComponent>() || entity.HasComponent<ConquerorScriptComponent>(), { ICON_FA_FILE_CODE, kColScript, "Script" });
        add(entity.HasComponent<AudioSourceComponent>(), { ICON_FA_VOLUME_HIGH, kColAudio, "Audio Source" });
        add(entity.HasComponent<AudioListenerComponent>(), { ICON_FA_EAR_LISTEN, kColAudio, "Audio Listener" });
        add(entity.HasComponent<AudioReverbZoneComponent>(), { ICON_FA_MUSIC, kColAudio, "Reverb Zone" });
        add(entity.HasComponent<CameraComponent>(), { ICON_FA_VIDEO, kColCamera, "Camera" });
        add(entity.HasComponent<DirectionalLightComponent>(), { ICON_FA_SUN, kColLight, "Directional Light" });
        add(entity.HasComponent<PointLightComponent>(), { ICON_FA_LIGHTBULB, kColLight, "Point Light" });
        add(entity.HasComponent<SpotLightComponent>(), { ICON_FA_LIGHTBULB, kColLight, "Spot Light" });
        add(entity.HasComponent<TextRendererComponent>(), { ICON_FA_FONT, kColUI, "Text" });
        add(entity.HasComponent<ButtonComponent>(), { ICON_FA_HAND_POINTER, kColUI, "Button" });
        add(entity.HasComponent<NavMeshAgentComponent>(), { ICON_FA_ROUTE, kColNav, "NavMesh Agent" });
        add(entity.HasComponent<NavMeshObstacleComponent>(), { ICON_FA_ROUTE, kColNav, "NavMesh Obstacle" });
        add(entity.HasComponent<ReflectionProbeComponent>(), { ICON_FA_GLOBE, kColProbe, "Reflection Probe" });
        add(entity.HasComponent<ParticleSystemComponent>(), { ICON_FA_FIRE, kColAudio, "Particle System" });
        // Kayıtlı bileşenler (eklemler, Character Controller, Constant Force ...)
        for (auto& info : ComponentRegistry::All())
            if (!info.Builtin && info.Has(entity))
                add(true, { info.Icon.c_str(), ImVec4(info.IconColor.r, info.IconColor.g, info.IconColor.b, info.IconColor.a), info.Name.c_str() });
    }

    // ------------------------------------------------------------------ Temel

    HierarchyPanel::HierarchyPanel(const std::shared_ptr<Scene>& scene)
    {
        SetContext(scene);
    }

    void HierarchyPanel::SetContext(const std::shared_ptr<Scene>& scene)
    {
        m_Context = scene;
        m_SelectionContext = {};
        m_VisibleOrder.clear();
        m_RangeAnchor = {};
        m_IsRenaming = false;
    }

    void HierarchyPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectionContext = entity;
    }

    void HierarchyPanel::StartRename(Entity entity)
    {
        if (!entity)
            return;
        m_IsRenaming = true;
        m_RenameEntity = entity;
        m_RenameFrame = 0;
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        strncpy(m_RenameBuffer, tag.c_str(), sizeof(m_RenameBuffer) - 1);
        m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
    }

    std::vector<Entity> HierarchyPanel::GetRootEntities() const
    {
        std::vector<Entity> roots;
        m_Context->m_Registry.view<TagComponent>().each([&](auto entityID, auto&) {
            Entity entity{ entityID, m_Context.get() };
            if (!entity.GetParent())
                roots.push_back(entity);
        });
        // En eski en üstte
        std::reverse(roots.begin(), roots.end());
        return roots;
    }

    bool HierarchyPanel::MatchesFilter(Entity entity) const
    {
        if (m_Filter[0] == '\0')
            return true;
        std::string filter = ToLower(m_Filter);
        // "t:Light" -> bileşen/ikon adına göre filtre (Unity tarzı)
        if (filter.rfind("t:", 0) == 0)
        {
            std::string type = filter.substr(2);
            if (ToLower(GetPrimaryIcon(entity).Name).find(type) != std::string::npos)
                return true;
            std::vector<ComponentIcon> icons;
            GetComponentIcons(entity, icons);
            for (auto& icon : icons)
                if (ToLower(icon.Name).find(type) != std::string::npos)
                    return true;
            return false;
        }
        return ToLower(entity.GetComponent<TagComponent>().Tag).find(filter) != std::string::npos;
    }

    void HierarchyPanel::Select(Entity entity)
    {
        m_SelectionContext = entity;
        m_LastSelected = entity;
        if (m_SelectionChangedCallback)
            m_SelectionChangedCallback(entity);
    }

    void HierarchyPanel::HandleRowClick(Entity entity)
    {
        auto& io = ImGui::GetIO();
        auto& state = EditorState::Get();

        if (io.KeyShift && m_RangeAnchor && !m_VisibleOrder.empty())
        {
            auto a = std::find(m_VisibleOrder.begin(), m_VisibleOrder.end(), m_RangeAnchor);
            auto b = std::find(m_VisibleOrder.begin(), m_VisibleOrder.end(), entity);
            if (a != m_VisibleOrder.end() && b != m_VisibleOrder.end())
            {
                if (a > b) std::swap(a, b);
                std::vector<Entity> range(a, b + 1);
                // Birincil seçim tıklanan varlık olsun
                range.erase(std::remove(range.begin(), range.end(), entity), range.end());
                range.insert(range.begin(), entity);
                Select(entity);
                state.SelectMultipleEntities(range);
                return;
            }
        }

        if (io.KeyCtrl)
        {
            state.ToggleEntitySelection(entity);
            auto keep = state.GetSelectedEntities();
            Entity primary = state.GetSelectedEntity();
            Select(primary);
            state.SelectMultipleEntities(keep);
        }
        else
        {
            state.SelectEntity(entity);
            Select(entity);
        }
        m_RangeAnchor = entity;
    }

    // ------------------------------------------------------------------ Çizim

    void HierarchyPanel::OnImGuiRender()
    {
        if (m_PendingPrefabRevert)
        {
            Entity target = m_PendingPrefabRevert;
            m_PendingPrefabRevert = {};
            if (m_Context && m_Context->m_Registry.valid((entt::entity)target))
            {
                const bool wasSelected = EditorState::Get().IsEntitySelected(target);
                if (wasSelected)
                    EditorState::Get().ClearSelection();
                Entity fresh = PrefabSerializer::RevertInstance(target);
                if (fresh && wasSelected)
                {
                    EditorState::Get().SelectEntity(fresh);
                    Select(fresh);
                }
            }
        }
        if (!ImGui::Begin("Hierarchy"))
        {
            ImGui::End();
            return;
        }

        if (!m_Context)
        {
            ImGui::TextDisabled("No scene loaded");
            ImGui::End();
            return;
        }

        DrawToolbar();

        // Seçim dışarıdan (viewport) değiştiyse ebeveynleri aç ve kaydır
        Entity selected = EditorState::Get().GetSelectedEntity();
        if (selected && selected != m_LastSelected)
        {
            m_LastSelected = selected;
            m_ScrollTo = selected;
            for (Entity p = selected.GetParent(); p; p = p.GetParent())
                m_ForceOpen.insert((uint32_t)p);
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##HierarchyTree", ImVec2(0, 0), ImGuiChildFlags_None);

        m_VisibleOrderBuilding.clear();

        // Sahne başlığı
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
            ImGui::Text(ICON_FA_CLAPPERBOARD "  %s", m_SceneName.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        if (m_Filter[0] != '\0')
        {
            DrawFilteredList();
        }
        else
        {
            // Unity: Play'de DontDestroyOnLoad / Autoload nesneleri sahneden ayrı "DontDestroyOnLoad" başlığı altında
            std::vector<Entity> persistent;
            for (auto& entity : GetRootEntities())
            {
                if (m_Context->IsPersistent(entity))
                    persistent.push_back(entity);
                else
                    DrawEntityNode(entity, 0);
            }
            if (!persistent.empty())
            {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
                ImGui::Text(ICON_FA_INFINITY "  DontDestroyOnLoad");
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (auto& entity : persistent)
                    DrawEntityNode(entity, 0);
            }
        }
        ImGui::PopStyleVar(2);

        m_VisibleOrder = m_VisibleOrderBuilding;

        // Boş alan: seçimi temizle, kök seviyesine bırak, oluşturma menüsü
        ImVec2 rest = ImGui::GetContentRegionAvail();
        rest.y = std::max(rest.y, 40.0f);
        ImGui::InvisibleButton("##HierarchyEmpty", rest);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_SelectionContext = {};
            EditorState::Get().ClearSelection();
            if (m_SelectionChangedCallback)
                m_SelectionChangedCallback({});
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
            {
                Entity dropped = *(Entity*)payload->Data;
                if (dropped && dropped.GetParent())
                    UndoRedoManager::Get().Execute(std::make_shared<ReparentEntityCommand>(dropped, Entity{}));
            }
            // İçerik Tarayıcısından: prefab / model / sprite / ses -> sahneye
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string path((const char*)payload->Data, payload->DataSize - 1);
                if (Entity created = AssetActions::SpawnFromAsset(m_Context.get(), path))
                {
                    EditorState::Get().SelectEntity(created);
                    Select(created);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_SPRITE"))
            {
                const std::string path((const char*)payload->Data);
                const std::string slice((const char*)payload->Data + path.size() + 1);
                if (Entity created = AssetActions::SpawnFromAsset(m_Context.get(), path, Entity{}, nullptr, slice))
                {
                    EditorState::Get().SelectEntity(created);
                    Select(created);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##HierarchyEmptyCtx", ImGuiPopupFlags_MouseButtonRight))
        {
            Entity created = DrawCreateMenuItems(m_Context.get(), Entity{});
            if (created)
            {
                EditorState::Get().SelectEntity(created);
                Select(created);
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void HierarchyPanel::DrawToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::Button(ICON_FA_PLUS " " ICON_FA_CARET_DOWN))
            ImGui::OpenPopup("##CreatePopup");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Create GameObject");
        if (ImGui::BeginPopup("##CreatePopup"))
        {
            // Seçili nesne varsa Shift ile çocuk olarak oluşturulabilir
            Entity parent = ImGui::GetIO().KeyShift ? EditorState::Get().GetSelectedEntity() : Entity{};
            Entity created = DrawCreateMenuItems(m_Context.get(), parent);
            if (created)
            {
                EditorState::Get().SelectEntity(created);
                Select(created);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchySearch", ICON_FA_MAGNIFYING_GLASS "  Search  (t:Light)", m_Filter, sizeof(m_Filter));
        ImGui::PopStyleVar();
    }

    void HierarchyPanel::DrawFilteredList()
    {
        // Eşleşenleri düz liste olarak göster (yol ipucu ile)
        std::vector<Entity> matches;
        m_Context->m_Registry.view<TagComponent>().each([&](auto id, auto&) {
            Entity e{ id, m_Context.get() };
            if (MatchesFilter(e))
                matches.push_back(e);
        });
        std::reverse(matches.begin(), matches.end());

        if (matches.empty())
        {
            ImGui::TextDisabled("No matches");
            return;
        }
        for (Entity e : matches)
            DrawEntityNode(e, -1);
    }

    void HierarchyPanel::DrawEntityNode(Entity entity, int depth)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        const bool flat = depth < 0;
        m_VisibleOrderBuilding.push_back(entity);

        const bool isSelected = EditorState::Get().IsEntitySelected(entity);
        const bool hasChildren = !flat && entity.HasChildren();
        const bool active = m_Context->IsEntityActive(entity);
        const bool renaming = m_IsRenaming && m_RenameEntity == entity;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // Dışarıdan seçilen varlığın ebeveynlerini aç
        if (hasChildren && m_ForceOpen.count((uint32_t)entity))
        {
            ImGui::SetNextItemOpen(true);
            m_ForceOpen.erase((uint32_t)entity);
        }

        ComponentIcon primary = GetPrimaryIcon(entity);
        // Etiket metni kendimiz çiziyoruz; gizli etiket yalnızca otomasyon/arama için ("##Ad")
        const std::string hiddenLabel = "##" + tag;
        bool opened = ImGui::TreeNodeBehavior(ImGui::GetID((void*)(uint64_t)(uint32_t)entity), flags, hiddenLabel.c_str(), hiddenLabel.c_str());

        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
        const bool rowDoubleClicked = rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
        const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        if (m_ScrollTo == entity)
        {
            ImGui::SetScrollHereY(0.4f);
            m_ScrollTo = {};
        }

        // Sürükle-bırak: üzerine bırak = çocuk yap
        if (!renaming && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("ENTITY_NODE", &entity, sizeof(Entity));
            ImGui::Text("%s  %s", primary.Icon, tag.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
            {
                Entity dropped = *(Entity*)payload->Data;
                if (dropped != entity && !IsDescendant(dropped, entity) && dropped.GetParent() != entity)
                    UndoRedoManager::Get().Execute(std::make_shared<ReparentEntityCommand>(dropped, entity));
            }
            // İçerik Tarayıcısından: bu nesnenin çocuğu olarak (betik: bu nesneye eklenir)
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string path((const char*)payload->Data, payload->DataSize - 1);
                if (AssetActions::IsAppliedToEntity(path))
                {
                    if (AssetActions::ApplyToEntity(entity, path))
                    {
                        EditorState::Get().SelectEntity(entity);
                        Select(entity);
                    }
                }
                else if (Entity created = AssetActions::SpawnFromAsset(m_Context.get(), path, entity))
                {
                    EditorState::Get().SelectEntity(created);
                    Select(created);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_SPRITE"))
            {
                const std::string path((const char*)payload->Data);
                const std::string slice((const char*)payload->Data + path.size() + 1);
                if (Entity created = AssetActions::SpawnFromAsset(m_Context.get(), path, entity, nullptr, slice))
                {
                    EditorState::Get().SelectEntity(created);
                    Select(created);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PushID((void*)(uint64_t)(uint32_t)entity);

        // İkon + ad
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float textY = rowMin.y + ImGui::GetStyle().FramePadding.y;
        float x = rowMin.x + ImGui::GetTreeNodeToLabelSpacing();
        ImVec4 iconColor = primary.Color;
        if (!active) iconColor.w *= 0.45f;
        dl->AddText(ImVec2(x, textY), ImGui::GetColorU32(iconColor), primary.Icon);
        x += ImGui::GetFontSize() * 1.35f;

        const float eyeW = ImGui::GetFontSize() + 8.0f;
        float nameEnd = x;
        if (renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(x - 4.0f, rowMin.y));
            ImGui::SetNextItemWidth(std::max(60.0f, rowMax.x - x - eyeW));
            if (m_RenameFrame == 0)
                ImGui::SetKeyboardFocusHere();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, ImGui::GetStyle().FramePadding.y));
            bool renameDone = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleVar();
            bool renameCancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (ImGui::IsItemDeactivated() && m_RenameFrame > 1)
                renameDone = true;
            m_RenameFrame++;

            if (renameCancel)
                m_IsRenaming = false;
            else if (renameDone)
            {
                if (strlen(m_RenameBuffer) > 0 && std::string(m_RenameBuffer) != tag)
                    UndoRedoManager::Get().Execute(std::make_shared<RenameEntityCommand>(entity, std::string(m_RenameBuffer)));
                m_IsRenaming = false;
            }
            nameEnd = rowMax.x - eyeW;
        }
        else
        {
            ImU32 textColor = ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            if (((entity.HasComponent<MeshRendererComponent>() && entity.GetComponent<MeshRendererComponent>().IsModel())
                 || entity.HasComponent<PrefabInstanceComponent>()) && active)
                textColor = ImGui::GetColorU32(ImVec4(0.55f, 0.75f, 1.0f, 1.0f)); // prefab / model örnekleri mavi (Unity gibi)
            dl->AddText(ImVec2(x, textY), textColor, tag.c_str());
            nameEnd = x + ImGui::CalcTextSize(tag.c_str()).x + 8.0f;
        }

        // Sağ: bileşen ikonları
        const float right = rowMax.x - eyeW - 4.0f;
        if (!renaming)
        {
            std::vector<ComponentIcon> icons;
            GetComponentIcons(entity, icons);
            const float iconStep = ImGui::GetFontSize() * 1.3f;
            int fit = (int)std::max(0.0f, (right - nameEnd) / iconStep);
            int shown = std::min((int)icons.size(), fit);
            if (shown < (int)icons.size() && shown > 0)
                shown--; // "+N" için yer
            float ix = right - shown * iconStep;
            int hoveredIcon = -1;
            for (int i = 0; i < shown; ++i)
            {
                ImVec4 c = icons[(size_t)i].Color;
                c.w *= active ? 0.85f : 0.35f;
                ImVec2 p(ix + i * iconStep, textY);
                dl->AddText(p, ImGui::GetColorU32(c), icons[(size_t)i].Icon);
                if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + iconStep, p.y + ImGui::GetFontSize())))
                    hoveredIcon = i;
            }
            if (shown < (int)icons.size() && fit > 0)
            {
                char more[16];
                snprintf(more, sizeof(more), "+%d", (int)icons.size() - shown);
                float mw = ImGui::CalcTextSize(more).x;
                dl->AddText(ImVec2(ix - mw - 2.0f, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), more);
            }
            if (rowHovered && hoveredIcon >= 0)
                ImGui::SetTooltip("%s", icons[(size_t)hoveredIcon].Name);
            else if (flat && rowHovered && entity.GetParent())
            {
                std::string path = tag;
                for (Entity p = entity.GetParent(); p; p = p.GetParent())
                    path = p.GetComponent<TagComponent>().Tag + " / " + path;
                ImGui::SetTooltip("%s", path.c_str());
            }
        }

        // Görünürlük (aktiflik) düğmesi: satır üzerindeyken ya da kapalıyken görünür
        {
            auto* activeComp = entity.HasComponent<ActiveComponent>() ? &entity.GetComponent<ActiveComponent>() : nullptr;
            const bool selfActive = !activeComp || activeComp->Active;
            const bool showEye = rowHovered || !selfActive || ImGui::IsMouseHoveringRect(ImVec2(rowMax.x - eyeW, rowMin.y), rowMax);
            ImGui::SetCursorScreenPos(ImVec2(rowMax.x - eyeW, rowMin.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_Text, showEye ? ImGui::GetStyleColorVec4(selfActive ? ImGuiCol_Text : ImGuiCol_TextDisabled) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, ImGui::GetStyle().FramePadding.y));
            if (ImGui::Button(selfActive ? ICON_FA_EYE "##vis" : ICON_FA_EYE_SLASH "##vis", ImVec2(eyeW, rowMax.y - rowMin.y)))
            {
                if (!activeComp)
                    activeComp = &entity.AddComponent<ActiveComponent>();
                activeComp->Active = !selfActive;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(selfActive ? "Deactivate" : "Activate");
        }

        if (rowClicked && !renaming)
            HandleRowClick(entity);
        if (rowDoubleClicked && !renaming)
        {
            if (m_FrameCallback)
                m_FrameCallback(entity);
        }

        if (rowRightClicked && !renaming)
        {
            // Unity gibi: sağ tık seçili değilse önce seçer
            if (!isSelected)
            {
                EditorState::Get().SelectEntity(entity);
                Select(entity);
            }
            ImGui::OpenPopup("##EntityCtx");
        }

        bool entityDeleted = false;
        DrawEntityContextMenu(entity, entityDeleted);

        ImGui::PopID();

        if (opened && hasChildren)
        {
            for (auto child : entity.GetChildren())
                DrawEntityNode(child, depth + 1);
            ImGui::TreePop();
        }

        if (entityDeleted)
        {
            EditorState::Get().ClearSelection();
            UndoRedoManager::Get().Execute(std::make_shared<DestroyEntityCommand>(m_Context, entity));
            if (m_SelectionContext == entity)
                m_SelectionContext = {};
            if (m_SelectionChangedCallback)
                m_SelectionChangedCallback({});
        }
    }

    void HierarchyPanel::DrawEntityContextMenu(Entity entity, bool& deleted)
    {
        if (!ImGui::BeginPopup("##EntityCtx"))
            return;

        auto& tag = entity.GetComponent<TagComponent>().Tag;
        ImGui::TextDisabled("%s", tag.c_str());
        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_COPY "  Copy", "Ctrl+C"))
            EditorState::Get().CopyEntities({ entity });
        if (ImGui::MenuItem(ICON_FA_SCISSORS "  Cut", "Ctrl+X"))
            EditorState::Get().CutEntities({ entity });
        if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", "Ctrl+V", false, EditorState::Get().HasClipboardData()))
        {
            auto& state = EditorState::Get();
            std::vector<Entity> newEntities;
            bool wasCut = state.IsCutOperation();
            for (auto& src : state.GetClipboardEntities())
                if (src)
                    if (Entity e = m_Context->DuplicateEntity(src))
                        newEntities.push_back(e);
            if (wasCut)
            {
                for (auto& src : state.GetClipboardEntities())
                    if (src && src.GetScene() == m_Context.get())
                        m_Context->DestroyEntity(src);
                state.FinalizeCut();
            }
            state.ClearClipboard();
            if (!newEntities.empty())
            {
                state.SelectEntity(newEntities[0]);
                Select(newEntities[0]);
            }
        }
        if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", "Ctrl+D"))
        {
            auto cmd = std::make_shared<DuplicateEntityCommand>(m_Context, entity);
            UndoRedoManager::Get().Execute(cmd);
            if (cmd->GetDuplicatedEntity())
            {
                EditorState::Get().SelectEntity(cmd->GetDuplicatedEntity());
                Select(cmd->GetDuplicatedEntity());
            }
        }
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2"))
            StartRename(entity);
        if (ImGui::MenuItem(ICON_FA_CROSSHAIRS "  Frame", "F") && m_FrameCallback)
        {
            EditorState::Get().SelectEntity(entity);
            Select(entity);
            m_FrameCallback(entity);
        }

        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_CUBES_STACKED "  Prefab"))
        {
            Entity instanceRoot = PrefabSerializer::FindInstanceRoot(entity);
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Create Prefab Asset"))
            {
                auto dir = Project::GetActive() ? Project::GetAssetDirectory() / "Prefabs" : std::filesystem::path("Assets/Prefabs");
                AssetActions::CreatePrefab(entity, dir);
            }
            ImGui::BeginDisabled(!instanceRoot);
            if (instanceRoot)
                ImGui::TextDisabled("%s", instanceRoot.GetComponent<PrefabInstanceComponent>().Prefab.c_str());
            const bool overrides = instanceRoot && PrefabSerializer::HasOverrides(instanceRoot);
            if (ImGui::MenuItem(ICON_FA_UPLOAD "  Apply Overrides to Prefab", nullptr, false, overrides))
                PrefabSerializer::ApplyInstance(instanceRoot);
            if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Revert to Prefab", nullptr, false, overrides))
                m_PendingPrefabRevert = instanceRoot;
            if (ImGui::MenuItem(ICON_FA_BOX_OPEN "  Unpack Prefab"))
                instanceRoot.RemoveComponent<PrefabInstanceComponent>();
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Create Child"))
        {
            Entity created = DrawCreateMenuItems(m_Context.get(), entity);
            if (created)
            {
                EditorState::Get().SelectEntity(created);
                Select(created);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Create Empty Parent"))
        {
            Entity parent = m_Context->CreateEntity(tag + " Parent");
            Entity oldParent = entity.GetParent();
            if (entity.HasComponent<TransformComponent>())
            {
                auto& src = entity.GetComponent<TransformComponent>();
                auto& dst = parent.GetComponent<TransformComponent>();
                dst.Position = src.Position;
                src.Position = glm::vec3(0.0f);
            }
            if (oldParent)
                parent.SetParent(oldParent);
            entity.SetParent(parent);
            EditorState::Get().SelectEntity(parent);
            Select(parent);
        }
        if (entity.HasChildren() && ImGui::MenuItem(ICON_FA_OBJECT_GROUP "  Select Children"))
        {
            std::vector<Entity> all;
            std::function<void(Entity)> collect = [&](Entity e) {
                for (auto c : e.GetChildren()) { all.push_back(c); collect(c); }
            };
            collect(entity);
            if (!all.empty())
            {
                Select(all[0]);
                EditorState::Get().SelectMultipleEntities(all);
            }
        }
        if (entity.GetParent() && ImGui::MenuItem(ICON_FA_ARROW_TURN_UP "  Unparent"))
            UndoRedoManager::Get().Execute(std::make_shared<ReparentEntityCommand>(entity, Entity{}));

        ImGui::Separator();
        {
            auto* activeComp = entity.HasComponent<ActiveComponent>() ? &entity.GetComponent<ActiveComponent>() : nullptr;
            bool selfActive = !activeComp || activeComp->Active;
            if (ImGui::MenuItem(selfActive ? ICON_FA_EYE_SLASH "  Deactivate" : ICON_FA_EYE "  Activate"))
            {
                if (!activeComp)
                    activeComp = &entity.AddComponent<ActiveComponent>();
                activeComp->Active = !selfActive;
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete", "Del"))
            deleted = true;
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------ Oluşturma menüsü

    namespace
    {
        // Create menüsünün girişleri tek yerde (Hierarchy "+", sağ tık, Create Child, GameObject menüsü, şeritteki Insert): menüler bu
        // ağaçtan çizilir; girişler Viewport'a sürüklenip bırakılabilir (Unreal Place Actors gibi). Bırakmada aynı oluşturma kodu çalışır
        struct CreateNode
        {
            enum class Kind { Item, Separator, Heading, Menu };
            Kind Type = Kind::Item;
            std::string Label;
            std::string Id;   // Item: menü yolu + etiket (ayraç \x1f), sürükleme yükü
            HierarchyPanel::CreatePlacement Placement = HierarchyPanel::CreatePlacement::Preview;
            std::function<Entity(Scene*, Entity)> Create;
            std::vector<CreateNode> Children;
        };

        CreateNode Item(const char* label, std::function<Entity(Scene*, Entity)> create,
                        HierarchyPanel::CreatePlacement placement = HierarchyPanel::CreatePlacement::Preview)
        {
            CreateNode n;
            n.Label = label;
            n.Create = std::move(create);
            n.Placement = placement;
            return n;
        }
        CreateNode Separator() { CreateNode n; n.Type = CreateNode::Kind::Separator; return n; }
        CreateNode Heading(const char* text) { CreateNode n; n.Type = CreateNode::Kind::Heading; n.Label = text; return n; }
        CreateNode Menu(const char* label, std::vector<CreateNode> children)
        {
            CreateNode n;
            n.Type = CreateNode::Kind::Menu;
            n.Label = label;
            n.Children = std::move(children);
            return n;
        }

        Entity MakeEntity(Scene* scene, Entity parent, const std::string& name)
        {
            Entity e = scene->CreateEntity(name);
            if (parent)
                e.SetParent(parent);
            return e;
        }
        Entity MakeMesh(Scene* scene, Entity parent, const char* name, MeshType type)
        {
            Entity e = MakeEntity(scene, parent, name);
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = type;   // varsayılan malzeme (Default Grid) eklemede atanır
            AssetActions::AddPrimitiveCollider(e);   // Unity gibi çarpıştırıcısıyla gelir
            return e;
        }
        // Eski (Legacy) UI öğesi: verilen canvas / UI öğesinin, yoksa ilk canvas'ın altına
        Entity MakeLegacyUI(Scene* scene, Entity parent, const char* name)
        {
            Entity canvas = (parent && (parent.HasComponent<CanvasComponent>() || parent.GetParent())) ? parent : FindOrCreateCanvas(scene);
            Entity e = scene->CreateEntity(name);
            e.SetParent(canvas);
            MakeUI(e);
            return e;
        }
        // UI öğeleri seçili UI öğesinin / canvas'ın altına (yoksa ilk canvas) eklenir
        Entity UIParentFor(Entity parent)
        {
            if (parent)
                return parent;
            Entity sel = EditorState::Get().GetSelectedEntity();
            if (sel && (sel.HasComponent<CanvasComponent>() || sel.HasComponent<RectTransformComponent>()))
                return sel;
            return {};
        }

        CreateNode BuildCreateTree()
        {
            using P = HierarchyPanel::CreatePlacement;
            std::vector<CreateNode> root;
            root.push_back(Item(ICON_FA_CIRCLE_DOT "  Create Empty", [](Scene* s, Entity p) { return MakeEntity(s, p, "GameObject"); }));
            root.push_back(Separator());

            root.push_back(Menu(ICON_FA_CUBE "  3D Object", {
                Item("Cube", [](Scene* s, Entity p) { return MakeMesh(s, p, "Cube", MeshType::Cube); }),
                Item("Sphere", [](Scene* s, Entity p) { return MakeMesh(s, p, "Sphere", MeshType::Sphere); }),
                Item("Plane", [](Scene* s, Entity p) { return MakeMesh(s, p, "Plane", MeshType::Plane); }),
                Item("Cylinder", [](Scene* s, Entity p) { return MakeMesh(s, p, "Cylinder", MeshType::Cylinder); }),
                // Arazi büyük ve sahneye göre yerleşir: sürüklenmez (Unreal'de de Landscape Place Actors'ta değil)
                Item(ICON_FA_MOUNTAIN_SUN "  Terrain", [](Scene* s, Entity p) {
                    Entity e = TerrainPanel::CreateTerrain(s);
                    if (e && p)
                        e.SetParent(p);
                    return e;
                }, P::None),
                // Unity 3D Object > Tree: Tree Creator ağacı (gövde, dallar, ince dallar, yapraklar), Mesh Renderer çizer
                Item(ICON_FA_TREE "  Tree", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Tree");
                    if (auto* info = ComponentRegistry::Find("TreeComponent"))
                        ComponentRegistry::AddWithRequirements(*info, e);
                    return e;
                }),
                Item(ICON_FA_CHESS_BOARD "  Hex Board", [](Scene* s, Entity p) {
                    return TilePalettePanel::CreateTilemap(s, GridLayout::HexagonPointyTop, GridPlane::XZ, true, p);
                }, P::OnDrop),
                Item(ICON_FA_CHESS_BOARD "  Square Board", [](Scene* s, Entity p) {
                    return TilePalettePanel::CreateTilemap(s, GridLayout::Rectangle, GridPlane::XZ, true, p);
                }, P::OnDrop),
            }));

            {
                std::vector<CreateNode> items;
                items.push_back(Heading("Sprites"));
                for (const char* spriteName : { "Capsule", "Circle", "Diamond", "HexagonFlat", "HexagonPoint", "IsometricDiamond",
                                                 "NineSliced", "Pentagon", "Square", "Star", "Triangle" })
                {
                    const std::string name = spriteName;
                    items.push_back(Item(spriteName, [name](Scene* s, Entity p) {
                        Entity e = MakeEntity(s, p, name);
                        e.AddComponent<SpriteRendererComponent>().Texture = Texture2D::Create("Resources/Packages/Core/Sprites/" + name + ".png");
                        return e;
                    }));
                }
                items.push_back(Separator());
                items.push_back(Menu(ICON_FA_TABLE_CELLS "  Tilemap", {
                    Item("Rectangular", [](Scene* s, Entity p) { return TilePalettePanel::CreateTilemap(s, GridLayout::Rectangle, GridPlane::XY, false, p); }, P::OnDrop),
                    Item("Hexagonal - Pointy Top", [](Scene* s, Entity p) { return TilePalettePanel::CreateTilemap(s, GridLayout::HexagonPointyTop, GridPlane::XY, false, p); }, P::OnDrop),
                    Item("Hexagonal - Flat Top", [](Scene* s, Entity p) { return TilePalettePanel::CreateTilemap(s, GridLayout::HexagonFlatTop, GridPlane::XY, false, p); }, P::OnDrop),
                    Item("Isometric", [](Scene* s, Entity p) { return TilePalettePanel::CreateTilemap(s, GridLayout::Isometric, GridPlane::XY, false, p); }, P::OnDrop),
                }));
                items.push_back(Separator());
                items.push_back(Item(ICON_FA_VIDEO "  2D Camera", [](Scene* s, Entity p) {
                    // Ortografik kamera, sahnenin önünde (z=10) -Z'ye bakar
                    Entity cam = MakeEntity(s, p, "2D Camera");
                    cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f };
                    auto& cc = cam.AddComponent<CameraComponent>();
                    cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
                    cc.Camera.SetOrthographicSize(10.0f);
                    return cam;
                }));
                items.push_back(Separator());
                items.push_back(Heading("Physics"));
                items.push_back(Item("Dynamic Sprite (Rigidbody 2D + Box)", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Dynamic Sprite");
                    e.AddComponent<SpriteRendererComponent>().Texture = Texture2D::Create("Resources/Packages/Core/Sprites/Square.png");
                    e.AddComponent<RigidBody2DComponent>();
                    e.AddComponent<BoxCollider2DComponent>();
                    return e;
                }));
                root.push_back(Menu(ICON_FA_SHAPES "  2D Object", std::move(items)));
            }

            {
                std::vector<CreateNode> items;
                items.push_back(Item("Particle System", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Particle System");
                    e.AddComponent<ParticleSystemComponent>();
                    return e;
                }));
                items.push_back(Separator());
                for (const auto& preset : ParticleSystem::GetPresetNames())
                {
                    const std::string name = preset;
                    items.push_back(Item(preset.c_str(), [name](Scene* s, Entity p) {
                        Entity e = MakeEntity(s, p, name);
                        ParticleSystem::ApplyPreset(e.AddComponent<ParticleSystemComponent>(), name);
                        return e;
                    }));
                }
                root.push_back(Menu(ICON_FA_FIRE "  Effects", std::move(items)));
            }

            root.push_back(Menu(ICON_FA_LIGHTBULB "  Light", {
                Item(ICON_FA_SUN "  Directional Light", [](Scene* s, Entity p) {
                    Entity sun = MakeEntity(s, p, "Directional Light");
                    auto& light = sun.AddComponent<DirectionalLightComponent>();
                    // Yön dönüşten gelir: aşağı bakan eğik güneş
                    sun.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(glm::vec3(-0.4f, -1.0f, -0.3f));
                    sun.GetComponent<TransformComponent>().Position = { 0.0f, 3.0f, 0.0f };
                    light.Color = glm::vec3(1.0f);
                    light.Intensity = 1.0f;
                    return sun;
                }),
                Item(ICON_FA_LIGHTBULB "  Point Light", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Point Light");
                    auto& light = e.AddComponent<PointLightComponent>();
                    light.Color = glm::vec3(1.0f);
                    light.Intensity = 1.0f;
                    light.Range = 10.0f;
                    return e;
                }),
                Item(ICON_FA_LIGHTBULB "  Spot Light", [](Scene* s, Entity p) {
                    Entity spot = MakeEntity(s, p, "Spot Light");
                    auto& light = spot.AddComponent<SpotLightComponent>();
                    spot.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(glm::vec3(0.0f, -1.0f, 0.0f));
                    spot.GetComponent<TransformComponent>().Position = { 0.0f, 3.0f, 0.0f };
                    light.Color = glm::vec3(1.0f);
                    light.Intensity = 1.0f;
                    light.Range = 10.0f;
                    return spot;
                }),
                Separator(),
                Item(ICON_FA_GLOBE "  Reflection Probe", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Reflection Probe");
                    e.AddComponent<ReflectionProbeComponent>();
                    return e;
                }),
                Item(ICON_FA_BRAILLE "  Adaptive Probe Volume", [](Scene* s, Entity p) {
                    Entity e = MakeEntity(s, p, "Adaptive Probe Volume");
                    e.AddComponent<AdaptiveProbeVolumeComponent>();
                    return e;
                }),
            }));

            root.push_back(Menu(ICON_FA_VOLUME_HIGH "  Audio", {
                Item("Audio Source", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "Audio Source"); e.AddComponent<AudioSourceComponent>(); return e; }),
                Item("Audio Listener", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "Audio Listener"); e.AddComponent<AudioListenerComponent>(); return e; }),
                Item("Audio Reverb Zone", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "Audio Reverb Zone"); e.AddComponent<AudioReverbZoneComponent>(); return e; }),
            }));

            {
                // UI öğeleri canvas'a yerleşir (konumu RectTransform belirler): Viewport'a sürüklenmez
                std::vector<CreateNode> items;
                items.push_back(Item(ICON_FA_TABLE_CELLS_LARGE "  Canvas", [](Scene* s, Entity) { return UISystem::CreateCanvas(*s); }, P::None));
                items.push_back(Separator());
                struct { const char* Label; const char* Type; } elements[] = {
                    { ICON_FA_SQUARE "  Panel", "Panel" }, { ICON_FA_IMAGE "  Image", "Image" }, { ICON_FA_FONT "  Text", "Text" },
                    { ICON_FA_HAND_POINTER "  Button", "Button" }, { ICON_FA_SQUARE_CHECK "  Toggle", "Toggle" }, { ICON_FA_SLIDERS "  Slider", "Slider" },
                    { ICON_FA_I_CURSOR "  Input Field", "InputField" }, { ICON_FA_CARET_DOWN "  Dropdown", "Dropdown" },
                    { ICON_FA_SCROLL "  Scroll View", "ScrollView" }, { ICON_FA_GRIP "  Layout Group", "LayoutGroup" },
                };
                for (auto& it : elements)
                {
                    const std::string type = it.Type;
                    items.push_back(Item(it.Label, [type](Scene* s, Entity p) { return UISystem::CreateElement(*s, type, UIParentFor(p)); }, P::None));
                }
                // Godot denetimleri (Unity UGUI'de yok)
                {
                    std::vector<CreateNode> controls;
                    struct { const char* Label; const char* Type; } godot[] = {
                        { "Spin Box", "SpinBox" }, { "Color Picker", "ColorPicker" }, { "Color Picker Button", "ColorPickerButton" },
                        { "Menu Button", "MenuButton" }, { "Popup Menu", "PopupMenu" }, { "Menu Bar", "MenuBar" },
                        { "Tab Container", "TabContainer" }, { "Tab Bar", "TabBar" }, { "HSplit Container", "HSplitContainer" },
                        { "VSplit Container", "VSplitContainer" }, { "Item List", "ItemList" }, { "Tree", "Tree" },
                        { "Link Button", "LinkButton" }, { "Progress Bar", "ProgressBar" }, { "Accept Dialog", "AcceptDialog" },
                        { "Confirmation Dialog", "ConfirmationDialog" }, { "File Dialog", "FileDialog" }, { "Text Edit", "TextEdit" },
                        { "Code Edit", "CodeEdit" }, { "Graph Edit", "GraphEdit" }, { "Graph Node", "GraphNode" },
                    };
                    for (auto& it : godot)
                    {
                        const std::string type = it.Type;
                        controls.push_back(Item(it.Label, [type](Scene* s, Entity p) { return UISystem::CreateElement(*s, type, UIParentFor(p)); }, P::None));
                    }
                    items.push_back(Menu("Controls", controls));
                }
                items.push_back(Separator());
                items.push_back(Menu("Legacy", {
                    Item("Canvas", [](Scene* s, Entity p) {
                        Entity c = MakeEntity(s, p, "Canvas");
                        c.AddComponent<CanvasComponent>();
                        c.AddComponent<CanvasScalerComponent>();
                        c.AddComponent<GraphicRaycasterComponent>();
                        return c;
                    }, P::None),
                    Separator(),
                    Item("Text", [](Scene* s, Entity p) {
                        Entity e = MakeLegacyUI(s, p, "Text");
                        e.AddComponent<TextRendererComponent>();
                        return e;
                    }, P::None),
                    // Eski düğmenin arka planı Sprite Renderer (eski Image bileşeni kaldırıldı; resim / panel için yukarıdaki UI Image)
                    Item("Button", [](Scene* s, Entity p) {
                        Entity button = MakeLegacyUI(s, p, "Button");
                        auto& image = button.AddComponent<SpriteRendererComponent>();
                        image.Texture = Texture2D::Create("Resources/ui/UIButton.png");
                        image.Color = glm::vec4(1.0f);
                        button.AddComponent<ButtonComponent>();
                        Entity text = s->CreateEntity("Text");
                        auto& tr = text.AddComponent<TextRendererComponent>();
                        tr.Text = "Button";
                        tr.Color = glm::vec4(1.0f);
                        MakeUI(text);
                        text.SetParent(button);
                        return button;
                    }, P::None),
                }));
                root.push_back(Menu(ICON_FA_TABLE_CELLS_LARGE "  UI", std::move(items)));
            }

            root.push_back(Menu(ICON_FA_ROUTE "  AI", {
                Item("NavMesh Surface", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "NavMesh Surface"); e.AddComponent<NavMeshSurfaceComponent>(); return e; }),
                Item("NavMesh Agent", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "NavMesh Agent"); e.AddComponent<NavMeshAgentComponent>(); return e; }),
                Item("NavMesh Obstacle", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "NavMesh Obstacle"); e.AddComponent<NavMeshObstacleComponent>(); return e; }),
            }));

            root.push_back(Separator());
            root.push_back(Item(ICON_FA_VIDEO "  Camera", [](Scene* s, Entity p) { Entity e = MakeEntity(s, p, "Camera"); e.AddComponent<CameraComponent>(); return e; }));

            CreateNode tree = Menu("", std::move(root));
            // Kimlikler: menü yolu + etiket
            std::function<void(CreateNode&, const std::string&)> assign = [&](CreateNode& n, const std::string& prefix) {
                for (auto& c : n.Children)
                {
                    if (c.Type == CreateNode::Kind::Item)
                        c.Id = prefix + c.Label;
                    else if (c.Type == CreateNode::Kind::Menu)
                        assign(c, prefix + c.Label + "\x1f");
                }
            };
            assign(tree, "");
            return tree;
        }

        // Menü öğesinden sürükleme: ImGui menü öğesi fare bırakılınca tetiklenir, basılıyken etkin öğe olmadığından olağan sürükleme
        // kaynağı hiç başlamaz; açık menü de arkadaki Viewport'un fareyi görmesini engeller. Basılan giriş kaydedilir, fare sürüklenince
        // menüler kapanır ve yük her karede dış kaynak olarak (UpdateCreateDrag) bırakılana kadar sürer
        std::string s_PressedEntry, s_PressedLabel, s_DraggingEntry, s_DraggingLabel;

        Entity DrawCreateNode(const CreateNode& node, Scene* scene, Entity parent)
        {
            Entity created;
            for (const auto& c : node.Children)
            {
                switch (c.Type)
                {
                    case CreateNode::Kind::Separator: ImGui::Separator(); break;
                    case CreateNode::Kind::Heading: ImGui::TextDisabled("%s", c.Label.c_str()); break;
                    case CreateNode::Kind::Menu:
                        if (ImGui::BeginMenu(c.Label.c_str()))
                        {
                            if (Entity e = DrawCreateNode(c, scene, parent))
                                created = e;
                            ImGui::EndMenu();
                        }
                        break;
                    case CreateNode::Kind::Item:
                        if (ImGui::MenuItem(c.Label.c_str()) && s_DraggingEntry.empty())
                            created = c.Create(scene, parent);
                        // Viewport'a sürükleyip bırak (Unreal Place Actors gibi): bırakılan yüzeye oluşturulur
                        if (c.Placement != HierarchyPanel::CreatePlacement::None)
                        {
                            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                s_PressedEntry = c.Id;
                                s_PressedLabel = c.Label;
                            }
                            if (s_PressedEntry == c.Id && s_DraggingEntry.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                            {
                                s_DraggingEntry = c.Id;
                                s_DraggingLabel = c.Label;
                                ImGui::ClosePopupsExceptModals();
                            }
                        }
                        break;
                }
            }
            return created;
        }

        // (UpdateCreateDrag aşağıda, HierarchyPanel üyesi)

        // Simge önekini (ve boşlukları) atlayarak etiket karşılaştırması: otomasyon yolları simgesiz yazılabilir ("3D Object/Cube")
        std::string PlainLabel(const std::string& label)
        {
            size_t i = 0;
            while (i < label.size() && ((unsigned char)label[i] >= 0x80 || label[i] == ' '))
                ++i;
            return label.substr(i);
        }

        const CreateNode* FindCreateNode(const CreateNode& node, const std::vector<std::string>& path, size_t depth, bool plain)
        {
            for (const auto& c : node.Children)
            {
                const bool match = plain ? PlainLabel(c.Label) == path[depth] : c.Label == path[depth];
                if (!match)
                    continue;
                if (depth + 1 == path.size())
                {
                    if (c.Type == CreateNode::Kind::Item)
                        return &c;
                }
                else if (c.Type == CreateNode::Kind::Menu)
                {
                    if (const CreateNode* r = FindCreateNode(c, path, depth + 1, plain))
                        return r;
                }
            }
            return nullptr;
        }

        // Kimlik: menü yolu \x1f ile (sürükleme yükü) ya da otomasyonda "/" ile ve simgesiz
        const CreateNode* FindCreateEntry(const CreateNode& tree, const std::string& id)
        {
            const bool plain = id.find('\x1f') == std::string::npos;
            std::vector<std::string> path;
            size_t start = 0;
            const char sep = plain ? '/' : '\x1f';
            while (true)
            {
                const size_t k = id.find(sep, start);
                path.push_back(id.substr(start, k == std::string::npos ? std::string::npos : k - start));
                if (k == std::string::npos)
                    break;
                start = k + 1;
            }
            if (path.empty() || path.back().empty())
                return nullptr;
            return FindCreateNode(tree, path, 0, plain);
        }
    }

    Entity HierarchyPanel::DrawCreateMenuItems(Scene* scene, Entity parent)
    {
        if (!scene)
            return {};
        const CreateNode tree = BuildCreateTree();
        return DrawCreateNode(tree, scene, parent);
    }

    void HierarchyPanel::UpdateCreateDrag()
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            // Bırakıldı: yük bu karede hâlâ geçerli (önceki karede verildi), Viewport'un hedefi teslim alır
            s_PressedEntry.clear();
            s_DraggingEntry.clear();
            return;
        }
        if (s_DraggingEntry.empty())
            return;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
        {
            ImGui::SetDragDropPayload("CQ_CREATE_ENTRY", s_DraggingEntry.c_str(), s_DraggingEntry.size() + 1);
            ImGui::TextUnformatted(s_DraggingLabel.c_str());
            ImGui::EndDragDropSource();
        }
    }

    HierarchyPanel::CreatePlacement HierarchyPanel::GetCreatePlacement(const std::string& id)
    {
        const CreateNode tree = BuildCreateTree();
        const CreateNode* n = FindCreateEntry(tree, id);
        return n ? n->Placement : CreatePlacement::None;
    }

    Entity HierarchyPanel::CreateFromEntry(Scene* scene, const std::string& id, Entity parent)
    {
        if (!scene)
            return {};
        const CreateNode tree = BuildCreateTree();
        const CreateNode* n = FindCreateEntry(tree, id);
        return n && n->Create ? n->Create(scene, parent) : Entity{};
    }

    bool HierarchyPanel::IsDescendant(Entity parent, Entity potentialDescendant)
    {
        for (auto child : parent.GetChildren())
        {
            if (child == potentialDescendant || IsDescendant(child, potentialDescendant))
                return true;
        }
        return false;
    }
}
