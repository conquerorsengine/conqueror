#pragma once
#include "Core/Base/Base.h"
#include <cstdint>
#include <string>

namespace Conqueror::Math {
    class CQ_API HashMath {
    public:
        // MurmurHash3 (32-bit)
        static uint32_t MurmurHash3_32(const void* key, int len, uint32_t seed = 0);
        
        // FNV-1a (64-bit)
        static uint64_t FNV1a_64(const void* key, int len);
        static uint64_t FNV1a_64(const std::string& str);
        
        // PCG32 (Permuted Congruential Generator)
        static uint32_t PCG32(uint64_t& state, uint64_t inc);
    };
}
