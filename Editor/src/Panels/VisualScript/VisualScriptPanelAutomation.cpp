#include "VisualScriptPanel.h"
#include "Core/Automation/Automation.h"
#include "Core/Utils/PlayerPrefs.h"
#include "EditorState.h"

#include <imnodes.h>

#include <algorithm>
#include <cmath>
#include <sstream>

// Görsel betik paneli otomasyonu: "vsx" komutu (Unity / Unreal araçlarının regresyon testleri). Düğüm kurmak için vs komutunun
// node / link / default / prop / var alt komutlarının takma adlı karşılıkları da buradadır (takma adlar panel başına).
namespace Conqueror::Editor
{
    using namespace Visual;

    namespace
    {
        std::string Unescape(const std::string& t)
        {
            std::string out;
            for (size_t i = 0; i < t.size(); ++i)
            {
                if (t[i] == '\\' && i + 1 < t.size())
                {
                    const char c = t[++i];
                    out += c == 'n' ? '\n' : c == 's' ? ' ' : c;
                }
                else
                    out += t[i];
            }
            return out;
        }

        std::vector<std::string> Split(const std::string& s, char sep)
        {
            std::vector<std::string> out;
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, sep))
            {
                const size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
                if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
            }
            return out;
        }
    }

    void VisualScriptPanel::RegisterAutomation()
    {
        Automation::RegisterCommand("vsx", [this](const Automation::CommandArgs& a, std::string& err) {
            if (a.size() < 2 || m_Path.empty()) { err = "vsx: open a visual script first (vs new / vs open)"; return false; }
            const std::string& cmd = a[1];
            auto node = [&](const std::string& alias) {
                auto it = m_AutoAlias.find(alias);
                return it != m_AutoAlias.end() ? it->second : std::atoi(alias.c_str());
            };
            auto ids = [&](size_t from) {
                std::vector<int> out;
                for (size_t i = from; i < a.size(); ++i) out.push_back(node(a[i]));
                return out;
            };
            auto onOff = [&](size_t i) { return i < a.size() && (a[i] == "on" || a[i] == "true" || a[i] == "1"); };
            Graph& graph = G();

            // ── Grafik kurma (takma adlı) ──
            if (cmd == "node" && a.size() > 3)
            {
                const float x = a.size() > 5 ? std::stof(a[4]) : 40.0f + 240.0f * (float)(graph.Nodes.size() % 4);
                const float y = a.size() > 5 ? std::stof(a[5]) : 40.0f + 160.0f * (float)(graph.Nodes.size() / 4);
                const int id = graph.AddNode(a[3], x, y);
                if (!id) { err = "unknown node type " + a[3]; return false; }
                m_AutoAlias[a[2]] = id;
                MarkEdited();
                return true;
            }
            if (cmd == "alias" && a.size() > 3) { m_AutoAlias[a[2]] = std::atoi(a[3].c_str()); return true; }
            if (cmd == "remove" && a.size() > 2) { graph.RemoveNode(node(a[2])); MarkEdited(); return true; }
            // Saved kapsamındaki değişkenin kalıcı değerini sil (testler temiz başlar)
            if (cmd == "saved_clear" && a.size() > 2) { PlayerPrefs::DeleteKey("VS." + a[2]); PlayerPrefs::Save(); return true; }
            if (cmd == "link" && a.size() > 5)
            {
                if (!graph.Connect(node(a[2]), Unescape(a[3]), node(a[4]), Unescape(a[5]), &err)) return false;
                MarkEdited();
                return true;
            }
            if ((cmd == "default" || cmd == "prop") && a.size() > 4)
            {
                Node* n = graph.FindNode(node(a[2]));
                if (!n) { err = "no node " + a[2]; return false; }
                (cmd == "default" ? n->InputDefaults : n->Properties)[Unescape(a[3])] = Unescape(a[4]);
                MarkEdited();
                return true;
            }
            if (cmd == "var" && a.size() > 4)
            {
                Variable v;
                v.Name = a[2];
                v.Type = PinTypeFromName(a[3]);
                v.Default = Unescape(a[4]);
                v.Scope = a.size() > 5 ? VariableScopeFromName(a[5]) : VariableScope::Graph;
                v.Function = a.size() > 6 ? a[6] : "";
                if (Variable* existing = m_Graph.FindVariable(v.Name)) *existing = v;
                else m_Graph.Variables.push_back(v);
                MarkEdited();
                return true;
            }
            if (cmd == "type_asset" && a.size() > 3)
            {
                Variable* v = m_Graph.FindVariable(a[2]);
                if (!v) { err = "no variable " + a[2]; return false; }
                v->TypeAsset = a[3];
                MarkEdited();
                return true;
            }
            if (cmd == "dispatcher" && a.size() > 2) { m_Graph.Dispatchers.push_back(a[2]); MarkEdited(); return true; }
            if (cmd == "implement" && a.size() > 2) { m_Graph.Interfaces.push_back(a[2]); MarkEdited(); return true; }

            // ── Kullanıcı türü varlıkları ──
            if (cmd == "enum" && a.size() > 3)
            {
                EnumAsset e;
                e.Name = std::filesystem::path(a[2]).stem().string();
                e.Entries = Split(a[3], ',');
                if (!UserTypes::SaveEnum(a[2], e)) { err = "cannot write " + a[2]; return false; }
                return true;
            }
            if (cmd == "struct" && a.size() > 3)
            {
                // "Ad:tür=varsayılan; ..."
                StructAsset s;
                s.Name = std::filesystem::path(a[2]).stem().string();
                for (auto& f : Split(Unescape(a[3]), ';'))
                {
                    StructField field;
                    const size_t colon = f.find(':'), eq = f.find('=');
                    field.Name = f.substr(0, std::min(colon, eq));
                    field.Type = colon == std::string::npos ? PinType::Any : PinTypeFromName(f.substr(colon + 1, eq == std::string::npos ? std::string::npos : eq - colon - 1));
                    field.Default = eq == std::string::npos ? "" : f.substr(eq + 1);
                    s.Fields.push_back(field);
                }
                if (!UserTypes::SaveStruct(a[2], s)) { err = "cannot write " + a[2]; return false; }
                return true;
            }
            if (cmd == "interface" && a.size() > 3)
            {
                // "Fonksiyon|giriş1,giriş2|true; ..."
                InterfaceAsset ia;
                ia.Name = std::filesystem::path(a[2]).stem().string();
                for (auto& f : Split(Unescape(a[3]), ';'))
                {
                    const auto parts = Split(f + "| |", '|');
                    InterfaceFunction fn;
                    fn.Name = parts.empty() ? f : parts[0];
                    if (parts.size() > 1) fn.Inputs = Split(parts[1], ',');
                    fn.HasReturn = parts.size() > 2 && parts[2] == "true";
                    ia.Functions.push_back(fn);
                }
                if (!UserTypes::SaveInterface(a[2], ia)) { err = "cannot write " + a[2]; return false; }
                return true;
            }

            // ── Unity araç çubuğu ──
            if (cmd == "toggle" && a.size() > 3)
            {
                const std::string name = a[2];
                if (name != "relations" && name != "values" && name != "dim" && name != "carry" && name != "lock" && name != "inspector" && name != "bottom")
                {
                    err = "unknown toggle " + name;
                    return false;
                }
                Toggle(name) = onOff(3);
                m_ReachableDirty = true;
                return true;
            }
            if (cmd == "align" && a.size() > 4)
            {
                const std::string m = a[2];
                const AlignMode mode = m == "left" ? AlignMode::Left : m == "center" ? AlignMode::Center : m == "right" ? AlignMode::Right
                                     : m == "top" ? AlignMode::Top : m == "middle" ? AlignMode::Middle : AlignMode::Bottom;
                AlignNodes(ids(3), mode);
                return true;
            }
            if (cmd == "distribute" && a.size() > 5) { DistributeNodes(ids(3), a[2] == "h" || a[2] == "horizontal"); return true; }
            if (cmd == "straighten" && a.size() > 2) { StraightenNodes(ids(2)); return true; }
            if (cmd == "move" && a.size() > 4)
            {
                MoveNodes({ { node(a[2]), ImVec2(std::stof(a[3]), std::stof(a[4])) } });
                return true;
            }
            if (cmd == "expect_pos" && a.size() > 4)
            {
                const Node* n = graph.FindNode(node(a[2]));
                if (!n) { err = "no node " + a[2]; return false; }
                const float tol = a.size() > 5 ? std::stof(a[5]) : 1.0f;
                Automation::Report("VSX POS " + a[2] + " = " + std::to_string((int)n->X) + " " + std::to_string((int)n->Y));
                if (std::fabs(n->X - std::stof(a[3])) > tol || std::fabs(n->Y - std::stof(a[4])) > tol)
                {
                    err = "position " + std::to_string(n->X) + ", " + std::to_string(n->Y);
                    return false;
                }
                return true;
            }
            if (cmd == "expect_dim" && a.size() > 3)
            {
                const bool dimmed = IsDimmed(node(a[2]));
                Automation::Report(std::string("VSX DIM ") + a[2] + " = " + (dimmed ? "true" : "false"));
                if (dimmed != (a[3] == "true")) { err = dimmed ? "dimmed" : "not dimmed"; return false; }
                return true;
            }
            if (cmd == "expect_relations" && a.size() > 3)
            {
                const int n = RelationCount(node(a[2]));
                Automation::Report("VSX RELATIONS " + a[2] + " = " + std::to_string(n));
                if (n != std::atoi(a[3].c_str())) { err = "relations " + std::to_string(n); return false; }
                return true;
            }
            if (cmd == "expect_path" && a.size() > 2)
            {
                const std::string p = m_Path.generic_string();
                Automation::Report("VSX PATH " + p);
                if (p.find(a[2]) == std::string::npos) { err = "open graph is " + p; return false; }
                return true;
            }

            // ── Unreal araçları ──
            if (cmd == "disable" && a.size() > 3) { SetNodeDisabled(node(a[2]), onOff(3)); return true; }
            if (cmd == "reroute" && a.size() > 6)
            {
                const int from = node(a[3]), to = node(a[5]);
                for (auto& l : graph.Links)
                    if (l.FromNode == from && l.FromPin == Unescape(a[4]) && l.ToNode == to && l.ToPin == Unescape(a[6]))
                    {
                        const Node* f = graph.FindNode(from);
                        const Node* t = graph.FindNode(to);
                        PushUndo();
                        const int r = graph.InsertReroute(l.Id, (f->X + t->X) * 0.5f, (f->Y + t->Y) * 0.5f);
                        if (!r) { err = "reroute failed"; return false; }
                        m_AutoAlias[a[2]] = r;
                        MarkEdited();
                        return true;
                    }
                err = "no such connection";
                return false;
            }
            if ((cmd == "collapse_function" || cmd == "collapse_macro") && a.size() > 4)
            {
                const int id = cmd == "collapse_function" ? CollapseToFunction(ids(4), a[3]) : CollapseToMacro(ids(4), a[3]);
                if (!id) { err = m_Toast; return false; }
                m_AutoAlias[a[2]] = id;
                return true;
            }
            if (cmd == "promote" && a.size() > 6)
            {
                const int id = PromoteToVariable(node(a[3]), Unescape(a[4]), a[5] == "out", a[6]);
                if (!id) { err = m_Toast; return false; }
                m_AutoAlias[a[2]] = id;
                return true;
            }
            if (cmd == "watch" && a.size() > 3)
            {
                if (!IsWatched(node(a[2]), Unescape(a[3]))) ToggleWatch(node(a[2]), Unescape(a[3]));
                return true;
            }
            if (cmd == "expect_value" && a.size() > 4)
            {
                std::string value;
                const bool has = Debugger::GetValue(DebugKey(), node(a[2]), Unescape(a[3]), value);
                Automation::Report("VSX VALUE " + a[2] + "." + a[3] + " = " + (has ? value : std::string("(none)")));
                if (!has || value.find(Unescape(a[4])) == std::string::npos) { err = "value " + (has ? value : std::string("(none)")); return false; }
                return true;
            }
            if (cmd == "comment" && a.size() > 7)
            {
                const int id = graph.AddNode("comment", std::stof(a[3]), std::stof(a[4]));
                Node* c = graph.FindNode(id);
                c->Width = std::stof(a[5]);
                c->Height = std::stof(a[6]);
                c->Properties["Text"] = Unescape(a[7]);
                m_AutoAlias[a[2]] = id;
                MarkEdited();
                return true;
            }
            if (cmd == "comment_around" && a.size() > 3)
            {
                const int id = AddCommentAround(ids(3), "Comment");
                if (!id) { err = "no nodes"; return false; }
                m_AutoAlias[a[2]] = id;
                return true;
            }
            if (cmd == "move_box" && a.size() > 4)
            {
                // Yorum kutusunu başlığından sürüklemenin karşılığı: içindeki düğümler de taşınır
                Node* c = graph.FindNode(node(a[2]));
                if (!c || !IsBoxNode(*c)) { err = "no comment " + a[2]; return false; }
                const float dx = std::stof(a[3]), dy = std::stof(a[4]);
                PushUndo();
                std::vector<int> inside;
                if (c->Type == "comment")
                    for (auto& other : graph.Nodes)
                    {
                        if (other.Id == c->Id) continue;
                        ImVec2 size(200.0f, 100.0f);
                        if (IsBoxNode(other)) size = ImVec2(other.Width, other.Height);
                        else
                        {
                            const ImVec2 d = NodeDimensions(other.Id);
                            if (d.x > 1.0f) size = ImVec2(d.x / m_Zoom, d.y / m_Zoom);
                        }
                        if (other.X >= c->X && other.Y >= c->Y && other.X + size.x <= c->X + c->Width && other.Y + size.y <= c->Y + c->Height)
                            inside.push_back(other.Id);
                    }
                c->X += dx;
                c->Y += dy;
                for (int id : inside)
                    if (Node* n = graph.FindNode(id)) { n->X += dx; n->Y += dy; }
                MarkEdited();
                Automation::Report("VSX BOX CONTENTS " + std::to_string(inside.size()));
                return true;
            }
            if (cmd == "find" && a.size() > 3)
            {
                const bool all = !(a.size() > 4 && a[4] == "this");
                const auto& results = RunFind(Unescape(a[2]), all);
                Automation::Report("VSX FIND " + a[2] + " = " + std::to_string(results.size()));
                for (auto& r : results) Automation::Report("  " + r.Path.filename().string() + (r.Macro.empty() ? "" : "/" + r.Macro) + " #" + std::to_string(r.Node) + " " + r.Text);
                if ((int)results.size() != std::atoi(a[3].c_str())) { err = "results " + std::to_string(results.size()); return false; }
                return true;
            }
            if (cmd == "bookmark" && a.size() > 2) { AddBookmark(a[2]); return true; }
            if (cmd == "jump" && a.size() > 2)
            {
                if (!JumpToBookmark(a[2])) { err = "no bookmark " + a[2]; return false; }
                return true;
            }
            if (cmd == "expect_bookmarks" && a.size() > 2)
            {
                Automation::Report("VSX BOOKMARKS " + std::to_string(m_Graph.Bookmarks.size()));
                if ((int)m_Graph.Bookmarks.size() != std::atoi(a[2].c_str())) { err = "bookmarks " + std::to_string(m_Graph.Bookmarks.size()); return false; }
                return true;
            }
            if (cmd == "diff" && a.size() > 2)
            {
                if (!RunDiff(a[2], &err)) return false;
                int added = 0, removed = 0, changed = 0, moved = 0, links = 0;
                for (auto& e : m_Diff)
                {
                    Automation::Report(std::string("  ") + DiffKindName(e.Type) + ": " + e.Text);
                    if (e.Type == DiffEntry::Kind::NodeAdded) ++added;
                    else if (e.Type == DiffEntry::Kind::NodeRemoved) ++removed;
                    else if (e.Type == DiffEntry::Kind::NodeChanged) ++changed;
                    else if (e.Type == DiffEntry::Kind::NodeMoved) ++moved;
                    else if (e.Type == DiffEntry::Kind::LinkAdded || e.Type == DiffEntry::Kind::LinkRemoved) ++links;
                }
                Automation::Report("VSX DIFF added=" + std::to_string(added) + " removed=" + std::to_string(removed) + " changed=" + std::to_string(changed) +
                                   " moved=" + std::to_string(moved) + " links=" + std::to_string(links));
                // vsx diff <saved|head> [eklenen kaldırılan değişen]
                if (a.size() > 5 && (added != std::atoi(a[3].c_str()) || removed != std::atoi(a[4].c_str()) || changed != std::atoi(a[5].c_str())))
                {
                    err = "diff counts differ";
                    return false;
                }
                return true;
            }
            if (cmd == "expect_callstack" && a.size() > 2)
            {
                const auto frames = BuildCallStack();
                std::string joined;
                for (auto& f : frames) joined += f + " | ";
                Automation::Report("VSX CALLSTACK " + std::to_string(frames.size()) + ": " + joined);
                const std::string want = Unescape(a[2]);
                if (joined.find(want) == std::string::npos) { err = "call stack does not contain " + want; return false; }
                if (a.size() > 3 && (int)frames.size() != std::atoi(a[3].c_str())) { err = "frames " + std::to_string(frames.size()); return false; }
                return true;
            }
            if (cmd == "expect_type_count" && a.size() > 3)
            {
                int n = 0;
                for (auto& nd : graph.Nodes)
                    if (nd.Type == a[2]) ++n;
                Automation::Report("VSX TYPE " + a[2] + " = " + std::to_string(n));
                if (n != std::atoi(a[3].c_str())) { err = "count " + std::to_string(n); return false; }
                return true;
            }
            if (cmd == "expect_macro" && a.size() > 2)
            {
                if (!m_Graph.FindMacro(a[2])) { err = "no macro " + a[2]; return false; }
                return true;
            }
            if (cmd == "expect_variable" && a.size() > 3)
            {
                const Variable* v = m_Graph.FindVariable(a[2]);
                if (!v) { err = "no variable " + a[2]; return false; }
                Automation::Report(std::string("VSX VARIABLE ") + a[2] + " " + VariableScopeName(v->Scope) + " " + PinTypeName(v->Type) + " = " + v->Default);
                if (std::string(VariableScopeName(v->Scope)) != a[3] && std::string(PinTypeName(v->Type)) != a[3]) { err = "variable differs"; return false; }
                return true;
            }
            if (cmd == "expect_bottom_tab" && a.size() > 2)
            {
                if (!m_ShowBottom || m_BottomTab != std::atoi(a[2].c_str())) { err = "bottom tab " + std::to_string(m_ShowBottom ? m_BottomTab : -1); return false; }
                return true;
            }
            if (cmd == "frame") { FrameAll(); return true; }
            if (cmd == "focus") { m_IsOpen = true; m_RequestFocus = true; return true; }
            // Seçim bir sonraki karede uygulanır (imnodes)
            if (cmd == "select" && a.size() > 2) { m_PendingSelect = ids(2); return true; }
            // Takma adlı kesme noktası / isabet sayısı (vs breakpoint / expect_hits karşılığı)
            if (cmd == "breakpoint" && a.size() > 3) { Debugger::SetBreakpoint(DebugKey(), node(a[2]), onOff(3)); return true; }
            if (cmd == "expect_hits" && a.size() > 3)
            {
                const uint64_t n = Debugger::HitCount(DebugKey(), node(a[2]));
                Automation::Report("VSX HITS " + a[2] + " = " + std::to_string(n));
                const bool ok = a[3][0] == '>' ? n > (uint64_t)std::stoll(a[3].substr(1)) : n == (uint64_t)std::stoll(a[3]);
                if (!ok) { err = "hits " + std::to_string(n); return false; }
                return true;
            }
            err = "unknown vsx command " + cmd;
            return false;
        });
    }
}
