#pragma once

#include "NetworkTypes.h"
#include <string>

namespace Conqueror
{
    /// Information about a connected peer
    struct NetworkPeer
    {
        PeerID          ID = PEER_ID_INVALID;
        ConnectionState State = ConnectionState::Disconnected;
        std::string     Address;
        uint16_t        Port = 0;
        uint32_t        RTT = 0;        // Round-trip time in ms
        uint64_t        BytesSent = 0;
        uint64_t        BytesReceived = 0;
    };
}
