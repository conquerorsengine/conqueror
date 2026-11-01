#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Networking/Multiplayer/NetworkSession.h"
#include "Networking/Multiplayer/NetworkComponents.h"
#include "Networking/Steam/SteamPlatform.h"

#include <cmath>
#include <string>

// Çok oyunculu betik API'si (Unity Netcode benzeri). Varlık argümanları -1/null ise "bu varlık".
namespace Conqueror::CQS
{
    namespace
    {
        using Net::NetworkSession;

        uint32_t EntityArg(int argc, Value* args, int i)
        {
            if (i < argc && args[i].IsNumber() && args[i].ToNumber() >= 0)
                return (uint32_t)(int64_t)args[i].ToNumber();
            return ScriptContext::GetCurrentEntity();
        }

        std::string StrArg(int argc, Value* args, int i, const std::string& def = "")
        {
            return i < argc && args[i].IsString() ? args[i].AsString()->Value : def;
        }

        double NumArg(int argc, Value* args, int i, double def = 0.0)
        {
            return i < argc && args[i].IsNumber() ? args[i].ToNumber() : def;
        }

        // Sahnedeki NetworkManager (adres/port/taşıyıcı varsayılanları)
        NetworkManagerComponent ManagerSettings()
        {
            if (Scene* scene = ScriptContext::GetActiveScene())
            {
                auto view = scene->m_Registry.view<NetworkManagerComponent>();
                for (auto e : view)
                    return view.get<NetworkManagerComponent>(e);
            }
            return {};
        }

        Value StartServer(int argc, Value* args, bool host)
        {
            auto& s = NetworkSession::Get();
            if (s.IsOnline())
                return Value::MakeBool(false);
            const auto settings = ManagerSettings();
            const int port = (int)NumArg(argc, args, 0, settings.Port);
            return Value::MakeBool(s.StartServer(ScriptContext::GetActiveScene(), (uint16_t)port, host, settings.Transport));
        }

        Net::RpcTarget TargetFromName(const std::string& t)
        {
            if (t == "server") return Net::RpcTarget::Server;
            if (t == "owner") return Net::RpcTarget::Owner;
            if (t == "everyone" || t == "all") return Net::RpcTarget::Everyone;
            return Net::RpcTarget::Clients;
        }

        // NetRpc*(method, args...) bu varlıkta; NetEntityRpc(entity, target, method, args...)
        Value SendRpc(uint32_t entity, Net::RpcTarget target, int argc, Value* args, int methodIndex)
        {
            const std::string method = StrArg(argc, args, methodIndex);
            if (method.empty())
                return Value::MakeBool(false);
            const int first = methodIndex + 1;
            return Value::MakeBool(NetworkSession::Get().SendRpc(entity, method, target, args + first, std::max(0, argc - first)));
        }
    }

