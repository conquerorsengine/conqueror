#include "NetworkManager.h"
#include "RPC/RPCSystem.h"
#include "Replication/Replication.h"
#include "Core/Logging/Log.h"

#include <algorithm>

namespace Conqueror
{
    NetworkManager& NetworkManager::Get()
    {
        static NetworkManager instance;
        return instance;
    }

    void NetworkManager::Initialize()
    {
        if (m_Initialized) return;

        ENetTransport::InitializeENet();
        m_Initialized = true;
        CQ_CORE_INFO("[Network] NetworkManager initialized.");
    }

    void NetworkManager::Shutdown()
    {
        if (m_EncryptionEnabled)
            m_Encryption.Shutdown();

        StopServer();
        DisconnectFromServer();
        ENetTransport::ShutdownENet();
        m_Initialized = false;
        CQ_CORE_INFO("[Network] NetworkManager shutdown.");
    }

    void NetworkManager::Update()
    {
        if (!m_Initialized) return;

        m_Transport.PollEvents();

        if (m_Role == NetworkRole::Server && m_TCPPort > 0)
            UpdateTCPServer();
        else if (m_Role == NetworkRole::Client && m_TCPPort > 0)
            UpdateTCPClient();

        // Replication sync (server only)
        if (m_Role == NetworkRole::Server)
            ReplicationManager::Get().SyncAll();
    }

    // ═══════════════════════════════════
    //  Server
    // ═══════════════════════════════════

    bool NetworkManager::StartServer(uint16_t udpPort, uint16_t tcpPort)
    {
        if (m_Role != NetworkRole::None)
        {
            CQ_CORE_ERROR("[Network] Already running as {0}!", m_Role == NetworkRole::Server ? "server" : "client");
            return false;
        }

        Initialize();

        m_Transport.SetOnConnect([this](PeerID id)    { OnENetConnect(id); });
        m_Transport.SetOnDisconnect([this](PeerID id) { OnENetDisconnect(id); });
        m_Transport.SetOnData([this](PeerID id, const uint8_t* data, size_t size) { OnENetData(id, data, size); });

        if (!m_Transport.CreateServer(udpPort))
            return false;

        m_Role = NetworkRole::Server;
        m_State = ConnectionState::Connected;
        m_LocalPeerID = PEER_ID_SERVER;

        if (tcpPort > 0)
        {
            m_TCPPort = tcpPort;
            if (m_TCPSocket.Listen(tcpPort))
            {
                m_TCPSocket.SetNonBlocking(true);
                CQ_CORE_INFO("[Network] TCP server listening on port {0}", tcpPort);
            }
        }

        CQ_CORE_INFO("[Network] Server started. UDP:{0} TCP:{1}", udpPort, tcpPort);
        return true;
    }

    void NetworkManager::StopServer()
    {
        if (m_Role != NetworkRole::Server) return;

        m_NAT.ClosePort(7777, "UDP");

        for (int fd : m_TCPClientFds) close(fd);
        m_TCPClientFds.clear();
        m_TCPSocket.Close();

        m_Transport.Shutdown();
        m_Peers.clear();
        m_Role = NetworkRole::None;
        m_State = ConnectionState::Disconnected;
        m_LocalPeerID = PEER_ID_INVALID;
        m_TCPPort = 0;

        CQ_CORE_INFO("[Network] Server stopped.");
    }

    // ═══════════════════════════════════
    //  Client
    // ═══════════════════════════════════

    bool NetworkManager::ConnectToServer(const std::string& host, uint16_t udpPort, uint16_t tcpPort)
    {
        if (m_Role != NetworkRole::None)
        {
            CQ_CORE_ERROR("[Network] Already running!");
            return false;
        }

        Initialize();

        m_Transport.SetOnConnect([this](PeerID id)    { OnENetConnect(id); });
        m_Transport.SetOnDisconnect([this](PeerID id) { OnENetDisconnect(id); });
        m_Transport.SetOnData([this](PeerID id, const uint8_t* data, size_t size) { OnENetData(id, data, size); });

        if (!m_Transport.CreateClient()) return false;

        if (!m_Transport.Connect(host, udpPort))
        {
            m_Transport.Shutdown();
            return false;
        }

        m_Role = NetworkRole::Client;
        m_State = ConnectionState::Connecting;

        if (tcpPort > 0)
        {
            m_TCPPort = tcpPort;
            if (m_TCPSocket.Connect(host, tcpPort))
                m_TCPSocket.SetNonBlocking(true);
        }

        return true;
    }

