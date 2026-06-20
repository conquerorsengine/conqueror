#include "HierarchyPanel.h"
#include "Scene/Components.h"
#include "Renderer/Renderer2D.h"
#include "EditorState.h"
#include "UndoRedo/UndoRedoManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

namespace Conqueror::Editor
{
    HierarchyPanel::HierarchyPanel(const std::shared_ptr<Scene>& scene)
    {
        SetContext(scene);
    }

    void HierarchyPanel::SetContext(const std::shared_ptr<Scene>& scene)
    {
        m_Context = scene;
        m_SelectionContext = {};
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

    void HierarchyPanel::OnImGuiRender()
    {
        ImGui::Begin("Hierarchy");

        if (m_Context)
        {
            // Tüm root entity'leri topla
            std::vector<Entity> rootEntities;
            m_Context->m_Registry.view<TagComponent>().each([&](auto entityID, [[maybe_unused]] auto& tc)
            {
                Entity entity{ entityID, m_Context.get() };
                
                // Parent'ı yoksa listeye ekle
                if (!entity.GetParent())
                {
                    rootEntities.push_back(entity);
                }
            });
            
            // Ters çevir (en eski en üstte, en yeni en altta)
            std::reverse(rootEntities.begin(), rootEntities.end());
            
            // Göster
            for (auto& entity : rootEntities)
            {
                DrawEntityNode(entity);
            }

            if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            {
                m_SelectionContext = {};
                EditorState::Get().ClearSelection();
            }

            // Window'un boş alanına drop için
            if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(0))
            {
                ImGuiPayload* payload = (ImGuiPayload*)ImGui::GetDragDropPayload();
                if (payload && payload->IsDataType("ENTITY_NODE"))
                {
                    Entity droppedEntity = *(Entity*)payload->Data;
                    droppedEntity.RemoveParent();
                }
            }

            // Boş alana drop - unparent
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                {
                    Entity droppedEntity = *(Entity*)payload->Data;
                    droppedEntity.RemoveParent();
                }
                ImGui::EndDragDropTarget();
            }

            // Right-click on blank space
            if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Create Empty Entity"))
                    m_Context->CreateEntity("Empty Entity");

                ImGui::Separator();

                // 2D Object submenu
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
                            auto entity = m_Context->CreateEntity(spriteName);
                            auto& sprite = entity.AddComponent<SpriteRendererComponent>();
                            
                            std::string texturePath = "Resources/test/" + std::string(spriteName) + ".png";
                            sprite.Texture = Texture2D::Create(texturePath);
                        }
                    }

                    ImGui::EndMenu();
                }

                // 3D Object submenu
                if (ImGui::BeginMenu("3D Object"))
                {
                    if (ImGui::MenuItem("Import Model..."))
                    {
                        auto entity = m_Context->CreateEntity("Model");
                        entity.AddComponent<ModelComponent>();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Cube"))
                    {
                        auto entity = m_Context->CreateEntity("Cube");
                        auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                        meshRenderer.Type = MeshType::Cube;
                        meshRenderer.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    if (ImGui::MenuItem("Sphere"))
                    {
                        auto entity = m_Context->CreateEntity("Sphere");
                        auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                        meshRenderer.Type = MeshType::Sphere;
                        meshRenderer.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    if (ImGui::MenuItem("Plane"))
                    {
                        auto entity = m_Context->CreateEntity("Plane");
                        auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                        meshRenderer.Type = MeshType::Plane;
                        meshRenderer.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    if (ImGui::MenuItem("Cylinder"))
                    {
                        auto entity = m_Context->CreateEntity("Cylinder");
                        auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                        meshRenderer.Type = MeshType::Cylinder;
                        meshRenderer.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    ImGui::EndMenu();
                }

                ImGui::Separator();

                // Camera
                if (ImGui::MenuItem("Camera"))
                {
                    auto entity = m_Context->CreateEntity("Camera");
                    entity.AddComponent<CameraComponent>();
                }

                // Light submenu
                if (ImGui::BeginMenu("Light"))
                {
                    if (ImGui::MenuItem("Directional Light"))
                    {
                        auto entity = m_Context->CreateEntity("Directional Light");
                        auto& light = entity.AddComponent<DirectionalLightComponent>();
                        light.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
                        light.Color = glm::vec3(1.0f, 1.0f, 1.0f);
                        light.Intensity = 1.0f;
                    }

                    if (ImGui::MenuItem("Point Light"))
                    {
                        auto entity = m_Context->CreateEntity("Point Light");
                        auto& light = entity.AddComponent<PointLightComponent>();
                        light.Color = glm::vec3(1.0f, 1.0f, 1.0f);
                        light.Intensity = 1.0f;
                        light.Range = 10.0f;
                    }

                    if (ImGui::MenuItem("Spot Light"))
                    {
                        auto entity = m_Context->CreateEntity("Spot Light");
                        auto& light = entity.AddComponent<SpotLightComponent>();
                        light.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
                        light.Color = glm::vec3(1.0f, 1.0f, 1.0f);
                        light.Intensity = 1.0f;
                        light.Range = 10.0f;
                    }

                    ImGui::EndMenu();
                }

                // Audio
                if (ImGui::BeginMenu("Audio"))
                {
                    if (ImGui::MenuItem("Audio Source"))
                    {
                        auto entity = m_Context->CreateEntity("Audio Source");
                        entity.AddComponent<AudioSourceComponent>();
                    }
                    
                    if (ImGui::MenuItem("Audio Listener"))
                    {
                        auto entity = m_Context->CreateEntity("Audio Listener");
                        entity.AddComponent<AudioListenerComponent>();
                    }
                    
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                // UI
                if (ImGui::BeginMenu("UI"))
                {
                    if (ImGui::MenuItem("Text"))
                    {
                        // Canvas bul veya oluştur
                        Entity canvasEntity;
                        auto canvasView = m_Context->m_Registry.view<CanvasComponent>();
                        if (canvasView.empty())
                        {
                            // Canvas yok, oluştur
                            canvasEntity = m_Context->CreateEntity("Canvas");
                            canvasEntity.AddComponent<CanvasComponent>();
                            canvasEntity.AddComponent<CanvasScalerComponent>();
                            canvasEntity.AddComponent<GraphicRaycasterComponent>();
                        }
                        else
                        {
                            // İlk Canvas'ı kullan
                            canvasEntity = Entity{ *canvasView.begin(), m_Context.get() };
                        }
                        
                        auto entity = m_Context->CreateEntity("Text");
                        entity.AddComponent<TextRendererComponent>();
                        
                        // UI layer'ını ayarla (layer 5)
                        if (entity.HasComponent<LayerComponent>())
                        {
                            auto& layer = entity.GetComponent<LayerComponent>();
                            layer.Layer = 5; // UI layer
                        }
                        
                        // Canvas'ın child'ı yap
                        entity.SetParent(canvasEntity);
                    }

                    if (ImGui::MenuItem("Button"))
                    {
                        // Canvas bul veya oluştur
                        Entity canvasEntity;
                        auto canvasView = m_Context->m_Registry.view<CanvasComponent>();
                        if (canvasView.empty())
                        {
                            // Canvas yok, oluştur
                            canvasEntity = m_Context->CreateEntity("Canvas");
                            canvasEntity.AddComponent<CanvasComponent>();
                            canvasEntity.AddComponent<CanvasScalerComponent>();
                            canvasEntity.AddComponent<GraphicRaycasterComponent>();
                        }
                        else
                        {
                            // İlk Canvas'ı kullan
                            canvasEntity = Entity{ *canvasView.begin(), m_Context.get() };
                        }
                        
                        // Button parent entity
                        auto buttonEntity = m_Context->CreateEntity("Button");
                        auto& buttonImage = buttonEntity.AddComponent<ImageComponent>();
                        auto& buttonComp = buttonEntity.AddComponent<ButtonComponent>();
                        
                        // UIButton.png yükle
                        buttonImage.Texture = Texture2D::Create("Resources/ui/UIButton.png");
                        buttonImage.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                        
                        // UI layer
                        if (buttonEntity.HasComponent<LayerComponent>())
                        {
                            auto& layer = buttonEntity.GetComponent<LayerComponent>();
                            layer.Layer = 5; // UI layer
                        }
                        
                        // Canvas'ın child'ı yap
                        buttonEntity.SetParent(canvasEntity);

                        // Text child entity
                        auto textEntity = m_Context->CreateEntity("Text");
                        auto& textRenderer = textEntity.AddComponent<TextRendererComponent>();
                        textRenderer.Text = "Button";
                        textRenderer.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                        
                        // UI layer
                        if (textEntity.HasComponent<LayerComponent>())
                        {
                            auto& layer = textEntity.GetComponent<LayerComponent>();
                            layer.Layer = 5; // UI layer
                        }

                        // Text'i Button'ın child'ı yap
                        textEntity.SetParent(buttonEntity);
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }

    void HierarchyPanel::DrawEntityNode(Entity entity)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        bool isSelected = EditorState::Get().IsEntitySelected(entity);
        ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
        
        bool hasChildren = entity.HasChildren();
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (m_IsRenaming && m_RenameEntity == entity)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
            ImGui::PushItemWidth(-1);
            ImGui::PushID((void*)(uint64_t)(uint32_t)entity);

            bool renameDone = false;
            bool renameCancel = false;

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue))
            {
                renameDone = true;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                renameCancel = true;

            if (ImGui::IsItemDeactivated() && m_RenameFrame > 0)
            {
                renameDone = true;
            }

            m_RenameFrame++;

            ImGui::PopItemWidth();
            ImGui::PopStyleVar();
            ImGui::PopID();

            if (renameDone && !renameCancel)
            {
                if (strlen(m_RenameBuffer) > 0 && std::string(m_RenameBuffer) != tag)
                {
                    auto cmd = std::make_shared<RenameEntityCommand>(entity, std::string(m_RenameBuffer));
                    UndoRedoManager::Get().Execute(cmd);
                }
                m_IsRenaming = false;
            }
            else if (renameCancel)
            {
                m_IsRenaming = false;
            }

            ImGui::SameLine();
        }
        else
        {
            bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());

            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ENTITY_NODE", &entity, sizeof(Entity));
                ImGui::Text("%s", tag.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                {
                    Entity droppedEntity = *(Entity*)payload->Data;
                    
                    if (droppedEntity != entity && !IsDescendant(entity, droppedEntity))
                    {
                        droppedEntity.SetParent(entity);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::IsItemClicked())
            {
                auto& io = ImGui::GetIO();
                auto& state = EditorState::Get();

                if (io.KeyCtrl)
                    state.ToggleEntitySelection(entity);
                else
                    state.SelectEntity(entity);

                m_SelectionContext = entity;
                if (m_SelectionChangedCallback)
                    m_SelectionChangedCallback(entity);
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_IsRenaming = true;
                m_RenameEntity = entity;
                strncpy(m_RenameBuffer, tag.c_str(), sizeof(m_RenameBuffer) - 1);
                m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
            }

            bool entityDeleted = false;
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Duplicate"))
                {
                    if (m_Context)
                    {
                        auto cmd = std::make_shared<DuplicateEntityCommand>(m_Context, entity);
                        UndoRedoManager::Get().Execute(cmd);
                        if (cmd->GetDuplicatedEntity())
                        {
                            EditorState::Get().SelectEntity(cmd->GetDuplicatedEntity());
                            m_SelectionContext = cmd->GetDuplicatedEntity();
                            if (m_SelectionChangedCallback)
                                m_SelectionChangedCallback(cmd->GetDuplicatedEntity());
                        }
                    }
                }

                if (ImGui::MenuItem("Rename"))
                {
                    m_IsRenaming = true;
                    m_RenameEntity = entity;
                    strncpy(m_RenameBuffer, tag.c_str(), sizeof(m_RenameBuffer) - 1);
                    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Copy"))
                {
                    EditorState::Get().CopyEntities({ entity });
                }

                if (ImGui::MenuItem("Cut"))
                {
                    auto& state = EditorState::Get();
                    state.CutEntities({ entity });
                }

                if (ImGui::MenuItem("Paste"))
                {
                    auto& state = EditorState::Get();
                    if (state.HasClipboardData())
                    {
                        std::vector<Entity> newEntities;
                        bool wasCut = state.IsCutOperation();
                        for (auto& src : state.GetClipboardEntities())
                        {
                            if (src)
                            {
                                Entity newEntity = m_Context->DuplicateEntity(src);
                                if (newEntity)
                                    newEntities.push_back(newEntity);
                            }
                        }
                        if (wasCut)
                        {
                            for (auto& src : state.GetClipboardEntities())
                            {
                                if (src && src.GetScene() == m_Context.get())
                                    m_Context->DestroyEntity(src);
                            }
                            state.FinalizeCut();
                        }
                        state.ClearClipboard();
                        if (!newEntities.empty())
                        {
                            m_SelectionContext = newEntities[0];
                            if (m_SelectionChangedCallback)
                                m_SelectionChangedCallback(newEntities[0]);
                        }
                    }
                }

                if (entity.GetParent())
                {
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Unparent"))
                    {
                        entity.RemoveParent();
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Delete"))
                    entityDeleted = true;

                ImGui::EndPopup();
            }

            if (opened && hasChildren)
            {
                auto children = entity.GetChildren();
                for (auto child : children)
                {
                    DrawEntityNode(child);
                }
                
                ImGui::TreePop();
            }

        if (entityDeleted)
        {
            EditorState::Get().ClearSelection();
            auto cmd = std::make_shared<DestroyEntityCommand>(m_Context, entity);
            UndoRedoManager::Get().Execute(cmd);
            if (m_SelectionContext == entity)
                m_SelectionContext = {};
        }
        }
    }

    bool HierarchyPanel::IsDescendant(Entity parent, Entity potentialDescendant)
    {
        auto children = parent.GetChildren();
        for (auto child : children)
        {
            if (child == potentialDescendant)
                return true;
            
            if (IsDescendant(child, potentialDescendant))
                return true;
        }
        return false;
    }
}
