#pragma once

#include "Base.h"
#include <cstdint>
#include <type_traits>

namespace Conqueror::BitUtils
{
    // Count set bits (population count)
    CQ_API inline int PopCount(uint32_t value)
    {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_popcount(value);
        #elif defined(_MSC_VER)
            return __popcnt(value);
        #else
            int count = 0;
            while (value)
            {
                count += value & 1;
                value >>= 1;
            }
            return count;
        #endif
    }
    
    CQ_API inline int PopCount(uint64_t value)
    {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_popcountll(value);
        #elif defined(_MSC_VER)
            return __popcnt64(value);
        #else
            return PopCount(static_cast<uint32_t>(value)) + 
                   PopCount(static_cast<uint32_t>(value >> 32));
        #endif
    }
    
    // Count leading zeros
    CQ_API inline int CountLeadingZeros(uint32_t value)
    {
        if (value == 0) return 32;
        
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_clz(value);
        #elif defined(_MSC_VER)
            unsigned long index;
            _BitScanReverse(&index, value);
            return 31 - index;
        #else
            int count = 0;
            for (int i = 31; i >= 0; --i)
            {
                if (value & (1u << i)) break;
                count++;
            }
            return count;
        #endif
    }
    
    CQ_API inline int CountLeadingZeros(uint64_t value)
    {
        if (value == 0) return 64;
        
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_clzll(value);
        #elif defined(_MSC_VER) && defined(_WIN64)
            unsigned long index;
            _BitScanReverse64(&index, value);
            return 63 - index;
        #else
            if (value >> 32)
                return CountLeadingZeros(static_cast<uint32_t>(value >> 32));
            return 32 + CountLeadingZeros(static_cast<uint32_t>(value));
        #endif
    }
    
    // Count trailing zeros
    CQ_API inline int CountTrailingZeros(uint32_t value)
    {
        if (value == 0) return 32;
        
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_ctz(value);
        #elif defined(_MSC_VER)
            unsigned long index;
            _BitScanForward(&index, value);
            return index;
        #else
            int count = 0;
            while ((value & 1) == 0)
            {
                value >>= 1;
                count++;
            }
            return count;
        #endif
    }
    
    CQ_API inline int CountTrailingZeros(uint64_t value)
    {
        if (value == 0) return 64;
        
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_ctzll(value);
        #elif defined(_MSC_VER) && defined(_WIN64)
            unsigned long index;
            _BitScanForward64(&index, value);
            return index;
        #else
            uint32_t low = static_cast<uint32_t>(value);
            if (low != 0)
                return CountTrailingZeros(low);
            return 32 + CountTrailingZeros(static_cast<uint32_t>(value >> 32));
        #endif
    }
    
    // Find first set bit (1-indexed, 0 if no bits set)
    CQ_API inline int FindFirstSet(uint32_t value)
    {
        return value == 0 ? 0 : CountTrailingZeros(value) + 1;
    }
    
    CQ_API inline int FindFirstSet(uint64_t value)
    {
        return value == 0 ? 0 : CountTrailingZeros(value) + 1;
    }
    
    // Find last set bit (1-indexed, 0 if no bits set)
    CQ_API inline int FindLastSet(uint32_t value)
    {
        return value == 0 ? 0 : 32 - CountLeadingZeros(value);
    }
    
    CQ_API inline int FindLastSet(uint64_t value)
    {
        return value == 0 ? 0 : 64 - CountLeadingZeros(value);
    }
    
    // Check if power of 2
    CQ_API inline bool IsPowerOfTwo(uint32_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }
    
    CQ_API inline bool IsPowerOfTwo(uint64_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }
    
    // Next power of 2
    CQ_API inline uint32_t NextPowerOfTwo(uint32_t value)
    {
        if (value == 0) return 1;
        value--;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        return value + 1;
    }
    
    CQ_API inline uint64_t NextPowerOfTwo(uint64_t value)
    {
        if (value == 0) return 1;
        value--;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        value |= value >> 32;
        return value + 1;
    }
    
    // Previous power of 2
    CQ_API inline uint32_t PrevPowerOfTwo(uint32_t value)
    {
        if (value == 0) return 0;
        return 1u << (31 - CountLeadingZeros(value));
    }
    
    CQ_API inline uint64_t PrevPowerOfTwo(uint64_t value)
    {
        if (value == 0) return 0;
        return 1ull << (63 - CountLeadingZeros(value));
    }
    
