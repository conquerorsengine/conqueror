#include "VisualGraph.h"
#include "Core/Project/Project.h"
#include "Core/Utils/JSON.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

// Görsel betik düzenleme araçları (Unreal Blueprint: Collapse to Function / Macro, Promote to Variable, Reroute, Diff) ve
// grafiklerde kullanılan kullanıcı türü varlıkları (.cqenum, .cqstruct, .cqinterface) ile alt grafik dosyaları
namespace Conqueror::Visual
{
    namespace fs = std::filesystem;

    // ════════════════════════════════════════
    //  Kullanıcı türleri / alt grafikler
    // ════════════════════════════════════════
    namespace
    {
        template <typename T>
        struct CacheEntry
        {
            fs::file_time_type Time{};
            double Checked = -1.0;
            std::shared_ptr<T> Value;
        };

        template <typename T>
        std::unordered_map<std::string, CacheEntry<T>>& CacheOf()
        {
            static auto& c = *new std::unordered_map<std::string, CacheEntry<T>>();
            return c;
        }

        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        // Dosya değişince yeniden okunur; her karede yüzlerce ResolveDef çağrısı olduğundan disk en fazla 0.5 sn'de bir denetlenir
        template <typename T>
        std::shared_ptr<T> Cached(const std::string& path, const std::function<std::shared_ptr<T>(const std::string&)>& parse)
        {
            if (path.empty()) return nullptr;
            const std::string full = UserTypes::ResolvePath(path);
            auto& e = CacheOf<T>()[full];
            const double now = Debugger::Now();
            if (e.Checked >= 0.0 && now - e.Checked < 0.5)
                return e.Value;
            e.Checked = now;
            std::error_code ec;
            const auto time = fs::last_write_time(full, ec);
            if (ec)
            {
                e.Value.reset();
                return nullptr;
            }
            if (e.Value && time == e.Time)
                return e.Value;
            std::string text;
            if (!ReadFile(full, text))
            {
                e.Value.reset();
                return nullptr;
            }
            e.Value = parse(text);
            e.Time = time;
            return e.Value;
        }

        std::string Str(const JSON::Value& m, const char* key, const std::string& def = "")
        {
            auto it = m.MapVal.find(key);
            return it != m.MapVal.end() && it->second.Type == JSON::ValueType::String ? it->second.StringVal : def;
        }

        bool WriteText(const std::string& path, const std::string& text)
        {
            const std::string full = UserTypes::ResolvePath(path);
            std::error_code ec;
            fs::create_directories(fs::path(full).parent_path(), ec);
            std::ofstream f(full, std::ios::binary);
            if (!f) return false;
            f << text;
            return (bool)f;
        }
    }

    std::string UserTypes::ResolvePath(const std::string& path)
    {
        fs::path p = path;
        if (p.is_absolute())
            return p.lexically_normal().string();
        const fs::path project = Project::GetActiveProjectDirectory();
        return project.empty() ? p.string() : (project / p).lexically_normal().string();
    }

    std::string UserTypes::DisplayName(const std::string& path) { return fs::path(path).stem().string(); }

    const EnumAsset* UserTypes::LoadEnum(const std::string& path)
    {
        return Cached<EnumAsset>(path, [&](const std::string& text) -> std::shared_ptr<EnumAsset> {
            JSON::Value root = JSON::Parse(text);
            if (root.Type != JSON::ValueType::Map) return nullptr;
            auto a = std::make_shared<EnumAsset>();
            a->Name = Str(root, "name", DisplayName(path));
            if (auto it = root.MapVal.find("entries"); it != root.MapVal.end())
                for (auto& v : it->second.ListVal)
                    if (!v.StringVal.empty()) a->Entries.push_back(v.StringVal);
            return a;
        }).get();
    }

    const StructAsset* UserTypes::LoadStruct(const std::string& path)
    {
        return Cached<StructAsset>(path, [&](const std::string& text) -> std::shared_ptr<StructAsset> {
            JSON::Value root = JSON::Parse(text);
            if (root.Type != JSON::ValueType::Map) return nullptr;
            auto a = std::make_shared<StructAsset>();
            a->Name = Str(root, "name", DisplayName(path));
            if (auto it = root.MapVal.find("fields"); it != root.MapVal.end())
                for (auto& v : it->second.ListVal)
                {
                    StructField f;
                    f.Name = Str(v, "name");
                    f.Type = PinTypeFromName(Str(v, "type", "number"));
                    f.Default = Str(v, "default");
                    if (!f.Name.empty()) a->Fields.push_back(f);
                }
            return a;
        }).get();
    }

