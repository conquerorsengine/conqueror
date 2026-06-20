#pragma once

#include "NetworkTypes.h"
#include <string>

// Forward declaration — ENet header included only in .cpp
struct _ENetHost;
struct _ENetPeer;
struct _ENetEvent;

namespace Conqueror
{
    /// Low-level ENet UDP transport layer.
    /// Handles creating hosts, connecting, sending/receiving raw data.
    class ENetTransport
    {
    public:
        ENetTransport();
        ~ENetTransport();

        // Prevent copying
        ENetTransport(const ENetTransport&) = delete;
        ENetTransport& operator=(const ENetTransport&) = delete;

        /// Initialize ENet globally (call once at engine startup)
        static bool InitializeENet();
        /// Shutdown ENet globally (call once at engine shutdown)
        static void ShutdownENet();

        /// Create a server host that listens on the given port
        bool CreateServer(uint16_t port, int maxClients = CQ_MAX_CLIENTS);
        /// Create a client host (does not bind to a port)
        bool CreateClient();

        /// Connect to a server (client only)
        bool Connect(const std::string& host, uint16_t port);
        /// Disconnect from server or disconnect a specific peer
        void Disconnect(PeerID peerID = PEER_ID_SERVER);
        /// Disconnect all peers and destroy the host
        void Shutdown();

        /// Poll for events. Call every frame.
        /// Returns true if an event was processed.
        bool PollEvents();

        /// Send data to a specific peer
        void Send(PeerID peerID, const uint8_t* data, size_t size, NetworkChannel channel = NetworkChannel::Reliable);
        /// Broadcast data to all connected peers (server only)
        void Broadcast(const uint8_t* data, size_t size, NetworkChannel channel = NetworkChannel::Reliable);

        /// Get connection state
        bool IsActive() const { return m_Host != nullptr; }
        NetworkRole GetRole() const { return m_Role; }

        // ── Callbacks ──
        void SetOnConnect(OnConnectCallback cb)       { m_OnConnect = std::move(cb); }
        void SetOnDisconnect(OnDisconnectCallback cb) { m_OnDisconnect = std::move(cb); }
        void SetOnData(OnDataCallback cb)             { m_OnData = std::move(cb); }

    private:
        _ENetHost*  m_Host = nullptr;
        NetworkRole m_Role = NetworkRole::None;
        PeerID      m_NextPeerID = 2; // 1 = server

        // Peer mapping: PeerID ↔ ENetPeer*
        struct PeerEntry
        {
            _ENetPeer* Peer = nullptr;
            PeerID     ID = PEER_ID_INVALID;
        };
        std::vector<PeerEntry> m_Peers;

        _ENetPeer* FindPeer(PeerID id) const;
        PeerID     RegisterPeer(_ENetPeer* peer);
        void       UnregisterPeer(_ENetPeer* peer);
        PeerID     GetPeerID(_ENetPeer* peer) const;

        // Callbacks
        OnConnectCallback    m_OnConnect;
        OnDisconnectCallback m_OnDisconnect;
        OnDataCallback       m_OnData;

        static bool s_ENetInitialized;
    };
}
