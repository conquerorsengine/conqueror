#pragma once

#include "Base.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace Conqueror
{
    // FNV-1a hash (fast, good distribution)
    class CQ_API FNV1aHash
    {
    public:
        static constexpr uint32_t Hash32(const char* str, size_t length)
        {
            uint32_t hash = 2166136261u;
            for (size_t i = 0; i < length; ++i)
            {
                hash ^= static_cast<uint32_t>(str[i]);
                hash *= 16777619u;
            }
            return hash;
        }
        
        static constexpr uint64_t Hash64(const char* str, size_t length)
        {
            uint64_t hash = 14695981039346656037ull;
            for (size_t i = 0; i < length; ++i)
            {
                hash ^= static_cast<uint64_t>(str[i]);
                hash *= 1099511628211ull;
            }
            return hash;
        }
        
        static uint32_t Hash32(const std::string& str)
        {
            return Hash32(str.c_str(), str.length());
        }
        
        static uint64_t Hash64(const std::string& str)
        {
            return Hash64(str.c_str(), str.length());
        }
    };

    // MurmurHash3 (excellent distribution, fast)
    class CQ_API MurmurHash3
    {
    public:
        static uint32_t Hash32(const void* key, size_t length, uint32_t seed = 0);
        static uint64_t Hash64(const void* key, size_t length, uint64_t seed = 0);
        
        static uint32_t Hash32(const std::string& str, uint32_t seed = 0)
        {
            return Hash32(str.c_str(), str.length(), seed);
        }
        
        static uint64_t Hash64(const std::string& str, uint64_t seed = 0)
        {
            return Hash64(str.c_str(), str.length(), seed);
        }
    };

    // CRC32 (cyclic redundancy check, good for data integrity)
    class CQ_API CRC32
    {
    public:
        static void Init();
        static uint32_t Calculate(const void* data, size_t length);
        static uint32_t Calculate(const std::string& str)
        {
            return Calculate(str.c_str(), str.length());
        }
        
    private:
        static uint32_t s_Table[256];
        static bool s_Initialized;
    };

    // xxHash (extremely fast, excellent quality)
    class CQ_API XXHash
    {
    public:
        static uint32_t Hash32(const void* data, size_t length, uint32_t seed = 0);
        static uint64_t Hash64(const void* data, size_t length, uint64_t seed = 0);
        
        static uint32_t Hash32(const std::string& str, uint32_t seed = 0)
        {
            return Hash32(str.c_str(), str.length(), seed);
        }
        
        static uint64_t Hash64(const std::string& str, uint64_t seed = 0)
        {
            return Hash64(str.c_str(), str.length(), seed);
        }
    };

    // Compile-time string hashing (for switch statements, etc.)
    namespace CompileTime
    {
        constexpr uint32_t Hash(const char* str)
        {
            uint32_t hash = 2166136261u;
            while (*str)
            {
                hash ^= static_cast<uint32_t>(*str++);
                hash *= 16777619u;
            }
            return hash;
        }
        
        constexpr uint64_t Hash64(const char* str)
        {
            uint64_t hash = 14695981039346656037ull;
            while (*str)
            {
                hash ^= static_cast<uint64_t>(*str++);
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    // Hash combine (for hashing multiple values)
    template<typename T>
    inline void HashCombine(size_t& seed, const T& value)
    {
        std::hash<T> hasher;
        seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    // Pair hash (for std::unordered_map with pair keys)
    struct PairHash
    {
        template<typename T1, typename T2>
        size_t operator()(const std::pair<T1, T2>& pair) const
        {
            size_t seed = 0;
            HashCombine(seed, pair.first);
            HashCombine(seed, pair.second);
            return seed;
        }
    };
}

// Compile-time string literal hashing
constexpr uint32_t operator""_hash(const char* str, size_t)
{
    return Conqueror::CompileTime::Hash(str);
}

constexpr uint64_t operator""_hash64(const char* str, size_t)
{
    return Conqueror::CompileTime::Hash64(str);
}