    void RegisterNetNatives(Scripting::NativeRegistry* vm)
    {
        // ── Oturum ──
        vm->DefineNative("NetStartHost", [](int argc, Value* args) -> Value { return StartServer(argc, args, true); }, -1);
        vm->DefineNative("NetStartServer", [](int argc, Value* args) -> Value { return StartServer(argc, args, false); }, -1);
        vm->DefineNative("NetStartClient", [](int argc, Value* args) -> Value {
            auto& s = NetworkSession::Get();
            if (s.IsOnline())
                return Value::MakeBool(false);
            const auto settings = ManagerSettings();
            const std::string address = StrArg(argc, args, 0, settings.Address);
            const int port = (int)NumArg(argc, args, 1, settings.Port);
            return Value::MakeBool(s.StartClient(ScriptContext::GetActiveScene(), address, (uint16_t)port, settings.Transport));
        }, -1);
        vm->DefineNative("NetShutdown", [](int, Value*) -> Value {
            NetworkSession::Get().RequestShutdown();
            return Value::MakeNull();
        }, 0);
        vm->DefineNative("NetIsServer", [](int, Value*) -> Value { return Value::MakeBool(NetworkSession::Get().IsServer()); }, 0);
        vm->DefineNative("NetIsClient", [](int, Value*) -> Value { return Value::MakeBool(NetworkSession::Get().IsClient()); }, 0);
        vm->DefineNative("NetIsHost", [](int, Value*) -> Value { return Value::MakeBool(NetworkSession::Get().IsHost()); }, 0);
        vm->DefineNative("NetIsOnline", [](int, Value*) -> Value { return Value::MakeBool(NetworkSession::Get().IsOnline()); }, 0);
        vm->DefineNative("NetIsConnected", [](int, Value*) -> Value { return Value::MakeBool(NetworkSession::Get().IsConnected()); }, 0);
        vm->DefineNative("NetLocalClientId", [](int, Value*) -> Value { return Value::MakeInt((int64_t)NetworkSession::Get().GetLocalClientId()); }, 0);
        vm->DefineNative("NetClientCount", [](int, Value*) -> Value { return Value::MakeInt((int64_t)NetworkSession::Get().GetClients().size()); }, 0);
        vm->DefineNative("NetGetClients", [](int, Value*) -> Value {
            auto* list = new CQSListObject();
            for (const auto& c : NetworkSession::Get().GetClients())
                list->Elements.push_back(Value::MakeInt((int64_t)c.ClientId));
            return Value::MakeObject(list);
        }, 0);
        vm->DefineNative("NetGetStatus", [](int, Value*) -> Value { return Value::MakeString(NetworkSession::Get().GetStatusText()); }, 0);
        vm->DefineNative("NetGetLastError", [](int, Value*) -> Value { return Value::MakeString(NetworkSession::Get().GetLastError()); }, 0);
        vm->DefineNative("NetGetStats", [](int, Value*) -> Value {
            const auto& st = NetworkSession::Get().GetStats();
            auto* map = new CQSMapObject();
            map->Entries["sentKBps"] = Value::MakeFloat(st.SendKBps);
            map->Entries["receivedKBps"] = Value::MakeFloat(st.ReceiveKBps);
            map->Entries["rtt"] = Value::MakeFloat(st.RTTMs);
            map->Entries["bytesSent"] = Value::MakeInt((int64_t)st.BytesSent);
            map->Entries["bytesReceived"] = Value::MakeInt((int64_t)st.BytesReceived);
            return Value::MakeObject(map);
        }, 0);
        vm->DefineNative("NetGetPing", [](int, Value*) -> Value { return Value::MakeFloat(NetworkSession::Get().GetStats().RTTMs); }, 0);

        // ── Nesneler ──
        vm->DefineNative("NetSpawn", [](int argc, Value* args) -> Value {
            const std::string prefab = StrArg(argc, args, 0);
            const glm::vec3 pos((float)NumArg(argc, args, 1), (float)NumArg(argc, args, 2), (float)NumArg(argc, args, 3));
            const uint32_t owner = (uint32_t)NumArg(argc, args, 4, 0.0);
            const uint32_t e = NetworkSession::Get().Spawn(prefab, pos, owner);
            return e == UINT32_MAX ? Value::MakeInt(-1) : Value::MakeInt((int64_t)e);
        }, -1);
        vm->DefineNative("NetDespawn", [](int argc, Value* args) -> Value {
            return Value::MakeBool(NetworkSession::Get().Despawn(EntityArg(argc, args, 0)));
        }, -1);
        vm->DefineNative("NetIsOwner", [](int argc, Value* args) -> Value {
            return Value::MakeBool(NetworkSession::Get().IsOwner(EntityArg(argc, args, 0)));
        }, -1);
        vm->DefineNative("NetGetOwner", [](int argc, Value* args) -> Value {
            return Value::MakeInt((int64_t)NetworkSession::Get().GetOwner(EntityArg(argc, args, 0)));
        }, -1);
        vm->DefineNative("NetChangeOwner", [](int argc, Value* args) -> Value {
            return Value::MakeBool(NetworkSession::Get().ChangeOwnership(EntityArg(argc, args, 0), (uint32_t)NumArg(argc, args, 1, 0.0)));
        }, 2);
        vm->DefineNative("NetGetNetworkId", [](int argc, Value* args) -> Value {
            return Value::MakeInt((int64_t)NetworkSession::Get().GetNetworkId(EntityArg(argc, args, 0)));
        }, -1);
        vm->DefineNative("NetFindEntity", [](int argc, Value* args) -> Value {
            const uint64_t id = (uint64_t)(int64_t)NumArg(argc, args, 0, 0.0);
            const uint32_t e = NetworkSession::Get().FindEntity(id);
            return e == UINT32_MAX ? Value::MakeInt(-1) : Value::MakeInt((int64_t)e);
        }, 1);

        // ── RPC'ler (bu varlıkta): sunucuya, tüm istemcilere, sahibine, herkese ──
        vm->DefineNative("NetRpcServer", [](int argc, Value* args) -> Value {
            return SendRpc(ScriptContext::GetCurrentEntity(), Net::RpcTarget::Server, argc, args, 0);
        }, -1);
        vm->DefineNative("NetRpcClients", [](int argc, Value* args) -> Value {
            return SendRpc(ScriptContext::GetCurrentEntity(), Net::RpcTarget::Clients, argc, args, 0);
        }, -1);
        vm->DefineNative("NetRpcOwner", [](int argc, Value* args) -> Value {
            return SendRpc(ScriptContext::GetCurrentEntity(), Net::RpcTarget::Owner, argc, args, 0);
        }, -1);
        vm->DefineNative("NetRpcAll", [](int argc, Value* args) -> Value {
            return SendRpc(ScriptContext::GetCurrentEntity(), Net::RpcTarget::Everyone, argc, args, 0);
        }, -1);
        // NetEntityRpc(entity, "server"|"clients"|"owner"|"all", method, args...)
        vm->DefineNative("NetEntityRpc", [](int argc, Value* args) -> Value {
            return SendRpc(EntityArg(argc, args, 0), TargetFromName(StrArg(argc, args, 1, "clients")), argc, args, 2);
        }, -1);

        // ── Ağ değişkenleri ve mesajlar ──
        vm->DefineNative("NetSetVar", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            return Value::MakeBool(NetworkSession::Get().SetVar(ScriptContext::GetCurrentEntity(), StrArg(argc, args, 0), args[1]));
        }, 2);
        vm->DefineNative("NetGetVar", [](int argc, Value* args) -> Value {
            Net::NetValue v;
            if (NetworkSession::Get().GetVar(ScriptContext::GetCurrentEntity(), StrArg(argc, args, 0), v))
                return v.ToScript();
            return argc > 1 ? args[1] : Value::MakeNull();
        }, -1);
        vm->DefineNative("NetSetEntityVar", [](int argc, Value* args) -> Value {
            if (argc < 3) return Value::MakeBool(false);
            return Value::MakeBool(NetworkSession::Get().SetVar(EntityArg(argc, args, 0), StrArg(argc, args, 1), args[2]));
        }, 3);
        vm->DefineNative("NetGetEntityVar", [](int argc, Value* args) -> Value {
            Net::NetValue v;
            if (NetworkSession::Get().GetVar(EntityArg(argc, args, 0), StrArg(argc, args, 1), v))
                return v.ToScript();
            return argc > 2 ? args[2] : Value::MakeNull();
        }, -1);
        vm->DefineNative("NetSend", [](int argc, Value* args) -> Value {
            return Value::MakeBool(NetworkSession::Get().SendNetMessage(StrArg(argc, args, 0), argc > 1 ? args[1] : Value::MakeNull()));
        }, -1);

