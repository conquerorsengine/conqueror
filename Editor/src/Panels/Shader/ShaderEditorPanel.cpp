#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Panels/MaterialEditorUI.h"
#include "Core/Project/Project.h"
#include "ShaderEditorPanel.h"

#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/Texture.h"

#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/ShaderCompiler/ShaderCompiler.h"
#include "Scene/Scene.h"
#include "Core/Time/TimeManager.h"
#include "UI/EditorUI.h"
#include "Core/ImGui/ImGuiLayer.h"

#include <imgui.h>
#include <imnodes.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

#include <algorithm>
#include <string>

namespace Conqueror::Editor
{
    namespace
    {
        int GetInputAttributeID(int nodeID, int pinIndex) { return nodeID * 100 + pinIndex * 10 + 1; }
        int GetOutputAttributeID(int nodeID, int pinIndex) { return nodeID * 100 + pinIndex * 10 + 2; }
        
        bool IsInputAttribute(int attributeID) { return attributeID % 10 == 1; }
        bool IsOutputAttribute(int attributeID) { return attributeID % 10 == 2; }

        int GetNodeIDFromAttribute(int attributeID) { return attributeID / 100; }
        int GetPinIndexFromAttribute(int attributeID) { return (attributeID % 100) / 10; }

        const char* ShaderNodeTypeToString(ShaderGraphNodeType type)
        {
            switch (type)
            {
                case ShaderGraphNodeType::MasterOutput: return "Master Output";
                case ShaderGraphNodeType::Color:        return "Color";
                case ShaderGraphNodeType::Float:        return "Float";
                case ShaderGraphNodeType::Vec2:         return "Vector 2";
                case ShaderGraphNodeType::Vec3:         return "Vector 3";
                case ShaderGraphNodeType::TextureSample:return "Texture Sample";
                case ShaderGraphNodeType::Add:          return "Add";
                case ShaderGraphNodeType::Subtract:     return "Subtract";
                case ShaderGraphNodeType::Multiply:     return "Multiply";
                case ShaderGraphNodeType::Divide:       return "Divide";
                case ShaderGraphNodeType::Power:        return "Power";
                case ShaderGraphNodeType::Sqrt:         return "Square Root";
                case ShaderGraphNodeType::Abs:          return "Absolute";
                case ShaderGraphNodeType::Sign:         return "Sign";
                case ShaderGraphNodeType::Ceil:         return "Ceiling";
                case ShaderGraphNodeType::Floor:        return "Floor";
                case ShaderGraphNodeType::Round:        return "Round";
                case ShaderGraphNodeType::Mod:          return "Modulo";
                case ShaderGraphNodeType::Min:          return "Minimum";
                case ShaderGraphNodeType::Max:          return "Maximum";
                case ShaderGraphNodeType::Clamp:        return "Clamp";
                case ShaderGraphNodeType::Saturate:     return "Saturate";
                case ShaderGraphNodeType::Mix:          return "Lerp / Mix";
                case ShaderGraphNodeType::Step:         return "Step";
                case ShaderGraphNodeType::Smoothstep:   return "Smoothstep";
                case ShaderGraphNodeType::OneMinus:     return "One Minus";
                case ShaderGraphNodeType::Sin:          return "Sine";
                case ShaderGraphNodeType::Cos:          return "Cosine";
                case ShaderGraphNodeType::Tan:          return "Tangent";
                case ShaderGraphNodeType::Asin:         return "Arc Sine";
                case ShaderGraphNodeType::Acos:         return "Arc Cosine";
                case ShaderGraphNodeType::Atan:         return "Arc Tangent";
                case ShaderGraphNodeType::Dot:          return "Dot Product";
                case ShaderGraphNodeType::Cross:        return "Cross Product";
                case ShaderGraphNodeType::Normalize:    return "Normalize";
                case ShaderGraphNodeType::Length:       return "Length";
                case ShaderGraphNodeType::Distance:     return "Distance";
                case ShaderGraphNodeType::Reflect:      return "Reflect";
                case ShaderGraphNodeType::Refract:      return "Refract";
                case ShaderGraphNodeType::UV:           return "UV Coordinates";
                case ShaderGraphNodeType::WorldPosition:return "World Position";
                case ShaderGraphNodeType::ViewDirection:return "View Direction";
                case ShaderGraphNodeType::NormalVector: return "Normal Vector";
                case ShaderGraphNodeType::Time:         return "Time";
                case ShaderGraphNodeType::Fresnel:      return "Fresnel";
                case ShaderGraphNodeType::NormalMap:    return "Normal Map";
                case ShaderGraphNodeType::Panner:       return "Panner";
                case ShaderGraphNodeType::Noise:        return "Noise";
                case ShaderGraphNodeType::TextureObject:return "Texture Object";
                case ShaderGraphNodeType::Fract:       return "Fract";
                case ShaderGraphNodeType::Reciprocal:  return "Reciprocal";
                case ShaderGraphNodeType::Square:      return "Square";
                case ShaderGraphNodeType::Log:         return "Log";
                case ShaderGraphNodeType::Exp:         return "Exp";
                case ShaderGraphNodeType::ArcTan2:     return "ArcTan2";
                case ShaderGraphNodeType::If:          return "If";
                case ShaderGraphNodeType::Append:      return "Append";
                case ShaderGraphNodeType::ComponentMask:return "Component Mask";
                case ShaderGraphNodeType::TextureCoordinate: return "Texture Coordinates";
                case ShaderGraphNodeType::LightVector: return "Light Vector";
                case ShaderGraphNodeType::VertexColor: return "Vertex Color";
                case ShaderGraphNodeType::Desaturation:return "Desaturation";
                case ShaderGraphNodeType::DepthFade:   return "Depth Fade";
                case ShaderGraphNodeType::SphereMask:  return "Sphere Mask";
                case ShaderGraphNodeType::BoxMask:     return "Box Mask";
                case ShaderGraphNodeType::CameraDepthFade: return "Camera Depth Fade";
                case ShaderGraphNodeType::ObjectPosition:  return "Object Position";
                case ShaderGraphNodeType::ObjectBounds:    return "Object Bounds";
                case ShaderGraphNodeType::CustomExpression: return "Custom Expression";
                default: return "Unknown";
            }
        }

        glm::vec2 DefaultPositionForType(ShaderGraphNodeType type)
        {
            if (type == ShaderGraphNodeType::MasterOutput) return { 800.0f, 300.0f };
            return { 400.0f, 300.0f };
        }

