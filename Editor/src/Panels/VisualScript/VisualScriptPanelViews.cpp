#include "VisualScriptPanel.h"
#include "Core/Project/Project.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"
#include "UI/EditorUI.h"
#include "EditorState.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// Görsel betik paneli görünümleri: ikinci araç çubuğu (Unity Relations / Values / Dim / Carry / Align / Distribute / Lock / Graph
// Inspector, Unreal Find / Bookmarks / Diff / Debug), Graph Inspector, alt panel (Find Results, Call Stack, Watches, Diff), yorum
// kutuları ve yapışkan notlar, pin / düğüm bağlam menüleri, Event Dispatcher / Interface ve kullanıcı türleri kenar çubuğu bölümleri
namespace Conqueror::Editor
{
    using namespace Visual;
    namespace fs = std::filesystem;

    namespace
    {
        struct ContextScope
        {
            ImNodesContext* Previous;
            explicit ContextScope(ImNodesContext* ctx) : Previous(ImNodes::GetCurrentContext()) { if (ctx) ImNodes::SetCurrentContext(ctx); }
            ~ContextScope() { if (Previous) ImNodes::SetCurrentContext(Previous); }
        };

        // Açık / kapalı metin düğmesi (Unity araç çubuğu anahtarları)
        bool ToggleButton(const char* label, bool& value, const char* tip)
        {
            if (value) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const bool pressed = ImGui::SmallButton(label);
            if (value) ImGui::PopStyleColor();
            if (pressed) value = !value;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
            return pressed;
        }

        ImU32 CommentColorU(const std::string& name, int alpha)
        {
            struct C { const char* Name; int R, G, B; };
            static const C colors[] = { { "Gray", 120, 124, 135 }, { "Red", 190, 70, 70 }, { "Green", 70, 160, 90 }, { "Blue", 70, 120, 200 },
                                        { "Yellow", 200, 175, 60 }, { "Purple", 140, 90, 190 }, { "Orange", 210, 120, 50 }, { "Teal", 50, 150, 150 } };
            for (auto& c : colors)
                if (name == c.Name) return IM_COL32(c.R, c.G, c.B, alpha);
            return IM_COL32(120, 124, 135, alpha);
        }

        void StickyColors(const std::string& theme, ImU32& bg, ImU32& text)
        {
            if (theme == "Black") { bg = IM_COL32(24, 24, 26, 245); text = IM_COL32(235, 235, 235, 255); }
            else if (theme == "Dark") { bg = IM_COL32(62, 64, 72, 245); text = IM_COL32(225, 225, 230, 255); }
            else if (theme == "Green") { bg = IM_COL32(130, 200, 130, 245); text = IM_COL32(20, 35, 20, 255); }
            else if (theme == "Blue") { bg = IM_COL32(120, 165, 230, 245); text = IM_COL32(15, 25, 45, 255); }
            else { bg = IM_COL32(250, 232, 140, 245); text = IM_COL32(45, 38, 10, 255); }
        }

        constexpr const char* kColors[] = { "Gray", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "Teal" };
        constexpr const char* kThemes[] = { "Classic", "Black", "Dark", "Green", "Blue" };

        // Yeni varlık yolu: grafiğin klasöründe benzersiz ad (proje göreli)
        std::string NewAssetPath(const fs::path& graphPath, const std::string& base, const std::string& ext)
        {
            fs::path dir = graphPath.parent_path();
            std::error_code ec;
            fs::path p = dir / (base + ext);
            for (int i = 1; fs::exists(p, ec); ++i) p = dir / (base + std::to_string(i) + ext);
            const fs::path project = Project::GetActiveProjectDirectory();
            return project.empty() ? p.generic_string() : fs::relative(p, project, ec).generic_string();
        }
    }

