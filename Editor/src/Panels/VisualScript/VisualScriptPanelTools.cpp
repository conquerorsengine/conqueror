#include "VisualScriptPanel.h"
#include "Core/Automation/Automation.h"
#include "Core/Logging/Log.h"
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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

// Görsel betik paneli: Unity Visual Scripting araç çubuğu (Relations, Values, Dim, Carry, Align, Distribute, Lock, Graph Inspector) ve
// Unreal Blueprint araçları (reroute, Collapse to Function / Macro, Watch, Promote to Variable, Straighten, Find in Blueprints,
// yer imleri, Disable Node, yorum kutuları, çağrı yığını + izleme penceresi, Diff), kullanıcı türleri ve otomasyon (vsx)
namespace Conqueror::Editor
{
    using namespace Visual;
    namespace fs = std::filesystem;

    namespace
    {
        // ImNodes bağlamını bir kapsam boyunca bu panelinkine ayarlar
        struct ContextScope
        {
            ImNodesContext* Previous;
            explicit ContextScope(ImNodesContext* ctx) : Previous(ImNodes::GetCurrentContext()) { if (ctx) ImNodes::SetCurrentContext(ctx); }
            ~ContextScope() { if (Previous) ImNodes::SetCurrentContext(Previous); }
        };

        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::string ShellQuote(const std::string& s)
        {
#if defined(_WIN32)
            return "\"" + s + "\"";
#else
            std::string out = "'";
            for (char c : s)
                out += c == '\'' ? std::string("'\\''") : std::string(1, c);
            return out + "'";
#endif
        }

        // Komutu çalıştırır, stdout'u döndürür (git show HEAD:...)
        int RunCapture(const std::string& command, std::string& output)
        {
            output.clear();
#if defined(_WIN32)
            FILE* pipe = _popen(command.c_str(), "r");
#else
            FILE* pipe = popen(command.c_str(), "r");
#endif
            if (!pipe) return -1;
            char buffer[4096];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
                output.append(buffer, n);
#if defined(_WIN32)
            return _pclose(pipe);
#else
            const int status = pclose(pipe);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }

    }

    // ════════════════════════════════════════
    //  Yardımcılar
    // ════════════════════════════════════════
    bool VisualScriptPanel::IsBoxNode(const Node& n) const { return n.Type == "comment" || n.Type == "stickynote"; }

    ImVec2 VisualScriptPanel::NodeDimensions(int id) const
    {
        if (!m_RenderedNodes.count(id))
            return ImVec2(0.0f, 0.0f);
        ContextScope scope(m_Context);
        return ImNodes::GetNodeDimensions(id);
    }

    std::vector<int> VisualScriptPanel::SelectedNodes() const
    {
        ContextScope scope(m_Context);
        const int count = ImNodes::NumSelectedNodes();
        std::vector<int> ids((size_t)std::max(count, 0));
        if (count > 0) ImNodes::GetSelectedNodes(ids.data());
        return ids;
    }

    Graph* VisualScriptPanel::GraphForMacro(const std::string& macro)
    {
        if (macro.empty()) return &m_Graph;
        for (auto& m : m_Graph.Macros)
            if (m.Name == macro && m.Body) return m.Body.get();
        return nullptr;
    }

    bool& VisualScriptPanel::Toggle(const std::string& name)
    {
        static bool s_Unknown = false;
        if (name == "relations") return m_ShowRelations;
        if (name == "values") return m_ShowValues;
        if (name == "dim") return m_Dim;
        if (name == "carry") return m_Carry;
        if (name == "lock") return m_Locked;
        if (name == "inspector") return m_ShowInspector;
        if (name == "minimap") return m_ShowMinimap;
        if (name == "code") return m_ShowCode;
        if (name == "bottom") return m_ShowBottom;
        s_Unknown = false;
        return s_Unknown;
    }

    bool VisualScriptPanel::IsDimmed(int node)
    {
        if (!m_Dim) return false;
        if (m_ReachableDirty)
        {
            m_Reachable = G().ReachableNodes();
            m_ReachableDirty = false;
        }
        const Node* n = G().FindNode(node);
        return n && !IsBoxNode(*n) && !m_Reachable.count(node);
    }

