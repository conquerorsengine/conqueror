#include "AudioGraphPanel.h"

#include "Scene/Components.h"

#include <imgui.h>
#include <imnodes.h>
#include "Core/Audio/AudioEngine.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    namespace
    {
        int GetInputAttributeID(int nodeID) { return nodeID * 10 + 1; }
        int GetOutputAttributeID(int nodeID) { return nodeID * 10 + 2; }
        bool IsInputAttribute(int attributeID) { return attributeID % 10 == 1; }
        bool IsOutputAttribute(int attributeID) { return attributeID % 10 == 2; }

        const char* AudioNodeTypeToString(AudioGraphNodeType type)
        {
            switch (type)
            {
                case AudioGraphNodeType::Source: return "Audio Source";
                case AudioGraphNodeType::LowPass: return "Low Pass";
                case AudioGraphNodeType::HighPass: return "High Pass";
                case AudioGraphNodeType::Echo: return "Echo";
                case AudioGraphNodeType::Reverb: return "Reverb";
                case AudioGraphNodeType::Chorus: return "Chorus";
                case AudioGraphNodeType::Distortion: return "Distortion";
                case AudioGraphNodeType::Gain: return "Gain";
                case AudioGraphNodeType::Pan: return "Pan";
                case AudioGraphNodeType::Master: return "Master Output";
                default: return "Unknown";
            }
        }

        bool IsFilterNodeType(AudioGraphNodeType type)
        {
            return type != AudioGraphNodeType::Source && type != AudioGraphNodeType::Master;
        }

        glm::vec2 DefaultPositionForType(AudioGraphNodeType type)
        {
            switch (type)
            {
                case AudioGraphNodeType::Source: return { 40.0f, 80.0f };
                case AudioGraphNodeType::LowPass: return { 200.0f, 80.0f };
                case AudioGraphNodeType::HighPass: return { 360.0f, 80.0f };
                case AudioGraphNodeType::Echo: return { 520.0f, 80.0f };
                case AudioGraphNodeType::Reverb: return { 680.0f, 80.0f };
                case AudioGraphNodeType::Chorus: return { 840.0f, 80.0f };
                case AudioGraphNodeType::Distortion: return { 1000.0f, 80.0f };
                case AudioGraphNodeType::Gain: return { 1160.0f, 80.0f };
                case AudioGraphNodeType::Pan: return { 1320.0f, 80.0f };
                case AudioGraphNodeType::Master: return { 1480.0f, 80.0f };
                default: return { 0.0f, 0.0f };
            }
        }

        AudioGraphNode* FindNodeByType(AudioSourceComponent& audio, AudioGraphNodeType type)
        {
            for (auto& node : audio.GraphNodes)
            {
                if (node.Type == type)
                    return &node;
            }

            return nullptr;
        }

        AudioGraphNode* FindNodeByID(AudioSourceComponent& audio, int nodeID)
        {
            for (auto& node : audio.GraphNodes)
            {
                if (node.ID == nodeID)
                    return &node;
            }

            return nullptr;
        }

        bool HasFilterComponent(Entity entity, AudioGraphNodeType type)
        {
            switch (type)
            {
                case AudioGraphNodeType::LowPass: return entity.HasComponent<AudioLowPassFilterComponent>();
                case AudioGraphNodeType::HighPass: return entity.HasComponent<AudioHighPassFilterComponent>();
                case AudioGraphNodeType::Echo: return entity.HasComponent<AudioEchoFilterComponent>();
                case AudioGraphNodeType::Reverb: return entity.HasComponent<AudioReverbFilterComponent>();
                case AudioGraphNodeType::Chorus: return entity.HasComponent<AudioChorusFilterComponent>();
                case AudioGraphNodeType::Distortion: return entity.HasComponent<AudioDistortionFilterComponent>();
                case AudioGraphNodeType::Gain: return entity.HasComponent<AudioGainComponent>();
                case AudioGraphNodeType::Pan: return entity.HasComponent<AudioPanComponent>();
                default: return true;
            }
        }

        void AddFilterComponentForType(Entity entity, AudioGraphNodeType type)
        {
            switch (type)
            {
                case AudioGraphNodeType::LowPass:
                    if (!entity.HasComponent<AudioLowPassFilterComponent>())
                        entity.AddComponent<AudioLowPassFilterComponent>();
                    break;
                case AudioGraphNodeType::HighPass:
                    if (!entity.HasComponent<AudioHighPassFilterComponent>())
                        entity.AddComponent<AudioHighPassFilterComponent>();
                    break;
                case AudioGraphNodeType::Echo:
                    if (!entity.HasComponent<AudioEchoFilterComponent>())
                        entity.AddComponent<AudioEchoFilterComponent>();
                    break;
                case AudioGraphNodeType::Reverb:
                    if (!entity.HasComponent<AudioReverbFilterComponent>())
                        entity.AddComponent<AudioReverbFilterComponent>();
                    break;
                case AudioGraphNodeType::Chorus:
                    if (!entity.HasComponent<AudioChorusFilterComponent>())
                        entity.AddComponent<AudioChorusFilterComponent>();
                    break;
                case AudioGraphNodeType::Distortion:
                    if (!entity.HasComponent<AudioDistortionFilterComponent>())
                        entity.AddComponent<AudioDistortionFilterComponent>();
                    break;
                case AudioGraphNodeType::Gain:
                    if (!entity.HasComponent<AudioGainComponent>())
                        entity.AddComponent<AudioGainComponent>();
                    break;
                case AudioGraphNodeType::Pan:
                    if (!entity.HasComponent<AudioPanComponent>())
                        entity.AddComponent<AudioPanComponent>();
                    break;
                default:
                    break;
            }
        }

        void RemoveFilterComponentForType(Entity entity, AudioGraphNodeType type)
        {
            switch (type)
            {
                case AudioGraphNodeType::LowPass:
                    if (entity.HasComponent<AudioLowPassFilterComponent>())
                        entity.RemoveComponent<AudioLowPassFilterComponent>();
                    break;
                case AudioGraphNodeType::HighPass:
                    if (entity.HasComponent<AudioHighPassFilterComponent>())
                        entity.RemoveComponent<AudioHighPassFilterComponent>();
                    break;
                case AudioGraphNodeType::Echo:
                    if (entity.HasComponent<AudioEchoFilterComponent>())
                        entity.RemoveComponent<AudioEchoFilterComponent>();
                    break;
                case AudioGraphNodeType::Reverb:
                    if (entity.HasComponent<AudioReverbFilterComponent>())
                        entity.RemoveComponent<AudioReverbFilterComponent>();
                    break;
                case AudioGraphNodeType::Chorus:
                    if (entity.HasComponent<AudioChorusFilterComponent>())
                        entity.RemoveComponent<AudioChorusFilterComponent>();
                    break;
                case AudioGraphNodeType::Distortion:
                    if (entity.HasComponent<AudioDistortionFilterComponent>())
                        entity.RemoveComponent<AudioDistortionFilterComponent>();
                    break;
                case AudioGraphNodeType::Gain:
                    if (entity.HasComponent<AudioGainComponent>())
                        entity.RemoveComponent<AudioGainComponent>();
                    break;
                case AudioGraphNodeType::Pan:
                    if (entity.HasComponent<AudioPanComponent>())
                        entity.RemoveComponent<AudioPanComponent>();
                    break;
                default:
                    break;
            }
        }

        void RemoveLinksForNode(AudioSourceComponent& audio, int nodeID)
        {
            audio.GraphLinks.erase(
                std::remove_if(audio.GraphLinks.begin(), audio.GraphLinks.end(),
                    [nodeID](const AudioGraphLink& link)
                    {
                        return link.FromNodeID == nodeID || link.ToNodeID == nodeID;
                    }),
                audio.GraphLinks.end());
        }

        void EnsureNode(AudioSourceComponent& audio, AudioGraphNodeType type)
        {
            if (FindNodeByType(audio, type))
                return;

            audio.GraphNodes.emplace_back(audio.NextGraphNodeID++, type, DefaultPositionForType(type));
        }

        void RemoveNodeByType(AudioSourceComponent& audio, AudioGraphNodeType type)
        {
            auto* node = FindNodeByType(audio, type);
            if (!node)
                return;

            const int nodeID = node->ID;
            RemoveLinksForNode(audio, nodeID);
            audio.GraphNodes.erase(
                std::remove_if(audio.GraphNodes.begin(), audio.GraphNodes.end(),
                    [nodeID](const AudioGraphNode& graphNode)
                    {
                        return graphNode.ID == nodeID;
                    }),
                audio.GraphNodes.end());
        }

        void AddLink(AudioSourceComponent& audio, int fromNodeID, int toNodeID)
        {
            for (const auto& link : audio.GraphLinks)
            {
                if (link.FromNodeID == fromNodeID && link.ToNodeID == toNodeID)
                    return;
            }

            audio.GraphLinks.emplace_back(audio.NextGraphLinkID++, fromNodeID, toNodeID);
        }

        void RebuildLinearGraph(Entity entity, AudioSourceComponent& audio)
        {
            EnsureNode(audio, AudioGraphNodeType::Source);
            EnsureNode(audio, AudioGraphNodeType::Master);

            audio.GraphLinks.clear();

            int currentNodeID = FindNodeByType(audio, AudioGraphNodeType::Source)->ID;
            const AudioGraphNodeType filterTypes[] = {
                AudioGraphNodeType::LowPass,
                AudioGraphNodeType::HighPass,
                AudioGraphNodeType::Echo,
                AudioGraphNodeType::Reverb,
                AudioGraphNodeType::Chorus,
                AudioGraphNodeType::Distortion,
                AudioGraphNodeType::Gain,
                AudioGraphNodeType::Pan
            };

            for (AudioGraphNodeType type : filterTypes)
            {
                if (!HasFilterComponent(entity, type))
                    continue;

                EnsureNode(audio, type);
                AddLink(audio, currentNodeID, FindNodeByType(audio, type)->ID);
                currentNodeID = FindNodeByType(audio, type)->ID;
            }

            AddLink(audio, currentNodeID, FindNodeByType(audio, AudioGraphNodeType::Master)->ID);
            audio.GraphInitialized = true;
        }

        void SyncGraphWithEntity(Entity entity, AudioSourceComponent& audio)
        {
            EnsureNode(audio, AudioGraphNodeType::Source);
            EnsureNode(audio, AudioGraphNodeType::Master);

            const AudioGraphNodeType filterTypes[] = {
                AudioGraphNodeType::LowPass,
                AudioGraphNodeType::HighPass,
                AudioGraphNodeType::Echo,
                AudioGraphNodeType::Reverb,
                AudioGraphNodeType::Chorus,
                AudioGraphNodeType::Distortion,
                AudioGraphNodeType::Gain,
                AudioGraphNodeType::Pan
            };

            bool nodesChanged = false;
            for (AudioGraphNodeType type : filterTypes)
            {
                bool hasComp = HasFilterComponent(entity, type);
                bool hasNode = FindNodeByType(audio, type) != nullptr;
                
                if (hasComp && !hasNode)
                {
                    EnsureNode(audio, type);
                    nodesChanged = true;
                }
                else if (!hasComp && hasNode)
                {
                    RemoveNodeByType(audio, type);
                    nodesChanged = true;
                }
            }

            if (nodesChanged || audio.GraphLinks.empty())
            {
                RebuildLinearGraph(entity, audio);
                audio.NeedsReconnection = true;
            }

            int maxNodeID = 0;
            int maxLinkID = 0;
            for (const auto& node : audio.GraphNodes)
                maxNodeID = std::max(maxNodeID, node.ID);
            for (const auto& link : audio.GraphLinks)
                maxLinkID = std::max(maxLinkID, link.ID);

            audio.NextGraphNodeID = std::max(audio.NextGraphNodeID, maxNodeID + 1);
            audio.NextGraphLinkID = std::max(audio.NextGraphLinkID, maxLinkID + 1);
            audio.GraphInitialized = true;
        }

        bool WouldCreateCycle(const AudioSourceComponent& audio, int fromNodeID, int toNodeID)
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

                for (const auto& link : audio.GraphLinks)
                {
                    if (link.FromNodeID == current)
                        stack.push_back(link.ToNodeID);
                }
            }

            return false;
        }
    }

    AudioGraphPanel::AudioGraphPanel()
    {
        ImNodes::CreateContext();
        ImNodes::GetStyle().Colors[ImNodesCol_GridBackground] = IM_COL32(15, 15, 18, 200);
    }

    AudioGraphPanel::~AudioGraphPanel()
    {
    }

    void AudioGraphPanel::SetContext(Scene* scene)
    {
        m_Context = scene;
    }

    void AudioGraphPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectedEntity = entity;
    }

    void AudioGraphPanel::OnImGuiRender()
    {
        ImGui::Begin("Audio Graph Editor");

        if (!m_Context)
        {
            ImGui::TextUnformatted("Scene context bulunamadi.");
            ImGui::End();
            return;
        }

        if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<AudioSourceComponent>())
        {
            ImGui::TextUnformatted("Select an entity with an Audio Source.");
            ImGui::TextUnformatted("Blueprint graph manages the DSP chain of the selected audio source.");
            ImGui::End();
            return;
        }

        auto& audio = m_SelectedEntity.GetComponent<AudioSourceComponent>();
        SyncGraphWithEntity(m_SelectedEntity, audio);

        ImGui::Text("File: %s", audio.FilePath.empty() ? "(empty)" : audio.FilePath.c_str());
        ImGui::Text("Nodes: %zu | Links: %zu", audio.GraphNodes.size(), audio.GraphLinks.size());
        ImGui::SameLine();
        if (ImGui::Button("Rebuild Linear Chain"))
        {
            RebuildLinearGraph(m_SelectedEntity, audio);
            audio.NeedsReconnection = true;
        }

        auto addNodeButton = [&](const char* label, AudioGraphNodeType type)
        {
            const bool exists = FindNodeByType(audio, type) != nullptr;
            if (exists)
                ImGui::BeginDisabled();
            if (ImGui::Button(label))
            {
                AddFilterComponentForType(m_SelectedEntity, type);
                SyncGraphWithEntity(m_SelectedEntity, audio);
                RebuildLinearGraph(m_SelectedEntity, audio);
                audio.NeedsReconnection = true;
            }
            if (exists)
                ImGui::EndDisabled();
        };

        // Context menu (right-click)
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("AudioGraphContextMenu");

        if (ImGui::BeginPopup("AudioGraphContextMenu"))
        {
            ImGui::TextDisabled("Add Filter");
            ImGui::Separator();
            addNodeButton("Low Pass", AudioGraphNodeType::LowPass);
            addNodeButton("High Pass", AudioGraphNodeType::HighPass);
            addNodeButton("Echo", AudioGraphNodeType::Echo);
            addNodeButton("Reverb", AudioGraphNodeType::Reverb);
            addNodeButton("Chorus", AudioGraphNodeType::Chorus);
            addNodeButton("Distortion", AudioGraphNodeType::Distortion);
            addNodeButton("Gain", AudioGraphNodeType::Gain);
            addNodeButton("Pan", AudioGraphNodeType::Pan);
            ImGui::EndPopup();
        }

        ImGui::Separator();

        ImNodes::BeginNodeEditor();
        for (auto& node : audio.GraphNodes)
        {
            // Stylize based on type
            if (node.Type == AudioGraphNodeType::Source)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 120, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 150, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 180, 50, 255));
            }
            else if (node.Type == AudioGraphNodeType::Master)
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(120, 30, 30, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(150, 40, 40, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(180, 50, 50, 255));
            }
            else
            {
                ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(30, 30, 120, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(40, 40, 150, 255));
                ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(50, 50, 180, 255));
            }

            ImNodes::BeginNode(node.ID);
            ImNodes::BeginNodeTitleBar();
            
            const char* icon = "[?]";
            if (node.Type == AudioGraphNodeType::Source) icon = "\xef\x80\x81"; // Icon for Source (e.g. Play)
            else if (node.Type == AudioGraphNodeType::Master) icon = "\xef\x80\xa8"; // Icon for Master (e.g. Speaker)
            else if (node.Type == AudioGraphNodeType::LowPass) icon = "LPF";
            else if (node.Type == AudioGraphNodeType::HighPass) icon = "HPF";
            else if (node.Type == AudioGraphNodeType::Echo) icon = "ECH";
            else if (node.Type == AudioGraphNodeType::Reverb) icon = "RVB";
            else if (node.Type == AudioGraphNodeType::Chorus) icon = "CHO";
            else if (node.Type == AudioGraphNodeType::Distortion) icon = "DST";
            else if (node.Type == AudioGraphNodeType::Gain) icon = "VOL";
            else if (node.Type == AudioGraphNodeType::Pan) icon = "PAN";

            ImGui::Text("%s%s", icon, AudioNodeTypeToString(node.Type));
            ImNodes::EndNodeTitleBar();

            if (node.Type != AudioGraphNodeType::Source)
            {
                ImNodes::BeginInputAttribute(GetInputAttributeID(node.ID));
                ImGui::TextUnformatted("In");
                ImNodes::EndInputAttribute();
            }

            if (IsFilterNodeType(node.Type))
            {
                ImGui::PushItemWidth(100.0f);
                if (ImGui::Checkbox(("Bypass##" + std::to_string(node.ID)).c_str(), &node.Bypassed))
                    audio.NeedsReconnection = true;
                ImGui::PopItemWidth();
            }

            if (node.Type == AudioGraphNodeType::Master)
            {
                // Muazzam Spectrum Visualizer
                ImGui::Text("Spectrum Analysis");
                float spectrum[32];
                AudioEngine::Get().GetSpectrumData(spectrum, 32, audio.IsPlaying, audio.Volume);
                
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 startPos = ImGui::GetCursorScreenPos();
                float width = 120.0f;
                float height = 40.0f;
                float barWidth = width / 32.0f;
                
                drawList->AddRectFilled(startPos, ImVec2(startPos.x + width, startPos.y + height), IM_COL32(20, 20, 20, 255));
                for (int i = 0; i < 32; i++)
                {
                    float barHeight = spectrum[i] * height;
                    drawList->AddRectFilled(
                        ImVec2(startPos.x + i * barWidth, startPos.y + height - barHeight),
                        ImVec2(startPos.x + (i + 1) * barWidth - 1, startPos.y + height),
                        IM_COL32(0, 255, 0, 255)
                    );
                }
                ImGui::Dummy(ImVec2(width, height));
            }

            if (node.Type != AudioGraphNodeType::Master)
            {
                ImNodes::BeginOutputAttribute(GetOutputAttributeID(node.ID));
                ImGui::Indent(40.0f);
                ImGui::TextUnformatted("Out");
                ImNodes::EndOutputAttribute();
            }

            ImNodes::EndNode();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            
            ImNodes::SetNodeGridSpacePos(node.ID, ImVec2(node.EditorPosition.x, node.EditorPosition.y));
        }

        for (const auto& link : audio.GraphLinks)
        {
            ImNodes::Link(link.ID, GetOutputAttributeID(link.FromNodeID), GetInputAttributeID(link.ToNodeID));
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        for (auto& node : audio.GraphNodes)
        {
            const ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.ID);
            node.EditorPosition = { pos.x, pos.y };
        }

        int startNodeID = 0, startAttr = 0, endNodeID = 0, endAttr = 0;
        if (ImNodes::IsLinkCreated(&startNodeID, &startAttr, &endNodeID, &endAttr))
        {
            int fromNodeID = 0;
            int toNodeID = 0;

            if (IsOutputAttribute(startAttr) && IsInputAttribute(endAttr))
            {
                fromNodeID = startNodeID;
                toNodeID = endNodeID;
            }
            else if (IsInputAttribute(startAttr) && IsOutputAttribute(endAttr))
            {
                fromNodeID = endNodeID;
                toNodeID = startNodeID;
            }

            auto* fromNode = FindNodeByID(audio, fromNodeID);
            auto* toNode = FindNodeByID(audio, toNodeID);

            if (fromNode && toNode &&
                fromNode->Type != AudioGraphNodeType::Master &&
                toNode->Type != AudioGraphNodeType::Source &&
                fromNodeID != toNodeID &&
                !WouldCreateCycle(audio, fromNodeID, toNodeID))
            {
                audio.GraphLinks.erase(
                    std::remove_if(audio.GraphLinks.begin(), audio.GraphLinks.end(),
                        [fromNodeID, toNodeID](const AudioGraphLink& link)
                        {
                            return link.FromNodeID == fromNodeID || link.ToNodeID == toNodeID;
                        }),
                    audio.GraphLinks.end());

                AddLink(audio, fromNodeID, toNodeID);
                audio.NeedsReconnection = true;
            }
        }

        int destroyedLinkID = 0;
        if (ImNodes::IsLinkDestroyed(&destroyedLinkID))
        {
            audio.GraphLinks.erase(
                std::remove_if(audio.GraphLinks.begin(), audio.GraphLinks.end(),
                    [destroyedLinkID](const AudioGraphLink& link)
                    {
                        return link.ID == destroyedLinkID;
                    }),
                audio.GraphLinks.end());
            audio.NeedsReconnection = true;
        }

        // Context menu - delete selected node
        const int selectedNodeCount = ImNodes::NumSelectedNodes();
        if (selectedNodeCount > 0)
        {
            std::vector<int> selectedNodes((size_t)selectedNodeCount);
            ImNodes::GetSelectedNodes(selectedNodes.data());
            const int selectedNodeID = selectedNodes.front();
            auto* selectedNode = FindNodeByID(audio, selectedNodeID);
            if (selectedNode && IsFilterNodeType(selectedNode->Type))
            {
                if (ImGui::BeginPopup("AudioGraphContextMenu"))
                {
                    if (ImGui::MenuItem("Delete Node"))
                    {
                        RemoveFilterComponentForType(m_SelectedEntity, selectedNode->Type);
                        RemoveNodeByType(audio, selectedNode->Type);
                        if (audio.GraphLinks.empty())
                            RebuildLinearGraph(m_SelectedEntity, audio);
                        audio.NeedsReconnection = true;
                        ImNodes::ClearNodeSelection();
                    }
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::End();
    }
}