        // ── Eski adlar (önceki düşük seviye API) yeni oturuma yönlendirilir ──
        vm->DefineNative("NetConnect", [](int argc, Value* args) -> Value {
            auto& s = NetworkSession::Get();
            if (s.IsOnline())
                return Value::MakeBool(false);
            const auto settings = ManagerSettings();
            return Value::MakeBool(s.StartClient(ScriptContext::GetActiveScene(), StrArg(argc, args, 0, settings.Address),
                                                 (uint16_t)NumArg(argc, args, 1, settings.Port), settings.Transport));
        }, -1);
        vm->DefineNative("NetDisconnect", [](int, Value*) -> Value { NetworkSession::Get().RequestShutdown(); return Value::MakeNull(); }, 0);
        vm->DefineNative("NetBroadcast", [](int argc, Value* args) -> Value {
            return Value::MakeBool(NetworkSession::Get().SendNetMessage("broadcast", argc > 0 ? args[0] : Value::MakeNull()));
        }, -1);
        vm->DefineNative("NetGetPeerCount", [](int, Value*) -> Value { return Value::MakeInt((int64_t)NetworkSession::Get().GetClients().size()); }, 0);
        vm->DefineNative("NetCallRPC", [](int argc, Value* args) -> Value {
            return SendRpc(ScriptContext::GetCurrentEntity(), Net::RpcTarget::Everyone, argc, args, 0);
        }, -1);