    void NetworkManager::DisconnectFromServer()
    {
        if (m_Role != NetworkRole::Client) return;

        m_TCPSocket.Close();
        m_Transport.Shutdown();
        m_Peers.clear();
        m_Role = NetworkRole::None;
        m_State = ConnectionState::Disconnected;
        m_LocalPeerID = PEER_ID_INVALID;
        m_TCPPort = 0;

        CQ_CORE_INFO("[Network] Disconnected from server.");
    }

    // ═══════════════════════════════════
    //  Sending
    // ═══════════════════════════════════

    void NetworkManager::SendPacket(PeerID peerID, const Packet& packet, NetworkChannel channel)
    {
        auto serialized = packet.Serialize();
        m_Transport.Send(peerID, serialized.data(), serialized.size(), channel);
    }

    void NetworkManager::BroadcastPacket(const Packet& packet, NetworkChannel channel)
    {
        if (m_Role != NetworkRole::Server)
        {
            CQ_CORE_WARN("[Network] Only server can broadcast!");
            return;
        }
        auto serialized = packet.Serialize();
        m_Transport.Broadcast(serialized.data(), serialized.size(), channel);
    }

    void NetworkManager::SendTCP(int socketFd, const Packet& packet)
    {
        auto serialized = packet.Serialize();
        uint32_t len = static_cast<uint32_t>(serialized.size());
        m_TCPSocket.Send(reinterpret_cast<const uint8_t*>(&len), 4, socketFd);
        m_TCPSocket.Send(serialized.data(), serialized.size(), socketFd);
    }

    void NetworkManager::SendEncrypted(PeerID peerID, const Packet& packet, NetworkChannel channel)
    {
        if (!m_EncryptionEnabled)
        {
            SendPacket(peerID, packet, channel);
            return;
        }

        auto serialized = packet.Serialize();
        auto encrypted = m_Encryption.Encrypt(serialized.data(), serialized.size());
        if (encrypted.empty())
        {
            CQ_CORE_ERROR("[Network] Encryption failed, sending unencrypted!");
            SendPacket(peerID, packet, channel);
            return;
        }
        m_Transport.Send(peerID, encrypted.data(), encrypted.size(), channel);
    }

    // ═══════════════════════════════════
    //  Encryption
    // ═══════════════════════════════════

    bool NetworkManager::EnableEncryption()
    {
        if (!m_Encryption.Initialize()) return false;
        if (!m_Encryption.GenerateSessionKey()) return false;
        if (!m_Encryption.GenerateRSAKeyPair()) return false;

        m_EncryptionEnabled = true;
        CQ_CORE_INFO("[Network] Encryption enabled (AES-256-GCM + RSA-2048).");
        return true;
    }

    // ═══════════════════════════════════
    //  NAT Traversal
    // ═══════════════════════════════════

    bool NetworkManager::OpenNATPort(uint16_t port, const std::string& protocol)
    {
        if (!m_NAT.IsAvailable())
        {
            if (!m_NAT.Discover())
            {
                CQ_CORE_WARN("[Network] UPnP not available on this network.");
                return false;
            }
        }
        return m_NAT.OpenPort(port, port, protocol);
    }

    bool NetworkManager::CloseNATPort(uint16_t port, const std::string& protocol)
    {
        return m_NAT.ClosePort(port, protocol);
    }

    std::string NetworkManager::GetExternalIP() const
    {
        return m_NAT.GetExternalIP();
    }

    // ═══════════════════════════════════
    //  Message Handlers
    // ═══════════════════════════════════

    void NetworkManager::RegisterHandler(MessageType type, PacketHandler handler)
    {
        m_Handlers[static_cast<uint16_t>(type)] = std::move(handler);
    }

    void NetworkManager::UnregisterHandler(MessageType type)
    {
        m_Handlers.erase(static_cast<uint16_t>(type));
    }

    // ═══════════════════════════════════
    //  ENet Callbacks
    // ═══════════════════════════════════