    const InterfaceAsset* UserTypes::LoadInterface(const std::string& path)
    {
        return Cached<InterfaceAsset>(path, [&](const std::string& text) -> std::shared_ptr<InterfaceAsset> {
            JSON::Value root = JSON::Parse(text);
            if (root.Type != JSON::ValueType::Map) return nullptr;
            auto a = std::make_shared<InterfaceAsset>();
            a->Name = Str(root, "name", DisplayName(path));
            if (auto it = root.MapVal.find("functions"); it != root.MapVal.end())
                for (auto& v : it->second.ListVal)
                {
                    InterfaceFunction f;
                    f.Name = Str(v, "name");
                    std::stringstream ss(Str(v, "inputs"));
                    std::string part;
                    while (std::getline(ss, part, ','))
                    {
                        const size_t b = part.find_first_not_of(" \t"), e = part.find_last_not_of(" \t");
                        if (b != std::string::npos) f.Inputs.push_back(part.substr(b, e - b + 1));
                    }
                    auto r = v.MapVal.find("returns");
                    f.HasReturn = r != v.MapVal.end() && r->second.BoolVal;
                    if (!f.Name.empty()) a->Functions.push_back(f);
                }
            return a;
        }).get();
    }

    std::shared_ptr<const Graph> UserTypes::LoadSubgraph(const std::string& path)
    {
        return Cached<Graph>(path, [&](const std::string& text) -> std::shared_ptr<Graph> {
            auto g = std::make_shared<Graph>();
            if (!g->FromJson(text, nullptr)) return nullptr;
            // Makro gövdeleri kendi grafiğini gösterir (paylaşılan nesne taşınmaz: shared_ptr içinde sabit adres)
            for (auto& m : g->Macros)
                if (m.Body) m.Body->MacroScope = g.get();
            return g;
        });
    }

    namespace
    {
        void Invalidate(const std::string& path)
        {
            const std::string full = UserTypes::ResolvePath(path);
            CacheOf<EnumAsset>().erase(full);
            CacheOf<StructAsset>().erase(full);
            CacheOf<InterfaceAsset>().erase(full);
            CacheOf<Graph>().erase(full);
        }
    }

    bool UserTypes::SaveEnum(const std::string& path, const EnumAsset& asset)
    {
        using JSON::Value;
        std::unordered_map<std::string, Value> root;
        root["format"] = Value::MakeString("cqenum");
        root["name"] = Value::MakeString(asset.Name);
        std::vector<Value> entries;
        for (auto& e : asset.Entries) entries.push_back(Value::MakeString(e));
        root["entries"] = Value::MakeList(entries);
        Invalidate(path);
        return WriteText(path, JSON::Stringify(Value::MakeMap(root)));
    }

    bool UserTypes::SaveStruct(const std::string& path, const StructAsset& asset)
    {
        using JSON::Value;
        std::unordered_map<std::string, Value> root;
        root["format"] = Value::MakeString("cqstruct");
        root["name"] = Value::MakeString(asset.Name);
        std::vector<Value> fields;
        for (auto& f : asset.Fields)
        {
            std::unordered_map<std::string, Value> m;
            m["name"] = Value::MakeString(f.Name);
            m["type"] = Value::MakeString(PinTypeName(f.Type));
            m["default"] = Value::MakeString(f.Default);
            fields.push_back(Value::MakeMap(m));
        }
        root["fields"] = Value::MakeList(fields);
        Invalidate(path);
        return WriteText(path, JSON::Stringify(Value::MakeMap(root)));
    }

    bool UserTypes::SaveInterface(const std::string& path, const InterfaceAsset& asset)
    {
        using JSON::Value;
        std::unordered_map<std::string, Value> root;
        root["format"] = Value::MakeString("cqinterface");
        root["name"] = Value::MakeString(asset.Name);
        std::vector<Value> functions;
        for (auto& f : asset.Functions)
        {
            std::unordered_map<std::string, Value> m;
            m["name"] = Value::MakeString(f.Name);
            std::string inputs;
            for (size_t i = 0; i < f.Inputs.size(); ++i) inputs += (i ? ", " : "") + f.Inputs[i];
            m["inputs"] = Value::MakeString(inputs);
            m["returns"] = Value::MakeBool(f.HasReturn);
            functions.push_back(Value::MakeMap(m));
        }
        root["functions"] = Value::MakeList(functions);
        Invalidate(path);
        return WriteText(path, JSON::Stringify(Value::MakeMap(root)));
    }

