#include "VisualScriptPanel.h"
#include "Core/ImGui/ImGuiLayer.h"
#include "Core/Logging/Log.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Scripting/NativeRegistry.h"
#include "UI/EditorUI.h"
#include "EditorState.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

namespace Conqueror::Editor
{
    using namespace Visual;

    namespace
    {
        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        ImU32 PinColor(PinType t)
        {
            switch (t)
            {
                case PinType::Exec: return IM_COL32(235, 235, 240, 255);
                case PinType::Number: return IM_COL32(110, 220, 130, 255);
                case PinType::Bool: return IM_COL32(225, 85, 85, 255);
                case PinType::String: return IM_COL32(230, 115, 220, 255);
                case PinType::Vector: return IM_COL32(245, 200, 70, 255);
                case PinType::List: return IM_COL32(85, 200, 235, 255);
                default: return IM_COL32(165, 165, 175, 255);
            }
        }

        ImU32 TitleColor(const NodeDef& d)
        {
            bool native = d.Id.rfind("native.", 0) == 0;
            switch (d.Kind)
            {
                case NodeKind::Event: return IM_COL32(165, 45, 45, 255);
                case NodeKind::Flow: return IM_COL32(85, 88, 98, 255);
                case NodeKind::Pure: return native ? IM_COL32(30, 110, 118, 255) : IM_COL32(45, 118, 72, 255);
                case NodeKind::Action: return native ? IM_COL32(45, 82, 158, 255) : IM_COL32(60, 92, 150, 255);
                case NodeKind::Literal: return IM_COL32(150, 92, 30, 255);
                case NodeKind::Variable: return IM_COL32(118, 62, 140, 255);
                case NodeKind::Comment: return IM_COL32(112, 102, 40, 255);
            }
            return IM_COL32(80, 80, 80, 255);
        }

        ImU32 Brighten(ImU32 c, float f)
        {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x = std::min(1.0f, v.x * f);
            v.y = std::min(1.0f, v.y * f);
            v.z = std::min(1.0f, v.z * f);
            return ImGui::ColorConvertFloat4ToU32(v);
        }

        const char* KindIcon(const NodeDef& d)
        {
            switch (d.Kind)
            {
                case NodeKind::Event: return ICON_FA_BOLT;
                case NodeKind::Flow: return ICON_FA_CODE_BRANCH;
                case NodeKind::Pure: return ICON_FA_FLASK;
                case NodeKind::Action: return ICON_FA_PLAY;
                case NodeKind::Literal: return ICON_FA_HASHTAG;
                case NodeKind::Variable: return ICON_FA_TAG;
                case NodeKind::Comment: return ICON_FA_COMMENT;
            }
            return ICON_FA_CUBE;
        }

        std::vector<std::string> KeyNames()
        {
            std::vector<std::string> names;
            for (const auto& [name, value] : Scripting::NativeRegistry::Get().GetConstants())
                if (name == "Key" && value.IsMap())
                    for (auto& [k, v] : value.AsMap()->Entries) names.push_back(k);
            std::sort(names.begin(), names.end());
            if (names.empty()) names = { "Space", "W", "A", "S", "D" };
            return names;
        }

        // Zoom 1.0'daki imnodes stil değerleri
        constexpr float kGridSpacing = 16.0f;
        constexpr float kNodeCornerRounding = 6.0f;
        constexpr ImVec2 kNodePadding = ImVec2(8, 6);
        constexpr float kLinkThickness = 2.6f;
        constexpr float kLinkHoverDistance = 10.0f;
        constexpr float kPinCircleRadius = 4.5f;
        constexpr float kPinTriangleSideLength = 10.0f;
        constexpr float kPinQuadSideLength = 7.0f;
        constexpr float kPinHoverRadius = 10.0f;
        constexpr float kMinZoom = 0.25f;
        constexpr float kMaxZoom = 2.0f;

        int Score(const std::string& text, const std::string& filter)
        {
            if (filter.empty()) return 1;
            std::string t = text, f = filter;
            for (char& c : t) c = (char)std::tolower((unsigned char)c);
            for (char& c : f) c = (char)std::tolower((unsigned char)c);
            if (t.rfind(f, 0) == 0) return 4;
            size_t p = t.find(f);
            if (p != std::string::npos) return (p > 0 && (text[p - 1] == ' ' || text[p - 1] == '.' || std::isupper((unsigned char)text[p]))) ? 3 : 2;
            // Baş harfler: "spo" -> SetPosition
            size_t i = 0;
            for (char c : t) if (i < f.size() && c == f[i]) ++i;
            return i == f.size() ? 1 : 0;
        }
    }

    // ════════════════════════════════════════
    //  Lifetime
    // ════════════════════════════════════════
    VisualScriptPanel::VisualScriptPanel()
    {
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        m_Context = ImNodes::CreateContext();
        ImNodes::SetCurrentContext(m_Context);
        ImNodesStyle& style = ImNodes::GetStyle();
        style.Colors[ImNodesCol_GridBackground] = IM_COL32(22, 23, 27, 255);
        style.Colors[ImNodesCol_GridLine] = IM_COL32(40, 42, 48, 255);
        style.Colors[ImNodesCol_GridLinePrimary] = IM_COL32(52, 55, 62, 255);
        style.Colors[ImNodesCol_NodeBackground] = IM_COL32(38, 40, 46, 245);
        style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(44, 46, 53, 245);
        style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(48, 51, 60, 245);
        style.Colors[ImNodesCol_NodeOutline] = IM_COL32(20, 20, 24, 255);
        style.Flags |= ImNodesStyleFlags_GridLinesPrimary | ImNodesStyleFlags_GridSnapping;
        ApplyZoomStyle();
        ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
        ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyShift;
        if (previous) ImNodes::SetCurrentContext(previous);
    }

    VisualScriptPanel::~VisualScriptPanel()
    {
        if (m_Context)
            ImNodes::DestroyContext(m_Context);
    }

    void VisualScriptPanel::Open(const std::filesystem::path& path)
    {
        if (m_Dirty && !m_Path.empty() && path != m_Path)
            Save();
        Graph g;
        std::string error;
        if (!g.Load(path.string(), &error))
        {
            CQ_CORE_ERROR("Visual Script: cannot open {0}: {1}", path.string(), error);
            return;
        }
        m_Graph = std::move(g);
        if (m_Graph.Name.empty() || m_Graph.Name == "NewGraph")
            m_Graph.Name = path.stem().string();
        m_Path = path;
        m_IsOpen = true;
        m_RequestFocus = true;
        m_Dirty = false;
        m_PositionsDirty = true;
        m_NeedsCompile = true;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_LastEdit = 0.0;
        CQ_CORE_INFO("Visual Script: opened {0} ({1} nodes)", path.filename().string(), m_Graph.Nodes.size());
    }

    bool VisualScriptPanel::Save()
    {
        if (m_Path.empty())
            return false;
        if (!m_Graph.Save(m_Path.string()))
        {
            CQ_CORE_ERROR("Visual Script: cannot save {0}", m_Path.string());
            return false;
        }
        m_Dirty = false;
        CQ_CORE_INFO("Visual Script: saved {0}", m_Path.filename().string());
        if (OnFileSaved)
            OnFileSaved(m_Path);
        return true;
    }

    void VisualScriptPanel::MarkEdited()
    {
        m_Dirty = true;
        m_NeedsCompile = true;
        m_LastEdit = Now();
        m_PositionsDirty = true;
    }

