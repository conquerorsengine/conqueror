#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace Conqueror
{
    // ── Network Constants ──
    constexpr uint16_t CQ_DEFAULT_PORT = 7777;
    constexpr int      CQ_MAX_CLIENTS = 32;
    constexpr int      CQ_MAX_CHANNELS = 4;
    constexpr uint32_t CQ_PROTOCOL_ID = 0x434F4E51; // "CONQ"
    constexpr int      CQ_PACKET_MAX_SIZE = 1024 * 64; // 64KB

    // ── Network Channels ──
    enum class NetworkChannel : uint8_t
    {
        Reliable    = 0,  // Guaranteed delivery, ordered
        Unreliable  = 1,  // Fast, no guarantee (position updates)
        ReliableUnordered = 2,  // Guaranteed but unordered (events)
        StateSync   = 3   // State synchronization channel
    };

    // ── Network Role ──
    enum class NetworkRole : uint8_t
    {
        None = 0,
        Server,
        Client
    };

    // ── Connection State ──
    enum class ConnectionState : uint8_t
    {
        Disconnected = 0,
        Connecting,
        Connected,
        Disconnecting
    };

    // ── Packet / Message Types ──
    enum class MessageType : uint16_t
    {
        // System
        Handshake       = 0,
        HandshakeAck    = 1,
        Ping            = 2,
        Pong            = 3,
        Disconnect      = 4,
        Kick            = 5,

        // Replication
        EntitySpawn     = 100,
        EntityDestroy   = 101,
        EntityState     = 102,
        EntityRPC       = 103,

        // Scene
        SceneLoad       = 200,
        SceneReady      = 201,

        // Custom (user messages start here)
        UserMessage     = 1000
    };

    // ── Peer ID ──
    using PeerID = uint32_t;
    constexpr PeerID PEER_ID_INVALID = 0;
    constexpr PeerID PEER_ID_SERVER  = 1;

    // ── Callbacks ──
    using OnConnectCallback    = std::function<void(PeerID)>;
    using OnDisconnectCallback = std::function<void(PeerID)>;
    using OnDataCallback       = std::function<void(PeerID, const uint8_t*, size_t)>;
}