    void UserTypes::ClearCache()
    {
        CacheOf<EnumAsset>().clear();
        CacheOf<StructAsset>().clear();
        CacheOf<InterfaceAsset>().clear();
        CacheOf<Graph>().clear();
    }

    std::vector<std::string> UserTypes::FindAssets(const std::string& extension)
    {
        // Arama menüsü her karede sorar: tarama 2 sn önbellekte
        struct Scan { double Time = -100.0; std::string Project; std::vector<std::string> Paths; };
        static auto& scans = *new std::unordered_map<std::string, Scan>();
        const fs::path project = Project::GetActiveProjectDirectory();
        Scan& scan = scans[extension];
        const double now = Debugger::Now();
        if (now - scan.Time < 2.0 && scan.Project == project.string())
            return scan.Paths;
        scan.Time = now;
        scan.Project = project.string();
        scan.Paths.clear();
        if (project.empty())
            return scan.Paths;
        std::error_code ec;
        const fs::path root = fs::exists(project / "Assets", ec) ? project / "Assets" : project;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
        {
            if (ec) break;
            if (!it->is_regular_file(ec) || it->path().extension() != extension) continue;
            scan.Paths.push_back(fs::relative(it->path(), project, ec).generic_string());
        }
        std::sort(scan.Paths.begin(), scan.Paths.end());
        return scan.Paths;
    }

    // ════════════════════════════════════════
    //  Diff
    // ════════════════════════════════════════
    const char* DiffKindName(DiffEntry::Kind kind)
    {
        switch (kind)
        {
            case DiffEntry::Kind::NodeAdded: return "Node added";
            case DiffEntry::Kind::NodeRemoved: return "Node removed";
            case DiffEntry::Kind::NodeChanged: return "Node changed";
            case DiffEntry::Kind::NodeMoved: return "Node moved";
            case DiffEntry::Kind::LinkAdded: return "Connection added";
            case DiffEntry::Kind::LinkRemoved: return "Connection removed";
            case DiffEntry::Kind::VariableAdded: return "Variable added";
            case DiffEntry::Kind::VariableRemoved: return "Variable removed";
            case DiffEntry::Kind::VariableChanged: return "Variable changed";
            case DiffEntry::Kind::MacroAdded: return "Macro added";
            case DiffEntry::Kind::MacroRemoved: return "Macro removed";
            case DiffEntry::Kind::MacroChanged: return "Macro changed";
        }
        return "";
    }