        ShaderGraphNode* FindNodeByID(std::vector<ShaderGraphNode>& nodes, int nodeID)
        {
            for (auto& node : nodes)
            {
                if (node.ID == nodeID)
                    return &node;
            }
            return nullptr;
        }

        void EnsureMasterNode(Material* material)
        {
            if (!material) return;
            
            bool hasMaster = false;
            for (const auto& node : material->GraphNodes)
            {
                if (node.Type == ShaderGraphNodeType::MasterOutput)
                {
                    hasMaster = true;
                    break;
                }
            }

            if (!hasMaster)
            {
                material->GraphNodes.emplace_back(material->NextGraphNodeID++, ShaderGraphNodeType::MasterOutput, DefaultPositionForType(ShaderGraphNodeType::MasterOutput));
            }
        }

        bool WouldCreateCycle(const std::vector<ShaderGraphLink>& links, int fromNodeID, int toNodeID)
        {
            std::vector<int> stack = { toNodeID };
            std::vector<int> visited;

            while (!stack.empty())
            {
                const int current = stack.back();
                stack.pop_back();

                if (current == fromNodeID)
                    return true;

                if (std::find(visited.begin(), visited.end(), current) != visited.end())
                    continue;
                visited.push_back(current);

                for (const auto& link : links)
                {
                    if (link.FromNodeID == current)
                        stack.push_back(link.ToNodeID);
                }
            }
            return false;
        }
    }

    ShaderEditorPanel::ShaderEditorPanel()
    {
        // Panel kendi ImNodes bağlamına sahiptir (diğer düğüm editörleriyle karışmaz)
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        m_NodesContext = ImNodes::CreateContext();
        ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(15, 15, 18, 200);
        if (previous)
            ImNodes::SetCurrentContext(previous);
    }

    ShaderEditorPanel::~ShaderEditorPanel()
    {
        if (m_NodesContext)
            ImNodes::DestroyContext(m_NodesContext);
        m_NodesContext = nullptr;
    }

    void ShaderEditorPanel::SetContext(Scene* scene)
    {
        m_Context = scene;
    }

