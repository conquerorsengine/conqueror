#pragma once

#include "TypeInfo.h"
#include <yaml-cpp/yaml.h>

namespace Conqueror::Reflection
{
    // Reflection kullanarak otomatik serialization
    class CQ_API SerializationHelper
    {
    public:
        // Component'i YAML'a serialize et
        template<typename T>
        static void Serialize(YAML::Emitter& out, const T& component)
        {
            const TypeInfo* typeInfo = TypeRegistry::Get().GetType<T>();
            if (!typeInfo)
                return;
            
            out << YAML::BeginMap;
            
            for (const auto& prop : typeInfo->Properties)
            {
                if (!(prop.Flags & PropertyFlags::Serializable))
                    continue;
                
                out << YAML::Key << prop.Name;
                out << YAML::Value;
                
                SerializeProperty(out, prop, &component);
            }
            
            out << YAML::EndMap;
        }
        
        // YAML'dan component'i deserialize et
        template<typename T>
        static void Deserialize(const YAML::Node& node, T& component)
        {
            const TypeInfo* typeInfo = TypeRegistry::Get().GetType<T>();
            if (!typeInfo)
                return;
            
            for (const auto& prop : typeInfo->Properties)
            {
                if (!(prop.Flags & PropertyFlags::Serializable))
                    continue;
                
                if (!node[prop.Name])
                    continue;
                
                DeserializeProperty(node[prop.Name], prop, &component);
            }
        }
        
        // Component'i clone et (deep copy)
        template<typename T>
        static T Clone(const T& source)
        {
            T dest;
            const TypeInfo* typeInfo = TypeRegistry::Get().GetType<T>();
            if (!typeInfo)
                return dest;
            
            for (const auto& prop : typeInfo->Properties)
            {
                std::any value = prop.Getter(&source);
                prop.Setter(&dest, value);
            }
            
            return dest;
        }
        
    private:
        static void SerializeProperty(YAML::Emitter& out, const PropertyInfo& prop, const void* obj);
        static void DeserializeProperty(const YAML::Node& node, const PropertyInfo& prop, void* obj);
    };
}