    std::vector<DiffEntry> DiffGraphs(const Graph& base, const Graph& current)
    {
        std::vector<DiffEntry> out;
        using K = DiffEntry::Kind;
        for (auto& n : current.Nodes)
        {
            const Node* old = base.FindNode(n.Id);
            const std::string title = current.ResolveDef(n).Title;
            if (!old || old->Type != n.Type)
            {
                if (old) out.push_back({ K::NodeRemoved, n.Id, base.ResolveDef(*old).Title });
                out.push_back({ K::NodeAdded, n.Id, title });
                continue;
            }
            std::string changes;
            auto note = [&](const std::string& what) { changes += (changes.empty() ? "" : ", ") + what; };
            for (auto& [k, v] : n.Properties)
            {
                auto it = old->Properties.find(k);
                if (it == old->Properties.end() ? !v.empty() : it->second != v) note(k);
            }
            for (auto& [k, v] : old->Properties)
                if (!n.Properties.count(k) && !v.empty()) note(k);
            // Pin değerleri: boş değer pinin varsayılanıyla aynıdır (düzenleyici çizerken varsayılanı yazar)
            std::unordered_map<std::string, std::string> pinDefaults;
            for (auto& p : current.ResolveDef(n).Inputs) pinDefaults[p.Name] = p.Default;
            auto effective = [&](const std::map<std::string, std::string>& values, const std::string& key) {
                auto it = values.find(key);
                const std::string v = it != values.end() ? it->second : std::string();
                if (!v.empty()) return v;
                auto d = pinDefaults.find(key);
                return d != pinDefaults.end() ? d->second : std::string();
            };
            std::set<std::string> keys;
            for (auto& [k, v] : n.InputDefaults) keys.insert(k);
            for (auto& [k, v] : old->InputDefaults) keys.insert(k);
            for (auto& k : keys)
                if (k.find('#') == std::string::npos && effective(n.InputDefaults, k) != effective(old->InputDefaults, k)) note(k + " value");
            if (old->Disabled != n.Disabled) note(n.Disabled ? "disabled" : "enabled");
            if (std::fabs(old->Width - n.Width) > 0.5f || std::fabs(old->Height - n.Height) > 0.5f) note("size");
            if (!changes.empty())
                out.push_back({ K::NodeChanged, n.Id, title + ": " + changes });
            else if (std::fabs(old->X - n.X) > 0.5f || std::fabs(old->Y - n.Y) > 0.5f)
                out.push_back({ K::NodeMoved, n.Id, title });
        }
        for (auto& n : base.Nodes)
            if (!current.FindNode(n.Id))
                out.push_back({ K::NodeRemoved, n.Id, base.ResolveDef(n).Title });
        auto key = [](const Link& l) { return std::to_string(l.FromNode) + "\x1f" + l.FromPin + "\x1f" + std::to_string(l.ToNode) + "\x1f" + l.ToPin; };
        std::set<std::string> baseLinks, currentLinks;
        for (auto& l : base.Links) baseLinks.insert(key(l));
        for (auto& l : current.Links) currentLinks.insert(key(l));
        for (auto& l : current.Links)
            if (!baseLinks.count(key(l)))
                out.push_back({ K::LinkAdded, l.ToNode, std::to_string(l.FromNode) + "." + l.FromPin + " -> " + std::to_string(l.ToNode) + "." + l.ToPin });
        for (auto& l : base.Links)
            if (!currentLinks.count(key(l)))
                out.push_back({ K::LinkRemoved, l.ToNode, std::to_string(l.FromNode) + "." + l.FromPin + " -> " + std::to_string(l.ToNode) + "." + l.ToPin });
        for (auto& v : current.Variables)
        {
            const Variable* old = base.FindVariable(v.Name);
            if (!old)
                out.push_back({ K::VariableAdded, 0, v.Name });
            else if (old->Type != v.Type || old->Default != v.Default || old->Scope != v.Scope || old->Public != v.Public || old->Function != v.Function)
                out.push_back({ K::VariableChanged, 0, v.Name });
        }
        for (auto& v : base.Variables)
            if (!current.FindVariable(v.Name))
                out.push_back({ K::VariableRemoved, 0, v.Name });
        for (auto& m : current.Macros)
        {
            const MacroDef* old = nullptr;
            for (auto& bm : base.Macros)
                if (bm.Name == m.Name) old = &bm;
            if (!old)
                out.push_back({ K::MacroAdded, 0, m.Name });
            else if (m.Body && old->Body && m.Body->ToJson() != old->Body->ToJson())
                out.push_back({ K::MacroChanged, 0, m.Name });
        }
        for (auto& bm : base.Macros)
        {
            bool found = false;
            for (auto& m : current.Macros)
                if (m.Name == bm.Name) found = true;
            if (!found)
                out.push_back({ K::MacroRemoved, 0, bm.Name });
        }
        return out;
    }

    // ════════════════════════════════════════
    //  Ulaşılabilirlik (Unity Dim)
    // ════════════════════════════════════════
    std::set<int> Graph::ReachableNodes() const
    {
        std::unordered_map<int, NodeDef> defs;
        for (auto& n : Nodes) defs[n.Id] = ResolveDef(n);
        auto outType = [&](int node, const std::string& pin) {
            auto it = defs.find(node);
            if (it == defs.end()) return PinType::Any;
            for (auto& o : it->second.Outputs)
                if (o.Name == pin) return o.Type;
            return PinType::Any;
        };
        std::set<int> reached;
        std::vector<int> stack;
        for (auto& n : Nodes)
        {
            const auto& d = defs[n.Id];
            if ((d.Kind == NodeKind::Event && !n.Disabled) || n.Type == "macro.inputs")
            {
                reached.insert(n.Id);
                stack.push_back(n.Id);
            }
        }
        // 1) Exec ile ulaşılanlar
        while (!stack.empty())
        {
            const int id = stack.back();
            stack.pop_back();
            for (auto& l : Links)
                if (l.FromNode == id && outType(l.FromNode, l.FromPin) == PinType::Exec && reached.insert(l.ToNode).second)
                    stack.push_back(l.ToNode);
        }
        // 2) Onların veri bağımlılıkları
        stack.assign(reached.begin(), reached.end());
        while (!stack.empty())
        {
            const int id = stack.back();
            stack.pop_back();
            for (auto& l : Links)
                if (l.ToNode == id && outType(l.FromNode, l.FromPin) != PinType::Exec && reached.insert(l.FromNode).second)
                    stack.push_back(l.FromNode);
        }
        return reached;
    }

