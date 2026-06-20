#include "ENetTransport.h"
#include "Core/Logging/Log.h"

#include <enet/enet.h>

namespace Conqueror
{
    bool ENetTransport::s_ENetInitialized = false;

    ENetTransport::ENetTransport() = default;

    ENetTransport::~ENetTransport()
    {
        Shutdown();
    }

    bool ENetTransport::InitializeENet()
    {
        if (s_ENetInitialized) return true;
        if (enet_initialize() != 0)
        {
            CQ_CORE_ERROR("[Network] ENet initialization failed!");
            return false;
        }
        s_ENetInitialized = true;
        CQ_CORE_INFO("[Network] ENet initialized.");
        return true;
    }

    void ENetTransport::ShutdownENet()
    {
        if (!s_ENetInitialized) return;
        enet_deinitialize();
        s_ENetInitialized = false;
        CQ_CORE_INFO("[Network] ENet shutdown.");
    }

    bool ENetTransport::CreateServer(uint16_t port, int maxClients)
    {
        if (!s_ENetInitialized && !InitializeENet()) return false;

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = port;

        m_Host = enet_host_create(&address, maxClients, CQ_MAX_CHANNELS, 0, 0);
        if (!m_Host)
        {
            CQ_CORE_ERROR("[Network] Failed to create server on port {0}!", port);
            return false;
        }

        m_Role = NetworkRole::Server;
        CQ_CORE_INFO("[Network] Server started on port {0} (max clients: {1})", port, maxClients);
        return true;
    }

    bool ENetTransport::CreateClient()
    {
        if (!s_ENetInitialized && !InitializeENet()) return false;

        m_Host = enet_host_create(nullptr, 1, CQ_MAX_CHANNELS, 0, 0);
        if (!m_Host)
        {
            CQ_CORE_ERROR("[Network] Failed to create client host!");
            return false;
        }

        m_Role = NetworkRole::Client;
        CQ_CORE_INFO("[Network] Client host created.");
        return true;
    }

    bool ENetTransport::Connect(const std::string& host, uint16_t port)
    {
        if (!m_Host || m_Role != NetworkRole::Client)
        {
            CQ_CORE_ERROR("[Network] Must create client before connecting!");
            return false;
        }

        ENetAddress address;
        enet_address_set_host(&address, host.c_str());
        address.port = port;

        ENetPeer* peer = enet_host_connect(m_Host, &address, CQ_MAX_CHANNELS, 0);
        if (!peer)
        {
            CQ_CORE_ERROR("[Network] Failed to initiate connection to {0}:{1}", host, port);
            return false;
        }

        CQ_CORE_INFO("[Network] Connecting to {0}:{1}...", host, port);
        return true;
    }

    void ENetTransport::Disconnect(PeerID peerID)
    {
        ENetPeer* peer = FindPeer(peerID);
        if (peer)
        {
            enet_peer_disconnect(peer, 0);
        }
    }

    void ENetTransport::Shutdown()
    {
        if (m_Host)
        {
            // Disconnect all peers gracefully
            for (auto& entry : m_Peers)
            {
                if (entry.Peer)
                    enet_peer_disconnect_now(entry.Peer, 0);
            }
            m_Peers.clear();

            enet_host_destroy(m_Host);
            m_Host = nullptr;
            CQ_CORE_INFO("[Network] Host destroyed.");
        }
        m_Role = NetworkRole::None;
    }

    bool ENetTransport::PollEvents()
    {
        if (!m_Host) return false;

        ENetEvent event;
        bool hadEvent = false;

        while (enet_host_service(m_Host, &event, 0) > 0)
        {
            hadEvent = true;

            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    PeerID id = RegisterPeer(event.peer);
                    CQ_CORE_INFO("[Network] Peer connected: ID={0}", id);
                    if (m_OnConnect) m_OnConnect(id);
                    break;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    PeerID id = GetPeerID(event.peer);
                    CQ_CORE_INFO("[Network] Peer disconnected: ID={0}", id);
                    if (m_OnDisconnect) m_OnDisconnect(id);
                    UnregisterPeer(event.peer);
                    break;
                }

                case ENET_EVENT_TYPE_RECEIVE:
                {
                    PeerID id = GetPeerID(event.peer);
                    if (m_OnData)
                    {
                        m_OnData(id, event.packet->data, event.packet->dataLength);
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }

                default:
                    break;
            }
        }

        return hadEvent;
    }

    void ENetTransport::Send(PeerID peerID, const uint8_t* data, size_t size, NetworkChannel channel)
    {
        ENetPeer* peer = FindPeer(peerID);
        if (!peer)
        {
            CQ_CORE_WARN("[Network] Cannot send to peer {0}: not found", peerID);
            return;
        }

        enet_uint32 flags = (channel == NetworkChannel::Unreliable) ? 0 : ENET_PACKET_FLAG_RELIABLE;
        ENetPacket* packet = enet_packet_create(data, size, flags);
        enet_peer_send(peer, static_cast<enet_uint8>(channel), packet);
    }

    void ENetTransport::Broadcast(const uint8_t* data, size_t size, NetworkChannel channel)
    {
        if (!m_Host) return;

        enet_uint32 flags = (channel == NetworkChannel::Unreliable) ? 0 : ENET_PACKET_FLAG_RELIABLE;
        ENetPacket* packet = enet_packet_create(data, size, flags);
        enet_host_broadcast(m_Host, static_cast<enet_uint8>(channel), packet);
    }

    // ── Internal Peer Management ──

    ENetPeer* ENetTransport::FindPeer(PeerID id) const
    {
        for (const auto& entry : m_Peers)
        {
            if (entry.ID == id) return entry.Peer;
        }
        return nullptr;
    }

    PeerID ENetTransport::RegisterPeer(ENetPeer* peer)
    {
        PeerID id = m_NextPeerID++;
        peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
        m_Peers.push_back({peer, id});
        return id;
    }

    void ENetTransport::UnregisterPeer(ENetPeer* peer)
    {
        for (auto it = m_Peers.begin(); it != m_Peers.end(); ++it)
        {
            if (it->Peer == peer)
            {
                m_Peers.erase(it);
                return;
            }
        }
    }

    PeerID ENetTransport::GetPeerID(ENetPeer* peer) const
    {
        if (!peer || !peer->data) return PEER_ID_INVALID;
        return static_cast<PeerID>(reinterpret_cast<uintptr_t>(peer->data));
    }
}
