#pragma once

#include "NetworkTypes.h"
#include <vector>
#include <cstring>
#include <string>

namespace Conqueror
{
    /// Binary packet for network serialization.
    /// Write data in order on sender side, read in same order on receiver side.
    class Packet
    {
    public:
        Packet() = default;
        explicit Packet(MessageType type) : m_Type(type) {}
        Packet(const uint8_t* data, size_t size);

        // ── Header ──
        void SetType(MessageType type) { m_Type = type; }
        MessageType GetType() const { return m_Type; }

        // ── Write ──
        void WriteBool(bool val);
        void WriteInt8(int8_t val);
        void WriteUint8(uint8_t val);
        void WriteInt16(int16_t val);
        void WriteUint16(uint16_t val);
        void WriteInt32(int32_t val);
        void WriteUint32(uint32_t val);
        void WriteInt64(int64_t val);
        void WriteUint64(uint64_t val);
        void WriteFloat(float val);
        void WriteDouble(double val);
        void WriteString(const std::string& val);
        void WriteBytes(const uint8_t* data, size_t size);
        void WriteVec3(float x, float y, float z);

        // ── Read ──
        bool        ReadBool();
        int8_t      ReadInt8();
        uint8_t     ReadUint8();
        int16_t     ReadInt16();
        uint16_t    ReadUint16();
        int32_t     ReadInt32();
        uint32_t    ReadUint32();
        int64_t     ReadInt64();
        uint64_t    ReadUint64();
        float       ReadFloat();
        double      ReadDouble();
        std::string ReadString();
        void        ReadBytes(uint8_t* outData, size_t size);
        void        ReadVec3(float& x, float& y, float& z);

        // ── Serialization ──
        /// Serialize entire packet (header + body) into a byte buffer for sending
        std::vector<uint8_t> Serialize() const;
        /// Deserialize from raw bytes received from network
        static Packet Deserialize(const uint8_t* data, size_t size);

        // ── Compression ──
        /// Compress the body using zstd
        std::vector<uint8_t> SerializeCompressed() const;
        /// Decompress from zstd-compressed data
        static Packet DeserializeCompressed(const uint8_t* data, size_t size);

        // ── Accessors ──
        const std::vector<uint8_t>& GetBody() const { return m_Body; }
        size_t GetBodySize() const { return m_Body.size(); }
        size_t GetReadPosition() const { return m_ReadPos; }
        void ResetRead() { m_ReadPos = 0; }

    private:
        template<typename T>
        void WriteRaw(T val);

        template<typename T>
        T ReadRaw();

        MessageType          m_Type = MessageType::UserMessage;
        std::vector<uint8_t> m_Body;
        size_t               m_ReadPos = 0;
    };
}
