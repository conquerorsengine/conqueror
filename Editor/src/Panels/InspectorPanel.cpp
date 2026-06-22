#include "InspectorPanel.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Core/Animation/AnimationController.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <nfd.h>

#ifdef _WIN32
#include <windows.h>
#include <libloaderapi.h>
#else
#include <unistd.h>
#include <linux/limits.h>
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
        char buffer[PATH_MAX] = { 0 };
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1)
        {
            buffer[len] = '\0';
            return std::filesystem::path(buffer).parent_path();
        }
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
                return relative.string();
        }

        auto engineDir = GetEngineDirectory();
        std::error_code ec;
        auto relative = std::filesystem::relative(absolutePath, engineDir, ec);
        if (!ec && !relative.empty() && relative.native()[0] != '.')
        {
            std::string relStr = relative.string();
            if (relStr.find("Resources/") == 0 || relStr.find("Resources\\") == 0)
                return "CQ:" + relStr;
        }

        return absolutePath;
    }

    void InspectorPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectionContext = entity;
    }

    void InspectorPanel::OnImGuiRender()
    {
        ImGui::Begin("Inspector");

        if (m_SelectionContext)
        {
            DrawComponents(m_SelectionContext);
        }

        ImGui::End();
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

    template<typename T, typename UIFunction>
    static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        
        if (entity.HasComponent<T>())
        {
            auto& component = entity.GetComponent<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
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
                uiFunction(component);
                ImGui::TreePop();
            }

            if (removeComponent)
                entity.RemoveComponent<T>();
        }
    }

    void InspectorPanel::DrawComponents(Entity entity)
    {
        // Entity ismi
        if (entity.HasComponent<TagComponent>())
        {
            auto& tagComp = entity.GetComponent<TagComponent>();

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, tagComp.Tag.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("##EntityName", buffer, sizeof(buffer)))
            {
                tagComp.Tag = std::string(buffer);
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(-1);

        if (ImGui::Button("Add Component"))
            ImGui::OpenPopup("AddComponent");

        if (ImGui::BeginPopup("AddComponent"))
        {
            static char searchBuffer[128] = "";
            ImGui::SetNextItemWidth(200.0f);
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

            bool animOpen = beginCategory("Animation");
            if (animOpen || !searchString.empty())
            {
                drawEntry("Animator", [&]() { DrawAddComponentEntry<AnimatorComponent>("Animator"); });
                drawEntry("Animation", [&]() { DrawAddComponentEntry<AnimationComponent>("Animation"); });
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
                endCategory(audioOpen);
            }

            bool meshOpen = beginCategory("Mesh");
            if (meshOpen || !searchString.empty())
            {
                drawEntry("Mesh Renderer", [&]() { DrawAddComponentEntry<MeshRendererComponent>("Mesh Renderer"); });
                drawEntry("Model", [&]() { DrawAddComponentEntry<ModelComponent>("Model"); });
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
                endCategory(physOpen);
            }

            bool phys2dOpen = beginCategory("Physics 2D");
            if (phys2dOpen || !searchString.empty())
            {
                drawEntry("RigidBody 2D", [&]() { DrawAddComponentEntry<RigidBody2DComponent>("RigidBody 2D"); });
                drawEntry("Box Collider 2D", [&]() { DrawAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D"); });
                drawEntry("Circle Collider 2D", [&]() { DrawAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D"); });
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
                endCategory(rendOpen);
            }

            bool scriptsOpen = beginCategory("Scripts");
            if (scriptsOpen || !searchString.empty())
            {
                drawEntry("Conqueror Script", [&]() {
                    if (ImGui::MenuItem("Conqueror Script"))
                    {
                        if (!m_SelectionContext.HasComponent<ConquerorScriptComponent>())
                            m_SelectionContext.AddComponent<ConquerorScriptComponent>().Scripts.push_back({});
                        else
                            m_SelectionContext.GetComponent<ConquerorScriptComponent>().Scripts.push_back({});
                        ImGui::CloseCurrentPopup();
                    }
                });
                drawEntry("Native Script", [&]() {
                    if (ImGui::MenuItem("Native Script"))
                    {
                        if (!m_SelectionContext.HasComponent<NativeScriptComponent>())
                            m_SelectionContext.AddComponent<NativeScriptComponent>().Scripts.push_back({});
                        else
                            m_SelectionContext.GetComponent<NativeScriptComponent>().Scripts.push_back({});
                        ImGui::CloseCurrentPopup();
                    }
                });
                endCategory(scriptsOpen);
            }

            bool aiOpen = beginCategory("AI & Navigation");
            if (aiOpen || !searchString.empty())
            {
                drawEntry("NavMesh Agent", [&]() { DrawAddComponentEntry<NavMeshAgentComponent>("NavMesh Agent"); });
                drawEntry("NavMesh Obstacle", [&]() { DrawAddComponentEntry<NavMeshObstacleComponent>("NavMesh Obstacle"); });
                drawEntry("NavMesh Surface", [&]() { DrawAddComponentEntry<NavMeshSurfaceComponent>("NavMesh Surface"); });
                endCategory(aiOpen);
            }

            bool uiOpen = beginCategory("UI");
            if (uiOpen || !searchString.empty())
            {
                drawEntry("Button", [&]() { DrawAddComponentEntry<ButtonComponent>("Button"); });
                drawEntry("Canvas", [&]() { DrawAddComponentEntry<CanvasComponent>("Canvas"); });
                drawEntry("Canvas Scaler", [&]() { DrawAddComponentEntry<CanvasScalerComponent>("Canvas Scaler"); });
                drawEntry("Graphic Raycaster", [&]() { DrawAddComponentEntry<GraphicRaycasterComponent>("Graphic Raycaster"); });
                drawEntry("Image", [&]() { DrawAddComponentEntry<ImageComponent>("Image"); });
                drawEntry("Text Renderer", [&]() { DrawAddComponentEntry<TextRendererComponent>("Text Renderer"); });
                endCategory(uiOpen);
            }

            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();

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
        });

        DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
        {
            ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
            
            // Sprite dropdown (Resources/test klasöründeki PNG'ler - hot reload)
            std::vector<std::string> spriteFiles;
            spriteFiles.push_back("None");
            
            // Her frame Resources/test klasöründeki PNG dosyalarını tara
            std::string testPath = "Resources/test";
            if (std::filesystem::exists(testPath))
            {
                std::vector<std::string> tempFiles;
                for (const auto& entry : std::filesystem::directory_iterator(testPath))
                {
                    if (entry.is_regular_file() && entry.path().extension() == ".png")
                    {
                        tempFiles.push_back(entry.path().stem().string());
                    }
                }
                
                // Alfabetik sırala
                std::sort(tempFiles.begin(), tempFiles.end());
                spriteFiles.insert(spriteFiles.end(), tempFiles.begin(), tempFiles.end());
            }
            
            // Mevcut texture'ın adını bul
            std::string currentSprite = "None";
            if (component.Texture)
            {
                // Component'te texture path'i sakla
                if (component.Texture->GetPath().empty())
                {
                    currentSprite = "Custom";
                }
                else
                {
                    // Path'den dosya adını çıkar
                    std::filesystem::path texPath(component.Texture->GetPath());
                    currentSprite = texPath.stem().string();
                }
            }
            
            if (ImGui::BeginCombo("Sprite", currentSprite.c_str()))
            {
                for (const auto& spriteName : spriteFiles)
                {
                    bool isSelected = (currentSprite == spriteName);
                    if (ImGui::Selectable(spriteName.c_str(), isSelected))
                    {
                        if (spriteName == "None")
                        {
                            component.Texture = nullptr;
                        }
                        else
                        {
                            std::string texturePath = "Resources/test/" + spriteName + ".png";
                            component.Texture = Texture2D::Create(texturePath);
                        }
                    }
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                
                ImGui::EndCombo();
            }
            
            // Drag & Drop target (Content Browser'dan texture sürükleme)
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                    std::filesystem::path path(droppedPath);
                    std::string ext = path.extension().string();
                    
                    CQ_CORE_INFO("Sprite Renderer: Drag & Drop received - Path: {0}, Extension: {1}", droppedPath, ext);
                    
                    // Sadece png/jpg dosyalarını kabul et
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                    {
                        component.Texture = Texture2D::Create(droppedPath);
                        CQ_CORE_INFO("Sprite texture set: {0}", droppedPath);
                    }
                    else
                    {
                        CQ_CORE_WARN("Sprite Renderer: Invalid file type {0}, only PNG/JPG accepted", ext);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            
            ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.0f);
        });

        DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [](auto& component)
        {
            const char* meshTypeNames[] = { "Cube", "Sphere", "Plane", "Cylinder" };
            int currentType = static_cast<int>(component.Type);
            if (ImGui::Combo("Mesh Type", &currentType, meshTypeNames, 4))
                component.Type = static_cast<MeshType>(currentType);

            ImGui::Separator();

            ImGui::Text("Albedo Texture");
            if (!component.TexturePath.empty())
            {
                std::string displayPath = ToSerializablePath(component.TexturePath);
                ImGui::Text("%s", displayPath.c_str());
                ImGui::SameLine();
                if (ImGui::Button("X##RemoveTexMR"))
                {
                    component.Texture = nullptr;
                    component.TexturePath.clear();
                }

                if (component.Texture)
                {
                    uint32_t texID = component.Texture->GetRendererID();
                    if (texID != 0)
                    {
                        ImVec2 texSize(64, 64);
                        ImGui::Image((ImTextureID)(uintptr_t)texID, texSize);
                    }
                }
            }
            else
            {
                ImGui::Text("None");
            }

            if (ImGui::Button("Load Texture...##MR"))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Image Files", "png,jpg,jpeg" } };
                nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
                if (result == NFD_OKAY)
                {
                    std::string loadedPathStr(outPath);
                    component.Texture = Texture2D::Create(loadedPathStr);
                    component.TexturePath = ToSerializablePath(loadedPathStr);
                    NFD_FreePathU8(outPath);
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
                    
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                    {
                        component.Texture = Texture2D::Create(droppedPath);
                        component.TexturePath = ToSerializablePath(droppedPath);
                        CQ_CORE_INFO("MeshRenderer texture set: {0}", droppedPath);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();

            if (component.MaterialInstance.get())
            {
                ImGui::Text("Material: %s", component.MaterialInstance->Name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Remove##MaterialMR"))
                {
                    component.MaterialInstance = nullptr;
                }
                else
                {
                    ImGui::Separator();
                    ImGui::Text("Material Properties");
                    char buffer[256] = {};
                    strncpy(buffer, component.MaterialInstance->Name.c_str(), sizeof(buffer) - 1);
                    if (ImGui::InputText("Name##MaterialMR", buffer, sizeof(buffer)))
                        component.MaterialInstance->Name = buffer;

                    if (ImGui::ColorEdit3("Albedo##MR", glm::value_ptr(component.MaterialInstance->Albedo)))
                        component.Color = glm::vec4(component.MaterialInstance->Albedo, 1.0f);

                    ImGui::SliderFloat("Metallic##MR", &component.MaterialInstance->Metallic, 0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness##MR", &component.MaterialInstance->Roughness, 0.0f, 1.0f);
                    ImGui::SliderFloat("AO##MR", &component.MaterialInstance->AO, 0.0f, 1.0f);
                    
                    ImGui::ColorEdit3("Emission Color##MR", glm::value_ptr(component.MaterialInstance->EmissionColor));
                    ImGui::DragFloat("Emission Strength##MR", &component.MaterialInstance->EmissionStrength, 0.1f, 0.0f, 100.0f);

                    component.Color = glm::vec4(component.MaterialInstance->Albedo, 1.0f);
                    component.Metallic = component.MaterialInstance->Metallic;
                    component.Roughness = component.MaterialInstance->Roughness;
                    component.AO = component.MaterialInstance->AO;
                }
            }
            else
            {
                ImGui::Text("No Material Assigned");
                if (ImGui::Button("Create New Material##MR"))
                {
                    component.MaterialInstance = Material::CreateDefault();
                    component.MaterialInstance->Albedo = glm::vec3(component.Color);
                    component.MaterialInstance->Metallic = component.Metallic;
                    component.MaterialInstance->Roughness = component.Roughness;
                    component.MaterialInstance->AO = component.AO;
                }
                
                ImGui::Separator();
                ImGui::Text("Fallback Properties");
                ImGui::ColorEdit4("Color##MR", glm::value_ptr(component.Color));
                ImGui::SliderFloat("Metallic##MR", &component.Metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness##MR", &component.Roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("AO##MR", &component.AO, 0.0f, 1.0f);
            }
        });

        DrawComponent<ModelComponent>("Model", entity, [](auto& component)
        {
            // Model path
            if (!component.FilePath.empty())
            {
                ImGui::Text("Model: %s", component.FilePath.c_str());
            }
            else
            {
                ImGui::Text("Model: None");
            }

            if (ImGui::Button("Load Model..."))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[4] = { { "3D Models", "obj,fbx,gltf,glb" }, { "OBJ", "obj" }, { "FBX", "fbx" }, { "GLTF", "gltf,glb" } };
                nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 4, nullptr);
                
                if (result == NFD_OKAY)
                {
                    component.FilePath = ToSerializablePath(outPath);
                    component.ModelData = ModelLoader::Load(outPath);
                    NFD_FreePathU8(outPath);
                }
                else if (result == NFD_CANCEL)
                {
                    CQ_CORE_INFO("Model load cancelled");
                }
                else
                {
                    CQ_CORE_ERROR("NFD Error: {0}", NFD_GetError());
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
                    
                    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
                    {
                        component.FilePath = ToSerializablePath(droppedPath);
                        component.ModelData = ModelLoader::Load(droppedPath);
                        CQ_CORE_INFO("Model loaded: {0}", droppedPath);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Model info
            if (component.ModelData)
            {
                ImGui::Separator();
                if (component.ModelData->IsSkinned)
                {
                    ImGui::Text("Skinned meshes: %zu", component.ModelData->SkinnedMeshes.size());
                    ImGui::Text("Animations: %zu", component.ModelData->Animations.size());
                    if (component.ModelData->SkeletonData)
                        ImGui::Text("Bones: %u", component.ModelData->SkeletonData->GetBoneCount());
                }
                else
                    ImGui::Text("Meshes: %zu", component.ModelData->Meshes.size());
                ImGui::Text("Materials: %zu", component.ModelData->Materials.size());

                if (!component.ModelData->Materials.empty())
                {
                    ImGui::Separator();
                    ImGui::Text("Material Properties");

                    for (size_t i = 0; i < component.ModelData->Materials.size(); i++)
                    {
                        auto& mat = component.ModelData->Materials[i];
                        if (!mat) continue;

                        std::string matLabel = "Material " + std::to_string(i) + "##modelmat" + std::to_string(i);
                        if (ImGui::TreeNode(matLabel.c_str()))
                        {
                            char buffer[256] = {};
                            strncpy(buffer, mat->Name.c_str(), sizeof(buffer) - 1);
                            std::string nameId = "Name##modelmat" + std::to_string(i);
                            if (ImGui::InputText(nameId.c_str(), buffer, sizeof(buffer)))
                                mat->Name = buffer;

                            std::string albedoId = "Albedo##modelmat" + std::to_string(i);
                            ImGui::ColorEdit3(albedoId.c_str(), glm::value_ptr(mat->Albedo));

                            std::string metallicId = "Metallic##modelmat" + std::to_string(i);
                            ImGui::SliderFloat(metallicId.c_str(), &mat->Metallic, 0.0f, 1.0f);

                            std::string roughnessId = "Roughness##modelmat" + std::to_string(i);
                            ImGui::SliderFloat(roughnessId.c_str(), &mat->Roughness, 0.0f, 1.0f);

                            std::string aoId = "AO##modelmat" + std::to_string(i);
                            ImGui::SliderFloat(aoId.c_str(), &mat->AO, 0.0f, 1.0f);

                            std::string emColorId = "Emission Color##modelmat" + std::to_string(i);
                            ImGui::ColorEdit3(emColorId.c_str(), glm::value_ptr(mat->EmissionColor));

                            std::string emStrId = "Emission Strength##modelmat" + std::to_string(i);
                            ImGui::DragFloat(emStrId.c_str(), &mat->EmissionStrength, 0.1f, 0.0f, 100.0f);

                            ImGui::TreePop();
                        }
                    }
                }

                if (ImGui::Button("Add Material"))
                {
                    component.ModelData->Materials.push_back(Material::CreateDefault());
                }
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
                // Controller file path
                ImGui::Text("Controller:");
                ImGui::SameLine();
                char ctrlPathBuf[512];
                memset(ctrlPathBuf, 0, sizeof(ctrlPathBuf));
                strncpy(ctrlPathBuf, component.ControllerFilePath.c_str(), sizeof(ctrlPathBuf) - 1);
                if (ImGui::InputText("##ctrlpath", ctrlPathBuf, sizeof(ctrlPathBuf)))
                {
                    std::string newPath = ctrlPathBuf;
                    if (newPath != component.ControllerFilePath)
                    {
                        component.ControllerFilePath = newPath;
                        if (!newPath.empty())
                            component.Controller = AnimationController::Deserialize(newPath);
                    }
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("..."))
                {
                    auto projectDir = Project::GetActiveProjectDirectory();
                    if (!projectDir.empty())
                    {
                        nfdchar_t* outPath = nullptr;
                        nfdfilteritem_t filterItem[] = { { "Animation Controller", "cqcont" } };
                        nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, projectDir.string().c_str());
                        if (result == NFD_OKAY)
                        {
                            component.ControllerFilePath = outPath;
                            component.Controller = AnimationController::Deserialize(outPath);
                            NFD_FreePath(outPath);
                        }
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

        DrawComponent<ImageComponent>("Image", entity, [](auto& component)
        {
            ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

            // Texture path input
            ImGui::Text("Texture:");
            ImGui::SameLine();
            
            char imgPathBuffer[256];
            memset(imgPathBuffer, 0, sizeof(imgPathBuffer));
            if (component.Texture)
            {
                std::string texPath = component.Texture->GetPath();
                auto projectDir = Project::GetActiveProjectDirectory();
                if (!projectDir.empty())
                {
                    std::error_code ec;
                    auto relative = std::filesystem::relative(texPath, projectDir, ec);
                    if (!ec && !relative.empty() && relative.native()[0] != '.')
                        texPath = relative.string();
                }
                strncpy(imgPathBuffer, texPath.c_str(), sizeof(imgPathBuffer) - 1);
            }
            
            if (ImGui::InputText("##ImagePath", imgPathBuffer, sizeof(imgPathBuffer)))
            {
                std::string inputPath = std::string(imgPathBuffer);
                if (std::filesystem::path(inputPath).is_relative())
                {
                    auto projectDir = Project::GetActiveProjectDirectory();
                    if (!projectDir.empty())
                        inputPath = (projectDir / inputPath).string();
                }
                if (!inputPath.empty() && std::filesystem::exists(inputPath))
                    component.Texture = Texture2D::Create(inputPath);
            }

            if (ImGui::Button("Browse..."))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Images", "png,jpg,jpeg,bmp,tga" } };
                nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
                
                if (result == NFD_OKAY)
                {
                    component.Texture = Texture2D::Create(outPath);
                    NFD_FreePathU8(outPath);
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
                    
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
                    {
                        component.Texture = Texture2D::Create(droppedPath);
                        CQ_CORE_INFO("Image texture set: {0}", droppedPath);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.1f, 10.0f);
        });

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
        });

        DrawComponent<NavMeshAgentComponent>("NavMesh Agent", entity, [](auto& component)
        {
            ImGui::DragFloat("Speed", &component.Speed, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Angular Speed", &component.AngularSpeed, 1.0f, 0.0f, 720.0f);
            ImGui::DragFloat("Acceleration", &component.Acceleration, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Stopping Dist", &component.StoppingDistance, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Radius", &component.Radius, 0.1f, 0.01f, 100.0f);
            ImGui::DragFloat("Height", &component.Height, 0.1f, 0.01f, 100.0f);
            ImGui::Checkbox("Auto Braking", &component.AutoBraking);
        });

        DrawComponent<NavMeshObstacleComponent>("NavMesh Obstacle", entity, [](auto& component)
        {
            ImGui::Checkbox("Carve", &component.Carve);
            ImGui::DragFloat("Radius", &component.Radius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat3("Size", glm::value_ptr(component.Size), 0.1f);
            ImGui::DragFloat3("Center", glm::value_ptr(component.Center), 0.1f);
        });

        DrawComponent<NavMeshSurfaceComponent>("NavMesh Surface", entity, [](auto& component)
        {
            const char* areaTypes[] = { "Walkable", "Not Walkable", "Jump" };
            ImGui::Combo("Area Type", &component.AreaType, areaTypes, IM_ARRAYSIZE(areaTypes));
            
            ImGui::Checkbox("Override Voxel Size", &component.OverrideVoxelSize);
            if (component.OverrideVoxelSize)
            {
                ImGui::DragFloat("Voxel Size", &component.VoxelSize, 0.01f, 0.01f, 2.0f);
            }
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
            
            ImGui::DragFloat("Scale Factor", &component.ScaleFactor, 0.01f, 0.1f, 10.0f);
            ImGui::DragFloat("Reference Pixels Per Unit", &component.ReferencePixelsPerUnit, 1.0f, 1.0f, 1000.0f);
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

        DrawComponent<DirectionalLightComponent>("Directional Light", entity, [](auto& component)
        {
            DrawVec3Control("Direction", component.Direction);
            ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
            ImGui::DragFloat("Intensity", &component.Intensity, 0.1f, 0.0f, 10.0f);
            ImGui::Checkbox("Cast Shadows", &component.CastShadows);
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
        });

        DrawComponent<SpotLightComponent>("Spot Light", entity, [](auto& component)
        {
            DrawVec3Control("Direction", component.Direction);
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
        });

        // 2D Physics Components
        DrawComponent<RigidBody2DComponent>("RigidBody 2D", entity, [](auto& component)
        {
            const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
            const char* currentBodyTypeString = bodyTypeStrings[(int)component.Type];
            if (ImGui::BeginCombo("Body Type", currentBodyTypeString))
            {
                for (int i = 0; i < 3; i++)
                {
                    bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
                    {
                        component.Type = (RigidBody2DComponent::BodyType)i;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            
            ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
        });

        DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
        {
            ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset), 0.01f);
            ImGui::DragFloat2("Size", glm::value_ptr(component.Size), 0.01f);
            ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
        });

        DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
        {
            ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset), 0.01f);
            ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
        });

        // 3D Physics Components
        DrawComponent<RigidbodyComponent>("Rigidbody", entity, [](auto& component)
        {
            const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
            const char* currentBodyTypeString = bodyTypeStrings[(int)component.Type];
            if (ImGui::BeginCombo("Body Type", currentBodyTypeString))
            {
                for (int i = 0; i < 3; i++)
                {
                    bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
                    {
                        component.Type = (RigidbodyComponent::BodyType)i;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            
            ImGui::DragFloat("Mass", &component.Mass, 0.01f, 0.01f, 1000.0f);
            ImGui::DragFloat("Linear Drag", &component.LinearDrag, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Angular Drag", &component.AngularDrag, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Gravity Scale", &component.GravityScale, 0.01f, -10.0f, 10.0f);
            ImGui::Checkbox("Freeze Rotation", &component.FreezeRotation);
            
            ImGui::Separator();
            ImGui::Text("Material");
            ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
        });

        DrawComponent<BoxColliderComponent>("Box Collider", entity, [](auto& component)
        {
            ImGui::DragFloat3("Offset", glm::value_ptr(component.Offset), 0.01f);
            ImGui::DragFloat3("Size", glm::value_ptr(component.Size), 0.01f);
            ImGui::Checkbox("Is Trigger", &component.IsTrigger);
        });

        DrawComponent<SphereColliderComponent>("Sphere Collider", entity, [](auto& component)
        {
            ImGui::DragFloat3("Offset", glm::value_ptr(component.Offset), 0.01f);
            ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("Is Trigger", &component.IsTrigger);
        });

        DrawComponent<CapsuleColliderComponent>("Capsule Collider", entity, [](auto& component)
        {
            ImGui::DragFloat3("Offset", glm::value_ptr(component.Offset), 0.01f);
            ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Height", &component.Height, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("Is Trigger", &component.IsTrigger);
        });

        DrawComponent<MeshColliderComponent>("Mesh Collider", entity, [](auto& component)
        {
            ImGui::Checkbox("Is Convex", &component.IsConvex);
            ImGui::Checkbox("Is Trigger", &component.IsTrigger);
        });

        // Audio Components
        DrawComponent<AudioSourceComponent>("Audio Source", entity, [](auto& component)
        {
            // Dosya yolu
            ImGui::Text("Audio File:");
            ImGui::SameLine();
            
            char pathBuffer[256];
            memset(pathBuffer, 0, sizeof(pathBuffer));
            strncpy(pathBuffer, component.FilePath.c_str(), sizeof(pathBuffer) - 1);
            
            if (ImGui::InputText("##AudioFilePath", pathBuffer, sizeof(pathBuffer)))
            {
                component.FilePath = ToSerializablePath(std::string(pathBuffer));
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Audio Files", "wav,mp3,ogg,flac" } };
                
                nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
                
                if (result == NFD_OKAY)
                {
                    component.FilePath = ToSerializablePath(std::string(outPath));
                    NFD_FreePath(outPath);
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
                    
                    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                    {
                        component.FilePath = ToSerializablePath(droppedPath);
                        CQ_CORE_INFO("Audio loaded: {0}", droppedPath);
                    }
                }
                ImGui::EndDragDropTarget();
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
                            
                            if (ext == ".dll" || ext == ".so")
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
                    
                    ImGui::Separator();
                    
                    if (script.Instance)
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Script Active");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Script Not Loaded");
                    
                    ImGui::Text("Note: DLL-based scripting system");
                    ImGui::TextWrapped("Load a compiled script DLL to attach behavior to this entity.");
                    
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
        {
            auto& csc = entity.GetComponent<ConquerorScriptComponent>();
            int indexToRemove = -1;
            
            for (int i = 0; i < csc.Scripts.size(); i++)
            {
                auto& script = csc.Scripts[i];
                std::string cqsName = "Conqueror Script";
                if (!script.ScriptPath.empty())
                {
                    std::filesystem::path path(script.ScriptPath);
                    cqsName = path.stem().string() + " (Conqueror Script)";
                }
                    
                const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
                float lineHeight = ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f;
                ImGui::Separator();
                
                ImGui::PushID((void*)&script);
                bool open = ImGui::TreeNodeEx((void*)&script, treeNodeFlags, cqsName.c_str());
                ImGui::PopStyleVar();
                
                ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
                
                std::string popupName = "ComponentSettingsCQS" + std::to_string(i);
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
                    ImGui::Text("Script Path:");
                    ImGui::SameLine();
                    
                    char pathBuffer[256];
                    memset(pathBuffer, 0, sizeof(pathBuffer));
                    strncpy(pathBuffer, script.ScriptPath.c_str(), sizeof(pathBuffer) - 1);
                    
                    if (ImGui::InputText("##CQSPath", pathBuffer, sizeof(pathBuffer)))
                    {
                        script.ScriptPath = ToSerializablePath(std::string(pathBuffer));
                        std::string resolvePath = std::filesystem::path(pathBuffer).is_relative() ?
                            (Project::GetActiveProjectDirectory() / pathBuffer).string() : std::string(pathBuffer);
                        std::ifstream file(resolvePath);
                        std::string line;
                        while (std::getline(file, line)) {
                            size_t pos = line.find("script ");
                            if (pos != std::string::npos) {
                                size_t start = pos + 7;
                                while (start < line.length() && isspace(line[start])) start++;
                                size_t end = start;
                                while (end < line.length() && (isalnum(line[end]) || line[end] == '_')) end++;
                                if (end > start) {
                                    script.ClassName = line.substr(start, end - start);
                                    break;
                                }
                            }
                        }
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button("Browse..."))
                    {
                        nfdchar_t* outPath = nullptr;
                        nfdfilteritem_t filters[1] = { { "ConquerorScript", "cqs" } };
                        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
                        
                        if (result == NFD_OKAY)
                        {
                            script.ScriptPath = ToSerializablePath(std::string(outPath));
                            
                            std::ifstream file(outPath);
                            std::string line;
                            while (std::getline(file, line)) {
                                size_t pos = line.find("script ");
                                if (pos != std::string::npos) {
                                    size_t start = pos + 7;
                                    while (start < line.length() && isspace(line[start])) start++;
                                    size_t end = start;
                                    while (end < line.length() && (isalnum(line[end]) || line[end] == '_')) end++;
                                    if (end > start) {
                                        script.ClassName = line.substr(start, end - start);
                                        break;
                                    }
                                }
                            }
                            NFD_FreePath(outPath);
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
                            
                            if (ext == ".cqs")
                            {
                                script.ScriptPath = ToSerializablePath(droppedPath);
                                
                                std::ifstream file(droppedPath);
                                std::string line;
                                while (std::getline(file, line)) {
                                    size_t pos = line.find("script ");
                                    if (pos != std::string::npos) {
                                        size_t start = pos + 7;
                                        while (start < line.length() && isspace(line[start])) start++;
                                        size_t end = start;
                                        while (end < line.length() && (isalnum(line[end]) || line[end] == '_')) end++;
                                        if (end > start) {
                                            script.ClassName = line.substr(start, end - start);
                                            break;
                                        }
                                    }
                                }
                                CQ_CORE_INFO("ConquerorScript loaded: {0}", droppedPath);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (script.Instance)
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Instance Running");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Not Initialized");
                    
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
