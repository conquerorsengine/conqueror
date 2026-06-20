#include "Packet.h"
#include "Core/Logging/Log.h"
#include <zstd.h>

namespace Conqueror
{
    // ── Packet Header Layout ──
    // [4 bytes: Protocol ID] [2 bytes: MessageType] [4 bytes: Body Size] [N bytes: Body]
    static constexpr size_t HEADER_SIZE = 4 + 2 + 4; // 10 bytes

    Packet::Packet(const uint8_t* data, size_t size)
    {
        if (data && size > 0)
        {
            m_Body.assign(data, data + size);
        }
    }

    // ═══════════════════════════════════
    //  Write Methods
    // ═══════════════════════════════════

    template<typename T>
    void Packet::WriteRaw(T val)
    {
        size_t oldSize = m_Body.size();
        m_Body.resize(oldSize + sizeof(T));
        std::memcpy(m_Body.data() + oldSize, &val, sizeof(T));
    }

    void Packet::WriteBool(bool val)       { WriteRaw<uint8_t>(val ? 1 : 0); }
    void Packet::WriteInt8(int8_t val)     { WriteRaw(val); }
    void Packet::WriteUint8(uint8_t val)   { WriteRaw(val); }
    void Packet::WriteInt16(int16_t val)   { WriteRaw(val); }
    void Packet::WriteUint16(uint16_t val) { WriteRaw(val); }
    void Packet::WriteInt32(int32_t val)   { WriteRaw(val); }
    void Packet::WriteUint32(uint32_t val) { WriteRaw(val); }
    void Packet::WriteInt64(int64_t val)   { WriteRaw(val); }
    void Packet::WriteUint64(uint64_t val) { WriteRaw(val); }
    void Packet::WriteFloat(float val)     { WriteRaw(val); }
    void Packet::WriteDouble(double val)   { WriteRaw(val); }

    void Packet::WriteString(const std::string& val)
    {
        WriteUint32(static_cast<uint32_t>(val.size()));
        if (!val.empty())
        {
            WriteBytes(reinterpret_cast<const uint8_t*>(val.data()), val.size());
        }
    }

    void Packet::WriteBytes(const uint8_t* data, size_t size)
    {
        size_t oldSize = m_Body.size();
        m_Body.resize(oldSize + size);
        std::memcpy(m_Body.data() + oldSize, data, size);
    }

    void Packet::WriteVec3(float x, float y, float z)
    {
        WriteFloat(x);
        WriteFloat(y);
        WriteFloat(z);
    }

    // ═══════════════════════════════════
    //  Read Methods
    // ═══════════════════════════════════

    template<typename T>
    T Packet::ReadRaw()
    {
        if (m_ReadPos + sizeof(T) > m_Body.size())
        {
            CQ_CORE_ERROR("[Packet] Read overflow! Pos: {0}, Size: {1}, Need: {2}",
                          m_ReadPos, m_Body.size(), sizeof(T));
            return T{};
        }
        T val;
        std::memcpy(&val, m_Body.data() + m_ReadPos, sizeof(T));
        m_ReadPos += sizeof(T);
        return val;
    }

    bool     Packet::ReadBool()    { return ReadRaw<uint8_t>() != 0; }
    int8_t   Packet::ReadInt8()    { return ReadRaw<int8_t>(); }
    uint8_t  Packet::ReadUint8()   { return ReadRaw<uint8_t>(); }
    int16_t  Packet::ReadInt16()   { return ReadRaw<int16_t>(); }
    uint16_t Packet::ReadUint16()  { return ReadRaw<uint16_t>(); }
    int32_t  Packet::ReadInt32()   { return ReadRaw<int32_t>(); }
    uint32_t Packet::ReadUint32()  { return ReadRaw<uint32_t>(); }
    int64_t  Packet::ReadInt64()   { return ReadRaw<int64_t>(); }
    uint64_t Packet::ReadUint64()  { return ReadRaw<uint64_t>(); }
    float    Packet::ReadFloat()   { return ReadRaw<float>(); }
    double   Packet::ReadDouble()  { return ReadRaw<double>(); }

    std::string Packet::ReadString()
    {
        uint32_t len = ReadUint32();
        if (len == 0) return "";
        if (m_ReadPos + len > m_Body.size())
        {
            CQ_CORE_ERROR("[Packet] String read overflow!");
            return "";
        }
        std::string result(reinterpret_cast<const char*>(m_Body.data() + m_ReadPos), len);
        m_ReadPos += len;
        return result;
    }

