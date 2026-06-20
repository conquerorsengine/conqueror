#include "TypeInfo.h"
#include "Core/Logging/Log.h"

namespace Conqueror::Reflection
{
    TypeRegistry& TypeRegistry::Get()
    {
        static TypeRegistry instance;
        return instance;
    }

    void TypeRegistry::RegisterType(const TypeInfo& typeInfo)
    {
        m_TypesByName[typeInfo.Name] = typeInfo;
        m_TypesByIndex[typeInfo.TypeIndex] = typeInfo;
        // Log sistemi henüz başlamadıysa log atma (static initialization order fiasco)
    }

    const TypeInfo* TypeRegistry::GetType(const std::string& name) const
    {
        auto it = m_TypesByName.find(name);
        if (it != m_TypesByName.end())
            return &it->second;
        return nullptr;
    }

    const TypeInfo* TypeRegistry::GetType(std::type_index typeIndex) const
    {
        auto it = m_TypesByIndex.find(typeIndex);
        if (it != m_TypesByIndex.end())
            return &it->second;
        return nullptr;
    }

    bool TypeRegistry::HasType(const std::string& name) const
    {
        return m_TypesByName.find(name) != m_TypesByName.end();
    }

    bool TypeRegistry::HasType(std::type_index typeIndex) const
    {
        return m_TypesByIndex.find(typeIndex) != m_TypesByIndex.end();
    }

    std::vector<const TypeInfo*> TypeRegistry::GetAllTypes() const
    {
        std::vector<const TypeInfo*> types;
        types.reserve(m_TypesByName.size());
        for (const auto& [name, typeInfo] : m_TypesByName)
        {
            types.push_back(&typeInfo);
        }
        return types;
    }

    const PropertyInfo* TypeInfo::GetProperty(const std::string& name) const
    {
        for (const auto& prop : Properties)
        {
            if (prop.Name == name)
                return &prop;
        }
        return nullptr;
    }

    const MethodInfo* TypeInfo::GetMethod(const std::string& name) const
    {
        for (const auto& method : Methods)
        {
            if (method.Name == name)
                return &method;
        }
        return nullptr;
    }

    bool TypeInfo::HasProperty(const std::string& name) const
    {
        return GetProperty(name) != nullptr;
    }

    bool TypeInfo::HasMethod(const std::string& name) const
    {
        return GetMethod(name) != nullptr;
    }
}

// Component reflection registration
#include "Scene/Components.h"

namespace
{
    // TransformComponent
    struct TransformComponent_ReflectionRegistrar
    {
        TransformComponent_ReflectionRegistrar()
        {
            using T = Conqueror::TransformComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("TransformComponent");
            registrar.Property("Position", &T::Position);
            registrar.Property("Rotation", &T::Rotation);
            registrar.Property("Scale", &T::Scale);
        }
    };
    static TransformComponent_ReflectionRegistrar s_TransformComponent_Registrar;
    
    // TagComponent
    struct TagComponent_ReflectionRegistrar
    {
        TagComponent_ReflectionRegistrar()
        {
            using T = Conqueror::TagComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("TagComponent");
            registrar.Property("Tag", &T::Tag);
            registrar.Property("GameTag", &T::GameTag);
        }
    };
    static TagComponent_ReflectionRegistrar s_TagComponent_Registrar;
    
    // SpriteRendererComponent
    struct SpriteRendererComponent_ReflectionRegistrar
    {
        SpriteRendererComponent_ReflectionRegistrar()
        {
            using T = Conqueror::SpriteRendererComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("SpriteRendererComponent");
            registrar.Property("TilingFactor", &T::TilingFactor);
        }
    };
    static SpriteRendererComponent_ReflectionRegistrar s_SpriteRendererComponent_Registrar;
    
    // MeshRendererComponent
    struct MeshRendererComponent_ReflectionRegistrar
    {
        MeshRendererComponent_ReflectionRegistrar()
        {
            using T = Conqueror::MeshRendererComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("MeshRendererComponent");
            registrar.Property("Metallic", &T::Metallic);
            registrar.Property("Roughness", &T::Roughness);
            registrar.Property("AO", &T::AO);
        }
    };
    static MeshRendererComponent_ReflectionRegistrar s_MeshRendererComponent_Registrar;
    
