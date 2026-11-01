#pragma once

#include "Core/Base/Base.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Net
{
    // Betik değerlerinin ağ biçimi (GC dışında saklanır; okunurken CQS değerine çevrilir)
    struct NetValue
    {
        enum class Type : uint8_t { Null = 0, Bool, Number, String, Map, List };
        Type Kind = Type::Null;
        double Number = 0.0;
        std::string Text;
        std::vector<std::pair<std::string, NetValue>> Map;
        std::vector<NetValue> List;

        static NetValue FromScript(const CQS::Value& v, int depth = 0);
        CQS::Value ToScript() const;
        bool operator==(const NetValue& o) const;
    };

    enum class NetMode : uint8_t { Offline = 0, Server, Host, Client };
    enum class RpcTarget : uint8_t { Server = 0, Clients, Owner, Everyone };

    struct ClientInfo
    {
        uint32_t ClientId = 0;
        uint32_t Connection = 0;     // taşıyıcı bağlantı kimliği (host'un yerel istemcisi için 0)
        std::string Name;
        float RTTMs = 0.0f;
        uint64_t PlayerObject = 0;   // oluşturulan oyuncu nesnesinin ağ kimliği
    };

    struct NetStats
    {
        uint64_t BytesSent = 0, BytesReceived = 0;
        uint64_t MessagesSent = 0, MessagesReceived = 0;
        float SendKBps = 0.0f, ReceiveKBps = 0.0f;
        float RTTMs = 0.0f;          // istemci: sunucuya gidiş-dönüş
    };

    class INetTransport;

    // Unity Netcode benzeri oturum: host/sunucu/istemci, nesne oluşturma, NetworkTransform eşleme,
    // RPC'ler, ağ değişkenleri ve mesajlar. Çalışma zamanı sahnesine bağlıdır; Stop'ta kapanır.
    class CQ_API NetworkSession
    {
    public:
        static NetworkSession& Get();

        bool StartServer(Scene* scene, uint16_t port, bool host, int transport = 0);
        bool StartClient(Scene* scene, const std::string& address, uint16_t port, int transport = 0);
        void Shutdown();
        // Betiklerden: oturum karenin sonunda kapanır (çalışan betiğin varlığı silinmesin)
        void RequestShutdown() { m_ShutdownRequested = true; }

        // Scene::OnRuntimeStart: NetworkManager "Auto Start" ya da komut satırı (--host / --server / --connect <adres>)
        void OnRuntimeStart(Scene* scene);
        // Scene::OnUpdateRuntime başı: gelen mesajlar, oluşturma/silme, ara değerleme
        void Update(Scene* scene, float dt);
        // Betiklerden sonra: durum gönderimi (tick)
        void PostUpdate(Scene* scene, float dt);
        // Sahne durdurulurken / yok edilirken
        void OnSceneStopped(Scene* scene);
        // Derlenmiş oyun komut satırı: --host, --server, --connect <adres[:port]>
        static void SetCommandLine(int argc, char** argv);

        NetMode GetMode() const { return m_Mode; }
        bool IsOnline() const { return m_Mode != NetMode::Offline; }
        bool IsServer() const { return m_Mode == NetMode::Server || m_Mode == NetMode::Host; }
        bool IsClient() const { return m_Mode == NetMode::Client || m_Mode == NetMode::Host; }
        bool IsHost() const { return m_Mode == NetMode::Host; }
        bool IsConnected() const;
        uint32_t GetLocalClientId() const { return m_LocalClientId; }
        const std::vector<ClientInfo>& GetClients() const { return m_Clients; }
        const NetStats& GetStats() const { return m_Stats; }
        std::string GetStatusText() const;
        const std::string& GetLastError() const { return m_LastError; }
        const char* GetTransportName() const;
        size_t GetObjectCount() const { return m_Objects.size(); }

        // ── Nesneler (yalnızca sunucu) ──
        uint32_t Spawn(const std::string& prefab, const glm::vec3& position, uint32_t owner);
        bool Despawn(uint32_t entity);
        bool ChangeOwnership(uint32_t entity, uint32_t owner);
        uint64_t GetNetworkId(uint32_t entity) const;
        uint32_t FindEntity(uint64_t networkId) const;
        uint32_t GetOwner(uint32_t entity) const;
        bool IsOwner(uint32_t entity) const;

        // ── RPC / değişken / mesaj ──
        bool SendRpc(uint32_t entity, const std::string& method, RpcTarget target, const CQS::Value* args, int argc);
        bool SetVar(uint32_t entity, const std::string& name, const CQS::Value& value);
        bool GetVar(uint32_t entity, const std::string& name, NetValue& out) const;
        // (SendMessage adı Windows makrosuyla çakışır)
        bool SendNetMessage(const std::string& name, const CQS::Value& value);

    private:
        NetworkSession() = default;
        ~NetworkSession();

        struct NetObject
        {
            uint64_t Id = 0;
            uint32_t Entity = 0;
            uint32_t Owner = 0;
            std::string Prefab;
            bool SceneObject = false;
            std::unordered_map<std::string, NetValue> Vars;
        };

        void RegisterSceneObjects();
        void HandleConnect(uint32_t connection);
        void HandleDisconnect(uint32_t connection);
        void HandleData(uint32_t connection, const uint8_t* data, size_t size);

        void ServerHandleHello(uint32_t connection, class Reader& r);
        void ClientHandleWelcome(class Reader& r);
        void ClientHandleSpawn(class Reader& r);
        void ClientHandleDespawn(class Reader& r);
        void ClientHandleSnapshot(class Reader& r);
        void ServerHandleOwnerState(uint32_t connection, class Reader& r);
        void HandleRpc(uint32_t connection, class Reader& r);
        void HandleVar(uint32_t connection, class Reader& r);
        void HandleMessage(uint32_t connection, class Reader& r);
        void ClientHandleOwnership(class Reader& r);

        void SendTick();
        void SendAnimParams();
        void HandleAnimParams(uint32_t connection, class Reader& r);
        void MakeRemoteKinematic(uint32_t entity);
        uint32_t InstantiateNetworked(const std::string& prefab, const glm::vec3& pos, const glm::quat* rot, const glm::vec3* scale);
        void ApplyInterpolation();
        void SendToConnection(uint32_t connection, const std::vector<uint8_t>& data, bool reliable);
        void SendToAllClients(const std::vector<uint8_t>& data, bool reliable, uint32_t exceptConnection = 0);
        void SendToServer(const std::vector<uint8_t>& data, bool reliable);
        std::vector<uint8_t> BuildSpawnMessage(const NetObject& obj) const;
        uint32_t ClientIdForConnection(uint32_t connection) const;
        ClientInfo* FindClient(uint32_t clientId);
        void SpawnPlayerFor(uint32_t clientId);
        void DespawnInternal(uint64_t id, bool broadcast);
        // Betik geri çağrıları kuyruğa alınır ve betik yürütmesi dışında (Update/PostUpdate sonunda) çalıştırılır:
        // bir betik NetStartHost / NetRpc çağırırken başka betiklere iç içe girilmez
        void InvokeOnEntity(uint32_t entity, const char* method, const CQS::Value* args, int argc);
        void InvokeOnAll(const char* method, const CQS::Value* args, int argc);
        void QueueCall(uint32_t entity, bool all, const char* method, std::vector<NetValue> args);
        void FlushCalls(Scene* scene);
        void InvokeNow(uint32_t entity, const char* method, const CQS::Value* args, int argc);
        void Fail(const std::string& error);

    private:
        std::unique_ptr<INetTransport> m_Transport;
        Scene* m_Scene = nullptr;
        NetMode m_Mode = NetMode::Offline;
        bool m_Welcomed = false;              // istemci: sunucu Welcome gönderdi
        uint32_t m_LocalClientId = 0;
        uint32_t m_NextClientId = 1;
        uint32_t m_ServerConnection = 0;      // istemci tarafında sunucu bağlantısı
        std::vector<ClientInfo> m_Clients;    // sunucu: bağlı istemciler (host dahil)
        std::unordered_map<uint32_t, uint32_t> m_PendingConnections; // hello bekleyen bağlantılar
        std::unordered_map<uint64_t, NetObject> m_Objects;
        uint64_t m_NextDynamicId = 1;

        float m_TickRate = 30.0f;
        float m_InterpolationDelay = 0.1f;
        double m_Time = 0.0;
        double m_TickAccumulator = 0.0;
        double m_LastFullSync = 0.0;
        uint32_t m_Tick = 0;
        double m_ServerTimeOffset = 0.0;      // istemci: sunucu saati - yerel saat (yumuşatılmış)
        bool m_HasTimeOffset = false;

        NetStats m_Stats;
        double m_StatsWindowStart = 0.0;
        uint64_t m_StatsBytesSentStart = 0, m_StatsBytesRecvStart = 0;
        std::string m_LastError;
        struct PendingCall
        {
            Scene* Target = nullptr;
            uint32_t Entity = 0;
            bool All = false;
            int Attempts = 0;              // betik örneği henüz oluşmadıysa sonraki karede yeniden denenir
            std::string Method;
            std::vector<NetValue> Args;
        };
        std::vector<PendingCall> m_PendingCalls;
        std::vector<uint64_t> m_PendingDespawns;
        bool m_ShutdownRequested = false;
        Scene* m_AutoStartScene = nullptr;
        std::string m_PlayerPrefab;
        glm::vec3 m_SpawnPoint{ 0.0f };
        float m_SpawnSpacing = 2.0f;
        int m_SpawnCount = 0;
    };
}
