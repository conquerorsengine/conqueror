#include "HashMath.h"

namespace Conqueror::Math {
    static inline uint32_t rotl32(uint32_t x, int8_t r) {
        return (x << r) | (x >> (32 - r));
    }

    uint32_t HashMath::MurmurHash3_32(const void* key, int len, uint32_t seed) {
        const uint8_t* data = (const uint8_t*)key;
        const int nblocks = len / 4;

        uint32_t h1 = seed;
        const uint32_t c1 = 0xcc9e2d51;
        const uint32_t c2 = 0x1b873593;

        const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);

        for (int i = -nblocks; i; i++) {
            uint32_t k1 = blocks[i];
            k1 *= c1;
            k1 = rotl32(k1, 15);
            k1 *= c2;
            h1 ^= k1;
            h1 = rotl32(h1, 13);
            h1 = h1 * 5 + 0xe6546b64;
        }

        const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);
        uint32_t k1 = 0;

        switch (len & 3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
            k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
        };

        h1 ^= len;
        h1 ^= h1 >> 16;
        h1 *= 0x85ebca6b;
        h1 ^= h1 >> 13;
        h1 *= 0xc2b2ae35;
        h1 ^= h1 >> 16;

        return h1;
    }

    uint64_t HashMath::FNV1a_64(const void* key, int len) {
        const uint8_t* data = (const uint8_t*)key;
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (int i = 0; i < len; ++i) {
            hash ^= data[i];
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    uint64_t HashMath::FNV1a_64(const std::string& str) {
        return FNV1a_64(str.c_str(), str.length());
    }

    uint32_t HashMath::PCG32(uint64_t& state, uint64_t inc) {
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ULL + inc;
        uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
        uint32_t rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
}