    int VisualScriptPanel::RelationCount(int node) const
    {
        const Graph& g = const_cast<VisualScriptPanel*>(this)->G();
        const Node* n = g.FindNode(node);
        if (!n) return 0;
        const NodeDef d = g.ResolveDef(*n);
        int execIn = 0, execOut = 0, dataIn = 0, dataOut = 0;
        for (auto& p : d.Inputs) (p.Type == PinType::Exec ? execIn : dataIn)++;
        for (auto& p : d.Outputs) (p.Type == PinType::Exec ? execOut : dataOut)++;
        return execIn * execOut + (execIn ? dataIn : dataIn * dataOut);
    }

    // ════════════════════════════════════════
    //  Seçimi izleme / kilit (Unity Lock)
    // ════════════════════════════════════════
    void VisualScriptPanel::FollowSelection()
    {
        Entity e = EditorState::Get().GetSelectedEntity();
        const uint64_t key = e ? (((uint64_t)(uintptr_t)e.GetScene()) << 20) ^ (uint64_t)(uint32_t)e : 0ull;
        if (key == m_LastSelection)
            return;
        const bool first = m_LastSelection == ~0ull;
        m_LastSelection = key;
        if (first || m_Locked || !e || !e.HasComponent<ConquerorScriptComponent>())
            return;
        for (auto& script : e.GetComponent<ConquerorScriptComponent>().Scripts)
        {
            fs::path path = script.ScriptPath;
            if (path.extension() != ".cqgraph") continue;
            if (path.is_relative() && !Project::GetActiveProjectDirectory().empty()) path = Project::GetActiveProjectDirectory() / path;
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
            if (!m_Path.empty() && fs::equivalent(path, m_Path, ec)) return;
            Open(path);
            return;
        }
    }

    // ════════════════════════════════════════
    //  Hizala / dağıt / düzleştir / taşı
    // ════════════════════════════════════════
    namespace
    {
        ImVec2 NodeSize(const VisualScriptPanel& panel, const Node& n, float zoom)
        {
            if (n.Width > 0.0f && (n.Type == "comment" || n.Type == "stickynote")) return ImVec2(n.Width, n.Height);
            const ImVec2 d = panel.NodeDimensions(n.Id);
            return d.x > 1.0f ? ImVec2(d.x / zoom, d.y / zoom) : ImVec2(200.0f, 100.0f);
        }
    }

    void VisualScriptPanel::AlignNodes(const std::vector<int>& ids, AlignMode mode)
    {
        if (ids.size() < 2) return;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (int id : ids)
            if (const Node* n = G().FindNode(id))
            {
                const ImVec2 s = NodeSize(*this, *n, m_Zoom);
                minX = std::min(minX, n->X);
                minY = std::min(minY, n->Y);
                maxX = std::max(maxX, n->X + s.x);
                maxY = std::max(maxY, n->Y + s.y);
            }
        PushUndo();
        for (int id : ids)
            if (Node* n = G().FindNode(id))
            {
                const ImVec2 s = NodeSize(*this, *n, m_Zoom);
                switch (mode)
                {
                    case AlignMode::Left: n->X = minX; break;
                    case AlignMode::Center: n->X = std::round((minX + maxX) * 0.5f - s.x * 0.5f); break;
                    case AlignMode::Right: n->X = std::round(maxX - s.x); break;
                    case AlignMode::Top: n->Y = minY; break;
                    case AlignMode::Middle: n->Y = std::round((minY + maxY) * 0.5f - s.y * 0.5f); break;
                    case AlignMode::Bottom: n->Y = std::round(maxY - s.y); break;
                }
            }
        MarkEdited();
    }

    void VisualScriptPanel::DistributeNodes(const std::vector<int>& ids, bool horizontal)
    {
        std::vector<Node*> nodes;
        for (int id : ids)
            if (Node* n = G().FindNode(id)) nodes.push_back(n);
        if (nodes.size() < 3) return;
        std::sort(nodes.begin(), nodes.end(), [&](Node* a, Node* b) { return horizontal ? a->X < b->X : a->Y < b->Y; });
        float total = 0.0f;
        for (Node* n : nodes)
        {
            const ImVec2 s = NodeSize(*this, *n, m_Zoom);
            total += horizontal ? s.x : s.y;
        }
        Node* first = nodes.front();
        Node* last = nodes.back();
        const ImVec2 lastSize = NodeSize(*this, *last, m_Zoom);
        const float span = horizontal ? (last->X + lastSize.x - first->X) : (last->Y + lastSize.y - first->Y);
        const float gap = (span - total) / (float)(nodes.size() - 1);
        PushUndo();
        float cursor = horizontal ? first->X : first->Y;
        for (Node* n : nodes)
        {
            const ImVec2 s = NodeSize(*this, *n, m_Zoom);
            if (horizontal) n->X = std::round(cursor);
            else n->Y = std::round(cursor);
            cursor += (horizontal ? s.x : s.y) + gap;
        }
        MarkEdited();
    }