    // RigidbodyComponent
    struct RigidbodyComponent_ReflectionRegistrar
    {
        RigidbodyComponent_ReflectionRegistrar()
        {
            using T = Conqueror::RigidbodyComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("RigidbodyComponent");
            registrar.Property("Mass", &T::Mass);
            registrar.Property("LinearDrag", &T::LinearDrag);
            registrar.Property("AngularDrag", &T::AngularDrag);
            registrar.Property("GravityScale", &T::GravityScale);
            registrar.Property("FreezeRotation", &T::FreezeRotation);
            registrar.Property("Friction", &T::Friction);
            registrar.Property("Restitution", &T::Restitution);
        }
    };
    static RigidbodyComponent_ReflectionRegistrar s_RigidbodyComponent_Registrar;
    
    // BoxColliderComponent
    struct BoxColliderComponent_ReflectionRegistrar
    {
        BoxColliderComponent_ReflectionRegistrar()
        {
            using T = Conqueror::BoxColliderComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("BoxColliderComponent");
            registrar.Property("Offset", &T::Offset);
            registrar.Property("Size", &T::Size);
            registrar.Property("IsTrigger", &T::IsTrigger);
        }
    };
    static BoxColliderComponent_ReflectionRegistrar s_BoxColliderComponent_Registrar;
    
    // SphereColliderComponent
    struct SphereColliderComponent_ReflectionRegistrar
    {
        SphereColliderComponent_ReflectionRegistrar()
        {
            using T = Conqueror::SphereColliderComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("SphereColliderComponent");
            registrar.Property("Offset", &T::Offset);
            registrar.Property("Radius", &T::Radius);
            registrar.Property("IsTrigger", &T::IsTrigger);
        }
    };
    static SphereColliderComponent_ReflectionRegistrar s_SphereColliderComponent_Registrar;
    
    // DirectionalLightComponent
    struct DirectionalLightComponent_ReflectionRegistrar
    {
        DirectionalLightComponent_ReflectionRegistrar()
        {
            using T = Conqueror::DirectionalLightComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("DirectionalLightComponent");
            registrar.Property("Direction", &T::Direction);
            registrar.Property("Intensity", &T::Intensity);
            registrar.Property("CastShadows", &T::CastShadows);
        }
    };
    static DirectionalLightComponent_ReflectionRegistrar s_DirectionalLightComponent_Registrar;
    
    // PointLightComponent
    struct PointLightComponent_ReflectionRegistrar
    {
        PointLightComponent_ReflectionRegistrar()
        {
            using T = Conqueror::PointLightComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("PointLightComponent");
            registrar.Property("Intensity", &T::Intensity);
            registrar.Property("Range", &T::Range);
            registrar.Property("Constant", &T::Constant);
            registrar.Property("Linear", &T::Linear);
            registrar.Property("Quadratic", &T::Quadratic);
            registrar.Property("CastShadows", &T::CastShadows);
        }
    };
    static PointLightComponent_ReflectionRegistrar s_PointLightComponent_Registrar;
    
    // AudioSourceComponent
    struct AudioSourceComponent_ReflectionRegistrar
    {
        AudioSourceComponent_ReflectionRegistrar()
        {
            using T = Conqueror::AudioSourceComponent;
            Conqueror::Reflection::TypeRegistrar<T> registrar("AudioSourceComponent");
            registrar.Property("FilePath", &T::FilePath);
            registrar.Property("PlayOnAwake", &T::PlayOnAwake);
            registrar.Property("Loop", &T::Loop);
            registrar.Property("Mute", &T::Mute);
            registrar.Property("Volume", &T::Volume);
            registrar.Property("Pitch", &T::Pitch);
            registrar.Property("Pan", &T::Pan);
            registrar.Property("Is3D", &T::Is3D);
            registrar.Property("MinDistance", &T::MinDistance);
            registrar.Property("MaxDistance", &T::MaxDistance);
        }
    };
    static AudioSourceComponent_ReflectionRegistrar s_AudioSourceComponent_Registrar;
}
