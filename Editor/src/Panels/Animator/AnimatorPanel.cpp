#include "AnimatorPanel.h"

#include "Scene/Components.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <string>
#include <filesystem>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        constexpr float NODE_W = 160.f;
        constexpr float NODE_H = 60.f;
        constexpr float NODE_TITLE_H = 24.f;
        constexpr float TRANSITION_OFFSET = 6.f;
        constexpr float ARROW_SIZE = 8.f;
        constexpr float HIT_THRESHOLD = 12.f;

        enum class AnimNodeType
        {
            Entry, State, AnyState, Exit, SubState, UpNode, BlendTree
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

        std::vector<AnimGraphNode> s_Nodes;
        int s_NextGraphID = 100;

        struct TransitionEdge
        {
            std::string LeftState;
            std::string RightState;
        };
        std::vector<TransitionEdge> s_TransitionEdges;

        struct SubStateData
        {
            std::vector<AnimState> States;
            std::vector<AnimTransition> Transitions;
            std::string DefaultState;
            glm::vec2 EntryPos = {50.f, 200.f};
            glm::vec2 AnyStatePos = {50.f, 400.f};
            glm::vec2 ExitPos = {700.f, 300.f};
            glm::vec2 UpNodePos = {350.f, 150.f};
        };
        std::unordered_map<int, SubStateData> s_SubStateData;

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

        AnimSubStateData* FindSubStateRecursive(std::vector<AnimSubStateData>& subStates, const std::string& name)
        {
            for (auto& ss : subStates)
            {
                if (ss.Name == name) return &ss;
                auto* found = FindSubStateRecursive(ss.SubStates, name);
                if (found) return found;
            }
            return nullptr;
        }

        const AnimSubStateData* FindSubStateRecursive(const std::vector<AnimSubStateData>& subStates, const std::string& name)
        {
            for (const auto& ss : subStates)
            {
                if (ss.Name == name) return &ss;
                auto* found = FindSubStateRecursive(ss.SubStates, name);
                if (found) return found;
            }
            return nullptr;
        }

        ImVec2 NodeCenter(const AnimGraphNode& node)
        {
            return ImVec2(node.EditorPosition.x + NODE_W / 2.f, node.EditorPosition.y + NODE_H / 2.f);
        }

        ImVec2 NodeRightCenter(const AnimGraphNode& node)
        {
            return ImVec2(node.EditorPosition.x + NODE_W, node.EditorPosition.y + NODE_H / 2.f);
        }

        ImVec2 NodeLeftCenter(const AnimGraphNode& node)
        {
            return ImVec2(node.EditorPosition.x, node.EditorPosition.y + NODE_H / 2.f);
        }

        bool IsMouseOverNode(const AnimGraphNode& node, const ImVec2& mousePos)
        {
            return mousePos.x >= node.EditorPosition.x && mousePos.x <= node.EditorPosition.x + NODE_W &&
                   mousePos.y >= node.EditorPosition.y && mousePos.y <= node.EditorPosition.y + NODE_H;
        }

        float DistToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
        {
            ImVec2 ab = { b.x - a.x, b.y - a.y };
            ImVec2 ap = { p.x - a.x, p.y - a.y };
            float t = (ab.x * ap.x + ab.y * ap.y) / (ab.x * ab.x + ab.y * ab.y + 1e-8f);
            t = std::max(0.f, std::min(1.f, t));
            ImVec2 closest = { a.x + ab.x * t, a.y + ab.y * t };
            float dx = p.x - closest.x;
            float dy = p.y - closest.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        void DrawArrow(ImDrawList* dl, const ImVec2& from, const ImVec2& to, ImU32 color, float size)
        {
            float dx = to.x - from.x;
            float dy = to.y - from.y;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-3f) return;

            float ux = dx / len;
            float uy = dy / len;

            ImVec2 mid = { (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f };
            ImVec2 p1 = { mid.x - ux * size - uy * size * 0.5f, mid.y - uy * size + ux * size * 0.5f };
            ImVec2 p2 = { mid.x - ux * size + uy * size * 0.5f, mid.y - uy * size - ux * size * 0.5f };
            dl->AddQuadFilled(mid, p1, { mid.x - ux * size * 0.3f, mid.y - uy * size * 0.3f }, p2, color);
        }

        void DrawTransitionLine(ImDrawList* dl, const ImVec2& from, const ImVec2& to, ImU32 color, float offset)
        {
            ImVec2 dir = { to.x - from.x, to.y - from.y };
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1e-3f) return;

            float nx = -dir.y / len;
            float ny = dir.x / len;

            ImVec2 oFrom = { from.x + nx * offset, from.y + ny * offset };
            ImVec2 oTo = { to.x + nx * offset, to.y + ny * offset };

            dl->AddLine(oFrom, oTo, color, 2.0f);
            DrawArrow(dl, oFrom, oTo, color, ARROW_SIZE);
        }

        void RebuildGraphFromController(const AnimationController& controller, int layerIndex = 0)
        {
            s_Nodes.clear();
            s_TransitionEdges.clear();
            s_NextGraphID = 100;

            int entryID = s_NextGraphID++;
            s_Nodes.emplace_back(entryID, AnimNodeType::Entry, "Entry", glm::vec2(50.f, 200.f));

            int anyStateID = s_NextGraphID++;
            s_Nodes.emplace_back(anyStateID, AnimNodeType::AnyState, "Any State", glm::vec2(50.f, 400.f));

            int exitID = s_NextGraphID++;
            s_Nodes.emplace_back(exitID, AnimNodeType::Exit, "Exit", glm::vec2(700.f, 300.f));

            // SubState node'larını layer'dan yükle
            if (!controller.Layers.empty() && layerIndex < (int)controller.Layers.size())
            {
                for (const auto& ss : controller.Layers[layerIndex].SubStates)
                {
                    int ssNodeID = s_NextGraphID++;
                    s_Nodes.emplace_back(ssNodeID, AnimNodeType::SubState, ss.Name, ss.EditorPosition);
                }
            }

            if (!controller.Layers.empty() && layerIndex < (int)controller.Layers.size())
            {
                const auto& layer = controller.Layers[layerIndex];

                for (const auto& state : layer.States)
                {
                    int stateID = s_NextGraphID++;
                    s_Nodes.emplace_back(stateID, state.Type == AnimStateType::BlendTree ? AnimNodeType::BlendTree : AnimNodeType::State, state.Name, state.EditorPosition);
                }

                for (const auto& trans : layer.Transitions)
                {
                    bool found = false;
                    for (auto& edge : s_TransitionEdges)
                    {
                        if ((edge.LeftState == trans.FromState && edge.RightState == trans.ToState) ||
                            (edge.LeftState == trans.ToState && edge.RightState == trans.FromState))
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        auto* fromNode = FindNodeByStateName(trans.FromState);
                        auto* toNode = FindNodeByStateName(trans.ToState);
                        if (fromNode && toNode)
                        {
                            TransitionEdge edge;
                            if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                            {
                                edge.LeftState = trans.FromState;
                                edge.RightState = trans.ToState;
                            }
                            else
                            {
                                edge.LeftState = trans.ToState;
                                edge.RightState = trans.FromState;
                            }
                            s_TransitionEdges.push_back(edge);
                        }
                    }
                }
            }
        }
    }

    void AnimatorPanel::RebuildGraphFromSubState(int subStateNodeID)
    {
        if (!m_Controller) return;

        std::string subStateName;
        for (auto& node : s_Nodes)
        {
            if (node.ID == subStateNodeID)
            {
                subStateName = node.StateName;
                break;
            }
        }
        if (subStateName.empty())
        {
            for (const auto& entry : m_NavigationStack)
            {
                if (entry.NodeID == subStateNodeID)
                {
                    subStateName = entry.Name;
                    break;
                }
            }
        }
        if (subStateName.empty()) return;

        RebuildGraphFromSubStateByName(subStateName);
    }

    void AnimatorPanel::RebuildGraphFromSubStateByName(const std::string& subStateName)
    {
        if (!m_Controller || subStateName.empty()) return;

        s_Nodes.clear();
        s_TransitionEdges.clear();
        s_NextGraphID = 100;

        AnimLayer* targetLayer = nullptr;
        AnimSubStateData* subData = nullptr;

        for (int li = 0; li < (int)m_Controller->Layers.size(); li++)
        {
            if (m_Controller->Layers[li].Name == subStateName)
            {
                targetLayer = &m_Controller->Layers[li];
                break;
            }
        }
        if (!targetLayer)
        {
            subData = const_cast<AnimSubStateData*>(FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, subStateName));
        }

        std::string upName;
        if (!m_NavigationStack.empty())
        {
            upName = m_NavigationStack.back().Name;
        }
        else if (targetLayer)
        {
            upName = targetLayer->Name;
        }
        else
        {
            upName = subStateName;
        }

        int upID = s_NextGraphID++;
        s_Nodes.emplace_back(upID, AnimNodeType::UpNode, "(Up) " + upName, glm::vec2(400.f, 250.f));

        int entryID = s_NextGraphID++;
        s_Nodes.emplace_back(entryID, AnimNodeType::Entry, "Entry", glm::vec2(50.f, 200.f));

        int anyStateID = s_NextGraphID++;
        s_Nodes.emplace_back(anyStateID, AnimNodeType::AnyState, "Any State", glm::vec2(50.f, 400.f));

        int exitID = s_NextGraphID++;
        s_Nodes.emplace_back(exitID, AnimNodeType::Exit, "Exit", glm::vec2(700.f, 300.f));

        if (targetLayer)
        {
            for (const auto& state : targetLayer->States)
            {
                int stateID = s_NextGraphID++;
                s_Nodes.emplace_back(stateID, AnimNodeType::State, state.Name, state.EditorPosition);
            }

            for (const auto& trans : targetLayer->Transitions)
            {
                bool found = false;
                for (auto& edge : s_TransitionEdges)
                {
                    if ((edge.LeftState == trans.FromState && edge.RightState == trans.ToState) ||
                        (edge.LeftState == trans.ToState && edge.RightState == trans.FromState))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    auto* fromNode = FindNodeByStateName(trans.FromState);
                    auto* toNode = FindNodeByStateName(trans.ToState);
                    if (fromNode && toNode)
                    {
                        TransitionEdge edge;
                        if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                        {
                            edge.LeftState = trans.FromState;
                            edge.RightState = trans.ToState;
                        }
                        else
                        {
                            edge.LeftState = trans.ToState;
                            edge.RightState = trans.FromState;
                        }
                        s_TransitionEdges.push_back(edge);
                    }
                }
            }
        }
        else if (subData)
        {
            for (const auto& nestedSS : subData->SubStates)
            {
                int ssNodeID = s_NextGraphID++;
                s_Nodes.emplace_back(ssNodeID, AnimNodeType::SubState, nestedSS.Name, nestedSS.EditorPosition);
            }

            for (const auto& state : subData->States)
            {
                int stateID = s_NextGraphID++;
                s_Nodes.emplace_back(stateID, AnimNodeType::State, state.Name, state.EditorPosition);
            }

            for (const auto& trans : subData->Transitions)
            {
                bool found = false;
                for (auto& edge : s_TransitionEdges)
                {
                    if ((edge.LeftState == trans.FromState && edge.RightState == trans.ToState) ||
                        (edge.LeftState == trans.ToState && edge.RightState == trans.FromState))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    auto* fromNode = FindNodeByStateName(trans.FromState);
                    auto* toNode = FindNodeByStateName(trans.ToState);
                    if (fromNode && toNode)
                    {
                        TransitionEdge edge;
                        if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                        {
                            edge.LeftState = trans.FromState;
                            edge.RightState = trans.ToState;
                        }
                        else
                        {
                            edge.LeftState = trans.ToState;
                            edge.RightState = trans.FromState;
                        }
                        s_TransitionEdges.push_back(edge);
                    }
                }
            }
        }
    }

    int AnimatorPanel::GetCurrentSubStateID()
    {
        if (m_NavigationStack.empty()) return -1;
        return m_NavigationStack.back().NodeID;
    }

    void AnimatorPanel::RebuildGraphFromBlendTree(const std::string& stateName)
    {
        s_Nodes.clear();
        s_TransitionEdges.clear();
        s_NextGraphID = 100;

        int blendTreeID = s_NextGraphID++;
        s_Nodes.emplace_back(blendTreeID, AnimNodeType::BlendTree, stateName, glm::vec2(300.f, 200.f));
    }

    AnimatorPanel::AnimatorPanel()
    {
    }

    AnimationController* AnimatorPanel::s_ActiveController = nullptr;

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
            s_ActiveController = m_Controller.get();
            RebuildGraphFromController(*m_Controller);
            m_IsOpen = true;
            m_Dirty = false;
        }
    }

    void AnimatorPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;

        if (m_Dirty)
            ImGui::Begin("Animator *###Animator", nullptr);
        else
            ImGui::Begin("Animator###Animator", nullptr);

        // Ctrl+S kaydetme
        if (m_Controller && !m_ControllerFilePath.empty())
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            {
                for (auto& node : s_Nodes)
                {
                    if (node.Type == AnimNodeType::State)
                    {
                        // SubState içinde mi yoksa layer'da mı?
                        if (m_InSubStateView && !m_CurrentSubStateName.empty())
                        {
                            auto* ss = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
                            if (ss)
                            {
                                for (auto& s : ss->States)
                                {
                                    if (s.Name == node.StateName) { s.EditorPosition = node.EditorPosition; break; }
                                }
                            }
                            else
                            {
                                auto* state = m_Controller->GetState(m_Controller->Layers[m_SelectedLayerIndex], node.StateName);
                                if (state) state->EditorPosition = node.EditorPosition;
                            }
                        }
                        else
                        {
                            auto* state = m_Controller->GetState(m_Controller->Layers[m_SelectedLayerIndex], node.StateName);
                            if (state) state->EditorPosition = node.EditorPosition;
                        }
                    }
                    else if (node.Type == AnimNodeType::SubState)
                    {
                        auto* ss = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, node.StateName);
                        if (ss)
                        {
                            const_cast<AnimSubStateData*>(ss)->EditorPosition = node.EditorPosition;
                        }
                    }
                }
                m_Controller->NextNodeID = s_NextGraphID;
                m_Controller->Serialize(m_ControllerFilePath);
                m_Dirty = false;
            }
        }

        if (!m_Controller)
        {
            ImGui::TextUnformatted("No Animation Controller loaded.");
            ImGui::End();
            return;
        }

        float availW = ImGui::GetContentRegionAvail().x;
        float paramsW = 160.f;
        float graphW = availW - paramsW;
        if (graphW < 100.f) graphW = 100.f;

        ImGui::BeginChild("##leftPanel", ImVec2(paramsW, 0));
        if (ImGui::BeginTabBar("##AnimatorTabs"))
        {
            if (ImGui::BeginTabItem("Parameters"))
            {
                m_ActiveTab = 0;
                DrawParameters();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Layers"))
            {
                m_ActiveTab = 1;
                DrawLayers();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##graph", ImVec2(graphW, 0));
        DrawNodeEditor();
        ImGui::EndChild();

        ImGui::End();
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
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Bool"))
        {
            AnimParameter param;
            param.Name = "NewBool";
            param.Type = AnimParameterType::Bool;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Int"))
        {
            AnimParameter param;
            param.Name = "NewInt";
            param.Type = AnimParameterType::Int;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
            m_Dirty = true;
        }
        if (ImGui::Button("+ Trigger"))
        {
            AnimParameter param;
            param.Name = "NewTrigger";
            param.Type = AnimParameterType::Trigger;
            param.DefaultValue = 0.f;
            m_Controller->Parameters.push_back(param);
            m_Dirty = true;
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
        {
            m_Controller->Parameters.erase(m_Controller->Parameters.begin() + toRemove);
            m_Dirty = true;
        }
    }

    void AnimatorPanel::DrawLayers()
    {
        if (ImGui::Button("+", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        {
            AnimLayer newLayer;
            newLayer.Name = "New Layer " + std::to_string((int)m_Controller->Layers.size());
            newLayer.Weight = 1.f;
            m_Controller->Layers.push_back(newLayer);
            m_Dirty = true;
        }

        ImGui::Separator();

        for (int i = 0; i < (int)m_Controller->Layers.size(); i++)
        {
            auto& layer = m_Controller->Layers[i];
            bool isSelected = (m_SelectedLayerIndex == i);

            float availW = ImGui::GetContentRegionAvail().x;
            float dotW = 25.f;
            float selectableW = availW - dotW - 4.f;

            std::string selectLabel = layer.Name + "##ls" + std::to_string(i);
            std::string dotLabel = "...##layer" + std::to_string(i);
            std::string popupLabel = "LayerSettings##layer" + std::to_string(i);

            ImGui::PushID(i);

            ImGui::PushItemWidth(selectableW);
            if (ImGui::Selectable(selectLabel.c_str(), isSelected, 0, ImVec2(selectableW, 0)))
            {
                m_SelectedLayerIndex = i;
                m_NavigationStack.clear();
                m_InSubStateView = false;
                m_CurrentSubStateName.clear();
                m_BlendTreeView = false;
                m_BlendTreeStateName.clear();
                if (m_Controller)
                    RebuildGraphFromController(*m_Controller, i);
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 2.f);

            if (ImGui::SmallButton(dotLabel.c_str()))
            {
                ImGui::OpenPopup(popupLabel.c_str());
            }

            if (ImGui::BeginPopup(popupLabel.c_str()))
            {
                ImGui::TextDisabled(layer.Name.c_str());
                ImGui::Separator();

                ImGui::Text("Name");
                ImGui::SameLine(60.f);
                ImGui::PushItemWidth(130.f);
                char nameBuf[128];
                strncpy(nameBuf, layer.Name.c_str(), sizeof(nameBuf) - 1);
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("##lname", nameBuf, sizeof(nameBuf)))
                {
                    layer.Name = nameBuf;
                    m_Dirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::Separator();

                ImGui::Text("Weight");
                ImGui::SameLine(60.f);
                ImGui::PushItemWidth(130.f);
                if (ImGui::DragFloat("##lw", &layer.Weight, 0.01f, 0.f, 1.f))
                    m_Dirty = true;
                ImGui::PopItemWidth();

                ImGui::Text("Blending");
                ImGui::SameLine(60.f);
                ImGui::PushItemWidth(130.f);
                const char* blendModes[] = { "Override", "Additive" };
                static int blendSelection = 0;
                if (ImGui::Combo("##lb", &blendSelection, blendModes, 2))
                    m_Dirty = true;
                ImGui::PopItemWidth();

                ImGui::Text("Sync");
                ImGui::SameLine(60.f);
                static bool syncEnabled = false;
                ImGui::Checkbox("##lsync", &syncEnabled);

                if (syncEnabled)
                {
                    ImGui::Text("Timing");
                    ImGui::SameLine(60.f);
                    ImGui::PushItemWidth(130.f);
                    static int timingMode = 0;
                    const char* timingModes[] = { "Offset", "Multiplier" };
                    ImGui::Combo("##ltiming", &timingMode, timingModes, 2);
                    ImGui::PopItemWidth();
                }

                ImGui::Text("IK Pass");
                ImGui::SameLine(60.f);
                static bool ikPass = false;
                ImGui::Checkbox("##lik", &ikPass);

                ImGui::Text("Mask");
                ImGui::SameLine(60.f);
                ImGui::PushItemWidth(130.f);
                ImGui::TextDisabled("None (Avatar Mask)");
                ImGui::PopItemWidth();

                ImGui::Separator();

                if (ImGui::Button("Delete Layer") && m_Controller->Layers.size() > 1)
                {
                    m_Controller->Layers.erase(m_Controller->Layers.begin() + i);
                    if (m_SelectedLayerIndex >= (int)m_Controller->Layers.size())
                        m_SelectedLayerIndex = (int)m_Controller->Layers.size() - 1;
                    m_Dirty = true;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    void AnimatorPanel::DrawNodeEditor()
    {
        if (!m_Controller || m_Controller->Layers.empty())
        {
            ImGui::TextDisabled("No layers");
            return;
        }

        if (s_Nodes.empty())
        {
            RebuildGraphFromController(*m_Controller, m_SelectedLayerIndex);
        }

        auto& layer = m_Controller->Layers[m_SelectedLayerIndex];
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        std::vector<AnimTransition>* currentTransitions = &layer.Transitions;
        std::string currentDefaultState = layer.DefaultState;
        if (m_InSubStateView && !m_CurrentSubStateName.empty())
        {
            auto* subSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
            if (subSS)
            {
                currentTransitions = const_cast<std::vector<AnimTransition>*>(&subSS->Transitions);
                currentDefaultState = subSS->DefaultState;
                static int lastTransCount = -1;
                if (lastTransCount != (int)subSS->Transitions.size())
                {
                    CQ_CORE_INFO("SubState '{0}': {1} states, {2} transitions", m_CurrentSubStateName, (int)subSS->States.size(), (int)subSS->Transitions.size());
                    lastTransCount = (int)subSS->Transitions.size();
                }
            }
            else
            {
                CQ_CORE_WARN("SubState '{0}' NOT found by FindSubStateRecursive!", m_CurrentSubStateName);
            }
        }

        // Escape ile BlendTree/SubState'ten çık
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (m_BlendTreeView)
            {
                m_BlendTreeView = false;
                m_BlendTreeStateName.clear();
                m_InSubStateView = false;
                m_CurrentSubStateName.clear();
                RebuildGraphFromController(*m_Controller, m_SelectedLayerIndex);
                return;
            }
            else if (!m_NavigationStack.empty())
            {
                m_NavigationStack.pop_back();
                if (m_NavigationStack.empty())
                {
                    m_InSubStateView = false;
                    m_CurrentSubStateName.clear();
                    RebuildGraphFromController(*m_Controller, m_SelectedLayerIndex);
                }
                else
                {
                    m_CurrentSubStateName = m_NavigationStack.back().Name;
                    RebuildGraphFromSubStateByName(m_CurrentSubStateName);
                }
                return;
            }
        }

        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 localMouse = { mousePos.x - canvasPos.x, mousePos.y - canvasPos.y };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(18, 18, 22, 200));

        float gridSize = 20.f;
        for (float x = 0; x < canvasSize.x; x += gridSize)
            dl->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), IM_COL32(40, 40, 45, 100));
        for (float y = 0; y < canvasSize.y; y += gridSize)
            dl->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), IM_COL32(40, 40, 45, 100));

        bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        bool mouseInCanvas = localMouse.x >= 0 && localMouse.x <= canvasSize.x &&
                             localMouse.y >= 0 && localMouse.y <= canvasSize.y;

        int hoveredNodeID = -1;
        for (auto& node : s_Nodes)
        {
            if (IsMouseOverNode(node, localMouse))
            {
                hoveredNodeID = node.ID;
                break;
            }
        }

        // Transition creation mode — draw line from source to mouse
        if (m_TransitionMode)
        {
            auto* srcNode = FindNodeByID(m_TransitionSourceNodeID);
            if (srcNode)
            {
                ImVec2 srcPt = ImVec2(canvasPos.x + srcNode->EditorPosition.x + NODE_W, canvasPos.y + srcNode->EditorPosition.y + NODE_H / 2.f);
                dl->AddLine(srcPt, mousePos, IM_COL32(255, 255, 0, 200), 2.0f);
                DrawArrow(dl, srcPt, mousePos, IM_COL32(255, 255, 0, 200), ARROW_SIZE);
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                // Tıklanan node'u bul
                int clickedNode = -1;
                for (auto& node : s_Nodes)
                {
                    if (IsMouseOverNode(node, localMouse))
                    {
                        clickedNode = node.ID;
                        break;
                    }
                }

                if (clickedNode != -1 && clickedNode != m_TransitionSourceNodeID)
                {
                    auto* srcNode = FindNodeByID(m_TransitionSourceNodeID);
                    auto* dstNode = FindNodeByID(clickedNode);
                    if (srcNode && dstNode &&
                        srcNode->Type != AnimNodeType::Exit &&
                        dstNode->Type != AnimNodeType::Entry)
                    {
                        AddTransition(srcNode->StateName.c_str(), dstNode->StateName.c_str());
                    }
                }
                m_TransitionMode = false;
                m_TransitionSourceNodeID = -1;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_TransitionMode = false;
                m_TransitionSourceNodeID = -1;
            }
        }

        // Draw transitions
        for (int ti = 0; ti < (int)currentTransitions->size(); ti++)
        {
            auto& trans = (*currentTransitions)[ti];
            auto* fromNode = FindNodeByStateName(trans.FromState);
            auto* toNode = FindNodeByStateName(trans.ToState);
            if (!fromNode || !toNode) continue;

            float offset = 0.f;
            for (int tj = 0; tj < (int)currentTransitions->size(); tj++)
            {
                if (ti == tj) continue;
                auto& other = (*currentTransitions)[tj];
                if (other.FromState == trans.ToState && other.ToState == trans.FromState)
                {
                    offset = (ti < tj) ? -TRANSITION_OFFSET : TRANSITION_OFFSET;
                    break;
                }
            }

            bool isSelected = (m_SelectedTransitionFrom == trans.FromState && m_SelectedTransitionTo == trans.ToState);
            ImU32 color = isSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(150, 150, 200, 255);

            if (fromNode == toNode)
            {
                ImVec2 fromPt = ImVec2(canvasPos.x + fromNode->EditorPosition.x + NODE_W, canvasPos.y + fromNode->EditorPosition.y + 10.f);
                ImVec2 toPt = ImVec2(canvasPos.x + toNode->EditorPosition.x + NODE_W, canvasPos.y + toNode->EditorPosition.y + NODE_H - 10.f);
                DrawTransitionLine(dl, fromPt, toPt, color, offset);

                if (!m_TransitionMode && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (DistToSegment(mousePos, fromPt, toPt) < HIT_THRESHOLD)
                    {
                        m_SelectedTransitionFrom = trans.FromState;
                        m_SelectedTransitionTo = trans.ToState;
                        m_SelectedNodeID = -1;
                        m_SelectedStateName.clear();
                        auto& info = GetSelectionInfo();
                        info.HasSelection = true;
                        info.IsState = false;
                        info.IsTransition = true;
                        info.StateName.clear();
                        info.TransitionFrom = trans.FromState;
                        info.TransitionTo = trans.ToState;
                    }
                }
            }
            else
            {
                // Kaydedilmiş kenar bilgisini bul
                std::string leftState, rightState;
                bool found = false;
                for (auto& edge : s_TransitionEdges)
                {
                    if ((edge.LeftState == trans.FromState && edge.RightState == trans.ToState) ||
                        (edge.LeftState == trans.ToState && edge.RightState == trans.FromState))
                    {
                        leftState = edge.LeftState;
                        rightState = edge.RightState;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    leftState = (fromNode->EditorPosition.x <= toNode->EditorPosition.x) ? trans.FromState : trans.ToState;
                    rightState = (fromNode->EditorPosition.x <= toNode->EditorPosition.x) ? trans.ToState : trans.FromState;
                }

                auto* leftNode = FindNodeByStateName(leftState);
                auto* rightNode = FindNodeByStateName(rightState);
                if (!leftNode || !rightNode) continue;

                ImVec2 leftPt = ImVec2(canvasPos.x + leftNode->EditorPosition.x + NODE_W, canvasPos.y + leftNode->EditorPosition.y + NODE_H / 2.f);
                ImVec2 rightPt = ImVec2(canvasPos.x + rightNode->EditorPosition.x, canvasPos.y + rightNode->EditorPosition.y + NODE_H / 2.f);

                DrawTransitionLine(dl, leftPt, rightPt, color, offset);

                if (!m_TransitionMode && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    float dx = rightPt.x - leftPt.x;
                    float dy = rightPt.y - leftPt.y;
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 1e-3f)
                    {
                        float nx = -dy / len;
                        float ny = dx / len;
                        ImVec2 oFrom = { leftPt.x + nx * offset, leftPt.y + ny * offset };
                        ImVec2 oTo = { rightPt.x + nx * offset, rightPt.y + ny * offset };
                        if (DistToSegment(mousePos, oFrom, oTo) < HIT_THRESHOLD)
                        {
                            m_SelectedTransitionFrom = trans.FromState;
                            m_SelectedTransitionTo = trans.ToState;
                            m_SelectedNodeID = -1;
                            m_SelectedStateName.clear();
                            auto& info = GetSelectionInfo();
                            info.HasSelection = true;
                            info.IsState = false;
                            info.IsTransition = true;
                            info.StateName.clear();
                            info.TransitionFrom = trans.FromState;
                            info.TransitionTo = trans.ToState;
                        }
                    }
                }
            }
        }

        // Draw Entry → DefaultState line
        if (!currentDefaultState.empty())
        {
            auto* entryNode = FindNodeByStateName("Entry");
            auto* defaultNode = FindNodeByStateName(currentDefaultState);
            if (entryNode && defaultNode)
            {
                ImVec2 fromPt = ImVec2(canvasPos.x + entryNode->EditorPosition.x + NODE_W, canvasPos.y + entryNode->EditorPosition.y + NODE_H / 2.f);
                ImVec2 toPt = ImVec2(canvasPos.x + defaultNode->EditorPosition.x, canvasPos.y + defaultNode->EditorPosition.y + NODE_H / 2.f);
                DrawTransitionLine(dl, fromPt, toPt, IM_COL32(80, 200, 80, 200), 0.f);
            }
        }

        // Draw nodes
        for (auto& node : s_Nodes)
        {
            ImVec2 pos = ImVec2(canvasPos.x + node.EditorPosition.x, canvasPos.y + node.EditorPosition.y);
            ImVec2 size = ImVec2(NODE_W, NODE_H);

            ImU32 bgColor, borderColor;
            if (node.Type == AnimNodeType::Entry)
            {
                bgColor = IM_COL32(30, 100, 30, 255);
                borderColor = IM_COL32(50, 160, 50, 255);
            }
            else if (node.Type == AnimNodeType::Exit)
            {
                bgColor = IM_COL32(100, 30, 30, 255);
                borderColor = IM_COL32(160, 50, 50, 255);
            }
            else if (node.Type == AnimNodeType::AnyState)
            {
                bgColor = IM_COL32(100, 100, 30, 255);
                borderColor = IM_COL32(160, 160, 50, 255);
            }
            else if (node.Type == AnimNodeType::SubState)
            {
                bgColor = IM_COL32(60, 80, 120, 255);
                borderColor = IM_COL32(100, 140, 200, 255);
            }
            else if (node.Type == AnimNodeType::UpNode)
            {
                bgColor = IM_COL32(180, 100, 30, 255);
                borderColor = IM_COL32(220, 140, 50, 255);
            }
            else if (node.Type == AnimNodeType::BlendTree)
            {
                bgColor = IM_COL32(40, 40, 130, 255);
                borderColor = IM_COL32(100, 100, 220, 255);
            }
            else
            {
                bool isDefault = (currentDefaultState == node.StateName);
                bool isSelected = false;
                for (int id : m_SelectedNodeIDs)
                {
                    if (id == node.ID) { isSelected = true; break; }
                }
                bgColor = isSelected ? IM_COL32(60, 60, 180, 255) : IM_COL32(40, 40, 130, 255);
                borderColor = isDefault ? IM_COL32(80, 220, 80, 255) : (isSelected ? IM_COL32(200, 200, 100, 255) : IM_COL32(70, 70, 170, 255));
            }

            if (node.Type == AnimNodeType::SubState || node.Type == AnimNodeType::UpNode)
            {
                float hex = NODE_H * 0.4f;
                ImVec2 pts[6] = {
                    { pos.x + hex, pos.y },
                    { pos.x + size.x - hex, pos.y },
                    { pos.x + size.x, pos.y + size.y * 0.5f },
                    { pos.x + size.x - hex, pos.y + size.y },
                    { pos.x + hex, pos.y + size.y },
                    { pos.x, pos.y + size.y * 0.5f }
                };
                dl->AddConvexPolyFilled(pts, 6, bgColor);
                dl->AddPolyline(pts, 6, borderColor, ImDrawFlags_Closed, 1.5f);
            }
            else if (node.Type == AnimNodeType::BlendTree)
            {
                if (m_BlendTreeView)
                {
                    float btW = 220.f;
                    float btH = 80.f;
                    ImVec2 btPos = ImVec2(pos.x + (NODE_W - btW) * 0.5f, pos.y + (NODE_H - btH) * 0.5f);

                    dl->AddRectFilled(btPos, ImVec2(btPos.x + btW, btPos.y + btH), bgColor, 4.f);
                    dl->AddRect(btPos, ImVec2(btPos.x + btW, btPos.y + btH), borderColor, 4.f, 0, 1.5f);

                    dl->AddRectFilled(btPos, ImVec2(btPos.x + btW, btPos.y + NODE_TITLE_H), IM_COL32(0, 0, 0, 60), 4.f);
                    ImVec2 btTextSize = ImGui::CalcTextSize("Blend Tree");
                    dl->AddText(ImVec2(btPos.x + (btW - btTextSize.x) * 0.5f, btPos.y + 5.f), IM_COL32(255, 255, 255, 255), "Blend Tree");

                    if (m_Controller && !m_Controller->Parameters.empty())
                    {
                        ImGui::SetCursorScreenPos(ImVec2(btPos.x + 8.f, btPos.y + NODE_TITLE_H + 6.f));
                        ImGui::BeginChild("##bt_params", ImVec2(btW - 16.f, btH - NODE_TITLE_H - 10.f), false, ImGuiWindowFlags_NoScrollbar);
                        for (int pi = 0; pi < (int)m_Controller->Parameters.size() && pi < 3; pi++)
                        {
                            auto& param = m_Controller->Parameters[pi];
                            ImGui::PushID(pi + node.ID * 100);

                            ImGui::Text("%s", param.Name.c_str());
                            ImGui::SameLine(85.f);
                            ImGui::PushItemWidth(btW - 100.f);

                            if (param.Type == AnimParameterType::Float)
                            {
                                if (ImGui::DragFloat("##val", &param.DefaultValue, 0.01f))
                                    m_Dirty = true;
                            }
                            else if (param.Type == AnimParameterType::Int)
                            {
                                int intVal = (int)param.DefaultValue;
                                if (ImGui::DragInt("##val", &intVal))
                                {
                                    param.DefaultValue = (float)intVal;
                                    m_Dirty = true;
                                }
                            }
                            else if (param.Type == AnimParameterType::Bool)
                            {
                                bool boolVal = param.DefaultValue > 0.5f;
                                if (ImGui::Checkbox("##val", &boolVal))
                                {
                                    param.DefaultValue = boolVal ? 1.f : 0.f;
                                    m_Dirty = true;
                                }
                            }

                            ImGui::PopItemWidth();
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                }
                else
                {
                    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 4.f);
                    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 4.f, 0, 1.5f);
                }
            }
            else
            {
                dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 4.f);
                dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 4.f, 0, 1.5f);
            }

            // Title bar
            if (node.Type != AnimNodeType::SubState && node.Type != AnimNodeType::UpNode && node.Type != AnimNodeType::BlendTree)
                dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + NODE_TITLE_H), IM_COL32(0, 0, 0, 60), 4.f);

            const char* icon = "";
            if (node.Type == AnimNodeType::Entry) icon = ">> ";
            else if (node.Type == AnimNodeType::Exit) icon = "XX ";
            else if (node.Type == AnimNodeType::AnyState) icon = "** ";

            char label[128];
            snprintf(label, sizeof(label), "%s%s", icon, node.StateName.c_str());

            if (node.Type == AnimNodeType::SubState || node.Type == AnimNodeType::UpNode)
            {
                ImVec2 textSize = ImGui::CalcTextSize(label);
                float textX = pos.x + (size.x - textSize.x) * 0.5f;
                float textY = pos.y + size.y * 0.5f - textSize.y * 0.5f;
                dl->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), label);
            }
            else if (!(node.Type == AnimNodeType::BlendTree && m_BlendTreeView))
            {
                dl->AddText(ImVec2(pos.x + 8.f, pos.y + 5.f), IM_COL32(255, 255, 255, 255), label);
            }
        }

        // Right-click context menu
        static int rightClickNodeID = -1;
        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_TransitionMode)
        {
            rightClickNodeID = hoveredNodeID;
            ImGui::OpenPopup("NodeContextMenu");
        }

        if (ImGui::BeginPopup("NodeContextMenu"))
        {
            if (rightClickNodeID != -1)
            {
                auto* node = FindNodeByID(rightClickNodeID);
                if (node && node->Type == AnimNodeType::State)
                {
                    ImGui::TextDisabled(node->StateName.c_str());
                    ImGui::Separator();

                    if (ImGui::MenuItem("Add Transition"))
                    {
                        m_TransitionMode = true;
                        m_TransitionSourceNodeID = rightClickNodeID;
                        m_Dirty = true;
                    }

                    {
                        bool isDefault = (currentDefaultState == node->StateName);
                        if (isDefault) ImGui::BeginDisabled();
                        if (ImGui::MenuItem("Set as Layer Default State"))
                        {
                            if (m_InSubStateView && !m_CurrentSubStateName.empty())
                            {
                                auto* subSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
                                if (subSS) const_cast<AnimSubStateData*>(subSS)->DefaultState = node->StateName;
                            }
                            else
                            {
                                layer.DefaultState = node->StateName;
                            }
                            m_Dirty = true;
                        }
                        if (isDefault) ImGui::EndDisabled();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Copy"))
                    {
                        m_ClipboardStateName = node->StateName;
                        m_ClipboardIsStateMachine = false;
                        m_ClipboardStates.clear();
                        m_HasClipboard = true;
                    }

                    if (ImGui::MenuItem("Duplicate"))
                    {
                        std::string newName = node->StateName + "_copy";
                        AddState(newName.c_str(), node->EditorPosition.x + 40.f, node->EditorPosition.y + 40.f);
                        m_Dirty = true;
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Delete State"))
                    {
                        RemoveState(node->StateName.c_str());
                        m_SelectedNodeID = -1;
                        m_SelectedStateName.clear();
                        m_SelectedTransitionFrom.clear();
                        m_SelectedTransitionTo.clear();
                        m_Dirty = true;
                    }
                }
                else if (node && node->Type == AnimNodeType::Entry)
                {
                    ImGui::TextDisabled("Entry");
                    ImGui::Separator();

                    if (ImGui::MenuItem("Add Transition"))
                    {
                        m_TransitionMode = true;
                        m_TransitionSourceNodeID = rightClickNodeID;
                        m_Dirty = true;
                    }

                    {
                        bool isDefault = (layer.DefaultState.empty());
                        if (isDefault) ImGui::BeginDisabled();
                        if (ImGui::MenuItem("Set StateMachine Default State"))
                        {
                            if (!layer.States.empty())
                            {
                                layer.DefaultState = layer.States[0].Name;
                                m_Dirty = true;
                            }
                        }
                        if (isDefault) ImGui::EndDisabled();
                    }
                }
                else if (node && node->Type == AnimNodeType::SubState)
                {
                    ImGui::TextDisabled(node->StateName.c_str());
                    ImGui::Separator();

                    if (ImGui::MenuItem("Make Transition"))
                    {
                        m_TransitionMode = true;
                        m_TransitionSourceNodeID = rightClickNodeID;
                        m_Dirty = true;
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Copy"))
                    {
                        m_ClipboardStates.clear();
                        m_ClipboardIsStateMachine = true;
                        m_ClipboardStateName = node->StateName;
                        m_HasClipboard = true;
                    }

                    if (ImGui::MenuItem("Duplicate"))
                    {
                        int nodeID = s_NextGraphID++;
                        std::string newName = node->StateName + "_copy";
                        s_Nodes.emplace_back(nodeID, AnimNodeType::SubState, newName, glm::vec2(node->EditorPosition.x + 40.f, node->EditorPosition.y + 40.f));
                        m_Dirty = true;
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Delete"))
                    {
                        // Controller'dan sil
                        m_Controller->Layers[m_SelectedLayerIndex].SubStates.erase(
                            std::remove_if(m_Controller->Layers[m_SelectedLayerIndex].SubStates.begin(), m_Controller->Layers[m_SelectedLayerIndex].SubStates.end(),
                                [node](const AnimSubStateData& ss) { return ss.Name == node->StateName; }),
                            m_Controller->Layers[m_SelectedLayerIndex].SubStates.end());

                        s_Nodes.erase(
                            std::remove_if(s_Nodes.begin(), s_Nodes.end(),
                                [rightClickNodeID](const AnimGraphNode& n) { return n.ID == rightClickNodeID; }),
                            s_Nodes.end());
                        m_SelectedNodeID = -1;
                        m_SelectedStateName.clear();
                        m_SelectedTransitionFrom.clear();
                        m_SelectedTransitionTo.clear();
                        m_Dirty = true;
                    }
                }
                else if (node && node->Type == AnimNodeType::AnyState)
                {
                    ImGui::TextDisabled("Any State");
                    ImGui::Separator();

                    if (ImGui::MenuItem("Add Transition"))
                    {
                        m_TransitionMode = true;
                        m_TransitionSourceNodeID = rightClickNodeID;
                        m_Dirty = true;
                    }
                }
                else if (node && node->Type == AnimNodeType::Exit)
                {
                    ImGui::TextDisabled("Exit");
                }
            }
            else
            {
                if (ImGui::BeginMenu("Create State"))
                {
                    if (ImGui::MenuItem("From New"))
                    {
                        std::string name = "State_" + std::to_string(s_NextGraphID);
                        AddState(name.c_str(), localMouse.x, localMouse.y);
                        m_Dirty = true;
                    }

                    if (ImGui::MenuItem("From Selected Clip"))
                    {
                        std::string name = "State_" + std::to_string(s_NextGraphID);
                        AddState(name.c_str(), localMouse.x, localMouse.y);
                        m_Dirty = true;
                    }

                    if (ImGui::MenuItem("From New Blend Tree"))
                    {
                        std::string name = "BlendTree_" + std::to_string(s_NextGraphID);
                        int nodeID = s_NextGraphID++;
                        AnimState newState;
                        newState.Name = name;
                        newState.Type = AnimStateType::BlendTree;
                        newState.EditorPosition = { localMouse.x, localMouse.y };

                        if (m_InSubStateView && !m_CurrentSubStateName.empty())
                        {
                            auto* targetSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
                            if (targetSS)
                            {
                                auto* mutableSS = const_cast<AnimSubStateData*>(targetSS);
                                bool isFirst = mutableSS->States.empty();
                                mutableSS->States.push_back(newState);
                                if (isFirst) mutableSS->DefaultState = name;
                            }
                        }
                        else
                        {
                            auto& layer = m_Controller->Layers[m_SelectedLayerIndex];
                            bool isFirst = layer.States.empty();
                            layer.States.push_back(newState);
                            if (isFirst) layer.DefaultState = name;
                        }

                        s_Nodes.emplace_back(nodeID, AnimNodeType::BlendTree, name, glm::vec2(localMouse.x, localMouse.y));
                        m_Dirty = true;
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Create Sub-State Machine"))
                {
                    std::string name = "StateMachine_" + std::to_string(s_NextGraphID);
                    int nodeID = s_NextGraphID++;
                    s_Nodes.emplace_back(nodeID, AnimNodeType::SubState, name, glm::vec2(localMouse.x, localMouse.y));
                    AnimSubStateData ss;
                    ss.Name = name;
                    ss.EditorPosition = { localMouse.x, localMouse.y };

                    if (m_InSubStateView && !m_CurrentSubStateName.empty())
                    {
                        auto* parentSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
                        if (parentSS)
                        {
                            const_cast<AnimSubStateData*>(parentSS)->SubStates.push_back(ss);
                        }
                    }
                    else
                    {
                        m_Controller->Layers[m_SelectedLayerIndex].SubStates.push_back(ss);
                    }
                    m_Dirty = true;
                }

                ImGui::Separator();

                if (m_HasClipboard)
                {
                    if (ImGui::MenuItem("Paste"))
                    {
                        if (m_ClipboardIsStateMachine)
                        {
                            int nodeID = s_NextGraphID++;
                            s_Nodes.emplace_back(nodeID, AnimNodeType::SubState, m_ClipboardStateName, glm::vec2(localMouse.x, localMouse.y));
                            AnimSubStateData ss;
                            ss.Name = m_ClipboardStateName;
                            ss.EditorPosition = { localMouse.x, localMouse.y };
                            ss.States = m_ClipboardStates;
                            ss.Transitions = m_ClipboardTransitions;
                            ss.SubStates = m_ClipboardSubStates;

                            if (m_InSubStateView && !m_CurrentSubStateName.empty())
                            {
                                auto* parentSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
                                if (parentSS)
                                {
                                    const_cast<AnimSubStateData*>(parentSS)->SubStates.push_back(ss);
                                }
                            }
                            else
                            {
                                m_Controller->Layers[m_SelectedLayerIndex].SubStates.push_back(ss);
                            }
                        }
                        else
                        {
                            for (auto& s : layer.States)
                            {
                                if (s.Name == m_ClipboardStateName)
                                {
                                    std::string newName = s.Name + "_copy";
                                    AnimState newState = s;
                                    newState.Name = newName;
                                    newState.EditorPosition = { localMouse.x, localMouse.y };
                                    layer.States.push_back(newState);
                                    int nodeID = s_NextGraphID++;
                                    s_Nodes.emplace_back(nodeID, AnimNodeType::State, newName, glm::vec2(localMouse.x, localMouse.y));
                                    break;
                                }
                            }
                        }
                        m_Dirty = true;
                    }
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("Paste");
                    ImGui::EndDisabled();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Copy current StateMachine"))
                {
                    m_ClipboardStates.clear();
                    m_ClipboardTransitions.clear();
                    m_ClipboardSubStates.clear();
                    for (auto& s : layer.States)
                    {
                        m_ClipboardStates.push_back(s);
                    }
                    for (auto& t : layer.Transitions)
                    {
                        m_ClipboardTransitions.push_back(t);
                    }
                    for (auto& ss : m_Controller->Layers[m_SelectedLayerIndex].SubStates)
                    {
                        m_ClipboardSubStates.push_back(ss);
                    }
                    m_ClipboardIsStateMachine = true;
                    m_ClipboardStateName = layer.Name;
                    m_HasClipboard = true;
                }
            }
            ImGui::EndPopup();
        }

        // Left-click: node selection OR box selection start
        static int draggingNodeID = -1;
        static ImVec2 dragOffset = {0.f, 0.f};
        static bool boxSelecting = false;
        static ImVec2 boxStart = {0.f, 0.f};

        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_TransitionMode && !ImGui::GetIO().KeyAlt)
        {
            if (hoveredNodeID != -1)
            {
                // Çift tıklama — SubState'e gir veya UpNode ile çık
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    auto* node = FindNodeByID(hoveredNodeID);
                    if (node && node->Type == AnimNodeType::SubState)
                    {
                        bool isLayer = false;
                        for (const auto& lyr : m_Controller->Layers)
                        {
                            if (lyr.Name == node->StateName) { isLayer = true; break; }
                        }
                        m_NavigationStack.push_back({ hoveredNodeID, node->StateName, isLayer });
                        m_InSubStateView = true;
                        m_CurrentSubStateName = node->StateName;
                        RebuildGraphFromSubState(hoveredNodeID);
                        m_SelectedNodeID = -1;
                        m_SelectedNodeIDs.clear();
                        m_SelectedTransitionFrom.clear();
                        m_SelectedTransitionTo.clear();
                    }
                    else if (node && node->Type == AnimNodeType::BlendTree && m_NavigationStack.empty())
                    {
                        // BlendTree görünümüne gir
                        m_BlendTreeView = true;
                        m_BlendTreeStateName = node->StateName;
                        RebuildGraphFromBlendTree(node->StateName);
                        m_SelectedNodeID = -1;
                        m_SelectedNodeIDs.clear();
                        m_SelectedTransitionFrom.clear();
                        m_SelectedTransitionTo.clear();
                    }
            else if (node && node->Type == AnimNodeType::UpNode)
                    {
                        if (m_BlendTreeView)
                        {
                            m_BlendTreeView = false;
                            m_BlendTreeStateName.clear();
                            m_InSubStateView = false;
                            m_CurrentSubStateName.clear();
                            RebuildGraphFromController(*m_Controller, m_SelectedLayerIndex);
                        }
                        else if (!m_NavigationStack.empty())
                        {
                            m_NavigationStack.pop_back();
                            if (m_NavigationStack.empty())
                            {
                                m_InSubStateView = false;
                                m_CurrentSubStateName.clear();
                                RebuildGraphFromController(*m_Controller, m_SelectedLayerIndex);
                            }
                            else
                            {
                                m_CurrentSubStateName = m_NavigationStack.back().Name;
                                RebuildGraphFromSubStateByName(m_CurrentSubStateName);
                            }
                        }
                        m_SelectedNodeID = -1;
                        m_SelectedNodeIDs.clear();
                        m_SelectedTransitionFrom.clear();
                        m_SelectedTransitionTo.clear();
                    }
                    return;
                }

                // Tek tıklama — seçim
                bool alreadySelected = false;
                for (int id : m_SelectedNodeIDs)
                {
                    if (id == hoveredNodeID) { alreadySelected = true; break; }
                }

                if (!alreadySelected)
                {
                    m_SelectedNodeIDs.clear();
                    m_SelectedNodeIDs.push_back(hoveredNodeID);
                }

                m_SelectedNodeID = hoveredNodeID;
                m_SelectedTransitionFrom.clear();
                m_SelectedTransitionTo.clear();
                auto* node = FindNodeByID(hoveredNodeID);
                if (node)
                {
                    m_SelectedStateName = node->StateName;
                    auto& info = GetSelectionInfo();
                    info.HasSelection = true;
                    info.IsState = (node->Type == AnimNodeType::State);
                    info.IsTransition = false;
                    info.StateName = node->StateName;
                    info.TransitionFrom.clear();
                    info.TransitionTo.clear();
                }

                // Start dragging all selected nodes
                draggingNodeID = hoveredNodeID;
                dragOffset = { localMouse.x - node->EditorPosition.x, localMouse.y - node->EditorPosition.y };
            }
            else
            {
                m_SelectedNodeIDs.clear();
                m_SelectedNodeID = -1;
                m_SelectedStateName.clear();
                boxSelecting = true;
                boxStart = localMouse;
            }
        }

        // Box selection drawing and finalization
        if (boxSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 boxMin = { std::min(boxStart.x, localMouse.x), std::min(boxStart.y, localMouse.y) };
            ImVec2 boxMax = { std::max(boxStart.x, localMouse.x), std::max(boxStart.y, localMouse.y) };

            dl->AddRect(
                ImVec2(canvasPos.x + boxMin.x, canvasPos.y + boxMin.y),
                ImVec2(canvasPos.x + boxMax.x, canvasPos.y + boxMax.y),
                IM_COL32(100, 150, 255, 180), 0.f, 0, 1.5f);
            dl->AddRectFilled(
                ImVec2(canvasPos.x + boxMin.x, canvasPos.y + boxMin.y),
                ImVec2(canvasPos.x + boxMax.x, canvasPos.y + boxMax.y),
                IM_COL32(100, 150, 255, 30));

            float edgeMargin = 40.f;
            float panSpeed = 8.f;
            ImVec2 panDelta = {0.f, 0.f};

            if (localMouse.x < edgeMargin)
                panDelta.x = panSpeed;
            else if (localMouse.x > canvasSize.x - edgeMargin)
                panDelta.x = -panSpeed;

            if (localMouse.y < edgeMargin)
                panDelta.y = panSpeed;
            else if (localMouse.y > canvasSize.y - edgeMargin)
                panDelta.y = -panSpeed;

            if (panDelta.x != 0.f || panDelta.y != 0.f)
            {
                for (auto& n : s_Nodes)
                {
                    n.EditorPosition.x += panDelta.x;
                    n.EditorPosition.y += panDelta.y;
                }
                boxStart.x += panDelta.x;
                boxStart.y += panDelta.y;
            }
        }
        if (boxSelecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 boxMin = { std::min(boxStart.x, localMouse.x), std::min(boxStart.y, localMouse.y) };
            ImVec2 boxMax = { std::max(boxStart.x, localMouse.x), std::max(boxStart.y, localMouse.y) };

            m_SelectedNodeIDs.clear();
            for (auto& node : s_Nodes)
            {
                float nx = node.EditorPosition.x;
                float ny = node.EditorPosition.y;
                if (nx + NODE_W > boxMin.x && nx < boxMax.x && ny + NODE_H > boxMin.y && ny < boxMax.y)
                {
                    m_SelectedNodeIDs.push_back(node.ID);
                }
            }
            boxSelecting = false;
        }

        // Node dragging (all selected nodes)
        if (draggingNodeID != -1 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !boxSelecting)
        {
            auto* node = FindNodeByID(draggingNodeID);
            if (node)
            {
                float newX = localMouse.x - dragOffset.x;
                float newY = localMouse.y - dragOffset.y;
                float dx = newX - node->EditorPosition.x;
                float dy = newY - node->EditorPosition.y;

                for (int id : m_SelectedNodeIDs)
                {
                    auto* n = FindNodeByID(id);
                    if (n)
                    {
                        n->EditorPosition.x += dx;
                        n->EditorPosition.y += dy;
                    }
                }

                float edgeMargin = 40.f;
                float panSpeed = 8.f;
                ImVec2 panDelta = {0.f, 0.f};

                if (localMouse.x < edgeMargin)
                    panDelta.x = panSpeed;
                else if (localMouse.x > canvasSize.x - edgeMargin)
                    panDelta.x = -panSpeed;

                if (localMouse.y < edgeMargin)
                    panDelta.y = panSpeed;
                else if (localMouse.y > canvasSize.y - edgeMargin)
                    panDelta.y = -panSpeed;

                if (panDelta.x != 0.f || panDelta.y != 0.f)
                {
                    for (auto& n : s_Nodes)
                    {
                        if (std::find(m_SelectedNodeIDs.begin(), m_SelectedNodeIDs.end(), n.ID) == m_SelectedNodeIDs.end())
                        {
                            n.EditorPosition.x += panDelta.x;
                            n.EditorPosition.y += panDelta.y;
                        }
                    }
                }
            }
        }
        else
        {
            draggingNodeID = -1;
        }

        // Alt + Left-click canvas panning
        static ImVec2 panStart = {0.f, 0.f};
        static bool isPanning = false;

        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt)
        {
            isPanning = true;
            panStart = mousePos;
        }

        if (isPanning && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = { mousePos.x - panStart.x, mousePos.y - panStart.y };
            for (auto& node : s_Nodes)
            {
                node.EditorPosition.x += delta.x;
                node.EditorPosition.y += delta.y;
            }
            panStart = mousePos;
        }
        else
        {
            isPanning = false;
        }

        // Scroll wheel panning
        if (mouseInCanvas && ImGui::GetIO().MouseWheel != 0.f)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            for (auto& node : s_Nodes)
            {
                node.EditorPosition.y += wheel * 30.f;
            }
        }

        // Seçim bilgisini güncelle — hiçbir şey seçilmediyse temizle
        if (m_SelectedNodeID == -1 && m_SelectedTransitionFrom.empty())
        {
            auto& info = GetSelectionInfo();
            info.HasSelection = false;
            info.IsState = false;
            info.IsTransition = false;
            info.StateName.clear();
            info.TransitionFrom.clear();
            info.TransitionTo.clear();
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
                auto& layer = m_Controller->Layers[m_SelectedLayerIndex];
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
                        m_Dirty = true;
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
                    if (ImGui::IsItemDeactivatedAfterEdit()) m_Dirty = true;
                    ImGui::Checkbox("Loop", &state->Loop);
                    if (ImGui::IsItemDeactivatedAfterEdit()) m_Dirty = true;

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
                        m_Dirty = true;
                    }
                    }

                    ImGui::Separator();

                    if (ImGui::Button("Delete State"))
                    {
                        RemoveState(state->Name.c_str());
                        m_SelectedNodeID = -1;
                        m_SelectedStateName.clear();
                        m_Dirty = true;
                    }
                }
            }
            else if (node && node->Type == AnimNodeType::Entry)
            {
                ImGui::Text("Entry Node");
                ImGui::TextDisabled("Connects to Default State");
                if (m_Controller && !m_Controller->Layers.empty())
                {
                    ImGui::Text("Default: %s", m_Controller->Layers[m_SelectedLayerIndex].DefaultState.c_str());
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
        else if (!m_SelectedTransitionFrom.empty() && !m_SelectedTransitionTo.empty() && m_Controller && !m_Controller->Layers.empty())
        {
            ImGui::Text("Transition");
            ImGui::Text("From: %s", m_SelectedTransitionFrom.c_str());
            ImGui::Text("To: %s", m_SelectedTransitionTo.c_str());

            auto& layer = m_Controller->Layers[m_SelectedLayerIndex];
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
                if (ImGui::IsItemDeactivatedAfterEdit()) m_Dirty = true;
                ImGui::Checkbox("Has Exit Time", &trans->HasExitTime);
                if (ImGui::IsItemDeactivatedAfterEdit()) m_Dirty = true;
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
                    m_Dirty = true;
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
                {
                    trans->Conditions.erase(trans->Conditions.begin() + condToRemove);
                    m_Dirty = true;
                }

                ImGui::Separator();

                if (ImGui::Button("Delete Transition"))
                {
                    layer.Transitions.erase(
                        std::remove_if(layer.Transitions.begin(), layer.Transitions.end(),
                            [&](const AnimTransition& t) {
                                return t.FromState == m_SelectedTransitionFrom && t.ToState == m_SelectedTransitionTo;
                            }),
                        layer.Transitions.end());

                    s_TransitionEdges.erase(
                        std::remove_if(s_TransitionEdges.begin(), s_TransitionEdges.end(),
                            [&](const TransitionEdge& e) {
                                return (e.LeftState == m_SelectedTransitionFrom && e.RightState == m_SelectedTransitionTo) ||
                                       (e.LeftState == m_SelectedTransitionTo && e.RightState == m_SelectedTransitionFrom);
                            }),
                        s_TransitionEdges.end());

                    m_SelectedTransitionFrom.clear();
                    m_SelectedTransitionTo.clear();
                    m_Dirty = true;
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

        // Layer'a ekle (SubState içinde olsak da olmasak da)
        if (m_Controller->Layers.empty())
        {
            AnimLayer layer;
            layer.Name = "Base Layer";
            m_Controller->Layers.push_back(layer);
        }

        // SubState içinde miyiz? Hangi layer'a ekleyeceğimizi bul
        int targetLayerIdx = m_SelectedLayerIndex;
        if (m_InSubStateView && !m_CurrentSubStateName.empty())
        {
            for (int i = 0; i < (int)m_Controller->Layers.size(); i++)
            {
                if (m_Controller->Layers[i].Name == m_CurrentSubStateName)
                {
                    targetLayerIdx = i;
                    break;
                }
            }

            // SubState içine ekle
            auto* targetSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
            if (targetSS)
            {
                auto* mutableSS = const_cast<AnimSubStateData*>(targetSS);
                bool isFirst = mutableSS->States.empty();
                AnimState newState;
                newState.Name = name;
                newState.EditorPosition = { x, y };
                mutableSS->States.push_back(newState);
                if (isFirst)
                {
                    mutableSS->DefaultState = name;
                }
                int nodeID = s_NextGraphID++;
                s_Nodes.emplace_back(nodeID, AnimNodeType::State, name, glm::vec2(x, y));
                return;
            }
        }

        AnimState state;
        state.Name = name;
        state.EditorPosition = { x, y };
        auto& targetLayer = m_Controller->Layers[targetLayerIdx];
        bool isFirst = targetLayer.States.empty();
        targetLayer.States.push_back(state);
        if (isFirst)
        {
            targetLayer.DefaultState = name;
        }

        int nodeID = s_NextGraphID++;
        s_Nodes.emplace_back(nodeID, AnimNodeType::State, name, glm::vec2(x, y));
    }

    void AnimatorPanel::AddTransition(const char* from, const char* to)
    {
        if (!m_Controller || m_Controller->Layers.empty()) return;

        if (m_InSubStateView && !m_CurrentSubStateName.empty())
        {
            auto* targetSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
            if (targetSS)
            {
                for (const auto& t : targetSS->Transitions)
                {
                    if (t.FromState == from && t.ToState == to) return;
                }
                AnimTransition trans;
                trans.FromState = from;
                trans.ToState = to;
                trans.Duration = 0.25f;
                trans.ExitTime = 0.8f;
                const_cast<AnimSubStateData*>(targetSS)->Transitions.push_back(trans);

                bool foundEdge = false;
                for (auto& edge : s_TransitionEdges)
                {
                    if ((edge.LeftState == from && edge.RightState == to) || (edge.LeftState == to && edge.RightState == from))
                    { foundEdge = true; break; }
                }
                if (!foundEdge)
                {
                    auto* fromNode = FindNodeByStateName(from);
                    auto* toNode = FindNodeByStateName(to);
                    if (fromNode && toNode)
                    {
                        TransitionEdge edge;
                        if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                        { edge.LeftState = from; edge.RightState = to; }
                        else
                        { edge.LeftState = to; edge.RightState = from; }
                        s_TransitionEdges.push_back(edge);
                    }
                }
                return;
            }
            auto* targetLayer = m_Controller->GetLayer(m_CurrentSubStateName);
            if (targetLayer)
            {
                for (const auto& t : targetLayer->Transitions)
                {
                    if (t.FromState == from && t.ToState == to) return;
                }
                AnimTransition trans;
                trans.FromState = from;
                trans.ToState = to;
                trans.Duration = 0.25f;
                trans.ExitTime = 0.8f;
                targetLayer->Transitions.push_back(trans);

                bool foundEdge = false;
                for (auto& edge : s_TransitionEdges)
                {
                    if ((edge.LeftState == from && edge.RightState == to) || (edge.LeftState == to && edge.RightState == from))
                    { foundEdge = true; break; }
                }
                if (!foundEdge)
                {
                    auto* fromNode = FindNodeByStateName(from);
                    auto* toNode = FindNodeByStateName(to);
                    if (fromNode && toNode)
                    {
                        TransitionEdge edge;
                        if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                        { edge.LeftState = from; edge.RightState = to; }
                        else
                        { edge.LeftState = to; edge.RightState = from; }
                        s_TransitionEdges.push_back(edge);
                    }
                }
                return;
            }
            return;
        }

        auto& targetTransitions = m_Controller->Layers[m_SelectedLayerIndex].Transitions;

        for (const auto& t : targetTransitions)
        {
            if (t.FromState == from && t.ToState == to)
                return;
        }

        // Kenar ataması kaydet — bu çift için zaten varsa kullan, yoksa oluştur
        bool foundEdge = false;
        for (auto& edge : s_TransitionEdges)
        {
            if ((edge.LeftState == from && edge.RightState == to) ||
                (edge.LeftState == to && edge.RightState == from))
            {
                foundEdge = true;
                break;
            }
        }
        if (!foundEdge)
        {
            auto* fromNode = FindNodeByStateName(from);
            auto* toNode = FindNodeByStateName(to);
            if (fromNode && toNode)
            {
                TransitionEdge edge;
                if (fromNode->EditorPosition.x <= toNode->EditorPosition.x)
                {
                    edge.LeftState = from;
                    edge.RightState = to;
                }
                else
                {
                    edge.LeftState = to;
                    edge.RightState = from;
                }
                s_TransitionEdges.push_back(edge);
            }
        }

        AnimTransition trans;
        trans.FromState = from;
        trans.ToState = to;
        trans.Duration = 0.25f;
        trans.ExitTime = 0.8f;
        targetTransitions.push_back(trans);
    }

    void AnimatorPanel::RemoveState(const char* name)
    {
        if (!m_Controller || m_Controller->Layers.empty()) return;

        if (m_InSubStateView && !m_CurrentSubStateName.empty())
        {
            auto* targetSS = FindSubStateRecursive(m_Controller->Layers[m_SelectedLayerIndex].SubStates, m_CurrentSubStateName);
            if (targetSS)
            {
                auto* ss = const_cast<AnimSubStateData*>(targetSS);
                ss->States.erase(std::remove_if(ss->States.begin(), ss->States.end(), [name](const AnimState& s) { return s.Name == name; }), ss->States.end());
                ss->Transitions.erase(std::remove_if(ss->Transitions.begin(), ss->Transitions.end(), [name](const AnimTransition& t) { return t.FromState == name || t.ToState == name; }), ss->Transitions.end());
                if (ss->DefaultState == name && !ss->States.empty()) ss->DefaultState = ss->States[0].Name;
            }
            else
            {
                auto* lyr = m_Controller->GetLayer(m_CurrentSubStateName);
                if (lyr)
                {
                    lyr->States.erase(std::remove_if(lyr->States.begin(), lyr->States.end(), [name](const AnimState& s) { return s.Name == name; }), lyr->States.end());
                    lyr->Transitions.erase(std::remove_if(lyr->Transitions.begin(), lyr->Transitions.end(), [name](const AnimTransition& t) { return t.FromState == name || t.ToState == name; }), lyr->Transitions.end());
                    if (lyr->DefaultState == name && !lyr->States.empty()) lyr->DefaultState = lyr->States[0].Name;
                }
            }
        }
        else
        {
            auto& layer = m_Controller->Layers[m_SelectedLayerIndex];
            layer.States.erase(std::remove_if(layer.States.begin(), layer.States.end(), [name](const AnimState& s) { return s.Name == name; }), layer.States.end());
            layer.Transitions.erase(std::remove_if(layer.Transitions.begin(), layer.Transitions.end(), [name](const AnimTransition& t) { return t.FromState == name || t.ToState == name; }), layer.Transitions.end());
            if (layer.DefaultState == name && !layer.States.empty()) layer.DefaultState = layer.States[0].Name;
        }

        s_TransitionEdges.erase(std::remove_if(s_TransitionEdges.begin(), s_TransitionEdges.end(), [name](const TransitionEdge& e) { return e.LeftState == name || e.RightState == name; }), s_TransitionEdges.end());
        s_Nodes.erase(std::remove_if(s_Nodes.begin(), s_Nodes.end(), [name](const AnimGraphNode& n) { return n.StateName == name; }), s_Nodes.end());
    }
}