    // ════════════════════════════════════════
    //  Collapse / Promote / Reroute
    // ════════════════════════════════════════
    namespace
    {
        void AddLinkRaw(Graph& g, int fromNode, const std::string& fromPin, int toNode, const std::string& toPin)
        {
            Link l;
            l.Id = g.NextId++;
            l.FromNode = fromNode;
            l.FromPin = fromPin;
            l.ToNode = toNode;
            l.ToPin = toPin;
            g.Links.push_back(l);
        }

        std::string UniqueName(const std::string& base, const std::set<std::string>& used)
        {
            std::string name = base.empty() ? std::string("Value") : base;
            for (int i = 1; used.count(name); ++i) name = base + std::to_string(i);
            return name;
        }

        // Seçim sınırları ve dış bağlantıların sınıflandırması
        struct SelectionLinks
        {
            std::vector<Link> Internal, ExecIn, ExecOut, DataIn, DataOut;
            float MinX = 1e9f, MinY = 1e9f, MaxX = -1e9f, MaxY = -1e9f;
        };

        bool Classify(const Graph& g, const std::set<int>& nodes, SelectionLinks& s, std::string* error)
        {
            if (nodes.empty())
            {
                if (error) *error = "nothing selected";
                return false;
            }
            for (int id : nodes)
            {
                const Node* n = g.FindNode(id);
                if (!n)
                {
                    if (error) *error = "unknown node " + std::to_string(id);
                    return false;
                }
                const NodeDef d = g.ResolveDef(*n);
                if (d.Kind == NodeKind::Event || n->Type == "macro.inputs" || n->Type == "macro.outputs" || d.Kind == NodeKind::Comment)
                {
                    if (error) *error = "events, tunnels and comments cannot be collapsed";
                    return false;
                }
                s.MinX = std::min(s.MinX, n->X);
                s.MinY = std::min(s.MinY, n->Y);
                s.MaxX = std::max(s.MaxX, n->X);
                s.MaxY = std::max(s.MaxY, n->Y);
            }
            for (auto& l : g.Links)
            {
                const bool from = nodes.count(l.FromNode) != 0, to = nodes.count(l.ToNode) != 0;
                if (!from && !to) continue;
                PinType t = PinType::Any;
                if (const Node* src = g.FindNode(l.FromNode))
                    for (auto& o : g.ResolveDef(*src).Outputs)
                        if (o.Name == l.FromPin) t = o.Type;
                const bool exec = t == PinType::Exec;
                if (from && to) s.Internal.push_back(l);
                else if (to) (exec ? s.ExecIn : s.DataIn).push_back(l);
                else (exec ? s.ExecOut : s.DataOut).push_back(l);
            }
            return true;
        }

        PinType SourceType(const Graph& g, int node, const std::string& pin)
        {
            if (const Node* src = g.FindNode(node))
                for (auto& o : g.ResolveDef(*src).Outputs)
                    if (o.Name == pin) return o.Type;
            return PinType::Any;
        }
    }

