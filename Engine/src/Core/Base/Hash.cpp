#include "Hash.h"
#include <cstring>

namespace Conqueror
{
    // ============================================================================
    // MurmurHash3 Implementation
    // ============================================================================
    
    static inline uint32_t rotl32(uint32_t x, int8_t r)
    {
        return (x << r) | (x >> (32 - r));
    }
    
    static inline uint64_t rotl64(uint64_t x, int8_t r)
    {
        return (x << r) | (x >> (64 - r));
    }
    
    uint32_t MurmurHash3::Hash32(const void* key, size_t length, uint32_t seed)
    {
        const uint8_t* data = static_cast<const uint8_t*>(key);
        const int nblocks = length / 4;
        
        uint32_t h1 = seed;
        
        const uint32_t c1 = 0xcc9e2d51;
        const uint32_t c2 = 0x1b873593;
        
        // Body
        const uint32_t* blocks = reinterpret_cast<const uint32_t*>(data + nblocks * 4);
        
        for (int i = -nblocks; i; i++)
        {
            uint32_t k1 = blocks[i];
            
            k1 *= c1;
            k1 = rotl32(k1, 15);
            k1 *= c2;
            
            h1 ^= k1;
            h1 = rotl32(h1, 13);
            h1 = h1 * 5 + 0xe6546b64;
        }
        
        // Tail
        const uint8_t* tail = data + nblocks * 4;
        
        uint32_t k1 = 0;
        
        switch (length & 3)
        {
            case 3: k1 ^= tail[2] << 16; [[fallthrough]];
            case 2: k1 ^= tail[1] << 8;  [[fallthrough]];
            case 1: k1 ^= tail[0];
                    k1 *= c1;
                    k1 = rotl32(k1, 15);
                    k1 *= c2;
                    h1 ^= k1;
        }
        
        // Finalization
        h1 ^= length;
        
        h1 ^= h1 >> 16;
        h1 *= 0x85ebca6b;
        h1 ^= h1 >> 13;
        h1 *= 0xc2b2ae35;
        h1 ^= h1 >> 16;
        
        return h1;
    }
    
    uint64_t MurmurHash3::Hash64(const void* key, size_t length, uint64_t seed)
    {
        const uint8_t* data = static_cast<const uint8_t*>(key);
        const int nblocks = length / 16;
        
        uint64_t h1 = seed;
        uint64_t h2 = seed;
        
        const uint64_t c1 = 0x87c37b91114253d5ull;
        const uint64_t c2 = 0x4cf5ad432745937full;
        
        // Body
        const uint64_t* blocks = reinterpret_cast<const uint64_t*>(data);
        
        for (int i = 0; i < nblocks; i++)
        {
            uint64_t k1 = blocks[i * 2 + 0];
            uint64_t k2 = blocks[i * 2 + 1];
            
            k1 *= c1;
            k1 = rotl64(k1, 31);
            k1 *= c2;
            h1 ^= k1;
            
            h1 = rotl64(h1, 27);
            h1 += h2;
            h1 = h1 * 5 + 0x52dce729;
            
            k2 *= c2;
            k2 = rotl64(k2, 33);
            k2 *= c1;
            h2 ^= k2;
            
            h2 = rotl64(h2, 31);
            h2 += h1;
            h2 = h2 * 5 + 0x38495ab5;
        }
        
        // Tail
        const uint8_t* tail = data + nblocks * 16;
        
        uint64_t k1 = 0;
        uint64_t k2 = 0;
        
        switch (length & 15)
        {
            case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48; [[fallthrough]];
            case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40; [[fallthrough]];
            case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32; [[fallthrough]];
            case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24; [[fallthrough]];
            case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16; [[fallthrough]];
            case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8;   [[fallthrough]];
            case 9:  k2 ^= static_cast<uint64_t>(tail[8]) << 0;
                     k2 *= c2;
                     k2 = rotl64(k2, 33);
                     k2 *= c1;
                     h2 ^= k2;
                     [[fallthrough]];
            
            case 8:  k1 ^= static_cast<uint64_t>(tail[7]) << 56; [[fallthrough]];
            case 7:  k1 ^= static_cast<uint64_t>(tail[6]) << 48; [[fallthrough]];
            case 6:  k1 ^= static_cast<uint64_t>(tail[5]) << 40; [[fallthrough]];
            case 5:  k1 ^= static_cast<uint64_t>(tail[4]) << 32; [[fallthrough]];
            case 4:  k1 ^= static_cast<uint64_t>(tail[3]) << 24; [[fallthrough]];
            case 3:  k1 ^= static_cast<uint64_t>(tail[2]) << 16; [[fallthrough]];
            case 2:  k1 ^= static_cast<uint64_t>(tail[1]) << 8;  [[fallthrough]];
            case 1:  k1 ^= static_cast<uint64_t>(tail[0]) << 0;
                     k1 *= c1;
                     k1 = rotl64(k1, 31);
                     k1 *= c2;
                     h1 ^= k1;
        }
        
        // Finalization
        h1 ^= length;
        h2 ^= length;
        
        h1 += h2;
        h2 += h1;
        
        h1 ^= h1 >> 33;
        h1 *= 0xff51afd7ed558ccdull;
        h1 ^= h1 >> 33;
        h1 *= 0xc4ceb9fe1a85ec53ull;
        h1 ^= h1 >> 33;
        
        h2 ^= h2 >> 33;
        h2 *= 0xff51afd7ed558ccdull;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ull;
        h2 ^= h2 >> 33;
        
        h1 += h2;
        
        return h1;
    }
    