    void ShaderEditorPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectedEntity = entity;
        m_SelectedMaterialIndex = 0;
    }

    void ShaderEditorPanel::SetMaterialAsset(const std::string& path)
    {
        if (MaterialAsset::IsReadOnly(path))
        {
            CQ_CORE_WARN("Shader Graph: built-in package materials are read-only; make a copy with New first");
            return;
        }
        m_MaterialAssetPath = path;
        m_AssetMaterial = path.empty() ? nullptr : MaterialAsset::Load(path);
        if (!path.empty() && !m_AssetMaterial)
            CQ_CORE_WARN("Shader Graph: material '{0}' could not be opened", path);
    }

    void ShaderEditorPanel::OnImGuiRender()
    {
        ImNodes::SetCurrentContext(m_NodesContext);
        ImGui::Begin("Shader Graph Editor");

        // Varlık modu (Unity: Shader Graph bir dosyayı düzenler): seçimden bağımsız
        Material* material = nullptr;
        if (m_AssetMaterial)
        {
            material = m_AssetMaterial.get();
            ImGui::TextDisabled(ICON_FA_FILE " %s", std::filesystem::path(m_MaterialAssetPath).filename().string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK " Close Asset"))
            {
                m_AssetMaterial = nullptr;
                m_MaterialAssetPath.clear();
                ImGui::End();
                return;
            }
            goto haveMaterial;
        }

        if (!m_Context)
        {
            ImGui::TextUnformatted("Scene context not found.");
            ImGui::End();
            return;
        }

        if (!m_SelectedEntity || (!m_SelectedEntity.HasComponent<MeshRendererComponent>() && !m_SelectedEntity.HasComponent<SpriteRendererComponent>() && !m_SelectedEntity.HasComponent<ModelComponent>()))
        {
            ImGui::TextUnformatted("Select an entity with a MeshRenderer, SpriteRenderer, or Model to edit its Material.");
            ImGui::End();
            return;
        }

        // ModelComponent için material seçimi
        if (m_SelectedEntity.HasComponent<ModelComponent>())
        {
            auto& modelComp = m_SelectedEntity.GetComponent<ModelComponent>();
            if (!modelComp.ModelData || modelComp.ModelData->Materials.empty())
            {
                ImGui::TextUnformatted("Model has no materials.");
                if (ImGui::Button("Create Default Material"))
                {
                    if (modelComp.ModelData)
                    {
                        modelComp.ModelData->Materials.push_back(Material::CreateDefault());
                        EnsureMasterNode(modelComp.ModelData->Materials.back().get());
                        modelComp.ModelData->Materials.back()->CompileShaderGraph();
                        modelComp.ModelData->Materials.back()->ApplyGraphToProperties();
                    }
                }
                ImGui::End();
                return;
            }

            // Birden fazla material varsa radio button'lar ile seçim
            if (modelComp.ModelData->Materials.size() > 1)
            {
                ImGui::Text("Materials (%zu):", modelComp.ModelData->Materials.size());
                ImGui::Separator();

                for (size_t i = 0; i < modelComp.ModelData->Materials.size(); i++)
                {
                    auto& mat = modelComp.ModelData->Materials[i];
                    std::string label = "##mat" + std::to_string(i);
                    std::string nameLabel = mat ? mat->Name : ("Material " + std::to_string(i));

                    bool isSelected = (m_SelectedMaterialIndex == (int)i);
                    if (ImGui::RadioButton((nameLabel + label).c_str(), isSelected))
                    {
                        m_SelectedMaterialIndex = (int)i;
                    }
                    ImGui::SameLine();
                }
                ImGui::Separator();
            }
            else
            {
                m_SelectedMaterialIndex = 0;
            }

            if (m_SelectedMaterialIndex >= 0 && m_SelectedMaterialIndex < (int)modelComp.ModelData->Materials.size())
                material = modelComp.ModelData->Materials[m_SelectedMaterialIndex].get();
        }
        else if (m_SelectedEntity.HasComponent<MeshRendererComponent>())
        {
            auto& meshRenderer = m_SelectedEntity.GetComponent<MeshRendererComponent>();
            if (!meshRenderer.MaterialInstance)
            {
                ImGui::TextUnformatted("This Mesh Renderer uses the built-in material.");
                if (ImGui::Button(ICON_FA_PLUS " Create Material Asset"))
                {
                    // Gerçek .cqmat dosyası (Assets/Materials), mevcut görünüm kopyalanır
                    std::string name = m_SelectedEntity.HasComponent<TagComponent>() ? m_SelectedEntity.GetComponent<TagComponent>().Tag : "New Material";
                    std::string dir = Project::GetActive() ? (Project::GetAssetDirectory() / "Materials").string() : std::string("Materials");
                    std::string path = Editor::MaterialEditorUI::UniqueMaterialPath(dir, name);
                    Material from = MeshRendererMaterial::Shared(meshRenderer);
                    EnsureMasterNode(&from);
                    from.GraphInitialized = true;
                    if (auto mat = MaterialAsset::Create(path, &from))
                    {
                        meshRenderer.MaterialInstance = mat;
                        meshRenderer.MaterialPath = path;
                        mat->CompileShaderGraph();
                        mat->ApplyGraphToProperties();
                        MaterialAsset::Save(mat, path);
                    }
                }
                ImGui::End();
                return;
            }
            material = meshRenderer.MaterialInstance.get();
        }
        else
        {
            ImGui::TextUnformatted("Entity does not have a MeshRendererComponent.");
            ImGui::End();
            return;
        }

    haveMaterial:
        if (!material)
        {
            ImGui::TextUnformatted("No material selected.");
            ImGui::End();
            return;
        }

        if (!material->GraphInitialized)
        {
            EnsureMasterNode(material);
            material->GraphInitialized = true;
        }

        // ---- Araç çubuğu
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_DIAGRAM_PROJECT);
        ImGui::SameLine();
        ImGui::Text("%s", material->Name.c_str());
        ImGui::SameLine(0, 16);
        if (UI::AccentButton(ICON_FA_HAMMER " Compile"))
            Recompile(material);
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &m_AutoCompile);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Add Node"))
        {
            m_SearchSpawnPos = ImGui::GetMousePos();
            ImGui::OpenPopup("##NodeSearch");
        }
        ImGui::SameLine(0, 16);
        bool ok = material->MaterialShader && material->MaterialShader->IsValid();
        if (!m_CompileStatus.empty())
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ok ? UI::Colors::Success : UI::Colors::Error, "%s %s", ok ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE_XMARK, m_CompileStatus.c_str());
        }
        DrawNodeSearch(material);

        // Otomatik derleme: graf değişti ve 0.35 sn boşta
        {
            uint64_t h = HashGraph(material);
            if (h != m_LastGraphHash)
            {
                m_LastGraphHash = h;
                m_DirtyTimer = 0.0f;
            }
            if (m_DirtyTimer >= 0.0f)
            {
                m_DirtyTimer += ImGui::GetIO().DeltaTime;
                if (m_AutoCompile && m_DirtyTimer > 0.35f && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    Recompile(material);
                    m_DirtyTimer = -1.0f;
                }
            }
        }

        auto addNodeButton = [&](const char* label, ShaderGraphNodeType type)
        {
            if (type == ShaderGraphNodeType::MasterOutput) return;
            if (ImGui::MenuItem(label))
            {
                material->GraphNodes.emplace_back(material->NextGraphNodeID++, type, DefaultPositionForType(type));
                ImGui::CloseCurrentPopup();
            }
        };

        // Düğüm ekleme menüsü yalnız graph alanında (önceki karenin üzerinde-olma bilgisi)
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            ImGui::IsMouseHoveringRect(m_GraphRectMin, m_GraphRectMax, false))
        {
            int hoveredNodeID = 0;
            if (ImNodes::IsNodeHovered(&hoveredNodeID))
            {
                ImNodes::ClearNodeSelection();
                ImNodes::SelectNode(hoveredNodeID);
            }
            else
            {
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
                ImGui::OpenPopup("ShaderGraphContextMenu");
            }
        }

        if (ImGui::BeginPopup("ShaderGraphContextMenu"))
        {
            if (ImGui::BeginMenu("Add Node"))
            {
                if (ImGui::BeginMenu("Data"))
                {
                    addNodeButton("Color", ShaderGraphNodeType::Color);
                    addNodeButton("Float", ShaderGraphNodeType::Float);
                    addNodeButton("Vec2", ShaderGraphNodeType::Vec2);
                    addNodeButton("Vec3", ShaderGraphNodeType::Vec3);
                    addNodeButton("Texture", ShaderGraphNodeType::TextureSample);
                    addNodeButton("TextureObj", ShaderGraphNodeType::TextureObject);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Math"))
                {
                    addNodeButton("Add", ShaderGraphNodeType::Add);
                    addNodeButton("Subtract", ShaderGraphNodeType::Subtract);
                    addNodeButton("Multiply", ShaderGraphNodeType::Multiply);
                    addNodeButton("Divide", ShaderGraphNodeType::Divide);
                    addNodeButton("Power", ShaderGraphNodeType::Power);
                    addNodeButton("Sqrt", ShaderGraphNodeType::Sqrt);
                    addNodeButton("Abs", ShaderGraphNodeType::Abs);
                    addNodeButton("Sign", ShaderGraphNodeType::Sign);
                    addNodeButton("Ceil", ShaderGraphNodeType::Ceil);
                    addNodeButton("Floor", ShaderGraphNodeType::Floor);
                    addNodeButton("Round", ShaderGraphNodeType::Round);
                    addNodeButton("Fract", ShaderGraphNodeType::Fract);
                    addNodeButton("Mod", ShaderGraphNodeType::Mod);
                    addNodeButton("Min", ShaderGraphNodeType::Min);
                    addNodeButton("Max", ShaderGraphNodeType::Max);
                    addNodeButton("Reciprocal", ShaderGraphNodeType::Reciprocal);
                    addNodeButton("Square", ShaderGraphNodeType::Square);
                    addNodeButton("Log", ShaderGraphNodeType::Log);
                    addNodeButton("Exp", ShaderGraphNodeType::Exp);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Trig"))
                {
                    addNodeButton("Sin", ShaderGraphNodeType::Sin);
                    addNodeButton("Cos", ShaderGraphNodeType::Cos);
                    addNodeButton("Tan", ShaderGraphNodeType::Tan);
                    addNodeButton("Asin", ShaderGraphNodeType::Asin);
                    addNodeButton("Acos", ShaderGraphNodeType::Acos);
                    addNodeButton("Atan", ShaderGraphNodeType::Atan);
                    addNodeButton("ArcTan2", ShaderGraphNodeType::ArcTan2);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Interpolation"))
                {
                    addNodeButton("Mix / Lerp", ShaderGraphNodeType::Mix);
                    addNodeButton("Clamp", ShaderGraphNodeType::Clamp);
                    addNodeButton("Saturate", ShaderGraphNodeType::Saturate);
                    addNodeButton("Step", ShaderGraphNodeType::Step);
                    addNodeButton("Smoothstep", ShaderGraphNodeType::Smoothstep);
                    addNodeButton("OneMinus", ShaderGraphNodeType::OneMinus);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Vector"))
                {
                    addNodeButton("Dot", ShaderGraphNodeType::Dot);
                    addNodeButton("Cross", ShaderGraphNodeType::Cross);
                    addNodeButton("Normalize", ShaderGraphNodeType::Normalize);
                    addNodeButton("Length", ShaderGraphNodeType::Length);
                    addNodeButton("Distance", ShaderGraphNodeType::Distance);
                    addNodeButton("Reflect", ShaderGraphNodeType::Reflect);
                    addNodeButton("Refract", ShaderGraphNodeType::Refract);
                    addNodeButton("Append", ShaderGraphNodeType::Append);
                    addNodeButton("CompMask", ShaderGraphNodeType::ComponentMask);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Coordinates"))
                {
                    addNodeButton("UV", ShaderGraphNodeType::UV);
                    addNodeButton("TexCoords", ShaderGraphNodeType::TextureCoordinate);
                    addNodeButton("WorldPos", ShaderGraphNodeType::WorldPosition);
                    addNodeButton("ViewDir", ShaderGraphNodeType::ViewDirection);
                    addNodeButton("NormalVec", ShaderGraphNodeType::NormalVector);
                    addNodeButton("LightVec", ShaderGraphNodeType::LightVector);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Special"))
                {
                    addNodeButton("Time", ShaderGraphNodeType::Time);
                    addNodeButton("Panner", ShaderGraphNodeType::Panner);
                    addNodeButton("Noise", ShaderGraphNodeType::Noise);
                    addNodeButton("Fresnel", ShaderGraphNodeType::Fresnel);
                    addNodeButton("NormalMap", ShaderGraphNodeType::NormalMap);
                    addNodeButton("VertexColor", ShaderGraphNodeType::VertexColor);
                    addNodeButton("Desaturate", ShaderGraphNodeType::Desaturation);
                    addNodeButton("DepthFade", ShaderGraphNodeType::DepthFade);
                    addNodeButton("SphereMask", ShaderGraphNodeType::SphereMask);
                    addNodeButton("BoxMask", ShaderGraphNodeType::BoxMask);
                    addNodeButton("CamDepthFade", ShaderGraphNodeType::CameraDepthFade);
                    addNodeButton("ObjPosition", ShaderGraphNodeType::ObjectPosition);
                    addNodeButton("ObjBounds", ShaderGraphNodeType::ObjectBounds);
                    addNodeButton("CustomExpr", ShaderGraphNodeType::CustomExpression);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Logic"))
                {
                    addNodeButton("If", ShaderGraphNodeType::If);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        ImGui::BeginChild("##GraphArea", ImVec2(std::max(200.0f, ImGui::GetContentRegionAvail().x - m_SideWidth - 6.0f), 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        m_GraphOrigin = ImGui::GetCursorScreenPos();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            m_SearchSpawnPos = ImGui::GetMousePos();
            ImGui::OpenPopup("##NodeSearch");
        }
        DrawNodeSearch(material);

        ImNodes::BeginNodeEditor();

        // Malzeme değişince düğümleri kadraja al
        if (material != m_LastMaterial && !material->GraphNodes.empty())
        {
            m_LastMaterial = material;
            glm::vec2 mn(1e9f), mx(-1e9f);
            for (const auto& n : material->GraphNodes)
            {
                mn = glm::min(mn, n.EditorPosition);
                mx = glm::max(mx, n.EditorPosition + glm::vec2(160.0f, 120.0f));
            }
            ImVec2 area = ImGui::GetContentRegionAvail();
            glm::vec2 center = (mn + mx) * 0.5f;
            ImNodes::EditorContextResetPanning(ImVec2(area.x * 0.5f - center.x, area.y * 0.5f - center.y));
        }

        for (auto& node : material->GraphNodes)
        {
            // Stil ayarlamaları
            if (node.Type == ShaderGraphNodeType::MasterOutput)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(120, 30, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(150, 40, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(180, 50, 50, 255));
            }
            else if (node.Type == ShaderGraphNodeType::TextureSample)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 120, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 150, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 180, 50, 255));
            }
            else
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 30, 120, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 40, 150, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 50, 180, 255));
            }

            ImNodes::BeginNode(node.ID);

            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(ShaderNodeTypeToString(node.Type));
            ImNodes::EndNodeTitleBar();

            switch (node.Type)
            {
                case ShaderGraphNodeType::MasterOutput:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("Albedo");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("Normal");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 2));
                    ImGui::TextUnformatted("Metallic");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 3));
                    ImGui::TextUnformatted("Roughness");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 4));
                    ImGui::TextUnformatted("AO");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 5));
                    ImGui::TextUnformatted("Emission");
                    ImNodes::EndInputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Color:
                {
                    ImGui::PushItemWidth(120.0f);
                    if (ImGui::ColorEdit4("##color", &node.ColorValue.x, ImGuiColorEditFlags_NoInputs))
                        material->ApplyGraphToProperties();
                    ImGui::PopItemWidth();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("RGBA");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Float:
                {
                    ImGui::PushItemWidth(80.0f);
                    if (ImGui::DragFloat("##float", &node.FloatValue, 0.01f))
                        material->ApplyGraphToProperties();
                    ImGui::PopItemWidth();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::TextureSample:
                {
                    ImGui::PushItemWidth(150.0f);
                    char buffer[256];
                    strncpy(buffer, node.TexturePath.c_str(), sizeof(buffer));
                    if (ImGui::InputText("Path", buffer, sizeof(buffer)))
                    {
                        node.TexturePath = buffer;
                        node.TextureRef = nullptr; // Reset to reload
                        material->ApplyGraphToProperties();
                    }
                    ImGui::PopItemWidth();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("UV");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("RGBA");
                    ImNodes::EndOutputAttribute();
                    
                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 1));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("RGB");
                    ImNodes::EndOutputAttribute();
                    
                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 2));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("A");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Multiply:
                case ShaderGraphNodeType::Add:
                case ShaderGraphNodeType::Subtract:
                case ShaderGraphNodeType::Divide:
                case ShaderGraphNodeType::Power:
                case ShaderGraphNodeType::Dot:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("A");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("B");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Mix:
                case ShaderGraphNodeType::Clamp:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("A");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("B");
                    ImNodes::EndInputAttribute();
                    
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 2));
                    ImGui::TextUnformatted("T/C");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::UV:
                case ShaderGraphNodeType::Time:
                case ShaderGraphNodeType::WorldPosition:
                case ShaderGraphNodeType::ViewDirection:
                case ShaderGraphNodeType::NormalVector:
                {
                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Sin:
                case ShaderGraphNodeType::Cos:
                case ShaderGraphNodeType::Tan:
                case ShaderGraphNodeType::Asin:
                case ShaderGraphNodeType::Acos:
                case ShaderGraphNodeType::Atan:
                case ShaderGraphNodeType::Saturate:
                case ShaderGraphNodeType::OneMinus:
                case ShaderGraphNodeType::Normalize:
                case ShaderGraphNodeType::Abs:
                case ShaderGraphNodeType::Sign:
                case ShaderGraphNodeType::Ceil:
                case ShaderGraphNodeType::Floor:
                case ShaderGraphNodeType::Round:
                case ShaderGraphNodeType::Fract:
                case ShaderGraphNodeType::Reciprocal:
                case ShaderGraphNodeType::Square:
                case ShaderGraphNodeType::Log:
                case ShaderGraphNodeType::Exp:
                case ShaderGraphNodeType::Desaturation:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("In");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("Amount");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::ArcTan2:
                case ShaderGraphNodeType::Mod:
                case ShaderGraphNodeType::Min:
                case ShaderGraphNodeType::Max:
                case ShaderGraphNodeType::Distance:
                case ShaderGraphNodeType::Reflect:
                case ShaderGraphNodeType::Refract:
                case ShaderGraphNodeType::Sqrt:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("A");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("B");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Smoothstep:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("Min");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("Max");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 2));
                    ImGui::TextUnformatted("Value");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::If:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("A");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("B");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 2));
                    ImGui::TextUnformatted("True");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 3));
                    ImGui::TextUnformatted("False");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Append:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("A");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("B");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::ComponentMask:
                {
                    ImGui::PushItemWidth(80.0f);
                    int mask = node.IntValue;
                    bool r = mask & 1, g = mask & 2, b = mask & 4, a = mask & 8;
                    if (ImGui::Checkbox("R", &r)) { if (r) mask |= 1; else mask &= ~1; node.IntValue = mask; }
                    if (ImGui::Checkbox("G", &g)) { if (g) mask |= 2; else mask &= ~2; node.IntValue = mask; }
                    if (ImGui::Checkbox("B", &b)) { if (b) mask |= 4; else mask &= ~4; node.IntValue = mask; }
                    if (ImGui::Checkbox("A", &a)) { if (a) mask |= 8; else mask &= ~8; node.IntValue = mask; }
                    ImGui::PopItemWidth();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("In");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::TextureCoordinate:
                {
                    ImGui::PushItemWidth(100.0f);
                    ImGui::DragFloat2("Tiling", &node.Vec2Value.x, 0.1f);
                    ImGui::DragFloat2("Offset", &node.Vec2Value.y, 0.1f);
                    ImGui::PopItemWidth();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("UV");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::CustomExpression:
                {
                    ImGui::PushItemWidth(150.0f);
                    char buffer[512];
                    strncpy(buffer, node.CustomCode.c_str(), sizeof(buffer));
                    if (ImGui::InputTextMultiline("##code", buffer, sizeof(buffer), ImVec2(150, 60)))
                        node.CustomCode = buffer;
                    ImGui::PopItemWidth();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::VertexColor:
                case ShaderGraphNodeType::ObjectPosition:
                case ShaderGraphNodeType::ObjectBounds:
                case ShaderGraphNodeType::LightVector:
                case ShaderGraphNodeType::TextureObject:
                {
                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                case ShaderGraphNodeType::Panner:
                {
                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 0));
                    ImGui::TextUnformatted("UV");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID, 1));
                    ImGui::TextUnformatted("Speed");
                    ImNodes::EndInputAttribute();

                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
                default:
                {
                    ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID, 0));
                    ImGui::Indent(40.0f);
                    ImGui::TextUnformatted("Out");
                    ImNodes::EndOutputAttribute();
                    break;
                }
            }

            ImNodes::EndNode();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();

            ImNodes::SetNodeGridSpacePos(node.ID, ImVec2(node.EditorPosition.x, node.EditorPosition.y));
        }

        for (const auto& link : material->GraphLinks)
        {
            ImNodes::Link(link.ID, GetOutputAttributeID(link.FromNodeID, link.FromPinIndex), GetInputAttributeID(link.ToNodeID, link.ToPinIndex));
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        m_GraphRectMin = ImGui::GetItemRectMin();   // graph alt penceresi
        m_GraphRectMax = ImGui::GetItemRectMax();

        // Update positions
        for (auto& node : material->GraphNodes)
        {
            const ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.ID);
            node.EditorPosition = { pos.x, pos.y };
        }

        // Link creation
        int startNodeID = 0, startAttr = 0, endNodeID = 0, endAttr = 0;
        if (ImNodes::IsLinkCreated(&startNodeID, &startAttr, &endNodeID, &endAttr))
        {
            int fromAttr = 0;
            int toAttr = 0;

            if (IsOutputAttribute(startAttr) && IsInputAttribute(endAttr))
            {
                fromAttr = startAttr;
                toAttr = endAttr;
            }
            else if (IsInputAttribute(startAttr) && IsOutputAttribute(endAttr))
            {
                fromAttr = endAttr;
                toAttr = startAttr;
            }

            if (fromAttr != 0 && toAttr != 0)
            {
                int fromNode = GetNodeIDFromAttribute(fromAttr);
                int fromPin = GetPinIndexFromAttribute(fromAttr);
                int toNode = GetNodeIDFromAttribute(toAttr);
                int toPin = GetPinIndexFromAttribute(toAttr);

                if (fromNode != toNode && !WouldCreateCycle(material->GraphLinks, fromNode, toNode))
                {
                    // Remove existing link to the same input pin
                    material->GraphLinks.erase(
                        std::remove_if(material->GraphLinks.begin(), material->GraphLinks.end(),
                            [toNode, toPin](const ShaderGraphLink& link)
                            {
                                return link.ToNodeID == toNode && link.ToPinIndex == toPin;
                            }),
                        material->GraphLinks.end());

                    material->GraphLinks.emplace_back(material->NextGraphLinkID++, fromNode, fromPin, toNode, toPin);
                    CQ_CORE_INFO("[ShaderEditor] Link created: Node{0} pin{1} -> Node{2} pin{3}", fromNode, fromPin, toNode, toPin);
                    material->CompileShaderGraph();
                    material->ApplyGraphToProperties();
                }
            }
        }

        // Link destruction
        int destroyedLinkID = 0;
        if (ImNodes::IsLinkDestroyed(&destroyedLinkID))
        {
                material->GraphLinks.erase(
                std::remove_if(material->GraphLinks.begin(), material->GraphLinks.end(),
                    [destroyedLinkID](const ShaderGraphLink& link)
                    {
                        return link.ID == destroyedLinkID;
                    }),
                material->GraphLinks.end());
            CQ_CORE_INFO("[ShaderEditor] Link destroyed, ID: {0}", destroyedLinkID);
            material->CompileShaderGraph();
            material->ApplyGraphToProperties();
        }

        // Node context menu (right-click on selected node -> delete)
        const int selectedNodeCount = ImNodes::NumSelectedNodes();
        // Yalnız graph alanında sağ tık (eskiden seçili düğüm varken Viewport'ta sağ tık da bu menüyü açıyordu)
        if (selectedNodeCount > 0 && ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            std::vector<int> selectedNodes((size_t)selectedNodeCount);
            ImNodes::GetSelectedNodes(selectedNodes.data());
            const int selectedNodeID = selectedNodes.front();
            auto* selectedNode = FindNodeByID(material->GraphNodes, selectedNodeID);

            if (selectedNode && selectedNode->Type != ShaderGraphNodeType::MasterOutput)
            {
                ImGui::SetNextWindowPos(ImGui::GetMousePos());
                ImGui::OpenPopup("NodeContextMenu");
            }
        }

        if (ImGui::BeginPopup("NodeContextMenu"))
        {
            const int selectedNodeCount2 = ImNodes::NumSelectedNodes();
            if (selectedNodeCount2 > 0)
            {
                std::vector<int> selectedNodes2((size_t)selectedNodeCount2);
                ImNodes::GetSelectedNodes(selectedNodes2.data());
                const int selectedNodeID = selectedNodes2.front();

                if (ImGui::MenuItem("Delete Node"))
                {
                    material->GraphLinks.erase(
                        std::remove_if(material->GraphLinks.begin(), material->GraphLinks.end(),
                            [selectedNodeID](const ShaderGraphLink& link)
                            {
                                return link.FromNodeID == selectedNodeID || link.ToNodeID == selectedNodeID;
                            }),
                        material->GraphLinks.end());
                    material->GraphNodes.erase(
                        std::remove_if(material->GraphNodes.begin(), material->GraphNodes.end(),
                            [selectedNodeID](const ShaderGraphNode& node)
                            {
                                return node.ID == selectedNodeID;
                            }),
                        material->GraphNodes.end());
                    CQ_CORE_INFO("[ShaderEditor] Node deleted, ID: {0}", selectedNodeID);
                    material->CompileShaderGraph();
                    material->ApplyGraphToProperties();
                    ImNodes::ClearNodeSelection();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        // Delete: seçili düğümleri sil (Master hariç)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            int n = ImNodes::NumSelectedNodes();
            if (n > 0)
            {
                std::vector<int> ids((size_t)n);
                ImNodes::GetSelectedNodes(ids.data());
                for (int id : ids)
                {
                    auto* node = FindNodeByID(material->GraphNodes, id);
                    if (!node || node->Type == ShaderGraphNodeType::MasterOutput)
                        continue;
                    material->GraphLinks.erase(std::remove_if(material->GraphLinks.begin(), material->GraphLinks.end(),
                        [id](const ShaderGraphLink& l) { return l.FromNodeID == id || l.ToNodeID == id; }), material->GraphLinks.end());
                    material->GraphNodes.erase(std::remove_if(material->GraphNodes.begin(), material->GraphNodes.end(),
                        [id](const ShaderGraphNode& nd) { return nd.ID == id; }), material->GraphNodes.end());
                }
                ImNodes::ClearNodeSelection();
            }
        }
        ImGui::EndChild();

        // Ayırıcı + yan panel
        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##sideSplit", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) m_SideWidth = std::clamp(m_SideWidth - ImGui::GetIO().MouseDelta.x, 240.0f, 900.0f);
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##ShaderSide", ImVec2(0, 0));
        DrawSidePanel(material);
        ImGui::EndChild();

        ImGui::End();
    }

    // ------------------------------------------------------------------ Yan panel

    uint64_t ShaderEditorPanel::HashGraph(const Material* m) const
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* data, size_t n) {
            const unsigned char* p = (const unsigned char*)data;
            for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        };
        for (const auto& n : m->GraphNodes)
        {
            mix(&n.ID, sizeof(n.ID)); mix(&n.Type, sizeof(n.Type));
            mix(&n.ColorValue, sizeof(n.ColorValue)); mix(&n.FloatValue, sizeof(n.FloatValue));
            mix(&n.Vec2Value, sizeof(n.Vec2Value)); mix(&n.Vec3Value, sizeof(n.Vec3Value)); mix(&n.IntValue, sizeof(n.IntValue));
            mix(n.TexturePath.data(), n.TexturePath.size()); mix(n.CustomCode.data(), n.CustomCode.size());
        }
        for (const auto& l : m->GraphLinks)
        {
            mix(&l.FromNodeID, sizeof(int)); mix(&l.FromPinIndex, sizeof(int)); mix(&l.ToNodeID, sizeof(int)); mix(&l.ToPinIndex, sizeof(int));
        }
        return h;
    }

    void ShaderEditorPanel::Recompile(Material* material)
    {
        auto t0 = std::chrono::steady_clock::now();
        material->CompileShaderGraph();
        material->ApplyGraphToProperties();
        m_LastCompileMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool ok = material->MaterialShader && material->MaterialShader->IsValid();
        char buf[128];
        if (ok)
            snprintf(buf, sizeof(buf), "Compiled in %.1f ms%s", m_LastCompileMs, material->MaterialShader->IsCompiledFromSPIRV() ? " (SPIR-V)" : "");
        else
            snprintf(buf, sizeof(buf), "Compile failed - see Code tab");
        m_CompileStatus = buf;
        m_CodeCacheKey = 0;
        // Malzeme bir .cqmat varlığıysa graph değişikliği dosyaya yazılır (sahneyi kaydetmeden kalıcı)
        const std::string assetPath = MaterialAsset::PathOf(material);
        if (!assetPath.empty() && !MaterialAsset::IsReadOnly(assetPath))
            if (auto shared = MaterialAsset::Load(assetPath))
                MaterialAsset::Save(shared, assetPath);
    }

    void ShaderEditorPanel::DrawNodeSearch(Material* material)
    {
        ImGui::SetNextWindowPos(m_SearchSpawnPos, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(260, 340), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("##NodeSearch"))
            return;
        if (ImGui::IsWindowAppearing())
        {
            m_SearchBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        bool enter = ImGui::InputTextWithHint("##q", ICON_FA_MAGNIFYING_GLASS "  Search nodes...", m_SearchBuffer, sizeof(m_SearchBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        std::string q = m_SearchBuffer;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        ImGui::Separator();
        ImGui::BeginChild("##results", ImVec2(0, 280));
        int shown = 0;
        for (int t = 1; t <= (int)ShaderGraphNodeType::CustomExpression; ++t)
        {
            auto type = (ShaderGraphNodeType)t;
            std::string name = ShaderNodeTypeToString(type);
            if (name == "Unknown" || type == ShaderGraphNodeType::MasterOutput)
                continue;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (!q.empty() && lower.find(q) == std::string::npos)
                continue;
            if (ImGui::Selectable(name.c_str()) || (enter && shown == 0))
            {
                // Fare konumuna (ızgara uzayı) yerleştir
                ImVec2 panning = ImNodes::EditorContextGetPanning();
                ImVec2 origin = m_GraphOrigin;
                glm::vec2 pos(m_SearchSpawnPos.x - origin.x - panning.x, m_SearchSpawnPos.y - origin.y - panning.y);
                material->GraphNodes.emplace_back(material->NextGraphNodeID++, type, glm::vec2(std::max(0.0f, pos.x), std::max(0.0f, pos.y)));
                ImGui::CloseCurrentPopup();
                break;
            }
            ++shown;
        }
        if (shown == 0)
            ImGui::TextDisabled("No matching nodes");
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    void ShaderEditorPanel::DrawSidePanel(Material* material)
    {
        const char* tabs[] = { ICON_FA_EYE " Preview", ICON_FA_SLIDERS " Properties", ICON_FA_CODE " Code" };
        if (ImGui::BeginTabBar("##sidetabs"))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ImGui::BeginTabItem(tabs[i]))
                {
                    m_SideTab = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        if (m_SideTab == 0) DrawPreview(material);
        else if (m_SideTab == 1) DrawProperties(material);
        else DrawCode(material);
    }

    void ShaderEditorPanel::DrawPreview(Material* material)
    {
        const float size = std::floor(std::min(ImGui::GetContentRegionAvail().x, 512.0f));
        const uint32_t px = (uint32_t)std::max(64.0f, size);
        if (!m_PreviewFramebuffer)
        {
            FramebufferSpecification spec;
            spec.Width = px;
            spec.Height = px;
            spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
            m_PreviewFramebuffer = Framebuffer::Create(spec);
        }
        else if (m_PreviewFramebuffer->GetSpecification().Width != px)
            m_PreviewFramebuffer->Resize(px, px);

        // Kamera (yörünge)
        m_PreviewTime += ImGui::GetIO().DeltaTime;
        if (m_PreviewRotate)
            m_PreviewYaw += ImGui::GetIO().DeltaTime * 0.4f;
        glm::vec3 eye(std::cos(m_PreviewPitch) * std::sin(m_PreviewYaw), std::sin(m_PreviewPitch), std::cos(m_PreviewPitch) * std::cos(m_PreviewYaw));
        eye *= m_PreviewDistance;
        RenderCamera cam;
        cam.View = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        cam.Projection = glm::perspective(glm::radians(35.0f), 1.0f, 0.05f, 100.0f);
        cam.Position = eye;
        cam.NearClip = 0.05f;
        cam.FarClip = 100.0f;
        cam.ViewportWidth = px;
        cam.ViewportHeight = px;

        SceneEnvironment env = m_Context ? m_Context->BuildRenderEnvironment(true) : SceneEnvironment{};
        env.Settings.Shadows.Enabled = false;
        env.Settings.Fog.Enabled = false;
        env.UseProceduralSky = env.UseProceduralSky || !env.Skybox;

        m_PreviewPost.BeginScene(px, px);
        RenderCommand::SetClearColor({ 0.05f, 0.05f, 0.06f, 1.0f });
        RenderCommand::Clear();
        Renderer3D::BeginFrame(cam, env);
        DirectionalLightComponent sun;
        sun.Direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.4f));
        sun.Color = glm::vec3(1.0f);
        sun.Intensity = 2.5f;
        if (m_Context)
        {
            Entity s = m_Context->GetSunSourceEntity();
            if (s && s.HasComponent<DirectionalLightComponent>())
                sun = s.GetComponent<DirectionalLightComponent>();
        }
        Renderer3D::SetDirectionalLight(sun);
        Renderer3D::DrawEnvironment();
        std::shared_ptr<Material> mat(material, [](Material*) {});
        glm::mat4 xf(1.0f);
        switch (m_PreviewMesh)
        {
            case 1: Renderer3D::DrawCube(glm::scale(xf, glm::vec3(0.6f)), mat); break;
            case 2: Renderer3D::DrawPlane(glm::rotate(glm::scale(xf, glm::vec3(1.1f)), glm::radians(90.0f), glm::vec3(1, 0, 0)), mat); break;
            case 3: Renderer3D::DrawCylinder(glm::scale(xf, glm::vec3(0.8f)), mat); break;
            default: Renderer3D::DrawSphere(glm::scale(xf, glm::vec3(1.0f)), mat); break;
        }
        Renderer3D::EndScene();
        m_PreviewPost.EndScene();
        m_PreviewFramebuffer->Bind();
        RenderCommand::Clear();
        PostProcessSettings post = m_Context ? m_Context->GetRenderSettings().Post : PostProcessSettings{};
        post.AmbientOcclusion = false;
        m_PreviewPost.Render(post, cam, m_PreviewTime);
        m_PreviewFramebuffer->Unbind();

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uint64_t)m_PreviewFramebuffer->GetColorAttachmentRendererID(0), ImVec2(size, size), ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                m_PreviewYaw -= ImGui::GetIO().MouseDelta.x * 0.01f;
                m_PreviewPitch = std::clamp(m_PreviewPitch + ImGui::GetIO().MouseDelta.y * 0.01f, -1.4f, 1.4f);
                m_PreviewRotate = false;
            }
            if (ImGui::GetIO().MouseWheel != 0.0f)
                m_PreviewDistance = std::clamp(m_PreviewDistance * std::pow(0.9f, ImGui::GetIO().MouseWheel), 1.5f, 12.0f);
        }
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + size, p.y + size), IM_COL32(0, 0, 0, 160));

        const char* meshes[] = { ICON_FA_CIRCLE " Sphere", ICON_FA_CUBE " Cube", ICON_FA_SQUARE " Plane", ICON_FA_DATABASE " Cylinder" };
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0) ImGui::SameLine();
            if (ImGui::RadioButton(meshes[i], m_PreviewMesh == i)) m_PreviewMesh = i;
        }
        ImGui::Checkbox("Auto Rotate", &m_PreviewRotate);
        ImGui::TextDisabled("Drag to orbit, wheel to zoom");
    }

    void ShaderEditorPanel::DrawProperties(Material* m)
    {
        UI::InputText("Name", m->Name);
        if (UI::BeginSection("Surface", true, ICON_FA_PALETTE))
        {
            UI::Color3("Albedo", m->Albedo);
            UI::SliderFloat("Metallic", m->Metallic, 0.0f, 1.0f);
            UI::SliderFloat("Roughness", m->Roughness, 0.0f, 1.0f);
            UI::SliderFloat("Occlusion", m->AO, 0.0f, 1.0f);
            UI::Color3("Emission", m->EmissionColor);
            UI::DragFloat("Emission Strength", m->EmissionStrength, 0.05f, 0.0f, 100.0f);
            UI::Vec2("Tiling", m->Tiling, 0.01f);
            UI::Vec2("Offset", m->Offset, 0.01f);
            UI::EndSection();
        }
        if (UI::BeginSection("Shading Model", true, ICON_FA_USER))
        {
            const char* models[] = { "Default Lit", "Subsurface (Skin)", "Hair", "Cloth", "Unlit" };
            UI::ComboEnum("Shading Model", m->Shading, models, 5,
                          "Default Lit: standard PBR. Subsurface: soft skin with light bleeding (MetaHuman-style). Hair: anisotropic strand highlights. Cloth: velvet sheen. Unlit: no lighting, base color + emission (screens, LED walls).");
            switch (m->Shading)
            {
                case Material::ShadingModel::Subsurface:
                    UI::Color3("Scatter Color", m->SubsurfaceColor, "Color of light scattered under the surface (reddish for skin)");
                    UI::SliderFloat("Subsurface", m->SubsurfaceAmount, 0.0f, 1.0f);
                    break;
                case Material::ShadingModel::Hair:
                    UI::Color3("Secondary Highlight", m->SubsurfaceColor, "Tint of the second (colored) highlight");
                    UI::SliderFloat("Highlight Shift", m->SubsurfaceAmount, 0.0f, 1.0f);
                    break;
                case Material::ShadingModel::Cloth:
                    UI::Color3("Sheen Color", m->SubsurfaceColor);
                    UI::SliderFloat("Sheen", m->SubsurfaceAmount, 0.0f, 1.0f);
                    break;
                default:
                    break;
            }
            UI::EndSection();
        }
        if (UI::BeginSection("Render State", true, ICON_FA_LAYER_GROUP))
        {
            const char* modes[] = { "Opaque", "Cutout", "Transparent" };
            UI::ComboEnum("Render Mode", m->Rendering, modes, 3);
            if (m->Rendering == Material::RenderMode::Cutout)
                UI::SliderFloat("Alpha Clip", m->AlphaClip, 0.0f, 1.0f);
            const char* culls[] = { "Back", "Front", "Off (Two Sided)" };
            UI::ComboEnum("Cull Mode", m->Culling, culls, 3);
            UI::EndSection();
        }
        if (UI::BeginSection("Shader Uniforms (reflection)", false, ICON_FA_LIST))
        {
            const ShaderReflection* r = m->MaterialShader ? m->MaterialShader->GetReflection() : nullptr;
            if (!r)
                ImGui::TextDisabled("No reflection data (compile the graph).");
            else if (ImGui::BeginTable("##refl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Stages");
                ImGui::TableHeadersRow();
                for (const auto& u : r->Uniforms)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(u.IsBlock ? UI::Colors::Accent : (u.IsSampler ? UI::Colors::Success : ImGui::GetStyleColorVec4(ImGuiCol_Text)), "%s", u.Name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    if (u.IsBlock) ImGui::TextDisabled("block %u B", u.Size);
                    else ImGui::TextDisabled("%s%s", u.Type.c_str(), u.ArraySize > 1 ? ("[" + std::to_string(u.ArraySize) + "]").c_str() : "");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s%s", (u.StageMask & 1) ? "V" : "", (u.StageMask & 2) ? "F" : "");
                }
                ImGui::EndTable();
                ImGui::TextDisabled("SPIR-V size: %u words", r->InstructionCount);
            }
            UI::EndSection();
        }
    }

    void ShaderEditorPanel::DrawCode(Material* m)
    {
        const char* views[] = { "Graph Source", "GLSL (from SPIR-V)", "SPIR-V", "HLSL (DirectX)", "MSL (Metal)" };
        const char* viewKeys[] = { "source", "glsl", "spirv", "hlsl", "msl" };
        ImGui::SetNextItemWidth(170.0f);
        ImGui::Combo("##view", &m_CodeView, views, 5);
        ImGui::SameLine();
        ImGui::RadioButton("Vertex", &m_CodeStage, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Fragment", &m_CodeStage, 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_COPY))
            ImGui::SetClipboardText(m_CodeCache.c_str());

        auto shader = m->MaterialShader;
        uint64_t key = (uint64_t)(uintptr_t)shader.get() * 31 + (uint64_t)m_CodeView * 7 + (uint64_t)m_CodeStage + 1;
        if (key != m_CodeCacheKey)
        {
            m_CodeCacheKey = key;
            if (!shader)
                m_CodeCache = m_CodeView == 0 ? (m_CodeStage == 1 ? m->LastCompiledFragmentSrc : m->LastCompiledVertexSrc) : "// Compile the graph first";
            else if (!shader->IsValid())
                m_CodeCache = "// Compile errors:\n" + shader->GetLastError();
            else
                m_CodeCache = shader->GetStageCode(m_CodeStage, viewKeys[m_CodeView]);
            if (m_CodeCache.empty())
                m_CodeCache = m_CodeStage == 1 ? m->LastCompiledFragmentSrc : m->LastCompiledVertexSrc;
        }
        ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontRole::Mono));
        ImGui::InputTextMultiline("##code", m_CodeCache.data(), m_CodeCache.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
    }
}
