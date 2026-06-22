#include "AnimatorPanel.h"

#include "Scene/Components.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <imgui.h>
#include <imnodes.h>
#include <nfd.h>

#include <algorithm>
#include <string>
#include <filesystem>

namespace Conqueror::Editor
{
    namespace
    {
        int GetInputAttrID(int nodeID) { return nodeID * 10 + 1; }
        int GetOutputAttrID(int nodeID) { return nodeID * 10 + 2; }
        bool IsInputAttr(int attrID) { return attrID % 10 == 1; }
        bool IsOutputAttr(int attrID) { return attrID % 10 == 2; }

        enum class AnimNodeType
        {
            Entry, State, AnyState, Exit
        };

        struct AnimGraphNode
        {
            int ID;
            AnimNodeType Type;
            std::string StateName;
            glm::vec2 EditorPosition;

            AnimGraphNode(int id, AnimNodeType type, const std::string& name = "", const glm::vec2& pos = {0.f, 0.f})
                : ID(id), Type(type), StateName(name), EditorPosition(pos) {}
        };

        struct AnimGraphLink
        {
            int ID;
            int FromNodeID;
            int ToNodeID;

            AnimGraphLink(int id, int from, int to)
                : ID(id), FromNodeID(from), ToNodeID(to) {}
        };

        std::vector<AnimGraphNode> s_Nodes;
        std::vector<AnimGraphLink> s_Links;
        int s_NextGraphID = 100;

        AnimGraphNode* FindNodeByID(int id)
        {
            for (auto& node : s_Nodes)
            {
                if (node.ID == id)
                    return &node;
            }
            return nullptr;
        }

        AnimGraphNode* FindNodeByStateName(const std::string& name)
        {
            for (auto& node : s_Nodes)
            {
                if (node.StateName == name)
                    return &node;
            }
            return nullptr;
        }

        void RebuildGraphFromController(const AnimationController& controller)
        {
            s_Nodes.clear();
            s_Links.clear();
            s_NextGraphID = 100;

            int entryID = s_NextGraphID++;
            s_Nodes.emplace_back(entryID, AnimNodeType::Entry, "Entry", glm::vec2(50.f, 200.f));

            int anyStateID = s_NextGraphID++;
            s_Nodes.emplace_back(anyStateID, AnimNodeType::AnyState, "Any State", glm::vec2(50.f, 400.f));

            int exitID = s_NextGraphID++;
            s_Nodes.emplace_back(exitID, AnimNodeType::Exit, "Exit", glm::vec2(1200.f, 300.f));

            if (!controller.Layers.empty())
            {
                const auto& layer = controller.Layers[0];

                for (const auto& state : layer.States)
                {
                    int stateID = s_NextGraphID++;
                    s_Nodes.emplace_back(stateID, AnimNodeType::State, state.Name, state.EditorPosition);
                }

                auto* defaultNode = FindNodeByStateName(layer.DefaultState);
                if (defaultNode)
                {
                    s_Links.emplace_back(s_NextGraphID++, entryID, defaultNode->ID);
                }

                for (const auto& trans : layer.Transitions)
                {
                    auto* fromNode = FindNodeByStateName(trans.FromState);
                    auto* toNode = FindNodeByStateName(trans.ToState);
                    if (fromNode && toNode)
                    {
                        s_Links.emplace_back(s_NextGraphID++, fromNode->ID, toNode->ID);
                    }
                }
            }
        }
    }

    AnimatorPanel::AnimatorPanel()
    {
    }

    AnimatorPanel::~AnimatorPanel()
    {
    }

    void AnimatorPanel::SetContext(Scene* scene)
    {
        m_Context = scene;
    }

