#include "StateGraph.h"

#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Input/Input.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/Visual/VisualGraph.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace Conqueror::Scripting
{
    namespace
    {
        namespace fs = std::filesystem;

        enum class When { AfterSeconds, KeyPressed, TriggerEvent, VariableTrue, VariableFalse };

        When ParseWhen(const std::string& s)
        {
            if (s == "Key Pressed") return When::KeyPressed;
            if (s == "Trigger") return When::TriggerEvent;
            if (s == "Variable Is True") return When::VariableTrue;
            if (s == "Variable Is False") return When::VariableFalse;
            return When::AfterSeconds;
        }

        int KeyCodeOf(const std::string& name)
        {
            for (const auto& [constant, value] : NativeRegistry::Get().GetConstants())
                if (constant == "Key" && value.IsMap())
                    for (auto& [k, v] : value.AsMap()->Entries)
                        if (k == name)
                            return (int)v.ToNumber();
            return -1;
        }

        struct State
        {
            int NodeId = 0;
            std::string Name, Script;
            ScriptRuntime* Runtime = nullptr;
            void* Instance = nullptr;
            bool Started = false;
        };

        struct Transition
        {
            int From = -1;      // durum dizini; -1: Any State
            int To = 0;
            When Mode = When::AfterSeconds;
            std::string Value;
            float Seconds = 0.0f;
            int Key = -1;
        };

        struct Graph
        {
            std::string Path;
            uint32_t Entity = 0;
            std::vector<State> States;
            std::vector<Transition> Transitions;
            int Entry = 0;
            int Current = -1;
            float Time = 0.0f;
            std::set<std::string> Triggers;
            std::unordered_map<std::string, bool> Variables;
        };

        std::vector<Graph*>& Live()
        {
            static auto* v = new std::vector<Graph*>();
            return *v;
        }

        void Forward(State& s, const char* method, const CQS::Value* args = nullptr, int argc = 0)
        {
            if (s.Instance && s.Runtime && s.Runtime->HasMethod(s.Instance, method))
                s.Runtime->Invoke(s.Instance, method, args, argc);
        }

        void Enter(Graph& g, int index)
        {
            g.Current = index;
            g.Time = 0.0f;
            if (index < 0 || index >= (int)g.States.size())
                return;
            State& s = g.States[(size_t)index];
            if (!s.Started)
            {
                s.Started = true;
                Forward(s, "Awake");
                Forward(s, "OnCreate");
                Forward(s, "Start");
            }
            Forward(s, "OnEnterState");
        }

        void Leave(Graph& g)
        {
            if (g.Current >= 0 && g.Current < (int)g.States.size())
                Forward(g.States[(size_t)g.Current], "OnExitState");
        }

        bool Passes(Graph& g, const Transition& t)
        {
            switch (t.Mode)
            {
                case When::AfterSeconds: return g.Time >= t.Seconds;
                case When::KeyPressed: return t.Key >= 0 && Input::IsKeyDown((KeyCode)t.Key);
                case When::TriggerEvent: return g.Triggers.count(t.Value) != 0;
                case When::VariableTrue:
                {
                    auto it = g.Variables.find(t.Value);
                    return it != g.Variables.end() && it->second;
                }
                case When::VariableFalse:
                {
                    auto it = g.Variables.find(t.Value);
                    return it == g.Variables.end() || !it->second;
                }
            }
            return false;
        }

        void Step(Graph& g)
        {
            for (const auto& t : g.Transitions)
            {
                if ((t.From != g.Current && t.From != -1) || t.To == g.Current || !Passes(g, t))
                    continue;
                g.Triggers.erase(t.Value);
                Leave(g);
                Enter(g, t.To);
                break;
            }
            // Bu karede karşılanmayan tetikleyiciler bekletilmez (Unity: olay o anda geçerlidir)
            g.Triggers.clear();
        }

        bool Build(const std::string& path, Graph& g, std::string* error)
        {
            NodeAssets::File file;
            if (!NodeAssets::Load(path, file))
            {
                if (error) *error = "Cannot read State Graph '" + path + "'.";
                return false;
            }
            std::unordered_map<int, int> index;
            for (const auto& n : file.Nodes)
                if (n.Type == "state")
                {
                    State s;
                    s.NodeId = n.Id;
                    s.Name = n.Str("Name", "State");
                    s.Script = n.Str("Script");
                    index[n.Id] = (int)g.States.size();
                    if (n.Bool("Entry"))
                        g.Entry = (int)g.States.size();
                    g.States.push_back(std::move(s));
                }
            for (const auto& n : file.Nodes)
            {
                if (n.Type != "transition")
                    continue;
                std::vector<int> sources;
                bool fromAny = false;
                for (const auto* l : file.In(n.Id))
                {
                    if (const auto* src = file.Find(l->From))
                    {
                        if (src->Type == "anystate")
                            fromAny = true;
                        else if (auto it = index.find(l->From); it != index.end())
                            sources.push_back(it->second);
                    }
                }
                std::vector<int> targets;
                for (const auto* l : file.Out(n.Id))
                    if (auto it = index.find(l->To); it != index.end())
                        targets.push_back(it->second);
                if (fromAny)
                    sources.push_back(-1);
                for (int from : sources)
                    for (int to : targets)
                    {
                        Transition t;
                        t.From = from;
                        t.To = to;
                        t.Mode = ParseWhen(n.Str("When", "After Seconds"));
                        t.Value = n.Str("Value");
                        t.Seconds = n.Float("Value", 1.0f);
                        if (t.Mode == When::KeyPressed)
                            t.Key = KeyCodeOf(t.Value);
                        g.Transitions.push_back(std::move(t));
                    }
            }
            return true;
        }
    }

    class StateGraphRuntime final : public ScriptRuntime
    {
    public:
        const char* GetName() const override { return "State Graph"; }
        const char* GetLanguageId() const override { return "stategraph"; }
        std::vector<std::string> GetExtensions() const override { return { StateGraphs::Extension }; }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            Graph g;
            if (!Build(path, g, error))
                return false;
            for (auto& s : g.States)
            {
                if (s.Script.empty())
                    continue;
                const std::string file = SceneSerializer::ResolveAssetPath(s.Script);
                ScriptRuntime* runtime = ScriptRuntimes::ForPath(file);
                if (!runtime || runtime == this)
                {
                    const std::string message = "State '" + s.Name + "' has no usable script: " + s.Script;
                    CQ_CORE_ERROR("[State Graph] {0}", message);
                    if (error) *error = message;
                    return false;
                }
                if (!runtime->LoadScript(file, error))
                    return false;
            }
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override
        {
            return Visual::SanitizeIdentifier(fs::path(path).stem().string());
        }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)className;
            auto* g = new Graph();
            g->Path = SceneSerializer::ResolveAssetPath(path);
            g->Entity = entity;
            std::string error;
            if (!Build(path, *g, &error))
            {
                delete g;
                return nullptr;
            }
            for (auto& s : g->States)
            {
                if (s.Script.empty())
                    continue;
                const std::string file = SceneSerializer::ResolveAssetPath(s.Script);
                s.Runtime = ScriptRuntimes::ForPath(file);
                if (s.Runtime && s.Runtime != this)
                    s.Instance = s.Runtime->CreateInstance(file, s.Runtime->GetDefaultClass(file), entity);
            }
            Live().push_back(g);
            return g;
        }

        void DestroyInstance(void* instance) override
        {
            auto* g = static_cast<Graph*>(instance);
            auto& live = Live();
            live.erase(std::remove(live.begin(), live.end(), g), live.end());
            for (auto& s : g->States)
                if (s.Instance && s.Runtime)
                    s.Runtime->DestroyInstance(s.Instance);
            delete g;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* g = static_cast<Graph*>(instance);
            if (method == "Update" || method == "Start" || method == "OnDestroy")
                return true;
            if (g->Current >= 0 && g->Current < (int)g->States.size())
            {
                State& s = g->States[(size_t)g->Current];
                return s.Instance && s.Runtime && s.Runtime->HasMethod(s.Instance, method);
            }
            return false;
        }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            auto* g = static_cast<Graph*>(instance);
            if (method == "Awake" || method == "OnCreate" || method == "OnUpdate" || method == "OnLateUpdate")
                return true;    // Awake durum ilk girildiğinde çağrılır; OnUpdate Update içinde iletilir
            ScriptContext::EntityScope scope(g->Entity);
            if (method == "Start")
            {
                if (g->Current < 0 && !g->States.empty())
                    Enter(*g, g->Entry);
                return true;
            }
            if (g->Current < 0 || g->Current >= (int)g->States.size())
                return true;
            if (method == "Update")
            {
                g->Time += argc > 0 ? (float)args[0].ToNumber() : 0.0f;
                Step(*g);
                if (g->Current >= 0 && g->Current < (int)g->States.size())
                {
                    Forward(g->States[(size_t)g->Current], "OnUpdate", args, argc);
                    if (g->Current >= 0 && g->Current < (int)g->States.size())
                        Forward(g->States[(size_t)g->Current], "Update", args, argc);
                }
                return true;
            }
            if (method == "LateUpdate")
            {
                State& s = g->States[(size_t)g->Current];
                Forward(s, "OnLateUpdate", args, argc);
                Forward(s, "LateUpdate", args, argc);
                return true;
            }
            if (method == "OnDestroy")
            {
                Leave(*g);
                for (auto& s : g->States)
                    if (s.Started)
                        Forward(s, "OnDestroy");
                return true;
            }
            State& s = g->States[(size_t)g->Current];
            if (s.Instance && s.Runtime && s.Runtime->HasMethod(s.Instance, method))
                return s.Runtime->Invoke(s.Instance, method, args, argc);
            return true;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* g = static_cast<Graph*>(instance);
            g->Variables[field] = value.Type == ScriptValue::Kind::Bool ? value.Bool
                                  : value.Type == ScriptValue::Kind::Int ? value.Int != 0
                                  : value.Type == ScriptValue::Kind::Float ? value.Float != 0.0 : false;
            return true;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* g = static_cast<Graph*>(instance);
            if (field == "CurrentState")
            {
                const std::string name = g->Current >= 0 && g->Current < (int)g->States.size() ? g->States[(size_t)g->Current].Name : std::string();
                value = ScriptValue::MakeString(name);
                if (display) *display = name;
                return true;
            }
            auto it = g->Variables.find(field);
            value = ScriptValue::MakeBool(it != g->Variables.end() && it->second);
            if (display) *display = value.Bool ? "true" : "false";
            return true;
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            (void)error;
            classes.push_back(GetDefaultClass(path));
            return true;
        }

        // Geçişlerde kullanılan değişkenler Inspector'da Script alanı olarak görünür
        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)className;
            NodeAssets::File file;
            if (!NodeAssets::Load(path, file))
            {
                if (error) *error = "Cannot read State Graph.";
                return false;
            }
            std::set<std::string> names;
            for (const auto& n : file.Nodes)
                if (n.Type == "transition")
                {
                    const When mode = ParseWhen(n.Str("When"));
                    if ((mode == When::VariableTrue || mode == When::VariableFalse) && !n.Str("Value").empty())
                        names.insert(n.Str("Value"));
                }
            for (const auto& name : names)
            {
                ScriptFieldInfo info;
                info.Name = name;
                info.TypeName = "bool";
                info.HasDefault = true;
                info.Default = ScriptValue::MakeBool(false);
                fields.push_back(std::move(info));
            }
            return true;
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            NodeAssets::File file;
            file.Kind = StateGraphs::Kind;
            (void)className;
            return file.ToText();
        }
    };

    std::unique_ptr<ScriptRuntime> CreateStateGraphRuntime()
    {
        return std::make_unique<StateGraphRuntime>();
    }

    namespace StateGraphs
    {
        bool Trigger(uint32_t entity, const std::string& name)
        {
            bool any = false;
            for (auto* g : Live())
                if (g->Entity == entity)
                {
                    g->Triggers.insert(name);
                    any = true;
                }
            return any;
        }

        bool SetVariable(uint32_t entity, const std::string& name, bool value)
        {
            bool any = false;
            for (auto* g : Live())
                if (g->Entity == entity)
                {
                    g->Variables[name] = value;
                    any = true;
                }
            return any;
        }

        std::string CurrentState(uint32_t entity)
        {
            for (auto* g : Live())
                if (g->Entity == entity && g->Current >= 0 && g->Current < (int)g->States.size())
                    return g->States[(size_t)g->Current].Name;
            return "";
        }

        std::vector<int> ActiveNodes(const std::string& path)
        {
            std::vector<int> out;
            std::error_code ec;
            const auto wanted = fs::weakly_canonical(SceneSerializer::ResolveAssetPath(path), ec);
            for (auto* g : Live())
                if (g->Current >= 0 && g->Current < (int)g->States.size() && fs::weakly_canonical(g->Path, ec) == wanted)
                    out.push_back(g->States[(size_t)g->Current].NodeId);
            return out;
        }

        void RegisterNatives(NativeRegistry* vm)
        {
            using CQS::Value;
            // StateGraphTrigger(entity, "Alert") -> bu varlığın çalışan State Graph'larına tetikleyici
            vm->DefineNative("StateGraphTrigger", [](int argc, Value* args) -> Value {
                if (argc < 2 || !args[1].IsString()) return Value::MakeBool(false);
                return Value::MakeBool(Trigger((uint32_t)args[0].ToNumber(), args[1].AsString()->Value));
            }, 2);
            // StateGraphSetVariable(entity, "HasTarget", true): Variable Is True / False geçişleri için
            vm->DefineNative("StateGraphSetVariable", [](int argc, Value* args) -> Value {
                if (argc < 3 || !args[1].IsString()) return Value::MakeBool(false);
                return Value::MakeBool(SetVariable((uint32_t)args[0].ToNumber(), args[1].AsString()->Value, args[2].IsTruthy()));
            }, 3);
            // StateGraphGetState(entity) -> geçerli durumun adı
            vm->DefineNative("StateGraphGetState", [](int argc, Value* args) -> Value {
                if (argc < 1) return Value::MakeString("");
                return Value::MakeString(CurrentState((uint32_t)args[0].ToNumber()));
            }, 1);
        }
    }
}