    void VisualScriptPanel::StraightenNodes(const std::vector<int>& ids)
    {
        // Unreal Straighten Connections: her düğüm ilk (tercihen exec) giriş bağlantısının kaynağına göre dikeyde hizalanır
        std::vector<Node*> nodes;
        for (int id : ids)
            if (Node* n = G().FindNode(id)) nodes.push_back(n);
        std::sort(nodes.begin(), nodes.end(), [](Node* a, Node* b) { return a->X < b->X; });
        bool changed = false;
        std::string snapshot = m_Graph.ToJson();
        for (Node* n : nodes)
        {
            const Link* best = nullptr;
            bool bestExec = false;
            for (auto& l : G().Links)
            {
                if (l.ToNode != n->Id || l.FromNode == n->Id) continue;
                PinType t = PinType::Any;
                if (const Node* src = G().FindNode(l.FromNode))
                    for (auto& o : G().ResolveDef(*src).Outputs)
                        if (o.Name == l.FromPin) t = o.Type;
                const bool exec = t == PinType::Exec;
                if (!best || (exec && !bestExec)) { best = &l; bestExec = exec; }
            }
            if (!best) continue;
            const Node* src = G().FindNode(best->FromNode);
            if (!src) continue;
            auto offset = [&](int side, int node, const std::string& pin) {
                auto it = m_PinOffsets[side].find(node);
                if (it == m_PinOffsets[side].end()) return 12.0f;
                auto p = it->second.find(pin);
                return p == it->second.end() ? 12.0f : p->second;
            };
            const float y = std::round(src->Y + offset(1, src->Id, best->FromPin) - offset(0, n->Id, best->ToPin));
            if (std::fabs(y - n->Y) > 0.5f)
            {
                n->Y = y;
                changed = true;
            }
        }
        if (changed)
        {
            m_UndoStack.push_back(snapshot);
            m_RedoStack.clear();
            MarkEdited();
        }
    }

    void VisualScriptPanel::MoveNodes(const std::map<int, ImVec2>& deltas)
    {
        PushUndo();
        std::map<int, ImVec2> all = deltas;
        if (m_Carry)
            for (auto& [id, d] : deltas)
            {
                std::vector<int> stack{ id };
                std::set<int> seen{ id };
                while (!stack.empty())
                {
                    const int cur = stack.back();
                    stack.pop_back();
                    for (auto& l : G().Links)
                        if (l.FromNode == cur && seen.insert(l.ToNode).second)
                        {
                            stack.push_back(l.ToNode);
                            if (!all.count(l.ToNode)) all[l.ToNode] = d;
                        }
                }
            }
        for (auto& [id, d] : all)
            if (Node* n = G().FindNode(id)) { n->X += d.x; n->Y += d.y; }
        MarkEdited();
    }

    // ════════════════════════════════════════
    //  Collapse / Promote / Watch / Disable / Comment
    // ════════════════════════════════════════
    int VisualScriptPanel::CollapseToFunction(const std::vector<int>& ids, const std::string& name)
    {
        PushUndo();
        std::string err;
        const int call = G().CollapseToFunction(std::set<int>(ids.begin(), ids.end()), name, &err);
        if (!call)
        {
            m_UndoStack.pop_back();
            m_Toast = "Collapse to Function: " + err;
            m_ToastTime = ImGui::GetTime();
            return 0;
        }
        ContextScope scope(m_Context);
        ImNodes::ClearNodeSelection();
        MarkEdited();
        return call;
    }

