#include "NetTransport.h"
#include "Networking/Steam/SteamPlatform.h"

#include <chrono>
#include <cstdlib>
#include <unordered_map>

namespace Conqueror::Net
{
    namespace
    {
        // Steam P2P mesajları bağlantısızdır: bağlantı olayları burada üretilir.
        // Kanal 0 güvenilir, kanal 1 güvenilmez veri taşır.
        constexpr int kChannelReliable = 0;
        constexpr int kChannelUnreliable = 1;
        constexpr double kTimeoutSeconds = 12.0;

        double Now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        class SteamNetTransport final : public INetTransport
        {
        public:
            SteamNetTransport()
            {
                SteamPlatform::OnSessionRequest() = [this](uint64_t steamId) {
                    // Sunucu herkesi kabul eder (oturum katmanı Hello ile doğrular); istemci yalnızca sunucuyu
                    if (m_Server || steamId == m_ServerSteamId)
                        SteamPlatform::AcceptP2PSession(steamId);
                };
                SteamPlatform::OnSessionFailed() = [this](uint64_t steamId) { m_Failed.push_back(steamId); };
            }

            ~SteamNetTransport() override
            {
                Shutdown();
                SteamPlatform::OnSessionRequest() = nullptr;
                SteamPlatform::OnSessionFailed() = nullptr;
            }

            const char* GetName() const override { return "Steam P2P"; }

            bool StartServer(uint16_t, int maxClients, std::string& error) override
            {
                if (!SteamPlatform::IsAvailable() && !SteamPlatform::Initialize())
                {
                    error = SteamPlatform::GetLastError();
                    return false;
                }
                m_Server = true;
                m_MaxClients = maxClients;
                return true;
            }

            bool StartClient(const std::string& address, uint16_t, std::string& error) override
            {
                if (!SteamPlatform::IsAvailable() && !SteamPlatform::Initialize())
                {
                    error = SteamPlatform::GetLastError();
                    return false;
                }
                m_ServerSteamId = std::strtoull(address.c_str(), nullptr, 10);
                if (!m_ServerSteamId)
                {
                    error = "Steam transport needs the host's SteamID (or join a lobby)";
                    return false;
                }
                m_Server = false;
                SteamPlatform::AcceptP2PSession(m_ServerSteamId);
                // Bağlantısız taşıyıcı: oturum katmanı Hello gönderebilsin diye bağlantı hemen "açılır"
                AddPeer(m_ServerSteamId);
                return true;
            }

            void Shutdown() override
            {
                for (auto& [conn, peer] : m_Peers)
                    SteamPlatform::CloseP2PSession(peer.SteamId);
                m_Peers.clear();
                m_BySteamId.clear();
                m_Server = false;
                m_ServerSteamId = 0;
            }

            void Poll(std::vector<TransportEvent>& events) override
            {
                for (auto& e : m_PendingEvents)
                    events.push_back(std::move(e));
                m_PendingEvents.clear();

                SteamPlatform::RunCallbacks();
                const double now = Now();
                for (int channel : { kChannelReliable, kChannelUnreliable })
                {
                    m_Inbox.clear();
                    SteamPlatform::ReceiveP2P(channel, m_Inbox);
                    for (auto& [sender, data] : m_Inbox)
                    {
                        auto it = m_BySteamId.find(sender);
                        if (it == m_BySteamId.end())
                        {
                            if (!m_Server || (int)m_Peers.size() >= m_MaxClients)
                                continue;
                            const uint32_t conn = AddPeer(sender, &events);
                            it = m_BySteamId.find(sender);
                            (void)conn;
                        }
                        m_Peers[it->second].LastReceive = now;
                        TransportEvent te;
                        te.Kind = TransportEvent::Type::Data;
                        te.Connection = it->second;
                        te.Payload = std::move(data);
                        events.push_back(std::move(te));
                    }
                }
                // Zaman aşımı ve Steam oturum hataları
                std::vector<uint32_t> dropped;
                for (auto& [conn, peer] : m_Peers)
                    if (now - peer.LastReceive > kTimeoutSeconds)
                        dropped.push_back(conn);
                for (uint64_t failed : m_Failed)
                    if (auto it = m_BySteamId.find(failed); it != m_BySteamId.end())
                        dropped.push_back(it->second);
                m_Failed.clear();
                for (uint32_t conn : dropped)
                {
                    auto it = m_Peers.find(conn);
                    if (it == m_Peers.end())
                        continue;
                    SteamPlatform::CloseP2PSession(it->second.SteamId);
                    m_BySteamId.erase(it->second.SteamId);
                    m_Peers.erase(it);
                    TransportEvent te;
                    te.Kind = TransportEvent::Type::Disconnect;
                    te.Connection = conn;
                    events.push_back(std::move(te));
                }
            }

            void Send(uint32_t connection, const uint8_t* data, size_t size, bool reliable) override
            {
                auto it = m_Peers.find(connection);
                if (it == m_Peers.end() || !data || size == 0)
                    return;
                SteamPlatform::SendP2P(it->second.SteamId, data, (uint32_t)size, reliable, reliable ? kChannelReliable : kChannelUnreliable);
            }

            void Disconnect(uint32_t connection) override
            {
                auto it = m_Peers.find(connection);
                if (it == m_Peers.end())
                    return;
                SteamPlatform::CloseP2PSession(it->second.SteamId);
                m_BySteamId.erase(it->second.SteamId);
                m_Peers.erase(it);
                TransportEvent te;
                te.Kind = TransportEvent::Type::Disconnect;
                te.Connection = connection;
                m_PendingEvents.push_back(std::move(te));
            }

            float GetRTT(uint32_t connection) const override
            {
                auto it = m_Peers.find(connection);
                return it == m_Peers.end() ? 0.0f : SteamPlatform::GetP2PPing(it->second.SteamId);
            }

        private:
            struct Peer
            {
                uint64_t SteamId = 0;
                double LastReceive = 0.0;
            };

            uint32_t AddPeer(uint64_t steamId, std::vector<TransportEvent>* events = nullptr)
            {
                const uint32_t conn = m_NextConnection++;
                m_Peers[conn] = Peer{ steamId, Now() };
                m_BySteamId[steamId] = conn;
                TransportEvent te;
                te.Kind = TransportEvent::Type::Connect;
                te.Connection = conn;
                if (events)
                    events->push_back(std::move(te));
                else
                    m_PendingEvents.push_back(std::move(te));
                return conn;
            }

            bool m_Server = false;
            int m_MaxClients = 16;
            uint64_t m_ServerSteamId = 0;
            uint32_t m_NextConnection = 1;
            std::unordered_map<uint32_t, Peer> m_Peers;
            std::unordered_map<uint64_t, uint32_t> m_BySteamId;
            std::vector<TransportEvent> m_PendingEvents;
            std::vector<uint64_t> m_Failed;
            std::vector<std::pair<uint64_t, std::vector<uint8_t>>> m_Inbox;
        };
    }

    std::unique_ptr<INetTransport> CreateSteamTransport(std::string& error)
    {
        if (!SteamPlatform::IsAvailable() && !SteamPlatform::Initialize())
        {
            error = SteamPlatform::GetLastError();
            return nullptr;
        }
        return std::make_unique<SteamNetTransport>();
    }
}