    int VisualScriptPanel::GetErrorCount() const
    {
        int n = 0;
        for (auto& m : m_Messages) if (!m.IsWarning) ++n;
        return n;
    }

    std::string VisualScriptPanel::GetGeneratedCode()
    {
        Recompile();
        return m_GeneratedCode;
    }

    // ════════════════════════════════════════
    //  Compile
    // ════════════════════════════════════════
    void VisualScriptPanel::Recompile()
    {
        m_NeedsCompile = false;
        m_Messages.clear();
        m_ErrorNodes.clear();
        std::vector<int> lineToNode;
        std::string className = SanitizeIdentifier(m_Path.empty() ? m_Graph.Name : m_Path.stem().string());
        m_GeneratedCode = m_Graph.Transpile(className, m_Messages, &lineToNode);
        std::vector<std::string> errors, warnings;
        CQS::CQSEngine::CheckSyntax(m_GeneratedCode, className + ".cqs", errors, &warnings);
        auto map = [&](const std::string& e, bool warning) {
            int line = 0;
            size_t p = e.find("Line ");
            if (p != std::string::npos) line = std::atoi(e.c_str() + p + 5);
            int node = (line > 0 && line <= (int)lineToNode.size()) ? lineToNode[(size_t)line - 1] : 0;
            std::string text = e;
            size_t colon = p == std::string::npos ? std::string::npos : e.find(": ", p);
            if (colon != std::string::npos) text = e.substr(colon + 2);
            m_Messages.push_back({ node, text, warning });
        };
        for (auto& e : errors) map(e, false);
        for (auto& w : warnings) map(w, true);
        for (auto& m : m_Messages)
            if (!m.IsWarning && m.NodeId) m_ErrorNodes.insert(m.NodeId);
    }

    // ════════════════════════════════════════
    //  Editing operations
    // ════════════════════════════════════════
    void VisualScriptPanel::PushUndo()
    {
        m_UndoStack.push_back(m_Graph.ToJson());
        if (m_UndoStack.size() > 200)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
    }

    void VisualScriptPanel::Undo()
    {
        if (m_UndoStack.empty()) return;
        m_RedoStack.push_back(m_Graph.ToJson());
        m_Graph.FromJson(m_UndoStack.back());
        m_UndoStack.pop_back();
        MarkEdited();
    }

    void VisualScriptPanel::Redo()
    {
        if (m_RedoStack.empty()) return;
        m_UndoStack.push_back(m_Graph.ToJson());
        m_Graph.FromJson(m_RedoStack.back());
        m_RedoStack.pop_back();
        MarkEdited();
    }

    void VisualScriptPanel::CreateNodeAt(const std::string& type, const ImVec2& gridPos)
    {
        PushUndo();
        int id = m_Graph.AddNode(type, gridPos.x, gridPos.y);
        if (!id) return;
        // Bırakılan bağlantıdan açıldıysa uyumlu ilk pine bağla
        if (m_PendingLinkAttribute >= 0)
        {
            int srcNode, srcPin;
            bool srcOutput;
            DecodeAttribute(m_PendingLinkAttribute, srcNode, srcPin, srcOutput);
            const Node* src = m_Graph.FindNode(srcNode);
            const Node* dst = m_Graph.FindNode(id);
            if (src && dst)
            {
                NodeDef sd = m_Graph.ResolveDef(*src), dd = m_Graph.ResolveDef(*dst);
                if (srcOutput && srcPin < (int)sd.Outputs.size())
                {
                    for (auto& in : dd.Inputs)
                        if (PinsCompatible(sd.Outputs[(size_t)srcPin].Type, in.Type) && m_Graph.Connect(srcNode, sd.Outputs[(size_t)srcPin].Name, id, in.Name))
                            break;
                }
                else if (!srcOutput && srcPin < (int)sd.Inputs.size())
                {
                    for (auto& out : dd.Outputs)
                        if (PinsCompatible(out.Type, sd.Inputs[(size_t)srcPin].Type) && m_Graph.Connect(id, out.Name, srcNode, sd.Inputs[(size_t)srcPin].Name))
                            break;
                }
            }
            m_PendingLinkAttribute = -1;
        }
        MarkEdited();
    }

    void VisualScriptPanel::DeleteSelection()
    {
        int nl = ImNodes::NumSelectedLinks(), nn = ImNodes::NumSelectedNodes();
        if (nl == 0 && nn == 0) return;
        PushUndo();
        if (nl > 0)
        {
            std::vector<int> links((size_t)nl);
            ImNodes::GetSelectedLinks(links.data());
            for (int l : links) m_Graph.Disconnect(l);
        }
        if (nn > 0)
        {
            std::vector<int> nodes((size_t)nn);
            ImNodes::GetSelectedNodes(nodes.data());
            for (int n : nodes) m_Graph.RemoveNode(n);
        }
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        MarkEdited();
    }

    void VisualScriptPanel::CopySelection()
    {
        int nn = ImNodes::NumSelectedNodes();
        if (nn == 0) return;
        std::vector<int> ids((size_t)nn);
        ImNodes::GetSelectedNodes(ids.data());
        std::set<int> selected(ids.begin(), ids.end());
        Graph clip;
        clip.Variables = m_Graph.Variables;
        for (auto& n : m_Graph.Nodes) if (selected.count(n.Id)) clip.Nodes.push_back(n);
        for (auto& l : m_Graph.Links) if (selected.count(l.FromNode) && selected.count(l.ToNode)) clip.Links.push_back(l);
        clip.NextId = m_Graph.NextId;
        m_Clipboard = clip.ToJson();
        ImGui::SetClipboardText(m_Clipboard.c_str());
    }

    void VisualScriptPanel::Paste(const ImVec2& gridPos)
    {
        const char* text = ImGui::GetClipboardText();
        std::string json = text && std::strstr(text, "\"cqgraph\"") ? text : m_Clipboard;
        Graph clip;
        if (json.empty() || !clip.FromJson(json) || clip.Nodes.empty()) return;
        PushUndo();
        float minX = 1e9f, minY = 1e9f;
        for (auto& n : clip.Nodes) { minX = std::min(minX, n.X); minY = std::min(minY, n.Y); }
        std::map<int, int> remap;
        ImNodes::ClearNodeSelection();
        for (auto& n : clip.Nodes)
        {
            Node copy = n;
            copy.Id = m_Graph.NextId++;
            copy.X = gridPos.x + (n.X - minX);
            copy.Y = gridPos.y + (n.Y - minY);
            remap[n.Id] = copy.Id;
            m_Graph.Nodes.push_back(copy);
        }
        for (auto& l : clip.Links)
            m_Graph.Connect(remap[l.FromNode], l.FromPin, remap[l.ToNode], l.ToPin);
        // Yapıştırılan grafikte olup burada olmayan değişkenleri ekle
        for (auto& v : clip.Variables)
            if (!m_Graph.FindVariable(v.Name)) m_Graph.Variables.push_back(v);
        MarkEdited();
        for (auto& [oldId, newId] : remap) m_PendingSelect.push_back(newId);
    }

