#include "SerializationHelper.h"
#include <glm/glm.hpp>

namespace Conqueror::Reflection
{
    void SerializationHelper::SerializeProperty(YAML::Emitter& out, const PropertyInfo& prop, const void* obj)
    {
        std::any value = prop.Getter(obj);
        
        switch (prop.Type)
        {
            case PropertyType::Bool:
                out << std::any_cast<bool>(value);
                break;
            case PropertyType::Int32:
                out << std::any_cast<int32_t>(value);
                break;
            case PropertyType::UInt32:
                out << std::any_cast<uint32_t>(value);
                break;
            case PropertyType::Float:
                out << std::any_cast<float>(value);
                break;
            case PropertyType::Double:
                out << std::any_cast<double>(value);
                break;
            case PropertyType::String:
                out << std::any_cast<std::string>(value);
                break;
            case PropertyType::Vec2:
            {
                glm::vec2 vec = std::any_cast<glm::vec2>(value);
                out << YAML::Flow << YAML::BeginSeq << vec.x << vec.y << YAML::EndSeq;
                break;
            }
            case PropertyType::Vec3:
            {
                glm::vec3 vec = std::any_cast<glm::vec3>(value);
                out << YAML::Flow << YAML::BeginSeq << vec.x << vec.y << vec.z << YAML::EndSeq;
                break;
            }
            case PropertyType::Vec4:
            {
                glm::vec4 vec = std::any_cast<glm::vec4>(value);
                out << YAML::Flow << YAML::BeginSeq << vec.x << vec.y << vec.z << vec.w << YAML::EndSeq;
                break;
            }
            default:
                out << "null";
                break;
        }
    }

    void SerializationHelper::DeserializeProperty(const YAML::Node& node, const PropertyInfo& prop, void* obj)
    {
        switch (prop.Type)
        {
            case PropertyType::Bool:
                prop.Setter(obj, node.as<bool>());
                break;
            case PropertyType::Int32:
                prop.Setter(obj, node.as<int32_t>());
                break;
            case PropertyType::UInt32:
                prop.Setter(obj, node.as<uint32_t>());
                break;
            case PropertyType::Float:
                prop.Setter(obj, node.as<float>());
                break;
            case PropertyType::Double:
                prop.Setter(obj, node.as<double>());
                break;
            case PropertyType::String:
                prop.Setter(obj, node.as<std::string>());
                break;
            case PropertyType::Vec2:
            {
                glm::vec2 vec;
                vec.x = node[0].as<float>();
                vec.y = node[1].as<float>();
                prop.Setter(obj, vec);
                break;
            }
            case PropertyType::Vec3:
            {
                glm::vec3 vec;
                vec.x = node[0].as<float>();
                vec.y = node[1].as<float>();
                vec.z = node[2].as<float>();
                prop.Setter(obj, vec);
                break;
            }
            case PropertyType::Vec4:
            {
                glm::vec4 vec;
                vec.x = node[0].as<float>();
                vec.y = node[1].as<float>();
                vec.z = node[2].as<float>();
                vec.w = node[3].as<float>();
                prop.Setter(obj, vec);
                break;
            }
            default:
                break;
        }
    }
}