    int Graph::CollapseToFunction(const std::set<int>& nodes, const std::string& requestedName, std::string* error)
    {
        SelectionLinks s;
        if (!Classify(*this, nodes, s, error))
            return 0;
        std::set<std::pair<int, std::string>> execTargets, execSources, dataSources;
        for (auto& l : s.ExecIn) execTargets.insert({ l.ToNode, l.ToPin });
        for (auto& l : s.ExecOut) execSources.insert({ l.FromNode, l.FromPin });
        for (auto& l : s.DataOut) dataSources.insert({ l.FromNode, l.FromPin });
        bool hasExecPins = false;
        for (int id : nodes)
            for (auto& p : ResolveDef(*FindNode(id)).Inputs)
                if (p.Type == PinType::Exec) hasExecPins = true;
        if (execTargets.size() > 1)
        {
            if (error) *error = "the selection must have a single execution input";
            return 0;
        }
        if (execTargets.empty() && hasExecPins)
        {
            if (error) *error = "the selection has execution pins but no incoming execution connection";
            return 0;
        }
        if (execSources.size() > 1)
        {
            if (error) *error = "the selection must have at most one execution output";
            return 0;
        }
        if (dataSources.size() > 1)
        {
            if (error) *error = "a function returns one value: the selection may feed only one output pin outside";
            return 0;
        }
        const bool pure = execTargets.empty();

        // Benzersiz fonksiyon adı
        std::set<std::string> usedNames;
        for (auto& n : Nodes)
            if (n.Type == "event.function") usedNames.insert(n.Properties.count("Name") ? n.Properties.at("Name") : "");
        const std::string name = UniqueName(SanitizeIdentifier(requestedName.empty() ? "NewFunction" : requestedName), usedNames);

        // Fonksiyon girişleri: her dış veri kaynağı bir giriş
        std::map<std::pair<int, std::string>, std::string> inputOf;
        std::set<std::string> inputNames;
        std::string inputList;
        for (auto& l : s.DataIn)
        {
            const auto key = std::make_pair(l.FromNode, l.FromPin);
            if (inputOf.count(key)) continue;
            const std::string in = UniqueName(SanitizeIdentifier(l.ToPin), inputNames);
            inputNames.insert(in);
            inputOf[key] = in;
            inputList += (inputList.empty() ? "" : ", ") + in;
        }

        // Seçimi boş bir alana taşı (fonksiyon bölgesi): diğer düğümlerin altına
        float bottom = -1e9f;
        for (auto& n : Nodes)
            if (!nodes.count(n.Id)) bottom = std::max(bottom, n.Y + 160.0f);
        if (bottom < -1e8f) bottom = s.MaxY + 200.0f;
        const float dx = 300.0f, dy = bottom + 120.0f - s.MinY;
        for (auto& n : Nodes)
            if (nodes.count(n.Id)) { n.X += dx; n.Y += dy; }

        const int entry = AddNode("event.function", s.MinX + dx - 280.0f, s.MinY + dy);
        FindNode(entry)->Properties["Name"] = name;
        FindNode(entry)->Properties["Inputs"] = inputList;
        const int call = AddNode("action.callfunction", s.MinX, s.MinY);
        FindNode(call)->Properties["Function"] = name;
        if (pure) FindNode(call)->Properties["Pure"] = "true";

        auto drop = [&](const std::vector<Link>& list) {
            for (auto& l : list) Disconnect(l.Id);
        };
        drop(s.ExecIn);
        drop(s.ExecOut);
        drop(s.DataIn);
        drop(s.DataOut);
        // Fonksiyon gövdesi
        if (!pure)
            AddLinkRaw(*this, entry, "Then", execTargets.begin()->first, execTargets.begin()->second);
        for (auto& l : s.DataIn)
            AddLinkRaw(*this, entry, inputOf[{ l.FromNode, l.FromPin }], l.ToNode, l.ToPin);
        if (!dataSources.empty())
        {
            const auto [srcNode, srcPin] = *dataSources.begin();
            const int ret = AddNode("flow.returnvalue", s.MaxX + dx + 260.0f, s.MinY + dy);
            AddLinkRaw(*this, srcNode, srcPin, ret, "Value");
            if (pure)
                AddLinkRaw(*this, entry, "Then", ret, "In");
            else if (!execSources.empty())
                AddLinkRaw(*this, execSources.begin()->first, execSources.begin()->second, ret, "In");
            else
            {
                // Zincirin bağlı olmayan ilk exec çıkışı
                bool linked = false;
                for (int id : nodes)
                {
                    for (auto& o : ResolveDef(*FindNode(id)).Outputs)
                        if (o.Type == PinType::Exec && FindOutputLinks(id, o.Name).empty())
                        {
                            AddLinkRaw(*this, id, o.Name, ret, "In");
                            linked = true;
                            break;
                        }
                    if (linked) break;
                }
            }
        }
        // Çağıran taraf
        for (auto& l : s.ExecIn)
            AddLinkRaw(*this, l.FromNode, l.FromPin, call, "In");
        for (auto& l : s.ExecOut)
            AddLinkRaw(*this, call, "Then", l.ToNode, l.ToPin);
        for (auto& [src, in] : inputOf)
            AddLinkRaw(*this, src.first, src.second, call, in);
        for (auto& l : s.DataOut)
            AddLinkRaw(*this, call, "Result", l.ToNode, l.ToPin);
        return call;
    }

