#include "Replication.h"
#include "Networking/NetworkManager.h"
#include "Core/Logging/Log.h"
#include <cstring>

namespace Conqueror
{
    // ═══════════════════════════════════
    //  ReplicatedProperty
    // ═══════════════════════════════════

    void ReplicatedProperty::Serialize(Packet& pkt) const
    {
        pkt.WriteString(Name);
        pkt.WriteUint8(static_cast<uint8_t>(PropertyType));

        switch (PropertyType)
        {
            case Type::Float:  pkt.WriteFloat(FloatVal); break;
            case Type::Int:    pkt.WriteInt32(IntVal); break;
            case Type::Bool:   pkt.WriteBool(BoolVal); break;
            case Type::Vec3:   pkt.WriteVec3(Vec3Val[0], Vec3Val[1], Vec3Val[2]); break;
            case Type::String: pkt.WriteString(StringVal); break;
        }
    }

    void ReplicatedProperty::Deserialize(Packet& pkt)
    {
        Name = pkt.ReadString();
        PropertyType = static_cast<Type>(pkt.ReadUint8());

        switch (PropertyType)
        {
            case Type::Float:  FloatVal = pkt.ReadFloat(); break;
            case Type::Int:    IntVal = pkt.ReadInt32(); break;
            case Type::Bool:   BoolVal = pkt.ReadBool(); break;
            case Type::Vec3:   pkt.ReadVec3(Vec3Val[0], Vec3Val[1], Vec3Val[2]); break;
            case Type::String: StringVal = pkt.ReadString(); break;
        }
    }

    // ═══════════════════════════════════
    //  NetworkObject
    // ═══════════════════════════════════

    void NetworkObject::RegisterFloat(const std::string& name, float* valuePtr)
    {
        PropertyBinding b;
        b.Prop.Name = name;
        b.Prop.PropertyType = ReplicatedProperty::Type::Float;
        b.ValuePtr = valuePtr;
        m_Bindings.push_back(b);
    }

    void NetworkObject::RegisterInt(const std::string& name, int* valuePtr)
    {
        PropertyBinding b;
        b.Prop.Name = name;
        b.Prop.PropertyType = ReplicatedProperty::Type::Int;
        b.ValuePtr = valuePtr;
        m_Bindings.push_back(b);
    }

    void NetworkObject::RegisterBool(const std::string& name, bool* valuePtr)
    {
        PropertyBinding b;
        b.Prop.Name = name;
        b.Prop.PropertyType = ReplicatedProperty::Type::Bool;
        b.ValuePtr = valuePtr;
        m_Bindings.push_back(b);
    }

    void NetworkObject::RegisterVec3(const std::string& name, float* xyz)
    {
        PropertyBinding b;
        b.Prop.Name = name;
        b.Prop.PropertyType = ReplicatedProperty::Type::Vec3;
        b.ValuePtr = xyz;
        m_Bindings.push_back(b);
    }

    void NetworkObject::RegisterString(const std::string& name, std::string* valuePtr)
    {
        PropertyBinding b;
        b.Prop.Name = name;
        b.Prop.PropertyType = ReplicatedProperty::Type::String;
        b.ValuePtr = valuePtr;
        m_Bindings.push_back(b);
    }

    Packet NetworkObject::SerializeState()
    {
        Packet pkt(MessageType::EntityState);
        pkt.WriteUint32(m_ID);

        // Count dirty properties
        uint16_t dirtyCount = 0;
        for (auto& b : m_Bindings)
        {
            // Read current value from pointer and check if changed
            switch (b.Prop.PropertyType)
            {
                case ReplicatedProperty::Type::Float:
                {
                    float cur = *static_cast<float*>(b.ValuePtr);
                    if (cur != b.Prop.FloatVal) { b.Prop.FloatVal = cur; b.Prop.IsDirty = true; }
                    break;
                }
                case ReplicatedProperty::Type::Int:
                {
                    int cur = *static_cast<int*>(b.ValuePtr);
                    if (cur != b.Prop.IntVal) { b.Prop.IntVal = cur; b.Prop.IsDirty = true; }
                    break;
                }
                case ReplicatedProperty::Type::Bool:
                {
                    bool cur = *static_cast<bool*>(b.ValuePtr);
                    if (cur != b.Prop.BoolVal) { b.Prop.BoolVal = cur; b.Prop.IsDirty = true; }
                    break;
                }
                case ReplicatedProperty::Type::Vec3:
                {
                    float* cur = static_cast<float*>(b.ValuePtr);
                    if (cur[0] != b.Prop.Vec3Val[0] || cur[1] != b.Prop.Vec3Val[1] || cur[2] != b.Prop.Vec3Val[2])
                    {
                        b.Prop.Vec3Val[0] = cur[0]; b.Prop.Vec3Val[1] = cur[1]; b.Prop.Vec3Val[2] = cur[2];
                        b.Prop.IsDirty = true;
                    }
                    break;
                }
                case ReplicatedProperty::Type::String:
                {
                    std::string* cur = static_cast<std::string*>(b.ValuePtr);
                    if (*cur != b.Prop.StringVal) { b.Prop.StringVal = *cur; b.Prop.IsDirty = true; }
                    break;
                }
            }
            if (b.Prop.IsDirty) dirtyCount++;
        }

        pkt.WriteUint16(dirtyCount);
        for (auto& b : m_Bindings)
        {
            if (b.Prop.IsDirty)
                b.Prop.Serialize(pkt);
        }

        return pkt;
    }

