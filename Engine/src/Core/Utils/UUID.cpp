#include "UUID.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace Conqueror
{
    static std::random_device s_RandomDevice;
    static std::mt19937_64 s_Engine(s_RandomDevice());
    static std::uniform_int_distribution<uint64_t> s_UniformDistribution;

    UUID::UUID()
        : m_UUID(s_UniformDistribution(s_Engine))
    {
    }

    UUID::UUID(uint64_t uuid)
        : m_UUID(uuid)
    {
    }

    std::string UUID::ToString() const
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(16) << m_UUID;
        return ss.str();
    }

    UUID UUID::FromString(const std::string& str)
    {
        uint64_t uuid;
        std::stringstream ss(str);
        ss >> std::hex >> uuid;
        return UUID(uuid);
    }

    std::string UUID::GenerateV4()
    {
        uint64_t a = s_UniformDistribution(s_Engine);
        uint64_t b = s_UniformDistribution(s_Engine);
        
        a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        
        uint32_t part1 = (uint32_t)(a >> 32);
        uint16_t part2 = (uint16_t)((a >> 16) & 0xFFFF);
        uint16_t part3 = (uint16_t)(a & 0xFFFF);
        uint16_t part4 = (uint16_t)(b >> 48);
        uint64_t part5 = b & 0xFFFFFFFFFFFFULL;
        
        ss << std::setw(8) << part1 << "-"
           << std::setw(4) << part2 << "-"
           << std::setw(4) << part3 << "-"
           << std::setw(4) << part4 << "-"
           << std::setw(12) << part5;
           
        return ss.str();
    }
}