    int Graph::CollapseToMacro(const std::set<int>& nodes, const std::string& requestedName, std::string* error)
    {
        SelectionLinks s;
        if (!Classify(*this, nodes, s, error))
            return 0;
        std::set<std::string> used;
        for (auto& m : Macros) used.insert(m.Name);
        const std::string name = UniqueName(requestedName.empty() ? std::string("Macro") : requestedName, used);

        // Tünel pinleri
        std::set<std::string> inNames, outNames;
        std::map<std::pair<int, std::string>, std::string> execInPin, dataInPin, execOutPin, dataOutPin;
        std::string inPins, outPins;
        for (auto& l : s.ExecIn)
        {
            const auto key = std::make_pair(l.ToNode, l.ToPin);
            if (execInPin.count(key)) continue;
            const std::string p = UniqueName("In", inNames);
            inNames.insert(p);
            execInPin[key] = p;
            inPins += (inPins.empty() ? "" : ", ") + p + ":exec";
        }
        for (auto& l : s.DataIn)
        {
            const auto key = std::make_pair(l.FromNode, l.FromPin);
            if (dataInPin.count(key)) continue;
            const std::string p = UniqueName(l.ToPin, inNames);
            inNames.insert(p);
            dataInPin[key] = p;
            inPins += (inPins.empty() ? "" : ", ") + p + ":" + PinTypeName(SourceType(*this, l.FromNode, l.FromPin));
        }
        for (auto& l : s.ExecOut)
        {
            const auto key = std::make_pair(l.FromNode, l.FromPin);
            if (execOutPin.count(key)) continue;
            const std::string p = UniqueName("Out", outNames);
            outNames.insert(p);
            execOutPin[key] = p;
            outPins += (outPins.empty() ? "" : ", ") + p + ":exec";
        }
        for (auto& l : s.DataOut)
        {
            const auto key = std::make_pair(l.FromNode, l.FromPin);
            if (dataOutPin.count(key)) continue;
            const std::string p = UniqueName(l.FromPin, outNames);
            outNames.insert(p);
            dataOutPin[key] = p;
            outPins += (outPins.empty() ? "" : ", ") + p + ":" + PinTypeName(SourceType(*this, l.FromNode, l.FromPin));
        }

        MacroDef& macro = AddMacro(name);
        Graph& body = *macro.Body;
        body.MacroScope = this;
        int inputsNode = 0, outputsNode = 0;
        for (auto& n : body.Nodes)
        {
            if (n.Type == "macro.inputs") { inputsNode = n.Id; n.Properties["Pins"] = inPins; n.X = -320.0f; n.Y = 0.0f; }
            if (n.Type == "macro.outputs") { outputsNode = n.Id; n.Properties["Pins"] = outPins; n.X = s.MaxX - s.MinX + 360.0f; n.Y = 0.0f; }
        }
        // Düğümler gövdeye (aynı kimliklerle; tüneller benzersiz kimlik alır)
        int maxId = 0;
        for (int id : nodes)
        {
            Node copy = *FindNode(id);
            copy.X -= s.MinX;
            copy.Y -= s.MinY;
            maxId = std::max(maxId, copy.Id);
            body.Nodes.push_back(copy);
        }
        const int shift = maxId + 1;
        for (auto& n : body.Nodes)
            if (n.Id == inputsNode || n.Id == outputsNode) n.Id += shift;
        inputsNode += shift;
        outputsNode += shift;
        body.NextId = std::max(body.NextId, std::max(inputsNode, outputsNode) + 1);
        for (auto& l : s.Internal) AddLinkRaw(body, l.FromNode, l.FromPin, l.ToNode, l.ToPin);
        for (auto& [key, pin] : execInPin) AddLinkRaw(body, inputsNode, pin, key.first, key.second);
        for (auto& l : s.DataIn) AddLinkRaw(body, inputsNode, dataInPin[{ l.FromNode, l.FromPin }], l.ToNode, l.ToPin);
        for (auto& [key, pin] : execOutPin) AddLinkRaw(body, key.first, key.second, outputsNode, pin);
        for (auto& [key, pin] : dataOutPin) AddLinkRaw(body, key.first, key.second, outputsNode, pin);

        // Olay grafiğinde seçim makro düğümüyle değişir
        for (int id : nodes) RemoveNode(id);
        const int inst = AddNode("macro.instance", s.MinX, s.MinY);
        FindNode(inst)->Properties["Macro"] = name;
        for (auto& l : s.ExecIn) AddLinkRaw(*this, l.FromNode, l.FromPin, inst, execInPin[{ l.ToNode, l.ToPin }]);
        for (auto& [key, pin] : dataInPin) AddLinkRaw(*this, key.first, key.second, inst, pin);
        for (auto& l : s.ExecOut) AddLinkRaw(*this, inst, execOutPin[{ l.FromNode, l.FromPin }], l.ToNode, l.ToPin);
        for (auto& l : s.DataOut) AddLinkRaw(*this, inst, dataOutPin[{ l.FromNode, l.FromPin }], l.ToNode, l.ToPin);
        return inst;
    }

