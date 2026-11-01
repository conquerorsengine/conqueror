#include "NetTransport.h"
#include "Core/Logging/Log.h"

#include <enet/enet.h>

#include <unordered_map>

namespace Conqueror::Net
{
    namespace
    {
        constexpr uint8_t kChannelReliable = 0;
        constexpr uint8_t kChannelUnreliable = 1;
        constexpr size_t kChannelCount = 2;

        bool EnsureENet()
        {
            static bool s_Initialized = false;
            if (!s_Initialized)
            {
                if (enet_initialize() != 0)
                    return false;
                s_Initialized = true;
            }
            return true;
        }

        class ENetNetTransport final : public INetTransport
        {
        public:
            ~ENetNetTransport() override { Shutdown(); }

            const char* GetName() const override { return "UDP (ENet)"; }

            bool StartServer(uint16_t port, int maxClients, std::string& error) override
            {
                Shutdown();
                if (!EnsureENet())
                {
                    error = "ENet could not be initialized";
                    return false;
                }
                ENetAddress address{};
                address.host = ENET_HOST_ANY;
                address.port = port;
                m_Host = enet_host_create(&address, (size_t)std::max(1, maxClients), kChannelCount, 0, 0);
                if (!m_Host)
                {
                    error = "Port " + std::to_string(port) + " is not available (already in use?)";
                    return false;
                }
                m_Server = true;
                return true;
            }

            bool StartClient(const std::string& addressText, uint16_t port, std::string& error) override
            {
                Shutdown();
                if (!EnsureENet())
                {
                    error = "ENet could not be initialized";
                    return false;
                }
                m_Host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
                if (!m_Host)
                {
                    error = "Could not create the client socket";
                    return false;
                }
                ENetAddress address{};
                if (enet_address_set_host(&address, addressText.c_str()) != 0)
                {
                    error = "Unknown host '" + addressText + "'";
                    Shutdown();
                    return false;
                }
                address.port = port;
                ENetPeer* peer = enet_host_connect(m_Host, &address, kChannelCount, 0);
                if (!peer)
                {
                    error = "Could not start connecting to " + addressText;
                    Shutdown();
                    return false;
                }
                // Yerel ağda hızlı zaman aşımı: 5 sn içinde yanıt yoksa bağlantı düşer
                enet_peer_timeout(peer, 32, 3000, 5000);
                m_Server = false;
                return true;
            }

            void Shutdown() override
            {
                if (!m_Host)
                    return;
                for (auto& [id, peer] : m_Peers)
                    enet_peer_disconnect_now(peer, 0);
                enet_host_flush(m_Host);
                enet_host_destroy(m_Host);
                m_Host = nullptr;
                m_Peers.clear();
            }

            void Poll(std::vector<TransportEvent>& events) override
            {
                if (!m_Host)
                    return;
                ENetEvent ev;
                while (enet_host_service(m_Host, &ev, 0) > 0)
                {
                    switch (ev.type)
                    {
                        case ENET_EVENT_TYPE_CONNECT:
                        {
                            const uint32_t id = m_NextConnection++;
                            ev.peer->data = reinterpret_cast<void*>((uintptr_t)id);
                            m_Peers[id] = ev.peer;
                            if (m_Server)
                                enet_peer_timeout(ev.peer, 32, 5000, 10000);
                            TransportEvent te;
                            te.Kind = TransportEvent::Type::Connect;
                            te.Connection = id;
                            events.push_back(std::move(te));
                            break;
                        }
                        case ENET_EVENT_TYPE_DISCONNECT:
                        {
                            const uint32_t id = (uint32_t)(uintptr_t)ev.peer->data;
                            m_Peers.erase(id);
                            ev.peer->data = nullptr;
                            TransportEvent te;
                            te.Kind = TransportEvent::Type::Disconnect;
                            te.Connection = id;
                            events.push_back(std::move(te));
                            break;
                        }
                        case ENET_EVENT_TYPE_RECEIVE:
                        {
                            const uint32_t id = (uint32_t)(uintptr_t)ev.peer->data;
                            TransportEvent te;
                            te.Kind = TransportEvent::Type::Data;
                            te.Connection = id;
                            te.Payload.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
                            events.push_back(std::move(te));
                            enet_packet_destroy(ev.packet);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            void Send(uint32_t connection, const uint8_t* data, size_t size, bool reliable) override
            {
                auto it = m_Peers.find(connection);
                if (it == m_Peers.end() || !data || size == 0)
                    return;
                // Güvenilmez paketler sıralı (eski durum yenisinin üstüne yazmaz) ve büyükse parçalanabilir
                const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
                ENetPacket* packet = enet_packet_create(data, size, flags);
                if (enet_peer_send(it->second, reliable ? kChannelReliable : kChannelUnreliable, packet) != 0)
                    enet_packet_destroy(packet);
            }

            void Disconnect(uint32_t connection) override
            {
                auto it = m_Peers.find(connection);
                if (it != m_Peers.end())
                    enet_peer_disconnect_later(it->second, 0);
            }

            void Flush() override
            {
                if (m_Host)
                    enet_host_flush(m_Host);
            }

            float GetRTT(uint32_t connection) const override
            {
                auto it = m_Peers.find(connection);
                return it == m_Peers.end() ? 0.0f : (float)it->second->roundTripTime;
            }

        private:
            ENetHost* m_Host = nullptr;
            bool m_Server = false;
            uint32_t m_NextConnection = 1;
            std::unordered_map<uint32_t, ENetPeer*> m_Peers;
        };
    }

    std::unique_ptr<INetTransport> CreateENetTransport()
    {
        return std::make_unique<ENetNetTransport>();
    }
}