    int VisualScriptPanel::CollapseToMacro(const std::vector<int>& ids, const std::string& name)
    {
        if (!m_EditMacro.empty())
        {
            m_Toast = "Collapse to Macro works in the Event Graph";
            m_ToastTime = ImGui::GetTime();
            return 0;
        }
        PushUndo();
        std::string err;
        const int inst = m_Graph.CollapseToMacro(std::set<int>(ids.begin(), ids.end()), name, &err);
        if (!inst)
        {
            m_UndoStack.pop_back();
            m_Toast = "Collapse to Macro: " + err;
            m_ToastTime = ImGui::GetTime();
            return 0;
        }
        for (auto& m : m_Graph.Macros)
            if (m.Body) m.Body->MacroScope = &m_Graph;
        ContextScope scope(m_Context);
        ImNodes::ClearNodeSelection();
        MarkEdited();
        return inst;
    }

    int VisualScriptPanel::PromoteToVariable(int node, const std::string& pin, bool output, const std::string& name)
    {
        PushUndo();
        std::string err;
        const size_t before = G().Variables.size();
        const int created = G().PromoteToVariable(node, pin, output, name, &err);
        if (!created)
        {
            m_UndoStack.pop_back();
            m_Toast = "Promote to Variable: " + err;
            m_ToastTime = ImGui::GetTime();
            return 0;
        }
        // Makro gövdesinde: değişken olay grafiğine aittir
        if (&G() != &m_Graph && G().Variables.size() > before)
        {
            Variable v = G().Variables.back();
            G().Variables.pop_back();
            if (!m_Graph.FindVariable(v.Name)) m_Graph.Variables.push_back(v);
        }
        MarkEdited();
        return created;
    }

    bool VisualScriptPanel::IsWatched(int node, const std::string& pin) const
    {
        const Graph& g = const_cast<VisualScriptPanel*>(this)->G();
        return std::find(g.Watches.begin(), g.Watches.end(), std::make_pair(node, pin)) != g.Watches.end();
    }

    void VisualScriptPanel::ToggleWatch(int node, const std::string& pin)
    {
        auto& w = G().Watches;
        auto it = std::find(w.begin(), w.end(), std::make_pair(node, pin));
        if (it != w.end()) w.erase(it);
        else w.push_back({ node, pin });
        m_Dirty = true;
    }

    void VisualScriptPanel::SetNodeDisabled(int node, bool disabled)
    {
        Node* n = G().FindNode(node);
        if (!n || n->Disabled == disabled) return;
        PushUndo();
        n->Disabled = disabled;
        MarkEdited();
    }

    int VisualScriptPanel::AddCommentAround(const std::vector<int>& ids, const std::string& text)
    {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (int id : ids)
            if (const Node* n = G().FindNode(id))
            {
                const ImVec2 s = NodeSize(*this, *n, m_Zoom);
                minX = std::min(minX, n->X);
                minY = std::min(minY, n->Y);
                maxX = std::max(maxX, n->X + s.x);
                maxY = std::max(maxY, n->Y + s.y);
            }
        if (minX > maxX) return 0;
        PushUndo();
        const float pad = 24.0f, header = 30.0f;
        const int id = G().AddNode("comment", minX - pad, minY - pad - header);
        Node* c = G().FindNode(id);
        c->Width = maxX - minX + pad * 2.0f;
        c->Height = maxY - minY + pad * 2.0f + header;
        c->Properties["Text"] = text;
        m_SelectedBox = id;
        MarkEdited();
        return id;
    }

