#include "ShaderEditorPanel.h"

#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/Texture.h"

#include <imgui.h>
#include <imnodes.h>

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
        ImNodes::CreateContext();
        ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(15, 15, 18, 200);
    }

    ShaderEditorPanel::~ShaderEditorPanel()
    {
        ImNodes::DestroyContext();
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

    void ShaderEditorPanel::OnImGuiRender()
    {
        ImGui::Begin("Shader Graph Editor");

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
        Material* material = nullptr;
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
                if (ImGui::Button("Create Material"))
                {
                    meshRenderer.MaterialInstance = Material::CreateDefault();
                    EnsureMasterNode(meshRenderer.MaterialInstance.get());
                    meshRenderer.MaterialInstance->CompileShaderGraph();
                    meshRenderer.MaterialInstance->ApplyGraphToProperties();
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

        ImGui::Text("Material: %s", material->Name.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Apply Graph"))
        {
            CQ_CORE_INFO("[ShaderEditor] Apply Graph pressed - Material: '{0}'", material->Name);
            CQ_CORE_INFO("[ShaderEditor] Graph node count: {0}, link count: {1}", material->GraphNodes.size(), material->GraphLinks.size());
            material->CompileShaderGraph();
            material->ApplyGraphToProperties();
            if (!material->LastCompiledFragmentSrc.empty())
            {
                CQ_CORE_INFO("[ShaderEditor] === COMPILED FRAGMENT SHADER START ===\n{0}", material->LastCompiledFragmentSrc);
                CQ_CORE_INFO("[ShaderEditor] === COMPILED FRAGMENT SHADER END ===");
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

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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

        ImNodes::BeginNodeEditor();

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
        if (selectedNodeCount > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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

        ImGui::End();
    }
}
