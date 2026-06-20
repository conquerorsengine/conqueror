#pragma once

#include "NetworkTypes.h"
#include "NetworkPeer.h"
#include "Packet.h"
#include "ENetTransport.h"
#include "TCPSocket.h"
#include "Encryption/NetworkEncryption.h"
#include "NAT/NATTraversal.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <queue>
#include <mutex>

namespace Conqueror
{
    /// Packet handler callback: (senderPeerID, packet)
    using PacketHandler = std::function<void(PeerID, Packet&)>;

    /// High-level Network Manager.
    /// Provides Server/Client API built on top of ENet (UDP) and TCP sockets.
    class NetworkManager
    {
    public:
        static NetworkManager& Get();

        // Prevent copying
        NetworkManager(const NetworkManager&) = delete;
        NetworkManager& operator=(const NetworkManager&) = delete;

        // ── Lifecycle ──
        void Initialize();
        void Shutdown();
        /// Call every frame to process network events
        void Update();

        // ── Server ──
        bool StartServer(uint16_t udpPort = CQ_DEFAULT_PORT, uint16_t tcpPort = 0);
        void StopServer();

        // ── Client ──
        bool ConnectToServer(const std::string& host, uint16_t udpPort = CQ_DEFAULT_PORT, uint16_t tcpPort = 0);
        void DisconnectFromServer();

        // ── Sending ──
        /// Send a packet to a specific peer (server→client or client→server)
        void SendPacket(PeerID peerID, const Packet& packet, NetworkChannel channel = NetworkChannel::Reliable);
        /// Broadcast a packet to all connected peers (server only)
        void BroadcastPacket(const Packet& packet, NetworkChannel channel = NetworkChannel::Reliable);
        /// Send a packet via TCP to a specific socket fd
        void SendTCP(int socketFd, const Packet& packet);
        /// Send an encrypted packet
        void SendEncrypted(PeerID peerID, const Packet& packet, NetworkChannel channel = NetworkChannel::Reliable);

        // ── Encryption ──
        /// Enable encryption (generates RSA keypair + session key)
        bool EnableEncryption();
        bool IsEncryptionEnabled() const { return m_EncryptionEnabled; }
        NetworkEncryption& GetEncryption() { return m_Encryption; }

        // ── NAT Traversal ──
        /// Open a port on the router via UPnP
        bool OpenNATPort(uint16_t port, const std::string& protocol = "UDP");
        /// Close a previously opened port
        bool CloseNATPort(uint16_t port, const std::string& protocol = "UDP");
        /// Get external (public) IP
        std::string GetExternalIP() const;
        NATTraversal& GetNAT() { return m_NAT; }

        // ── Message Handlers ──
        /// Register a handler for a specific message type
        void RegisterHandler(MessageType type, PacketHandler handler);
        /// Unregister handler
        void UnregisterHandler(MessageType type);

        // ── State ──
        bool IsServer() const { return m_Role == NetworkRole::Server; }
        bool IsClient() const { return m_Role == NetworkRole::Client; }
        bool IsConnected() const { return m_State == ConnectionState::Connected; }
        NetworkRole GetRole() const { return m_Role; }
        ConnectionState GetState() const { return m_State; }
        PeerID GetLocalPeerID() const { return m_LocalPeerID; }

        // ── Peers ──
        const std::unordered_map<PeerID, NetworkPeer>& GetPeers() const { return m_Peers; }
        size_t GetPeerCount() const { return m_Peers.size(); }

        // ── Events ──
        void SetOnPeerConnected(OnConnectCallback cb)       { m_OnPeerConnected = std::move(cb); }
        void SetOnPeerDisconnected(OnDisconnectCallback cb)  { m_OnPeerDisconnected = std::move(cb); }

    private:
        NetworkManager() = default;
        ~NetworkManager() = default;

        void OnENetConnect(PeerID peerID);
        void OnENetDisconnect(PeerID peerID);
        void OnENetData(PeerID peerID, const uint8_t* data, size_t size);

        void ProcessPacket(PeerID senderID, Packet& packet);
        void HandleHandshake(PeerID senderID, Packet& packet);
        void HandlePing(PeerID senderID, Packet& packet);

        // TCP server polling (non-blocking accept + recv)
        void UpdateTCPServer();
        void UpdateTCPClient();

        // ── State ──
        NetworkRole      m_Role  = NetworkRole::None;
        ConnectionState  m_State = ConnectionState::Disconnected;
        PeerID           m_LocalPeerID = PEER_ID_INVALID;
        bool             m_Initialized = false;

        // ── Transport ──
        ENetTransport    m_Transport;
        TCPSocket        m_TCPSocket;
        uint16_t         m_TCPPort = 0;

        // TCP server: connected client fds
        std::vector<int> m_TCPClientFds;

        // ── Peers ──
        std::unordered_map<PeerID, NetworkPeer> m_Peers;

        // ── Handlers ──
        std::unordered_map<uint16_t, PacketHandler> m_Handlers;

        // ── Callbacks ──
        OnConnectCallback    m_OnPeerConnected;
        OnDisconnectCallback m_OnPeerDisconnected;

        // ── Encryption ──
        NetworkEncryption m_Encryption;
        bool              m_EncryptionEnabled = false;

        // ── NAT Traversal ──
        NATTraversal m_NAT;
    };
}
