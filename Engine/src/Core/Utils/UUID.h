#pragma once

#include "Core/Base/Base.h"
#include <cstdint>
#include <string>
#include <functional>

namespace Conqueror
{
    // 64-bit UUID for entities and assets
    class CQ_API UUID
    {
    public:
        UUID();
        UUID(uint64_t uuid);
        UUID(const UUID&) = default;

        operator uint64_t() const { return m_UUID; }
        
        bool IsValid() const { return m_UUID != 0; }
        
        std::string ToString() const;
        static UUID FromString(const std::string& str);
        static std::string GenerateV4();
        
        bool operator==(const UUID& other) const { return m_UUID == other.m_UUID; }
        bool operator!=(const UUID& other) const { return m_UUID != other.m_UUID; }
        bool operator<(const UUID& other) const { return m_UUID < other.m_UUID; }

    private:
        uint64_t m_UUID;
    };
}

// Hash function for UUID (for unordered_map)
namespace std
{
    template<>
    struct hash<Conqueror::UUID>
    {
        size_t operator()(const Conqueror::UUID& uuid) const
        {
            return hash<uint64_t>()((uint64_t)uuid);
        }
    };
}