    int Graph::PromoteToVariable(int nodeId, const std::string& pin, bool output, const std::string& requested, std::string* error)
    {
        Node* node = FindNode(nodeId);
        if (!node)
        {
            if (error) *error = "unknown node";
            return 0;
        }
        const NodeDef def = ResolveDef(*node);
        const PinDef* pd = nullptr;
        int index = 0;
        const auto& pins = output ? def.Outputs : def.Inputs;
        for (size_t i = 0; i < pins.size(); ++i)
            if (pins[i].Name == pin) { pd = &pins[i]; index = (int)i; }
        if (!pd || pd->Type == PinType::Exec)
        {
            if (error) *error = pd ? "execution pins cannot be promoted" : "unknown pin " + pin;
            return 0;
        }
        std::set<std::string> used;
        for (auto& v : Variables) used.insert(v.Name);
        Variable v;
        v.Name = UniqueName(SanitizeIdentifier(requested.empty() ? pin : requested), used);
        v.Type = pd->Type == PinType::Any ? PinType::Any : pd->Type;
        if (!output)
        {
            auto it = node->InputDefaults.find(pin);
            v.Default = it != node->InputDefaults.end() ? it->second : pd->Default;
        }
        else
            v.Default = v.Type == PinType::Number ? "0" : v.Type == PinType::Bool ? "false" : "";
        Variables.push_back(v);
        const float x = node->X, y = node->Y;
        if (!output)
        {
            const int get = AddNode("var.get", x - 200.0f, y + 28.0f * (float)index);
            FindNode(get)->Properties["Variable"] = v.Name;
            Connect(get, "Value", nodeId, pin);
            return get;
        }
        const int set = AddNode("var.set", x + 280.0f, y + 40.0f);
        FindNode(set)->Properties["Variable"] = v.Name;
        Connect(nodeId, pin, set, "Value");
        // Kaynak eylemse Set akışa eklenir (Then -> Set -> eski hedef)
        bool hasThen = false;
        for (auto& o : def.Outputs)
            if (o.Name == "Then" && o.Type == PinType::Exec) hasThen = true;
        if (hasThen)
        {
            std::vector<Link> next;
            for (const Link* l : FindOutputLinks(nodeId, "Then")) next.push_back(*l);
            Connect(nodeId, "Then", set, "In");
            for (auto& l : next) Connect(set, "Then", l.ToNode, l.ToPin);
        }
        return set;
    }

    int Graph::InsertReroute(int linkId, float x, float y)
    {
        Link link;
        bool found = false;
        for (auto& l : Links)
            if (l.Id == linkId) { link = l; found = true; }
        if (!found)
            return 0;
        const int r = AddNode("reroute", x, y);
        Disconnect(linkId);
        if (!Connect(link.FromNode, link.FromPin, r, "In") || !Connect(r, "Out", link.ToNode, link.ToPin))
        {
            RemoveNode(r);
            AddLinkRaw(*this, link.FromNode, link.FromPin, link.ToNode, link.ToPin);
            return 0;
        }
        return r;
    }
}