    void AnimatorPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectedEntity = entity;
    }

    void AnimatorPanel::OpenController(const std::string& filepath)
    {
        m_ControllerFilePath = filepath;
        m_Controller = AnimationController::Deserialize(filepath);
        if (m_Controller)
        {
            RebuildGraphFromController(*m_Controller);
            m_IsOpen = true;
        }
    }

    void AnimatorPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;

        ImGui::Begin("Animator", &m_IsOpen);

        // Ctrl+S kaydetme
        if (m_Controller && !m_ControllerFilePath.empty() && ImGui::IsWindowFocused())
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            {
                for (auto& node : s_Nodes)
                {
                    if (node.Type == AnimNodeType::State && !m_Controller->Layers.empty())
                    {
                        auto* state = m_Controller->GetState(m_Controller->Layers[0], node.StateName);
                        if (state)
                            state->EditorPosition = node.EditorPosition;
                    }
                }
                m_Controller->NextNodeID = s_NextGraphID;
                m_Controller->Serialize(m_ControllerFilePath);
            }
        }

        if (!m_Controller)
        {
            ImGui::TextUnformatted("No Animation Controller loaded.");
            ImGui::Text("Drop a .cqcont file or use Open button.");
            DrawToolbar();
            ImGui::End();
            return;
        }

        DrawToolbar();
        ImGui::Separator();

        ImGui::Columns(3, "AnimatorColumns", true);

        ImGui::SetColumnWidth(0, 220.f);
        DrawParameters();

        ImGui::NextColumn();

        DrawNodeEditor();

        ImGui::NextColumn();

        ImGui::SetColumnWidth(2, 250.f);
        DrawStateProperties();

        ImGui::Columns(1);

        ImGui::End();
    }

    void AnimatorPanel::DrawToolbar()
    {
        ImGui::Text("%s", m_ControllerFilePath.empty() ? "(unsaved)" : m_ControllerFilePath.c_str());

        if (m_SelectedEntity && m_SelectedEntity.HasComponent<AnimationComponent>())
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 200.f);
            if (ImGui::Button("Assign to Entity"))
            {
                auto& ac = m_SelectedEntity.GetComponent<AnimationComponent>();
                ac.ControllerFilePath = m_ControllerFilePath;
                ac.Controller = m_Controller;
            }
        }
    }

    void AnimatorPanel::DrawParameters()
    {
        ImGui::TextDisabled("Parameters");
        ImGui::Separator();

        if (ImGui::Button("+ Float"))
        {
            AnimParameter param;
            param.Name = "NewFloat";
            param.Type = AnimParameterType::Float;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Bool"))
        {
            AnimParameter param;
            param.Name = "NewBool";
            param.Type = AnimParameterType::Bool;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Int"))
        {
            AnimParameter param;
            param.Name = "NewInt";
            param.Type = AnimParameterType::Int;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Trigger"))
        {
            AnimParameter param;
            param.Name = "NewTrigger";
            param.Type = AnimParameterType::Trigger;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
        }

        ImGui::Separator();

        int toRemove = -1;
        for (int i = 0; i < (int)m_Controller->Parameters.size(); i++)
        {
            auto& param = m_Controller->Parameters[i];
            ImGui::PushID(i);

            const char* typeIcon = "?";
            if (param.Type == AnimParameterType::Float) typeIcon = "F";
            else if (param.Type == AnimParameterType::Bool) typeIcon = "B";
            else if (param.Type == AnimParameterType::Int) typeIcon = "I";
            else if (param.Type == AnimParameterType::Trigger) typeIcon = "T";

            ImGui::Text("[%s]", typeIcon);
            ImGui::SameLine();

            char nameBuf[128];
            strncpy(nameBuf, param.Name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("##param", nameBuf, sizeof(nameBuf)))
                param.Name = nameBuf;

            if (param.Type != AnimParameterType::Trigger)
            {
                ImGui::Indent(20.f);
                ImGui::DragFloat("##default", &param.DefaultValue, 0.01f);
                ImGui::Unindent(20.f);
            }

            ImGui::SameLine(ImGui::GetColumnWidth() - 20.f);
            if (ImGui::SmallButton("X"))
                toRemove = i;

            ImGui::PopID();
        }

        if (toRemove >= 0 && toRemove < (int)m_Controller->Parameters.size())
            m_Controller->Parameters.erase(m_Controller->Parameters.begin() + toRemove);
    }

    void AnimatorPanel::DrawNodeEditor()
    {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("AnimGraphContextMenu");

        if (ImGui::BeginPopup("AnimGraphContextMenu"))
        {
            if (ImGui::MenuItem("Add State"))
            {
                std::string name = "State_" + std::to_string(s_NextGraphID);
                AddState(name.c_str(), 400.f, 200.f);
            }
            ImGui::EndPopup();
        }

        ImNodes::BeginNodeEditor();

        for (auto& node : s_Nodes)
        {
            if (node.Type == AnimNodeType::Entry)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 120, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 150, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 180, 50, 255));
            }
            else if (node.Type == AnimNodeType::Exit)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(120, 30, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(150, 40, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(180, 50, 50, 255));
            }
            else if (node.Type == AnimNodeType::AnyState)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(120, 120, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(150, 150, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(180, 180, 50, 255));
            }
            else
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 30, 120, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 40, 150, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 50, 180, 255));
            }

            ImNodes::BeginNode(node.ID);
            ImNodes::BeginNodeTitleBar();

            const char* icon = "";
            if (node.Type == AnimNodeType::Entry) icon = ">> ";
            else if (node.Type == AnimNodeType::Exit) icon = "XX ";
            else if (node.Type == AnimNodeType::AnyState) icon = "** ";

            ImGui::Text("%s%s", icon, node.StateName.c_str());
            ImNodes::EndNodeTitleBar();

            if (node.Type != AnimNodeType::Entry)
            {
                ImNodes::BeginInputAttribute(GetInputAttrID(node.ID));
                ImGui::TextUnformatted("In");
                ImNodes::EndInputAttribute();
            }

            if (node.Type != AnimNodeType::Exit)
            {
                ImNodes::BeginOutputAttribute(GetOutputAttrID(node.ID));
                ImGui::Indent(40.f);
                ImGui::TextUnformatted("Out");
                ImNodes::EndOutputAttribute();
            }

            ImNodes::EndNode();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();

            ImNodes::SetNodeGridSpacePos(node.ID, ImVec2(node.EditorPosition.x, node.EditorPosition.y));
        }

        for (auto& link : s_Links)
        {
            ImNodes::Link(link.ID, GetOutputAttrID(link.FromNodeID), GetInputAttrID(link.ToNodeID));
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        for (auto& node : s_Nodes)
        {
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.ID);
            node.EditorPosition = { pos.x, pos.y };
        }

        int startAttr = 0, endAttr = 0;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
        {
            int fromNodeID = 0, toNodeID = 0;
            if (IsOutputAttr(startAttr) && IsInputAttr(endAttr))
            {
                fromNodeID = startAttr / 10;
                toNodeID = endAttr / 10;
            }
            else if (IsInputAttr(startAttr) && IsOutputAttr(endAttr))
            {
                fromNodeID = endAttr / 10;
                toNodeID = startAttr / 10;
            }

            auto* fromNode = FindNodeByID(fromNodeID);
            auto* toNode = FindNodeByID(toNodeID);

            if (fromNode && toNode && fromNodeID != toNodeID &&
                fromNode->Type != AnimNodeType::Exit &&
                toNode->Type != AnimNodeType::Entry)
            {
                s_Links.emplace_back(s_NextGraphID++, fromNodeID, toNodeID);

                if (fromNode->Type == AnimNodeType::State && toNode->Type == AnimNodeType::State)
                {
                    AddTransition(fromNode->StateName.c_str(), toNode->StateName.c_str());
                }
            }
        }

        int destroyedLinkID = 0;
        if (ImNodes::IsLinkDestroyed(&destroyedLinkID))
        {
            auto linkIt = std::find_if(s_Links.begin(), s_Links.end(),
                [destroyedLinkID](const AnimGraphLink& l) { return l.ID == destroyedLinkID; });

            if (linkIt != s_Links.end())
            {
                auto* fromNode = FindNodeByID(linkIt->FromNodeID);
                auto* toNode = FindNodeByID(linkIt->ToNodeID);

                if (fromNode && toNode && fromNode->Type == AnimNodeType::State && toNode->Type == AnimNodeType::State)
                {
                    if (m_Controller && !m_Controller->Layers.empty())
                    {
                        auto& layer = m_Controller->Layers[0];
                        layer.Transitions.erase(
                            std::remove_if(layer.Transitions.begin(), layer.Transitions.end(),
                                [&](const AnimTransition& t) {
                                    return t.FromState == fromNode->StateName && t.ToState == toNode->StateName;
                                }),
                            layer.Transitions.end());
                    }
                }

                s_Links.erase(linkIt);
            }
        }

        {
            int nodeCount = ImNodes::NumSelectedNodes();
            if (nodeCount > 0)
            {
                std::vector<int> selectedNodes(nodeCount);
                ImNodes::GetSelectedNodes(selectedNodes.data());
                m_SelectedNodeID = selectedNodes.front();
                m_SelectedLinkID = -1;
                auto* node = FindNodeByID(m_SelectedNodeID);
                if (node)
                    m_SelectedStateName = node->StateName;
            }
            else
            {
                m_SelectedNodeID = -1;
                m_SelectedStateName.clear();
            }

            int linkCount = ImNodes::NumSelectedLinks();
            if (linkCount > 0)
            {
                std::vector<int> selectedLinks(linkCount);
                ImNodes::GetSelectedLinks(selectedLinks.data());
                m_SelectedLinkID = selectedLinks.front();
                m_SelectedNodeID = -1;

                AnimGraphLink* link = nullptr;
                for (auto& l : s_Links)
                {
                    if (l.ID == m_SelectedLinkID) { link = &l; break; }
                }
                if (link)
                {
                    auto* from = FindNodeByID(link->FromNodeID);
                    auto* to = FindNodeByID(link->ToNodeID);
                    if (from && to)
                    {
                        m_SelectedTransitionFrom = from->StateName;
                        m_SelectedTransitionTo = to->StateName;
                    }
                }
            }
            else
            {
                m_SelectedLinkID = -1;
                m_SelectedTransitionFrom.clear();
                m_SelectedTransitionTo.clear();
            }
        }
    }

    void AnimatorPanel::DrawStateProperties()
    {
        ImGui::TextDisabled("Properties");
        ImGui::Separator();

        if (m_SelectedNodeID >= 0)
        {
            auto* node = FindNodeByID(m_SelectedNodeID);
            if (node && node->Type == AnimNodeType::State && m_Controller && !m_Controller->Layers.empty())
            {
                auto& layer = m_Controller->Layers[0];
                auto* state = m_Controller->GetState(layer, node->StateName);

                if (state)
                {
                    ImGui::Text("State: %s", state->Name.c_str());

                    char nameBuf[128];
                    strncpy(nameBuf, state->Name.c_str(), sizeof(nameBuf) - 1);
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                    {
                        std::string oldName = state->Name;
                        state->Name = nameBuf;
                        node->StateName = nameBuf;
                        for (auto& trans : layer.Transitions)
                        {
                            if (trans.FromState == oldName) trans.FromState = nameBuf;
                            if (trans.ToState == oldName) trans.ToState = nameBuf;
                        }
                        if (layer.DefaultState == oldName) layer.DefaultState = nameBuf;
                    }

                    ImGui::Separator();

                    if (m_SelectedEntity && m_SelectedEntity.HasComponent<ModelComponent>())
                    {
                        auto& mc = m_SelectedEntity.GetComponent<ModelComponent>();
                        if (mc.ModelData && !mc.ModelData->Animations.empty())
                        {
                            ImGui::Text("Clip:");
                            ImGui::SameLine();

                            std::string preview = state->ClipName.empty() ? "(none)" : state->ClipName;
                            if (ImGui::BeginCombo("##clip", preview.c_str()))
                            {
                                for (int i = 0; i < (int)mc.ModelData->Animations.size(); i++)
                                {
                                    auto& clip = mc.ModelData->Animations[i];
                                    if (!clip) continue;

                                    bool selected = (state->ClipIndex == i);
                                    if (ImGui::Selectable(clip->Name.c_str(), selected))
                                    {
                                        state->ClipIndex = i;
                                        state->ClipName = clip->Name;
                                    }
                                    if (selected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                    }

                    ImGui::DragFloat("Speed", &state->Speed, 0.05f, -5.f, 5.f);
                    ImGui::Checkbox("Loop", &state->Loop);

                    ImGui::Separator();

                    bool isDefault = (layer.DefaultState == state->Name);
                    if (isDefault)
                    {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.f), "Default State");
                    }
                    else
                    {
                        if (ImGui::Button("Set as Default"))
                        {
                            layer.DefaultState = state->Name;
                        }
                    }

                    ImGui::Separator();

                    if (ImGui::Button("Delete State"))
                    {
                        RemoveState(state->Name.c_str());
                        m_SelectedNodeID = -1;
                        m_SelectedStateName.clear();
                    }
                }
            }
            else if (node && node->Type == AnimNodeType::Entry)
            {
                ImGui::Text("Entry Node");
                ImGui::TextDisabled("Connects to Default State");
                if (m_Controller && !m_Controller->Layers.empty())
                {
                    ImGui::Text("Default: %s", m_Controller->Layers[0].DefaultState.c_str());
                }
            }
            else if (node && node->Type == AnimNodeType::AnyState)
            {
                ImGui::Text("Any State");
                ImGui::TextDisabled("Can transition from any state");
            }
            else if (node && node->Type == AnimNodeType::Exit)
            {
                ImGui::Text("Exit Node");
            }
        }
        else if (m_SelectedLinkID >= 0 && m_Controller && !m_Controller->Layers.empty())
        {
            ImGui::Text("Transition");
            ImGui::Text("From: %s", m_SelectedTransitionFrom.c_str());
            ImGui::Text("To: %s", m_SelectedTransitionTo.c_str());

            auto& layer = m_Controller->Layers[0];
            AnimTransition* trans = nullptr;
            for (auto& t : layer.Transitions)
            {
                if (t.FromState == m_SelectedTransitionFrom && t.ToState == m_SelectedTransitionTo)
                {
                    trans = &t;
                    break;
                }
            }

            if (trans)
            {
                ImGui::Separator();
                ImGui::DragFloat("Duration", &trans->Duration, 0.01f, 0.f, 5.f);
                ImGui::Checkbox("Has Exit Time", &trans->HasExitTime);
                if (trans->HasExitTime)
                {
                    ImGui::DragFloat("Exit Time", &trans->ExitTime, 0.01f, 0.f, 1.f);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Conditions");

                if (ImGui::Button("+ Condition"))
                {
                    AnimCondition cond;
                    cond.ParameterName = m_Controller->Parameters.empty() ? "" : m_Controller->Parameters[0].Name;
                    cond.Mode = AnimConditionMode::Greater;
                    cond.Threshold = 0.f;
                    trans->Conditions.push_back(cond);
                }

                int condToRemove = -1;
                for (int i = 0; i < (int)trans->Conditions.size(); i++)
                {
                    auto& cond = trans->Conditions[i];
                    ImGui::PushID(i + 1000);

                    char idBuf[64];

                    char paramPreview[128];
                    strncpy(paramPreview, cond.ParameterName.c_str(), sizeof(paramPreview) - 1);
                    paramPreview[sizeof(paramPreview) - 1] = '\0';
                    snprintf(idBuf, sizeof(idBuf), "##cond_param_%d", i);
                    if (ImGui::BeginCombo(idBuf, paramPreview))
                    {
                        for (auto& p : m_Controller->Parameters)
                        {
                            bool selected = (cond.ParameterName == p.Name);
                            if (ImGui::Selectable(p.Name.c_str(), selected))
                                cond.ParameterName = p.Name;
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    const char* modes[] = { "Greater", "Less", "Equals", "NotEquals" };
                    int modeIdx = static_cast<int>(cond.Mode);
                    snprintf(idBuf, sizeof(idBuf), "##cond_mode_%d", i);
                    if (ImGui::Combo(idBuf, &modeIdx, modes, 4))
                        cond.Mode = static_cast<AnimConditionMode>(modeIdx);

                    for (auto& p : m_Controller->Parameters)
                    {
                        if (p.Name == cond.ParameterName && p.Type != AnimParameterType::Trigger)
                        {
                            snprintf(idBuf, sizeof(idBuf), "##cond_thresh_%d", i);
                            ImGui::DragFloat(idBuf, &cond.Threshold, 0.01f);
                            break;
                        }
                    }

                    snprintf(idBuf, sizeof(idBuf), "X##cond_%d", i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(idBuf))
                        condToRemove = i;

                    ImGui::PopID();
                }

                if (condToRemove >= 0 && condToRemove < (int)trans->Conditions.size())
                    trans->Conditions.erase(trans->Conditions.begin() + condToRemove);

                ImGui::Separator();

                if (ImGui::Button("Delete Transition"))
                {
                    layer.Transitions.erase(
                        std::remove_if(layer.Transitions.begin(), layer.Transitions.end(),
                            [&](const AnimTransition& t) {
                                return t.FromState == m_SelectedTransitionFrom && t.ToState == m_SelectedTransitionTo;
                            }),
                        layer.Transitions.end());

                    s_Links.erase(
                        std::remove_if(s_Links.begin(), s_Links.end(),
                            [this](const AnimGraphLink& l) { return l.ID == m_SelectedLinkID; }),
                        s_Links.end());

                    m_SelectedLinkID = -1;
                    m_SelectedTransitionFrom.clear();
                    m_SelectedTransitionTo.clear();
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Select a node or transition");
        }
    }

    void AnimatorPanel::AddState(const char* name, float x, float y)
    {
        if (!m_Controller) return;
        if (m_Controller->Layers.empty())
        {
            AnimLayer layer;
            layer.Name = "Base Layer";
            m_Controller->Layers.push_back(layer);
        }

        AnimState state;
        state.Name = name;
        state.EditorPosition = { x, y };
        m_Controller->Layers[0].States.push_back(state);

        int nodeID = s_NextGraphID++;
        s_Nodes.emplace_back(nodeID, AnimNodeType::State, name, glm::vec2(x, y));
    }

    void AnimatorPanel::AddTransition(const char* from, const char* to)
    {
        if (!m_Controller || m_Controller->Layers.empty()) return;

        auto& layer = m_Controller->Layers[0];

        for (const auto& t : layer.Transitions)
        {
            if (t.FromState == from && t.ToState == to)
                return;
        }

        AnimTransition trans;
        trans.FromState = from;
        trans.ToState = to;
        trans.Duration = 0.25f;
        trans.ExitTime = 0.8f;
        layer.Transitions.push_back(trans);
    }

    void AnimatorPanel::RemoveState(const char* name)
    {
        if (!m_Controller || m_Controller->Layers.empty()) return;

        auto& layer = m_Controller->Layers[0];

        layer.States.erase(
            std::remove_if(layer.States.begin(), layer.States.end(),
                [name](const AnimState& s) { return s.Name == name; }),
            layer.States.end());

        layer.Transitions.erase(
            std::remove_if(layer.Transitions.begin(), layer.Transitions.end(),
                [name](const AnimTransition& t) { return t.FromState == name || t.ToState == name; }),
            layer.Transitions.end());

        if (layer.DefaultState == name && !layer.States.empty())
            layer.DefaultState = layer.States[0].Name;

        s_Nodes.erase(
            std::remove_if(s_Nodes.begin(), s_Nodes.end(),
                [name](const AnimGraphNode& n) { return n.StateName == name; }),
            s_Nodes.end());

        s_Links.erase(
            std::remove_if(s_Links.begin(), s_Links.end(),
                [name](const AnimGraphLink& l) {
                    auto* from = FindNodeByID(l.FromNodeID);
                    auto* to = FindNodeByID(l.ToNodeID);
                    return (from && from->StateName == name) || (to && to->StateName == name);
                }),
            s_Links.end());
    }
}