    void Packet::ReadBytes(uint8_t* outData, size_t size)
    {
        if (m_ReadPos + size > m_Body.size())
        {
            CQ_CORE_ERROR("[Packet] Bytes read overflow!");
            return;
        }
        std::memcpy(outData, m_Body.data() + m_ReadPos, size);
        m_ReadPos += size;
    }

    void Packet::ReadVec3(float& x, float& y, float& z)
    {
        x = ReadFloat();
        y = ReadFloat();
        z = ReadFloat();
    }

    // ═══════════════════════════════════
    //  Serialization (Uncompressed)
    // ═══════════════════════════════════

    std::vector<uint8_t> Packet::Serialize() const
    {
        std::vector<uint8_t> buffer;
        buffer.resize(HEADER_SIZE + m_Body.size());

        size_t offset = 0;
        // Protocol ID
        uint32_t protocolID = CQ_PROTOCOL_ID;
        std::memcpy(buffer.data() + offset, &protocolID, 4); offset += 4;
        // Message type
        uint16_t type = static_cast<uint16_t>(m_Type);
        std::memcpy(buffer.data() + offset, &type, 2); offset += 2;
        // Body size
        uint32_t bodySize = static_cast<uint32_t>(m_Body.size());
        std::memcpy(buffer.data() + offset, &bodySize, 4); offset += 4;
        // Body
        if (!m_Body.empty())
            std::memcpy(buffer.data() + offset, m_Body.data(), m_Body.size());

        return buffer;
    }

    Packet Packet::Deserialize(const uint8_t* data, size_t size)
    {
        Packet pkt;
        if (size < HEADER_SIZE)
        {
            CQ_CORE_ERROR("[Packet] Data too small to deserialize: {0} bytes", size);
            return pkt;
        }

        size_t offset = 0;
        // Protocol ID
        uint32_t protocolID;
        std::memcpy(&protocolID, data + offset, 4); offset += 4;
        if (protocolID != CQ_PROTOCOL_ID)
        {
            CQ_CORE_ERROR("[Packet] Invalid protocol ID: 0x{0:X}", protocolID);
            return pkt;
        }
        // Message type
        uint16_t type;
        std::memcpy(&type, data + offset, 2); offset += 2;
        pkt.m_Type = static_cast<MessageType>(type);
        // Body size
        uint32_t bodySize;
        std::memcpy(&bodySize, data + offset, 4); offset += 4;

        if (offset + bodySize > size)
        {
            CQ_CORE_ERROR("[Packet] Body size mismatch: claimed {0}, available {1}", bodySize, size - offset);
            return pkt;
        }

        if (bodySize > 0)
        {
            pkt.m_Body.assign(data + offset, data + offset + bodySize);
        }

        return pkt;
    }

    // ═══════════════════════════════════
    //  Compression (zstd)
    // ═══════════════════════════════════

    std::vector<uint8_t> Packet::SerializeCompressed() const
    {
        // First serialize normally
        auto uncompressed = Serialize();

        // Compress with zstd
        size_t compressBound = ZSTD_compressBound(uncompressed.size());
        std::vector<uint8_t> compressed(compressBound + 4); // +4 for original size

        // Store original size first
        uint32_t originalSize = static_cast<uint32_t>(uncompressed.size());
        std::memcpy(compressed.data(), &originalSize, 4);

        size_t compressedSize = ZSTD_compress(
            compressed.data() + 4, compressBound,
            uncompressed.data(), uncompressed.size(),
            1 // Compression level (1 = fastest)
        );

        if (ZSTD_isError(compressedSize))
        {
            CQ_CORE_ERROR("[Packet] zstd compression failed: {0}", ZSTD_getErrorName(compressedSize));
            return uncompressed; // Fallback to uncompressed
        }

        compressed.resize(4 + compressedSize);
        return compressed;
    }

    Packet Packet::DeserializeCompressed(const uint8_t* data, size_t size)
    {
        if (size < 4)
        {
            CQ_CORE_ERROR("[Packet] Compressed data too small");
            return Packet{};
        }

        // Read original size
        uint32_t originalSize;
        std::memcpy(&originalSize, data, 4);

        std::vector<uint8_t> decompressed(originalSize);
        size_t decompressedSize = ZSTD_decompress(
            decompressed.data(), originalSize,
            data + 4, size - 4
        );

        if (ZSTD_isError(decompressedSize))
        {
            CQ_CORE_ERROR("[Packet] zstd decompression failed: {0}", ZSTD_getErrorName(decompressedSize));
            return Packet{};
        }

        return Deserialize(decompressed.data(), decompressedSize);
    }
}