    void NetworkObject::DeserializeState(Packet& pkt)
    {
        /* ID already read by caller */
        uint16_t propCount = pkt.ReadUint16();

        for (uint16_t i = 0; i < propCount; i++)
        {
            ReplicatedProperty incoming;
            incoming.Deserialize(pkt);

            // Find matching binding and apply
            for (auto& b : m_Bindings)
            {
                if (b.Prop.Name == incoming.Name)
                {
                    b.Prop = incoming;
                    // Write back to actual value pointer
                    switch (b.Prop.PropertyType)
                    {
                        case ReplicatedProperty::Type::Float:
                            *static_cast<float*>(b.ValuePtr) = b.Prop.FloatVal; break;
                        case ReplicatedProperty::Type::Int:
                            *static_cast<int*>(b.ValuePtr) = b.Prop.IntVal; break;
                        case ReplicatedProperty::Type::Bool:
                            *static_cast<bool*>(b.ValuePtr) = b.Prop.BoolVal; break;
                        case ReplicatedProperty::Type::Vec3:
                        {
                            float* dst = static_cast<float*>(b.ValuePtr);
                            dst[0] = b.Prop.Vec3Val[0]; dst[1] = b.Prop.Vec3Val[1]; dst[2] = b.Prop.Vec3Val[2];
                            break;
                        }
                        case ReplicatedProperty::Type::String:
                            *static_cast<std::string*>(b.ValuePtr) = b.Prop.StringVal; break;
                    }
                    break;
                }
            }
        }
    }

    void NetworkObject::MarkAllDirty()
    {
        for (auto& b : m_Bindings) b.Prop.IsDirty = true;
    }

    void NetworkObject::ClearDirty()
    {
        for (auto& b : m_Bindings) b.Prop.IsDirty = false;
    }

    bool NetworkObject::HasDirtyProperties() const
    {
        for (const auto& b : m_Bindings)
            if (b.Prop.IsDirty) return true;
        return false;
    }

    // ═══════════════════════════════════
    //  ReplicationManager
    // ═══════════════════════════════════

    ReplicationManager& ReplicationManager::Get()
    {
        static ReplicationManager instance;
        return instance;
    }

    NetworkObjectID ReplicationManager::RegisterObject(NetworkObject* obj)
    {
        NetworkObjectID id = m_NextID++;
        obj->SetID(id);
        m_Objects[id] = obj;
        CQ_CORE_INFO("[Replication] Object registered: ID={0}", id);
        return id;
    }

    void ReplicationManager::UnregisterObject(NetworkObjectID id)
    {
        m_Objects.erase(id);
    }

    NetworkObject* ReplicationManager::GetObject(NetworkObjectID id)
    {
        auto it = m_Objects.find(id);
        return (it != m_Objects.end()) ? it->second : nullptr;
    }

    void ReplicationManager::SyncAll()
    {
        auto& net = NetworkManager::Get();
        if (!net.IsServer()) return;

        for (auto& [id, obj] : m_Objects)
        {
            Packet statePkt = obj->SerializeState();
            if (obj->HasDirtyProperties())
            {
                net.BroadcastPacket(statePkt, NetworkChannel::StateSync);
                obj->ClearDirty();
            }
        }
    }

    void ReplicationManager::ApplyStateUpdate(Packet& pkt)
    {
        uint32_t objID = pkt.ReadUint32();
        auto* obj = GetObject(objID);
        if (!obj)
        {
            CQ_CORE_WARN("[Replication] Received state for unknown object: {0}", objID);
            return;
        }
        obj->DeserializeState(pkt);
    }
}
