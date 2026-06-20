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
    /// Unique RPC identifier
    using RPCId = uint32_t;

    /// RPC target: who should execute this call
    enum class RPCTarget : uint8_t
    {
        Server = 0,     // Only the server executes
        AllClients,     // All clients execute
        SpecificClient, // A specific client executes (peerID required)
        Everyone        // Server + all clients execute
    };

    /// RPC handler signature: (senderPeerID, args packet)
    using RPCHandler = std::function<void(PeerID, Packet&)>;

    /// Remote Procedure Call System.
    /// Register named functions that can be called across the network.
    class RPCSystem
    {
    public:
        static RPCSystem& Get();

        RPCSystem(const RPCSystem&) = delete;
        RPCSystem& operator=(const RPCSystem&) = delete;

        /// Register an RPC by name
        void Register(const std::string& name, RPCHandler handler);

        /// Unregister an RPC
        void Unregister(const std::string& name);

        /// Call an RPC on the server (client → server)
        void CallServer(const std::string& name, const Packet& args);

        /// Call an RPC on all clients (server → all clients)
        void CallAllClients(const std::string& name, const Packet& args);

        /// Call an RPC on a specific client (server → one client)
        void CallClient(PeerID peerID, const std::string& name, const Packet& args);

        /// Call an RPC on everyone including self
        void CallEveryone(const std::string& name, const Packet& args);

        /// Process an incoming RPC packet (called by NetworkManager)
        void ProcessRPC(PeerID senderID, Packet& pkt);

    private:
        RPCSystem() = default;

        void SendRPC(PeerID targetPeer, RPCTarget target, const std::string& name, const Packet& args);

        std::unordered_map<std::string, RPCHandler> m_Handlers;
    };
}