    // ============================================================================
    // CRC32 Implementation
    // ============================================================================
    
    uint32_t CRC32::s_Table[256];
    bool CRC32::s_Initialized = false;
    
    void CRC32::Init()
    {
        if (s_Initialized)
            return;
        
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; j++)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
            }
            s_Table[i] = crc;
        }
        
        s_Initialized = true;
    }
    
    uint32_t CRC32::Calculate(const void* data, size_t length)
    {
        if (!s_Initialized)
            Init();
        
        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        
        for (size_t i = 0; i < length; i++)
        {
            crc = (crc >> 8) ^ s_Table[(crc ^ bytes[i]) & 0xFF];
        }
        
        return ~crc;
    }
    
    // ============================================================================
    // xxHash Implementation
    // ============================================================================
    
    static const uint32_t PRIME32_1 = 0x9E3779B1U;
    static const uint32_t PRIME32_2 = 0x85EBCA77U;
    static const uint32_t PRIME32_3 = 0xC2B2AE3DU;
    static const uint32_t PRIME32_4 = 0x27D4EB2FU;
    static const uint32_t PRIME32_5 = 0x165667B1U;
    
    static const uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
    static const uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
    static const uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
    static const uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
    static const uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;
    
    uint32_t XXHash::Hash32(const void* data, size_t length, uint32_t seed)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        const uint8_t* const end = p + length;
        uint32_t h32;
        
        if (length >= 16)
        {
            const uint8_t* const limit = end - 16;
            uint32_t v1 = seed + PRIME32_1 + PRIME32_2;
            uint32_t v2 = seed + PRIME32_2;
            uint32_t v3 = seed + 0;
            uint32_t v4 = seed - PRIME32_1;
            
            do
            {
                v1 += *reinterpret_cast<const uint32_t*>(p) * PRIME32_2;
                v1 = rotl32(v1, 13);
                v1 *= PRIME32_1;
                p += 4;
                
                v2 += *reinterpret_cast<const uint32_t*>(p) * PRIME32_2;
                v2 = rotl32(v2, 13);
                v2 *= PRIME32_1;
                p += 4;
                
                v3 += *reinterpret_cast<const uint32_t*>(p) * PRIME32_2;
                v3 = rotl32(v3, 13);
                v3 *= PRIME32_1;
                p += 4;
                
                v4 += *reinterpret_cast<const uint32_t*>(p) * PRIME32_2;
                v4 = rotl32(v4, 13);
                v4 *= PRIME32_1;
                p += 4;
            } while (p <= limit);
            
            h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
        }
        else
        {
            h32 = seed + PRIME32_5;
        }
        
        h32 += static_cast<uint32_t>(length);
        
        while (p + 4 <= end)
        {
            h32 += *reinterpret_cast<const uint32_t*>(p) * PRIME32_3;
            h32 = rotl32(h32, 17) * PRIME32_4;
            p += 4;
        }
        
        while (p < end)
        {
            h32 += (*p) * PRIME32_5;
            h32 = rotl32(h32, 11) * PRIME32_1;
            p++;
        }
        
        h32 ^= h32 >> 15;
        h32 *= PRIME32_2;
        h32 ^= h32 >> 13;
        h32 *= PRIME32_3;
        h32 ^= h32 >> 16;
        
        return h32;
    }
    
    uint64_t XXHash::Hash64(const void* data, size_t length, uint64_t seed)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        const uint8_t* const end = p + length;
        uint64_t h64;
        
        if (length >= 32)
        {
            const uint8_t* const limit = end - 32;
            uint64_t v1 = seed + PRIME64_1 + PRIME64_2;
            uint64_t v2 = seed + PRIME64_2;
            uint64_t v3 = seed + 0;
            uint64_t v4 = seed - PRIME64_1;
            
            do
            {
                v1 += *reinterpret_cast<const uint64_t*>(p) * PRIME64_2;
                v1 = rotl64(v1, 31);
                v1 *= PRIME64_1;
                p += 8;
                
                v2 += *reinterpret_cast<const uint64_t*>(p) * PRIME64_2;
                v2 = rotl64(v2, 31);
                v2 *= PRIME64_1;
                p += 8;
                
                v3 += *reinterpret_cast<const uint64_t*>(p) * PRIME64_2;
                v3 = rotl64(v3, 31);
                v3 *= PRIME64_1;
                p += 8;
                
                v4 += *reinterpret_cast<const uint64_t*>(p) * PRIME64_2;
                v4 = rotl64(v4, 31);
                v4 *= PRIME64_1;
                p += 8;
            } while (p <= limit);
            
            h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
            
            v1 *= PRIME64_2;
            v1 = rotl64(v1, 31);
            v1 *= PRIME64_1;
            h64 ^= v1;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            
            v2 *= PRIME64_2;
            v2 = rotl64(v2, 31);
            v2 *= PRIME64_1;
            h64 ^= v2;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            
            v3 *= PRIME64_2;
            v3 = rotl64(v3, 31);
            v3 *= PRIME64_1;
            h64 ^= v3;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            
            v4 *= PRIME64_2;
            v4 = rotl64(v4, 31);
            v4 *= PRIME64_1;
            h64 ^= v4;
            h64 = h64 * PRIME64_1 + PRIME64_4;
        }
        else
        {
            h64 = seed + PRIME64_5;
        }
        
        h64 += static_cast<uint64_t>(length);
        
        while (p + 8 <= end)
        {
            uint64_t k1 = *reinterpret_cast<const uint64_t*>(p);
            k1 *= PRIME64_2;
            k1 = rotl64(k1, 31);
            k1 *= PRIME64_1;
            h64 ^= k1;
            h64 = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
            p += 8;
        }
        
        if (p + 4 <= end)
        {
            h64 ^= static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(p)) * PRIME64_1;
            h64 = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
            p += 4;
        }
        
        while (p < end)
        {
            h64 ^= (*p) * PRIME64_5;
            h64 = rotl64(h64, 11) * PRIME64_1;
            p++;
        }
        
        h64 ^= h64 >> 33;
        h64 *= PRIME64_2;
        h64 ^= h64 >> 29;
        h64 *= PRIME64_3;
        h64 ^= h64 >> 32;
        
        return h64;
    }
}