    void VisualScriptPanel::Duplicate()
    {
        CopySelection();
        int nn = ImNodes::NumSelectedNodes();
        if (nn == 0) return;
        std::vector<int> ids((size_t)nn);
        ImNodes::GetSelectedNodes(ids.data());
        float minX = 1e9f, minY = 1e9f;
        for (int id : ids)
            if (const Node* n = m_Graph.FindNode(id)) { minX = std::min(minX, n->X); minY = std::min(minY, n->Y); }
        Paste(ImVec2(minX + 32.0f, minY + 32.0f));
    }

    void VisualScriptPanel::FrameAll()
    {
        if (m_Graph.Nodes.empty()) return;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(m_Context);
        for (auto& n : m_Graph.Nodes)
        {
            // Gerçek düğüm boyutu (son çizimden), yoksa tahmin
            ImVec2 dims = ImNodes::GetNodeDimensions(n.Id);
            float w = dims.x > 1.0f ? dims.x / m_Zoom : 220.0f;
            float h = dims.y > 1.0f ? dims.y / m_Zoom : 120.0f;
            minX = std::min(minX, n.X);
            minY = std::min(minY, n.Y);
            maxX = std::max(maxX, n.X + w);
            maxY = std::max(maxY, n.Y + h);
        }
        if (previous) ImNodes::SetCurrentContext(previous);
        const float margin = 48.0f;
        ImVec2 size = m_CanvasSize.x > 1.0f ? m_CanvasSize : ImVec2(800, 600);
        float fit = std::min((size.x - margin * 2.0f) / std::max(1.0f, maxX - minX), (size.y - margin * 2.0f) / std::max(1.0f, maxY - minY));
        m_FrameZoom = std::clamp(fit, kMinZoom, 1.0f);
        m_FrameRequest = ImVec2((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
        m_HasFrameRequest = true;
    }

    void VisualScriptPanel::FocusNode(int id)
    {
        const Node* n = m_Graph.FindNode(id);
        if (!n) return;
        m_FrameZoom = std::max(m_Zoom, 0.8f);
        m_FrameRequest = ImVec2(n->X + 100.0f, n->Y + 50.0f);
        m_HasFrameRequest = true;
    }

    void VisualScriptPanel::ApplyZoomStyle()
    {
        ImNodesStyle& style = ImNodes::GetStyle();
        const float z = m_Zoom;
        style.GridSpacing = kGridSpacing * z;
        style.NodeCornerRounding = kNodeCornerRounding * z;
        style.NodePadding = ImVec2(kNodePadding.x * z, kNodePadding.y * z);
        style.LinkThickness = std::max(1.0f, kLinkThickness * z);
        style.LinkHoverDistance = kLinkHoverDistance * z;
        style.PinCircleRadius = kPinCircleRadius * z;
        style.PinTriangleSideLength = kPinTriangleSideLength * z;
        style.PinQuadSideLength = kPinQuadSideLength * z;
        style.PinHoverRadius = kPinHoverRadius * z;
    }

    void VisualScriptPanel::SetZoom(float zoom, const ImVec2& anchorLocal)
    {
        zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        if (std::fabs(zoom - m_Zoom) < 1e-4f) return;
        // İmlecin altındaki grafik noktası yerinde kalır
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        ImVec2 logical = ImVec2((anchorLocal.x - pan.x) / m_Zoom, (anchorLocal.y - pan.y) / m_Zoom);
        m_Zoom = zoom;
        ImNodes::EditorContextResetPanning(ImVec2(anchorLocal.x - logical.x * m_Zoom, anchorLocal.y - logical.y * m_Zoom));
        ApplyZoomStyle();
        m_PositionsDirty = true;
    }

    ImVec2 VisualScriptPanel::ScreenToGraph(const ImVec2& screen) const
    {
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(m_Context);
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        if (previous) ImNodes::SetCurrentContext(previous);
        return ImVec2((screen.x - m_CanvasOrigin.x - pan.x) / m_Zoom, (screen.y - m_CanvasOrigin.y - pan.y) / m_Zoom);
    }

    // ════════════════════════════════════════
    //  UI
    // ════════════════════════════════════════
    void VisualScriptPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;
        ImGui::SetNextWindowSize(ImVec2(1100, 680), ImGuiCond_FirstUseEver);
        std::string title = std::string(ICON_FA_DIAGRAM_PROJECT "  Visual Script") + (m_Path.empty() ? "" : " - " + m_Path.filename().string() + (m_Dirty ? " *" : "")) + "###VisualScript";
        if (m_RequestFocus)
        {
            ImGui::SetNextWindowFocus();
            m_RequestFocus = false;
        }
        if (!ImGui::Begin(title.c_str(), &m_IsOpen))
        {
            ImGui::End();
            return;
        }
        if (m_Path.empty())
        {
            ImGui::TextDisabled("No visual script open. Create one with Content Browser > Create > Script > Visual Script, then double-click it.");
            ImGui::End();
            return;
        }

        // Derleme (düzenlemeden 0.4 s sonra)
        if (m_NeedsCompile && Now() - m_LastEdit > 0.4)
            Recompile();

        RenderToolbar();
        float sidebarW = 250.0f;
        ImGui::BeginChild("##vs_sidebar", ImVec2(sidebarW, 0), true);
        RenderSidebar();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginGroup();
        float codeW = m_ShowCode ? std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.4f) : 0.0f;
        ImGui::BeginChild("##vs_canvas", ImVec2(ImGui::GetContentRegionAvail().x - codeW - (m_ShowCode ? ImGui::GetStyle().ItemSpacing.x : 0.0f), 0), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderCanvas();
        ImGui::EndChild();
        if (m_ShowCode)
        {
            ImGui::SameLine();
            ImGui::BeginChild("##vs_code", ImVec2(codeW, 0), true);
            RenderCodeView();
            ImGui::EndChild();
        }
        ImGui::EndGroup();
        ImGui::End();
    }

    std::string VisualScriptPanel::DebugKey() const { return SanitizeIdentifier(m_Path.stem().string()); }

    void VisualScriptPanel::ToggleBreakpoint(int node)
    {
        const std::string key = DebugKey();
        Debugger::SetBreakpoint(key, node, !Debugger::HasBreakpoint(key, node));
    }

    void VisualScriptPanel::ShowBreakpointHit(int node)
    {
        m_BreakHitNode = node;
        m_IsOpen = true;
        FocusNode(node);
    }

    void VisualScriptPanel::RenderToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        auto button = [](const char* icon, const char* tip, bool enabled = true) {
            ImGui::BeginDisabled(!enabled);
            bool pressed = ImGui::Button(icon);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
            return pressed;
        };
        if (button(ICON_FA_FLOPPY_DISK, "Save (Ctrl+S)", m_Dirty)) Save();
        if (button(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)", !m_UndoStack.empty())) Undo();
        if (button(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)", !m_RedoStack.empty())) Redo();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (button(ICON_FA_PLUS, "Add Node (Space / right-click)"))
        {
            m_OpenSearch = true;
            m_SearchScreenPos = ImGui::GetMousePos();
            m_SearchGridPos = ImVec2(64, 64);
        }
        if (button(ICON_FA_EXPAND, "Frame All (F)")) FrameAll();
        if (button(ICON_FA_MAP, "Toggle Minimap")) m_ShowMinimap = !m_ShowMinimap;
        if (button(ICON_FA_CODE, "Show Generated Code")) m_ShowCode = !m_ShowCode;
        if (button(ICON_FA_CIRCLE_CHECK, "Compile Now")) Recompile();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        // Durum
        int errors = GetErrorCount(), warnings = (int)m_Messages.size() - errors;
        ImGui::SameLine(0, 16);
        if (errors > 0) ImGui::TextColored(UI::Colors::Error, ICON_FA_CIRCLE_XMARK " %d error%s", errors, errors == 1 ? "" : "s");
        else ImGui::TextColored(UI::Colors::Success, ICON_FA_CIRCLE_CHECK " Compiled");
        if (warnings > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(UI::Colors::Warning, ICON_FA_TRIANGLE_EXCLAMATION " %d", warnings);
        }
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("%zu nodes", m_Graph.Nodes.size());
        // Hata ayıklayıcı: oyun kesme noktasında duraklatıldı
        if (m_BreakHitNode && EditorState::Get().IsPaused())
        {
            const Node* hit = m_Graph.FindNode(m_BreakHitNode);
            ImGui::SameLine(0, 16);
            ImGui::TextColored(UI::Colors::Error, ICON_FA_CIRCLE " Breakpoint: %s", hit ? m_Graph.ResolveDef(*hit).Title.c_str() : "?");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_PLAY " Resume (F8)"))
            {
                m_BreakHitNode = 0;
                EditorState::Get().SetSceneState(SceneState::Play);
            }
        }
        else if (!EditorState::Get().IsPaused())
            m_BreakHitNode = 0;
        ImGui::SameLine(0, 16);
        char zoomLabel[32];
        std::snprintf(zoomLabel, sizeof(zoomLabel), "%d%%##zoom", (int)std::round(m_Zoom * 100.0f));
        if (ImGui::SmallButton(zoomLabel))
        {
            // %100'e dön, görünümün ortası sabit
            ImNodesContext* previous = ImNodes::GetCurrentContext();
            ImNodes::SetCurrentContext(m_Context);
            SetZoom(1.0f, ImVec2(m_CanvasSize.x * 0.5f, m_CanvasSize.y * 0.5f));
            if (previous) ImNodes::SetCurrentContext(previous);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Zoom (mouse wheel). Click to reset to 100%%.");
    }

    void VisualScriptPanel::RenderSidebar()
    {
        // ── Değişkenler ──
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_TAG "  Variables");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 16.0f);
        if (ImGui::SmallButton(ICON_FA_PLUS "##addvar"))
        {
            PushUndo();
            std::string base = "newVar";
            std::string name = base;
            for (int i = 1; m_Graph.FindVariable(name); ++i) name = base + std::to_string(i);
            Variable v;
            v.Name = name;
            v.Type = PinType::Number;
            v.Default = "0";
            m_Graph.Variables.push_back(v);
            MarkEdited();
        }
        int removeIndex = -1;
        for (size_t i = 0; i < m_Graph.Variables.size(); ++i)
        {
            Variable& v = m_Graph.Variables[i];
            ImGui::PushID((int)i);
            ImGui::Separator();
            // Değişkeni tuvale sürükle: Get düğümü (Ctrl ile Set)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(PinColor(v.Type)));
            ImGui::TextUnformatted(ICON_FA_CIRCLE);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            char name[64] = {};
            std::strncpy(name, v.Name.c_str(), sizeof(name) - 1);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24.0f);
            if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                std::string newName = SanitizeIdentifier(name);
                if (!newName.empty() && newName != v.Name && !m_Graph.FindVariable(newName))
                {
                    PushUndo();
                    for (auto& n : m_Graph.Nodes)
                        if ((n.Type == "var.get" || n.Type == "var.set") && n.Properties["Variable"] == v.Name) n.Properties["Variable"] = newName;
                    v.Name = newName;
                    MarkEdited();
                }
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("VS_VARIABLE", v.Name.c_str(), v.Name.size() + 1);
                ImGui::Text("Get %s  (Ctrl: Set)", v.Name.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) removeIndex = (int)i;
            const char* types[] = { "number", "bool", "string", "vector", "list", "any" };
            PinType typeValues[] = { PinType::Number, PinType::Bool, PinType::String, PinType::Vector, PinType::List, PinType::Any };
            int current = 5;
            for (int t = 0; t < 6; ++t) if (typeValues[t] == v.Type) current = t;
            ImGui::SetNextItemWidth(84.0f);
            if (ImGui::Combo("##type", &current, types, 6))
            {
                PushUndo();
                v.Type = typeValues[current];
                v.Default = v.Type == PinType::Number ? "0" : v.Type == PinType::Bool ? "false" : "";
                MarkEdited();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 58.0f);
            if (v.Type == PinType::Number)
            {
                float f = (float)std::atof(v.Default.c_str());
                if (ImGui::DragFloat("##def", &f, 0.1f)) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); v.Default = b; MarkEdited(); }
            }
            else if (v.Type == PinType::Bool)
            {
                bool b = v.Default == "true";
                if (ImGui::Checkbox("##def", &b)) { v.Default = b ? "true" : "false"; MarkEdited(); }
            }
            else if (v.Type == PinType::String || v.Type == PinType::Vector)
            {
                char buf[128] = {};
                std::strncpy(buf, v.Default.c_str(), sizeof(buf) - 1);
                if (ImGui::InputTextWithHint("##def", v.Type == PinType::Vector ? "x, y, z" : "default", buf, sizeof(buf))) { v.Default = buf; MarkEdited(); }
            }
            else
                ImGui::TextDisabled("-");
            ImGui::SameLine();
            if (ImGui::Checkbox("##pub", &v.Public)) MarkEdited();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Public: shown in the Inspector");
            ImGui::PopID();
        }
        if (removeIndex >= 0)
        {
            PushUndo();
            std::string name = m_Graph.Variables[(size_t)removeIndex].Name;
            m_Graph.Variables.erase(m_Graph.Variables.begin() + removeIndex);
            MarkEdited();
            (void)name;
        }
        if (m_Graph.Variables.empty())
            ImGui::TextDisabled("No variables. Public variables appear in the Inspector.");

        // ── Derleme mesajları ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_LIST_CHECK "  Problems");
        if (m_Messages.empty())
            ImGui::TextDisabled("No problems.");
        for (size_t i = 0; i < m_Messages.size(); ++i)
        {
            const auto& m = m_Messages[i];
            ImGui::PushID((int)(1000 + i));
            ImGui::TextColored(m.IsWarning ? UI::Colors::Warning : UI::Colors::Error, m.IsWarning ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_XMARK);
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            std::string label = (m.NodeId ? "[" + std::to_string(m.NodeId) + "] " : std::string()) + m.Text;
            if (ImGui::Selectable(label.c_str(), false) && m.NodeId)
                m_FocusNode = m.NodeId;
            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }

        // ── Kısayollar ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Space/Right-click: add node");
        ImGui::TextDisabled("Drag a pin to empty space: add & connect");
        ImGui::TextDisabled("Del: delete   Ctrl+D: duplicate");
        ImGui::TextDisabled("Ctrl+C/V: copy/paste   F: frame");
        ImGui::TextDisabled("Ctrl+click link: detach");
    }

    void VisualScriptPanel::RenderCodeView()
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CODE "  Generated ConquerorScript");
        ImGui::Separator();
        ImFont* mono = ImGuiLayer::GetFont(ImGuiLayer::FontRole::Mono);
        if (mono) ImGui::PushFont(mono);
        ImGui::InputTextMultiline("##code", m_GeneratedCode.data(), m_GeneratedCode.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        if (mono) ImGui::PopFont();
    }

    bool VisualScriptPanel::DrawInputDefault(Node& node, const PinDef& pin)
    {
        std::string& value = node.InputDefaults[pin.Name];
        if (value.empty() && !pin.Default.empty() && !node.InputDefaults.count(pin.Name + "#set"))
            value = pin.Default;
        bool changed = false;
        ImGui::PushID(pin.Name.c_str());
        switch (pin.Type)
        {
            case PinType::Number:
            {
                float f = (float)std::atof(value.c_str());
                ImGui::SetNextItemWidth(Z(56.0f));
                if (ImGui::DragFloat("##v", &f, 0.1f, 0.0f, 0.0f, "%g")) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); value = b; changed = true; }
                break;
            }
            case PinType::Bool:
            {
                bool b = value == "true" || value == "1";
                if (ImGui::Checkbox("##v", &b)) { value = b ? "true" : "false"; changed = true; }
                break;
            }
            case PinType::Vector:
            {
                float v[3] = { 0, 0, 0 };
                std::sscanf(value.c_str(), "%f , %f , %f", &v[0], &v[1], &v[2]);
                ImGui::SetNextItemWidth(Z(120.0f));
                if (ImGui::DragFloat3("##v", v, 0.1f, 0.0f, 0.0f, "%g")) { char b[96]; std::snprintf(b, sizeof(b), "%g, %g, %g", v[0], v[1], v[2]); value = b; changed = true; }
                break;
            }
            case PinType::String:
            case PinType::Any:
            {
                char buf[256] = {};
                std::strncpy(buf, value.c_str(), sizeof(buf) - 1);
                ImGui::SetNextItemWidth(std::max(Z(70.0f), ImGui::CalcTextSize(buf).x + Z(16.0f)));
                if (ImGui::InputText("##v", buf, sizeof(buf))) { value = buf; changed = true; }
                break;
            }
            default: break;
        }
        ImGui::PopID();
        return changed;
    }

    bool VisualScriptPanel::DrawNodeProperties(Node& node)
    {
        bool changed = false;
        auto propText = [&](const char* key, float width, const char* hint) {
            std::string& v = node.Properties[key];
            char buf[128] = {};
            std::strncpy(buf, v.c_str(), sizeof(buf) - 1);
            ImGui::SetNextItemWidth(Z(width));
            if (ImGui::InputTextWithHint((std::string("##") + key).c_str(), hint, buf, sizeof(buf))) { v = buf; changed = true; }
        };
        auto keyCombo = [&](const char* key) {
            std::string& v = node.Properties[key];
            ImGui::SetNextItemWidth(Z(110.0f));
            if (ImGui::BeginCombo((std::string("##") + key).c_str(), v.c_str(), ImGuiComboFlags_HeightLarge))
            {
                static std::vector<std::string> s_Keys = KeyNames();
                for (auto& k : s_Keys)
                    if (ImGui::Selectable(k.c_str(), k == v)) { v = k; changed = true; }
                ImGui::EndCombo();
            }
        };
        if (node.Type == "event.custom") propText("Name", 130.0f, "event name");
        else if (node.Type == "event.keydown" || node.Type == "event.keyheld" || node.Type == "literal.key") keyCombo("Key");
        else if (node.Type == "event.mouse")
        {
            int b = std::atoi(node.Properties["Button"].c_str());
            const char* names[] = { "Left", "Right", "Middle" };
            ImGui::SetNextItemWidth(Z(90.0f));
            if (ImGui::Combo("##btn", &b, names, 3)) { node.Properties["Button"] = std::to_string(b); changed = true; }
        }
        else if (node.Type == "event.timer")
        {
            float f = (float)std::atof(node.Properties["Interval"].c_str());
            ImGui::SetNextItemWidth(Z(70.0f));
            if (ImGui::DragFloat("##int", &f, 0.05f, 0.01f, 3600.0f, "%.2f s")) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); node.Properties["Interval"] = b; changed = true; }
        }
        else if (node.Type == "literal.number")
        {
            float f = (float)std::atof(node.Properties["Value"].c_str());
            ImGui::SetNextItemWidth(Z(80.0f));
            if (ImGui::DragFloat("##val", &f, 0.1f, 0.0f, 0.0f, "%g")) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); node.Properties["Value"] = b; changed = true; }
        }
        else if (node.Type == "literal.string") propText("Value", 140.0f, "text");
        else if (node.Type == "literal.bool")
        {
            bool b = node.Properties["Value"] == "true";
            if (ImGui::Checkbox("##val", &b)) { node.Properties["Value"] = b ? "true" : "false"; changed = true; }
        }
        else if (node.Type == "var.get" || node.Type == "var.set")
        {
            std::string& v = node.Properties["Variable"];
            ImGui::SetNextItemWidth(Z(120.0f));
            if (ImGui::BeginCombo("##var", v.empty() ? "(select)" : v.c_str()))
            {
                for (auto& var : m_Graph.Variables)
                    if (ImGui::Selectable(var.Name.c_str(), var.Name == v)) { v = var.Name; changed = true; }
                ImGui::EndCombo();
            }
        }
        else if (node.Type == "comment")
        {
            std::string& v = node.Properties["Text"];
            char buf[1024] = {};
            std::strncpy(buf, v.c_str(), sizeof(buf) - 1);
            if (ImGui::InputTextMultiline("##text", buf, sizeof(buf), ImVec2(Z(220.0f), Z(70.0f)))) { v = buf; changed = true; }
        }
        else if (const NodeDef* base = NodeLibrary::Find(node.Type))
        {
            // Diğer düğümler: tanımdaki özellikler (açık / kapalı, seçim listesi, fonksiyon seçimi ya da metin)
            for (auto& [key, defValue] : base->Properties)
            {
                ImGui::PushID(key.c_str());
                ImGui::TextDisabled("%s", key.c_str());
                ImGui::SameLine();
                std::string& v = node.Properties[key];
                std::vector<const char*> options;
                if (key == "Phase")
                    options = node.Type == "event.inputaction" ? std::vector<const char*>{ "Started", "Performed", "Canceled" }
                                                               : std::vector<const char*>{ "Enter", "Stay", "Exit" };
                else if (key == "Physics")
                    options = { "3D", "2D" };
                if (defValue == "true" || defValue == "false")
                {
                    bool b = v == "true";
                    if (ImGui::Checkbox("##b", &b)) { v = b ? "true" : "false"; changed = true; }
                }
                else if (!options.empty())
                {
                    ImGui::SetNextItemWidth(Z(100.0f));
                    if (ImGui::BeginCombo("##c", v.c_str()))
                    {
                        for (const char* o : options)
                            if (ImGui::Selectable(o, v == o)) { v = o; changed = true; }
                        ImGui::EndCombo();
                    }
                }
                else if (key == "Function")
                {
                    ImGui::SetNextItemWidth(Z(130.0f));
                    if (ImGui::BeginCombo("##f", v.empty() ? "(select)" : v.c_str()))
                    {
                        for (auto& other : m_Graph.Nodes)
                        {
                            if (other.Type != "event.function") continue;
                            const std::string name = other.Properties["Name"];
                            if (ImGui::Selectable(name.c_str(), v == name)) { v = name; changed = true; }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                    propText(key.c_str(), 130.0f, key.c_str());
                ImGui::PopID();
            }
        }
        return changed;
    }

    void VisualScriptPanel::RenderNode(Node& node)
    {
        NodeDef def = m_Graph.ResolveDef(node);
        ImU32 title = TitleColor(def);
        bool hasError = m_ErrorNodes.count(node.Id) != 0;
        // Blueprint hata ayıklayıcısı: son 0.4 sn içinde yürütülen düğüm sarı çerçeveyle parlar
        const std::string debugKey = DebugKey();
        const double executedAgo = Debugger::Now() - Debugger::LastExecuted(debugKey, node.Id);
        const bool executed = Debugger::LastExecuted(debugKey, node.Id) >= 0.0 && executedAgo < 0.4;
        const bool breakpoint = Debugger::HasBreakpoint(debugKey, node.Id);
        const bool breakHit = m_BreakHitNode == node.Id && EditorState::Get().IsPaused();
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, Brighten(title, 1.15f));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, Brighten(title, 1.3f));
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline, hasError ? IM_COL32(255, 70, 70, 255)
                                                      : breakHit ? IM_COL32(255, 60, 60, 255)
                                                      : executed ? IM_COL32(255, 214, 90, (int)(255.0 * (1.0 - executedAgo / 0.4)))
                                                                 : IM_COL32(18, 18, 22, 255));
        const bool thick = hasError || breakHit || executed;
        if (thick) ImNodes::PushStyleVar(ImNodesStyleVar_NodeBorderThickness, 2.5f);

        ImNodes::BeginNode(node.Id);
        ImNodes::BeginNodeTitleBar();
        if (breakpoint)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), ICON_FA_CIRCLE);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Breakpoint (F9): play pauses when this node runs (at the end of that frame).");
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(KindIcon(def));
        ImGui::SameLine();
        ImGui::TextUnformatted(def.Title.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !def.Description.empty())
            ImGui::SetTooltip("%s", def.Description.c_str());
        if (const uint64_t hits = Debugger::HitCount(debugKey, node.Id); hits > 0 && !EditorState::Get().IsEditing())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("x%llu", (unsigned long long)hits);
        }
        ImNodes::EndNodeTitleBar();

        bool changed = false;
        // Düğüm özellikleri
        if (!def.Properties.empty() || node.Type == "var.get" || node.Type == "var.set" || def.Kind == NodeKind::Literal || node.Type == "comment")
        {
            ImNodes::BeginStaticAttribute(node.Id * 1024 + 1023); // pin olmayan, düğüme özgü kimlik
            changed |= DrawNodeProperties(node);
            ImNodes::EndStaticAttribute();
        }

        // Girişler (sol) ve çıkışlar (sağ) yan yana
        size_t rows = std::max(def.Inputs.size(), def.Outputs.size());
        float outWidth = 0.0f;
        for (auto& o : def.Outputs) outWidth = std::max(outWidth, ImGui::CalcTextSize(o.Name.c_str()).x);
        for (size_t r = 0; r < rows; ++r)
        {
            float startX = ImGui::GetCursorPosX();
            if (r < def.Inputs.size())
            {
                const PinDef& in = def.Inputs[r];
                ImNodes::PushColorStyle(ImNodesCol_Pin, PinColor(in.Type));
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, Brighten(PinColor(in.Type), 1.3f));
                bool connected = m_Graph.FindInputLink(node.Id, in.Name) != nullptr;
                ImNodesPinShape shape = in.Type == PinType::Exec ? (connected ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_Triangle)
                                                                 : (connected ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle);
                ImNodes::BeginInputAttribute(AttributeId(node.Id, (int)r, false), shape);
                if (in.Type != PinType::Exec || def.Inputs.size() > 1 || in.Name != "In")
                {
                    ImGui::TextUnformatted(in.Type == PinType::Exec && in.Name == "In" ? "" : in.Name.c_str());
                    if (!connected && in.Type != PinType::Exec && in.Type != PinType::List)
                    {
                        ImGui::SameLine();
                        changed |= DrawInputDefault(node, in);
                    }
                }
                else
                    ImGui::Dummy(ImVec2(Z(10.0f), ImGui::GetTextLineHeight()));
                ImNodes::EndInputAttribute();
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
            if (r < def.Outputs.size())
            {
                const PinDef& out = def.Outputs[r];
                if (r < def.Inputs.size()) ImGui::SameLine();
                float inputRight = ImGui::GetCursorPosX();
                float target = std::max(inputRight, startX + Z(120.0f));
                ImGui::SetCursorPosX(target);
                ImNodes::PushColorStyle(ImNodesCol_Pin, PinColor(out.Type));
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, Brighten(PinColor(out.Type), 1.3f));
                bool connected = !m_Graph.FindOutputLinks(node.Id, out.Name).empty();
                ImNodesPinShape shape = out.Type == PinType::Exec ? (connected ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_Triangle)
                                                                  : (connected ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle);
                ImNodes::BeginOutputAttribute(AttributeId(node.Id, (int)r, true), shape);
                float w = ImGui::CalcTextSize(out.Name.c_str()).x;
                ImGui::Indent(outWidth - w);
                ImGui::TextUnformatted(out.Type == PinType::Exec && out.Name == "Then" ? " " : out.Name.c_str());
                ImGui::Unindent(outWidth - w);
                ImNodes::EndOutputAttribute();
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }
        if (rows == 0 && def.Kind != NodeKind::Comment)
            ImGui::Dummy(ImVec2(Z(80.0f), Z(4.0f)));
        ImNodes::EndNode();

        if (thick) ImNodes::PopStyleVar();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        if (changed)
        {
            if (!m_EditingNodeValue)
            {
                PushUndo();
                m_EditingNodeValue = true;
            }
            MarkEdited();
            m_PositionsDirty = false;
        }
    }

    void VisualScriptPanel::RenderCanvas()
    {
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(m_Context);
        // Kısayollar: panel odaktayken ve bir metin alanı yazılmıyorken
        const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;

        ApplyZoomStyle();
        ImNodes::BeginNodeEditor();
        m_CanvasOrigin = ImGui::GetCursorScreenPos();
        m_CanvasSize = ImGui::GetContentRegionAvail();
        ImGuiIO& io = ImGui::GetIO();
        // Fare tekerleği: imlecin altındaki noktaya göre yakınlaştır
        if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive())
            SetZoom(m_Zoom * std::pow(1.12f, io.MouseWheel), ImVec2(io.MousePos.x - m_CanvasOrigin.x, io.MousePos.y - m_CanvasOrigin.y));
        if (m_HasFrameRequest)
        {
            m_Zoom = m_FrameZoom;
            ApplyZoomStyle();
            ImNodes::EditorContextResetPanning(ImVec2(m_CanvasSize.x * 0.5f - m_FrameRequest.x * m_Zoom, m_CanvasSize.y * 0.5f - m_FrameRequest.y * m_Zoom));
            m_PositionsDirty = true;
            m_HasFrameRequest = false;
        }
        if (m_PositionsDirty)
        {
            for (auto& n : m_Graph.Nodes)
                ImNodes::SetNodeGridSpacePos(n.Id, ImVec2(n.X * m_Zoom, n.Y * m_Zoom));
            m_PositionsDirty = false;
        }
        // Düğüm içeriği (yazı, boşluklar) zoom ile ölçeklenir
        ImGui::PushFont(nullptr, GImGui->FontSizeBase * m_Zoom);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * m_Zoom, ImGui::GetStyle().ItemSpacing.y * m_Zoom));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(ImGui::GetStyle().ItemInnerSpacing.x * m_Zoom, ImGui::GetStyle().ItemInnerSpacing.y * m_Zoom));
        for (auto& n : m_Graph.Nodes)
            RenderNode(n);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        // Bağlantılar (kaynak pin rengiyle)
        for (auto& l : m_Graph.Links)
        {
            const Node* from = m_Graph.FindNode(l.FromNode);
            const Node* to = m_Graph.FindNode(l.ToNode);
            if (!from || !to) continue;
            NodeDef fd = m_Graph.ResolveDef(*from), td = m_Graph.ResolveDef(*to);
            int fi = -1, ti = -1;
            for (size_t i = 0; i < fd.Outputs.size(); ++i) if (fd.Outputs[i].Name == l.FromPin) fi = (int)i;
            for (size_t i = 0; i < td.Inputs.size(); ++i) if (td.Inputs[i].Name == l.ToPin) ti = (int)i;
            if (fi < 0 || ti < 0) continue;
            ImU32 color = PinColor(fd.Outputs[(size_t)fi].Type);
            if (fd.Outputs[(size_t)fi].Type == PinType::Exec)
            {
                const double last = Debugger::LastExecuted(DebugKey(), l.FromNode);
                const double lastTo = Debugger::LastExecuted(DebugKey(), l.ToNode);
                if (last >= 0.0 && lastTo >= 0.0 && Debugger::Now() - lastTo < 0.4)
                    color = IM_COL32(255, 214, 90, 255);
            }
            ImNodes::PushColorStyle(ImNodesCol_Link, color);
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, Brighten(color, 1.3f));
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 200, 80, 255));
            ImNodes::Link(l.Id, AttributeId(l.FromNode, fi, true), AttributeId(l.ToNode, ti, false));
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
        if (m_ShowMinimap)
            ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        // Seçim isteği (yapıştırma)
        if (!m_PendingSelect.empty())
        {
            ImNodes::ClearNodeSelection();
            for (int id : m_PendingSelect) ImNodes::SelectNode(id);
            m_PendingSelect.clear();
        }
        if (m_FocusNode)
        {
            ImNodes::ClearNodeSelection();
            ImNodes::SelectNode(m_FocusNode);
            FocusNode(m_FocusNode);
            m_FocusNode = 0;
        }

        // Konumları geri oku (imnodes ızgara konumu / zoom = grafik konumu)
        bool moved = false;
        for (auto& n : m_Graph.Nodes)
        {
            ImVec2 p = ImNodes::GetNodeGridSpacePos(n.Id);
            p = ImVec2(p.x / m_Zoom, p.y / m_Zoom);
            if (std::fabs(p.x - n.X) > 0.5f || std::fabs(p.y - n.Y) > 0.5f)
            {
                n.X = std::round(p.x);
                n.Y = std::round(p.y);
                moved = true;
            }
        }
        if (moved)
        {
            if (!m_Dragging)
            {
                m_DragSnapshot = m_UndoStack.empty() || true ? m_PreFrameSnapshot : std::string();
                m_Dragging = true;
            }
            m_Dirty = true;
        }
        if (m_Dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Dragging = false;
            if (!m_DragSnapshot.empty())
            {
                m_UndoStack.push_back(m_DragSnapshot);
                m_RedoStack.clear();
            }
            m_DragSnapshot.clear();
        }
        if (!ImGui::IsAnyItemActive())
            m_EditingNodeValue = false;
        m_PreFrameSnapshot = m_Graph.ToJson();

        // Yeni bağlantı
        int start = 0, end = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            int sn, sp, en, ep;
            bool so, eo;
            DecodeAttribute(start, sn, sp, so);
            DecodeAttribute(end, en, ep, eo);
            if (!so) { std::swap(sn, en); std::swap(sp, ep); }
            const Node* a = m_Graph.FindNode(sn);
            const Node* b = m_Graph.FindNode(en);
            if (a && b)
            {
                NodeDef ad = m_Graph.ResolveDef(*a), bd = m_Graph.ResolveDef(*b);
                if (sp < (int)ad.Outputs.size() && ep < (int)bd.Inputs.size())
                {
                    PushUndo();
                    std::string err;
                    if (m_Graph.Connect(sn, ad.Outputs[(size_t)sp].Name, en, bd.Inputs[(size_t)ep].Name, &err))
                        MarkEdited();
                    else
                    {
                        m_UndoStack.pop_back();
                        m_Toast = "Cannot connect: " + err;
                        m_ToastTime = Now();
                    }
                }
            }
        }
        int destroyed = 0;
        if (ImNodes::IsLinkDestroyed(&destroyed))
        {
            PushUndo();
            m_Graph.Disconnect(destroyed);
            MarkEdited();
        }
        int dropped = -1;
        if (ImNodes::IsLinkDropped(&dropped, false))
        {
            m_PendingLinkAttribute = dropped;
            m_OpenSearch = true;
            m_SearchScreenPos = ImGui::GetMousePos();
        }

        // Tuval girişi
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        ImVec2 mouseGrid = ImVec2((io.MousePos.x - m_CanvasOrigin.x - pan.x) / m_Zoom, (io.MousePos.y - m_CanvasOrigin.y - pan.y) / m_Zoom);
        const ImVec2 rightDrag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && rightDrag.x * rightDrag.x + rightDrag.y * rightDrag.y < 36.0f)
        {
            int hoveredNode = 0;
            if (!ImNodes::IsNodeHovered(&hoveredNode))
            {
                m_OpenSearch = true;
                m_SearchScreenPos = io.MousePos;
                m_SearchGridPos = mouseGrid;
                m_PendingLinkAttribute = -1;
            }
        }
        if (m_OpenSearch && m_PendingLinkAttribute >= 0)
            m_SearchGridPos = mouseGrid;
        if (canvasFocused && !ImGui::IsAnyItemActive())
        {
            const bool ctrl = io.KeyCtrl || io.KeySuper;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) DeleteSelection();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(mouseGrid);
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) Duplicate();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
            if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameAll();
            if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
            {
                const int selected = ImNodes::NumSelectedNodes();
                std::vector<int> ids((size_t)std::max(selected, 0));
                if (selected > 0) ImNodes::GetSelectedNodes(ids.data());
                int hoveredNode = 0;
                if (ids.empty() && ImNodes::IsNodeHovered(&hoveredNode)) ids.push_back(hoveredNode);
                for (int id : ids) ToggleBreakpoint(id);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && EditorState::Get().IsPaused() && m_BreakHitNode)
            {
                m_BreakHitNode = 0;
                EditorState::Get().SetSceneState(SceneState::Play);
            }
            if (!ctrl && hovered && (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)))
            {
                m_OpenSearch = true;
                m_SearchScreenPos = io.MousePos;
                m_SearchGridPos = mouseGrid;
                m_PendingLinkAttribute = -1;
            }
        }

        // Değişken sürükle-bırak
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VS_VARIABLE"))
            {
                std::string name((const char*)payload->Data);
                CreateNodeAt(io.KeyCtrl ? "var.set" : "var.get", mouseGrid);
                m_Graph.Nodes.back().Properties["Variable"] = name;
                MarkEdited();
            }
            ImGui::EndDragDropTarget();
        }
        RenderSearchPopup();

        // Bildirim
        if (!m_Toast.empty() && Now() - m_ToastTime < 3.0)
        {
            ImVec2 p = ImVec2(ImGui::GetWindowPos().x + 12.0f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 36.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::CalcTextSize(m_Toast.c_str()).x + 20.0f, p.y + 26.0f), IM_COL32(120, 40, 40, 230), 4.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 10.0f, p.y + 5.0f), IM_COL32(255, 255, 255, 255), m_Toast.c_str());
        }
        if (m_Graph.Nodes.empty())
        {
            const char* hint = "Right-click or press Space to add a node";
            ImVec2 size = ImGui::CalcTextSize(hint);
            ImVec2 c = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), IM_COL32(140, 140, 150, 255), hint);
        }
        ImNodes::SetCurrentContext(previous ? previous : m_Context);
    }

    void VisualScriptPanel::RenderSearchPopup()
    {
        if (m_OpenSearch)
        {
            ImGui::OpenPopup("##vs_search");
            ImGui::SetNextWindowPos(m_SearchScreenPos);
            m_SearchBuffer[0] = 0;
            m_SearchSelected = 0;
            m_OpenSearch = false;
            m_FocusSearch = true;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(380, 460));
        if (!ImGui::BeginPopup("##vs_search"))
        {
            m_PendingLinkAttribute = -1;
            return;
        }
        if (m_FocusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputTextWithHint("##q", ICON_FA_MAGNIFYING_GLASS "  Search nodes", m_SearchBuffer, sizeof(m_SearchBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        std::string filter = m_SearchBuffer;

        // Bırakılan bağlantı varsa yalnızca uyumlu düğümler
        PinType linkType = PinType::Any;
        bool linkFromOutput = true, hasLink = false;
        if (m_PendingLinkAttribute >= 0)
        {
            int n, p;
            bool out;
            DecodeAttribute(m_PendingLinkAttribute, n, p, out);
            if (const Node* node = m_Graph.FindNode(n))
            {
                NodeDef d = m_Graph.ResolveDef(*node);
                const auto& pins = out ? d.Outputs : d.Inputs;
                if (p < (int)pins.size()) { linkType = pins[(size_t)p].Type; linkFromOutput = out; hasLink = true; }
            }
        }
        struct Entry { std::string Type, Label, Category, Desc, Variable; int Score; };
        std::vector<Entry> entries;
        auto compatible = [&](const NodeDef& d) {
            if (!hasLink) return true;
            const auto& pins = linkFromOutput ? d.Inputs : d.Outputs;
            for (auto& pin : pins)
                if (linkFromOutput ? PinsCompatible(linkType, pin.Type) : PinsCompatible(pin.Type, linkType)) return true;
            return false;
        };
        for (auto& d : NodeLibrary::All())
        {
            if (d.Id == "var.get" || d.Id == "var.set") continue;
            if (!compatible(d)) continue;
            int s = std::max(Score(d.Title, filter), Score(d.Category, filter) > 2 ? 1 : 0);
            if (s > 0) entries.push_back({ d.Id, d.Title, d.Category, d.Description, "", s });
        }
        // Değişken düğümleri
        for (auto& v : m_Graph.Variables)
        {
            for (int set = 0; set < 2; ++set)
            {
                std::string label = (set ? "Set " : "Get ") + v.Name;
                NodeDef d = *NodeLibrary::Find(set ? "var.set" : "var.get");
                for (auto& p : d.Inputs) if (p.Name == "Value") p.Type = v.Type;
                for (auto& p : d.Outputs) if (p.Name == "Value") p.Type = v.Type;
                if (!compatible(d)) continue;
                int s = Score(label, filter);
                if (s > 0) entries.push_back({ set ? "var.set" : "var.get", label, "Variables", "", v.Name, s + 1 });
            }
        }
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
            if (!filter.empty() && a.Score != b.Score) return a.Score > b.Score;
            if (filter.empty() && a.Category != b.Category)
            {
                // Sık kullanılanlar önce
                static const char* order[] = { "Events", "Flow", "Functions", "Variables", "Debug", "Math", "Logic", "Vector", "Text", "Literals", "Collections" };
                constexpr int kOrderCount = (int)(sizeof(order) / sizeof(order[0]));
                auto rank = [&](const std::string& c) { for (int i = 0; i < kOrderCount; ++i) if (c == order[i]) return i; return kOrderCount; };
                int ra = rank(a.Category), rb = rank(b.Category);
                if (ra != rb) return ra < rb;
                return a.Category < b.Category;
            }
            return a.Label.size() < b.Label.size();
        });
        int count = (int)entries.size();
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { m_SearchSelected = std::min(count - 1, m_SearchSelected + 1); m_SearchScrollToSelected = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { m_SearchSelected = std::max(0, m_SearchSelected - 1); m_SearchScrollToSelected = true; }
        if (filter != m_LastSearchFilter) { m_SearchSelected = 0; m_LastSearchFilter = filter; }
        m_SearchSelected = std::clamp(m_SearchSelected, 0, std::max(0, count - 1));

        int chosen = -1;
        ImGui::BeginChild("##results", ImVec2(0, std::min(380.0f, (float)std::max(1, count) * ImGui::GetTextLineHeightWithSpacing() + 40.0f)));
        if (filter.empty() && !hasLink)
        {
            // Kategori ağacı
            std::string currentCategory;
            bool open = false;
            for (int i = 0; i < count; ++i)
            {
                const Entry& e = entries[(size_t)i];
                std::string top = e.Category.substr(0, e.Category.find('/'));
                if (top != currentCategory)
                {
                    if (open) ImGui::TreePop();
                    currentCategory = top;
                    open = ImGui::TreeNodeEx(top.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | (top == "Events" || top == "Flow" ? ImGuiTreeNodeFlags_DefaultOpen : 0));
                }
                if (!open) continue;
                if (ImGui::Selectable((e.Label + "##" + e.Type + e.Variable).c_str(), false))
                    chosen = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !e.Desc.empty())
                    ImGui::SetTooltip("%s", e.Desc.c_str());
            }
            if (open) ImGui::TreePop();
        }
        else
        {
            for (int i = 0; i < count && i < 200; ++i)
            {
                const Entry& e = entries[(size_t)i];
                bool selected = i == m_SearchSelected;
                const float rowStart = ImGui::GetCursorPosX(), rowWidth = ImGui::GetContentRegionAvail().x;
                if (ImGui::Selectable((e.Label + "##" + e.Type + e.Variable).c_str(), selected))
                    chosen = i;
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                    if (m_SearchScrollToSelected) { ImGui::SetScrollHereY(); m_SearchScrollToSelected = false; }
                }
                ImGui::SameLine(rowStart + rowWidth - ImGui::CalcTextSize(e.Category.c_str()).x - 4.0f);
                ImGui::TextDisabled("%s", e.Category.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !e.Desc.empty())
                    ImGui::SetTooltip("%s", e.Desc.c_str());
            }
            if (count == 0)
                ImGui::TextDisabled("No matching nodes.");
        }
        ImGui::EndChild();
        if (enter && count > 0)
            chosen = m_SearchSelected;
        if (chosen >= 0)
        {
            const Entry& e = entries[(size_t)chosen];
            CreateNodeAt(e.Type, m_SearchGridPos);
            if (!e.Variable.empty())
            {
                m_Graph.Nodes.back().Properties["Variable"] = e.Variable;
                MarkEdited();
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_PendingLinkAttribute = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
