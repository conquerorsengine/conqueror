#include "BehaviorTreeAsset.h"

#include "AISystem/Components/NavComponents.h"
#include "AISystem/EQS/EnvQuery.h"
#include "AISystem/Navigation/NavAgents.h"
#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <unordered_map>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<BehaviorTreeComponent>(Entity, BehaviorTreeComponent&) {}

    namespace
    {
        namespace fs = std::filesystem;

        enum class Status { Success, Failure, Running };

        struct BBValue
        {
            enum Kind { Bool, Number, Text, Vector } Type = Number;
            bool B = false;
            double N = 0.0;
            std::string S;
            glm::vec3 V{ 0.0f };
            bool Set = false;
        };

        struct NodeState
        {
            int Cursor = 0;
            float Timer = 0.0f;
            int Count = 0;
            bool Running = false;
            float CooldownUntil = -1.0f;
            float Duration = 0.0f;
        };

        BBValue ParseValue(const std::string& type, const std::string& value)
        {
            BBValue v;
            v.Set = !value.empty();
            if (type == "Bool") { v.Type = BBValue::Bool; v.B = value == "true" || value == "1"; }
            else if (type == "String") { v.Type = BBValue::Text; v.S = value; }
            else if (type == "Vector")
            {
                v.Type = BBValue::Vector;
                std::sscanf(value.c_str(), "%f %f %f", &v.V.x, &v.V.y, &v.V.z);
            }
            else { v.Type = BBValue::Number; v.N = std::atof(value.c_str()); }
            return v;
        }

        bool IsTruthy(const BBValue& v)
        {
            if (!v.Set) return false;
            switch (v.Type)
            {
                case BBValue::Bool: return v.B;
                case BBValue::Number: return v.N != 0.0;
                case BBValue::Text: return !v.S.empty();
                case BBValue::Vector: return true;
            }
            return false;
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }
    }

    struct BehaviorTreeComponent::Runtime
    {
        std::string Path;
        NodeAssets::File File;
        std::unordered_map<int, NodeState> States;
        std::unordered_map<std::string, BBValue> Board;
        std::unordered_map<int, std::vector<int>> Children;
        std::vector<int> Active;
        int Root = 0;
        float Time = 0.0f;
        float TickTimer = 0.0f;
        std::string Task;

        void Reset(int id)
        {
            States[id] = NodeState{};
            for (int child : Children[id])
                Reset(child);
        }
    };

    namespace
    {
        using RT = BehaviorTreeComponent::Runtime;

        std::vector<RT*>& LiveRuntimes()
        {
            static auto* v = new std::vector<RT*>();
            return *v;
        }

        bool Compare(const BBValue& a, const std::string& op, const std::string& text)
        {
            if (op == "Is Set") return IsTruthy(a);
            if (op == "Is Not Set") return !IsTruthy(a);
            if (a.Type == BBValue::Text)
                return op == "Equals" ? a.S == text : op == "Not Equals" ? a.S != text : false;
            const double left = a.Type == BBValue::Bool ? (a.B ? 1.0 : 0.0) : a.N;
            const double right = text == "true" ? 1.0 : text == "false" ? 0.0 : std::atof(text.c_str());
            if (op == "Equals") return left == right;
            if (op == "Not Equals") return left != right;
            if (op == "Greater") return left > right;
            if (op == "Less") return left < right;
            return false;
        }

        Status Tick(RT& rt, Scene& scene, uint32_t entity, int id, float dt, int depth);

        Status TickChild(RT& rt, Scene& scene, uint32_t entity, int id, float dt, int depth)
        {
            const auto& children = rt.Children[id];
            if (children.empty())
                return Status::Failure;
            return Tick(rt, scene, entity, children.front(), dt, depth + 1);
        }

        glm::vec3 PositionOf(Scene& scene, uint32_t entity)
        {
            return glm::vec3(Entity{ (entt::entity)entity, &scene }.GetComponent<TransformComponent>().WorldTransform[3]);
        }

        Status RunTask(RT& rt, Scene& scene, uint32_t entity, const NodeAssets::Node& node, NodeState& state, float dt)
        {
            const std::string& type = node.Type;
            if (type == "wait")
            {
                if (!state.Running)
                {
                    state.Running = true;
                    state.Timer = 0.0f;
                    const float base = node.Float("Seconds", 1.0f), dev = node.Float("Random Deviation", 0.0f);
                    state.Duration = std::max(0.0f, dev > 0.0f ? std::uniform_real_distribution<float>(base - dev, base + dev)(Rng()) : base);
                }
                state.Timer += dt;
                if (state.Timer >= state.Duration)
                {
                    state.Running = false;
                    return Status::Success;
                }
                return Status::Running;
            }
            if (type == "setkey")
            {
                auto it = rt.Board.find(node.Str("Key"));
                if (it == rt.Board.end())
                    return Status::Failure;
                BBValue value = ParseValue(it->second.Type == BBValue::Bool ? "Bool" : it->second.Type == BBValue::Text ? "String" : it->second.Type == BBValue::Vector ? "Vector" : "Number", node.Str("Value"));
                it->second = value;
                return Status::Success;
            }
            if (type == "log")
            {
                CQ_INFO("[Behavior Tree] {0}", node.Str("Text"));
                return Status::Success;
            }
            if (type == "moveto")
            {
                auto it = rt.Board.find(node.Str("Target Key"));
                if (it == rt.Board.end() || it->second.Type != BBValue::Vector || !it->second.Set)
                    return Status::Failure;
                const glm::vec3 target = it->second.V;
                const float acceptance = node.Float("Acceptance", 0.5f);
                const glm::vec3 here = PositionOf(scene, entity);
                if (glm::distance(glm::vec2(here.x, here.z), glm::vec2(target.x, target.z)) <= acceptance)
                {
                    state.Running = false;
                    return Status::Success;
                }
                Entity e{ (entt::entity)entity, &scene };
                if (e.HasComponent<NavMeshAgentComponent>())
                {
                    if (!state.Running)
                    {
                        if (!NavAgents::SetDestination(scene, entity, target))
                            return Status::Failure;
                        state.Running = true;
                    }
                    return Status::Running;
                }
                state.Running = true;
                auto& tc = e.GetComponent<TransformComponent>();
                glm::vec3 delta = target - here;
                delta.y = 0.0f;
                const float length = glm::length(delta);
                if (length > 1e-4f)
                    tc.Position += delta / length * std::min(length, node.Float("Speed", 3.0f) * dt);
                return Status::Running;
            }
            if (type == "eqs")
            {
                EnvQueryDesc desc;
                if (!EnvQuery::Load(node.Str("Query"), desc))
                    return Status::Failure;
                const auto items = EnvQuery::Run(scene, entity, desc);
                if (items.empty())
                    return Status::Failure;
                BBValue v;
                v.Type = BBValue::Vector;
                v.V = items.front().Position;
                v.Set = true;
                rt.Board[node.Str("Result Key")] = v;
                return Status::Success;
            }
            if (type == "script")
            {
                // Metodun dönüşü: false başarısız, "running" devam, diğerleri başarılı
                Entity e{ (entt::entity)entity, &scene };
                if (!e.HasComponent<ConquerorScriptComponent>())
                    return Status::Failure;
                const std::string method = node.Str("Method");
                CQS::Value arg = CQS::Value::MakeFloat((double)dt);
                for (auto& s : e.GetComponent<ConquerorScriptComponent>().Scripts)
                {
                    auto* runtime = static_cast<Scripting::ScriptRuntime*>(s.Runtime);
                    if (!runtime || !s.Instance || !runtime->HasMethod(s.Instance, method))
                        continue;
                    CQS::Value result = CQS::Value::MakeNull();
                    ScriptContext::EntityScope scope(entity);
                    if (!runtime->InvokeWithResult(s.Instance, method, &arg, 1, &result))
                        return Status::Failure;
                    if (result.IsString() && result.AsString()->Value == "running")
                        return Status::Running;
                    if (result.IsBool() && !result.IsTruthy())
                        return Status::Failure;
                    return Status::Success;
                }
                return Status::Failure;
            }
            return Status::Failure;
        }

        Status Tick(RT& rt, Scene& scene, uint32_t entity, int id, float dt, int depth)
        {
            const NodeAssets::Node* node = rt.File.Find(id);
            if (!node || depth > 128)
                return Status::Failure;
            NodeState& state = rt.States[id];
            rt.Active.push_back(id);
            const std::string& type = node->Type;
            const auto& children = rt.Children[id];
            if (type == "root")
                return TickChild(rt, scene, entity, id, dt, depth);
            if (type == "selector" || type == "sequence")
            {
                const bool selector = type == "selector";
                while (state.Cursor < (int)children.size())
                {
                    const Status s = Tick(rt, scene, entity, children[(size_t)state.Cursor], dt, depth + 1);
                    if (s == Status::Running)
                        return s;
                    if ((selector && s == Status::Success) || (!selector && s == Status::Failure))
                    {
                        state.Cursor = 0;
                        return s;
                    }
                    ++state.Cursor;
                }
                state.Cursor = 0;
                return selector ? Status::Failure : Status::Success;
            }
            if (type == "parallel")
            {
                const bool all = node->Str("Finish When") != "Any Child Finishes";
                int running = 0, failed = 0, finished = 0;
                for (int child : children)
                {
                    const Status s = Tick(rt, scene, entity, child, dt, depth + 1);
                    if (s == Status::Running) ++running;
                    else ++finished;
                    if (s == Status::Failure) ++failed;
                }
                if ((all && running > 0) || (!all && finished == 0))
                    return Status::Running;
                for (int child : children)
                    rt.Reset(child);
                return failed > 0 && all ? Status::Failure : Status::Success;
            }
            if (type == "condition")
            {
                auto it = rt.Board.find(node->Str("Key"));
                const bool pass = it != rt.Board.end() && Compare(it->second, node->Str("Operator", "Is Set"), node->Str("Value"));
                if (!pass)
                {
                    if (!children.empty())
                        rt.Reset(children.front());
                    return Status::Failure;
                }
                return TickChild(rt, scene, entity, id, dt, depth);
            }
            if (type == "invert")
            {
                const Status s = TickChild(rt, scene, entity, id, dt, depth);
                return s == Status::Running ? s : s == Status::Success ? Status::Failure : Status::Success;
            }
            if (type == "forcesuccess")
            {
                const Status s = TickChild(rt, scene, entity, id, dt, depth);
                return s == Status::Running ? s : Status::Success;
            }
            if (type == "cooldown")
            {
                if (rt.Time < state.CooldownUntil)
                    return Status::Failure;
                const Status s = TickChild(rt, scene, entity, id, dt, depth);
                if (s != Status::Running)
                    state.CooldownUntil = rt.Time + node->Float("Seconds", 1.0f);
                return s;
            }
            if (type == "loop")
            {
                const int limit = node->Int("Count", 0);
                const Status s = TickChild(rt, scene, entity, id, dt, depth);
                if (s == Status::Running)
                    return s;
                if (s == Status::Failure)
                {
                    state.Count = 0;
                    return s;
                }
                ++state.Count;
                if (limit > 0 && state.Count >= limit)
                {
                    state.Count = 0;
                    return Status::Success;
                }
                return Status::Running;
            }
            if (type == "timelimit")
            {
                state.Timer += dt;
                if (state.Timer >= node->Float("Seconds", 5.0f))
                {
                    state.Timer = 0.0f;
                    if (!children.empty())
                        rt.Reset(children.front());
                    return Status::Failure;
                }
                const Status s = TickChild(rt, scene, entity, id, dt, depth);
                if (s != Status::Running)
                    state.Timer = 0.0f;
                return s;
            }
            rt.Task = node->Str("Name", type);
            return RunTask(rt, scene, entity, *node, state, dt);
        }

        std::shared_ptr<RT> Build(const std::string& path)
        {
            auto rt = std::make_shared<RT>();
            rt->Path = SceneSerializer::ResolveAssetPath(path);
            if (!NodeAssets::Load(path, rt->File))
                return nullptr;
            for (const auto& n : rt->File.Nodes)
            {
                if (n.Type == "root")
                {
                    rt->Root = n.Id;
                    SimpleAssets::Data board;
                    if (SimpleAssets::Load(n.Str("Blackboard"), board))
                        for (const auto& row : board.Table("Keys"))
                            rt->Board[SimpleAssets::Cell(row, 0)] = ParseValue(SimpleAssets::Cell(row, 1, "Number"), SimpleAssets::Cell(row, 2));
                }
                // Çocuklar soldan sağa (Unreal: X sırası)
                std::vector<int> children;
                for (const auto* link : rt->File.Out(n.Id))
                    children.push_back(link->To);
                std::sort(children.begin(), children.end(), [&](int a, int b) { return rt->File.Find(a)->X < rt->File.Find(b)->X; });
                rt->Children[n.Id] = std::move(children);
            }
            return rt->Root ? rt : nullptr;
        }
    }

    namespace BehaviorTrees
    {
        void Update(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<BehaviorTreeComponent>();
            for (auto e : view)
            {
                auto& c = view.get<BehaviorTreeComponent>(e);
                if (!c.Enabled || c.Tree.empty())
                    continue;
                if (!c.RT)
                {
                    c.RT = Build(c.Tree);
                    if (!c.RT)
                    {
                        CQ_CORE_ERROR("[Behavior Tree] Cannot load {0}", c.Tree);
                        c.Enabled = false;
                        continue;
                    }
                    LiveRuntimes().push_back(c.RT.get());
                }
                RT& rt = *c.RT;
                rt.Time += dt;
                rt.TickTimer += dt;
                if (rt.TickTimer < c.TickInterval)
                    continue;
                const float step = rt.TickTimer;
                rt.TickTimer = 0.0f;
                rt.Active.clear();
                rt.Task.clear();
                Tick(rt, scene, (uint32_t)e, rt.Root, step, 0);
                c.RunningTask = rt.Task;
            }
        }

        std::vector<int> ActiveNodes(const std::string& path)
        {
            std::vector<int> out;
            std::error_code ec;
            const auto wanted = fs::weakly_canonical(SceneSerializer::ResolveAssetPath(path), ec);
            for (auto* rt : LiveRuntimes())
                if (fs::weakly_canonical(rt->Path, ec) == wanted)
                    out.insert(out.end(), rt->Active.begin(), rt->Active.end());
            return out;
        }

        void RegisterComponents()
        {
            using C = BehaviorTreeComponent;
            ComponentDef<C>("Behavior Tree", "BehaviorTreeComponent", "AI", ICON_FA_SITEMAP)
                .Describe("Runs a Behavior Tree (.cqbt) while the game plays. Move To tasks use this object's NavMesh Agent when it has one.")
                .Field("Tree", "Tree", &C::Tree, FieldKind::Asset).Filter(".cqbt")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("TickInterval", "Tick Interval", &C::TickInterval).Tooltip("Seconds between updates; 0 runs every frame.")
                .Field("RunningTask", "Running Task", &C::RunningTask).ReadOnly();
        }

        namespace
        {
            RT* RuntimeOf(const CQS::Value& entity)
            {
                Scene* scene = ScriptContext::GetActiveScene();
                if (!scene)
                    return nullptr;
                const auto e = (entt::entity)(uint32_t)entity.ToNumber();
                if (!scene->m_Registry.valid(e))
                    return nullptr;
                auto* c = scene->m_Registry.try_get<BehaviorTreeComponent>(e);
                if (!c)
                    return nullptr;
                if (!c->RT)
                {
                    c->RT = Build(c->Tree);
                    if (c->RT)
                        LiveRuntimes().push_back(c->RT.get());
                }
                return c->RT.get();
            }
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            // BlackboardSet(entity, "Key", value); BlackboardSetVector(entity, "Key", x, y, z)
            vm->DefineNative("BlackboardSet", [](int argc, Value* args) -> Value {
                RT* rt = argc >= 3 && args[1].IsString() ? RuntimeOf(args[0]) : nullptr;
                if (!rt)
                    return Value::MakeBool(false);
                BBValue v;
                v.Set = true;
                if (args[2].IsBool()) { v.Type = BBValue::Bool; v.B = args[2].IsTruthy(); }
                else if (args[2].IsString()) { v.Type = BBValue::Text; v.S = args[2].AsString()->Value; }
                else { v.Type = BBValue::Number; v.N = args[2].ToNumber(); }
                rt->Board[args[1].AsString()->Value] = v;
                return Value::MakeBool(true);
            }, 3);
            vm->DefineNative("BlackboardSetVector", [](int argc, Value* args) -> Value {
                RT* rt = argc >= 5 && args[1].IsString() ? RuntimeOf(args[0]) : nullptr;
                if (!rt)
                    return Value::MakeBool(false);
                BBValue v;
                v.Type = BBValue::Vector;
                v.Set = true;
                v.V = glm::vec3((float)args[2].ToNumber(), (float)args[3].ToNumber(), (float)args[4].ToNumber());
                rt->Board[args[1].AsString()->Value] = v;
                return Value::MakeBool(true);
            }, 5);
            vm->DefineNative("BlackboardGet", [](int argc, Value* args) -> Value {
                RT* rt = argc >= 2 && args[1].IsString() ? RuntimeOf(args[0]) : nullptr;
                if (!rt)
                    return Value::MakeNull();
                auto it = rt->Board.find(args[1].AsString()->Value);
                if (it == rt->Board.end() || !it->second.Set)
                    return Value::MakeNull();
                const BBValue& v = it->second;
                if (v.Type == BBValue::Bool) return Value::MakeBool(v.B);
                if (v.Type == BBValue::Text) return Value::MakeString(v.S);
                if (v.Type == BBValue::Vector) return Value::MakeString(std::to_string(v.V.x) + " " + std::to_string(v.V.y) + " " + std::to_string(v.V.z));
                return Value::MakeFloat(v.N);
            }, 2);
        }
    }
}
