#include "InspectorPanel.h"
#include "ContentBrowserPanel.h"
#include "AssetActions.h"
#include "Scripting/DataAssets.h"
#include "Scripting/NativeScriptCompiler.h"
#include "AssetInspector.h"
#include "MaterialEditorUI.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "ObjectPicker.h"
#include "Assets/ScriptFieldUI.h"
#include "Assets/AssetUI.h"
#include "Core/Serialization/SceneSerializer.h"
#include "UndoRedo/UndoRedoManager.h"
#include "UndoRedo/EntitySnapshotCommand.h"
#include "Core/Math/Math.h"
#include "Addons/AddonManager.h"
#include "EditorState.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Scripting/ScriptRuntime.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Core/Animation/AnimationController.h"
#include "Panels/Animator/AnimatorPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Core/Audio/AudioMixer.h"
#include "Renderer/SpriteSheet.h"
#include "Terrain/Terrain.h"
#include "UI/EditorUI.h"
#include "Scene/ComponentRegistry.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"

#include <IconsFontAwesome6.h>
#include <cfloat>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <nfd.h>

#ifdef _WIN32
#include <windows.h>
#include <libloaderapi.h>
#else
#include <unistd.h>
#include "Core/Utils/SelfExecutable.h"
#endif

namespace Conqueror::Editor
{
    static std::filesystem::path GetEngineDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
#else
        const std::string exe = SelfExecutablePath();
        if (!exe.empty())
            return std::filesystem::path(exe).parent_path();
        return std::filesystem::current_path();
#endif
    }

    static std::string ToSerializablePath(const std::string& absolutePath)
    {
        if (absolutePath.empty())
            return absolutePath;

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::error_code ec;
            auto relative = std::filesystem::relative(absolutePath, projectDir, ec);
            if (!ec && !relative.empty() && relative.native()[0] != '.')
                return relative.generic_string();
        }

        auto engineDir = GetEngineDirectory();
        std::error_code ec;
        auto relative = std::filesystem::relative(absolutePath, engineDir, ec);
        if (!ec && !relative.empty() && relative.native()[0] != '.')
        {
            std::string relStr = relative.generic_string();
            if (relStr.find("Resources/") == 0 || relStr.find("Resources\\") == 0)
                return "CQ:" + relStr;
        }

        return absolutePath;
    }

    std::function<void(const std::string&)> InspectorPanel::s_OpenSpriteEditor;
    std::function<void(const std::string&)> InspectorPanel::s_OpenInputActions;

    void InspectorPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectionContext = entity;
        // Entity silindiyse veya geçersizse tamamen temizle
        if (!m_SelectionContext)
            m_SelectionContext = Entity();
    }

    std::function<std::shared_ptr<Scene>()> InspectorPanel::s_SharedScene;

    bool InspectorPanel::IsInspectorHovered() const
    {
        // Inspector ya da onun açtığı açılır pencereler (bağlam menüsü, Add Component, combo)
        for (ImGuiWindow* w = GImGui->HoveredWindow; w; w = w->ParentWindow)
            if (w->ID == m_WindowID)
                return true;
        return false;
    }

    void InspectorPanel::BeginUndoCapture()
    {
        if (!m_SelectionContext || !m_Context || m_SelectionContext.GetScene() != m_Context || !m_Context->m_Registry.valid(m_SelectionContext))
            return;
        if (!EditorState::Get().GetSelectedAsset().empty())
            return;   // dosya denetçisi açık
        if (!EditorState::Get().IsEditing())
            return;   // oyun modundaki değişiklikler geri alınmaz (Unity gibi)
        m_UndoPending = true;
        m_UndoEntityUUID = (uint64_t)m_SelectionContext.GetComponent<IDComponent>().ID;
        m_UndoBefore = EntitySnapshotCommand::Capture(m_SelectionContext);
        m_UndoBeforeText = SceneSerializer::SerializeEntityToString(m_SelectionContext);
    }

    void InspectorPanel::EndUndoCapture()
    {
        m_UndoPending = false;
        std::shared_ptr<Scene> shared = s_SharedScene ? s_SharedScene() : nullptr;
        Entity e = m_Context ? m_Context->FindEntityByUUID(m_UndoEntityUUID) : Entity{};
        if (e && shared && shared.get() == m_Context && EditorState::Get().IsEditing())
        {
            std::string after = SceneSerializer::SerializeEntityToString(e);
            if (after != m_UndoBeforeText)
                UndoRedoManager::Get().Execute(std::make_shared<EntitySnapshotCommand>(
                    shared, m_UndoEntityUUID, m_UndoBefore, EntitySnapshotCommand::Capture(e), "Edit " + e.GetComponent<TagComponent>().Tag));
        }
        m_UndoBefore.reset();
        m_UndoBeforeText.clear();
    }

    void InspectorPanel::OnImGuiRender()
    {
        // Düzenleme başlangıcı: Inspector'a tıklama (değer aynı karede değişebileceğinden pencere çizilmeden önce)
        if (!m_UndoPending && m_WindowID != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && IsInspectorHovered())
            BeginUndoCapture();
        // Content Browser'dan sürüklenen varlık Inspector üstünde: bırakılınca yuvaya girer (tek geri alma adımı)
        if (!m_UndoPending && m_WindowID != 0 && ImGui::GetDragDropPayload() && IsInspectorHovered())
            BeginUndoCapture();
        ImGui::Begin("Inspector");
        m_WindowID = ImGui::GetCurrentWindow()->ID;

        // Animator penceresinde bir durum/geçiş seçiliyse Inspector onun özelliklerini gösterir
        if (AnimatorPanel::DrawInspectorForSelection())
        {
        }
        // İçerik Tarayıcısında seçili dosya (doku içe aktarma ayarları vb.)
        else if (!EditorState::Get().GetSelectedAsset().empty())
        {
            AssetInspector::Draw(EditorState::Get().GetSelectedAsset());
        }
        else if (m_SelectionContext && m_SelectionContext.GetScene() && m_SelectionContext.GetScene()->m_Registry.valid(m_SelectionContext))
        {
            DrawComponents(m_SelectionContext);
        }

        // Klavyeyle başlayan düzenleme (Tab ile alana geçiş)
        if (!m_UndoPending && ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            BeginUndoCapture();
        ImGui::End();
        // Nesne seçici (◎) açıkken seçim penceresinde yapılır: kayıt seçim yuvaya alınana kadar sürer
        if (m_UndoPending && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ObjectPicker::IsBusy()
            && !ImGui::GetDragDropPayload())
            EndUndoCapture();
    }

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto boldFont = io.Fonts->Fonts[0];

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("X", buttonSize))
            values.x = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Y", buttonSize))
            values.y = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Z", buttonSize))
            values.z = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }

    // Bileşen başlığı ikonu (ada göre)
    static const char* ComponentIconFor(const std::string& name, ImVec4& color)
    {
        struct Entry { const char* Key; const char* Icon; ImVec4 Color; };
        static const Entry entries[] = {
            { "Transform", ICON_FA_UP_DOWN_LEFT_RIGHT, ImVec4(0.75f, 0.78f, 0.85f, 1) },
            { "Camera", ICON_FA_VIDEO, ImVec4(0.45f, 0.75f, 1.00f, 1) },
            { "Light", ICON_FA_LIGHTBULB, ImVec4(1.00f, 0.82f, 0.30f, 1) },
            { "Sprite", ICON_FA_SHAPES, ImVec4(0.55f, 0.85f, 0.65f, 1) },
            { "Mesh Renderer", ICON_FA_CUBE, ImVec4(0.62f, 0.70f, 0.85f, 1) },
            { "Model", ICON_FA_CUBES, ImVec4(0.62f, 0.70f, 0.85f, 1) },
            { "Animat", ICON_FA_PERSON_RUNNING, ImVec4(1.00f, 0.50f, 0.65f, 1) },
            { "Collider", ICON_FA_VECTOR_SQUARE, ImVec4(0.55f, 0.90f, 0.40f, 1) },
            { "Rigid", ICON_FA_WEIGHT_HANGING, ImVec4(0.55f, 0.90f, 0.40f, 1) },
            { "Audio", ICON_FA_VOLUME_HIGH, ImVec4(1.00f, 0.60f, 0.30f, 1) },
            { "Script", ICON_FA_FILE_CODE, ImVec4(0.75f, 0.55f, 1.00f, 1) },
            { "NavMesh", ICON_FA_ROUTE, ImVec4(0.40f, 0.80f, 0.75f, 1) },
            { "Canvas", ICON_FA_TABLE_CELLS_LARGE, ImVec4(0.35f, 0.85f, 0.85f, 1) },
            { "Raycaster", ICON_FA_ARROW_POINTER, ImVec4(0.35f, 0.85f, 0.85f, 1) },
            { "Button", ICON_FA_HAND_POINTER, ImVec4(0.35f, 0.85f, 0.85f, 1) },
            { "Image", ICON_FA_IMAGE, ImVec4(0.35f, 0.85f, 0.85f, 1) },
            { "Text", ICON_FA_FONT, ImVec4(0.35f, 0.85f, 0.85f, 1) },
            { "Layer", ICON_FA_LAYER_GROUP, ImVec4(0.70f, 0.70f, 0.75f, 1) },
            { "Probe", ICON_FA_GLOBE, ImVec4(0.65f, 0.85f, 1.00f, 1) },
            { "Terrain", ICON_FA_MOUNTAIN_SUN, ImVec4(0.55f, 0.85f, 0.65f, 1) },
            { "Particle", ICON_FA_FIRE, ImVec4(1.00f, 0.55f, 0.25f, 1) },
        };
        for (const auto& e : entries)
        {
            if (name.find(e.Key) != std::string::npos)
            {
                color = e.Color;
                return e.Icon;
            }
        }
        color = ImVec4(0.70f, 0.72f, 0.76f, 1);
        return ICON_FA_PUZZLE_PIECE;
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;

        if (!entity.HasComponent<T>())
            return;

        auto& component = entity.GetComponent<T>();
        ImGui::PushID(name.c_str());

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 6, 5 });
        const float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
        ImGui::Spacing();
        ImVec4 iconColor;
        const char* icon = ComponentIconFor(name, iconColor);
        std::string label = std::string("      ") + name;
        bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, "%s", label.c_str());
        ImGui::PopStyleVar();

        // İkon (ok işaretinin sağına)
        {
            ImVec2 min = ImGui::GetItemRectMin();
            float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, min.y + 5.0f), ImGui::GetColorU32(iconColor), icon);
        }

        // Ayarlar düğmesi (sağ)
        const float right = ImGui::GetItemRectMax().x;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(right - lineHeight - 2.0f, ImGui::GetItemRectMin().y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        // "##Settings_<bileşen>": otomasyon/erişilebilirlik için bileşene özgü kimlik
        if (ImGui::Button((std::string(ICON_FA_ELLIPSIS_VERTICAL "##Settings_") + name).c_str(), ImVec2{ lineHeight, lineHeight }))
            ImGui::OpenPopup("ComponentSettings");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Component settings");

        bool removeComponent = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();
            if constexpr (std::is_default_constructible_v<T> && std::is_copy_assignable_v<T>)
            {
                if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Reset"))
                    component = T{};
            }
            if constexpr (!std::is_same_v<T, TransformComponent>)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
                if (ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                    removeComponent = true;
                ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            ImGui::Indent(4.0f);
            uiFunction(component);
            ImGui::Unindent(4.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();

        if (removeComponent)
            entity.RemoveComponent<T>();
    }


    // ════════════════════════════════════════
    //  Kayıtlı (yansımalı) bileşenler: alan tanımlarından otomatik denetçi
    // ════════════════════════════════════════
    // Işık yönü nesne dönüşünden gelir: yükseklik / pusula açısı ile dönüşü ayarla (Unity: ışık -Z'ye bakar)
    static void DrawLightAim(Entity entity, const glm::vec3& direction, bool sun)
    {
        glm::vec3 d = glm::dot(direction, direction) > 1e-8f ? glm::normalize(direction) : glm::vec3(0, -1, 0);
        float elevation = glm::degrees(std::asin(std::clamp(-d.y, -1.0f, 1.0f)));
        float azimuth = glm::degrees(std::atan2(d.x, -d.z));
        bool changed = false;
        changed |= UI::SliderFloat(sun ? "Sun Elevation" : "Tilt", elevation, -90.0f, 90.0f, "%.1f deg",
                                   "90 = pointing straight down. Same as rotating the object (E).");
        changed |= UI::SliderFloat(sun ? "Sun Azimuth" : "Heading", azimuth, -180.0f, 180.0f, "%.1f deg", "Compass direction the light points to");
        if (changed && entity.HasComponent<TransformComponent>())
        {
            const float el = glm::radians(elevation), az = glm::radians(azimuth);
            glm::vec3 world(std::sin(az) * std::cos(el), -std::sin(el), -std::cos(az) * std::cos(el));
            // Ebeveyn dönüşünü telafi et (yerel dönüş yazılır)
            glm::vec3 local = world;
            if (Entity parent = entity.GetParent())
            {
                glm::vec3 pp, pr, ps;
                if (Math::DecomposeTransform(parent.GetComponent<TransformComponent>().WorldTransform, pp, pr, ps))
                    local = glm::inverse(glm::quat(pr)) * world;
            }
            entity.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(local);
        }
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Direction follows the object's rotation (%.2f, %.2f, %.2f)", d.x, d.y, d.z);
    }

    // Unity nesne alanı: yol kutusu (yazılabilir) + Content Browser'dan sürükle-bırak + ◎ seçici penceresi
    static bool AssetFieldRow(const char* label, std::string& value, const std::string& filter, const char* tooltip)
    {
        bool changed = false;
        const auto exts = ObjectPicker::ParseFilter(filter);
        UI::BeginProperty(label, tooltip);
        const float h = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", value.c_str());
        if (ImGui::InputText("##assetpath", buf, sizeof(buf)))
        {
            value = buf;
            changed = true;
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                value = SceneSerializer::ToAssetPath(std::string((const char*)payload->Data, payload->DataSize - 1));
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (std::string picked; ObjectPicker::Button("assetpick", ObjectPicker::TypeNameFor(exts), exts,
                                                     value.empty() ? value : SceneSerializer::ResolveAssetPath(value), picked))
        {
            value = picked.empty() ? picked : SceneSerializer::ToAssetPath(picked);
            changed = true;
        }
        UI::EndProperty();
        return changed;
    }

    static bool DrawRegistryField(Entity entity, const ComponentField& f, void* component)
    {
        if (f.Action)
        {
            if (UI::ButtonFullWidth(f.Label.c_str(), 26.0f))
                f.Action(entity, component);
            return false;
        }
        if (!f.Ptr)
        {
            // Sanal metin/varlık alanı
            if (!f.GetString)
                return false;
            std::string value = f.GetString(component);
            bool changed = f.Kind == FieldKind::Asset
                ? AssetFieldRow(f.Label.c_str(), value, f.AssetFilter, f.Tooltip.empty() ? nullptr : f.Tooltip.c_str())
                : UI::InputText(f.Label.c_str(), value, f.Tooltip.empty() ? nullptr : f.Tooltip.c_str());
            if (changed && f.SetString && !f.ReadOnly)
                f.SetString(component, value);
            return changed;
        }
        void* p = f.Ptr(component);
        const char* label = f.Label.c_str();
        const char* tip = f.Tooltip.empty() ? nullptr : f.Tooltip.c_str();
        const float speed = f.Speed > 0.0f ? f.Speed : 0.05f;
        bool changed = false;
        if (f.ReadOnly) ImGui::BeginDisabled();
        switch (f.Kind)
        {
            case FieldKind::Bool: changed = UI::Checkbox(label, *(bool*)p, tip); break;
            case FieldKind::Int: changed = UI::DragInt(label, *(int*)p, speed < 1.0f ? 1.0f : speed, (int)f.Min, (int)f.Max, tip); break;
            case FieldKind::Float:
            case FieldKind::Angle:
                changed = UI::DragFloat(label, *(float*)p, speed, f.Min, f.Max, f.Format.empty() ? "%.3f" : f.Format.c_str(), tip);
                break;
            case FieldKind::Vec2: changed = UI::Vec2(label, *(glm::vec2*)p, speed, tip); break;
            case FieldKind::Vec3: changed = UI::Vec3(label, *(glm::vec3*)p, speed, 0.0f, tip); break;
            case FieldKind::Vec4:
            {
                UI::BeginProperty(label, tip);
                ImGui::SetNextItemWidth(-1);
                changed = ImGui::DragFloat4("##v4", (float*)p, speed);
                UI::EndProperty();
                break;
            }
            case FieldKind::Color3: changed = UI::Color3(label, *(glm::vec3*)p, tip); break;
            case FieldKind::Color4: changed = UI::Color4(label, *(glm::vec4*)p, tip); break;
            case FieldKind::String: changed = UI::InputText(label, *(std::string*)p, tip); break;
            case FieldKind::Asset:
                changed = AssetFieldRow(label, *(std::string*)p, f.AssetFilter, tip);
                break;
            case FieldKind::Enum:
            {
                std::vector<const char*> items;
                for (auto& o : f.Options) items.push_back(o.c_str());
                int v = 0;
                std::memcpy(&v, p, (size_t)std::min(f.EnumSize, 4));
                if (f.EnumSize == 1) v = *(int8_t*)p;
                if (!items.empty() && UI::Combo(label, v, items.data(), (int)items.size(), tip))
                {
                    ComponentRegistry::FieldFromString(f, component, std::to_string(v));
                    changed = true;
                }
                break;
            }
            case FieldKind::EntityRef:
            {
                Scene* scene = entity.GetScene();
                uint64_t& uuid = *(uint64_t*)p;
                Entity target = uuid && scene ? scene->FindEntityByUUID(uuid) : Entity{};
                std::string preview = target ? target.GetComponent<TagComponent>().Tag : (uuid ? std::string("(missing)") : std::string("None (World)"));
                UI::BeginProperty(label, tip);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##entityref", (std::string(ICON_FA_CUBE "  ") + preview).c_str(), ImGuiComboFlags_HeightLarge))
                {
                    if (ImGui::Selectable("None (World)", uuid == 0)) { uuid = 0; changed = true; }
                    if (scene)
                    {
                        std::vector<std::pair<std::string, uint64_t>> options;
                        scene->m_Registry.view<IDComponent, TagComponent>().each([&](auto e, auto& id, auto& tag) {
                            if (e != (entt::entity)entity) options.emplace_back(tag.Tag, (uint64_t)id.ID);
                        });
                        std::sort(options.begin(), options.end());
                        for (auto& [name, id] : options)
                        {
                            ImGui::PushID((void*)(uintptr_t)id);
                            if (ImGui::Selectable(name.c_str(), uuid == id)) { uuid = id; changed = true; }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                    {
                        Entity dropped = *(const Entity*)payload->Data;
                        if (dropped && dropped.HasComponent<IDComponent>() && dropped != entity)
                        {
                            uuid = dropped.GetComponent<IDComponent>().ID;
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                UI::EndProperty();
                break;
            }
            case FieldKind::LayerMask:
            {
                uint32_t& mask = *(uint32_t*)p;
                Scene* scene = entity.GetScene();
                std::string preview = mask == 0xFFFFFFFFu ? "Everything" : mask == 0 ? "Nothing" : "Mixed";
                UI::BeginProperty(label, tip);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##mask", preview.c_str()))
                {
                    if (ImGui::Selectable("Everything")) { mask = 0xFFFFFFFFu; changed = true; }
                    if (ImGui::Selectable("Nothing")) { mask = 0; changed = true; }
                    ImGui::Separator();
                    if (scene)
                        for (auto& layer : scene->GetLayers())
                        {
                            if (layer.ID < 0 || layer.ID > 31) continue;
                            bool on = (mask >> layer.ID) & 1u;
                            if (ImGui::Checkbox((std::to_string(layer.ID) + ": " + layer.Name).c_str(), &on))
                            {
                                mask = on ? (mask | (1u << layer.ID)) : (mask & ~(1u << layer.ID));
                                changed = true;
                            }
                        }
                    ImGui::EndCombo();
                }
                UI::EndProperty();
                break;
            }
            case FieldKind::Vec2List:
            case FieldKind::Vec3List:
            {
                const bool is3 = f.Kind == FieldKind::Vec3List;
                size_t count = is3 ? ((std::vector<glm::vec3>*)p)->size() : ((std::vector<glm::vec2>*)p)->size();
                UI::BeginProperty(label, tip);
                ImGui::TextDisabled("%zu points", count);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_PLUS "##addpt"))
                {
                    if (is3)
                    {
                        auto& l = *(std::vector<glm::vec3>*)p;
                        l.push_back(l.empty() ? glm::vec3(0.0f) : l.back() + glm::vec3(0.0f, 0.0f, 1.0f));
                    }
                    else
                    {
                        auto& l = *(std::vector<glm::vec2>*)p;
                        l.push_back(l.empty() ? glm::vec2(0.0f) : l.back() + glm::vec2(0.5f, 0.0f));
                    }
                    changed = true;
                }
                UI::EndProperty();
                int removeIndex = -1;
                count = is3 ? ((std::vector<glm::vec3>*)p)->size() : ((std::vector<glm::vec2>*)p)->size();
                for (size_t i = 0; i < count; ++i)
                {
                    ImGui::PushID((int)i);
                    char rowLabel[32];
                    std::snprintf(rowLabel, sizeof(rowLabel), "  [%zu]", i);
                    UI::BeginProperty(rowLabel);
                    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6.0f);
                    if (is3)
                        changed |= ImGui::DragFloat3("##pt", &(*(std::vector<glm::vec3>*)p)[i].x, speed);
                    else
                        changed |= ImGui::DragFloat2("##pt", &(*(std::vector<glm::vec2>*)p)[i].x, speed);
                    ImGui::SameLine(0, 4);
                    if (ImGui::Button(ICON_FA_XMARK "##rm", ImVec2(ImGui::GetFrameHeight(), 0)))
                        removeIndex = (int)i;
                    UI::EndProperty();
                    ImGui::PopID();
                }
                if (removeIndex >= 0)
                {
                    if (is3) { auto& l = *(std::vector<glm::vec3>*)p; l.erase(l.begin() + removeIndex); }
                    else { auto& l = *(std::vector<glm::vec2>*)p; l.erase(l.begin() + removeIndex); }
                    changed = true;
                }
                break;
            }
        }
        if (f.ReadOnly) ImGui::EndDisabled();
        return changed;
    }

    bool InspectorPanel::DrawObjectFields(const std::string& componentType, void* object, const std::function<bool(const std::string&)>& include)
    {
        ComponentInfo* info = ComponentRegistry::Find(componentType);
        if (!info || !object)
            return false;
        bool changed = false;
        for (auto& f : info->Fields)
        {
            if (include && !include(f.Name))
                continue;
            // Görünürlük koşulu nesnenin kendi değerlerine göre (profil yolu hariç tutulmuş kopya)
            if (f.VisibleIf && !f.VisibleIf(object))
                continue;
            if (!f.Header.empty())
                UI::SubHeader(f.Header.c_str());
            ImGui::PushID(f.Name.c_str());
            changed |= DrawRegistryField(Entity{}, f, object);
            ImGui::PopID();
        }
        return changed;
    }

    static void DrawRegistryComponent(Entity entity, ComponentInfo& info)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
        void* component = info.Get(entity);
        if (!component)
            return;
        ImGui::PushID(info.TypeName.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 6, 5 });
        const float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
        ImGui::Spacing();
        std::string label = std::string("      ") + info.Name;
        bool open = ImGui::TreeNodeEx("##registry", treeNodeFlags, "%s", label.c_str());
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !info.Description.empty())
            ImGui::SetTooltip("%s", info.Description.c_str());
        {
            ImVec2 min = ImGui::GetItemRectMin();
            float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, min.y + 5.0f),
                ImGui::GetColorU32(ImVec4(info.IconColor.r, info.IconColor.g, info.IconColor.b, info.IconColor.a)), info.Icon.c_str());
        }
        const float right = ImGui::GetItemRectMax().x;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(right - lineHeight - 2.0f, ImGui::GetItemRectMin().y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button((std::string(ICON_FA_ELLIPSIS_VERTICAL "##Settings_") + info.Name).c_str(), ImVec2{ lineHeight, lineHeight }))
            ImGui::OpenPopup("ComponentSettings");
        ImGui::PopStyleColor();
        bool remove = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::TextDisabled("%s", info.Name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Reset"))
                info.Reset(entity);
            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
            if (ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                remove = true;
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (open)
        {
            ImGui::Indent(4.0f);
            component = info.Get(entity);
            for (auto& f : info.Fields)
            {
                if (f.VisibleIf && !f.VisibleIf(component))
                    continue;
                if (!f.Header.empty())
                    UI::SubHeader(f.Header.c_str());
                ImGui::PushID(f.Name.c_str());
                DrawRegistryField(entity, f, component);
                ImGui::PopID();
            }
            if (info.ExtraInspector)
                info.ExtraInspector(entity);
            ImGui::Unindent(4.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove)
            info.Remove(entity);
    }

    // ════════════════════════════════════════
    //  ConquerorScript bileşeni (Unity MonoBehaviour denetçisi gibi)
    // ════════════════════════════════════════
    std::function<void(const std::string&)> InspectorPanel::s_OpenCodeEditor;

    namespace
    {
        struct ScriptMetaCache
        {
            std::filesystem::file_time_type Time{};
            std::vector<std::string> Classes;
            std::unordered_map<std::string, std::vector<Scripting::ScriptFieldInfo>> Fields;
            std::string Error;
        };

        std::string ResolveScriptPath(const std::string& path)
        {
            std::filesystem::path p(path);
            if (p.is_relative())
            {
                auto dir = Project::GetActiveProjectDirectory();
                if (!dir.empty() && std::filesystem::exists(dir / p))
                    return (dir / p).string();
            }
            return path;
        }

        // Add Component > Scripts listesi için projedeki betik dosyaları (menü açılırken bir kez taranır)
        std::vector<std::filesystem::path> s_AddMenuScripts;

        void RefreshAddMenuScripts()
        {
            s_AddMenuScripts.clear();
            auto assets = Project::GetAssetDirectory();
            std::error_code ec;
            if (assets.empty() || !std::filesystem::is_directory(assets, ec))
                return;
            namespace fs = std::filesystem;
            for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator() && s_AddMenuScripts.size() < 2000; it.increment(ec))
            {
                std::string file = it->path().filename().string();
                if (!file.empty() && file[0] == '.')
                {
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file(ec) && Scripting::ScriptRuntimes::IsScriptFile(it->path().string()) &&
                    !DataAssets::IsDataOnlyScript(it->path().string()))
                    s_AddMenuScripts.push_back(it->path());
            }
            std::sort(s_AddMenuScripts.begin(), s_AddMenuScripts.end(),
                [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
        }

        // Dosya değişmediyse derleme sonucunu yeniden kullan
        ScriptMetaCache& GetScriptMeta(const std::string& resolved)
        {
            static std::unordered_map<std::string, ScriptMetaCache> s_Cache;
            auto& entry = s_Cache[resolved];
            std::error_code ec;
            auto time = std::filesystem::last_write_time(resolved, ec);
            if (ec || (time == entry.Time && (!entry.Classes.empty() || !entry.Error.empty())))
            {
                if (ec && entry.Error.empty())
                    entry.Error = "Script file not found.";
                return entry;
            }
            entry = ScriptMetaCache{};
            entry.Time = time;
            std::string error;
            auto* runtime = Scripting::ScriptRuntimes::ForPath(resolved);
            if (!runtime)
            {
                entry.Error = "Unsupported script type.";
                return entry;
            }
            if (!runtime->IsAvailable())
            {
                entry.Error = std::string(runtime->GetName()) + " is not available: " + runtime->GetUnavailableReason();
                return entry;
            }
            runtime->GetClasses(resolved, entry.Classes, &error);
            // Data Asset sınıfları nesnede çalışmaz (Unity: ScriptableObject MonoBehaviour değildir): sınıf listesinden çıkar
            std::vector<std::string> dataClasses;
            entry.Classes.erase(std::remove_if(entry.Classes.begin(), entry.Classes.end(), [&](const std::string& cls) {
                                    const bool data = DataAssets::IsDataClass(resolved, cls);
                                    if (data)
                                        dataClasses.push_back(cls);
                                    return data;
                                }),
                                entry.Classes.end());
            if (entry.Classes.empty() && !dataClasses.empty())
            {
                entry.Error = "'" + dataClasses.front() + "' is a Data Asset class: it holds data and cannot run on an object. "
                              "Create assets from it with Create > Data Asset > " + dataClasses.front() + ".";
                return entry;
            }
            for (auto& cls : entry.Classes)
            {
                std::vector<Scripting::ScriptFieldInfo> fields;
                std::string ferr;
                if (runtime->GetFields(resolved, cls, fields, &ferr))
                    entry.Fields[cls] = std::move(fields);
                else if (error.empty())
                    error = ferr;
            }
            entry.Error = error;
            return entry;
        }

        // Ortak betik alanı arayüzü (Assets/ScriptFieldUI): betik bileşenleri ve Data Asset aynı düzenleyiciyi kullanır
        std::string QuotedAttribute(const std::vector<std::string>& attrs, const char* prefix) { return ScriptFieldUI::Quoted(attrs, prefix); }
        std::string TooltipAttribute(const std::vector<std::string>& attrs) { return ScriptFieldUI::Tooltip(attrs); }
        bool HasSpaceAttribute(const std::vector<std::string>& attrs) { return ScriptFieldUI::HasSpace(attrs); }
        std::string PrettyFieldName(const std::string& name) { return ScriptFieldUI::PrettyName(name); }
        ScriptFieldOverride FieldDefault(const Scripting::ScriptFieldInfo& f) { return f.Default; }
        bool DrawScriptFieldValue(const char* id, ScriptFieldOverride& v, const Scripting::ScriptFieldInfo& f) { return ScriptFieldUI::DrawValue(id, v, f); }
    }

    void InspectorPanel::DrawConquerorScripts(Entity entity)
    {
        auto& csc = entity.GetComponent<ConquerorScriptComponent>();
        int indexToRemove = -1;
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
        const bool playing = !EditorState::Get().IsEditing();

        for (int i = 0; i < (int)csc.Scripts.size(); i++)
        {
            auto& script = csc.Scripts[i];
            std::string title = "Script";
            if (!script.ScriptPath.empty())
            {
                auto* rt = Scripting::ScriptRuntimes::ForPath(script.ScriptPath);
                title = std::filesystem::path(script.ScriptPath).stem().string() + " (" + (rt ? rt->GetName() : "Unknown script type") + ")";
            }
            ImGui::PushID(i);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 6, 5 });
            const float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
            ImGui::Spacing();
            bool open = ImGui::TreeNodeEx("##script", treeNodeFlags, "      %s", title.c_str());
            ImGui::PopStyleVar();
            {
                ImVec2 min = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + ImGui::GetTreeNodeToLabelSpacing(), min.y + 5.0f),
                                                    ImGui::GetColorU32(ImVec4(0.75f, 0.55f, 1.00f, 1)), ICON_FA_FILE_CODE);
            }
            const float right = ImGui::GetItemRectMax().x;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(right - lineHeight - 2.0f, ImGui::GetItemRectMin().y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_ELLIPSIS_VERTICAL, ImVec2{ lineHeight, lineHeight }))
                ImGui::OpenPopup("ScriptSettings");
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup("ScriptSettings"))
            {
                if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Reset Field Values"))
                    script.FieldOverrides.clear();
                if (!script.ScriptPath.empty() && s_OpenCodeEditor && ImGui::MenuItem(ICON_FA_CODE "  Edit Script"))
                    s_OpenCodeEditor(ResolveScriptPath(script.ScriptPath));
                ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
                if (ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                    indexToRemove = i;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }

            if (open)
            {
                ImGui::Indent(4.0f);
                // ── Betik dosyası ──
                auto assignScript = [&](const std::string& path) {
                    if (AssetActions::RefuseDataScript(path))
                        return;
                    script.ScriptPath = ToSerializablePath(path);
                    script.ClassName.clear();
                    script.FieldOverrides.clear();
                    auto& meta = GetScriptMeta(ResolveScriptPath(script.ScriptPath));
                    if (!meta.Classes.empty())
                        script.ClassName = meta.Classes.front();
                };
                auto isScriptFile = [](const std::string& path) { return Scripting::ScriptRuntimes::IsScriptFile(path); };
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Script");
                ImGui::SameLine(110.0f);
                {
                    std::string shown = script.ScriptPath.empty() ? "None (drop a script file)" : std::filesystem::path(script.ScriptPath).filename().string();
                    float w = ImGui::GetContentRegionAvail().x - 2 * (ImGui::GetFrameHeight() + 4.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                    ImGui::Button((std::string(ICON_FA_FILE_CODE "  ") + shown + "##scriptslot").c_str(), ImVec2(std::max(60.0f, w), 0));
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered() && !script.ScriptPath.empty())
                        ImGui::SetTooltip("%s\nDouble-click to edit", script.ScriptPath.c_str());
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && s_OpenCodeEditor && !script.ScriptPath.empty())
                        s_OpenCodeEditor(ResolveScriptPath(script.ScriptPath));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        {
                            std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                            if (isScriptFile(dropped))
                                assignScript(dropped);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine(0, 4.0f);
                    {
                        // Unity Object Picker: projedeki tüm betik dilleri
                        std::vector<std::string> scriptExts;
                        for (auto* runtime : Scripting::ScriptRuntimes::All())
                            for (const auto& e : runtime->GetExtensions())
                                scriptExts.push_back(e);
                        if (std::string picked; ObjectPicker::Button("scriptpick", "Script", scriptExts, script.ScriptPath, picked))
                        {
                            if (picked.empty())
                                script.ScriptPath.clear();
                            else
                                assignScript(picked);
                        }
                    }
                    ImGui::SameLine(0, 4.0f);
                    ImGui::BeginDisabled(script.ScriptPath.empty() || !s_OpenCodeEditor);
                    if (ImGui::Button(ICON_FA_PEN_TO_SQUARE "##edit", ImVec2(ImGui::GetFrameHeight(), 0)))
                        s_OpenCodeEditor(ResolveScriptPath(script.ScriptPath));
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Open in Code Editor");
                }

                if (!script.ScriptPath.empty())
                {
                    std::string resolved = ResolveScriptPath(script.ScriptPath);
                    auto& meta = GetScriptMeta(resolved);
                    if (!meta.Error.empty())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
                        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", meta.Error.c_str());
                        ImGui::PopStyleColor();
                    }
                    // ── Sınıf ── (boşsa ilk betik sınıfı: alanlar hemen görünsün)
                    if (script.ClassName.empty() && !meta.Classes.empty())
                        script.ClassName = meta.Classes.front();
                    if (meta.Classes.size() > 1 || (!meta.Classes.empty() && script.ClassName != meta.Classes.front()))
                    {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Class");
                        ImGui::SameLine(110.0f);
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##class", script.ClassName.empty() ? "(first script class)" : script.ClassName.c_str()))
                        {
                            for (auto& cls : meta.Classes)
                                if (ImGui::Selectable(cls.c_str(), cls == script.ClassName))
                                    script.ClassName = cls;
                            ImGui::EndCombo();
                        }
                    }
                    else if (script.ClassName.empty() && !meta.Classes.empty())
                        script.ClassName = meta.Classes.front();

                    // ── Alanlar ──
                    auto fit = meta.Fields.find(script.ClassName);
                    if (fit != meta.Fields.end() && !fit->second.empty())
                    {
                        ImGui::Spacing();
                        if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 106.0f);
                            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                            for (const auto& f : fit->second)
                            {
                                // Unity: [Header("Hareket")] başlık satırı, [Space] boşluk
                                std::string header = QuotedAttribute(f.Attributes, "Header(");
                                if (!header.empty() || HasSpaceAttribute(f.Attributes))
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    ImGui::Dummy(ImVec2(0.0f, header.empty() ? 6.0f : 3.0f));
                                    if (!header.empty())
                                        ImGui::TextColored(ImVec4(0.62f, 0.74f, 1.0f, 1.0f), "%s", header.c_str());
                                }
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::AlignTextToFramePadding();
                                bool overridden = script.FieldOverrides.count(f.Name) != 0;
                                if (overridden)
                                {
                                    // Unity: değiştirilmiş değerin solunda mavi işaret
                                    ImVec2 p = ImGui::GetCursorScreenPos();
                                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 6, p.y + 2), ImVec2(p.x - 4, p.y + ImGui::GetFrameHeight() - 2),
                                                                              ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 1)));
                                }
                                ImGui::TextUnformatted(PrettyFieldName(f.Name).c_str());
                                std::string tip = TooltipAttribute(f.Attributes);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                    ImGui::SetTooltip("%s%s%s (%s)", tip.c_str(), tip.empty() ? "" : "\n", f.Name.c_str(), f.TypeName.empty() ? "var" : f.TypeName.c_str());
                                if (ImGui::BeginPopupContextItem(("##ctx" + f.Name).c_str()))
                                {
                                    if (ImGui::MenuItem("Reset to Default", nullptr, false, overridden))
                                        script.FieldOverrides.erase(f.Name);
                                    ImGui::EndPopup();
                                }
                                ImGui::TableNextColumn();
                                std::string id = "##" + f.Name;
                                if (playing && script.Instance && script.Runtime)
                                {
                                    // Oyun modunda canlı değer
                                    auto* runtime = static_cast<Scripting::ScriptRuntime*>(script.Runtime);
                                    ScriptFieldOverride live;
                                    std::string display;
                                    if (runtime->GetField(script.Instance, f.Name, live, &display))
                                    {
                                        if (live.Type == ScriptFieldOverride::Kind::Null && !display.empty() && display != "null")
                                        {
                                            if (display.size() > 120) display = display.substr(0, 117) + "...";
                                            ImGui::TextDisabled("%s", display.c_str());
                                            continue;
                                        }
                                        // LuaQ sayıları (Luau VM) double: alan float tanımlıysa float göster
                                        if (live.Type == ScriptFieldOverride::Kind::Int && f.Default.Type == ScriptFieldOverride::Kind::Float)
                                            live = ScriptFieldOverride::MakeFloat((double)live.Int);
                                        if (DrawScriptFieldValue(id.c_str(), live, f))
                                            runtime->SetField(script.Instance, f.Name, live);
                                        continue;
                                    }
                                }
                                ScriptFieldOverride v = overridden ? script.FieldOverrides[f.Name] : FieldDefault(f);
                                if (DrawScriptFieldValue(id.c_str(), v, f))
                                    script.FieldOverrides[f.Name] = v;
                            }
                            ImGui::EndTable();
                        }
                    }
                    else if (meta.Error.empty())
                        ImGui::TextDisabled("No public fields.");
                }

                if (playing)
                {
                    if (script.Instance)
                        ImGui::TextColored(UI::Colors::Success, ICON_FA_CIRCLE_PLAY "  Running");
                    else if (script.bLoaded)
                        ImGui::TextColored(UI::Colors::Error, ICON_FA_CIRCLE_XMARK "  Failed to start (see Log)");
                }
                ImGui::Unindent(4.0f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (indexToRemove != -1)
        {
            csc.Scripts.erase(csc.Scripts.begin() + indexToRemove);
            if (csc.Scripts.empty())
                entity.RemoveComponent<ConquerorScriptComponent>();
        }
    }

    void DrawParticleSystemUI(ParticleSystemComponent& ps)
{
        auto& rt = *ps.RT;
        UI::BeginProperty("Asset", "A Particle System asset (.cqparticle): when set, these settings come from the file.");
        AssetUI::AssetSlot("##psasset", "Particle System", { ".cqparticle" }, ps.Asset);
        UI::EndProperty();
        if (!ps.Asset.empty())
        {
            if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save To Asset"))
                ParticleAssets::Save(ps.Asset, ps);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE "  Load From Asset"))
                rt.AppliedAsset.clear();
        }
        UI::BeginProperty("Sub UV", "A Sub UV Animation asset (.cqsubuv): sets the texture sheet.");
        AssetUI::AssetSlot("##pssubuv", "Sub UV Animation", { ".cqsubuv" }, ps.SubUV);
        UI::EndProperty();
        // Oynatma kontrolleri + hazır ayarlar
        {
            if (ImGui::Button(rt.Playing && !rt.Paused ? ICON_FA_PAUSE " Pause" : ICON_FA_PLAY " Play"))
            {
                if (rt.Playing && !rt.Paused) ParticleSystem::Pause(ps);
                else ParticleSystem::Play(ps);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Restart")) ParticleSystem::Restart(ps);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " Stop")) ParticleSystem::Stop(ps, true);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##preset", ICON_FA_WAND_MAGIC_SPARKLES " Preset"))
            {
                for (const auto& name : ParticleSystem::GetPresetNames())
                    if (ImGui::Selectable(name.c_str()))
                        ParticleSystem::ApplyPreset(ps, name);
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Particles: %zu   |   Time: %.2f s", rt.Particles.size(), rt.Time);
        }

        auto module = [](const char* name, bool* enabled, bool defaultOpen = false) -> bool {
            ImGui::PushID(name);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
            if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
            bool open = ImGui::TreeNodeEx("##mod", flags, "%s%s", enabled ? "       " : "", name);
            if (enabled)
            {
                ImVec2 min = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(ImVec2(min.x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f, min.y + 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                ImGui::Checkbox("##en", enabled);
                ImGui::PopStyleVar();
            }
            ImGui::PopID();
            if (open && enabled && !*enabled) ImGui::BeginDisabled();
            return open;
        };
        auto endModule = [](bool* enabled) {
            if (enabled && !*enabled) ImGui::EndDisabled();
            ImGui::TreePop();
        };

        if (module("Main", nullptr, true))
        {
            UI::DragFloat("Duration", ps.Duration, 0.05f, 0.05f, 1000.0f, "%.2f s");
            UI::Checkbox("Looping", ps.Looping);
            UI::Checkbox("Prewarm", ps.Prewarm);
            static const char* const kStopActions[] = { "None", "Disable", "Destroy" };
            UI::Combo("Stop Action", ps.StopAction, kStopActions, 3, "What happens to the object when the system has finished playing (non-looping) and every particle has died");
            UI::DragFloat("Start Delay", ps.StartDelay, 0.05f, 0.0f, 100.0f, "%.2f s");
            UI::Vec2("Start Lifetime", ps.StartLifetime, 0.05f);
            UI::Vec2("Start Speed", ps.StartSpeed, 0.05f);
            UI::Vec2("Start Size", ps.StartSize, 0.01f);
            UI::Vec2("Start Rotation", ps.StartRotation, 1.0f);
            UI::Color4("Start Color A", ps.StartColorA);
            UI::Color4("Start Color B", ps.StartColorB);
            UI::DragFloat("Gravity Modifier", ps.GravityModifier, 0.01f, -10.0f, 10.0f);
            const char* spaces[] = { "Local", "World" };
            UI::ComboEnum("Simulation Space", ps.SimulationSpace, spaces, 2);
            UI::DragFloat("Simulation Speed", ps.SimulationSpeed, 0.01f, 0.0f, 10.0f);
            UI::Checkbox("Play On Awake", ps.PlayOnAwake);
            UI::DragInt("Max Particles", ps.MaxParticles, 10.0f, 1, 100000);
            endModule(nullptr);
        }
        if (module("Emission", &ps.EmissionEnabled, true))
        {
            UI::DragFloat("Rate over Time", ps.RateOverTime, 0.5f, 0.0f, 100000.0f, "%.1f /s");
            UI::DragFloat("Rate over Distance", ps.RateOverDistance, 0.1f, 0.0f, 10000.0f, "%.1f /m");
            int remove = -1;
            for (int i = 0; i < (int)ps.Bursts.size(); ++i)
            {
                ImGui::PushID(i);
                auto& b = ps.Bursts[(size_t)i];
                UI::BeginProperty("Burst", "Time (s), Count, Cycles, Interval (s)");
                float w = (ImGui::GetContentRegionAvail().x - 34.0f) / 4.0f;
                ImGui::SetNextItemWidth(w); ImGui::DragFloat("##t", &b.Time, 0.01f, 0.0f, 1000.0f, "%.2fs"); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(w); ImGui::DragInt("##c", &b.Count, 1.0f, 0, 100000); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(w); ImGui::DragInt("##y", &b.Cycles, 0.1f, 1, 1000, "x%d"); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(w); ImGui::DragFloat("##i", &b.Interval, 0.01f, 0.001f, 100.0f, "%.2fs"); ImGui::SameLine(0, 2);
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
                UI::EndProperty();
                ImGui::PopID();
            }
            if (remove >= 0) ps.Bursts.erase(ps.Bursts.begin() + remove);
            UI::BeginProperty("");
            if (ImGui::SmallButton(ICON_FA_PLUS " Add Burst")) ps.Bursts.push_back({});
            UI::EndProperty();
            endModule(&ps.EmissionEnabled);
        }
        if (module("Shape", &ps.ShapeEnabled))
        {
            const char* shapes[] = { "Sphere", "Hemisphere", "Cone", "Box", "Circle", "Edge", "Point" };
            UI::ComboEnum("Shape", ps.Shape, shapes, 7);
            if (ps.Shape == ParticleShape::Box) UI::Vec3("Box Size", ps.BoxSize, 0.05f, 1.0f);
            else if (ps.Shape != ParticleShape::Point) UI::DragFloat("Radius", ps.Radius, 0.01f, 0.0f, 1000.0f);
            if (ps.Shape == ParticleShape::Cone) UI::SliderFloat("Angle", ps.Angle, 0.0f, 89.0f, "%.0f deg");
            UI::Checkbox("Emit from Shell", ps.EmitFromShell);
            UI::SliderFloat("Randomize Direction", ps.RandomizeDirection, 0.0f, 1.0f);
            endModule(&ps.ShapeEnabled);
        }
        if (module("Velocity over Lifetime", &ps.VelocityEnabled))
        {
            UI::Vec3("Linear", ps.VelocityOverLifetime, 0.05f);
            UI::DragFloat("Orbital (Y)", ps.OrbitalSpeed, 0.05f, -100.0f, 100.0f, "%.2f rad/s");
            endModule(&ps.VelocityEnabled);
        }
        if (module("Limit Velocity", &ps.LimitVelocityEnabled))
        {
            UI::DragFloat("Drag", ps.Drag, 0.01f, 0.0f, 100.0f);
            UI::DragFloat("Speed Limit", ps.SpeedLimit, 0.1f, 0.0f, 10000.0f);
            endModule(&ps.LimitVelocityEnabled);
        }
        if (module("Noise", &ps.NoiseEnabled))
        {
            UI::DragFloat("Strength", ps.NoiseStrength, 0.01f, 0.0f, 100.0f);
            UI::DragFloat("Frequency", ps.NoiseFrequency, 0.01f, 0.0f, 50.0f);
            UI::DragFloat("Scroll Speed", ps.NoiseScrollSpeed, 0.01f, 0.0f, 50.0f);
            endModule(&ps.NoiseEnabled);
        }
        if (module("Color over Lifetime", &ps.ColorOverLifetimeEnabled))
        {
            UI::GradientEditor("Color", ps.ColorOverLifetime);
            endModule(&ps.ColorOverLifetimeEnabled);
        }
        if (module("Size over Lifetime", &ps.SizeOverLifetimeEnabled))
        {
            UI::CurveEditor("Size", ps.SizeOverLifetime, 0.0f, 3.0f);
            endModule(&ps.SizeOverLifetimeEnabled);
        }
        if (module("Rotation over Lifetime", &ps.RotationOverLifetimeEnabled))
        {
            UI::DragFloat("Angular Velocity", ps.AngularVelocity, 1.0f, -3600.0f, 3600.0f, "%.0f deg/s");
            endModule(&ps.RotationOverLifetimeEnabled);
        }
        if (module("Collision (Ground Plane)", &ps.CollisionEnabled))
        {
            UI::DragFloat("Plane Height", ps.CollisionPlaneY, 0.05f, -10000.0f, 10000.0f);
            UI::SliderFloat("Bounce", ps.Bounce, 0.0f, 1.0f);
            UI::SliderFloat("Lifetime Loss", ps.LifetimeLoss, 0.0f, 1.0f);
            endModule(&ps.CollisionEnabled);
        }
        if (module("Texture Sheet Animation", &ps.TextureSheetEnabled))
        {
            UI::BeginProperty("Tiles");
            ImGui::DragInt2("##tiles", &ps.Tiles.x, 0.1f, 1, 64);
            UI::EndProperty();
            UI::DragFloat("Cycles", ps.AnimationCycles, 0.05f, 0.01f, 100.0f);
            UI::Checkbox("Random Start Frame", ps.RandomStartFrame);
            endModule(&ps.TextureSheetEnabled);
        }
        if (module("Renderer", nullptr, true))
        {
            const char* modes[] = { "Billboard", "Stretched Billboard", "Horizontal Billboard", "Vertical Billboard" };
            UI::ComboEnum("Render Mode", ps.RenderMode, modes, 4);
            if (ps.RenderMode == ParticleRenderMode::StretchedBillboard)
            {
                UI::DragFloat("Length Scale", ps.LengthScale, 0.01f, 0.0f, 100.0f);
                UI::DragFloat("Speed Scale", ps.SpeedScale, 0.01f, 0.0f, 10.0f);
            }
            const char* blends[] = { "Alpha Blended", "Additive", "Premultiplied" };
            UI::ComboEnum("Blend", ps.Blend, blends, 3);
            UI::BeginProperty("Texture", "Drag an image from the Content Browser (empty = soft circle)");
            std::string texName = ps.TexturePath.empty() ? std::string("Default (soft circle)") : std::filesystem::path(ps.TexturePath).filename().string();
            ImGui::Button(texName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 2.0f * (ImGui::GetFrameHeight() + 4.0f), 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    ps.TexturePath = (const char*)p->Data;
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0, 4);
            if (std::string picked; ObjectPicker::Button("ptexpick", "Texture", ObjectPicker::ImageExtensions(), ps.TexturePath, picked))
                ps.TexturePath = picked;
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_XMARK "##cleartex", ImVec2(ImGui::GetFrameHeight(), 0))) ps.TexturePath.clear();
            UI::EndProperty();
            UI::DragFloat("Emissive (HDR)", ps.EmissiveIntensity, 0.05f, 0.0f, 100.0f);
            UI::Checkbox("Sort by Distance", ps.SortByDistance);
            endModule(nullptr);
        }
    }

    void InspectorPanel::DrawComponents(Entity entity)
    {
        // ---- Başlık: aktiflik | ikon | ad | Static
        ImGui::PushID((int)(uint32_t)entity);
        {
            bool visible = true;
            if (entity.HasComponent<ActiveComponent>())
                visible = entity.GetComponent<ActiveComponent>().Active;
            if (ImGui::Checkbox("##visible", &visible))
            {
                if (!entity.HasComponent<ActiveComponent>())
                    entity.AddComponent<ActiveComponent>();
                entity.GetComponent<ActiveComponent>().Active = visible;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Active: inactive objects are not rendered, simulated or updated");
            ImGui::SameLine();

            auto icon = HierarchyPanel::GetPrimaryIcon(entity);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(icon.Color, "%s", icon.Icon);
            ImGui::SameLine();

            const float staticW = ImGui::CalcTextSize("Static").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            if (entity.HasComponent<TagComponent>())
            {
                auto& tagComp = entity.GetComponent<TagComponent>();
                char buffer[256] = {};
                strncpy(buffer, tagComp.Tag.c_str(), sizeof(buffer) - 1);
                ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - staticW));
                if (ImGui::InputText("##EntityName", buffer, sizeof(buffer)))
                    tagComp.Tag = std::string(buffer);

                ImGui::SameLine();
                bool isStatic = tagComp.IsStatic;
                if (ImGui::Checkbox("Static", &isStatic))
                    tagComp.IsStatic = isStatic;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Static objects are included in lightmap/probe baking and batching");
            }
        }
        ImGui::PopID();


        // Tag ve Layer dropdown'ları
        if (entity.HasComponent<TagComponent>())
        {
            auto& tagComp = entity.GetComponent<TagComponent>();
            
            ImGui::Text("Tag:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150.0f);
            if (ImGui::BeginCombo("##GameTag", tagComp.GameTag.c_str()))
            {
                auto& availableTags = Scene::GetAvailableTags();
                for (const auto& tag : availableTags)
                {
                    bool isSelected = (tagComp.GameTag == tag);
                    if (ImGui::Selectable(tag.c_str(), isSelected))
                        tagComp.GameTag = tag;
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }

        ImGui::SameLine();

        // Layer dropdown
        if (entity.HasComponent<LayerComponent>() && m_Context)
        {
            auto& layerComp = entity.GetComponent<LayerComponent>();
            
            ImGui::Text("Layer:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150.0f);
            
            std::string currentLayerName = m_Context->GetLayerName(layerComp.Layer);
            std::string displayName = std::to_string(layerComp.Layer) + ": " + currentLayerName;
            
            static bool openAddLayerPopup = false;
            
            if (ImGui::BeginCombo("##Layer", displayName.c_str()))
            {
                auto& layers = m_Context->GetLayers();
                for (const auto& layer : layers)
                {
                    bool isSelected = (layerComp.Layer == layer.ID);
                    std::string layerDisplay = std::to_string(layer.ID) + ": " + layer.Name;
                    
                    if (ImGui::Selectable(layerDisplay.c_str(), isSelected))
                        layerComp.Layer = layer.ID;
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                
                ImGui::Separator();
                if (ImGui::Selectable("+ Add Layer..."))
                {
                    openAddLayerPopup = true;
                }
                
                ImGui::EndCombo();
            }
            
            // Add Layer Popup (combo dışında)
            if (openAddLayerPopup)
            {
                ImGui::OpenPopup("AddLayerPopup");
                openAddLayerPopup = false;
            }
            
            if (ImGui::BeginPopupModal("AddLayerPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static char layerNameBuffer[128] = "New Layer";
                ImGui::Text("Add New Layer");
                ImGui::Separator();
                ImGui::InputText("Layer Name", layerNameBuffer, sizeof(layerNameBuffer));
                ImGui::Separator();
                
                if (ImGui::Button("Add", ImVec2(120, 0)))
                {
                    m_Context->AddLayer(layerNameBuffer);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::PopItemWidth();
        }
        else
        {
            ImGui::Text("Layer:");
            ImGui::SameLine();
            ImGui::TextDisabled("(No Layer Component)");
        }

        if (!entity.HasComponent<RectTransformComponent>())
        DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
        {
            DrawVec3Control("Position", component.Position);
            glm::vec3 rotation = glm::degrees(component.Rotation);
            DrawVec3Control("Rotation", rotation);
            component.Rotation = glm::radians(rotation);
            DrawVec3Control("Scale", component.Scale, 1.0f);
        });

        DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
        {
            auto& camera = component.Camera;

            ImGui::Checkbox("Primary", &component.Primary);

            const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
            const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
            if (ImGui::BeginCombo("Projection", currentProjectionTypeString))
            {
                for (int i = 0; i < 2; i++)
                {
                    bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
                    if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                    {
                        currentProjectionTypeString = projectionTypeStrings[i];
                        camera.SetProjectionType((SceneCamera::ProjectionType)i);
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
            {
                float perspectiveVerticalFov = glm::degrees(camera.GetPerspectiveVerticalFOV());
                if (ImGui::DragFloat("Vertical FOV", &perspectiveVerticalFov))
                    camera.SetPerspectiveVerticalFOV(glm::radians(perspectiveVerticalFov));

                float perspectiveNear = camera.GetPerspectiveNearClip();
                if (ImGui::DragFloat("Near", &perspectiveNear))
                    camera.SetPerspectiveNearClip(perspectiveNear);

                float perspectiveFar = camera.GetPerspectiveFarClip();
                if (ImGui::DragFloat("Far", &perspectiveFar))
                    camera.SetPerspectiveFarClip(perspectiveFar);
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
            {
                float orthoSize = camera.GetOrthographicSize();
                if (ImGui::DragFloat("Size", &orthoSize))
                    camera.SetOrthographicSize(orthoSize);

                float orthoNear = camera.GetOrthographicNearClip();
                if (ImGui::DragFloat("Near", &orthoNear))
                    camera.SetOrthographicNearClip(orthoNear);

                float orthoFar = camera.GetOrthographicFarClip();
                if (ImGui::DragFloat("Far", &orthoFar))
                    camera.SetOrthographicFarClip(orthoFar);

                ImGui::Checkbox("Fixed Aspect Ratio", &component.FixedAspectRatio);
            }

            // Unity "Background Type": oyunda bu kameranın arkasında ne görüneceği
            const char* backgroundModes[] = { "Auto", "Skybox", "Solid Color" };
            int background = (int)component.Background;
            if (ImGui::Combo("Background", &background, backgroundModes, 3))
                component.Background = (CameraComponent::BackgroundMode)background;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Auto: perspective cameras show the sky, orthographic (2D) cameras a solid color.\n"
                                  "Lighting > Environment > Sky = Solid Color overrides every camera.");
            const bool solid = component.Background == CameraComponent::BackgroundMode::SolidColor ||
                               (component.Background == CameraComponent::BackgroundMode::Auto &&
                                camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic);
            if (solid)
                ImGui::ColorEdit4("Background Color", &component.BackgroundColor.x);
        });

        DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
        {
            // ---- Sprite (Unity Sprite Renderer gibi tek satırlık nesne alanı: küçük önizleme + ad; Content Browser'dan
            // sürükle-bırak; yanında ◎ seçici: Assets / Packages (Square, Circle, Triangle ...) / Scene)
            {
                UI::BeginProperty("Sprite", "The sprite to draw. Drag an image from the Content Browser or use the target button.");
                const float h = ImGui::GetFrameHeight();
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const bool none = !component.Texture;
                std::string shown = "None (Sprite)";
                if (!none)
                {
                    shown = !component.SpriteName.empty() ? component.SpriteName : std::filesystem::path(component.Texture->GetPath()).stem().string();
                    if (shown.empty())
                        shown = "Custom";
                }
                const float extra = (none ? 1 : 2) * (h + spacing);
                ImGui::Button("##spriteslot", ImVec2(std::max(ImGui::GetContentRegionAvail().x - extra, h * 2.0f), 0.0f));
                const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float textX = rmin.x + 6.0f;
                if (!none && component.Texture->GetRendererID())
                {
                    // Önizleme kutunun içinde, solda (Unity'deki sprite simgesi yerine sprite'ın kendisi)
                    const float s = h - 6.0f;
                    const ImVec2 i0(rmin.x + 4.0f, rmin.y + 3.0f), i1(i0.x + s, i0.y + s);
                    dl->AddRectFilled(i0, i1, IM_COL32(30, 31, 35, 255), 2.0f);
                    dl->AddImage((ImTextureID)(uint64_t)component.Texture->GetRendererID(), i0, i1, ImVec2(0, 1), ImVec2(1, 0));
                    textX = i1.x + 6.0f;
                }
                dl->PushClipRect(rmin, rmax, true);
                dl->AddText(ImVec2(textX, rmin.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                            ImGui::GetColorU32(none ? UI::Colors::TextDim : ImGui::GetStyleColorVec4(ImGuiCol_Text)), shown.c_str());
                dl->PopClipRect();
                if (ImGui::IsItemHovered() && !none)
                    ImGui::SetTooltip("%s\n%u x %u", component.Texture->GetPath().c_str(), component.Texture->GetWidth(), component.Texture->GetHeight());
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::filesystem::path path((const char*)payload->Data);
                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
                        {
                            AssetActions::EnsureSpriteImport(path.string());
                            component.Texture = Texture2D::Create(path.string());
                            component.SpriteName.clear();
                            Renderer2D::ApplySpriteImport(component);
                        }
                        else
                            CQ_CORE_WARN("Sprite Renderer: unsupported image type {0}", ext);
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_SPRITE"))
                    {
                        const std::string path((const char*)payload->Data);
                        AssetActions::EnsureSpriteImport(path);
                        component.Texture = Texture2D::Create(path);
                        component.SpriteName = (const char*)payload->Data + path.size() + 1;
                        Renderer2D::ApplySpriteImport(component);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                const ImGuiID spritePick = ImGui::GetID("##spritepick");
                if (ObjectPicker::IconButton("##spritepick"))
                    ObjectPicker::Open(spritePick, "Sprite", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" },
                                       component.Texture ? component.Texture->GetPath() : std::string());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Select Sprite");
                if (std::string picked; ObjectPicker::Consume(spritePick, picked))
                {
                    component.Texture = picked.empty() ? nullptr : Texture2D::Create(picked);
                    component.SpriteName.clear();
                    Renderer2D::ApplySpriteImport(component);
                }
                if (!none)
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_XMARK "##cleartex", ImVec2(h, 0)))
                    {
                        component.Texture = nullptr;
                        component.SpriteName.clear();
                    }
                }
                UI::EndProperty();
            }

            // ---- Sprite sayfası dilimi
            std::shared_ptr<SpriteSheet> sheet = (component.Texture && !component.Texture->GetPath().empty()) ? SpriteSheet::Get(component.Texture->GetPath()) : nullptr;
            if (sheet && !sheet->Sprites.empty() && !ContentBrowserPanel::IsReadOnly(component.Texture->GetPath()))
            {
                UI::BeginProperty("Slice", "Which sprite of the image's sprite sheet (Sprite Editor) to draw");
                if (ImGui::BeginCombo("##subsprite", component.SpriteName.empty() ? "(Full Texture)" : component.SpriteName.c_str()))
                {
                    if (ImGui::Selectable("(Full Texture)", component.SpriteName.empty()))
                        component.SpriteName.clear();
                    for (const auto& def : sheet->Sprites)
                        if (ImGui::Selectable(def.Name.c_str(), def.Name == component.SpriteName))
                            component.SpriteName = def.Name;
                    ImGui::EndCombo();
                }
                UI::EndProperty();
                if (!component.SpriteName.empty() && !sheet->Find(component.SpriteName))
                    UI::InfoBox("The selected slice no longer exists in the sprite sheet.", UI::InfoType::Warning);
            }
            if (component.Texture && !component.Texture->GetPath().empty())
            {
                UI::BeginProperty("");
                if (ImGui::Button(ICON_FA_OBJECT_UNGROUP " Open Sprite Editor", ImVec2(-1, 0)) && InspectorPanel::s_OpenSpriteEditor)
                    InspectorPanel::s_OpenSpriteEditor(component.Texture->GetPath());
                UI::EndProperty();
            }

            UI::Color4("Color", component.Color);
            UI::BeginProperty("Flip");
            ImGui::Checkbox("X##flipx", &component.FlipX);
            ImGui::SameLine();
            ImGui::Checkbox("Y##flipy", &component.FlipY);
            UI::EndProperty();

            const char* modes[] = { "Simple", "Sliced", "Tiled" };
            UI::ComboEnum("Draw Mode", component.DrawMode, modes, 3);
            const char* maskModes[] = { "None", "Visible Inside Mask", "Visible Outside Mask" };
            UI::ComboEnum("Mask Interaction", component.MaskInteraction, maskModes, 3);
            if (component.DrawMode != SpriteDrawMode::Simple)
            {
                UI::Vec2("Size", component.Size, 0.01f);
                if (component.DrawMode == SpriteDrawMode::Sliced)
                {
                    const SpriteDef* def = sheet && !component.SpriteName.empty() ? sheet->Find(component.SpriteName) : nullptr;
                    glm::vec4 border = def ? def->Border : component.Border;
                    if (border == glm::vec4(0.0f))
                        UI::InfoBox("This sprite has no borders. Set them in the Sprite Editor (green lines) or below.", UI::InfoType::Info);
                }
            }

            // Sayfa dilimi kullanılmıyorsa: özel pivot / PPU / kenarlık
            if (component.SpriteName.empty())
            {
                UI::DragFloat("Pixels Per Unit", component.PixelsPerUnit, 1.0f, 1.0f, 10000.0f, "%.0f");
                UI::Vec2("Pivot", component.Pivot, 0.01f);
                UI::BeginProperty("Border", "9-slice borders in pixels: Left, Bottom, Right, Top");
                ImGui::DragFloat4("##border", &component.Border.x, 0.5f, 0.0f, 4096.0f, "%.0f");
                UI::EndProperty();
            }
        });

        DrawComponent<ParticleSystemComponent>("Particle System", entity, [](auto& ps) { DrawParticleSystemUI(ps); });

        DrawComponent<TerrainComponent>("Terrain", entity, [entity](auto& t)
        {
            TerrainSystem::EnsureData(t);
            UI::Text("Heightmap", "%d x %d", t.Resolution, t.Resolution);
            bool changed = false;
            changed |= UI::DragFloat("Width", t.Width, 0.5f, 1.0f, 100000.0f, "%.1f m");
            changed |= UI::DragFloat("Length", t.Length, 0.5f, 1.0f, 100000.0f, "%.1f m");
            changed |= UI::DragFloat("Height", t.Height, 0.2f, 0.1f, 10000.0f, "%.1f m");
            if (changed)
            {
                t.MeshDirty = true;
                t.ColliderVersion++;
            }
            UI::SliderInt("Texture Layers", t.LayerCount, 1, TerrainComponent::MaxLayers);
            UI::Checkbox("Cast Shadows", t.CastShadows);
            UI::Checkbox("Receive Shadows", t.ReceiveShadows);
            {
                Entity e = entity;
                bool collider = e.HasComponent<TerrainColliderComponent>();
                if (UI::Checkbox("Collider", collider, "Adds or removes the Terrain Collider component."))
                {
                    if (collider && !e.HasComponent<TerrainColliderComponent>()) e.AddComponent<TerrainColliderComponent>();
                    else if (!collider && e.HasComponent<TerrainColliderComponent>()) e.RemoveComponent<TerrainColliderComponent>();
                }
            }
            UI::InfoBox("Sculpt, paint and generate in the Terrain panel (Window > Terrain).", UI::InfoType::Info);
        });

        DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [entity](auto& component)
        {
            // Unity Mesh Renderer: ağ + malzeme yuvası + ışıklandırma; doku / renk / metalik malzemenin içinde
            // Mesh (Unity Mesh Filter alanı): kutuda ağın adı; Content Browser'dan .cqmesh ya da model dosyası (.glb / .gltf /
            // .fbx / .obj) sürükle-bırak; ◎ seçici Packages'taki yerleşik ağları (Packages/Core/Meshes) ve projedeki ağları /
            // modelleri listeler. Model verilince ilkel yerine model çizilir (eski Model bileşeni buraya katıldı)
            {
                static const char* meshTypeNames[] = { "Cube", "Sphere", "Plane", "Cylinder" };
                auto meshFromFile = [](const std::string& file, MeshType& out) {
                    std::ifstream in(SceneSerializer::ResolveAssetPath(file));
                    std::string line, name = std::filesystem::path(file).stem().string();
                    while (std::getline(in, line))
                        if (line.rfind("Mesh:", 0) == 0)
                        {
                            name = line.substr(5);
                            name.erase(0, name.find_first_not_of(" \t"));
                            name.erase(name.find_last_not_of(" \t\r") + 1);
                            break;
                        }
                    for (int i = 0; i < 4; ++i)
                        if (name == meshTypeNames[i]) { out = static_cast<MeshType>(i); return true; }
                    return false;
                };
                auto isModelFile = [](const std::string& file) {
                    std::string ext = std::filesystem::path(file).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    const auto& exts = ObjectPicker::ModelExtensions();
                    return std::find(exts.begin(), exts.end(), ext) != exts.end();
                };
                // Mesh alanına dosya ata: model ya da yerleşik ağ (.cqmesh)
                auto assign = [&](const std::string& file) {
                    MeshType t;
                    if (isModelFile(file))
                    {
                        auto model = ModelLoader::Load(file);
                        if (!model) { CQ_CORE_WARN("Mesh Renderer: model could not be loaded: {0}", file); return; }
                        component.ModelPath = ToSerializablePath(file);
                        component.ModelData = model;
                        component.MaterialInstance = nullptr;   // model kendi malzemeleriyle çizilir
                        component.MaterialPath.clear();
                    }
                    else if (std::filesystem::path(file).extension() == ".cqmesh" && meshFromFile(file, t))
                    {
                        const bool wasModel = component.IsModel();
                        component.ModelPath.clear();
                        component.ModelData = nullptr;
                        component.Type = t;
                        if (wasModel && !component.MaterialInstance)
                        {
                            component.MaterialPath = MaterialAsset::DefaultGridPath();
                            component.MaterialInstance = MaterialAsset::DefaultGrid();
                            if (!component.MaterialInstance)
                                component.MaterialPath.clear();
                        }
                    }
                    else
                        CQ_CORE_WARN("Mesh Renderer: '{0}' is not a mesh or model", file);
                };
                const int typeIndex = std::clamp(static_cast<int>(component.Type), 0, 3);
                const bool isModel = component.IsModel();
                std::string shown = meshTypeNames[typeIndex];
                if (isModel)
                    shown = std::filesystem::path(component.ModelPath).stem().string() + (component.ModelData ? "" : "  (missing)");
                UI::BeginProperty("Mesh", "The mesh to draw. Drag a mesh or a model file (.glb, .gltf, .fbx, .obj) from the Content Browser, "
                                          "or use the target button (Packages > Core > Meshes: Cube, Sphere, Plane, Cylinder).");
                const float h = ImGui::GetFrameHeight();
                ImGui::Button("##meshslot", ImVec2(std::max(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x, h * 2.0f), 0.0f));
                {
                    const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->PushClipRect(rmin, rmax, true);
                    const float ty = rmin.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
                    dl->AddText(ImVec2(rmin.x + 6.0f, ty), IM_COL32(150, 190, 235, 255), isModel ? ICON_FA_CUBES : ICON_FA_CUBE);
                    dl->AddText(ImVec2(rmin.x + 6.0f + h, ty), ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
                    dl->PopClipRect();
                }
                if (ImGui::IsItemHovered() && isModel)
                    ImGui::SetTooltip("%s", component.ModelPath.c_str());
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        assign(std::string((const char*)payload->Data));
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                const ImGuiID meshPick = ImGui::GetID("##meshpick");
                if (ObjectPicker::IconButton("##meshpick"))
                {
                    std::vector<std::string> exts = { ".cqmesh" };
                    for (const auto& e : ObjectPicker::ModelExtensions())
                        exts.push_back(e);
                    ObjectPicker::Open(meshPick, "Mesh", exts,
                                       isModel ? SceneSerializer::ResolveAssetPath(component.ModelPath)
                                               : SceneSerializer::ResolveAssetPath(std::string("CQ:Resources/Packages/Core/Meshes/") + meshTypeNames[typeIndex] + ".cqmesh"));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Select Mesh");
                if (std::string picked; ObjectPicker::Consume(meshPick, picked) && !picked.empty())
                    assign(picked);
                UI::EndProperty();

                if (isModel && component.ModelData)
                {
                    const auto& md = *component.ModelData;
                    if (md.IsSkinned)
                        UI::Text("Model", "%zu skinned meshes, %zu animations, %u bones", md.SkinnedMeshes.size(), md.Animations.size(),
                                 md.SkeletonData ? md.SkeletonData->GetBoneCount() : 0u);
                    else
                        UI::Text("Model", "%zu meshes", md.Meshes.size());
                }
                if (isModel)
                {
                    UI::BeginProperty("Tint", "Per-object color multiplier (the shared material is not changed)");
                    ImGui::ColorEdit3("##tint", glm::value_ptr(component.Tint));
                    UI::EndProperty();
                }
            }

            if (UI::BeginSection("Materials", true))
            {
                Entity e = entity;
                std::string name = e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : std::string();
                if (component.IsModel())
                {
                    // Model: malzemeler modelin kendisinden (içe aktarma ayarlarındaki eşleme / Extract Materials). Burada
                    // bu oturum için ince ayar yapılabilir (Unity'de modelin malzemeleri gibi)
                    const size_t count = component.ModelData ? component.ModelData->Materials.size() : 0;
                    UI::Text("Size", "%zu", count);
                    // Tree bileşeninin ürettiği ağaç: malzemeler grupların Branch / Leaf Material alanından (varsayılanlar
                    // bütün ağaçlarda ortak; burada değiştirilmez)
                    const bool generatedTree = component.ModelPath.empty() && component.ModelData && component.ModelData->Directory == "<tree>";
                    if (generatedTree)
                    {
                        for (size_t i = 0; i < count; ++i)
                            if (const auto& mat = component.ModelData->Materials[i])
                                UI::Text(("Element " + std::to_string(i)).c_str(), "%s", mat->Name.empty() ? "Material" : mat->Name.c_str());
                        UI::InfoBox("Generated by the Tree component: set each group's Branch Material or Leaf Material there.", UI::InfoType::Info);
                    }
                    for (size_t i = 0; i < count && !generatedTree; ++i)
                    {
                        auto& mat = component.ModelData->Materials[i];
                        if (!mat)
                            continue;
                        const std::string label = "Element " + std::to_string(i) + "  " + (mat->Name.empty() ? std::string("Material") : mat->Name) +
                                                  "##modelmat" + std::to_string(i);
                        if (ImGui::TreeNode(label.c_str()))
                        {
                            ImGui::PushID((int)i);
                            UI::Color3("Albedo", mat->Albedo);
                            UI::SliderFloat("Metallic", mat->Metallic, 0.0f, 1.0f);
                            UI::SliderFloat("Roughness", mat->Roughness, 0.0f, 1.0f);
                            UI::SliderFloat("AO", mat->AO, 0.0f, 1.0f);
                            UI::Color3("Emission Color", mat->EmissionColor);
                            UI::DragFloat("Emission Strength", mat->EmissionStrength, 0.1f, 0.0f, 100.0f);
                            ImGui::PopID();
                            ImGui::TreePop();
                        }
                    }
                    if (!generatedTree)
                        UI::InfoBox("Model materials come from the model's import settings (select the model file: Materials tab, "
                                    "Extract Materials to edit them as .cqmat files).", UI::InfoType::Info);
                }
                else
                    MaterialEditorUI::DrawMeshRendererMaterials(component, name);
                UI::EndSection();
            }

            if (UI::BeginSection("Lighting", true))
            {
                UI::Checkbox("Cast Shadows", component.CastShadows);
                UI::Checkbox("Receive Shadows", component.ReceiveShadows);
                UI::Checkbox("Realtime GI", component.RealtimeGI);
                MaterialEditorUI::AssetPathSlot("Lightmap Parameters", component.LightmapParameters, ".cqlmparams", "Lightmap Parameters",
                                                "Bake quality for this object (anti-aliasing, backface tolerance, pushoff). "
                                                "Empty = the default set in Lighting Settings.");
                UI::EndSection();
            }
        });

        DrawComponent<AnimatorComponent>("Animator", entity, [](auto& component)
        {
            ImGui::Checkbox("Playing", &component.Playing);
            ImGui::DragFloat("Speed", &component.Speed, 0.05f, -5.f, 5.f);
            ImGui::DragInt("Clip Index", &component.ActiveClipIndex, 0.1f, 0, 64);
            ImGui::Text("Time: %.2f s", component.CurrentTime);
        });

        // AnimationComponent - needs entity access for model clips
        if (entity.HasComponent<AnimationComponent>())
        {
            auto& component = entity.GetComponent<AnimationComponent>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(AnimationComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "Animation");
            ImGui::PopStyleVar();

            ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
            if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool removeComponent = false;
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem("Remove component"))
                    removeComponent = true;
                ImGui::EndPopup();
            }

            if (open)
            {
                // Controller (Unity Animator.controller): Animator Controller ya da Animator Override Controller; Content
                // Browser'dan bırak ya da ◎ ile seç
                {
                    std::string rel = component.ControllerFilePath.empty() ? std::string()
                                                                           : AssetUI::ToProjectRelative(std::filesystem::path(component.ControllerFilePath));
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Controller");
                    ImGui::SameLine(110.0f);
                    if (AssetUI::AssetSlot("##animcontroller", "Animator Controller", { ".cqcont", ".cqoverride" }, rel))
                    {
                        component.ControllerFilePath = rel.empty() ? std::string() : AssetUI::Resolve(rel).string();
                        component.Controller = rel.empty() ? nullptr : AnimationController::Deserialize(component.ControllerFilePath);
                    }
                }

                ImGui::Checkbox("Play On Awake", &component.PlayOnAwake);
                ImGui::DragFloat("Speed", &component.Speed, 0.05f, -5.f, 5.f);

                // Parameters
                if (component.Controller && !component.Parameters.empty())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("Parameters");

                    for (auto& param : component.Parameters)
                    {
                        ImGui::PushID(param.Name.c_str());

                        if (param.Type == AnimParameterType::Float)
                        {
                            ImGui::DragFloat(param.Name.c_str(), &param.FloatValue, 0.01f);
                        }
                        else if (param.Type == AnimParameterType::Bool)
                        {
                            ImGui::Checkbox(param.Name.c_str(), &param.BoolValue);
                        }
                        else if (param.Type == AnimParameterType::Int)
                        {
                            ImGui::DragInt(param.Name.c_str(), &param.IntValue);
                        }
                        else if (param.Type == AnimParameterType::Trigger)
                        {
                            if (ImGui::Button(param.Name.c_str()))
                            {
                                param.FloatValue = 1.f;
                            }
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::TreePop();
            }

            if (removeComponent)
                entity.RemoveComponent<AnimationComponent>();
        }

        DrawComponent<TextRendererComponent>("Text Renderer", entity, [](auto& component)
        {
            // Text input
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, component.Text.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputTextMultiline("Text", buffer, sizeof(buffer), ImVec2(-1, 100)))
            {
                component.Text = std::string(buffer);
            }

            ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
            ImGui::DragFloat("Font Size", &component.FontSize, 0.1f, 0.1f, 10.0f);
            const char* alignments[] = { "Left", "Center", "Right" };
            int alignment = (int)component.Alignment;
            if (ImGui::Combo("Alignment", &alignment, alignments, 3))
                component.Alignment = (TextAlignment)alignment;
        });

        DrawComponent<LayerComponent>("Layer", entity, [this](auto& component)
        {
            if (m_Context)
            {
                auto& layers = m_Context->GetLayers();
                std::string currentLayerName = m_Context->GetLayerName(component.Layer);
                
                if (ImGui::BeginCombo("Layer", currentLayerName.c_str()))
                {
                    for (const auto& layer : layers)
                    {
                        bool isSelected = (component.Layer == layer.ID);
                        if (ImGui::Selectable(layer.Name.c_str(), isSelected))
                            component.Layer = layer.ID;
                        
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            
            ImGui::DragInt("Order in Layer", &component.OrderInLayer, 1.0f);
        });

        DrawComponent<ButtonComponent>("Button", entity, [this](auto& component)
        {
            ImGui::Checkbox("Interactable", &component.Interactable);
            ImGui::Checkbox("Is Highlighted", &component.IsHighlighted);
            ImGui::Spacing();
            
            // Navigation
            if (ImGui::CollapsingHeader("Navigation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const char* navModes[] = { "None", "Horizontal", "Vertical", "Automatic", "Explicit" };
                int currentMode = (int)component.Navigation;
                if (ImGui::Combo("Mode", &currentMode, navModes, 5))
                {
                    component.Navigation = (ButtonComponent::NavigationMode)currentMode;
                }
                
                // Explicit mode için buton seçiciler
                if (component.Navigation == ButtonComponent::NavigationMode::Explicit && m_Context)
                {
                    ImGui::Indent();
                    
                    // Helper lambda: Buton dropdown çiz
                    auto DrawNavigationCombo = [this, &component](const char* label, entt::entity& targetEntity)
                    {
                        // Seçili butonun adını bul
                        std::string selectedName = "None";
                        if (targetEntity != entt::null && m_Context->m_Registry.valid(targetEntity))
                        {
                            if (m_Context->m_Registry.all_of<TagComponent>(targetEntity))
                            {
                                auto& tag = m_Context->m_Registry.get<TagComponent>(targetEntity);
                                selectedName = tag.Tag;
                            }
                        }
                        
                        if (ImGui::BeginCombo(label, selectedName.c_str()))
                        {
                            // None seçeneği
                            if (ImGui::Selectable("None", targetEntity == entt::null))
                            {
                                targetEntity = entt::null;
                            }
                            
                            // Tüm butonları listele
                            auto buttonView = m_Context->m_Registry.view<ButtonComponent, TagComponent>();
                            int idx = 0;
                            for (auto e : buttonView)
                            {
                                auto& tag = buttonView.get<TagComponent>(e);
                                
                                bool isSelected = (targetEntity == e);
                                
                                // Unique ID için PushID kullan
                                ImGui::PushID(idx++);
                                if (ImGui::Selectable(tag.Tag.c_str(), isSelected))
                                {
                                    targetEntity = e;
                                }
                                ImGui::PopID();
                                
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            
                            ImGui::EndCombo();
                        }
                    };
                    
                    DrawNavigationCombo("Up", component.NavigateUp);
                    DrawNavigationCombo("Down", component.NavigateDown);
                    DrawNavigationCombo("Left", component.NavigateLeft);
                    DrawNavigationCombo("Right", component.NavigateRight);
                    
                    // Debug: Değerleri göster
                    ImGui::Text("Debug: Up=%u Down=%u Left=%u Right=%u", 
                                (uint32_t)component.NavigateUp, (uint32_t)component.NavigateDown, 
                                (uint32_t)component.NavigateLeft, (uint32_t)component.NavigateRight);
                    
                    ImGui::Unindent();
                }
            }
            ImGui::Spacing();

            ImGui::Checkbox("Show Color Details", &component.ShowColorDetails);
            ImGui::Spacing();

            if (component.ShowColorDetails)
            {
                // Background Colors
                if (ImGui::CollapsingHeader("Background Colors", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::ColorEdit4("Normal", glm::value_ptr(component.BackgroundNormalColor));
                    ImGui::ColorEdit4("Hover", glm::value_ptr(component.BackgroundHoverColor));
                    ImGui::ColorEdit4("Pressed", glm::value_ptr(component.BackgroundPressedColor));
                    ImGui::ColorEdit4("Disabled", glm::value_ptr(component.BackgroundDisabledColor));
                    ImGui::ColorEdit4("Highlighted", glm::value_ptr(component.BackgroundHighlightedColor));
                }
                ImGui::Spacing();

                // Text Colors
                if (ImGui::CollapsingHeader("Text Colors", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::ColorEdit4("Normal##Text", glm::value_ptr(component.TextNormalColor));
                    ImGui::ColorEdit4("Hover##Text", glm::value_ptr(component.TextHoverColor));
                    ImGui::ColorEdit4("Pressed##Text", glm::value_ptr(component.TextPressedColor));
                    ImGui::ColorEdit4("Disabled##Text", glm::value_ptr(component.TextDisabledColor));
                    ImGui::ColorEdit4("Highlighted##Text", glm::value_ptr(component.TextHighlightedColor));
                }
                ImGui::Spacing();
            }

            // Color Multiplier
            ImGui::Text("Color Multiplier");
            ImGui::SliderFloat("##ColorMultiplier", &component.ColorMultiplier, 1.0f, 5.0f, "%.3f");
            ImGui::Spacing();

            // Transition
            ImGui::Text("Transition");
            ImGui::DragFloat("Speed", &component.TransitionSpeed, 0.1f, 0.1f, 100.0f);
            ImGui::Spacing();

            // 9-Slice
            ImGui::Text("9-Slice");
            ImGui::Checkbox("Use 9-Slice", &component.Use9Slice);
            if (component.Use9Slice)
            {
                ImGui::DragFloat("Border Size", &component.BorderSize, 0.1f, 0.0f, 100.0f);
            }
            ImGui::Spacing();

            // State Info (read-only)
            ImGui::Separator();
            ImGui::Text("State Info");
            
            const char* stateNames[] = { "Normal", "Hover", "Pressed", "Disabled", "Highlighted" };
            ImGui::Text("Current State: %s", stateNames[(int)component.CurrentState]);
            ImGui::Text("Is Hovered: %s", component.IsHovered ? "Yes" : "No");
            ImGui::Text("Is Pressed: %s", component.IsPressed ? "Yes" : "No");
            ImGui::Spacing();

            // Test Click butonu
            if (ImGui::Button("Test Click"))
            {
                if (component.OnClick)
                    component.OnClick();
                else
                    CQ_CORE_INFO("Button clicked but no callback assigned!");
            }
        });

        DrawComponent<CanvasComponent>("Canvas", entity, [](auto& component)
        {
            const char* renderModes[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
            int currentMode = (int)component.Mode;
            if (ImGui::Combo("Render Mode", &currentMode, renderModes, 3))
            {
                component.Mode = (CanvasComponent::RenderMode)currentMode;
            }
            
            ImGui::Checkbox("Pixel Perfect", &component.PixelPerfect);
            ImGui::DragInt("Sort Order", &component.SortOrder);
        });

        DrawComponent<CanvasScalerComponent>("Canvas Scaler", entity, [](auto& component)
        {
            const char* scaleModes[] = { "Constant Pixel Size", "Scale With Screen Size", "Constant Physical Size" };
            int currentMode = (int)component.ScaleMode;
            if (ImGui::Combo("UI Scale Mode", &currentMode, scaleModes, 3))
            {
                component.ScaleMode = (CanvasScalerComponent::UIScaleMode)currentMode;
            }
            
            if (component.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize)
            {
                UI::Vec2("Reference Resolution", component.ReferenceResolution, 1.0f);
                UI::SliderFloat("Match (Width - Height)", component.MatchWidthOrHeight, 0.0f, 1.0f);
            }
            else
                ImGui::DragFloat("Scale Factor", &component.ScaleFactor, 0.01f, 0.1f, 10.0f);
            ImGui::DragFloat("Reference Pixels Per Unit", &component.ReferencePixelsPerUnit, 1.0f, 1.0f, 1000.0f);
        });

        // ================= UI (RectTransform + widget'lar)
        DrawComponent<RectTransformComponent>("Rect Transform", entity, [](auto& rt)
        {
            // Çapa ön ayarları (Unity): satır = üst/orta/alt/esnet, sütun = sol/orta/sağ/esnet
            UI::BeginProperty("Anchor Preset", "Click: set anchors.  Shift: also pivot.  Alt: also position");
            if (ImGui::Button(ICON_FA_TABLE_CELLS " Presets"))
                ImGui::OpenPopup("##anchorpresets");
            ImGui::SameLine();
            ImGui::TextDisabled("(%.2f,%.2f)-(%.2f,%.2f)", rt.AnchorMin.x, rt.AnchorMin.y, rt.AnchorMax.x, rt.AnchorMax.y);
            if (ImGui::BeginPopup("##anchorpresets"))
            {
                static const float ys[4][2] = { { 1, 1 }, { 0.5f, 0.5f }, { 0, 0 }, { 0, 1 } };
                static const float xs[4][2] = { { 0, 0 }, { 0.5f, 0.5f }, { 1, 1 }, { 0, 1 } };
                static const char* rowNames[] = { "top", "middle", "bottom", "stretch" };
                static const char* colNames[] = { "left", "center", "right", "stretch" };
                ImGui::TextDisabled("Anchor Presets   (Shift: pivot, Alt: position)");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        if (c > 0) ImGui::SameLine();
                        ImGui::PushID(r * 4 + c);
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        bool cur = rt.AnchorMin == glm::vec2(xs[c][0], ys[r][0]) && rt.AnchorMax == glm::vec2(xs[c][1], ys[r][1]);
                        if (ImGui::Button("##p", ImVec2(44, 44)))
                        {
                            rt.AnchorMin = { xs[c][0], ys[r][0] };
                            rt.AnchorMax = { xs[c][1], ys[r][1] };
                            if (ImGui::GetIO().KeyShift)
                                rt.Pivot = { c == 3 ? 0.5f : xs[c][0], r == 3 ? 0.5f : ys[r][0] };
                            if (ImGui::GetIO().KeyAlt)
                            {
                                rt.AnchoredPosition = glm::vec2(0.0f);
                                if (c == 3) rt.SizeDelta.x = 0.0f;
                                if (r == 3) rt.SizeDelta.y = 0.0f;
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s %s", rowNames[r], colNames[c]);
                        // simge: kutu + çapa noktaları/çizgileri
                        ImU32 col = cur ? IM_COL32(90, 170, 255, 255) : IM_COL32(200, 200, 205, 255);
                        dl->AddRect(ImVec2(p.x + 10, p.y + 10), ImVec2(p.x + 34, p.y + 34), IM_COL32(140, 140, 150, 255));
                        float ax0 = p.x + 10 + 24 * xs[c][0], ax1 = p.x + 10 + 24 * xs[c][1];
                        float ay0 = p.y + 34 - 24 * ys[r][0], ay1 = p.y + 34 - 24 * ys[r][1];
                        if (c == 3) dl->AddLine(ImVec2(ax0, p.y + 22), ImVec2(ax1, p.y + 22), col, 2.0f);
                        if (r == 3) dl->AddLine(ImVec2(p.x + 22, ay0), ImVec2(p.x + 22, ay1), col, 2.0f);
                        if (c != 3 && r != 3) dl->AddCircleFilled(ImVec2(ax0, ay0), 3.5f, col);
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }
            UI::EndProperty();

            bool stretchX = rt.AnchorMin.x != rt.AnchorMax.x, stretchY = rt.AnchorMin.y != rt.AnchorMax.y;
            UI::Vec2("Position", rt.AnchoredPosition, 1.0f);
            UI::BeginProperty(stretchX || stretchY ? "Size Delta" : "Width / Height");
            ImGui::DragFloat2("##size", &rt.SizeDelta.x, 1.0f);
            UI::EndProperty();
            UI::Vec2("Anchor Min", rt.AnchorMin, 0.01f);
            UI::Vec2("Anchor Max", rt.AnchorMax, 0.01f);
            UI::Vec2("Pivot", rt.Pivot, 0.01f);
            UI::DragFloat("Rotation", rt.Rotation, 0.5f, -360.0f, 360.0f, "%.1f deg");
            UI::Vec2("Scale", rt.Scale, 0.01f);
            UI::Text("Computed", "%.0f x %.0f px", rt.ComputedRect.z, rt.ComputedRect.w);
        });

        auto selectableUI = [](UISelectableState& s) {
            UI::Checkbox("Interactable", s.Interactable);
            const char* transitions[] = { "None", "Color Tint", "Scale" };
            UI::ComboEnum("Transition", s.Transition, transitions, 3);
            if (s.Transition == UITransition::ColorTint)
            {
                UI::Color4("Normal Color", s.NormalColor);
                UI::Color4("Highlighted", s.HighlightedColor);
                UI::Color4("Pressed", s.PressedColor);
                UI::Color4("Disabled", s.DisabledColor);
            }
            UI::DragFloat("Fade Duration", s.FadeDuration, 0.01f, 0.0f, 2.0f, "%.2f s");
        };
        auto textureField = [](const char* label, std::string& path) {
            UI::BeginProperty(label, "Drag an image from the Content Browser");
            std::string name = path.empty() ? std::string("None") : std::filesystem::path(path).filename().string();
            ImGui::Button(name.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 2.0f * (ImGui::GetFrameHeight() + 4.0f), 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    path = (const char*)p->Data;
                ImGui::EndDragDropTarget();
            }
            ImGui::PushID(label);
            ImGui::SameLine(0, 4);
            if (std::string picked; ObjectPicker::Button("uitexpick", "Sprite", ObjectPicker::ImageExtensions(), path, picked))
                path = picked;
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(ImGui::GetFrameHeight(), 0))) path.clear();
            ImGui::PopID();
            UI::EndProperty();
        };
        // Unity Graphic.material: Shader alanında bir UI shader'ı olan .cqmat (Create > Shader > UI Shader); boş = varsayılan
        auto materialField = [](std::string& path) {
            UI::BeginProperty("Material", "A material whose Shader is a UI shader (Create > Shader > UI Shader). None = default UI drawing.");
            std::string name = path.empty() ? std::string("None (Default UI)") : std::filesystem::path(path).stem().string();
            ImGui::Button(name.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 2.0f * (ImGui::GetFrameHeight() + 4.0f), 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const std::string dropped = (const char*)p->Data;
                    if (std::filesystem::path(dropped).extension() == ".cqmat")
                        path = SceneSerializer::ToAssetPath(dropped);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PushID("uimaterial");
            ImGui::SameLine(0, 4);
            if (std::string picked; ObjectPicker::Button("uimatpick", "Material", { ".cqmat" }, path.empty() ? path : SceneSerializer::ResolveAssetPath(path), picked))
                path = picked.empty() ? std::string() : SceneSerializer::ToAssetPath(picked);
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(ImGui::GetFrameHeight(), 0))) path.clear();
            ImGui::PopID();
            UI::EndProperty();
        };

        DrawComponent<UIImageComponent>("Image (UI)", entity, [&](auto& c)
        {
            textureField("Source Image", c.TexturePath);
            if (!c.TexturePath.empty())
                if (auto sheet = SpriteSheet::Get(c.TexturePath); sheet && !sheet->Sprites.empty())
                {
                    UI::BeginProperty("Sprite");
                    if (ImGui::BeginCombo("##uisprite", c.SpriteName.empty() ? "(Full Texture)" : c.SpriteName.c_str()))
                    {
                        if (ImGui::Selectable("(Full Texture)", c.SpriteName.empty())) c.SpriteName.clear();
                        for (auto& d : sheet->Sprites)
                            if (ImGui::Selectable(d.Name.c_str(), d.Name == c.SpriteName)) c.SpriteName = d.Name;
                        ImGui::EndCombo();
                    }
                    UI::EndProperty();
                }
            UI::Color4("Color", c.Color);
            materialField(c.MaterialPath);
            const char* types[] = { "Simple", "Sliced", "Tiled", "Filled" };
            UI::ComboEnum("Image Type", c.Type, types, 4);
            if (c.Type == UIImageType::Sliced)
            {
                UI::BeginProperty("Border", "Left, Bottom, Right, Top (pixels)");
                ImGui::DragFloat4("##b", &c.Border.x, 0.5f, 0.0f, 4096.0f, "%.0f");
                UI::EndProperty();
                UI::DragFloat("Pixels Per Unit Mult.", c.PixelsPerUnitMultiplier, 0.01f, 0.01f, 10.0f);
            }
            if (c.Type == UIImageType::Filled)
            {
                const char* methods[] = { "Horizontal", "Vertical", "Radial 360" };
                UI::ComboEnum("Fill Method", c.FillMethod, methods, 3);
                UI::SliderFloat("Fill Amount", c.FillAmount, 0.0f, 1.0f);
                if (c.FillMethod == UIFillMethod::Radial360) UI::Checkbox("Clockwise", c.FillClockwise);
            }
            if (c.TexturePath.empty())
                UI::DragFloat("Corner Radius", c.CornerRadius, 0.5f, 0.0f, 500.0f, "%.0f px");
            else if (c.Type == UIImageType::Simple)
                UI::Checkbox("Preserve Aspect", c.PreserveAspect);
            UI::Checkbox("Raycast Target", c.RaycastTarget);
        });

        DrawComponent<UITextComponent>("Text (UI)", entity, [](auto& c)
        {
            UI::BeginProperty("Text");
            char buf[2048];
            snprintf(buf, sizeof(buf), "%s", c.Text.c_str());
            if (ImGui::InputTextMultiline("##txt", buf, sizeof(buf), ImVec2(-1, 60)))
                c.Text = buf;
            UI::EndProperty();
            UI::BeginProperty("Font", "Drag a .ttf/.otf from the Content Browser");
            std::string fn = c.FontPath.empty() ? std::string("Default (Open Sans)") : std::filesystem::path(c.FontPath).filename().string();
            ImGui::Button(fn.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 2.0f * (ImGui::GetFrameHeight() + 4.0f), 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string path = (const char*)p->Data;
                    if (path.ends_with(".ttf") || path.ends_with(".otf") || path.ends_with(".TTF") || path.ends_with(".OTF"))
                        c.FontPath = path;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0, 4);
            if (std::string picked; ObjectPicker::Button("fontpick", "Font", ObjectPicker::FontExtensions(), c.FontPath, picked))
                c.FontPath = picked;
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_XMARK "##font", ImVec2(ImGui::GetFrameHeight(), 0))) c.FontPath.clear();
            UI::EndProperty();
            UI::DragFloat("Font Size", c.FontSize, 0.25f, 4.0f, 512.0f, "%.1f px");
            UI::Color4("Color", c.Color);
            UI::BeginProperty("Alignment");
            const char* ah[] = { ICON_FA_ALIGN_LEFT, ICON_FA_ALIGN_CENTER, ICON_FA_ALIGN_RIGHT };
            for (int i = 0; i < 3; ++i)
            {
                if (i) ImGui::SameLine(0, 2);
                bool a = (int)c.AlignH == i;
                if (a) ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                if (ImGui::Button((std::string(ah[i]) + "##ah" + std::to_string(i)).c_str())) c.AlignH = (UITextAlignH)i;
                if (a) ImGui::PopStyleColor();
            }
            ImGui::SameLine(0, 10);
            const char* av[] = { "Top", "Mid", "Bot" };
            for (int i = 0; i < 3; ++i)
            {
                if (i) ImGui::SameLine(0, 2);
                bool a = (int)c.AlignV == i;
                if (a) ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                if (ImGui::Button((std::string(av[i]) + "##av").c_str())) c.AlignV = (UITextAlignV)i;
                if (a) ImGui::PopStyleColor();
            }
            UI::EndProperty();
            UI::Checkbox("Word Wrap", c.WordWrap);
            UI::Checkbox("Best Fit", c.BestFit);
            if (c.BestFit)
            {
                UI::DragFloat("Min Size", c.MinSize, 0.25f, 2.0f, 512.0f);
                UI::DragFloat("Max Size", c.MaxSize, 0.25f, 2.0f, 512.0f);
            }
            UI::DragFloat("Line Spacing", c.LineSpacing, 0.01f, 0.5f, 4.0f);
            UI::Checkbox("Outline", c.Outline);
            if (c.Outline) UI::Color4("Outline Color", c.OutlineColor);
            UI::Checkbox("Shadow", c.Shadow);
            if (c.Shadow)
            {
                UI::Vec2("Shadow Offset", c.ShadowOffset, 0.25f);
                UI::Color4("Shadow Color", c.ShadowColor);
            }
            UI::Checkbox("Raycast Target", c.RaycastTarget);
        });

        DrawComponent<UIButtonComponent>("Button (UI)", entity, [&](auto& c)
        {
            selectableUI(c.Selectable);
            UI::InputText("On Click Event", c.OnClick, "Event name scripts receive (empty = object name)");
            UI::Text("Clicks", "%u", c.ClickCount);
        });
        DrawComponent<UIToggleComponent>("Toggle (UI)", entity, [&](auto& c)
        {
            UI::Checkbox("Is On", c.IsOn);
            UI::DragInt("Group", c.Group, 0.1f, 0, 1000, "0 = none. Only one toggle per group can be on");
            UI::InputText("On Value Changed", c.OnValueChanged);
            selectableUI(c.Selectable);
        });
        DrawComponent<UISliderComponent>("Slider (UI)", entity, [&](auto& c)
        {
            UI::DragFloat("Min Value", c.MinValue, 0.1f);
            UI::DragFloat("Max Value", c.MaxValue, 0.1f);
            UI::SliderFloat("Value", c.Value, c.MinValue, std::max(c.MaxValue, c.MinValue + 0.0001f));
            UI::Checkbox("Whole Numbers", c.WholeNumbers);
            const char* dirs[] = { "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" };
            UI::ComboEnum("Direction", c.Direction, dirs, 4);
            UI::Color4("Background", c.BackgroundColor);
            UI::Color4("Fill", c.FillColor);
            UI::Color4("Handle", c.HandleColor);
            UI::DragFloat("Handle Size", c.HandleSize, 0.5f, 4.0f, 200.0f);
            UI::InputText("On Value Changed", c.OnValueChanged);
            selectableUI(c.Selectable);
        });
        DrawComponent<UIInputFieldComponent>("Input Field (UI)", entity, [&](auto& c)
        {
            UI::InputText("Text", c.Text);
            UI::InputText("Placeholder", c.Placeholder);
            UI::DragInt("Character Limit", c.CharacterLimit, 0.2f, 0, 100000);
            const char* types[] = { "Standard", "Integer Number", "Decimal Number", "Alphanumeric", "Password" };
            UI::ComboEnum("Content Type", c.ContentType, types, 5);
            UI::DragFloat("Font Size", c.FontSize, 0.25f, 4.0f, 256.0f);
            UI::Color4("Text Color", c.TextColor);
            UI::Color4("Background", c.BackgroundColor);
            UI::InputText("On Submit", c.OnSubmit);
            selectableUI(c.Selectable);
        });
        DrawComponent<UIDropdownComponent>("Dropdown (UI)", entity, [&](auto& c)
        {
            UI::BeginProperty("Options");
            int remove = -1;
            for (int i = 0; i < (int)c.Options.size(); ++i)
            {
                ImGui::PushID(i);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", c.Options[(size_t)i].c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 26.0f);
                if (ImGui::InputText("##opt", buf, sizeof(buf))) c.Options[(size_t)i] = buf;
                ImGui::SameLine(0, 2);
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
                ImGui::PopID();
            }
            if (remove >= 0) c.Options.erase(c.Options.begin() + remove);
            if (ImGui::SmallButton(ICON_FA_PLUS " Add Option")) c.Options.push_back("Option " + std::to_string(c.Options.size() + 1));
            UI::EndProperty();
            UI::SliderInt("Value", c.Value, 0, std::max(0, (int)c.Options.size() - 1));
            UI::DragFloat("Font Size", c.FontSize, 0.25f, 4.0f, 256.0f);
            UI::InputText("On Value Changed", c.OnValueChanged);
            selectableUI(c.Selectable);
        });
        DrawComponent<UIScrollRectComponent>("Scroll Rect (UI)", entity, [](auto& c)
        {
            UI::Checkbox("Horizontal", c.Horizontal);
            UI::Checkbox("Vertical", c.Vertical);
            UI::DragFloat("Scroll Sensitivity", c.ScrollSensitivity, 0.5f, 1.0f, 1000.0f);
            UI::Checkbox("Mask (clip content)", c.Mask);
            UI::Checkbox("Show Scrollbar", c.ShowScrollbar);
            UI::Text("Position", "%.2f, %.2f", c.NormalizedPosition.x, c.NormalizedPosition.y);
            UI::InfoBox("The first child is the scrolling Content.", UI::InfoType::Info);
        });
        DrawComponent<UILayoutGroupComponent>("Layout Group (UI)", entity, [](auto& c)
        {
            const char* types[] = { "Horizontal", "Vertical", "Grid", "Horizontal Flow", "Vertical Flow" };
            UI::ComboEnum("Type", c.Type, types, 5, "Flow: children wrap to the next row / column when they do not fit (Godot HFlowContainer / VFlowContainer)");
            UI::BeginProperty("Padding", "Left, Right, Top, Bottom");
            ImGui::DragFloat4("##pad", &c.Padding.x, 0.5f, 0.0f, 2000.0f, "%.0f");
            UI::EndProperty();
            const char* aligns[] = { "Upper Left", "Upper Center", "Upper Right", "Middle Left", "Middle Center", "Middle Right", "Lower Left", "Lower Center", "Lower Right" };
            UI::ComboEnum("Child Alignment", c.ChildAlignment, aligns, 9);
            if (c.Type == UILayoutType::Grid)
            {
                UI::Vec2("Cell Size", c.CellSize, 1.0f);
                UI::Vec2("Spacing", c.GridSpacing, 0.5f);
                UI::DragInt("Fixed Columns", c.Constraint, 0.1f, 0, 100, "0 = flexible");
            }
            else
            {
                UI::DragFloat("Spacing", c.Spacing, 0.5f, 0.0f, 2000.0f);
                UI::Checkbox("Control Child Width", c.ControlChildWidth);
                UI::Checkbox("Control Child Height", c.ControlChildHeight);
                UI::Checkbox("Force Expand Width", c.ChildForceExpandWidth);
                UI::Checkbox("Force Expand Height", c.ChildForceExpandHeight);
            }
            UI::Checkbox("Fit Content Size", c.FitContent);
        });

        DrawComponent<GraphicRaycasterComponent>("Graphic Raycaster", entity, [this](auto& component)
        {
            ImGui::Checkbox("Ignore Reversed Graphics", &component.IgnoreReversedGraphics);
            ImGui::Spacing();
            
            // Blocking Objects
            const char* blockingModes[] = { "None", "2D", "3D", "All" };
            int currentBlocking = (int)component.Blocking;
            if (ImGui::Combo("Blocking Objects", &currentBlocking, blockingModes, 4))
            {
                component.Blocking = (GraphicRaycasterComponent::BlockingObjects)currentBlocking;
            }
            
            // Blocking Mask (layer seçimi)
            if (component.Blocking != GraphicRaycasterComponent::BlockingObjects::None && m_Context)
            {
                ImGui::Spacing();
                ImGui::Text("Blocking Mask:");
                ImGui::Indent();
                
                auto& layers = m_Context->GetLayers();
                for (const auto& layer : layers)
                {
                    bool isEnabled = (component.BlockingMask & (1 << layer.ID)) != 0;
                    if (ImGui::Checkbox(layer.Name.c_str(), &isEnabled))
                    {
                        if (isEnabled)
                            component.BlockingMask |= (1 << layer.ID);
                        else
                            component.BlockingMask &= ~(1 << layer.ID);
                    }
                }
                
                ImGui::Unindent();
            }
        });

        DrawComponent<DirectionalLightComponent>("Directional Light", entity, [entity](auto& component)
        {
            DrawLightAim(entity, component.Direction, true);
            UI::Color3("Color", component.Color);
            UI::DragFloat("Intensity", component.Intensity, 0.05f, 0.0f, 20.0f, "%.2f");
            UI::Checkbox("Cast Shadows", component.CastShadows);
            const char* lightModes[] = { "Realtime", "Mixed", "Baked" };
            UI::ComboEnum("Mode", component.Mode, lightModes, 3);
        });

        DrawComponent<PointLightComponent>("Point Light", entity, [](auto& component)
        {
            ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
            ImGui::DragFloat("Intensity", &component.Intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Range", &component.Range, 0.1f, 0.1f, 100.0f);
            
            ImGui::Separator();
            ImGui::Text("Attenuation");
            ImGui::DragFloat("Constant", &component.Constant, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Linear", &component.Linear, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Quadratic", &component.Quadratic, 0.0001f, 0.0f, 1.0f);
            
            ImGui::Checkbox("Cast Shadows", &component.CastShadows);

            const char* lightModes[] = { "Realtime", "Mixed", "Baked" };
            int modeIdx = (int)component.Mode;
            if (ImGui::Combo("Mode", &modeIdx, lightModes, 3))
                component.Mode = (LightMode)modeIdx;
        });

        DrawComponent<SpotLightComponent>("Spot Light", entity, [entity](auto& component)
        {
            DrawLightAim(entity, component.Direction, false);
            ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
            ImGui::DragFloat("Intensity", &component.Intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Range", &component.Range, 0.1f, 0.1f, 100.0f);
            
            ImGui::Separator();
            ImGui::Text("Spot Cone");
            ImGui::SliderFloat("Inner Angle", &component.InnerConeAngle, 0.0f, 90.0f);
            ImGui::SliderFloat("Outer Angle", &component.OuterConeAngle, 0.0f, 90.0f);
            
            // Inner her zaman outer'dan küçük olmalı
            if (component.InnerConeAngle > component.OuterConeAngle)
                component.InnerConeAngle = component.OuterConeAngle;
            
            ImGui::Separator();
            ImGui::Text("Attenuation");
            ImGui::DragFloat("Constant", &component.Constant, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Linear", &component.Linear, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Quadratic", &component.Quadratic, 0.0001f, 0.0f, 1.0f);
            
            ImGui::Checkbox("Cast Shadows", &component.CastShadows);

            const char* lightModes[] = { "Realtime", "Mixed", "Baked" };
            int modeIdx = (int)component.Mode;
            if (ImGui::Combo("Mode", &modeIdx, lightModes, 3))
                component.Mode = (LightMode)modeIdx;
        });

        // 2D Physics Components
        DrawComponent<RigidBody2DComponent>("Rigidbody 2D", entity, [entity](auto& component)
        {
            static const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
            UI::ComboEnum("Body Type", component.Type, bodyTypes, 3);
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat2d", "Physics Material 2D",
                                            "Friction and bounciness for this body's colliders that have no material of their own (.cqphysmat2d).");
            if (component.Type == RigidBody2DComponent::BodyType::Dynamic)
            {
                UI::Checkbox("Use Auto Mass", component.UseAutoMass, "Mass from collider areas x density.");
                if (!component.UseAutoMass)
                    UI::DragFloat("Mass", component.Mass, 0.01f, 0.0001f, 100000.0f, "%.3f kg");
                UI::DragFloat("Linear Drag", component.LinearDrag, 0.01f, 0.0f, 1000.0f);
                UI::DragFloat("Angular Drag", component.AngularDrag, 0.01f, 0.0f, 1000.0f);
                UI::DragFloat("Gravity Scale", component.GravityScale, 0.01f, -100.0f, 100.0f);
                static const char* detection[] = { "Discrete", "Continuous" };
                int mode = component.ContinuousCollision ? 1 : 0;
                if (UI::Combo("Collision Detection", mode, detection, 2, "Continuous: fast bodies (bullets) won't tunnel."))
                    component.ContinuousCollision = mode == 1;
                UI::Checkbox("Interpolate", component.Interpolate);
                UI::Checkbox("Allow Sleep", component.AllowSleep);
                UI::SubHeader("Constraints");
                UI::Checkbox("Freeze Rotation Z", component.FixedRotation);
            }
            if (Scene* scene = entity.GetScene(); scene && scene->GetPhysicsWorld2D() && scene->GetPhysicsWorld2D()->IsRunning() && component.RuntimeBody)
            {
                auto* world = scene->GetPhysicsWorld2D();
                UI::SubHeader("Info");
                glm::vec2 v = world->GetLinearVelocity(entity);
                UI::Text("Velocity", "%.2f, %.2f", v.x, v.y);
                UI::Text("Angular Velocity", "%.2f rad/s", world->GetAngularVelocity(entity));
                UI::Text("Mass", "%.3f kg", world->GetMass(entity));
            }
        });

        DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
        {
            UI::Checkbox("Is Trigger", component.IsTrigger, "Detects overlaps (OnTriggerEnter2D) without blocking.");
            UI::Vec2("Offset", component.Offset, 0.01f);
            UI::Vec2("Size", component.Size, 0.01f);
            component.Size = glm::max(component.Size, glm::vec2(0.0f));
            UI::DragFloat("Edge Radius", component.EdgeRadius, 0.005f, 0.0f, 10.0f);
            UI::SubHeader("Material");
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat2d", "Physics Material 2D",
                                            "Friction and bounciness (.cqphysmat2d). Empty = the Rigidbody 2D material, or the values below.");
            UI::DragFloat("Density", component.Density, 0.01f, 0.0f, 1000.0f);
            UI::SliderFloat("Friction", component.Friction, 0.0f, 1.0f);
            UI::SliderFloat("Bounciness", component.Restitution, 0.0f, 1.0f);
        });

        DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
        {
            UI::Checkbox("Is Trigger", component.IsTrigger, "Detects overlaps (OnTriggerEnter2D) without blocking.");
            UI::Vec2("Offset", component.Offset, 0.01f);
            UI::DragFloat("Radius", component.Radius, 0.01f, 0.0f, 10000.0f);
            UI::SubHeader("Material");
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat2d", "Physics Material 2D",
                                            "Friction and bounciness (.cqphysmat2d). Empty = the Rigidbody 2D material, or the values below.");
            UI::DragFloat("Density", component.Density, 0.01f, 0.0f, 1000.0f);
            UI::SliderFloat("Friction", component.Friction, 0.0f, 1.0f);
            UI::SliderFloat("Bounciness", component.Restitution, 0.0f, 1.0f);
        });

        // 3D Physics Components
        DrawComponent<RigidbodyComponent>("Rigidbody", entity, [entity](auto& component)
        {
            static const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
            UI::ComboEnum("Body Type", component.Type, bodyTypes, 3, "Dynamic: simulated. Kinematic: moved by script/animation, pushes others. Static: never moves.");
            const bool dynamic = component.Type == RigidbodyComponent::BodyType::Dynamic;
            if (dynamic)
            {
                UI::DragFloat("Mass", component.Mass, 0.01f, 0.001f, 100000.0f, "%.3f kg");
                UI::DragFloat("Drag", component.LinearDrag, 0.01f, 0.0f, 100.0f, "%.3f", "Linear velocity damping.");
                UI::DragFloat("Angular Drag", component.AngularDrag, 0.01f, 0.0f, 100.0f);
                UI::Checkbox("Use Gravity", component.UseGravity);
                if (component.UseGravity)
                    UI::DragFloat("Gravity Scale", component.GravityScale, 0.01f, -10.0f, 10.0f);
                UI::Checkbox("Interpolate", component.Interpolate, "Smooths rendering between fixed physics steps.");
                static const char* ccd[] = { "Discrete", "Continuous" };
                UI::ComboEnum("Collision Detection", component.CollisionDetection, ccd, 2, "Continuous prevents fast bodies from tunneling through thin colliders.");
                UI::DragFloat("Max Angular Velocity", component.MaxAngularVelocity, 0.5f, 0.1f, 1000.0f, "%.1f rad/s");
                UI::Checkbox("Allow Sleep", component.AllowSleep);

                UI::SubHeader("Constraints");
                auto axisRow = [](const char* label, bool& x, bool& y, bool& z) {
                    UI::BeginProperty(label);
                    ImGui::Checkbox("X##ax", &x); ImGui::SameLine();
                    ImGui::Checkbox("Y##ay", &y); ImGui::SameLine();
                    ImGui::Checkbox("Z##az", &z);
                    UI::EndProperty();
                };
                ImGui::PushID("pos");
                axisRow("Freeze Position", component.FreezePositionX, component.FreezePositionY, component.FreezePositionZ);
                ImGui::PopID();
                ImGui::PushID("rot");
                bool rx = component.FreezeRotation || component.FreezeRotationX, ry = component.FreezeRotation || component.FreezeRotationY,
                     rz = component.FreezeRotation || component.FreezeRotationZ;
                axisRow("Freeze Rotation", rx, ry, rz);
                component.FreezeRotationX = rx; component.FreezeRotationY = ry; component.FreezeRotationZ = rz;
                component.FreezeRotation = rx && ry && rz;
                ImGui::PopID();
            }
            UI::SubHeader("Material");
            UI::SliderFloat("Friction", component.Friction, 0.0f, 1.0f);
            UI::SliderFloat("Bounciness", component.Restitution, 0.0f, 1.0f);

            // Oyun modunda canlı bilgi
            if (Scene* scene = entity.GetScene(); scene && scene->GetPhysicsWorld3D() && scene->GetPhysicsWorld3D()->IsRunning() && component.RuntimeBody)
            {
                auto* world = scene->GetPhysicsWorld3D();
                UI::SubHeader("Info");
                glm::vec3 v = world->GetLinearVelocity(entity), w = world->GetAngularVelocity(entity);
                UI::Text("Speed", "%.3f m/s", glm::length(v));
                UI::Text("Velocity", "%.2f, %.2f, %.2f", v.x, v.y, v.z);
                UI::Text("Angular Velocity", "%.2f, %.2f, %.2f", w.x, w.y, w.z);
                UI::Text("Sleeping", "%s", world->IsSleeping(entity) ? "Yes" : "No");
            }
        });

        DrawComponent<BoxColliderComponent>("Box Collider", entity, [](auto& component)
        {
            UI::Checkbox("Is Trigger", component.IsTrigger, "Detects overlaps (OnTriggerEnter) without blocking.");
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat", "Physics Material",
                                            "Friction and bounciness (.cqphysmat). Empty = the Rigidbody values.");
            UI::Vec3("Center", component.Offset, 0.01f);
            UI::Vec3("Size", component.Size, 0.01f, 1.0f);
            component.Size = glm::max(component.Size, glm::vec3(0.0f));
        });

        DrawComponent<SphereColliderComponent>("Sphere Collider", entity, [](auto& component)
        {
            UI::Checkbox("Is Trigger", component.IsTrigger, "Detects overlaps (OnTriggerEnter) without blocking.");
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat", "Physics Material",
                                            "Friction and bounciness (.cqphysmat). Empty = the Rigidbody values.");
            UI::Vec3("Center", component.Offset, 0.01f);
            UI::DragFloat("Radius", component.Radius, 0.01f, 0.0f, 10000.0f);
        });

        DrawComponent<CapsuleColliderComponent>("Capsule Collider", entity, [](auto& component)
        {
            UI::Checkbox("Is Trigger", component.IsTrigger, "Detects overlaps (OnTriggerEnter) without blocking.");
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat", "Physics Material",
                                            "Friction and bounciness (.cqphysmat). Empty = the Rigidbody values.");
            UI::Vec3("Center", component.Offset, 0.01f);
            UI::DragFloat("Radius", component.Radius, 0.01f, 0.0f, 10000.0f);
            UI::DragFloat("Height", component.Height, 0.01f, 0.0f, 10000.0f);
            static const char* axes[] = { "X-Axis", "Y-Axis", "Z-Axis" };
            component.Direction = std::clamp(component.Direction, 0, 2);
            UI::Combo("Direction", component.Direction, axes, 3);
        });

        DrawComponent<MeshColliderComponent>("Mesh Collider", entity, [](auto& component)
        {
            UI::Checkbox("Convex", component.IsConvex, "Required for dynamic rigidbodies. Uses the mesh's convex hull.");
            if (component.IsConvex)
                UI::Checkbox("Is Trigger", component.IsTrigger);
            else
                component.IsTrigger = false;
            MaterialEditorUI::AssetPathSlot("Material", component.PhysicsMaterial, ".cqphysmat", "Physics Material",
                                            "Friction and bounciness (.cqphysmat). Empty = the Rigidbody values.");
            UI::InfoBox("Uses the entity's Mesh Renderer or Model mesh.", UI::InfoType::Info);
        });

        // Audio Components
        DrawComponent<AudioSourceComponent>("Audio Source", entity, [](auto& component)
        {
            // Mikser çıkışı
            {
                auto& mixer = AudioMixer::Get();
                ImGui::Text("Output:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                bool exists = mixer.FindGroup(component.OutputGroup) != nullptr;
                std::string preview = component.OutputGroup + (exists || !mixer.IsInitialized() ? "" : "  (missing -> Master)");
                if (ImGui::BeginCombo("##AudioOutput", preview.c_str()))
                {
                    for (auto& g : mixer.GetGroups())
                    {
                        if (ImGui::Selectable(g.Name.c_str(), g.Name == component.OutputGroup))
                        {
                            component.OutputGroup = g.Name;
                            component.RoutingGeneration = 0; // yeniden bağlanır
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            // Ses klibi (Unity AudioClip alanı): ad düğmesi + sürükle-bırak + ◎ seçici + temizle
            {
                UI::BeginProperty("Audio Clip", "Drag a .wav/.mp3/.ogg/.flac from the Content Browser or pick one with the target button");
                const float h = ImGui::GetFrameHeight();
                std::string shown = component.FilePath.empty() ? std::string("None (Audio Clip)") : std::filesystem::path(component.FilePath).filename().string();
                ImGui::Button((shown + "##audioclip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 2.0f * (h + 4.0f), 0));
                if (ImGui::IsItemHovered() && !component.FilePath.empty())
                    ImGui::SetTooltip("%s", component.FilePath.c_str());
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                        std::string ext = std::filesystem::path(droppedPath).extension().string();
                        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                            component.FilePath = ToSerializablePath(droppedPath);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0, 4);
                if (std::string picked; ObjectPicker::Button("audiopick", "Audio Clip", ObjectPicker::AudioExtensions(), component.FilePath, picked))
                    component.FilePath = picked.empty() ? std::string() : ToSerializablePath(picked);
                ImGui::SameLine(0, 4);
                if (ImGui::Button(ICON_FA_XMARK "##clearaudio", ImVec2(h, 0)))
                    component.FilePath.clear();
                UI::EndProperty();
            }

            // Temel kontroller
            ImGui::Checkbox("Play On Awake", &component.PlayOnAwake);
            ImGui::Checkbox("Loop", &component.Loop);
            ImGui::Checkbox("Mute", &component.Mute);
            
            ImGui::Separator();
            
            // Volume, Pitch, Pan
            ImGui::SliderFloat("Volume", &component.Volume, 0.0f, 1.0f);
            ImGui::SliderFloat("Pitch", &component.Pitch, 0.5f, 2.0f);
            ImGui::SliderFloat("Pan", &component.Pan, -1.0f, 1.0f);
            
            ImGui::Separator();
            
            // 3D Ses ayarları
            ImGui::Checkbox("3D Sound", &component.Is3D);
            
            if (component.Is3D)
            {
                ImGui::DragFloat("Min Distance", &component.MinDistance, 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Max Distance", &component.MaxDistance, 1.0f, 0.0f, 10000.0f);
                ImGui::SliderFloat("Doppler Level", &component.DopplerLevel, 0.0f, 5.0f);
                
                const char* attenuationModels[] = { "None", "Inverse", "Linear", "Exponential" };
                int currentModel = (int)component.Attenuation;
                if (ImGui::Combo("Attenuation", &currentModel, attenuationModels, 4))
                {
                    component.Attenuation = (AudioSourceComponent::AttenuationModel)currentModel;
                }
                
                ImGui::DragFloat("Rolloff Factor", &component.RolloffFactor, 0.01f, 0.0f, 10.0f);
            }
            
            ImGui::Separator();
            
            // Runtime bilgisi
            if (component.IsPlaying)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Stopped");
            }
        });

        DrawComponent<AudioListenerComponent>("Audio Listener", entity, [](auto& component)
        {
            ImGui::Checkbox("Active", &component.IsActive);
            ImGui::TextWrapped("Audio Listener receives 3D audio. Only one should be active per scene (usually attached to the camera).");
        });

        DrawComponent<AudioEchoFilterComponent>("Audio Echo Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Delay (ms)", &component.Delay, 1.0f, 0.0f, 5000.0f);
            ImGui::SliderFloat("Decay", &component.Decay, 0.0f, 1.0f);
            ImGui::SliderFloat("Wet Mix", &component.WetMix, 0.0f, 1.0f);
            ImGui::SliderFloat("Dry Mix", &component.DryMix, 0.0f, 1.0f);
        });

        DrawComponent<AudioReverbFilterComponent>("Audio Reverb Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Decay Time", &component.DecayTime, 0.1f, 0.1f, 20.0f);
            ImGui::SliderFloat("Room", &component.Room, -10000.0f, 0.0f);
            ImGui::SliderFloat("Reverb Level", &component.ReverbLevel, -10000.0f, 2000.0f);
        });

        DrawComponent<AudioLowPassFilterComponent>("Audio Low Pass Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Cutoff Frequency (Hz)", &component.CutoffFrequency, 10.0f, 10.0f, 22000.0f);
            ImGui::SliderFloat("Resonance", &component.Resonance, 0.1f, 10.0f);
        });

        DrawComponent<AudioHighPassFilterComponent>("Audio High Pass Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Cutoff Frequency (Hz)", &component.CutoffFrequency, 10.0f, 10.0f, 22000.0f);
            ImGui::SliderFloat("Resonance", &component.Resonance, 0.1f, 10.0f);
        });

        DrawComponent<AudioChorusFilterComponent>("Audio Chorus Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Delay (ms)", &component.Delay, 0.1f, 0.0f, 100.0f);
            ImGui::SliderFloat("Depth", &component.Depth, 0.0f, 1.0f);
            ImGui::DragFloat("Rate (Hz)", &component.Rate, 0.1f, 0.0f, 20.0f);
            ImGui::SliderFloat("Wet Mix", &component.WetMix, 0.0f, 1.0f);
            ImGui::SliderFloat("Dry Mix", &component.DryMix, 0.0f, 1.0f);
        });

        DrawComponent<AudioDistortionFilterComponent>("Audio Distortion Filter", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::SliderFloat("Level", &component.Level, 0.0f, 1.0f);
        });

        DrawComponent<AudioGainComponent>("Audio Gain Node", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::SliderFloat("Gain", &component.Gain, 0.0f, 10.0f);
        });

        DrawComponent<AudioPanComponent>("Audio Pan Node", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::SliderFloat("Pan", &component.Pan, -1.0f, 1.0f);
        });

        DrawComponent<AudioReverbZoneComponent>("Audio Reverb Zone", entity, [](auto& component)
        {
            ImGui::Checkbox("Enabled", &component.Enabled);
            ImGui::DragFloat("Min Distance", &component.MinDistance, 0.1f, 0.0f, 1000.0f);
            ImGui::DragFloat("Max Distance", &component.MaxDistance, 0.1f, 0.0f, 1000.0f);

            const char* presets[] = { 
                "Generic", "PaddedCell", "Room", "Bathroom", "LivingRoom", 
                "StoneRoom", "Auditorium", "ConcertHall", "Cave", "Arena", 
                "Hangar", "CarpetedHallway", "Hallway", "StoneCorridor", 
                "Alley", "Forest", "City", "Mountains", "Quarry", "Plain", 
                "ParkingLot", "SewerPipe", "Underwater", "Custom" 
            };
            
            int currentPreset = (int)component.Preset;
            if (ImGui::Combo("Preset", &currentPreset, presets, IM_ARRAYSIZE(presets)))
            {
                component.Preset = (AudioReverbZoneComponent::ReverbPreset)currentPreset;
            }
        });

        // Yansıma probu (Unity Reflection Probe): Box Size tam boy, Box Offset kutu merkezinin probun yerine göre kayması. Bileşen kutuyu
        // en küçük köşe (BoxOffset) + yarı boy (BoxSize) olarak saklar
        DrawComponent<ReflectionProbeComponent>("Reflection Probe", entity, [](auto& probe)
        {
            glm::vec3 size = probe.BoxSize * 2.0f;
            glm::vec3 offset = probe.BoxOffset + probe.BoxSize;
            bool box = ImGui::DragFloat3("Box Size", &size.x, 0.05f, 0.01f, 100000.0f, "%.2f");
            box |= ImGui::DragFloat3("Box Offset", &offset.x, 0.05f, -100000.0f, 100000.0f, "%.2f");
            if (box)
            {
                size = glm::max(size, glm::vec3(0.01f));
                probe.BoxSize = size * 0.5f;
                probe.BoxOffset = offset - probe.BoxSize;
            }
            ImGui::DragFloat("Blend Distance", &probe.BlendDistance, 0.05f, 0.0f, 100.0f, "%.2f");
            ImGui::DragFloat("Intensity", &probe.Intensity, 0.01f, 0.0f, 10.0f, "%.2f");
            static const uint32_t kResolutions[] = { 16, 32, 64, 128, 256, 512, 1024 };
            const char* resNames[] = { "16", "32", "64", "128", "256", "512", "1024" };
            int ri = 3;
            for (int i = 0; i < 7; ++i)
                if (probe.Resolution == kResolutions[i])
                    ri = i;
            if (ImGui::Combo("Resolution", &ri, resNames, 7))
            {
                probe.Resolution = kResolutions[ri];
                probe.NeedsCapture = true;
            }
            int type = probe.RealtimeUpdate ? 1 : 0;
            const char* types[] = { "Baked", "Realtime" };
            if (ImGui::Combo("Type", &type, types, 2))
                probe.RealtimeUpdate = type == 1;
            if (probe.RealtimeUpdate)
                ImGui::DragFloat("Update Interval", &probe.UpdateInterval, 0.01f, 0.0f, 10.0f, "%.2f s");
            ImGui::TextDisabled("%s", probe.Captured ? "Captured" : "Not captured yet");
            if (ImGui::Button("Bake"))
                probe.NeedsCapture = true;
        });

        // Uyarlanabilir prob hacmi (Unity Probe Volume): kutu nesnenin konumu ve ölçeği; Lighting > Probes > Bake Probe Volume bu kutuyu pişirir
        DrawComponent<AdaptiveProbeVolumeComponent>("Adaptive Probe Volume", entity, [](auto& v)
        {
            ImGui::TextWrapped("The volume box is this object's position and scale. Bake it in Window > Lighting > Probes > Bake Probe Volume.");
            ImGui::DragFloat3("Probe Offset", &v.ProbeOffset.x, 0.05f, -100.0f, 100.0f, "%.2f");
            ImGui::DragFloat("Min Probe Spacing", &v.MinSpacing, 0.05f, 0.1f, 50.0f, "%.2f m");
            ImGui::DragFloat("Max Probe Spacing", &v.MaxSpacing, 0.1f, v.MinSpacing, 250.0f, "%.2f m");
            const char* modes[] = { "Single Scene", "Baking Set" };
            ImGui::Combo("Baking Mode", &v.BakingMode, modes, 2);
            ImGui::Checkbox("Dilation", &v.Dilation);
            ImGui::Checkbox("Virtual Offset", &v.VirtualOffset);
            if (v.VirtualOffset)
            {
                ImGui::SliderFloat("Validity Threshold", &v.ValidityThreshold, 0.0f, 1.0f);
                ImGui::SliderFloat("Search Distance", &v.SearchDistanceMultiplier, 0.0f, 1.0f);
                ImGui::DragFloat("Geometry Bias", &v.GeometryBias, 0.001f, 0.0f, 0.5f, "%.3f");
                ImGui::DragFloat("Ray Origin Bias", &v.RayOriginBias, 0.0005f, -0.1f, 0.0f, "%.4f");
            }
        });

        if (entity.HasComponent<NativeScriptComponent>())
        {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            int indexToRemove = -1;
            
            for (int i = 0; i < nsc.Scripts.size(); i++)
            {
                auto& script = nsc.Scripts[i];
                std::string nativeName = "Native Script";
                if (!script.ModuleName.empty())
                    nativeName = script.ModuleName + " (Native Script)";
                    
                const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
                float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
                ImGui::Separator();
                
                ImGui::PushID((void*)&script);
                bool open = ImGui::TreeNodeEx((void*)&script, treeNodeFlags, nativeName.c_str());
                ImGui::PopStyleVar();
                
                ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
                
                std::string popupName = "ComponentSettingsNative" + std::to_string(i);
                if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
                {
                    ImGui::OpenPopup(popupName.c_str());
                }

                if (ImGui::BeginPopup(popupName.c_str()))
                {
                    if (ImGui::MenuItem("Remove component"))
                        indexToRemove = i;

                    ImGui::EndPopup();
                }

                if (open)
                {
                    // C++ kaynağı (ScriptableEntity + CQ_NATIVE_SCRIPT): motor Play'de kendisi derler; DLL gerekmez
                    if (NativeScriptCompiler::IsSource(script.ScriptPath))
                    {
                        UI::Text("Source", "%s", std::filesystem::path(script.ScriptPath).filename().string().c_str());
                        UI::Text("Class", "%s", script.ClassName.empty() ? "(first CQ_NATIVE_SCRIPT in the file)" : script.ClassName.c_str());
                        if (ImGui::Button(ICON_FA_HAMMER " Compile Now"))
                        {
                            std::filesystem::path src = script.ScriptPath;
                            if (src.is_relative() && Project::GetActive())
                                src = Project::GetActiveProjectDirectory() / src;
                            const auto r = NativeScriptCompiler::Compile(src, true);
                            m_NativeCompileLog = r.Success ? std::string("Compiled: ") + r.Library.filename().string() : r.Log;
                            m_NativeCompileOk = r.Success;
                        }
                        if (!m_NativeCompileLog.empty())
                            UI::InfoBox(m_NativeCompileLog.c_str(), m_NativeCompileOk ? UI::InfoType::Success : UI::InfoType::Error);
                        else
                            ImGui::TextDisabled("Compiled automatically when you press Play (only if the file changed).");
                    }
                    if (ImGui::Button(ICON_FA_FILE_CODE " C++ Source"))
                    {
                        nfdchar_t* outPath = nullptr;
                        nfdfilteritem_t filters[1] = { { "C++ Native Script", "cpp,cc,cxx" } };
                        if (NFD_OpenDialog(&outPath, filters, 1, nullptr) == NFD_OKAY)
                        {
                            const std::filesystem::path src = outPath;
                            NFD_FreePath(outPath);
                            script.ModuleName = src.stem().string();
                            script.ClassName.clear();
                            script.ScriptPath = ToSerializablePath(src.string());
                            script.InstantiateScript = nullptr;
                            m_NativeCompileLog.clear();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Load DLL"))
                    {
                        CQ_CORE_INFO("InspectorPanel: Load DLL button clicked");
                        
                        nfdchar_t* outPath = nullptr;
                        nfdfilteritem_t filters[1] = { { "Script Library", "dll,so" } };
                        
                        CQ_CORE_INFO("InspectorPanel: Opening NFD dialog...");
                        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
                        
                        if (result == NFD_OKAY)
                        {
                            std::string dllPath = outPath;
                            NFD_FreePath(outPath);
                            
                            std::filesystem::path path(dllPath);
                            script.ModuleName = path.stem().string();
                            script.ScriptPath = ToSerializablePath(dllPath);
                            
                            if (ScriptEngine::LoadModule(script.ModuleName, dllPath))
                            {
                                auto classNames = ScriptEngine::GetModuleClassNames(script.ModuleName);
                                script.ClassName = classNames.empty() ? script.ModuleName : classNames.front();

                                script.InstantiateScript = [moduleName = script.ModuleName, className = script.ClassName]() {
                                    return ScriptEngine::CreateScriptInstance(moduleName, className);
                                };
                            }
                        }
                    }
                    
                    // Drag & Drop target
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        {
                            std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                            std::filesystem::path path(droppedPath);
                            std::string ext = path.extension().string();
                            
                            if (NativeScriptCompiler::IsSource(droppedPath))
                            {
                                script.ModuleName = path.stem().string();
                                script.ClassName.clear();
                                script.ScriptPath = ToSerializablePath(droppedPath);
                                script.InstantiateScript = nullptr;
                                m_NativeCompileLog.clear();
                            }
                            else if (ext == ".dll" || ext == ".so")
                            {
                                std::filesystem::path fpath(droppedPath);
                                script.ModuleName = fpath.stem().string();
                                script.ScriptPath = ToSerializablePath(droppedPath);
                                
                                if (ScriptEngine::LoadModule(script.ModuleName, droppedPath))
                                {
                                    auto classNames = ScriptEngine::GetModuleClassNames(script.ModuleName);
                                    script.ClassName = classNames.empty() ? script.ModuleName : classNames.front();

                                    script.InstantiateScript = [moduleName = script.ModuleName, className = script.ClassName]() {
                                        return ScriptEngine::CreateScriptInstance(moduleName, className);
                                    };
                                }
                                CQ_CORE_INFO("Native script loaded: {0}", droppedPath);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    // Unity Object Picker: C / C++ / Rust / Zig kaynakları ve derlenmiş kitaplıklar
                    if (std::string picked; ObjectPicker::Button("nativepick", "Native Script",
                                                                 { ".cpp", ".cc", ".cxx", ".c", ".rs", ".zig", ".dll", ".so" }, script.ScriptPath, picked))
                    {
                        if (picked.empty())
                        {
                            script.ScriptPath.clear();
                            script.ModuleName.clear();
                            script.ClassName.clear();
                            script.InstantiateScript = nullptr;
                        }
                        else if (NativeScriptCompiler::IsSource(picked))
                        {
                            script.ModuleName = std::filesystem::path(picked).stem().string();
                            script.ClassName.clear();
                            script.ScriptPath = ToSerializablePath(picked);
                            script.InstantiateScript = nullptr;
                            m_NativeCompileLog.clear();
                        }
                        else
                        {
                            script.ModuleName = std::filesystem::path(picked).stem().string();
                            script.ScriptPath = ToSerializablePath(picked);
                            if (ScriptEngine::LoadModule(script.ModuleName, picked))
                            {
                                auto classNames = ScriptEngine::GetModuleClassNames(script.ModuleName);
                                script.ClassName = classNames.empty() ? script.ModuleName : classNames.front();
                                script.InstantiateScript = [moduleName = script.ModuleName, className = script.ClassName]() {
                                    return ScriptEngine::CreateScriptInstance(moduleName, className);
                                };
                            }
                        }
                    }
                    
                    ImGui::Separator();
                    
                    if (script.Instance)
                        ImGui::TextColored(UI::Colors::Success, ICON_FA_CIRCLE_CHECK " Running");
                    else if (NativeScriptCompiler::IsSource(script.ScriptPath))
                        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO " Runs in Play mode");
                    else if (script.ScriptPath.empty())
                        ImGui::TextWrapped("Pick a C++ source (a ScriptableEntity class with CQ_NATIVE_SCRIPT) or a compiled script library.");
                    else if (!ScriptEngine::IsModuleLoaded(script.ModuleName))
                        ImGui::TextColored(UI::Colors::Error, ICON_FA_TRIANGLE_EXCLAMATION " Library not loaded");
                    else
                        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO " Runs in Play mode");
                    
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            
            if (indexToRemove != -1)
            {
                nsc.Scripts.erase(nsc.Scripts.begin() + indexToRemove);
                if (nsc.Scripts.empty())
                    entity.RemoveComponent<NativeScriptComponent>();
            }
        }

        if (entity.HasComponent<ConquerorScriptComponent>())
            DrawConquerorScripts(entity);

        // ---- Bileşen ekle (Unity gibi en altta)
        // Kayıtlı bileşenler (Character Controller, eklemler, Constant Force ...)
        for (auto& info : ComponentRegistry::All())
            if (!info.Builtin && info.Has(entity))
                DrawRegistryComponent(entity, info);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        {
            const float w = std::min(260.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            if (ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(w, ImGui::GetFrameHeight() + 6.0f)))
                ImGui::OpenPopup("AddComponent");
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(FLT_MAX, 520.0f));
        if (ImGui::BeginPopup("AddComponent"))
        {
            static char searchBuffer[128] = "";
            if (ImGui::IsWindowAppearing())
            {
                searchBuffer[0] = '\0';
                ImGui::SetKeyboardFocusHere();
                RefreshAddMenuScripts();
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##SearchComponent", "Search...", searchBuffer, sizeof(searchBuffer));
            std::string searchString = searchBuffer;
            
            auto toLower = [](char c) { return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c; };
            for (char& c : searchString) c = toLower(c);

            ImGui::Separator();

            auto drawEntry = [&](const std::string& name, auto func)
            {
                std::string lowerName = name;
                for (char& c : lowerName) c = toLower(c);
                
                if (searchString.empty() || lowerName.find(searchString) != std::string::npos)
                {
                    func();
                }
            };

            auto beginCategory = [&](const char* name)
            {
                if (!searchString.empty()) return true;
                return ImGui::BeginMenu(name);
            };

            auto endCategory = [&](bool open)
            {
                if (open && searchString.empty())
                    ImGui::EndMenu();
            };

            // Kayıtlı bileşenleri kendi kategorisinde listele
            auto drawRegistryCategory = [&](const std::string& category) {
                for (auto& info : ComponentRegistry::All())
                {
                    if (info.Builtin || !info.AllowInAddMenu || info.Category != category)
                        continue;
                    drawEntry(info.Name, [&]() {
                        if (info.Has(m_SelectionContext))
                            return;
                        if (ImGui::MenuItem((info.Icon + "  " + info.Name).c_str()))
                        {
                            ComponentRegistry::AddWithRequirements(info, m_SelectionContext);
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !info.Description.empty())
                            ImGui::SetTooltip("%s", info.Description.c_str());
                    });
                }
            };

            bool animOpen = beginCategory("Animation");
            if (animOpen || !searchString.empty())
            {
                drawEntry("Animator", [&]() { DrawAddComponentEntry<AnimatorComponent>("Animator"); });
                drawEntry("Animation", [&]() { DrawAddComponentEntry<AnimationComponent>("Animation"); });
                drawRegistryCategory("Animation");
                endCategory(animOpen);
            }

            bool audioOpen = beginCategory("Audio");
            if (audioOpen || !searchString.empty())
            {
                drawEntry("Audio Source", [&]() { DrawAddComponentEntry<AudioSourceComponent>("Audio Source"); });
                drawEntry("Audio Listener", [&]() { DrawAddComponentEntry<AudioListenerComponent>("Audio Listener"); });
                drawEntry("Audio Chorus Filter", [&]() { DrawAddComponentEntry<AudioChorusFilterComponent>("Audio Chorus Filter"); });
                drawEntry("Audio Distortion Filter", [&]() { DrawAddComponentEntry<AudioDistortionFilterComponent>("Audio Distortion Filter"); });
                drawEntry("Audio Echo Filter", [&]() { DrawAddComponentEntry<AudioEchoFilterComponent>("Audio Echo Filter"); });
                drawEntry("Audio High Pass Filter", [&]() { DrawAddComponentEntry<AudioHighPassFilterComponent>("Audio High Pass Filter"); });
                drawEntry("Audio Low Pass Filter", [&]() { DrawAddComponentEntry<AudioLowPassFilterComponent>("Audio Low Pass Filter"); });
                drawEntry("Audio Reverb Filter", [&]() { DrawAddComponentEntry<AudioReverbFilterComponent>("Audio Reverb Filter"); });
                drawEntry("Audio Reverb Zone", [&]() { DrawAddComponentEntry<AudioReverbZoneComponent>("Audio Reverb Zone"); });
                drawRegistryCategory("Audio");
                endCategory(audioOpen);
            }

            bool meshOpen = beginCategory("Mesh");
            if (meshOpen || !searchString.empty())
            {
                drawEntry("Mesh Renderer", [&]() { DrawAddComponentEntry<MeshRendererComponent>("Mesh Renderer"); });
                drawRegistryCategory("Mesh");
                endCategory(meshOpen);
            }

            bool physOpen = beginCategory("Physics");
            if (physOpen || !searchString.empty())
            {
                drawEntry("Rigidbody", [&]() { DrawAddComponentEntry<RigidbodyComponent>("Rigidbody"); });
                drawEntry("Box Collider", [&]() { DrawAddComponentEntry<BoxColliderComponent>("Box Collider"); });
                drawEntry("Sphere Collider", [&]() { DrawAddComponentEntry<SphereColliderComponent>("Sphere Collider"); });
                drawEntry("Capsule Collider", [&]() { DrawAddComponentEntry<CapsuleColliderComponent>("Capsule Collider"); });
                drawEntry("Mesh Collider", [&]() { DrawAddComponentEntry<MeshColliderComponent>("Mesh Collider"); });
                drawRegistryCategory("Physics");
                endCategory(physOpen);
            }

            bool phys2dOpen = beginCategory("Physics 2D");
            if (phys2dOpen || !searchString.empty())
            {
                drawEntry("RigidBody 2D", [&]() { DrawAddComponentEntry<RigidBody2DComponent>("RigidBody 2D"); });
                drawEntry("Box Collider 2D", [&]() { DrawAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D"); });
                drawEntry("Circle Collider 2D", [&]() { DrawAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D"); });
                drawRegistryCategory("Physics 2D");
                endCategory(phys2dOpen);
            }

            bool rendOpen = beginCategory("Rendering");
            if (rendOpen || !searchString.empty())
            {
                drawEntry("Camera", [&]() { DrawAddComponentEntry<CameraComponent>("Camera"); });
                drawEntry("Directional Light", [&]() { DrawAddComponentEntry<DirectionalLightComponent>("Directional Light"); });
                drawEntry("Point Light", [&]() { DrawAddComponentEntry<PointLightComponent>("Point Light"); });
                drawEntry("Spot Light", [&]() { DrawAddComponentEntry<SpotLightComponent>("Spot Light"); });
                drawEntry("Sprite Renderer", [&]() { DrawAddComponentEntry<SpriteRendererComponent>("Sprite Renderer"); });
                drawEntry("Terrain", [&]() { DrawAddComponentEntry<TerrainComponent>("Terrain"); });
                drawEntry("Particle System", [&]() { DrawAddComponentEntry<ParticleSystemComponent>("Particle System"); });
                drawEntry("Reflection Probe", [&]() { DrawAddComponentEntry<ReflectionProbeComponent>("Reflection Probe"); });
                drawEntry("Adaptive Probe Volume", [&]() { DrawAddComponentEntry<AdaptiveProbeVolumeComponent>("Adaptive Probe Volume"); });
                drawRegistryCategory("Rendering");
                endCategory(rendOpen);
            }

            bool scriptsOpen = beginCategory("Scripts");
            if (scriptsOpen || !searchString.empty())
            {
                // Her dildeki betik aynı Script bileşenine eklenir; çalışma zamanı dosya uzantısından seçilir
                auto attachScript = [&](const std::string& path) {
                    if (!path.empty() && AssetActions::RefuseDataScript(path))
                    {
                        ImGui::CloseCurrentPopup();
                        return;
                    }
                    if (!m_SelectionContext.HasComponent<ConquerorScriptComponent>())
                        m_SelectionContext.AddComponent<ConquerorScriptComponent>();
                    ConquerorScriptData data;
                    if (!path.empty())
                    {
                        data.ScriptPath = ToSerializablePath(path);
                        auto& meta = GetScriptMeta(path);
                        if (!meta.Classes.empty())
                            data.ClassName = meta.Classes.front();
                    }
                    m_SelectionContext.GetComponent<ConquerorScriptComponent>().Scripts.push_back(data);
                    ImGui::CloseCurrentPopup();
                };

                // Unity'deki "New script" gibi: ad + dil seç, Assets/Scripts altına oluştur ve ekle
                drawEntry("New Script", [&]() {
                    if (!ImGui::BeginMenu(ICON_FA_FILE_CIRCLE_PLUS "  New Script"))
                        return;
                    static char nameBuffer[64] = "NewBehaviour";
                    static std::string languageId = "cqs";
                    static std::string createError;
                    ImGui::TextDisabled("Name");
                    ImGui::SetNextItemWidth(240.0f);
                    bool enter = ImGui::InputText("##NewScriptName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::Spacing();
                    ImGui::TextDisabled("Language");
                    for (auto* runtime : Scripting::ScriptRuntimes::All())
                    {
                        if (std::string(runtime->GetLanguageId()) == "stategraph")
                            continue;
                        bool available = runtime->IsAvailable();
                        std::string label = std::string(runtime->GetName()) + "  (" + runtime->GetExtensions().front() + ")##lang_" + runtime->GetLanguageId();
                        ImGui::BeginDisabled(!available);
                        if (ImGui::RadioButton(label.c_str(), languageId == runtime->GetLanguageId()))
                            languageId = runtime->GetLanguageId();
                        ImGui::EndDisabled();
                        if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%s", runtime->GetUnavailableReason().c_str());
                    }
                    ImGui::Spacing();
                    auto* runtime = Scripting::ScriptRuntimes::ForLanguage(languageId);
                    auto assets = Project::GetAssetDirectory();
                    bool canCreate = runtime && runtime->IsAvailable() && !assets.empty();
                    ImGui::BeginDisabled(!canCreate);
                    bool create = ImGui::Button(ICON_FA_PLUS "  Create and Add##NewScriptCreate", ImVec2(240.0f, 0.0f)) || enter;
                    ImGui::EndDisabled();
                    if (create && canCreate)
                    {
                        // Sınıf adı olarak geçerli olsun (Java/C# dosya adı = sınıf adı)
                        std::string name;
                        for (char c : std::string(nameBuffer))
                            if (std::isalnum((unsigned char)c) || c == '_')
                                name += c;
                        if (name.empty())
                            name = "NewBehaviour";
                        if (std::isdigit((unsigned char)name[0]))
                            name = "_" + name;
                        std::error_code ec;
                        auto dir = std::filesystem::is_directory(assets / "Scripts", ec) ? assets / "Scripts" : assets;
                        auto path = dir / (name + runtime->GetExtensions().front());
                        if (std::filesystem::exists(path, ec))
                            createError = "A script named '" + path.filename().string() + "' already exists.";
                        else
                        {
                            std::ofstream f(path);
                            f << runtime->GetNewScriptTemplate(name);
                            f.close();
                            createError.clear();
                            CQ_CORE_INFO("Created script {}", path.string());
                            attachScript(path.string());
                        }
                    }
                    if (!createError.empty())
                        ImGui::TextColored(UI::Colors::Error, "%s", createError.c_str());
                    ImGui::EndMenu();
                });
                drawEntry("Empty Script", [&]() {
                    if (ImGui::MenuItem(ICON_FA_FILE "  Empty Script Slot"))
                        attachScript("");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Adds a Script slot; pick a file of any language in the Inspector.");
                });
                drawEntry("Native Script", [&]() {
                    if (ImGui::MenuItem(ICON_FA_MICROCHIP "  Native Script (C++ class)"))
                    {
                        if (!m_SelectionContext.HasComponent<NativeScriptComponent>())
                            m_SelectionContext.AddComponent<NativeScriptComponent>().Scripts.push_back({});
                        else
                            m_SelectionContext.GetComponent<NativeScriptComponent>().Scripts.push_back({});
                        ImGui::CloseCurrentPopup();
                    }
                });

                // Projedeki mevcut betikler, dile göre gruplu (aramada düz liste)
                if (!s_AddMenuScripts.empty() && searchString.empty())
                    ImGui::Separator();
                for (auto* runtime : Scripting::ScriptRuntimes::All())
                {
                    std::vector<const std::filesystem::path*> scripts;
                    for (auto& path : s_AddMenuScripts)
                        if (Scripting::ScriptRuntimes::ForPath(path.string()) == runtime)
                            scripts.push_back(&path);
                    if (scripts.empty())
                        continue;
                    // "C#" gibi adlarda '#' kimlik ayracıyla karışmasın: kimlik PushID ile, etikette '##' yok
                    std::string groupLabel = std::string(ICON_FA_FILE_CODE "  ") + runtime->GetName();
                    ImGui::PushID(runtime->GetLanguageId());
                    bool groupOpen = beginCategory(groupLabel.c_str());
                    if (groupOpen || !searchString.empty())
                    {
                        for (auto* path : scripts)
                        {
                            std::string name = path->stem().string();
                            drawEntry(name, [&]() {
                                std::string label = name + "##" + path->string();
                                if (ImGui::MenuItem(label.c_str(), runtime->GetExtensions().front().c_str()))
                                    attachScript(path->string());
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                    ImGui::SetTooltip("%s", ToSerializablePath(path->string()).c_str());
                            });
                        }
                        endCategory(groupOpen);
                    }
                    ImGui::PopID();
                }
                endCategory(scriptsOpen);
            }

            bool aiOpen = beginCategory("AI & Navigation");
            if (aiOpen || !searchString.empty())
            {
                drawRegistryCategory("AI & Navigation");
                endCategory(aiOpen);
            }

            bool uiOpen = beginCategory("UI");
            if (uiOpen || !searchString.empty())
            {
                drawEntry("Button", [&]() { DrawAddComponentEntry<ButtonComponent>("Button"); });
                drawEntry("Canvas", [&]() { DrawAddComponentEntry<CanvasComponent>("Canvas"); });
                drawEntry("Canvas Scaler", [&]() { DrawAddComponentEntry<CanvasScalerComponent>("Canvas Scaler"); });
                drawEntry("Graphic Raycaster", [&]() { DrawAddComponentEntry<GraphicRaycasterComponent>("Graphic Raycaster"); });
                drawEntry("Text Renderer", [&]() { DrawAddComponentEntry<TextRendererComponent>("Text Renderer"); });
                drawEntry("Rect Transform", [&]() { DrawAddComponentEntry<RectTransformComponent>("Rect Transform"); });
                drawEntry("UI Image", [&]() { DrawAddComponentEntry<UIImageComponent>("UI Image"); });
                drawEntry("UI Text", [&]() { DrawAddComponentEntry<UITextComponent>("UI Text"); });
                drawEntry("UI Button", [&]() { DrawAddComponentEntry<UIButtonComponent>("UI Button"); });
                drawEntry("UI Toggle", [&]() { DrawAddComponentEntry<UIToggleComponent>("UI Toggle"); });
                drawEntry("UI Slider", [&]() { DrawAddComponentEntry<UISliderComponent>("UI Slider"); });
                drawEntry("UI Input Field", [&]() { DrawAddComponentEntry<UIInputFieldComponent>("UI Input Field"); });
                drawEntry("UI Dropdown", [&]() { DrawAddComponentEntry<UIDropdownComponent>("UI Dropdown"); });
                drawEntry("UI Scroll Rect", [&]() { DrawAddComponentEntry<UIScrollRectComponent>("UI Scroll Rect"); });
                drawEntry("UI Layout Group", [&]() { DrawAddComponentEntry<UILayoutGroupComponent>("UI Layout Group"); });
                drawRegistryCategory("UI");
                endCategory(uiOpen);
            }

            // Yerleşik menüde olmayan kategoriler
            {
                static const char* known[] = { "Animation", "Audio", "Mesh", "Physics", "Physics 2D", "Rendering", "Scripts", "AI & Navigation", "UI" };
                std::vector<std::string> extra;
                for (auto& info : ComponentRegistry::All())
                {
                    if (info.Builtin || !info.AllowInAddMenu) continue;
                    bool isKnown = false;
                    for (const char* k : known) isKnown |= info.Category == k;
                    if (!isKnown && std::find(extra.begin(), extra.end(), info.Category) == extra.end())
                        extra.push_back(info.Category);
                }
                std::sort(extra.begin(), extra.end());
                for (auto& category : extra)
                {
                    bool open = beginCategory(category.c_str());
                    if (open || !searchString.empty())
                    {
                        drawRegistryCategory(category);
                        endCategory(open);
                    }
                }
            }

            // Addon'ların sağladığı betik bileşenleri
            {
                bool any = false;
                for (auto& addon : AddonManager::GetInstalled())
                    any |= AddonManager::IsEnabled(addon.Id) && !addon.Components.empty();
                if (any)
                {
                    bool open = beginCategory(ICON_FA_PUZZLE_PIECE "  Addons");
                    if (open || !searchString.empty())
                    {
                        for (auto& addon : AddonManager::GetInstalled())
                        {
                            if (!AddonManager::IsEnabled(addon.Id))
                                continue;
                            for (auto& component : addon.Components)
                            {
                                std::string name = std::filesystem::path(component).stem().string();
                                drawEntry(name, [&]() {
                                    std::string label = name + "##" + addon.Id;
                                    if (ImGui::MenuItem(label.c_str()))
                                    {
                                        if (!m_SelectionContext.HasComponent<ConquerorScriptComponent>())
                                            m_SelectionContext.AddComponent<ConquerorScriptComponent>();
                                        ConquerorScriptData data;
                                        data.ScriptPath = ToSerializablePath((addon.Directory / component).string());
                                        m_SelectionContext.GetComponent<ConquerorScriptComponent>().Scripts.push_back(data);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                        ImGui::SetTooltip("From addon: %s", addon.Name.c_str());
                                });
                            }
                        }
                        endCategory(open);
                    }
                }
            }

            ImGui::EndPopup();
        }

        }
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
    }

    template<typename T>
    void InspectorPanel::DrawAddComponentEntry(const std::string& entryName)
    {
        if (!m_SelectionContext.HasComponent<T>())
        {
            if (ImGui::MenuItem(entryName.c_str()))
            {
                m_SelectionContext.AddComponent<T>();
                ImGui::CloseCurrentPopup();
            }
        }
    }
}
