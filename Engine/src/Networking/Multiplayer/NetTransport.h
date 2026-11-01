#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror::Net
{
    struct TransportEvent
    {
        enum class Type : uint8_t { Connect, Disconnect, Data };
        Type Kind = Type::Data;
        uint32_t Connection = 0;
        std::vector<uint8_t> Payload;
    };

    // Oturumun kullandığı taşıyıcı. Sunucu tarafında her istemci bir bağlantı kimliği alır (1..);
    // istemci tarafında sunucu bağlantısı tektir.
    class INetTransport
    {
    public:
        virtual ~INetTransport() = default;

        virtual const char* GetName() const = 0;
        virtual bool StartServer(uint16_t port, int maxClients, std::string& error) = 0;
        // address: UDP için host adı / IP; Steam için 64-bit SteamID (ondalık)
        virtual bool StartClient(const std::string& address, uint16_t port, std::string& error) = 0;
        virtual void Shutdown() = 0;

        virtual void Poll(std::vector<TransportEvent>& events) = 0;
        virtual void Send(uint32_t connection, const uint8_t* data, size_t size, bool reliable) = 0;
        virtual void Disconnect(uint32_t connection) = 0;
        virtual void Flush() {}
        virtual float GetRTT(uint32_t connection) const = 0;    // ms
    };

    std::unique_ptr<INetTransport> CreateENetTransport();
    // Steam P2P (ISteamNetworkingMessages). Steam istemcisi + steam_api kitaplığı yoksa nullptr ve hata.
    std::unique_ptr<INetTransport> CreateSteamTransport(std::string& error);
}
