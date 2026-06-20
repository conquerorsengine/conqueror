#pragma once
#include "Core/Base/Base.h"
#include <unordered_map>
#include <vector>

namespace Conqueror::MapUtils
{
    template<typename K, typename V>
    std::vector<K> GetKeys(const std::unordered_map<K, V>& map)
    {
        std::vector<K> keys;
        keys.reserve(map.size());
        for (const auto& pair : map) keys.push_back(pair.first);
        return keys;
    }

    template<typename K, typename V>
    std::vector<V> GetValues(const std::unordered_map<K, V>& map)
    {
        std::vector<V> values;
        values.reserve(map.size());
        for (const auto& pair : map) values.push_back(pair.second);
        return values;
    }

    template<typename K, typename V>
    void Merge(std::unordered_map<K, V>& dst, const std::unordered_map<K, V>& src)
    {
        for (const auto& pair : src) dst[pair.first] = pair.second;
    }
    
    // Map Extras
    template<typename K, typename V>
    V DefaultGet(const std::unordered_map<K, V>& map, const K& key, const V& defaultVal)
    {
        auto it = map.find(key);
        if (it != map.end()) return it->second;
        return defaultVal;
    }
    
    template<typename K, typename V>
    std::unordered_map<V, K> Invert(const std::unordered_map<K, V>& map)
    {
        std::unordered_map<V, K> result;
        for (const auto& pair : map) {
            result[pair.second] = pair.first;
        }
        return result;
    }
}