        // ── Steam ──
        vm->DefineNative("SteamIsAvailable", [](int, Value*) -> Value {
            return Value::MakeBool(SteamPlatform::IsAvailable() || SteamPlatform::Initialize());
        }, 0);
        vm->DefineNative("SteamGetLastError", [](int, Value*) -> Value { return Value::MakeString(SteamPlatform::GetLastError()); }, 0);
        vm->DefineNative("SteamGetPersonaName", [](int, Value*) -> Value { return Value::MakeString(SteamPlatform::GetPersonaName()); }, 0);
        vm->DefineNative("SteamGetSteamId", [](int, Value*) -> Value { return Value::MakeString(std::to_string(SteamPlatform::GetSteamID())); }, 0);
        vm->DefineNative("SteamCreateLobby", [](int argc, Value* args) -> Value {
            return Value::MakeBool(SteamPlatform::CreateLobby((int)NumArg(argc, args, 0, 4.0), argc < 2 || !args[1].IsBool() || args[1].BoolVal));
        }, -1);
        vm->DefineNative("SteamJoinLobby", [](int argc, Value* args) -> Value {
            return Value::MakeBool(SteamPlatform::JoinLobby(std::strtoull(StrArg(argc, args, 0).c_str(), nullptr, 10)));
        }, 1);
        vm->DefineNative("SteamLeaveLobby", [](int, Value*) -> Value { SteamPlatform::LeaveLobby(); return Value::MakeNull(); }, 0);
        vm->DefineNative("SteamRequestLobbyList", [](int, Value*) -> Value { return Value::MakeBool(SteamPlatform::RequestLobbyList()); }, 0);
        vm->DefineNative("SteamGetLobbyOwner", [](int argc, Value* args) -> Value {
            const uint64_t lobby = argc > 0 ? std::strtoull(StrArg(argc, args, 0).c_str(), nullptr, 10) : SteamPlatform::GetCurrentLobby();
            return Value::MakeString(std::to_string(SteamPlatform::GetLobbyOwner(lobby)));
        }, -1);
        vm->DefineNative("SteamSetLobbyData", [](int argc, Value* args) -> Value {
            return Value::MakeBool(SteamPlatform::SetLobbyData(StrArg(argc, args, 0), StrArg(argc, args, 1)));
        }, 2);
        vm->DefineNative("SteamGetLobbyData", [](int argc, Value* args) -> Value {
            return Value::MakeString(SteamPlatform::GetLobbyData(std::strtoull(StrArg(argc, args, 0).c_str(), nullptr, 10), StrArg(argc, args, 1)));
        }, 2);
    }
}