    // Rotate left
    CQ_API inline uint32_t RotateLeft(uint32_t value, int shift)
    {
        shift &= 31;
        return (value << shift) | (value >> (32 - shift));
    }
    
    CQ_API inline uint64_t RotateLeft(uint64_t value, int shift)
    {
        shift &= 63;
        return (value << shift) | (value >> (64 - shift));
    }
    
    // Rotate right
    CQ_API inline uint32_t RotateRight(uint32_t value, int shift)
    {
        shift &= 31;
        return (value >> shift) | (value << (32 - shift));
    }
    
    CQ_API inline uint64_t RotateRight(uint64_t value, int shift)
    {
        shift &= 63;
        return (value >> shift) | (value << (64 - shift));
    }
    
    // Reverse bits
    CQ_API inline uint32_t ReverseBits(uint32_t value)
    {
        value = ((value & 0xAAAAAAAA) >> 1) | ((value & 0x55555555) << 1);
        value = ((value & 0xCCCCCCCC) >> 2) | ((value & 0x33333333) << 2);
        value = ((value & 0xF0F0F0F0) >> 4) | ((value & 0x0F0F0F0F) << 4);
        value = ((value & 0xFF00FF00) >> 8) | ((value & 0x00FF00FF) << 8);
        return (value >> 16) | (value << 16);
    }
    
    CQ_API inline uint64_t ReverseBits(uint64_t value)
    {
        value = ((value & 0xAAAAAAAAAAAAAAAA) >> 1) | ((value & 0x5555555555555555) << 1);
        value = ((value & 0xCCCCCCCCCCCCCCCC) >> 2) | ((value & 0x3333333333333333) << 2);
        value = ((value & 0xF0F0F0F0F0F0F0F0) >> 4) | ((value & 0x0F0F0F0F0F0F0F0F) << 4);
        value = ((value & 0xFF00FF00FF00FF00) >> 8) | ((value & 0x00FF00FF00FF00FF) << 8);
        value = ((value & 0xFFFF0000FFFF0000) >> 16) | ((value & 0x0000FFFF0000FFFF) << 16);
        return (value >> 32) | (value << 32);
    }
    
    // Byte swap (endianness conversion)
    CQ_API inline uint16_t ByteSwap(uint16_t value)
    {
        return (value >> 8) | (value << 8);
    }
    
    CQ_API inline uint32_t ByteSwap(uint32_t value)
    {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap32(value);
        #elif defined(_MSC_VER)
            return _byteswap_ulong(value);
        #else
            return ((value & 0xFF000000) >> 24) |
                   ((value & 0x00FF0000) >> 8) |
                   ((value & 0x0000FF00) << 8) |
                   ((value & 0x000000FF) << 24);
        #endif
    }
    
    CQ_API inline uint64_t ByteSwap(uint64_t value)
    {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap64(value);
        #elif defined(_MSC_VER)
            return _byteswap_uint64(value);
        #else
            return ((value & 0xFF00000000000000ull) >> 56) |
                   ((value & 0x00FF000000000000ull) >> 40) |
                   ((value & 0x0000FF0000000000ull) >> 24) |
                   ((value & 0x000000FF00000000ull) >> 8) |
                   ((value & 0x00000000FF000000ull) << 8) |
                   ((value & 0x0000000000FF0000ull) << 24) |
                   ((value & 0x000000000000FF00ull) << 40) |
                   ((value & 0x00000000000000FFull) << 56);
        #endif
    }
    
    // Bit manipulation
    CQ_API inline bool GetBit(uint32_t value, int bit)
    {
        return (value & (1u << bit)) != 0;
    }
    
    CQ_API inline uint32_t SetBit(uint32_t value, int bit)
    {
        return value | (1u << bit);
    }
    
    CQ_API inline uint32_t ClearBit(uint32_t value, int bit)
    {
        return value & ~(1u << bit);
    }
    
    CQ_API inline uint32_t ToggleBit(uint32_t value, int bit)
    {
        return value ^ (1u << bit);
    }
    
    // Extract bit range
    CQ_API inline uint32_t ExtractBits(uint32_t value, int start, int length)
    {
        uint32_t mask = (1u << length) - 1;
        return (value >> start) & mask;
    }
    
    // Insert bit range
    CQ_API inline uint32_t InsertBits(uint32_t dest, uint32_t src, int start, int length)
    {
        uint32_t mask = ((1u << length) - 1) << start;
        return (dest & ~mask) | ((src << start) & mask);
    }
}
