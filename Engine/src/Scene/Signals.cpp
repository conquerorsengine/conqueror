#include "Signals.h"
#include <fstream>
#include <filesystem>
#include <yaml-cpp/yaml.h>
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Core/Logging/Log.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        struct Connection
        {
            uint32_t Target = 0xFFFFFFFFu;
            std::string Method;
            bool OneShot = false;
            bool Persistent = false;   // Signal Connections bileşeninden
        };

        using Key = std::pair<uint32_t, std::string>;

        struct Bus
        {
            std::map<Key, std::vector<Connection>> Connections;
            uint32_t Frame = 0;
        };

        std::unordered_map<Scene*, Bus>& Buses()
        {
            static std::unordered_map<Scene*, Bus> buses;
            return buses;
        }

        std::string Trim(std::string s)
        {
            const auto b = s.find_first_not_of(" \t\r");
            const auto e = s.find_last_not_of(" \t\r");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        }

        uint32_t FindByName(Scene& scene, const std::string& name)
        {
            auto view = scene.m_Registry.view<TagComponent>();
            for (auto e : view)
                if (view.get<TagComponent>(e).Tag == name)
                    return (uint32_t)e;
            return 0xFFFFFFFFu;
        }
    }

    namespace Signals
    {
        bool Connect(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method, bool oneShot)
        {
            if (signal.empty() || method.empty() || !scene.m_Registry.valid((entt::entity)source) || !scene.m_Registry.valid((entt::entity)target))
                return false;
            auto& list = Buses()[&scene].Connections[{ source, signal }];
            for (auto& c : list)
                if (c.Target == target && c.Method == method)
                {
                    c.OneShot = oneShot;
                    return true;
                }
            list.push_back({ target, method, oneShot, false });
            return true;
        }

        bool Disconnect(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method)
        {
            auto bus = Buses().find(&scene);
            if (bus == Buses().end())
                return false;
            auto it = bus->second.Connections.find({ source, signal });
            if (it == bus->second.Connections.end())
                return false;
            auto& list = it->second;
            const size_t before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Connection& c) { return c.Target == target && c.Method == method; }), list.end());
            return list.size() != before;
        }

        bool IsConnected(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method)
        {
            auto bus = Buses().find(&scene);
            if (bus == Buses().end())
                return false;
            auto it = bus->second.Connections.find({ source, signal });
            if (it == bus->second.Connections.end())
                return false;
            for (const auto& c : it->second)
                if (c.Target == target && c.Method == method)
                    return true;
            return false;
        }

        int ConnectionCount(Scene& scene, uint32_t source, const std::string& signal)
        {
            auto bus = Buses().find(&scene);
            if (bus == Buses().end())
                return 0;
            auto it = bus->second.Connections.find({ source, signal });
            return it == bus->second.Connections.end() ? 0 : (int)it->second.size();
        }

        int Emit(Scene& scene, uint32_t source, const std::string& signal, const CQS::Value* args, int argc)
        {
            auto bus = Buses().find(&scene);
            if (bus == Buses().end())
                return 0;
            auto it = bus->second.Connections.find({ source, signal });
            if (it == bus->second.Connections.end() || it->second.empty())
                return 0;
            // Kopya: alıcılar bağlantıları değiştirebilir
            const std::vector<Connection> receivers = it->second;
            int called = 0;
            for (const auto& c : receivers)
            {
                if (!scene.m_Registry.valid((entt::entity)c.Target))
                    continue;
                scene.InvokeScriptMethod(c.Target, c.Method.c_str(), args, argc);
                ++called;
                if (c.OneShot)
                    Disconnect(scene, source, signal, c.Target, c.Method);
            }
            return called;
        }

        void Update(Scene& scene)
        {
            auto& bus = Buses()[&scene];
            auto view = scene.m_Registry.view<SignalConnectionsComponent>();
            for (auto h : view)
            {
                auto& sc = view.get<SignalConnectionsComponent>(h);
                if (sc.Applied && sc.AppliedFrom == sc.Connections)
                    continue;
                const uint32_t source = (uint32_t)h;
                // Önceki kalıcı bağlantıları kaldır
                for (auto& [key, list] : bus.Connections)
                    if (key.first == source)
                        list.erase(std::remove_if(list.begin(), list.end(), [](const Connection& c) { return c.Persistent; }), list.end());
                // Satır sonu ya da ';' ayırır (tek satırlık denetçi alanı için)
                std::string text = sc.Connections;
                std::replace(text.begin(), text.end(), ';', '\n');
                std::stringstream ss(text);
                std::string line;
                while (std::getline(ss, line))
                {
                    line = Trim(line);
                    if (line.empty() || line[0] == '#')
                        continue;
                    const size_t arrow = line.find("->");
                    const size_t dot = line.rfind('.');
                    if (arrow == std::string::npos || dot == std::string::npos || dot < arrow)
                    {
                        CQ_CORE_WARN("[Signals] Bad connection '{0}' (expected: signal -> Target.Method)", line);
                        continue;
                    }
                    const std::string signal = Trim(line.substr(0, arrow));
                    const std::string targetName = Trim(line.substr(arrow + 2, dot - arrow - 2));
                    std::string method = Trim(line.substr(dot + 1));
                    bool oneShot = false;
                    if (const size_t sp = method.find(' '); sp != std::string::npos)
                    {
                        oneShot = Trim(method.substr(sp)) == "oneshot";
                        method = method.substr(0, sp);
                    }
                    const uint32_t target = targetName == "self" ? source : FindByName(scene, targetName);
                    if (target == 0xFFFFFFFFu)
                    {
                        CQ_CORE_WARN("[Signals] Target '{0}' not found for '{1}'", targetName, line);
                        continue;
                    }
                    auto& list = bus.Connections[{ source, signal }];
                    list.push_back({ target, method, oneShot, true });
                }
                sc.AppliedFrom = sc.Connections;
                sc.Applied = true;
            }
            // Silinen varlıkların bağlantıları (seyrek temizlik)
            if (++bus.Frame % 120 == 0)
                for (auto it = bus.Connections.begin(); it != bus.Connections.end();)
                {
                    if (!scene.m_Registry.valid((entt::entity)it->first.first))
                    {
                        it = bus.Connections.erase(it);
                        continue;
                    }
                    auto& list = it->second;
                    list.erase(std::remove_if(list.begin(), list.end(), [&](const Connection& c) { return !scene.m_Registry.valid((entt::entity)c.Target); }), list.end());
                    ++it;
                }
        }

        void ForgetScene(Scene* scene)
        {
            Buses().erase(scene);
        }
    }

    namespace SignalAssets
    {
        bool Create(const std::string& path)
        {
            std::ofstream f(path);
            if (!f)
                return false;
            f << "Signal: " << std::filesystem::path(path).stem().string() << "\n";
            return true;
        }

        bool Same(const std::string& a, const std::string& b)
        {
            if (a.empty() || b.empty())
                return false;
            std::error_code ec;
            const auto ka = std::filesystem::weakly_canonical(SceneSerializer::ResolveAssetPath(a), ec);
            const auto kb = std::filesystem::weakly_canonical(SceneSerializer::ResolveAssetPath(b), ec);
            return ka == kb;
        }

        int Send(Scene& scene, uint32_t receiver, const std::string& signalPath)
        {
            auto* rc = scene.m_Registry.try_get<SignalReceiverComponent>((entt::entity)receiver);
            if (!rc)
                return 0;
            int count = 0;
            const auto reactions = rc->Reactions;   // tepki nesneleri değiştirebilir
            for (const auto& r : reactions)
            {
                if (r.Method.empty() || !Same(r.Signal, signalPath))
                    continue;
                uint32_t target = receiver;
                if (r.Target)
                {
                    Entity t = scene.FindEntityByUUID(r.Target);
                    if (!t)
                        continue;
                    target = (uint32_t)(entt::entity)t;
                }
                if (r.Argument.empty())
                    scene.InvokeScriptMethod(target, r.Method.c_str(), nullptr, 0);
                else
                {
                    CQS::Value arg = CQS::Value::MakeString(r.Argument);
                    scene.InvokeScriptMethod(target, r.Method.c_str(), &arg, 1);
                }
                ++count;
            }
            return count;
        }
    }

    void RegisterSignalComponents()
    {
        using C = SignalConnectionsComponent;
        ComponentDef<C>("Signal Connections", "SignalConnectionsComponent", "Scripting", ICON_FA_TOWER_BROADCAST)
            .Describe("Godot-style signal connections made in the editor. One per line: \"signal -> TargetObject.Method\" (optionally \" oneshot\"); "
                      "use \"self\" as the target for this object. Scripts emit with SignalEmit(self, \"signal\", ...); the engine emits "
                      "body_entered / body_exited (triggers), collided and pressed / value_changed (UI).")
            .Field("Connections", "Connections", &C::Connections);

        using R = SignalReceiverComponent;
        ComponentDef<R>("Signal Receiver", "SignalReceiverComponent", "Scripting", ICON_FA_TOWER_BROADCAST)
            .Describe("Reacts to Signal assets (.cqsignal) sent to this object by a Timeline Signal track or SignalSend(): "
                      "each reaction calls a script method on this or another object.")
            .CustomData([](YAML::Emitter& out, R& r) {
                out << YAML::Key << "Reactions" << YAML::Value << YAML::BeginSeq;
                for (const auto& x : r.Reactions)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Signal" << YAML::Value << SceneSerializer::ToAssetPath(SceneSerializer::ResolveAssetPath(x.Signal));
                    out << YAML::Key << "Target" << YAML::Value << x.Target;
                    out << YAML::Key << "Method" << YAML::Value << x.Method;
                    if (!x.Argument.empty()) out << YAML::Key << "Argument" << YAML::Value << x.Argument;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }, [](const YAML::Node& n, R& r) {
                r.Reactions.clear();
                if (auto list = n["Reactions"]; list && list.IsSequence())
                    for (const auto& x : list)
                    {
                        SignalReaction s;
                        s.Signal = x["Signal"].as<std::string>("");
                        s.Target = x["Target"].as<uint64_t>(0);
                        s.Method = x["Method"].as<std::string>("");
                        s.Argument = x["Argument"].as<std::string>("");
                        r.Reactions.push_back(s);
                    }
            });
    }
}