    // ════════════════════════════════════════
    //  Bul / Fark / Yer imleri / Çağrı yığını
    // ════════════════════════════════════════
    const std::vector<VisualScriptPanel::FindResult>& VisualScriptPanel::RunFind(const std::string& text, bool allGraphs)
    {
        m_FindResults.clear();
        const std::string needle = Lower(text);
        if (needle.empty()) return m_FindResults;
        auto search = [&](const Graph& g, const fs::path& path, const std::string& macro) {
            for (auto& v : g.Variables)
                if (Lower(v.Name).find(needle) != std::string::npos)
                    m_FindResults.push_back({ path, macro, 0, "Variable " + v.Name });
            for (auto& n : g.Nodes)
            {
                const NodeDef d = g.ResolveDef(n);
                std::string where;
                if (Lower(d.Title).find(needle) != std::string::npos) where = "title";
                else if (Lower(n.Type).find(needle) != std::string::npos) where = "type";
                for (auto& [k, v] : n.Properties)
                    if (where.empty() && Lower(v).find(needle) != std::string::npos) where = k;
                for (auto& [k, v] : n.InputDefaults)
                    if (where.empty() && Lower(v).find(needle) != std::string::npos) where = k;
                if (!where.empty())
                    m_FindResults.push_back({ path, macro, n.Id, d.Title + (where == "title" ? "" : "  (" + where + ")") });
            }
        };
        auto searchFile = [&](const Graph& g, const fs::path& path) {
            search(g, path, "");
            for (auto& m : g.Macros)
                if (m.Body) search(*m.Body, path, m.Name);
        };
        if (!allGraphs)
            searchFile(m_Graph, m_Path);
        else
        {
            std::error_code ec;
            for (auto& rel : UserTypes::FindAssets(".cqgraph"))
            {
                const fs::path full = UserTypes::ResolvePath(rel);
                if (!m_Path.empty() && fs::equivalent(full, m_Path, ec))
                {
                    searchFile(m_Graph, m_Path);   // açık grafiğin kaydedilmemiş hali
                    continue;
                }
                Graph g;
                if (g.Load(full.string()))
                {
                    for (auto& m : g.Macros)
                        if (m.Body) m.Body->MacroScope = &g;
                    searchFile(g, full);
                }
            }
        }
        return m_FindResults;
    }

