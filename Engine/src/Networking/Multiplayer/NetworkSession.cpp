#include "NetworkSession.h"
#include "NetTransport.h"
#include "NetworkComponents.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Logging/Log.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"
#include "Networking/Steam/SteamPlatform.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Conqueror::Net
{
    namespace
    {
        constexpr uint32_t kProtocol = 0x43514E31;   // "CQN1"
        constexpr double kFullSyncInterval = 1.0;     // her saniye tüm dönüşümler (yeni gelenler / kayıp paketler)
        constexpr uint64_t kDynamicIdBit = 1ull << 63;

        enum class Msg : uint8_t
        {
            Hello = 1, Welcome, Spawn, Despawn, Snapshot, OwnerState, Rpc, Var, Message, Ownership, AnimParams
        };

        enum SyncFlags : uint8_t { SyncPos = 1, SyncRot = 2, SyncScale = 4 };

        double WallTime()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::vector<std::string>& CommandLine()
        {
            static std::vector<std::string> s_Args;
            return s_Args;
        }
    }

    // ── İkili okuma / yazma (küçük uçlu) ──
    class Writer
    {
    public:
        explicit Writer(Msg type) { U8((uint8_t)type); }
        void U8(uint8_t v) { m_Data.push_back(v); }
        void U16(uint16_t v) { Raw(&v, 2); }
        void U32(uint32_t v) { Raw(&v, 4); }
        void U64(uint64_t v) { Raw(&v, 8); }
        void F32(float v) { Raw(&v, 4); }
        void F64(double v) { Raw(&v, 8); }
        void Str(const std::string& s)
        {
            const uint16_t n = (uint16_t)std::min<size_t>(s.size(), 0xFFFF);
            U16(n);
            Raw(s.data(), n);
        }
        void Vec3(const glm::vec3& v) { F32(v.x); F32(v.y); F32(v.z); }
        void Quat(const glm::quat& q) { F32(q.x); F32(q.y); F32(q.z); F32(q.w); }
        void Value(const NetValue& v, int depth = 0)
        {
            U8((uint8_t)v.Kind);
            switch (v.Kind)
            {
                case NetValue::Type::Bool: U8(v.Number != 0.0 ? 1 : 0); break;
                case NetValue::Type::Number: F64(v.Number); break;
                case NetValue::Type::String: Str(v.Text); break;
                case NetValue::Type::Map:
                    U16((uint16_t)std::min<size_t>(v.Map.size(), 256));
                    for (size_t i = 0; i < v.Map.size() && i < 256; ++i)
                    {
                        Str(v.Map[i].first);
                        Value(v.Map[i].second, depth + 1);
                    }
                    break;
                case NetValue::Type::List:
                    U16((uint16_t)std::min<size_t>(v.List.size(), 1024));
                    for (size_t i = 0; i < v.List.size() && i < 1024; ++i)
                        Value(v.List[i], depth + 1);
                    break;
                default: break;
            }
        }
        const std::vector<uint8_t>& Data() const { return m_Data; }

    private:
        void Raw(const void* p, size_t n)
        {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            m_Data.insert(m_Data.end(), b, b + n);
        }
        std::vector<uint8_t> m_Data;
    };

    class Reader
    {
    public:
        Reader(const uint8_t* data, size_t size) : m_Data(data), m_Size(size) {}
        bool Ok() const { return m_Ok; }
        uint8_t U8() { uint8_t v = 0; Raw(&v, 1); return v; }
        uint16_t U16() { uint16_t v = 0; Raw(&v, 2); return v; }
        uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
        uint64_t U64() { uint64_t v = 0; Raw(&v, 8); return v; }
        float F32() { float v = 0; Raw(&v, 4); return v; }
        double F64() { double v = 0; Raw(&v, 8); return v; }
        std::string Str()
        {
            const uint16_t n = U16();
            if (!m_Ok || m_Pos + n > m_Size) { m_Ok = false; return {}; }
            std::string s(reinterpret_cast<const char*>(m_Data + m_Pos), n);
            m_Pos += n;
            return s;
        }
        glm::vec3 Vec3() { glm::vec3 v; v.x = F32(); v.y = F32(); v.z = F32(); return v; }
        glm::quat Quat() { glm::quat q; q.x = F32(); q.y = F32(); q.z = F32(); q.w = F32(); return q; }
        NetValue Value(int depth = 0)
        {
            NetValue v;
            if (depth > 8) { m_Ok = false; return v; }
            v.Kind = (NetValue::Type)U8();
            switch (v.Kind)
            {
                case NetValue::Type::Null: break;
                case NetValue::Type::Bool: v.Number = U8() ? 1.0 : 0.0; break;
                case NetValue::Type::Number: v.Number = F64(); break;
                case NetValue::Type::String: v.Text = Str(); break;
                case NetValue::Type::Map:
                {
                    const uint16_t n = U16();
                    for (uint16_t i = 0; i < n && m_Ok; ++i)
                    {
                        std::string key = Str();
                        v.Map.emplace_back(std::move(key), Value(depth + 1));
                    }
                    break;
                }
                case NetValue::Type::List:
                {
                    const uint16_t n = U16();
                    for (uint16_t i = 0; i < n && m_Ok; ++i)
                        v.List.push_back(Value(depth + 1));
                    break;
                }
                default: m_Ok = false; break;
            }
            return v;
        }

    private:
        void Raw(void* out, size_t n)
        {
            if (!m_Ok || m_Pos + n > m_Size) { m_Ok = false; std::memset(out, 0, n); return; }
            std::memcpy(out, m_Data + m_Pos, n);
            m_Pos += n;
        }
        const uint8_t* m_Data;
        size_t m_Size;
        size_t m_Pos = 0;
        bool m_Ok = true;
    };

    // ── NetValue ──
    NetValue NetValue::FromScript(const CQS::Value& v, int depth)
    {
        NetValue out;
        switch (v.Type)
        {
            case CQS::ValueType::Bool: out.Kind = Type::Bool; out.Number = v.BoolVal ? 1.0 : 0.0; break;
            case CQS::ValueType::Int:
            case CQS::ValueType::Float: out.Kind = Type::Number; out.Number = v.ToNumber(); break;
            case CQS::ValueType::Object:
                if (v.IsString())
                {
                    out.Kind = Type::String;
                    out.Text = v.AsString()->Value;
                }
                else if (depth < 6 && v.ObjectVal && v.ObjectVal->Type == CQS::ObjectType::Map)
                {
                    out.Kind = Type::Map;
                    for (const auto& [key, value] : v.AsMap()->Entries)
                        out.Map.emplace_back(key, FromScript(value, depth + 1));
                    std::sort(out.Map.begin(), out.Map.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                }
                else if (depth < 6 && v.ObjectVal && v.ObjectVal->Type == CQS::ObjectType::List)
                {
                    out.Kind = Type::List;
                    for (const auto& e : v.AsList()->Elements)
                        out.List.push_back(FromScript(e, depth + 1));
                }
                break;
            default: break;
        }
        return out;
    }

    CQS::Value NetValue::ToScript() const
    {
        switch (Kind)
        {
            case Type::Bool: return CQS::Value::MakeBool(Number != 0.0);
            case Type::Number:
            {
                // Tam sayı görünümlü sayılar betikte tam sayı olarak döner (sayaçlar, kimlikler)
                if (Number == (double)(int64_t)Number && std::abs(Number) < 9.0e15)
                    return CQS::Value::MakeInt((int64_t)Number);
                return CQS::Value::MakeFloat(Number);
            }
            case Type::String: return CQS::Value::MakeString(Text);
            case Type::Map:
            {
                auto* map = new CQS::CQSMapObject();
                for (const auto& [key, value] : Map)
                    map->Entries[key] = value.ToScript();
                return CQS::Value::MakeObject(map);
            }
            case Type::List:
            {
                auto* list = new CQS::CQSListObject();
                for (const auto& e : List)
                    list->Elements.push_back(e.ToScript());
                return CQS::Value::MakeObject(list);
            }
            default: return CQS::Value::MakeNull();
        }
    }

    bool NetValue::operator==(const NetValue& o) const
    {
        if (Kind != o.Kind) return false;
        switch (Kind)
        {
            case Type::Bool:
            case Type::Number: return Number == o.Number;
            case Type::String: return Text == o.Text;
            case Type::Map: return Map == o.Map;
            case Type::List: return List == o.List;
            default: return true;
        }
    }

    // ── Oturum ──
    NetworkSession& NetworkSession::Get()
    {
        static NetworkSession s;
        return s;
    }

    NetworkSession::~NetworkSession()
    {
        if (m_Transport)
            m_Transport->Shutdown();
    }

    void NetworkSession::SetCommandLine(int argc, char** argv)
    {
        auto& args = CommandLine();
        args.clear();
        for (int i = 1; i < argc; ++i)
            args.emplace_back(argv[i] ? argv[i] : "");
    }

    const char* NetworkSession::GetTransportName() const
    {
        return m_Transport ? m_Transport->GetName() : "None";
    }

    bool NetworkSession::IsConnected() const
    {
        if (m_Mode == NetMode::Server || m_Mode == NetMode::Host)
            return true;
        return m_Mode == NetMode::Client && m_Welcomed;
    }

    std::string NetworkSession::GetStatusText() const
    {
        switch (m_Mode)
        {
            case NetMode::Server: return "Server (" + std::to_string(m_Clients.size()) + " clients)";
            case NetMode::Host: return "Host (" + std::to_string(m_Clients.size()) + " players)";
            case NetMode::Client: return m_Welcomed ? "Client #" + std::to_string(m_LocalClientId) : "Connecting...";
            default: return m_LastError.empty() ? "Offline" : "Offline - " + m_LastError;
        }
    }

    void NetworkSession::Fail(const std::string& error)
    {
        m_LastError = error;
        CQ_CORE_ERROR("[Net] {0}", error);
    }

    namespace
    {
        // Sahnedeki NetworkManager ayarları (yoksa varsayılanlar)
        NetworkManagerComponent FindManagerSettings(Scene* scene)
        {
            if (scene)
            {
                auto view = scene->m_Registry.view<NetworkManagerComponent>();
                for (auto e : view)
                    return view.get<NetworkManagerComponent>(e);
            }
            return NetworkManagerComponent{};
        }
    }

    bool NetworkSession::StartServer(Scene* scene, uint16_t port, bool host, int transport)
    {
        Shutdown();
        if (!scene)
        {
            Fail("No running scene (press Play first)");
            return false;
        }
        std::string error;
        m_Transport = transport == 1 ? CreateSteamTransport(error) : CreateENetTransport();
        const NetworkManagerComponent settings = FindManagerSettings(scene);
        if (!m_Transport || !m_Transport->StartServer(port, std::max(1, settings.MaxClients), error))
        {
            m_Transport.reset();
            Fail(error.empty() ? "Could not start the server" : error);
            return false;
        }
        m_Scene = scene;
        m_Mode = host ? NetMode::Host : NetMode::Server;
        m_LastError.clear();
        m_LocalClientId = 0;
        m_NextClientId = 1;
        m_TickRate = std::clamp(settings.TickRate, 1.0f, 128.0f);
        m_InterpolationDelay = std::clamp(settings.InterpolationDelay, 0.0f, 1.0f);
        m_PlayerPrefab = settings.PlayerPrefab;
        m_SpawnPoint = settings.SpawnPoint;
        m_SpawnSpacing = settings.SpawnSpacing;
        m_SpawnCount = 0;
        m_Time = 0.0;
        m_TickAccumulator = 0.0;
        m_LastFullSync = -kFullSyncInterval;
        m_Stats = NetStats{};
        RegisterSceneObjects();
        CQ_CORE_INFO("[Net] {0} started on port {1} ({2})", host ? "Host" : "Server", port, m_Transport->GetName());

        // Sahne nesneleri hazır: OnNetworkSpawn
        for (auto& [id, obj] : m_Objects)
            InvokeOnEntity(obj.Entity, "OnNetworkSpawn", nullptr, 0);
        if (host)
        {
            ClientInfo self;
            self.ClientId = 0;
            self.Connection = 0;
            self.Name = "Host";
            m_Clients.push_back(self);
            SpawnPlayerFor(0);
            CQS::Value arg = CQS::Value::MakeInt(0);
            InvokeOnAll("OnClientConnected", &arg, 1);
        }
        InvokeOnAll("OnServerStarted", nullptr, 0);
        return true;
    }

    bool NetworkSession::StartClient(Scene* scene, const std::string& address, uint16_t port, int transport)
    {
        Shutdown();
        if (!scene)
        {
            Fail("No running scene (press Play first)");
            return false;
        }
        std::string error;
        m_Transport = transport == 1 ? CreateSteamTransport(error) : CreateENetTransport();
        if (!m_Transport || !m_Transport->StartClient(address, port, error))
        {
            m_Transport.reset();
            Fail(error.empty() ? "Could not connect" : error);
            return false;
        }
        const NetworkManagerComponent settings = FindManagerSettings(scene);
        m_Scene = scene;
        m_Mode = NetMode::Client;
        m_LastError.clear();
        m_Welcomed = false;
        m_ServerConnection = 0;
        m_Time = 0.0;
        m_HasTimeOffset = false;
        m_TickRate = std::clamp(settings.TickRate, 1.0f, 128.0f);
        m_InterpolationDelay = std::clamp(settings.InterpolationDelay, 0.0f, 1.0f);
        m_Stats = NetStats{};
        CQ_CORE_INFO("[Net] Connecting to {0}:{1} ({2})", address, port, m_Transport->GetName());
        return true;
    }

    void NetworkSession::Shutdown()
    {
        if (m_Mode == NetMode::Offline && !m_Transport)
            return;
        const bool wasOnline = m_Mode != NetMode::Offline;
        if (m_Transport)
        {
            m_Transport->Flush();
            m_Transport->Shutdown();
            m_Transport.reset();
        }
        // Bu oturumda oluşturulan dinamik nesneler sahneden kaldırılır (sahne nesneleri kalır)
        if (m_Scene && wasOnline)
        {
            std::vector<uint32_t> dynamic;
            for (auto& [id, obj] : m_Objects)
            {
                if (!obj.SceneObject)
                    dynamic.push_back(obj.Entity);
                else if (m_Scene->m_Registry.valid((entt::entity)obj.Entity))
                    if (auto* no = m_Scene->m_Registry.try_get<NetworkObjectComponent>((entt::entity)obj.Entity))
                        no->Spawned = false;
            }
            for (uint32_t e : dynamic)
                if (m_Scene->m_Registry.valid((entt::entity)e))
                    m_Scene->DestroyEntity(Entity{ (entt::entity)e, m_Scene });
            if (m_Mode == NetMode::Client || m_Mode == NetMode::Host)
            {
                CQS::Value arg = CQS::Value::MakeInt((int64_t)m_LocalClientId);
                InvokeOnAll("OnClientDisconnected", &arg, 1);
            }
        }
        m_Objects.clear();
        m_Clients.clear();
        m_PendingConnections.clear();
        m_PendingDespawns.clear();
        m_Mode = NetMode::Offline;
        m_Welcomed = false;
        m_LocalClientId = 0;
        m_Scene = nullptr;
        if (wasOnline)
            CQ_CORE_INFO("[Net] Session closed");
    }

    void NetworkSession::OnSceneStopped(Scene* scene)
    {
        m_PendingCalls.erase(std::remove_if(m_PendingCalls.begin(), m_PendingCalls.end(),
                                            [&](const PendingCall& c) { return !scene || c.Target == scene; }),
                             m_PendingCalls.end());
        if (m_AutoStartScene == scene || !scene)
            m_AutoStartScene = nullptr;
        if (m_Scene == scene || !scene)
        {
            // Sahne yok olurken betik çağrısı yapılmaz
            m_Scene = nullptr;
            if (m_Transport)
            {
                m_Transport->Flush();
                m_Transport->Shutdown();
                m_Transport.reset();
            }
            m_Objects.clear();
            m_Clients.clear();
            m_PendingConnections.clear();
            m_Mode = NetMode::Offline;
            m_Welcomed = false;
        }
    }

    void NetworkSession::OnRuntimeStart(Scene* scene)
    {
        // Betikler ilk karede oluşturulur: oturum ilk karenin sonunda (PostUpdate) başlar ki
        // OnClientConnected / OnNetworkSpawn mevcut betiklere ulaşsın
        m_AutoStartScene = scene;
    }

    namespace
    {
        // Komut satırı (--host / --server / --connect adres[:port] / --port n) sahne ayarından önceliklidir
        void ApplyCommandLine(int& mode, std::string& address, int& port)
        {
            const auto& args = CommandLine();
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (args[i] == "--host") mode = 1;
                else if (args[i] == "--server") mode = 2;
                else if (args[i] == "--connect" && i + 1 < args.size())
                {
                    mode = 3;
                    address = args[++i];
                    if (auto colon = address.rfind(':'); colon != std::string::npos && address.find(':') == colon)
                    {
                        port = std::atoi(address.c_str() + colon + 1);
                        address = address.substr(0, colon);
                    }
                }
                else if (args[i] == "--port" && i + 1 < args.size())
                    port = std::atoi(args[++i].c_str());
            }
        }
    }

    void NetworkSession::PostUpdate(Scene* scene, float dt)
    {
        if (m_ShutdownRequested && (scene == m_Scene || !m_Scene))
        {
            m_ShutdownRequested = false;
            Shutdown();
        }
        if (!m_PendingDespawns.empty() && scene == m_Scene)
        {
            std::vector<uint64_t> ids;
            ids.swap(m_PendingDespawns);
            for (uint64_t id : ids)
                DespawnInternal(id, true);
        }
        if (m_AutoStartScene && m_AutoStartScene == scene)
        {
            m_AutoStartScene = nullptr;
            const NetworkManagerComponent settings = FindManagerSettings(scene);
            int mode = settings.AutoStart;
            std::string address = settings.Address;
            int port = settings.Port;
            ApplyCommandLine(mode, address, port);
            if (mode == 3)
                StartClient(scene, address, (uint16_t)port, settings.Transport);
            else if (mode == 1 || mode == 2)
                StartServer(scene, (uint16_t)port, mode == 1, settings.Transport);
        }
        if (m_Mode != NetMode::Offline && m_Transport && scene == m_Scene)
        {
            CQ_PROFILE_SAMPLE("Network.Send", Other);
            // Silinmiş varlıkları temizle (betik DestroyEntity çağırmış olabilir)
            std::vector<uint64_t> gone;
            for (auto& [id, obj] : m_Objects)
                if (!m_Scene->m_Registry.valid((entt::entity)obj.Entity))
                    gone.push_back(id);
            for (uint64_t id : gone)
                DespawnInternal(id, IsServer());
            m_TickAccumulator += dt;
            const double interval = 1.0 / std::max(1.0f, m_TickRate);
            if (m_TickAccumulator >= interval)
            {
                m_TickAccumulator = std::fmod(m_TickAccumulator, interval);
                SendTick();
                SendAnimParams();
            }
            if (m_Transport)
                m_Transport->Flush();
        }
        FlushCalls(scene);
    }

    void NetworkSession::RegisterSceneObjects()
    {
        m_Objects.clear();
        if (!m_Scene)
            return;
        auto view = m_Scene->m_Registry.view<IDComponent, NetworkObjectComponent>();
        for (auto e : view)
        {
            auto& no = view.get<NetworkObjectComponent>(e);
            NetObject obj;
            obj.Id = (uint64_t)view.get<IDComponent>(e).ID & ~kDynamicIdBit;
            obj.Entity = (uint32_t)e;
            obj.Owner = 0;
            obj.Prefab = no.Prefab;
            obj.SceneObject = true;
            no.NetworkId = obj.Id;
            no.OwnerClientId = 0;
            no.Spawned = true;
            m_Objects[obj.Id] = std::move(obj);
            if (m_Mode == NetMode::Client)
                MakeRemoteKinematic((uint32_t)e);
        }
    }

    // ── Kare güncellemesi ──
    void NetworkSession::Update(Scene* scene, float dt)
    {
        // Steam lobi olayları (oturum olmasa da): betiklere OnSteamLobbyCreated / Entered / List / JoinRequested
        if (SteamPlatform::IsAvailable())
        {
            static bool s_Hooked = false;
            if (!s_Hooked)
            {
                s_Hooked = true;
                auto& ev = SteamPlatform::GetEvents();
                ev.LobbyCreated = [this](uint64_t lobby) {
                    CQS::Value a = CQS::Value::MakeString(std::to_string(lobby));
                    InvokeOnAll("OnSteamLobbyCreated", &a, 1);
                };
                ev.LobbyEntered = [this](uint64_t lobby, uint64_t owner) {
                    CQS::Value a[2] = { CQS::Value::MakeString(std::to_string(lobby)), CQS::Value::MakeString(std::to_string(owner)) };
                    InvokeOnAll("OnSteamLobbyEntered", a, 2);
                };
                ev.LobbyList = [this](const std::vector<uint64_t>& lobbies) {
                    NetValue list;
                    list.Kind = NetValue::Type::List;
                    for (uint64_t l : lobbies)
                    {
                        NetValue v;
                        v.Kind = NetValue::Type::String;
                        v.Text = std::to_string(l);
                        list.List.push_back(v);
                    }
                    QueueCall(0, true, "OnSteamLobbyList", { list });
                };
                ev.JoinRequested = [this](uint64_t lobby) {
                    CQS::Value a = CQS::Value::MakeString(std::to_string(lobby));
                    InvokeOnAll("OnSteamJoinRequested", &a, 1);
                };
            }
            // Steam taşıyıcısı kendi Poll'unda çalıştırır
            if (!m_Transport || std::strcmp(m_Transport->GetName(), "Steam P2P") != 0)
                SteamPlatform::RunCallbacks();
        }

        if (m_Mode == NetMode::Offline || !m_Transport || scene != m_Scene)
        {
            FlushCalls(scene);
            return;
        }
        CQ_PROFILE_SAMPLE("Network.Receive", Other);
        m_Time += dt;

        std::vector<TransportEvent> events;
        m_Transport->Poll(events);
        for (auto& ev : events)
        {
            if (m_Mode == NetMode::Offline)
                break;   // bir olay oturumu kapattı
            switch (ev.Kind)
            {
                case TransportEvent::Type::Connect: HandleConnect(ev.Connection); break;
                case TransportEvent::Type::Disconnect: HandleDisconnect(ev.Connection); break;
                case TransportEvent::Type::Data:
                    m_Stats.BytesReceived += ev.Payload.size();
                    ++m_Stats.MessagesReceived;
                    HandleData(ev.Connection, ev.Payload.data(), ev.Payload.size());
                    break;
            }
        }
        if (m_Mode == NetMode::Client)
            ApplyInterpolation();

        // RTT ve bant genişliği (saniyelik pencere)
        if (m_Mode == NetMode::Client && m_ServerConnection)
            m_Stats.RTTMs = m_Transport ? m_Transport->GetRTT(m_ServerConnection) : 0.0f;
        for (auto& c : m_Clients)
            if (c.Connection && m_Transport)
                c.RTTMs = m_Transport->GetRTT(c.Connection);
        if (m_Time - m_StatsWindowStart >= 1.0)
        {
            const double span = m_Time - m_StatsWindowStart;
            m_Stats.SendKBps = (float)((double)(m_Stats.BytesSent - m_StatsBytesSentStart) / 1024.0 / span);
            m_Stats.ReceiveKBps = (float)((double)(m_Stats.BytesReceived - m_StatsBytesRecvStart) / 1024.0 / span);
            m_StatsWindowStart = m_Time;
            m_StatsBytesSentStart = m_Stats.BytesSent;
            m_StatsBytesRecvStart = m_Stats.BytesReceived;
        }
        FlushCalls(scene);
    }

    // ── Bağlantılar ──
    void NetworkSession::HandleConnect(uint32_t connection)
    {
        if (m_Mode == NetMode::Client)
        {
            m_ServerConnection = connection;
            Writer w(Msg::Hello);
            w.U32(kProtocol);
            w.Str("Player");
            SendToServer(w.Data(), true);
            return;
        }
        // Sunucu: Hello bekle
        m_PendingConnections[connection] = 0;
    }

    void NetworkSession::HandleDisconnect(uint32_t connection)
    {
        if (m_Mode == NetMode::Client)
        {
            if (connection == m_ServerConnection)
            {
                const bool wasWelcomed = m_Welcomed;
                Fail(wasWelcomed ? "Disconnected from server" : "Could not reach the server");
                const std::string error = m_LastError;
                Shutdown();
                m_LastError = error;
            }
            return;
        }
        m_PendingConnections.erase(connection);
        const uint32_t clientId = ClientIdForConnection(connection);
        auto it = std::find_if(m_Clients.begin(), m_Clients.end(), [&](const ClientInfo& c) { return c.Connection == connection && connection != 0; });
        if (it == m_Clients.end())
            return;
        m_Clients.erase(it);
        // Sahibi ayrılan nesneler (DestroyWithOwner) silinir, diğerleri sunucuya geçer
        std::vector<uint64_t> despawn;
        for (auto& [id, obj] : m_Objects)
        {
            if (obj.Owner != clientId)
                continue;
            bool destroy = true;
            if (m_Scene && m_Scene->m_Registry.valid((entt::entity)obj.Entity))
                if (auto* no = m_Scene->m_Registry.try_get<NetworkObjectComponent>((entt::entity)obj.Entity))
                    destroy = no->DestroyWithOwner && !obj.SceneObject;
            if (destroy)
                despawn.push_back(id);
            else
                ChangeOwnership(obj.Entity, 0);
        }
        for (uint64_t id : despawn)
            DespawnInternal(id, true);
        CQ_CORE_INFO("[Net] Client {0} disconnected", clientId);
        CQS::Value arg = CQS::Value::MakeInt((int64_t)clientId);
        InvokeOnAll("OnClientDisconnected", &arg, 1);
    }

    void NetworkSession::HandleData(uint32_t connection, const uint8_t* data, size_t size)
    {
        if (size < 1)
            return;
        Reader r(data + 1, size - 1);
        const Msg type = (Msg)data[0];
        if (IsServer())
        {
            // Hello'dan önce yalnızca Hello kabul edilir
            if (m_PendingConnections.count(connection))
            {
                if (type == Msg::Hello)
                    ServerHandleHello(connection, r);
                return;
            }
            switch (type)
            {
                case Msg::OwnerState: ServerHandleOwnerState(connection, r); break;
                case Msg::AnimParams: HandleAnimParams(connection, r); break;
                case Msg::Rpc: HandleRpc(connection, r); break;
                case Msg::Var: HandleVar(connection, r); break;
                case Msg::Message: HandleMessage(connection, r); break;
                default: break;
            }
            return;
        }
        switch (type)
        {
            case Msg::Welcome: ClientHandleWelcome(r); break;
            case Msg::Spawn: ClientHandleSpawn(r); break;
            case Msg::Despawn: ClientHandleDespawn(r); break;
            case Msg::Snapshot: ClientHandleSnapshot(r); break;
            case Msg::Rpc: HandleRpc(connection, r); break;
            case Msg::Var: HandleVar(connection, r); break;
            case Msg::Message: HandleMessage(connection, r); break;
            case Msg::Ownership: ClientHandleOwnership(r); break;
            case Msg::AnimParams: HandleAnimParams(connection, r); break;
            default: break;
        }
    }

    void NetworkSession::ServerHandleHello(uint32_t connection, Reader& r)
    {
        const uint32_t protocol = r.U32();
        std::string name = r.Str();
        if (!r.Ok() || protocol != kProtocol)
        {
            CQ_CORE_WARN("[Net] Rejected connection {0}: protocol mismatch", connection);
            m_Transport->Disconnect(connection);
            m_PendingConnections.erase(connection);
            return;
        }
        m_PendingConnections.erase(connection);
        ClientInfo info;
        info.ClientId = m_NextClientId++;
        info.Connection = connection;
        info.Name = name.empty() ? "Player" : name;
        m_Clients.push_back(info);

        Writer w(Msg::Welcome);
        w.U32(info.ClientId);
        w.F32(m_TickRate);
        w.F32(m_InterpolationDelay);
        w.F64(m_Time);
        SendToConnection(connection, w.Data(), true);

        // Mevcut nesneler (sahne nesneleri dahil: sahiplik + değişkenler)
        for (auto& [id, obj] : m_Objects)
            SendToConnection(connection, BuildSpawnMessage(obj), true);
        // Tam dönüşüm görüntüsü bir sonraki tick'te gider
        m_LastFullSync = -kFullSyncInterval;

        CQ_CORE_INFO("[Net] Client {0} connected ({1})", info.ClientId, info.Name);
        SpawnPlayerFor(info.ClientId);
        CQS::Value arg = CQS::Value::MakeInt((int64_t)info.ClientId);
        InvokeOnAll("OnClientConnected", &arg, 1);
    }

    void NetworkSession::ClientHandleWelcome(Reader& r)
    {
        const uint32_t clientId = r.U32();
        const float tick = r.F32();
        const float delay = r.F32();
        const double serverTime = r.F64();
        if (!r.Ok())
            return;
        m_LocalClientId = clientId;
        m_TickRate = tick;
        m_InterpolationDelay = delay;
        m_ServerTimeOffset = serverTime - m_Time;
        m_HasTimeOffset = true;
        m_Welcomed = true;
        RegisterSceneObjects();
        CQ_CORE_INFO("[Net] Connected as client {0}", clientId);
        for (auto& [id, obj] : m_Objects)
            InvokeOnEntity(obj.Entity, "OnNetworkSpawn", nullptr, 0);
        CQS::Value arg = CQS::Value::MakeInt((int64_t)clientId);
        InvokeOnAll("OnClientConnected", &arg, 1);
    }

    // ── Oluşturma ──
    std::vector<uint8_t> NetworkSession::BuildSpawnMessage(const NetObject& obj) const
    {
        Writer w(Msg::Spawn);
        w.U64(obj.Id);
        w.U32(obj.Owner);
        w.U8(obj.SceneObject ? 1 : 0);
        w.Str(obj.Prefab);
        glm::vec3 pos(0.0f), scale(1.0f);
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        if (m_Scene && m_Scene->m_Registry.valid((entt::entity)obj.Entity))
        {
            const auto& tc = m_Scene->m_Registry.get<TransformComponent>((entt::entity)obj.Entity);
            pos = tc.Position;
            rot = glm::quat(tc.Rotation);
            scale = tc.Scale;
        }
        w.Vec3(pos);
        w.Quat(rot);
        w.Vec3(scale);
        w.U16((uint16_t)obj.Vars.size());
        for (const auto& [name, value] : obj.Vars)
        {
            w.Str(name);
            w.Value(value);
        }
        return w.Data();
    }

    uint32_t NetworkSession::InstantiateNetworked(const std::string& prefab, const glm::vec3& pos, const glm::quat* rot, const glm::vec3* scale)
    {
        if (!m_Scene || prefab.empty())
            return UINT32_MAX;
        Entity e = PrefabSerializer::Instantiate(*m_Scene, prefab);
        if (!e)
            return UINT32_MAX;
        // Sunucuda prefab'ın kendi dönüşü/ölçeği korunur; istemci sunucunun gönderdiğini uygular
        auto& tc = e.GetComponent<TransformComponent>();
        tc.Position = pos;
        if (rot)
            tc.Rotation = glm::eulerAngles(*rot);
        if (scale)
            tc.Scale = *scale;
        if (!e.HasComponent<NetworkObjectComponent>())
            e.AddComponent<NetworkObjectComponent>();
        e.GetComponent<NetworkObjectComponent>().Prefab = prefab;
        return (uint32_t)(entt::entity)e;
    }

    uint32_t NetworkSession::Spawn(const std::string& prefab, const glm::vec3& position, uint32_t owner)
    {
        if (!IsServer() || !m_Scene)
        {
            m_LastError = "Only the server can spawn network objects";
            return UINT32_MAX;
        }
        const uint32_t entity = InstantiateNetworked(prefab, position, nullptr, nullptr);
        if (entity == UINT32_MAX)
        {
            m_LastError = "Prefab not found: " + prefab;
            return UINT32_MAX;
        }
        Entity e{ (entt::entity)entity, m_Scene };

        NetObject obj;
        obj.Id = kDynamicIdBit | m_NextDynamicId++;
        obj.Entity = entity;
        obj.Owner = owner;
        obj.Prefab = prefab;
        auto& no = e.GetComponent<NetworkObjectComponent>();
        no.NetworkId = obj.Id;
        no.OwnerClientId = owner;
        no.Spawned = true;
        m_Objects[obj.Id] = obj;
        SendToAllClients(BuildSpawnMessage(obj), true);
        InvokeOnEntity(entity, "OnNetworkSpawn", nullptr, 0);
        return entity;
    }

    void NetworkSession::SpawnPlayerFor(uint32_t clientId)
    {
        if (m_PlayerPrefab.empty())
            return;
        const glm::vec3 pos = m_SpawnPoint + glm::vec3(m_SpawnSpacing * (float)m_SpawnCount++, 0.0f, 0.0f);
        const uint32_t entity = Spawn(m_PlayerPrefab, pos, clientId);
        if (entity == UINT32_MAX)
        {
            CQ_CORE_ERROR("[Net] Player prefab could not be spawned: {0}", m_PlayerPrefab);
            return;
        }
        if (ClientInfo* c = FindClient(clientId))
            c->PlayerObject = GetNetworkId(entity);
    }

    void NetworkSession::ClientHandleSpawn(Reader& r)
    {
        const uint64_t id = r.U64();
        const uint32_t owner = r.U32();
        const bool sceneObject = r.U8() != 0;
        const std::string prefab = r.Str();
        const glm::vec3 pos = r.Vec3();
        const glm::quat rot = r.Quat();
        const glm::vec3 scale = r.Vec3();
        const uint16_t varCount = r.U16();
        std::unordered_map<std::string, NetValue> vars;
        for (uint16_t i = 0; i < varCount && r.Ok(); ++i)
        {
            std::string name = r.Str();
            vars[name] = r.Value();
        }
        if (!r.Ok() || !m_Scene)
            return;

        auto it = m_Objects.find(id);
        if (sceneObject)
        {
            // Sahne nesnesi: yalnızca sahiplik ve değişkenler
            if (it == m_Objects.end())
                return;
            it->second.Owner = owner;
            it->second.Vars = std::move(vars);
            if (auto* no = m_Scene->m_Registry.try_get<NetworkObjectComponent>((entt::entity)it->second.Entity))
                no->OwnerClientId = owner;
            return;
        }
        if (it != m_Objects.end())
            return; // zaten var
        const uint32_t entity = InstantiateNetworked(prefab, pos, &rot, &scale);
        if (entity == UINT32_MAX)
        {
            CQ_CORE_ERROR("[Net] Cannot spawn '{0}' on this client (prefab missing?)", prefab);
            return;
        }
        NetObject obj;
        obj.Id = id;
        obj.Entity = entity;
        obj.Owner = owner;
        obj.Prefab = prefab;
        obj.Vars = std::move(vars);
        auto& no = m_Scene->m_Registry.get<NetworkObjectComponent>((entt::entity)entity);
        no.NetworkId = id;
        no.OwnerClientId = owner;
        no.Spawned = true;
        m_Objects[id] = std::move(obj);
        MakeRemoteKinematic(entity);
        // İlk ara değerleme noktası
        if (auto* nt = m_Scene->m_Registry.try_get<NetworkTransformComponent>((entt::entity)entity))
        {
            nt->Buffer.clear();
            NetworkSnapshot s;
            s.Time = m_Time + m_ServerTimeOffset;
            s.Position = pos;
            s.Rotation = rot;
            s.Scale = scale;
            nt->Buffer.push_back(s);
        }
        InvokeOnEntity(entity, "OnNetworkSpawn", nullptr, 0);
    }

    void NetworkSession::MakeRemoteKinematic(uint32_t entity)
    {
        // Sunucu otoritesindeki nesnelerin istemci kopyası fizikle değil ağla hareket eder (Unity NetworkRigidbody)
        if (!m_Scene || !m_Scene->m_Registry.valid((entt::entity)entity))
            return;
        auto& reg = m_Scene->m_Registry;
        const auto ent = (entt::entity)entity;
        bool ownerDriven = false;
        if (auto* nt = reg.try_get<NetworkTransformComponent>(ent))
            ownerDriven = nt->Authority == 1;
        if (auto* no = reg.try_get<NetworkObjectComponent>(ent))
            if (ownerDriven && no->OwnerClientId == m_LocalClientId)
                return;
        Entity e{ ent, m_Scene };
        if (auto* nr = reg.try_get<NetworkRigidbodyComponent>(ent); nr && !nr->AutoUpdateKinematicState)
            return;
        if (auto* rb = reg.try_get<RigidbodyComponent>(ent); rb && rb->Type == RigidbodyComponent::BodyType::Dynamic)
        {
            rb->Type = RigidbodyComponent::BodyType::Kinematic;
            if (auto* pw = m_Scene->GetPhysicsWorld3D())
                pw->MarkDirty(e);
        }
        if (auto* rb2 = reg.try_get<RigidBody2DComponent>(ent); rb2 && rb2->Type == RigidBody2DComponent::BodyType::Dynamic)
        {
            rb2->Type = RigidBody2DComponent::BodyType::Kinematic;
            if (auto* pw = m_Scene->GetPhysicsWorld2D())
                pw->MarkDirty(e);
        }
    }

    void NetworkSession::ClientHandleDespawn(Reader& r)
    {
        const uint64_t id = r.U64();
        if (r.Ok())
            DespawnInternal(id, false);
    }

    void NetworkSession::DespawnInternal(uint64_t id, bool broadcast)
    {
        auto it = m_Objects.find(id);
        if (it == m_Objects.end())
            return;
        const NetObject obj = it->second;
        m_Objects.erase(it);
        if (broadcast && IsServer())
        {
            Writer w(Msg::Despawn);
            w.U64(id);
            SendToAllClients(w.Data(), true);
        }
        if (m_Scene && m_Scene->m_Registry.valid((entt::entity)obj.Entity))
        {
            // Betik yürütmesi dışında çağrılır (Update / PostUpdate): silmeden önce doğrudan bildir
            InvokeNow(obj.Entity, "OnNetworkDespawn", nullptr, 0);
            if (m_Scene && m_Scene->m_Registry.valid((entt::entity)obj.Entity))
                m_Scene->DestroyEntity(Entity{ (entt::entity)obj.Entity, m_Scene });
        }
    }

    bool NetworkSession::Despawn(uint32_t entity)
    {
        if (!IsServer())
        {
            m_LastError = "Only the server can despawn network objects";
            return false;
        }
        const uint64_t id = GetNetworkId(entity);
        if (!id)
            return false;
        // Karenin sonunda (betik yürütmesi dışında) silinir
        if (std::find(m_PendingDespawns.begin(), m_PendingDespawns.end(), id) == m_PendingDespawns.end())
            m_PendingDespawns.push_back(id);
        return true;
    }

    bool NetworkSession::ChangeOwnership(uint32_t entity, uint32_t owner)
    {
        if (!IsServer())
            return false;
        const uint64_t id = GetNetworkId(entity);
        auto it = m_Objects.find(id);
        if (it == m_Objects.end())
            return false;
        it->second.Owner = owner;
        if (m_Scene)
            if (auto* no = m_Scene->m_Registry.try_get<NetworkObjectComponent>((entt::entity)entity))
                no->OwnerClientId = owner;
        Writer w(Msg::Ownership);
        w.U64(id);
        w.U32(owner);
        SendToAllClients(w.Data(), true);
        CQS::Value arg = CQS::Value::MakeInt((int64_t)owner);
        InvokeOnEntity(entity, "OnOwnershipChanged", &arg, 1);
        return true;
    }

    void NetworkSession::ClientHandleOwnership(Reader& r)
    {
        const uint64_t id = r.U64();
        const uint32_t owner = r.U32();
        auto it = m_Objects.find(id);
        if (!r.Ok() || it == m_Objects.end())
            return;
        it->second.Owner = owner;
        if (m_Scene)
            if (auto* no = m_Scene->m_Registry.try_get<NetworkObjectComponent>((entt::entity)it->second.Entity))
                no->OwnerClientId = owner;
        if (auto* nt = m_Scene ? m_Scene->m_Registry.try_get<NetworkTransformComponent>((entt::entity)it->second.Entity) : nullptr)
            nt->Buffer.clear();
        CQS::Value arg = CQS::Value::MakeInt((int64_t)owner);
        InvokeOnEntity(it->second.Entity, "OnOwnershipChanged", &arg, 1);
    }

    uint64_t NetworkSession::GetNetworkId(uint32_t entity) const
    {
        for (const auto& [id, obj] : m_Objects)
            if (obj.Entity == entity)
                return id;
        return 0;
    }

    uint32_t NetworkSession::FindEntity(uint64_t networkId) const
    {
        auto it = m_Objects.find(networkId);
        return it == m_Objects.end() ? UINT32_MAX : it->second.Entity;
    }

    uint32_t NetworkSession::GetOwner(uint32_t entity) const
    {
        for (const auto& [id, obj] : m_Objects)
            if (obj.Entity == entity)
                return obj.Owner;
        return 0;
    }

    bool NetworkSession::IsOwner(uint32_t entity) const
    {
        if (m_Mode == NetMode::Offline)
            return true;   // çevrimdışı: her şey yerel
        for (const auto& [id, obj] : m_Objects)
            if (obj.Entity == entity)
                return obj.Owner == m_LocalClientId && (m_Mode != NetMode::Server || obj.Owner == 0);
        // Ağ nesnesi değil: sunucu/host sahibidir
        return IsServer();
    }

    // ── Dönüşüm eşleme ──
    void NetworkSession::SendTick()
    {
        if (!m_Scene)
            return;
        ++m_Tick;
        auto& reg = m_Scene->m_Registry;
        const bool full = m_Time - m_LastFullSync >= kFullSyncInterval;
        if (full)
            m_LastFullSync = m_Time;

        auto writeEntry = [&](Writer& w, uint64_t id, NetworkTransformComponent& nt, const TransformComponent& tc, bool force) -> bool {
            uint8_t flags = 0;
            const glm::quat rot(tc.Rotation);
            if (nt.SyncPosition && (force || glm::length(tc.Position - nt.LastSentPosition) > nt.PositionThreshold))
                flags |= SyncPos;
            if (nt.SyncRotation)
            {
                const float dot = std::abs(glm::dot(rot, nt.LastSentRotation));
                const float angle = glm::degrees(2.0f * std::acos(std::min(1.0f, dot)));
                if (force || angle > nt.RotationThreshold)
                    flags |= SyncRot;
            }
            if (nt.SyncScale && (force || glm::length(tc.Scale - nt.LastSentScale) > 1e-4f))
                flags |= SyncScale;
            if (!flags)
                return false;
            w.U64(id);
            w.U8(flags);
            if (flags & SyncPos) { w.Vec3(tc.Position); nt.LastSentPosition = tc.Position; }
            if (flags & SyncRot) { w.Quat(rot); nt.LastSentRotation = rot; }
            if (flags & SyncScale) { w.Vec3(tc.Scale); nt.LastSentScale = tc.Scale; }
            return true;
        };

        if (IsServer())
        {
            // Sunucu: tüm NetworkTransform'lar (sahip istemcinin gönderdiği durumlar dahil — diğerleri görsün)
            Writer w(Msg::Snapshot);
            w.U32(m_Tick);
            w.F64(m_Time);
            const size_t countPos = w.Data().size();
            w.U16(0);
            uint16_t count = 0;
            std::vector<uint8_t> data;
            for (auto& [id, obj] : m_Objects)
            {
                const auto ent = (entt::entity)obj.Entity;
                if (!reg.valid(ent))
                    continue;
                auto* nt = reg.try_get<NetworkTransformComponent>(ent);
                if (!nt)
                    continue;
                if (writeEntry(w, id, *nt, reg.get<TransformComponent>(ent), full))
                    ++count;
                if (count >= 2000)
                    break;
            }
            if (count > 0 && !m_Clients.empty())
            {
                data = w.Data();
                std::memcpy(data.data() + countPos, &count, 2);
                SendToAllClients(data, false);
            }
            return;
        }

        // İstemci: yalnızca sahip olduğu ve kendisinin sürdüğü nesneler
        if (!m_Welcomed)
            return;
        for (auto& [id, obj] : m_Objects)
        {
            if (obj.Owner != m_LocalClientId)
                continue;
            const auto ent = (entt::entity)obj.Entity;
            if (!reg.valid(ent))
                continue;
            auto* nt = reg.try_get<NetworkTransformComponent>(ent);
            if (!nt || nt->Authority != 1)
                continue;
            Writer w(Msg::OwnerState);
            if (writeEntry(w, id, *nt, reg.get<TransformComponent>(ent), full))
                SendToServer(w.Data(), false);
        }
    }

    // ── Network Animator: yetkili taraf değişen parametreleri gönderir (sunucu yetkisi ya da sahip istemci)
    void NetworkSession::SendAnimParams()
    {
        if (!m_Scene)
            return;
        auto& reg = m_Scene->m_Registry;
        for (auto& [id, obj] : m_Objects)
        {
            const auto ent = (entt::entity)obj.Entity;
            if (!reg.valid(ent))
                continue;
            auto* na = reg.try_get<NetworkAnimatorComponent>(ent);
            auto* ac = reg.try_get<AnimationComponent>(ent);
            if (!na || !ac)
                continue;
            const auto* nt = reg.try_get<NetworkTransformComponent>(ent);
            const bool ownerAuthority = nt && nt->Authority == 1;
            const bool authority = IsServer() ? !(ownerAuthority && obj.Owner != 0) : (ownerAuthority && obj.Owner == m_LocalClientId);
            if (!authority)
                continue;
            Writer w(Msg::AnimParams);
            w.U64(id);
            const size_t countPos = w.Data().size();
            w.U16(0);
            uint16_t count = 0;
            for (auto& p : ac->Parameters)
            {
                float value = 0.0f;
                bool want = false;
                switch (p.Type)
                {
                    case AnimParameterType::Float: value = p.FloatValue; want = na->SyncFloats; break;
                    case AnimParameterType::Int: value = (float)p.IntValue; want = na->SyncInts; break;
                    case AnimParameterType::Bool: value = p.BoolValue ? 1.0f : 0.0f; want = na->SyncBools; break;
                    case AnimParameterType::Trigger: value = p.BoolValue ? 1.0f : 0.0f; want = na->SyncTriggers; break;
                }
                if (!want)
                    continue;
                auto sent = na->Sent.find(p.Name);
                const float threshold = p.Type == AnimParameterType::Float ? std::max(na->FloatThreshold, 0.0f) : 0.0f;
                const bool changed = sent == na->Sent.end() || std::fabs(sent->second - value) > threshold;
                // Tetikleyiciler yalnızca kurulduğunda gönderilir
                if (!changed || (p.Type == AnimParameterType::Trigger && value == 0.0f))
                {
                    if (p.Type == AnimParameterType::Trigger) na->Sent[p.Name] = value;
                    continue;
                }
                na->Sent[p.Name] = value;
                w.Str(p.Name);
                w.U8((uint8_t)p.Type);
                w.F32(value);
                ++count;
            }
            if (count == 0)
                continue;
            std::vector<uint8_t> data = w.Data();
            std::memcpy(data.data() + countPos, &count, 2);
            if (IsServer())
                SendToAllClients(data, true);
            else
                SendToServer(data, true);
        }
    }

    void NetworkSession::HandleAnimParams(uint32_t connection, Reader& r)
    {
        const uint64_t id = r.U64();
        const uint16_t count = r.U16();
        auto it = m_Objects.find(id);
        if (!r.Ok() || it == m_Objects.end() || !m_Scene)
            return;
        const auto ent = (entt::entity)it->second.Entity;
        auto& reg = m_Scene->m_Registry;
        if (!reg.valid(ent))
            return;
        if (IsServer() && it->second.Owner != ClientIdForConnection(connection))
            return;   // yalnızca sahibi gönderebilir
        auto* ac = reg.try_get<AnimationComponent>(ent);
        Writer relay(Msg::AnimParams);
        relay.U64(id);
        relay.U16(count);
        for (uint16_t i = 0; i < count && r.Ok(); ++i)
        {
            const std::string name = r.Str();
            const uint8_t type = r.U8();
            const float value = r.F32();
            relay.Str(name);
            relay.U8(type);
            relay.F32(value);
            if (!ac)
                continue;
            for (auto& p : ac->Parameters)
                if (p.Name == name)
                {
                    switch ((AnimParameterType)type)
                    {
                        case AnimParameterType::Float: p.FloatValue = value; break;
                        case AnimParameterType::Int: p.IntValue = (int)std::lround(value); break;
                        case AnimParameterType::Bool:
                        case AnimParameterType::Trigger: p.BoolValue = value != 0.0f; break;
                    }
                    break;
                }
            if (auto* na = reg.try_get<NetworkAnimatorComponent>(ent))
                na->Sent[name] = value;   // yankıyı tekrar göndermesin
        }
        if (IsServer() && r.Ok())
            SendToAllClients(relay.Data(), true, connection);
    }

    void NetworkSession::ServerHandleOwnerState(uint32_t connection, Reader& r)
    {
        const uint32_t clientId = ClientIdForConnection(connection);
        const uint64_t id = r.U64();
        const uint8_t flags = r.U8();
        glm::vec3 pos(0.0f), scale(1.0f);
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        if (flags & SyncPos) pos = r.Vec3();
        if (flags & SyncRot) rot = r.Quat();
        if (flags & SyncScale) scale = r.Vec3();
        auto it = m_Objects.find(id);
        if (!r.Ok() || it == m_Objects.end() || it->second.Owner != clientId || !m_Scene)
            return;   // yalnızca sahibi sürebilir
        const auto ent = (entt::entity)it->second.Entity;
        auto* nt = m_Scene->m_Registry.try_get<NetworkTransformComponent>(ent);
        if (!nt || nt->Authority != 1 || !m_Scene->m_Registry.valid(ent))
            return;
        auto& tc = m_Scene->m_Registry.get<TransformComponent>(ent);
        if (flags & SyncPos) tc.Position = pos;
        if (flags & SyncRot) tc.Rotation = glm::eulerAngles(rot);
        if (flags & SyncScale) tc.Scale = scale;
    }

    void NetworkSession::ClientHandleSnapshot(Reader& r)
    {
        (void)r.U32(); // tick
        const double serverTime = r.F64();
        const uint16_t count = r.U16();
        if (!r.Ok() || !m_Scene)
            return;
        // Sunucu saati tahmini: en düşük gecikmeyi izle, sapmayı yavaşça düzelt
        const double offset = serverTime - m_Time;
        if (!m_HasTimeOffset || offset > m_ServerTimeOffset)
            m_ServerTimeOffset = offset;
        else
            m_ServerTimeOffset += (offset - m_ServerTimeOffset) * 0.02;
        m_HasTimeOffset = true;

        auto& reg = m_Scene->m_Registry;
        for (uint16_t i = 0; i < count && r.Ok(); ++i)
        {
            const uint64_t id = r.U64();
            const uint8_t flags = r.U8();
            NetworkSnapshot s;
            s.Time = serverTime;
            bool hasPos = false, hasRot = false, hasScale = false;
            if (flags & SyncPos) { s.Position = r.Vec3(); hasPos = true; }
            if (flags & SyncRot) { s.Rotation = r.Quat(); hasRot = true; }
            if (flags & SyncScale) { s.Scale = r.Vec3(); hasScale = true; }
            if (!r.Ok())
                break;
            auto it = m_Objects.find(id);
            if (it == m_Objects.end())
                continue;
            const auto ent = (entt::entity)it->second.Entity;
            if (!reg.valid(ent))
                continue;
            auto* nt = reg.try_get<NetworkTransformComponent>(ent);
            if (!nt)
                continue;
            // Kendi sürdüğüm nesnenin yankısı yok sayılır
            if (nt->Authority == 1 && it->second.Owner == m_LocalClientId)
                continue;
            auto& tc = reg.get<TransformComponent>(ent);
            // Eksik alanlar son bilinen değerden
            const NetworkSnapshot* last = nt->Buffer.empty() ? nullptr : &nt->Buffer.back();
            if (!hasPos) s.Position = last ? last->Position : tc.Position;
            if (!hasRot) s.Rotation = last ? last->Rotation : glm::quat(tc.Rotation);
            if (!hasScale) s.Scale = last ? last->Scale : tc.Scale;
            if (!nt->Interpolate)
            {
                tc.Position = s.Position;
                tc.Rotation = glm::eulerAngles(s.Rotation);
                tc.Scale = s.Scale;
                nt->Buffer.clear();
                nt->Buffer.push_back(s);
                continue;
            }
            if (last && serverTime <= last->Time)
                continue;  // eski / yinelenen paket
            nt->Buffer.push_back(s);
            while (nt->Buffer.size() > 64)
                nt->Buffer.pop_front();
        }
    }

    void NetworkSession::ApplyInterpolation()
    {
        if (!m_Scene || !m_HasTimeOffset)
            return;
        const double renderTime = m_Time + m_ServerTimeOffset - m_InterpolationDelay;
        auto& reg = m_Scene->m_Registry;
        for (auto& [id, obj] : m_Objects)
        {
            const auto ent = (entt::entity)obj.Entity;
            if (!reg.valid(ent))
                continue;
            auto* nt = reg.try_get<NetworkTransformComponent>(ent);
            if (!nt || !nt->Interpolate || nt->Buffer.empty())
                continue;
            if (nt->Authority == 1 && obj.Owner == m_LocalClientId)
                continue;
            auto& buf = nt->Buffer;
            // renderTime'ı çevreleyen iki örnek
            while (buf.size() >= 3 && buf[1].Time <= renderTime)
                buf.pop_front();
            NetworkSnapshot s;
            if (buf.size() == 1 || renderTime <= buf.front().Time)
            {
                s = buf.front();
            }
            else
            {
                const NetworkSnapshot& a = buf[0];
                const NetworkSnapshot& b = buf[1];
                const double span = std::max(1e-6, b.Time - a.Time);
                // Paket gecikirse en fazla bir tick kadar ileri tahmin
                const float t = (float)std::clamp((renderTime - a.Time) / span, 0.0, 1.0 + 1.0 / std::max(1.0f, m_TickRate) / span);
                s.Position = glm::mix(a.Position, b.Position, t);
                s.Rotation = glm::slerp(a.Rotation, b.Rotation, std::min(t, 1.0f));
                s.Scale = glm::mix(a.Scale, b.Scale, std::min(t, 1.0f));
            }
            auto& tc = reg.get<TransformComponent>(ent);
            if (nt->SyncPosition) tc.Position = s.Position;
            if (nt->SyncRotation) tc.Rotation = glm::eulerAngles(s.Rotation);
            if (nt->SyncScale) tc.Scale = s.Scale;
        }
    }

    // ── RPC / değişken / mesaj ──
    bool NetworkSession::SendRpc(uint32_t entity, const std::string& method, RpcTarget target, const CQS::Value* args, int argc)
    {
        if (m_Mode == NetMode::Offline)
        {
            // Çevrimdışı: yerel çağrı (tek oyunculu testler aynı kodu çalıştırır)
            InvokeOnEntity(entity, method.c_str(), args, argc);
            return true;
        }
        const uint64_t id = GetNetworkId(entity);
        if (!id)
        {
            m_LastError = "RPC on an entity without a spawned NetworkObject";
            return false;
        }
        Writer w(Msg::Rpc);
        w.U64(id);
        w.Str(method);
        w.U8((uint8_t)target);
        w.U32(m_LocalClientId);
        w.U8((uint8_t)std::clamp(argc, 0, 16));
        for (int i = 0; i < std::min(argc, 16); ++i)
            w.Value(NetValue::FromScript(args[i]));

        if (IsServer())
        {
            const auto it = m_Objects.find(id);
            const uint32_t owner = it != m_Objects.end() ? it->second.Owner : 0;
            switch (target)
            {
                case RpcTarget::Server:
                    InvokeOnEntity(entity, method.c_str(), args, argc);
                    break;
                case RpcTarget::Clients:
                case RpcTarget::Everyone:
                    SendToAllClients(w.Data(), true);
                    if (IsHost() || target == RpcTarget::Everyone)
                        InvokeOnEntity(entity, method.c_str(), args, argc);
                    break;
                case RpcTarget::Owner:
                    if (owner == 0)
                        InvokeOnEntity(entity, method.c_str(), args, argc);
                    else if (ClientInfo* c = FindClient(owner))
                        SendToConnection(c->Connection, w.Data(), true);
                    break;
            }
            return true;
        }
        if (!m_Welcomed)
        {
            m_LastError = "Not connected";
            return false;
        }
        SendToServer(w.Data(), true);
        // "Everyone" çağıran istemcide de çalışır (sunucu ona geri göndermez)
        if (target == RpcTarget::Everyone)
            InvokeOnEntity(entity, method.c_str(), args, argc);
        return true;
    }

    void NetworkSession::HandleRpc(uint32_t connection, Reader& r)
    {
        const uint64_t id = r.U64();
        const std::string method = r.Str();
        const RpcTarget target = (RpcTarget)r.U8();
        (void)r.U32(); // gönderen (istemcinin bildirdiği; sunucu bağlantıdan doğrular)
        const uint8_t argc = r.U8();
        std::vector<NetValue> values;
        for (uint8_t i = 0; i < argc && r.Ok(); ++i)
            values.push_back(r.Value());
        if (!r.Ok() || method.empty())
            return;
        auto it = m_Objects.find(id);
        if (it == m_Objects.end())
            return;
        const uint32_t entity = it->second.Entity;

        auto invoke = [&]() {
            std::vector<CQS::Value> args;
            args.reserve(values.size());
            for (const auto& v : values)
                args.push_back(v.ToScript());
            InvokeOnEntity(entity, method.c_str(), args.data(), (int)args.size());
        };

        if (!IsServer())
        {
            invoke();
            return;
        }
        // Sunucu: istemciden gelen RPC
        const uint32_t sender = ClientIdForConnection(connection);
        switch (target)
        {
            case RpcTarget::Server:
                invoke();
                break;
            case RpcTarget::Clients:
            case RpcTarget::Everyone:
            {
                // Aktarma: gönderen hariç tüm istemcilere (Everyone'da gönderen zaten kendinde çalıştırdı)
                Writer w(Msg::Rpc);
                w.U64(id);
                w.Str(method);
                w.U8((uint8_t)target);
                w.U32(sender);
                w.U8((uint8_t)values.size());
                for (const auto& v : values)
                    w.Value(v);
                SendToAllClients(w.Data(), true, target == RpcTarget::Everyone ? connection : 0);
                if (IsHost() || target == RpcTarget::Everyone)
                    invoke();
                break;
            }
            case RpcTarget::Owner:
                if (it->second.Owner == 0)
                    invoke();
                else if (ClientInfo* c = FindClient(it->second.Owner))
                {
                    Writer w(Msg::Rpc);
                    w.U64(id);
                    w.Str(method);
                    w.U8((uint8_t)target);
                    w.U32(sender);
                    w.U8((uint8_t)values.size());
                    for (const auto& v : values)
                        w.Value(v);
                    SendToConnection(c->Connection, w.Data(), true);
                }
                break;
        }
    }

    bool NetworkSession::SetVar(uint32_t entity, const std::string& name, const CQS::Value& value)
    {
        const uint64_t id = GetNetworkId(entity);
        auto it = m_Objects.find(id);
        if (it == m_Objects.end())
        {
            m_LastError = "Network variables need a spawned NetworkObject";
            return false;
        }
        NetValue v = NetValue::FromScript(value);
        // Sunucu her nesneye, istemci yalnızca sahip olduğu nesneye yazabilir
        if (!IsServer() && it->second.Owner != m_LocalClientId)
        {
            m_LastError = "Only the owner or the server can change this variable";
            return false;
        }
        auto existing = it->second.Vars.find(name);
        if (existing != it->second.Vars.end() && existing->second == v)
            return true;
        it->second.Vars[name] = v;
        Writer w(Msg::Var);
        w.U64(id);
        w.Str(name);
        w.Value(v);
        if (IsServer())
            SendToAllClients(w.Data(), true);
        else
            SendToServer(w.Data(), true);
        CQS::Value args[2] = { CQS::Value::MakeString(name), v.ToScript() };
        InvokeOnEntity(entity, "OnNetworkVariableChanged", args, 2);
        return true;
    }

    bool NetworkSession::GetVar(uint32_t entity, const std::string& name, NetValue& out) const
    {
        const uint64_t id = GetNetworkId(entity);
        auto it = m_Objects.find(id);
        if (it == m_Objects.end())
            return false;
        auto v = it->second.Vars.find(name);
        if (v == it->second.Vars.end())
            return false;
        out = v->second;
        return true;
    }

    void NetworkSession::HandleVar(uint32_t connection, Reader& r)
    {
        const uint64_t id = r.U64();
        const std::string name = r.Str();
        NetValue v = r.Value();
        auto it = m_Objects.find(id);
        if (!r.Ok() || it == m_Objects.end() || name.empty())
            return;
        if (IsServer())
        {
            // İstemci yalnızca kendi nesnesine yazabilir; sunucu herkese yayar
            if (it->second.Owner != ClientIdForConnection(connection))
                return;
            it->second.Vars[name] = v;
            Writer w(Msg::Var);
            w.U64(id);
            w.Str(name);
            w.Value(v);
            SendToAllClients(w.Data(), true, connection);
        }
        else
        {
            it->second.Vars[name] = v;
        }
        CQS::Value args[2] = { CQS::Value::MakeString(name), v.ToScript() };
        InvokeOnEntity(it->second.Entity, "OnNetworkVariableChanged", args, 2);
    }

    bool NetworkSession::SendNetMessage(const std::string& name, const CQS::Value& value)
    {
        if (m_Mode == NetMode::Offline)
            return false;
        Writer w(Msg::Message);
        w.Str(name);
        w.U32(m_LocalClientId);
        w.Value(NetValue::FromScript(value));
        if (IsServer())
            SendToAllClients(w.Data(), true);
        else if (m_Welcomed)
            SendToServer(w.Data(), true);
        else
            return false;
        return true;
    }

    void NetworkSession::HandleMessage(uint32_t connection, Reader& r)
    {
        const std::string name = r.Str();
        uint32_t sender = r.U32();
        NetValue v = r.Value();
        if (!r.Ok())
            return;
        if (IsServer())
        {
            // Sunucu gönderen kimliğini bağlantıdan belirler ve diğer istemcilere aktarır
            sender = ClientIdForConnection(connection);
            Writer w(Msg::Message);
            w.Str(name);
            w.U32(sender);
            w.Value(v);
            SendToAllClients(w.Data(), true, connection);
        }
        CQS::Value args[3] = { CQS::Value::MakeString(name), v.ToScript(), CQS::Value::MakeInt((int64_t)sender) };
        InvokeOnAll("OnNetMessage", args, 3);
    }

    // ── Gönderim yardımcıları ──
    void NetworkSession::SendToConnection(uint32_t connection, const std::vector<uint8_t>& data, bool reliable)
    {
        if (!m_Transport || connection == 0)
            return;
        m_Transport->Send(connection, data.data(), data.size(), reliable);
        m_Stats.BytesSent += data.size();
        ++m_Stats.MessagesSent;
    }

    void NetworkSession::SendToAllClients(const std::vector<uint8_t>& data, bool reliable, uint32_t exceptConnection)
    {
        for (const auto& c : m_Clients)
            if (c.Connection != 0 && c.Connection != exceptConnection)
                SendToConnection(c.Connection, data, reliable);
    }

    void NetworkSession::SendToServer(const std::vector<uint8_t>& data, bool reliable)
    {
        SendToConnection(m_ServerConnection, data, reliable);
    }

    uint32_t NetworkSession::ClientIdForConnection(uint32_t connection) const
    {
        for (const auto& c : m_Clients)
            if (c.Connection == connection && connection != 0)
                return c.ClientId;
        return UINT32_MAX;
    }

    ClientInfo* NetworkSession::FindClient(uint32_t clientId)
    {
        for (auto& c : m_Clients)
            if (c.ClientId == clientId)
                return &c;
        return nullptr;
    }

    // ── Betik çağrıları ──
    void NetworkSession::QueueCall(uint32_t entity, bool all, const char* method, std::vector<NetValue> args)
    {
        Scene* target = m_Scene ? m_Scene : ScriptContext::GetActiveScene();
        if (!target || !method)
            return;
        PendingCall call;
        call.Target = target;
        call.Entity = entity;
        call.All = all;
        call.Method = method;
        call.Args = std::move(args);
        m_PendingCalls.push_back(std::move(call));
    }

    void NetworkSession::InvokeOnEntity(uint32_t entity, const char* method, const CQS::Value* args, int argc)
    {
        std::vector<NetValue> values;
        for (int i = 0; i < argc; ++i)
            values.push_back(NetValue::FromScript(args[i]));
        QueueCall(entity, false, method, std::move(values));
    }

    void NetworkSession::InvokeOnAll(const char* method, const CQS::Value* args, int argc)
    {
        std::vector<NetValue> values;
        for (int i = 0; i < argc; ++i)
            values.push_back(NetValue::FromScript(args[i]));
        QueueCall(0, true, method, std::move(values));
    }

    void NetworkSession::FlushCalls(Scene* scene)
    {
        if (m_PendingCalls.empty() || !scene)
            return;
        // Çağrılar yeni çağrılar ekleyebilir: en fazla birkaç tur. Bekletilenler bir sonraki flush'a kalır.
        std::vector<PendingCall> deferred;
        for (int round = 0; round < 4 && !m_PendingCalls.empty(); ++round)
        {
            std::vector<PendingCall> calls;
            calls.swap(m_PendingCalls);
            for (auto& call : calls)
            {
                if (call.Target != scene)
                    continue;   // başka (durdurulmuş) sahneye ait
                // Yeni oluşturulan nesnenin betikleri bu karenin betik aşamasında örneklenir: o zamana dek bekle
                if (!call.All && call.Attempts < 8 && scene->m_Registry.valid((entt::entity)call.Entity))
                {
                    if (auto* sc = scene->m_Registry.try_get<ConquerorScriptComponent>((entt::entity)call.Entity))
                    {
                        bool pending = false;
                        for (const auto& script : sc->Scripts)
                            if (!script.bLoaded && !script.ScriptPath.empty())
                                pending = true;
                        if (pending)
                        {
                            ++call.Attempts;
                            deferred.push_back(std::move(call));
                            continue;
                        }
                    }
                }
                std::vector<CQS::Value> args;
                args.reserve(call.Args.size());
                for (const auto& v : call.Args)
                    args.push_back(v.ToScript());
                Scene* previous = m_Scene;
                if (!m_Scene)
                    m_Scene = scene;   // çevrimdışı RPC'ler
                if (call.All)
                {
                    std::vector<uint32_t> entities;
                    auto view = scene->m_Registry.view<ConquerorScriptComponent>();
                    for (auto e : view)
                        entities.push_back((uint32_t)e);
                    for (uint32_t e : entities)
                        InvokeNow(e, call.Method.c_str(), args.data(), (int)args.size());
                }
                else
                {
                    InvokeNow(call.Entity, call.Method.c_str(), args.data(), (int)args.size());
                }
                m_Scene = previous;
            }
        }
        for (auto& call : deferred)
            m_PendingCalls.push_back(std::move(call));
    }

    void NetworkSession::InvokeNow(uint32_t entity, const char* method, const CQS::Value* args, int argc)
    {
        if (!m_Scene || !m_Scene->m_Registry.valid((entt::entity)entity))
            return;
        auto* sc = m_Scene->m_Registry.try_get<ConquerorScriptComponent>((entt::entity)entity);
        if (!sc)
            return;
        // Betik çağrısı bileşen listesini değiştirebilir: örnekleri önce topla
        std::vector<std::pair<Scripting::ScriptRuntime*, void*>> targets;
        for (auto& script : sc->Scripts)
            if (script.Instance && script.Runtime)
                targets.emplace_back(static_cast<Scripting::ScriptRuntime*>(script.Runtime), script.Instance);
        ScriptContext::SetActiveScene(m_Scene);
        ScriptContext::EntityScope scope(entity);
        for (auto& [runtime, inst] : targets)
            if (!Scripting::ScriptRuntimes::IsRetired(inst) && runtime->HasMethod(inst, method))
                runtime->Invoke(inst, method, args, argc);
    }

}
