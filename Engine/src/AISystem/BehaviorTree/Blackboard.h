#pragma once

#include <string>
#include <unordered_map>
#include <any>

namespace Conqueror
{
    // Blackboard, AI'ın hafızasıdır. Değişkenleri burada tutar.
    class Blackboard
    {
    public:
        template<typename T>
        void SetValue(const std::string& key, const T& value)
        {
            m_Data[key] = value;
        }

        template<typename T>
        bool GetValue(const std::string& key, T& outValue) const
        {
            auto it = m_Data.find(key);
            if (it != m_Data.end())
            {
                try
                {
                    outValue = std::any_cast<T>(it->second);
                    return true;
                }
                catch (const std::bad_any_cast&)
                {
                    return false;
                }
            }
            return false;
        }

        bool HasKey(const std::string& key) const
        {
            return m_Data.find(key) != m_Data.end();
        }

    private:
        std::unordered_map<std::string, std::any> m_Data;
    };
}