    bool VisualScriptPanel::RunDiff(const std::string& against, std::string* error)
    {
        m_Diff.clear();
        m_DiffAdded.clear();
        m_DiffChanged.clear();
        m_DiffMoved.clear();
        m_DiffAgainst = against;
        m_DiffError.clear();
        std::string json;
        if (against == "head")
        {
            std::string top;
            const std::string dir = m_Path.parent_path().string();
            if (RunCapture("git -C " + ShellQuote(dir) + " rev-parse --show-toplevel 2>" + std::string(
#if defined(_WIN32)
                "NUL"
#else
                "/dev/null"
#endif
                ), top) != 0 || top.empty())
            {
                m_DiffError = "The graph is not in a Git repository.";
                if (error) *error = m_DiffError;
                return false;
            }
            while (!top.empty() && (top.back() == '\n' || top.back() == '\r')) top.pop_back();
            std::error_code ec;
            const std::string rel = fs::relative(m_Path, top, ec).generic_string();
            if (RunCapture("git -C " + ShellQuote(top) + " show " + ShellQuote("HEAD:" + rel), json) != 0 || json.empty())
            {
                m_DiffError = "The graph is not in the Git HEAD commit.";
                if (error) *error = m_DiffError;
                return false;
            }
        }
        else
        {
            std::ifstream f(m_Path, std::ios::binary);
            if (!f)
            {
                m_DiffError = "The graph has not been saved yet.";
                if (error) *error = m_DiffError;
                return false;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            json = ss.str();
        }
        Graph base;
        if (!base.FromJson(json))
        {
            m_DiffError = "The other version cannot be read.";
            if (error) *error = m_DiffError;
            return false;
        }
        for (auto& m : base.Macros)
            if (m.Body) m.Body->MacroScope = &base;
        m_Diff = DiffGraphs(base, m_Graph);
        for (auto& e : m_Diff)
        {
            if (e.Type == DiffEntry::Kind::NodeAdded) m_DiffAdded.insert(e.NodeId);
            else if (e.Type == DiffEntry::Kind::NodeChanged) m_DiffChanged.insert(e.NodeId);
            else if (e.Type == DiffEntry::Kind::NodeMoved) m_DiffMoved.insert(e.NodeId);
        }
        m_ShowBottom = true;
        m_BottomTab = 3;
        return true;
    }

    void VisualScriptPanel::AddBookmark(const std::string& name)
    {
        // Görünümün ortası (grafik birimi) ve yakınlaştırma
        ContextScope scope(m_Context);
        const ImVec2 pan = ImNodes::EditorContextGetPanning();
        const ImVec2 size = m_CanvasSize.x > 1.0f ? m_CanvasSize : ImVec2(800, 600);
        Bookmark b;
        b.Name = name;
        b.Macro = m_EditMacro;
        b.X = (size.x * 0.5f - pan.x) / m_Zoom;
        b.Y = (size.y * 0.5f - pan.y) / m_Zoom;
        b.Zoom = m_Zoom;
        auto& list = m_Graph.Bookmarks;
        auto it = std::find_if(list.begin(), list.end(), [&](const Bookmark& x) { return x.Name == name; });
        if (it != list.end()) *it = b;
        else list.push_back(b);
        m_Dirty = true;
        m_Toast = "Bookmark " + name + " set";
        m_ToastTime = ImGui::GetTime();
    }

    bool VisualScriptPanel::JumpToBookmark(const std::string& name)
    {
        for (auto& b : m_Graph.Bookmarks)
            if (b.Name == name)
            {
                if (b.Macro != m_EditMacro) EditMacro(b.Macro);
                m_FrameZoom = std::clamp(b.Zoom, 0.25f, 2.0f);
                m_FrameRequest = ImVec2(b.X, b.Y);
                m_HasFrameRequest = true;
                return true;
            }
        return false;
    }

    std::vector<std::string> VisualScriptPanel::BuildCallStack() const
    {
        // Kesme noktasına kadar yürütülen düğümlerden: bulunduğu olay / fonksiyon, fonksiyonu çağıran düğüm, ... (Unreal Call Stack)
        std::vector<std::string> frames;
        const auto trail = Debugger::GetBreakTrail();
        if (trail.empty()) return frames;
        auto& self = *const_cast<VisualScriptPanel*>(this);
        const std::string key = self.DebugKey();
        std::unordered_map<std::string, std::shared_ptr<Graph>> loaded;
        auto graphOf = [&](const std::string& g) -> const Graph* {
            if (g == key) return &self.m_Graph;
            auto it = loaded.find(g);
            if (it != loaded.end()) return it->second.get();
            auto gr = std::make_shared<Graph>();
            const std::string path = Debugger::GetGraphPath(g);
            if (path.empty() || !gr->Load(path)) gr.reset();
            loaded[g] = gr;
            return gr.get();
        };
        auto describe = [&](const std::string& g, int id, NodeDef* out) {
            const Graph* gr = graphOf(g);
            const Node* n = gr ? gr->FindNode(id) : nullptr;
            if (!n) return g + " node " + std::to_string(id);
            NodeDef d = gr->ResolveDef(*n);
            if (out) *out = d;
            return d.Title;
        };
        int i = (int)trail.size() - 1;
        std::string frame = describe(trail[(size_t)i].first, trail[(size_t)i].second, nullptr);
        for (int guard = 0; guard < 32 && i >= 0; ++guard)
        {
            const std::string g = trail[(size_t)i].first;
            int entry = -1;
            NodeDef entryDef;
            for (int j = i; j >= 0; --j)
            {
                if (trail[(size_t)j].first != g) break;
                NodeDef d;
                describe(g, trail[(size_t)j].second, &d);
                if (d.Kind == NodeKind::Event) { entry = j; entryDef = d; break; }
            }
            if (entry < 0)
            {
                frames.push_back(frame + "  [" + g + "]");
                break;
            }
            frames.push_back(frame + "  [" + entryDef.Title + ", " + g + "]");
            if (entryDef.Id != "event.function" && entryDef.Id != "event.interface")
            {
                if (entry != i) frames.push_back(entryDef.Title + "  [" + g + "]");
                break;
            }
            // Fonksiyon: onu çağıran en son Call Function düğümü
            const Graph* gr = graphOf(g);
            const Node* en = gr ? gr->FindNode(trail[(size_t)entry].second) : nullptr;
            const std::string fn = en && en->Properties.count("Name") ? en->Properties.at("Name") : std::string();
            int caller = -1;
            for (int j = entry - 1; j >= 0 && caller < 0; --j)
            {
                if (trail[(size_t)j].first != g) continue;
                const Node* cn = gr ? gr->FindNode(trail[(size_t)j].second) : nullptr;
                if (cn && cn->Type == "action.callfunction" && cn->Properties.count("Function") && cn->Properties.at("Function") == fn) caller = j;
            }
            if (caller < 0)
            {
                frames.push_back("(called from another script)");
                break;
            }
            i = caller;
            frame = describe(g, trail[(size_t)i].second, nullptr);
        }
        return frames;
    }
}