    void NetworkManager::OnENetConnect(PeerID peerID)
    {
        NetworkPeer peer;
        peer.ID = peerID;
        peer.State = ConnectionState::Connected;
        m_Peers[peerID] = peer;

        if (m_Role == NetworkRole::Client)
        {
            m_State = ConnectionState::Connected;
            m_LocalPeerID = peerID;

            Packet handshake(MessageType::Handshake);
            handshake.WriteUint32(CQ_PROTOCOL_ID);
            handshake.WriteString("ConquerorClient");
            SendPacket(peerID, handshake);
        }

        if (m_OnPeerConnected) m_OnPeerConnected(peerID);
    }

    void NetworkManager::OnENetDisconnect(PeerID peerID)
    {
        m_Peers.erase(peerID);

        if (m_Role == NetworkRole::Client)
            m_State = ConnectionState::Disconnected;

        if (m_OnPeerDisconnected) m_OnPeerDisconnected(peerID);
    }

    void NetworkManager::OnENetData(PeerID peerID, const uint8_t* data, size_t size)
    {
        Packet packet = Packet::Deserialize(data, size);
        ProcessPacket(peerID, packet);
    }

    void NetworkManager::ProcessPacket(PeerID senderID, Packet& packet)
    {
        MessageType type = packet.GetType();

        switch (type)
        {
            case MessageType::Handshake:
                HandleHandshake(senderID, packet);
                return;
            case MessageType::Ping:
                HandlePing(senderID, packet);
                return;
            case MessageType::EntityRPC:
                RPCSystem::Get().ProcessRPC(senderID, packet);
                return;
            case MessageType::EntityState:
                ReplicationManager::Get().ApplyStateUpdate(packet);
                return;
            default:
                break;
        }

        auto it = m_Handlers.find(static_cast<uint16_t>(type));
        if (it != m_Handlers.end())
            it->second(senderID, packet);
        else
            CQ_CORE_WARN("[Network] Unhandled message type: {0}", static_cast<int>(type));
    }

    void NetworkManager::HandleHandshake(PeerID senderID, Packet& packet)
    {
        uint32_t protocolID = packet.ReadUint32();
        std::string clientName = packet.ReadString();

        if (protocolID != CQ_PROTOCOL_ID)
        {
            CQ_CORE_WARN("[Network] Invalid handshake from peer {0}", senderID);
            m_Transport.Disconnect(senderID);
            return;
        }

        CQ_CORE_INFO("[Network] Handshake from '{0}' (peer {1})", clientName, senderID);

        if (m_Role == NetworkRole::Server)
        {
            Packet ack(MessageType::HandshakeAck);
            ack.WriteUint32(CQ_PROTOCOL_ID);
            ack.WriteUint32(senderID);
            ack.WriteString("ConquerorServer");
            SendPacket(senderID, ack);
        }
    }

    void NetworkManager::HandlePing(PeerID senderID, Packet& packet)
    {
        uint64_t timestamp = packet.ReadUint64();

        Packet pong(MessageType::Pong);
        pong.WriteUint64(timestamp);
        SendPacket(senderID, pong);
    }

    // ═══════════════════════════════════
    //  TCP Polling
    // ═══════════════════════════════════

    void NetworkManager::UpdateTCPServer()
    {
        int clientFd = m_TCPSocket.Accept();
        if (clientFd >= 0)
        {
            m_TCPSocket.SetNonBlocking(clientFd, true);
            m_TCPClientFds.push_back(clientFd);
            CQ_CORE_INFO("[Network] TCP client accepted (fd={0})", clientFd);
        }

        uint8_t buffer[4096];
        for (auto it = m_TCPClientFds.begin(); it != m_TCPClientFds.end();)
        {
            int received = m_TCPSocket.Receive(buffer, sizeof(buffer), *it);
            if (received > 0)
            {
                Packet packet = Packet::Deserialize(buffer, received);
                ProcessPacket(PEER_ID_INVALID, packet);
                ++it;
            }
            else if (received == 0)
            {
                close(*it);
                it = m_TCPClientFds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void NetworkManager::UpdateTCPClient()
    {
        if (!m_TCPSocket.IsValid()) return;

        uint8_t buffer[4096];
        int received = m_TCPSocket.Receive(buffer, sizeof(buffer));
        if (received > 0)
        {
            Packet packet = Packet::Deserialize(buffer, received);
            ProcessPacket(PEER_ID_SERVER, packet);
        }
    }
}
