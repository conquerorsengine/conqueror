#pragma once

#include "Networking/NetworkTypes.h"
#include "Networking/Packet.h"

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdint>

namespace Conqueror
{
    /// Unique identifier for a network-replicated object
    using NetworkObjectID = uint32_t;
    constexpr NetworkObjectID NET_OBJ_INVALID = 0;

    /// Authority: who owns and can modify this object
    enum class NetworkAuthority : uint8_t
    {
        Server = 0,     // Server authoritative (default)
        Client,         // Client authoritative (local player)
        Shared          // Both can modify (risky, use with care)
    };

    /// Serialized state of a single replicated property
    struct ReplicatedProperty
    {
        std::string Name;
        enum class Type : uint8_t { Float, Int, Bool, Vec3, String } PropertyType;
        
        // Current value (union-style)
        float  FloatVal = 0;
        int    IntVal = 0;
        bool   BoolVal = false;
        float  Vec3Val[3] = {0, 0, 0};
        std::string StringVal;

        /// Write this property into a packet
        void Serialize(Packet& pkt) const;
        /// Read this property from a packet
        void Deserialize(Packet& pkt);
        
        /// Check if value has changed since last sync
        bool IsDirty = false;
    };

    /// A network-replicated object. Attach to entities that need to sync across network.
    class NetworkObject
    {
    public:
        NetworkObject() = default;
        explicit NetworkObject(NetworkObjectID id) : m_ID(id) {}

        NetworkObjectID GetID() const { return m_ID; }
        void SetID(NetworkObjectID id) { m_ID = id; }

        PeerID GetOwner() const { return m_Owner; }
        void SetOwner(PeerID owner) { m_Owner = owner; }

        NetworkAuthority GetAuthority() const { return m_Authority; }
        void SetAuthority(NetworkAuthority auth) { m_Authority = auth; }

        /// Register a float property for replication
        void RegisterFloat(const std::string& name, float* valuePtr);
        void RegisterInt(const std::string& name, int* valuePtr);
        void RegisterBool(const std::string& name, bool* valuePtr);
        void RegisterVec3(const std::string& name, float* xyz);
        void RegisterString(const std::string& name, std::string* valuePtr);

        /// Serialize all dirty properties into a packet
        Packet SerializeState();
        /// Deserialize state from a packet and apply to local values
        void DeserializeState(Packet& pkt);

        /// Mark all properties as dirty (force full sync)
        void MarkAllDirty();
        /// Clear dirty flags after sync
        void ClearDirty();
        /// Check if any property is dirty
        bool HasDirtyProperties() const;

    private:
        NetworkObjectID  m_ID = NET_OBJ_INVALID;
        PeerID           m_Owner = PEER_ID_SERVER;
        NetworkAuthority m_Authority = NetworkAuthority::Server;

        struct PropertyBinding
        {
            ReplicatedProperty Prop;
            void* ValuePtr = nullptr;  // Pointer to the actual value in the component
        };

        std::vector<PropertyBinding> m_Bindings;
    };

    /// Manages all replicated objects and handles state synchronization
    class ReplicationManager
    {
    public:
        static ReplicationManager& Get();

        /// Register an object for replication. Returns its NetworkObjectID.
        NetworkObjectID RegisterObject(NetworkObject* obj);
        /// Unregister an object
        void UnregisterObject(NetworkObjectID id);

        /// Called every network tick (server): serialize dirty state and broadcast
        void SyncAll();
        /// Called when receiving state update (client): apply to local objects
        void ApplyStateUpdate(Packet& pkt);

        /// Get object by ID
        NetworkObject* GetObject(NetworkObjectID id);

        /// Get all registered objects
        const std::unordered_map<NetworkObjectID, NetworkObject*>& GetObjects() const { return m_Objects; }

    private:
        ReplicationManager() = default;

        NetworkObjectID m_NextID = 1;
        std::unordered_map<NetworkObjectID, NetworkObject*> m_Objects;
    };
}