    // ════════════════════════════════════════
    //  İkinci araç çubuğu
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderSecondToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        ToggleButton(ICON_FA_DIAGRAM_NEXT " Relations", m_ShowRelations, "Relations: show which ports of a node depend on each other");
        ToggleButton(ICON_FA_EYE " Values", m_ShowValues, "Values: show the live values of output ports while playing");
        if (ToggleButton(ICON_FA_CIRCLE_HALF_STROKE " Dim", m_Dim, "Dim: fade nodes that no event reaches (they never run)"))
            m_ReachableDirty = true;
        ToggleButton(ICON_FA_PEOPLE_CARRY_BOX " Carry", m_Carry, "Carry: dragging a node also moves the nodes connected to its outputs");
        const std::vector<int> selected = SelectedNodes();
        ImGui::BeginDisabled(selected.size() < 2);
        if (ImGui::SmallButton(ICON_FA_ALIGN_LEFT " Align"))
            ImGui::OpenPopup("##vs_align");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Align the selected nodes (select 2 or more)");
        if (ImGui::BeginPopup("##vs_align"))
        {
            const std::pair<const char*, AlignMode> modes[] = { { "Left Edges", AlignMode::Left }, { "Centers (horizontal)", AlignMode::Center },
                                                                { "Right Edges", AlignMode::Right }, { "Top Edges", AlignMode::Top },
                                                                { "Middles (vertical)", AlignMode::Middle }, { "Bottom Edges", AlignMode::Bottom } };
            for (auto& [label, mode] : modes)
                if (ImGui::MenuItem(label)) AlignNodes(selected, mode);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selected.size() < 3);
        if (ImGui::SmallButton(ICON_FA_ARROWS_LEFT_RIGHT " Distribute"))
            ImGui::OpenPopup("##vs_distribute");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Space the selected nodes evenly (select 3 or more)");
        if (ImGui::BeginPopup("##vs_distribute"))
        {
            if (ImGui::MenuItem("Horizontally")) DistributeNodes(selected, true);
            if (ImGui::MenuItem("Vertically")) DistributeNodes(selected, false);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ToggleButton(m_Locked ? ICON_FA_LOCK " Lock" : ICON_FA_LOCK_OPEN " Lock", m_Locked,
                     "Lock: keep this graph open when another entity with a visual script is selected");
        ToggleButton(ICON_FA_CIRCLE_INFO " Inspector", m_ShowInspector, "Graph Inspector: the selected node's ports, settings and documentation");
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_MAGNIFYING_GLASS " Find"))
        {
            m_ShowBottom = !(m_ShowBottom && m_BottomTab == 0);
            m_BottomTab = 0;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Find in graphs (Ctrl+F: this graph, Ctrl+Shift+F: all graphs)");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_BOOKMARK " Bookmarks"))
            ImGui::OpenPopup("##vs_bookmarks");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Bookmarks: Ctrl+1..9 sets, Shift+1..9 jumps");
        if (ImGui::BeginPopup("##vs_bookmarks"))
        {
            ImGui::SetNextItemWidth(160.0f);
            const bool enter = ImGui::InputTextWithHint("##bmname", "bookmark name", m_BookmarkName, sizeof(m_BookmarkName), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::SmallButton(ICON_FA_PLUS " Add") || enter) && m_BookmarkName[0])
            {
                AddBookmark(m_BookmarkName);
                m_BookmarkName[0] = 0;
            }
            ImGui::Separator();
            int remove = -1;
            for (size_t i = 0; i < m_Graph.Bookmarks.size(); ++i)
            {
                const auto& b = m_Graph.Bookmarks[i];
                ImGui::PushID((int)i);
                if (ImGui::Selectable((b.Name + (b.Macro.empty() ? "" : "  (" + b.Macro + ")")).c_str(), false, ImGuiSelectableFlags_DontClosePopups, ImVec2(180, 0)))
                    JumpToBookmark(b.Name);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove = (int)i;
                ImGui::PopID();
            }
            if (remove >= 0)
            {
                m_Graph.Bookmarks.erase(m_Graph.Bookmarks.begin() + remove);
                m_Dirty = true;
            }
            if (m_Graph.Bookmarks.empty()) ImGui::TextDisabled("No bookmarks.");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_CODE_COMPARE " Diff"))
            ImGui::OpenPopup("##vs_diff");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Compare this graph with the saved file or the Git HEAD version");
        if (ImGui::BeginPopup("##vs_diff"))
        {
            if (ImGui::MenuItem("Compare with Last Saved")) RunDiff("saved");
            if (ImGui::MenuItem("Compare with Git HEAD")) RunDiff("head");
            if (ImGui::MenuItem("Clear Diff Highlight", nullptr, false, !m_Diff.empty()))
            {
                m_Diff.clear();
                m_DiffAdded.clear();
                m_DiffChanged.clear();
                m_DiffMoved.clear();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_BUG " Debug"))
        {
            m_ShowBottom = !(m_ShowBottom && (m_BottomTab == 1 || m_BottomTab == 2));
            m_BottomTab = EditorState::Get().IsPaused() ? 1 : 2;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Call Stack and Watches (debugging)");
        ImGui::PopStyleVar();
        ImGui::NewLine();
    }

    // ════════════════════════════════════════
    //  Graph Inspector (Unity)
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderGraphInspector()
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CIRCLE_INFO "  Graph Inspector");
        ImGui::Separator();
        const std::vector<int> selected = SelectedNodes();
        Node* node = selected.size() == 1 ? G().FindNode(selected[0]) : (selected.empty() && m_SelectedBox ? G().FindNode(m_SelectedBox) : nullptr);
        if (!node)
        {
            if (selected.size() > 1)
            {
                ImGui::Text("%zu nodes selected", selected.size());
                if (ImGui::Button("Collapse to Function")) CollapseToFunction(selected, "NewFunction");
                if (ImGui::Button("Collapse to Macro")) CollapseToMacro(selected, "NewMacro");
                if (ImGui::Button("Straighten Connections")) StraightenNodes(selected);
                if (ImGui::Button("Add Comment")) AddCommentAround(selected, "Comment");
                return;
            }
            ImGui::TextUnformatted(m_EditMacro.empty() ? m_Graph.Name.c_str() : m_EditMacro.c_str());
            ImGui::TextDisabled("%s", m_EditMacro.empty() ? "Event Graph" : "Macro");
            ImGui::Spacing();
            ImGui::Text("Nodes: %zu   Connections: %zu", G().Nodes.size(), G().Links.size());
            ImGui::Text("Variables: %zu   Macros: %zu", m_Graph.Variables.size(), m_Graph.Macros.size());
            ImGui::Text("Dispatchers: %zu   Interfaces: %zu", m_Graph.Dispatchers.size(), m_Graph.Interfaces.size());
            bool tunnels = false;
            for (auto& n : m_Graph.Nodes)
                if (n.Type == "macro.inputs" || n.Type == "macro.outputs") tunnels = true;
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(tunnels ? "This graph has Inputs / Outputs nodes: other graphs can use it as a Subgraph node."
                                        : "Add Inputs / Outputs nodes to use this graph as a Subgraph in other graphs.");
            ImGui::TextDisabled("Select a node to see its ports, settings and documentation.");
            ImGui::PopTextWrapPos();
            return;
        }
        NodeDef d = G().ResolveDef(*node);
        const bool box = IsBoxNode(*node);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(box ? (node->Type == "comment" ? "Comment" : "Sticky Note") : d.Title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("%s  -  #%d  -  %s", d.Category.c_str(), node->Id, node->Type.c_str());
        // Düzenleme alanları yakınlaştırmadan bağımsız (Z() m_Zoom ile ölçekler)
        const float savedZoom = m_Zoom;
        m_Zoom = 1.0f;
        bool changed = false;
        if (box)
        {
            if (node->Type == "stickynote")
            {
                char title[128] = {};
                std::strncpy(title, node->Properties["Title"].c_str(), sizeof(title) - 1);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##ititle", title, sizeof(title))) { node->Properties["Title"] = title; changed = true; }
            }
            char text[2048] = {};
            std::strncpy(text, node->Properties["Text"].c_str(), sizeof(text) - 1);
            if (ImGui::InputTextMultiline("##itext", text, sizeof(text), ImVec2(-1, 90))) { node->Properties["Text"] = text; changed = true; }
            const bool sticky = node->Type == "stickynote";
            std::string& c = node->Properties[sticky ? "Theme" : "Color"];
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##icolor", c.empty() ? (sticky ? "Classic" : "Gray") : c.c_str()))
            {
                if (sticky)
                    for (const char* t : kThemes) { if (ImGui::Selectable(t, c == t)) { c = t; changed = true; } }
                else
                    for (const char* t : kColors) { if (ImGui::Selectable(t, c == t)) { c = t; changed = true; } }
                ImGui::EndCombo();
            }
        }
        else
        {
            bool disabled = node->Disabled;
            if (ImGui::Checkbox("Disabled", &disabled)) { m_Zoom = savedZoom; SetNodeDisabled(node->Id, disabled); m_Zoom = 1.0f; }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("A disabled node does nothing; execution passes through its first output");
            ImGui::SameLine();
            bool bp = Debugger::HasBreakpoint(DebugKey(), node->Id);
            if (ImGui::Checkbox("Breakpoint", &bp)) ToggleBreakpoint(node->Id);
            ImGui::Spacing();
            ImGui::TextColored(UI::Colors::Accent, "Documentation");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", d.Description.empty() ? "No description." : d.Description.c_str());
            ImGui::PopTextWrapPos();
            if (!d.Properties.empty() || node->Type == "var.get" || node->Type == "var.set" || d.Kind == NodeKind::Literal)
            {
                ImGui::Spacing();
                ImGui::TextColored(UI::Colors::Accent, "Settings");
                ImGui::PushID("##iprops");
                changed |= DrawNodeProperties(*node);
                ImGui::PopID();
            }
            const std::string key = DebugKey();
            const bool running = !EditorState::Get().IsEditing();
            if (!d.Inputs.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(UI::Colors::Accent, "Inputs");
                for (auto& in : d.Inputs)
                {
                    ImGui::PushID(in.Name.c_str());
                    ImGui::TextDisabled("%s", PinTypeName(in.Type));
                    ImGui::SameLine(64.0f);
                    ImGui::TextUnformatted(in.Name.c_str());
                    if (const Link* l = G().FindInputLink(node->Id, in.Name))
                    {
                        const Node* src = G().FindNode(l->FromNode);
                        ImGui::SameLine();
                        ImGui::TextDisabled("<- %s.%s", src ? G().ResolveDef(*src).Title.c_str() : "?", l->FromPin.c_str());
                        std::string value;
                        if (running && Debugger::GetValue(key, l->FromNode, l->FromPin, value))
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(UI::Colors::Warning, "= %s", value.c_str());
                        }
                    }
                    else if (in.Type != PinType::Exec && in.Type != PinType::List && in.Type != PinType::Dictionary)
                    {
                        ImGui::SameLine();
                        changed |= DrawInputDefault(*node, in);
                    }
                    ImGui::PopID();
                }
            }
            if (!d.Outputs.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(UI::Colors::Accent, "Outputs");
                for (auto& out : d.Outputs)
                {
                    ImGui::PushID(out.Name.c_str());
                    ImGui::TextDisabled("%s", PinTypeName(out.Type));
                    ImGui::SameLine(64.0f);
                    ImGui::TextUnformatted(out.Name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu)", G().FindOutputLinks(node->Id, out.Name).size());
                    if (out.Type != PinType::Exec)
                    {
                        ImGui::SameLine();
                        const bool watched = IsWatched(node->Id, out.Name);
                        if (ImGui::SmallButton(watched ? ICON_FA_EYE "##w" : ICON_FA_EYE_SLASH "##w")) ToggleWatch(node->Id, out.Name);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(watched ? "Stop watching this value" : "Watch this value");
                        std::string value;
                        if (running && Debugger::GetValue(key, node->Id, out.Name, value))
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(UI::Colors::Warning, "= %s", value.c_str());
                        }
                    }
                    ImGui::PopID();
                }
            }
            bool header = false;
            for (auto& m : m_Messages)
                if (m.NodeId == node->Id)
                {
                    if (!header) { ImGui::Spacing(); ImGui::TextColored(UI::Colors::Accent, "Problems"); header = true; }
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(m.IsWarning ? UI::Colors::Warning : UI::Colors::Error, "%s", m.Text.c_str());
                    ImGui::PopTextWrapPos();
                }
        }
        m_Zoom = savedZoom;
        if (changed)
        {
            if (!m_EditingNodeValue)
            {
                PushUndo();
                m_EditingNodeValue = true;
            }
            MarkEdited();
        }
        if (!ImGui::IsAnyItemActive())
            m_EditingNodeValue = false;
    }

    // ════════════════════════════════════════
    //  Alt panel: Find Results / Call Stack / Watches / Diff
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderBottomPanel()
    {
        auto navigate = [&](const fs::path& path, const std::string& macro, int node) {
            std::error_code ec;
            if (!path.empty() && (m_Path.empty() || !fs::equivalent(path, m_Path, ec)))
                Open(path);
            if (macro != m_EditMacro) EditMacro(macro);
            if (node) m_FocusNode = node;
        };
        if (ImGui::BeginTabBar("##vs_bottom_tabs"))
        {
            const char* tabs[] = { ICON_FA_MAGNIFYING_GLASS " Find Results", ICON_FA_LIST " Call Stack", ICON_FA_EYE " Watches", ICON_FA_CODE_COMPARE " Diff" };
            // Seçili sekme m_BottomTab'dır (kod da değiştirebilir: Find, Diff, Debug düğmeleri); tıklama onu günceller
            for (int t = 0; t < 4; ++t)
            {
                if (ImGui::BeginTabItem(tabs[t], nullptr, m_BottomTab == t ? ImGuiTabItemFlags_SetSelected : 0))
                    ImGui::EndTabItem();
                if (ImGui::IsItemClicked()) m_BottomTab = t;
            }
            if (ImGui::TabItemButton(ICON_FA_XMARK, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
                m_ShowBottom = false;
            ImGui::EndTabBar();
        }
        if (m_BottomTab == 0)
        {
            ImGui::SetNextItemWidth(260.0f);
            const bool enter = ImGui::InputTextWithHint("##find", ICON_FA_MAGNIFYING_GLASS "  node, variable, value ...", m_FindBuffer, sizeof(m_FindBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            ImGui::Checkbox("All graphs", &m_FindAll);
            ImGui::SameLine();
            if (ImGui::Button("Find") || enter) RunFind(m_FindBuffer, m_FindAll);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu result%s", m_FindResults.size(), m_FindResults.size() == 1 ? "" : "s");
            ImGui::BeginChild("##findres");
            const fs::path project = Project::GetActiveProjectDirectory();
            for (size_t i = 0; i < m_FindResults.size(); ++i)
            {
                const auto& r = m_FindResults[i];
                std::error_code ec;
                const std::string file = project.empty() ? r.Path.filename().string() : fs::relative(r.Path, project, ec).generic_string();
                ImGui::PushID((int)i);
                if (ImGui::Selectable((file + (r.Macro.empty() ? "" : " / " + r.Macro) + "   " + r.Text).c_str()))
                    navigate(r.Path, r.Macro, r.Node);
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        else if (m_BottomTab == 1)
        {
            if (!EditorState::Get().IsPaused() || !m_BreakHitNode)
                ImGui::TextDisabled("The call stack appears when play stops at a breakpoint (F9 on a node).");
            const auto frames = BuildCallStack();
            for (size_t i = 0; i < frames.size(); ++i)
            {
                ImGui::TextColored(i == 0 ? UI::Colors::Warning : UI::Colors::TextDim, "%s", i == 0 ? ICON_FA_ARROW_RIGHT : " ");
                ImGui::SameLine();
                ImGui::TextUnformatted(frames[i].c_str());
            }
        }
        else if (m_BottomTab == 2)
        {
            const std::string key = DebugKey();
            const bool running = !EditorState::Get().IsEditing();
            if (ImGui::BeginTable("##watches", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Source");
                ImGui::TableHeadersRow();
                int removeWatch = -1;
                for (size_t i = 0; i < G().Watches.size(); ++i)
                {
                    const auto& [nodeId, pin] = G().Watches[i];
                    const Node* n = G().FindNode(nodeId);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)i);
                    if (ImGui::SmallButton(ICON_FA_XMARK)) removeWatch = (int)i;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(pin.c_str());
                    ImGui::TableNextColumn();
                    std::string value;
                    if (running && Debugger::GetValue(key, nodeId, pin, value)) ImGui::TextUnformatted(value.c_str());
                    else ImGui::TextDisabled(running ? "(not run yet)" : "(not playing)");
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(n ? G().ResolveDef(*n).Title.c_str() : "(missing node)") && n) m_FocusNode = nodeId;
                    ImGui::PopID();
                }
                if (removeWatch >= 0) { G().Watches.erase(G().Watches.begin() + removeWatch); m_Dirty = true; }
                // Grafik değişkenlerinin canlı değerleri (bu grafiği çalıştıran varlıklar)
                Scene* scene = running ? ScriptContext::GetActiveScene() : nullptr;
                if (scene)
                {
                    auto view = scene->m_Registry.view<ConquerorScriptComponent>();
                    for (auto h : view)
                    {
                        Entity e{ h, scene };
                        for (auto& script : view.get<ConquerorScriptComponent>(h).Scripts)
                        {
                            if (!script.Instance || !script.Runtime) continue;
                            fs::path p = script.ScriptPath;
                            if (p.is_relative() && !Project::GetActiveProjectDirectory().empty()) p = Project::GetActiveProjectDirectory() / p;
                            std::error_code ec;
                            if (!fs::equivalent(p, m_Path, ec)) continue;
                            auto* runtime = static_cast<Scripting::ScriptRuntime*>(script.Runtime);
                            for (auto& v : m_Graph.Variables)
                            {
                                if (v.Scope != VariableScope::Graph) continue;
                                Scripting::ScriptValue live;
                                std::string display;
                                if (!runtime->GetField(script.Instance, v.Name, live, &display)) continue;
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted((ICON_FA_TAG " " + v.Name).c_str());
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(display.c_str());
                                ImGui::TableNextColumn();
                                ImGui::TextDisabled("%s", e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag.c_str() : "entity");
                            }
                        }
                    }
                }
                ImGui::EndTable();
            }
            if (G().Watches.empty())
                ImGui::TextDisabled("Right-click an output pin > Watch this value. Graph variables are listed while playing.");
        }
        else
        {
            if (ImGui::Button("Compare with Last Saved")) RunDiff("saved");
            ImGui::SameLine();
            if (ImGui::Button("Compare with Git HEAD")) RunDiff("head");
            ImGui::SameLine();
            if (!m_DiffError.empty()) ImGui::TextColored(UI::Colors::Error, "%s", m_DiffError.c_str());
            else if (!m_DiffAgainst.empty())
                ImGui::TextDisabled("%zu difference%s against %s", m_Diff.size(), m_Diff.size() == 1 ? "" : "s", m_DiffAgainst == "head" ? "Git HEAD" : "the saved file");
            ImGui::BeginChild("##diffres");
            for (size_t i = 0; i < m_Diff.size(); ++i)
            {
                const auto& e = m_Diff[i];
                const ImVec4 color = e.Type == DiffEntry::Kind::NodeAdded || e.Type == DiffEntry::Kind::LinkAdded || e.Type == DiffEntry::Kind::VariableAdded || e.Type == DiffEntry::Kind::MacroAdded
                                         ? UI::Colors::Success
                                     : e.Type == DiffEntry::Kind::NodeRemoved || e.Type == DiffEntry::Kind::LinkRemoved || e.Type == DiffEntry::Kind::VariableRemoved || e.Type == DiffEntry::Kind::MacroRemoved
                                         ? UI::Colors::Error
                                         : UI::Colors::Warning;
                ImGui::PushID((int)i);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                if (ImGui::Selectable((std::string(DiffKindName(e.Type)) + ":  " + e.Text).c_str()) && e.NodeId && m_Graph.FindNode(e.NodeId))
                {
                    if (!m_EditMacro.empty()) EditMacro("");
                    m_FocusNode = e.NodeId;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    // ════════════════════════════════════════
    //  Yorum kutuları (Unreal) ve yapışkan notlar (Unity)
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderBoxes()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pan = ImNodes::EditorContextGetPanning();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGuiIO& io = ImGui::GetIO();
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * m_Zoom;
        const float header = fontSize + 10.0f * m_Zoom;
        for (auto& n : G().Nodes)
        {
            if (!IsBoxNode(n)) continue;
            const bool sticky = n.Type == "stickynote";
            const ImVec2 tl(m_CanvasOrigin.x + pan.x + n.X * m_Zoom, m_CanvasOrigin.y + pan.y + n.Y * m_Zoom);
            const ImVec2 br(tl.x + n.Width * m_Zoom, tl.y + n.Height * m_Zoom);
            const bool selected = m_SelectedBox == n.Id;
            ImU32 body, head, text;
            if (sticky)
            {
                StickyColors(n.Properties.count("Theme") ? n.Properties.at("Theme") : "Classic", body, text);
                head = body;
            }
            else
            {
                const std::string color = n.Properties.count("Color") ? n.Properties.at("Color") : "Gray";
                body = CommentColorU(color, 46);
                head = CommentColorU(color, 200);
                text = IM_COL32(245, 245, 248, 255);
            }
            dl->AddRectFilled(tl, br, body, 5.0f * m_Zoom);
            dl->AddRectFilled(tl, ImVec2(br.x, tl.y + header), head, 5.0f * m_Zoom, ImDrawFlags_RoundCornersTop);
            dl->AddRect(tl, br, selected ? IM_COL32(255, 200, 80, 255) : (sticky ? IM_COL32(0, 0, 0, 90) : head), 5.0f * m_Zoom, 0, selected ? 2.0f : 1.0f);
            const std::string title = sticky ? (n.Properties.count("Title") ? n.Properties.at("Title") : "Note")
                                             : (n.Properties.count("Text") ? n.Properties.at("Text") : "Comment");
            const std::string firstLine = title.substr(0, title.find('\n'));
            dl->PushClipRect(tl, br, true);
            dl->AddText(font, fontSize, ImVec2(tl.x + 8.0f * m_Zoom, tl.y + 5.0f * m_Zoom), text, firstLine.c_str());
            if (sticky && n.Properties.count("Text"))
            {
                dl->AddLine(ImVec2(tl.x + 6.0f * m_Zoom, tl.y + header), ImVec2(br.x - 6.0f * m_Zoom, tl.y + header), IM_COL32(0, 0, 0, 60));
                dl->AddText(font, fontSize * 0.95f, ImVec2(tl.x + 8.0f * m_Zoom, tl.y + header + 6.0f * m_Zoom), text, n.Properties.at("Text").c_str(), nullptr,
                            n.Width * m_Zoom - 16.0f * m_Zoom);
            }
            dl->PopClipRect();
            // Boyutlandırma tutamacı
            const float grip = 12.0f * m_Zoom;
            dl->AddTriangleFilled(ImVec2(br.x - grip, br.y - 2.0f), ImVec2(br.x - 2.0f, br.y - grip), ImVec2(br.x - 2.0f, br.y - 2.0f), sticky ? IM_COL32(0, 0, 0, 80) : head);
            // Etkileşim: üstünde düğüm yokken başlık sürükleme / çift tık düzenleme / sağ tık menü, köşeden boyutlandırma
            if (m_LastHoveredNode != 0 && m_DragBox != n.Id)
                continue;
            ImGui::PushID(n.Id);
            ImGui::SetCursorScreenPos(tl);
            ImGui::InvisibleButton("##boxhead", ImVec2(std::max(n.Width * m_Zoom, 4.0f), std::max(header, 4.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_SelectedBox = n.Id;
                m_DragBox = n.Id;
                m_BoxSnapshot = m_Graph.ToJson();
                m_BoxMoved = false;
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
                // Yorum kutusu içindeki düğümler onunla taşınır (Unreal)
                m_BoxContents.clear();
                if (!sticky)
                    for (auto& other : G().Nodes)
                    {
                        if (other.Id == n.Id) continue;
                        ImVec2 size = IsBoxNode(other) ? ImVec2(other.Width, other.Height) : NodeDimensions(other.Id);
                        if (!IsBoxNode(other)) size = size.x > 1.0f ? ImVec2(size.x / m_Zoom, size.y / m_Zoom) : ImVec2(200.0f, 100.0f);
                        if (other.X >= n.X && other.Y >= n.Y && other.X + size.x <= n.X + n.Width && other.Y + size.y <= n.Y + n.Height)
                            m_BoxContents.push_back(other.Id);
                    }
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) && m_DragBox == n.Id)
            {
                const float dx = io.MouseDelta.x / m_Zoom, dy = io.MouseDelta.y / m_Zoom;
                if (!m_BoxMoved)
                {
                    m_UndoStack.push_back(m_BoxSnapshot);
                    m_RedoStack.clear();
                    m_BoxMoved = true;
                }
                n.X += dx;
                n.Y += dy;
                for (int id : m_BoxContents)
                    if (Node* c = G().FindNode(id)) { c->X += dx; c->Y += dy; }
                m_PositionsDirty = true;
                m_Dirty = true;
            }
            if (ImGui::IsItemDeactivated() && m_DragBox == n.Id)
            {
                n.X = std::round(n.X);
                n.Y = std::round(n.Y);
                for (int id : m_BoxContents)
                    if (Node* c = G().FindNode(id)) { c->X = std::round(c->X); c->Y = std::round(c->Y); }
                m_DragBox = 0;
                m_PositionsDirty = true;
            }
            if (ImGui::IsItemHovered() && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)))
            {
                m_SelectedBox = n.Id;
                m_EditBox = n.Id;
                m_OpenBoxEdit = true;
            }
            ImGui::SetCursorScreenPos(ImVec2(br.x - grip, br.y - grip));
            ImGui::InvisibleButton("##boxsize", ImVec2(grip, grip));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            if (ImGui::IsItemActivated()) { m_UndoStack.push_back(m_Graph.ToJson()); m_RedoStack.clear(); }
            if (ImGui::IsItemActive() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            {
                n.Width = std::max(90.0f, n.Width + io.MouseDelta.x / m_Zoom);
                n.Height = std::max(56.0f, n.Height + io.MouseDelta.y / m_Zoom);
                m_Dirty = true;
            }
            if (ImGui::IsItemDeactivated()) { n.Width = std::round(n.Width); n.Height = std::round(n.Height); }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(cursor);
        // Düzenleme menüsü (çift tık / sağ tık)
        if (m_OpenBoxEdit)
        {
            ImGui::OpenPopup("##boxedit");
            m_OpenBoxEdit = false;
        }
        if (ImGui::BeginPopup("##boxedit"))
        {
            Node* n = G().FindNode(m_EditBox);
            if (!n || !IsBoxNode(*n))
                ImGui::CloseCurrentPopup();
            else
            {
                const bool sticky = n->Type == "stickynote";
                bool changed = false;
                ImGui::TextColored(UI::Colors::Accent, sticky ? ICON_FA_NOTE_STICKY "  Sticky Note" : ICON_FA_COMMENT "  Comment");
                if (sticky)
                {
                    char title[128] = {};
                    std::strncpy(title, n->Properties["Title"].c_str(), sizeof(title) - 1);
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::InputText("Title", title, sizeof(title))) { n->Properties["Title"] = title; changed = true; }
                }
                char buf[2048] = {};
                std::strncpy(buf, n->Properties["Text"].c_str(), sizeof(buf) - 1);
                if (ImGui::InputTextMultiline("##boxtext", buf, sizeof(buf), ImVec2(300, 90))) { n->Properties["Text"] = buf; changed = true; }
                std::string& c = n->Properties[sticky ? "Theme" : "Color"];
                if (sticky)
                {
                    for (const char* t : kThemes)
                    {
                        if (ImGui::RadioButton(t, c == t || (c.empty() && std::strcmp(t, "Classic") == 0))) { c = t; changed = true; }
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                }
                else
                {
                    for (const char* t : kColors)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(CommentColorU(t, 255)));
                        if (ImGui::Button((std::string(c == t ? ICON_FA_CHECK : " ") + "##" + t).c_str(), ImVec2(24, 20))) { c = t; changed = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t);
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                }
                if (ImGui::Button(ICON_FA_TRASH " Delete"))
                {
                    PushUndo();
                    G().RemoveNode(n->Id);
                    m_SelectedBox = 0;
                    MarkEdited();
                    ImGui::CloseCurrentPopup();
                }
                else if (changed)
                {
                    m_Dirty = true;
                    m_NeedsCompile = true;
                }
            }
            ImGui::EndPopup();
        }
    }

    // ════════════════════════════════════════
    //  Bağlam menüleri
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderNodeContextMenu()
    {
        if (m_OpenNodeContext)
        {
            ImGui::OpenPopup("##vs_nodectx");
            m_OpenNodeContext = false;
        }
        if (!ImGui::BeginPopup("##vs_nodectx"))
            return;
        Node* node = G().FindNode(m_ContextNode);
        if (!node)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        std::vector<int> selected = SelectedNodes();
        if (selected.empty()) selected.push_back(node->Id);
        ImGui::TextDisabled("%s", G().ResolveDef(*node).Title.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_COPY "  Duplicate", "Ctrl+D")) Duplicate();
        if (ImGui::MenuItem(ICON_FA_CLIPBOARD "  Copy", "Ctrl+C")) CopySelection();
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete", "Del")) DeleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem(node->Disabled ? ICON_FA_TOGGLE_ON "  Enable Node" : ICON_FA_BAN "  Disable Node"))
        {
            const bool disable = !node->Disabled;
            for (int id : selected) SetNodeDisabled(id, disable);
        }
        if (ImGui::MenuItem(ICON_FA_CIRCLE "  Toggle Breakpoint", "F9"))
            for (int id : selected) ToggleBreakpoint(id);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_CUBE "  Collapse to Function")) CollapseToFunction(selected, "NewFunction");
        if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Collapse to Macro", nullptr, false, m_EditMacro.empty())) CollapseToMacro(selected, "NewMacro");
        if (ImGui::MenuItem(ICON_FA_RULER_HORIZONTAL "  Straighten Connections", "Q")) StraightenNodes(selected);
        if (ImGui::MenuItem(ICON_FA_COMMENT "  Add Comment to Selection", "C")) AddCommentAround(selected, "Comment");
        if (ImGui::BeginMenu(ICON_FA_ALIGN_LEFT "  Alignment", selected.size() >= 2))
        {
            if (ImGui::MenuItem("Align Left")) AlignNodes(selected, AlignMode::Left);
            if (ImGui::MenuItem("Align Center")) AlignNodes(selected, AlignMode::Center);
            if (ImGui::MenuItem("Align Right")) AlignNodes(selected, AlignMode::Right);
            if (ImGui::MenuItem("Align Top")) AlignNodes(selected, AlignMode::Top);
            if (ImGui::MenuItem("Align Middle")) AlignNodes(selected, AlignMode::Middle);
            if (ImGui::MenuItem("Align Bottom")) AlignNodes(selected, AlignMode::Bottom);
            ImGui::Separator();
            if (ImGui::MenuItem("Distribute Horizontally", nullptr, false, selected.size() >= 3)) DistributeNodes(selected, true);
            if (ImGui::MenuItem("Distribute Vertically", nullptr, false, selected.size() >= 3)) DistributeNodes(selected, false);
            ImGui::EndMenu();
        }
        if (node->Type == "subgraph.instance" || node->Type == "macro.instance")
        {
            ImGui::Separator();
            if (node->Type == "macro.instance" && ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Open Macro")) EditMacro(node->Properties["Macro"]);
            if (node->Type == "subgraph.instance" && ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Open Subgraph") && !node->Properties["Graph"].empty())
                Open(UserTypes::ResolvePath(node->Properties["Graph"]));
        }
        ImGui::EndPopup();
    }

    void VisualScriptPanel::RenderPinContextMenu()
    {
        if (m_OpenPinContext)
        {
            ImGui::OpenPopup("##vs_pinctx");
            m_OpenPinContext = false;
        }
        if (!ImGui::BeginPopup("##vs_pinctx"))
            return;
        int nodeId, pinIndex;
        bool output;
        DecodeAttribute(m_ContextPinAttr, nodeId, pinIndex, output);
        const Node* node = G().FindNode(nodeId);
        const NodeDef d = node ? G().ResolveDef(*node) : NodeDef();
        const auto& pins = output ? d.Outputs : d.Inputs;
        if (!node || pinIndex >= (int)pins.size())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const PinDef pin = pins[(size_t)pinIndex];
        ImGui::TextDisabled("%s  (%s)", pin.Name.c_str(), PinTypeName(pin.Type));
        ImGui::Separator();
        if (pin.Type != PinType::Exec)
        {
            // Giriş pini: bağlı kaynağın değeri izlenir
            int watchNode = nodeId;
            std::string watchPin = pin.Name;
            if (!output)
                if (const Link* l = G().FindInputLink(nodeId, pin.Name)) { watchNode = l->FromNode; watchPin = l->FromPin; }
            const bool canWatch = output || G().FindInputLink(nodeId, pin.Name);
            if (ImGui::MenuItem(IsWatched(watchNode, watchPin) ? ICON_FA_EYE_SLASH "  Stop Watching This Value" : ICON_FA_EYE "  Watch This Value", nullptr, false, canWatch))
                ToggleWatch(watchNode, watchPin);
            if (ImGui::MenuItem(ICON_FA_TAG "  Promote to Variable"))
                PromoteToVariable(nodeId, pin.Name, output, pin.Name);
        }
        if (ImGui::MenuItem(ICON_FA_LINK_SLASH "  Break Link(s)"))
        {
            PushUndo();
            std::vector<int> ids;
            for (auto& l : G().Links)
                if ((output && l.FromNode == nodeId && l.FromPin == pin.Name) || (!output && l.ToNode == nodeId && l.ToPin == pin.Name)) ids.push_back(l.Id);
            for (int id : ids) G().Disconnect(id);
            MarkEdited();
        }
        ImGui::EndPopup();
    }

    // ════════════════════════════════════════
    //  Kenar çubuğu: Event Dispatchers / Interfaces, kullanıcı türleri
    // ════════════════════════════════════════
    void VisualScriptPanel::RenderDispatchersAndInterfaces()
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_BOLT "  Event Dispatchers");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton(ICON_FA_PLUS "##adddisp"))
        {
            PushUndo();
            std::string name = "OnDispatched";
            for (int i = 1; std::find(m_Graph.Dispatchers.begin(), m_Graph.Dispatchers.end(), name) != m_Graph.Dispatchers.end(); ++i) name = "OnDispatched" + std::to_string(i);
            m_Graph.Dispatchers.push_back(name);
            MarkEdited();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New event dispatcher: Call broadcasts it, Bind connects custom events (also of other entities)");
        int removeDisp = -1;
        for (size_t i = 0; i < m_Graph.Dispatchers.size(); ++i)
        {
            ImGui::PushID((int)(7000 + i));
            char buf[64] = {};
            std::strncpy(buf, m_Graph.Dispatchers[i].c_str(), sizeof(buf) - 1);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24.0f);
            if (ImGui::InputText("##disp", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const std::string newName = SanitizeIdentifier(buf);
                if (!newName.empty() && std::find(m_Graph.Dispatchers.begin(), m_Graph.Dispatchers.end(), newName) == m_Graph.Dispatchers.end())
                {
                    PushUndo();
                    for (auto& n : m_Graph.Nodes)
                        if (n.Type.rfind("dispatcher.", 0) == 0 && n.Properties["Dispatcher"] == m_Graph.Dispatchers[i]) n.Properties["Dispatcher"] = newName;
                    m_Graph.Dispatchers[i] = newName;
                    MarkEdited();
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) removeDisp = (int)i;
            ImGui::PopID();
        }
        if (removeDisp >= 0)
        {
            PushUndo();
            m_Graph.Dispatchers.erase(m_Graph.Dispatchers.begin() + removeDisp);
            MarkEdited();
        }
        if (m_Graph.Dispatchers.empty()) ImGui::TextDisabled("No event dispatchers.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_LINK "  Interfaces");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton(ICON_FA_PLUS "##addiface"))
            ImGui::OpenPopup("##implement");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Implement an interface asset (.cqinterface)");
        if (ImGui::BeginPopup("##implement"))
        {
            const auto all = UserTypes::FindAssets(".cqinterface");
            for (auto& path : all)
            {
                const bool has = std::find(m_Graph.Interfaces.begin(), m_Graph.Interfaces.end(), path) != m_Graph.Interfaces.end();
                if (ImGui::MenuItem(UserTypes::DisplayName(path).c_str(), nullptr, has, !has))
                {
                    PushUndo();
                    m_Graph.Interfaces.push_back(path);
                    MarkEdited();
                }
            }
            if (all.empty()) ImGui::TextDisabled("No interface assets. Create one under User Types.");
            ImGui::EndPopup();
        }
        int removeIface = -1;
        for (size_t i = 0; i < m_Graph.Interfaces.size(); ++i)
        {
            const std::string& path = m_Graph.Interfaces[i];
            ImGui::PushID((int)(8000 + i));
            const InterfaceAsset* ia = UserTypes::LoadInterface(path);
            const bool open = ImGui::TreeNodeEx(UserTypes::DisplayName(path).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
            if (ImGui::SmallButton(ICON_FA_XMARK)) removeIface = (int)i;
            if (open)
            {
                if (!ia) ImGui::TextColored(UI::Colors::Error, "Missing: %s", path.c_str());
                else
                    for (auto& f : ia->Functions)
                    {
                        bool implemented = false;
                        for (auto& n : m_Graph.Nodes)
                            if (n.Type == "event.interface" && n.Properties["Interface"] == path && n.Properties["Function"] == f.Name) implemented = true;
                        ImGui::TextUnformatted(f.Name.c_str());
                        ImGui::SameLine();
                        if (implemented) ImGui::TextDisabled("(implemented)");
                        else if (ImGui::SmallButton((ICON_FA_PLUS " Event##" + f.Name).c_str()))
                        {
                            const ImVec2 at = ScreenToGraph(ImVec2(m_CanvasOrigin.x + m_CanvasSize.x * 0.3f, m_CanvasOrigin.y + m_CanvasSize.y * 0.3f));
                            if (!m_EditMacro.empty()) EditMacro("");
                            CreateNodeAt("event.interface", at, { { "Interface", path }, { "Function", f.Name } });
                        }
                    }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeIface >= 0)
        {
            PushUndo();
            m_Graph.Interfaces.erase(m_Graph.Interfaces.begin() + removeIface);
            MarkEdited();
        }
        if (m_Graph.Interfaces.empty()) ImGui::TextDisabled("No interfaces implemented.");
    }

    void VisualScriptPanel::RenderUserTypes()
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_TABLE_COLUMNS "  User Types");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton(ICON_FA_PLUS "##addtype"))
            ImGui::OpenPopup("##newtype");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New struct, enum or interface asset (saved next to this graph)");
        if (ImGui::BeginPopup("##newtype"))
        {
            if (ImGui::MenuItem("Struct"))
            {
                StructAsset a;
                a.Name = "NewStruct";
                a.Fields = { { "Value", PinType::Number, "0" } };
                const std::string p = NewAssetPath(m_Path, "NewStruct", ".cqstruct");
                a.Name = fs::path(p).stem().string();
                UserTypes::SaveStruct(p, a);
            }
            if (ImGui::MenuItem("Enum"))
            {
                EnumAsset a;
                const std::string p = NewAssetPath(m_Path, "NewEnum", ".cqenum");
                a.Name = fs::path(p).stem().string();
                a.Entries = { "First", "Second" };
                UserTypes::SaveEnum(p, a);
            }
            if (ImGui::MenuItem("Interface"))
            {
                InterfaceAsset a;
                const std::string p = NewAssetPath(m_Path, "NewInterface", ".cqinterface");
                a.Name = fs::path(p).stem().string();
                a.Functions = { { "Interact", { "Instigator" }, false } };
                UserTypes::SaveInterface(p, a);
            }
            ImGui::EndPopup();
        }
        int shown = 0;
        // Yapılar
        for (auto& path : UserTypes::FindAssets(".cqstruct"))
        {
            const StructAsset* sa = UserTypes::LoadStruct(path);
            if (!sa) continue;
            ++shown;
            ImGui::PushID(path.c_str());
            if (ImGui::TreeNodeEx((ICON_FA_TABLE_COLUMNS "  " + sa->Name).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                StructAsset edit = *sa;
                bool save = false;
                int remove = -1;
                for (size_t i = 0; i < edit.Fields.size(); ++i)
                {
                    auto& f = edit.Fields[i];
                    ImGui::PushID((int)i);
                    char name[64] = {};
                    std::strncpy(name, f.Name.c_str(), sizeof(name) - 1);
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::InputText("##fn", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) { f.Name = SanitizeIdentifier(name); save = true; }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(64.0f);
                    if (ImGui::BeginCombo("##ft", PinTypeName(f.Type)))
                    {
                        for (PinType t : { PinType::Number, PinType::Bool, PinType::String, PinType::Vector, PinType::List, PinType::Dictionary, PinType::Any })
                            if (ImGui::Selectable(PinTypeName(t), t == f.Type)) { f.Type = t; save = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    char def[64] = {};
                    std::strncpy(def, f.Default.c_str(), sizeof(def) - 1);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24.0f);
                    if (ImGui::InputTextWithHint("##fd", "default", def, sizeof(def), ImGuiInputTextFlags_EnterReturnsTrue)) { f.Default = def; save = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK)) remove = (int)i;
                    ImGui::PopID();
                }
                if (remove >= 0) { edit.Fields.erase(edit.Fields.begin() + remove); save = true; }
                if (ImGui::SmallButton(ICON_FA_PLUS " Field"))
                {
                    std::string n = "Field";
                    for (int k = 1; std::any_of(edit.Fields.begin(), edit.Fields.end(), [&](const StructField& x) { return x.Name == n; }); ++k) n = "Field" + std::to_string(k);
                    edit.Fields.push_back({ n, PinType::Number, "0" });
                    save = true;
                }
                if (save)
                {
                    UserTypes::SaveStruct(path, edit);
                    m_NeedsCompile = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        // Sayımlar
        for (auto& path : UserTypes::FindAssets(".cqenum"))
        {
            const EnumAsset* ea = UserTypes::LoadEnum(path);
            if (!ea) continue;
            ++shown;
            ImGui::PushID(path.c_str());
            if (ImGui::TreeNodeEx((ICON_FA_LIST "  " + ea->Name).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                EnumAsset edit = *ea;
                bool save = false;
                int remove = -1;
                for (size_t i = 0; i < edit.Entries.size(); ++i)
                {
                    ImGui::PushID((int)i);
                    char name[64] = {};
                    std::strncpy(name, edit.Entries[i].c_str(), sizeof(name) - 1);
                    ImGui::TextDisabled("%zu", i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24.0f);
                    if (ImGui::InputText("##en", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) { edit.Entries[i] = SanitizeIdentifier(name); save = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK)) remove = (int)i;
                    ImGui::PopID();
                }
                if (remove >= 0) { edit.Entries.erase(edit.Entries.begin() + remove); save = true; }
                if (ImGui::SmallButton(ICON_FA_PLUS " Entry"))
                {
                    edit.Entries.push_back("Entry" + std::to_string(edit.Entries.size()));
                    save = true;
                }
                if (save)
                {
                    UserTypes::SaveEnum(path, edit);
                    m_NeedsCompile = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        // Arayüzler
        for (auto& path : UserTypes::FindAssets(".cqinterface"))
        {
            const InterfaceAsset* ia = UserTypes::LoadInterface(path);
            if (!ia) continue;
            ++shown;
            ImGui::PushID(path.c_str());
            if (ImGui::TreeNodeEx((ICON_FA_LINK "  " + ia->Name).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                InterfaceAsset edit = *ia;
                bool save = false;
                int remove = -1;
                for (size_t i = 0; i < edit.Functions.size(); ++i)
                {
                    auto& f = edit.Functions[i];
                    ImGui::PushID((int)i);
                    char name[64] = {};
                    std::strncpy(name, f.Name.c_str(), sizeof(name) - 1);
                    ImGui::SetNextItemWidth(80.0f);
                    if (ImGui::InputText("##ifn", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) { f.Name = SanitizeIdentifier(name); save = true; }
                    ImGui::SameLine();
                    std::string inputs;
                    for (size_t k = 0; k < f.Inputs.size(); ++k) inputs += (k ? ", " : "") + f.Inputs[k];
                    char in[128] = {};
                    std::strncpy(in, inputs.c_str(), sizeof(in) - 1);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 52.0f);
                    if (ImGui::InputTextWithHint("##ifi", "inputs: A, B", in, sizeof(in), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        f.Inputs.clear();
                        std::string part;
                        std::stringstream ss(in);
                        while (std::getline(ss, part, ','))
                        {
                            const std::string id = SanitizeIdentifier(part.substr(part.find_first_not_of(' ') == std::string::npos ? 0 : part.find_first_not_of(' ')));
                            if (id != "_") f.Inputs.push_back(id);
                        }
                        save = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("##ret", &f.HasReturn)) save = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Returns a value");
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK)) remove = (int)i;
                    ImGui::PopID();
                }
                if (remove >= 0) { edit.Functions.erase(edit.Functions.begin() + remove); save = true; }
                if (ImGui::SmallButton(ICON_FA_PLUS " Function"))
                {
                    edit.Functions.push_back({ "Function" + std::to_string(edit.Functions.size()), {}, false });
                    save = true;
                }
                if (save)
                {
                    UserTypes::SaveInterface(path, edit);
                    m_NeedsCompile = true;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (shown == 0) ImGui::TextDisabled("No structs, enums or interfaces.");
    }
}
