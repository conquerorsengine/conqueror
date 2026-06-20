#include "RPCSystem.h"
#include "Networking/NetworkManager.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    RPCSystem& RPCSystem::Get()
    {
        static RPCSystem instance;
        return instance;
    }

    void RPCSystem::Register(const std::string& name, RPCHandler handler)
    {
        m_Handlers[name] = std::move(handler);
        CQ_CORE_INFO("[RPC] Registered: {0}", name);
    }

    void RPCSystem::Unregister(const std::string& name)
    {
        m_Handlers.erase(name);
    }

    void RPCSystem::CallServer(const std::string& name, const Packet& args)
    {
        auto& net = NetworkManager::Get();
        if (net.IsServer())
        {
            // We ARE the server, execute locally
            Packet mutableArgs = args;
            auto it = m_Handlers.find(name);
            if (it != m_Handlers.end())
                it->second(PEER_ID_SERVER, mutableArgs);
        }
        else
        {
            // Send to server
            SendRPC(PEER_ID_SERVER, RPCTarget::Server, name, args);
        }
    }

    void RPCSystem::CallAllClients(const std::string& name, const Packet& args)
    {
        auto& net = NetworkManager::Get();
        if (!net.IsServer())
        {
            CQ_CORE_WARN("[RPC] Only server can call AllClients!");
            return;
        }

        // Build RPC packet and broadcast
        Packet rpcPkt(MessageType::EntityRPC);
        rpcPkt.WriteUint8(static_cast<uint8_t>(RPCTarget::AllClients));
        rpcPkt.WriteUint32(0); // No specific peer
        rpcPkt.WriteString(name);

        // Append the args body
        auto argsBody = args.GetBody();
        rpcPkt.WriteUint32(static_cast<uint32_t>(argsBody.size()));
        if (!argsBody.empty())
            rpcPkt.WriteBytes(argsBody.data(), argsBody.size());

        net.BroadcastPacket(rpcPkt, NetworkChannel::Reliable);
    }

    void RPCSystem::CallClient(PeerID peerID, const std::string& name, const Packet& args)
    {
        auto& net = NetworkManager::Get();
        if (!net.IsServer())
        {
            CQ_CORE_WARN("[RPC] Only server can call specific clients!");
            return;
        }

        SendRPC(peerID, RPCTarget::SpecificClient, name, args);
    }

    void RPCSystem::CallEveryone(const std::string& name, const Packet& args)
    {
        auto& net = NetworkManager::Get();

        if (net.IsServer())
        {
            // Execute locally on server
            Packet mutableArgs = args;
            auto it = m_Handlers.find(name);
            if (it != m_Handlers.end())
                it->second(PEER_ID_SERVER, mutableArgs);

            // Broadcast to all clients
            Packet rpcPkt(MessageType::EntityRPC);
            rpcPkt.WriteUint8(static_cast<uint8_t>(RPCTarget::Everyone));
            rpcPkt.WriteUint32(0);
            rpcPkt.WriteString(name);

            auto argsBody = args.GetBody();
            rpcPkt.WriteUint32(static_cast<uint32_t>(argsBody.size()));
            if (!argsBody.empty())
                rpcPkt.WriteBytes(argsBody.data(), argsBody.size());

            net.BroadcastPacket(rpcPkt, NetworkChannel::Reliable);
        }
        else
        {
            // Client: send to server, server will relay
            SendRPC(PEER_ID_SERVER, RPCTarget::Everyone, name, args);
        }
    }

    void RPCSystem::ProcessRPC(PeerID senderID, Packet& pkt)
    {
        uint8_t targetRaw = pkt.ReadUint8();
        uint32_t targetPeerID = pkt.ReadUint32();
        std::string name = pkt.ReadString();
        uint32_t argsSize = pkt.ReadUint32();

        // Read args body
        Packet argsPkt;
        if (argsSize > 0)
        {
            std::vector<uint8_t> argsData(argsSize);
            pkt.ReadBytes(argsData.data(), argsSize);
            argsPkt = Packet(argsData.data(), argsSize);
        }

        RPCTarget target = static_cast<RPCTarget>(targetRaw);
        auto& net = NetworkManager::Get();

        // Server receiving an RPC
        if (net.IsServer())
        {
            if (target == RPCTarget::Server || target == RPCTarget::Everyone)
            {
                auto it = m_Handlers.find(name);
                if (it != m_Handlers.end())
                    it->second(senderID, argsPkt);
            }

            // Relay to clients if needed
            if (target == RPCTarget::AllClients || target == RPCTarget::Everyone)
            {
                Packet relayPkt(MessageType::EntityRPC);
                relayPkt.WriteUint8(targetRaw);
                relayPkt.WriteUint32(targetPeerID);
                relayPkt.WriteString(name);
                relayPkt.WriteUint32(argsSize);
                if (argsSize > 0)
                    relayPkt.WriteBytes(argsPkt.GetBody().data(), argsSize);

                net.BroadcastPacket(relayPkt, NetworkChannel::Reliable);
            }
            else if (target == RPCTarget::SpecificClient && targetPeerID != 0)
            {
                Packet relayPkt(MessageType::EntityRPC);
                relayPkt.WriteUint8(targetRaw);
                relayPkt.WriteUint32(targetPeerID);
                relayPkt.WriteString(name);
                relayPkt.WriteUint32(argsSize);
                if (argsSize > 0)
                    relayPkt.WriteBytes(argsPkt.GetBody().data(), argsSize);

                net.SendPacket(static_cast<PeerID>(targetPeerID), relayPkt, NetworkChannel::Reliable);
            }
        }
        else
        {
            // Client receiving an RPC — just execute
            auto it = m_Handlers.find(name);
            if (it != m_Handlers.end())
                it->second(senderID, argsPkt);
            else
                CQ_CORE_WARN("[RPC] No handler for '{0}'", name);
        }
    }

    void RPCSystem::SendRPC(PeerID targetPeer, RPCTarget target, const std::string& name, const Packet& args)
    {
        Packet rpcPkt(MessageType::EntityRPC);
        rpcPkt.WriteUint8(static_cast<uint8_t>(target));
        rpcPkt.WriteUint32(targetPeer);
        rpcPkt.WriteString(name);

        auto argsBody = args.GetBody();
        rpcPkt.WriteUint32(static_cast<uint32_t>(argsBody.size()));
        if (!argsBody.empty())
            rpcPkt.WriteBytes(argsBody.data(), argsBody.size());

        NetworkManager::Get().SendPacket(targetPeer, rpcPkt, NetworkChannel::Reliable);
    }
}
