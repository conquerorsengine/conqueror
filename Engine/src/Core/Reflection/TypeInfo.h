#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <any>
#include <typeindex>
#include <memory>

namespace Conqueror::Reflection
{
    // Property type enum
    enum class PropertyType
    {
        Unknown,
        Bool,
        Int8, Int16, Int32, Int64,
        UInt8, UInt16, UInt32, UInt64,
        Float, Double,
        String,
        Vec2, Vec3, Vec4,
        Quat, Mat3, Mat4,
        Enum,
        Pointer,
        Reference,
        Array,
        Custom
    };

    // Property flags
    enum class PropertyFlags : uint32_t
    {
        None = 0,
        ReadOnly = 1 << 0,
        Serializable = 1 << 1,
        EditorVisible = 1 << 2,
        Transient = 1 << 3, // Runtime only, not serialized
        Hidden = 1 << 4
    };

    inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b)
    {
        return static_cast<PropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool operator&(PropertyFlags a, PropertyFlags b)
    {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    // Attribute system (metadata)
    class CQ_API Attribute
    {
    public:
        virtual ~Attribute() = default;
        virtual std::string GetName() const = 0;
    };

    // Range attribute (min/max değerler için)
    class CQ_API RangeAttribute : public Attribute
    {
    public:
        RangeAttribute(float min, float max) : Min(min), Max(max) {}
        std::string GetName() const override { return "Range"; }
        
        float Min, Max;
    };

    // Tooltip attribute
    class CQ_API TooltipAttribute : public Attribute
    {
    public:
        TooltipAttribute(const std::string& text) : Text(text) {}
        std::string GetName() const override { return "Tooltip"; }
        
        std::string Text;
    };

    // Property metadata
    class CQ_API PropertyInfo
    {
    public:
        PropertyInfo() : TypeIndex(typeid(void)), Offset(0), Size(0), Type(PropertyType::Unknown), Flags(PropertyFlags::None) {}
        
        std::string Name;
        PropertyType Type;
        std::type_index TypeIndex;
        size_t Offset;
        size_t Size;
        PropertyFlags Flags;
        
        // Getter/Setter (type-erased)
        std::function<std::any(const void*)> Getter;
        std::function<void(void*, const std::any&)> Setter;
        
        // Attributes
        std::vector<std::shared_ptr<Attribute>> Attributes;
        
        template<typename T>
        T GetAttribute() const
        {
            for (const auto& attr : Attributes)
            {
                if (auto casted = std::dynamic_pointer_cast<T>(attr))
                    return casted;
            }
            return nullptr;
        }
        
        bool HasFlag(PropertyFlags flag) const { return Flags & flag; }
    };

    // Method metadata
    class CQ_API MethodInfo
    {
    public:
        std::string Name;
        std::type_index ReturnType;
        std::vector<std::type_index> ParameterTypes;
        
        // Invoke (type-erased)
        std::function<std::any(void*, const std::vector<std::any>&)> Invoker;
    };

    // Type metadata
    class CQ_API TypeInfo
    {
    public:
        TypeInfo() : TypeIndex(typeid(void)), Size(0), Parent(nullptr) {}
        
        std::string Name;
        std::type_index TypeIndex;
        size_t Size;
        
        std::vector<PropertyInfo> Properties;
        std::vector<MethodInfo> Methods;
        
        // Parent type (inheritance)
        const TypeInfo* Parent;
        
        // Constructor/Destructor
        std::function<void*(void)> Constructor;
        std::function<void(void*)> Destructor;
        
        // Helper methods
        const PropertyInfo* GetProperty(const std::string& name) const;
        const MethodInfo* GetMethod(const std::string& name) const;
        bool HasProperty(const std::string& name) const;
        bool HasMethod(const std::string& name) const;
    };

    // Type registry (singleton)
    class CQ_API TypeRegistry
    {
    public:
        static TypeRegistry& Get();
        
        void RegisterType(const TypeInfo& typeInfo);
        const TypeInfo* GetType(const std::string& name) const;
        const TypeInfo* GetType(std::type_index typeIndex) const;
        
        template<typename T>
        const TypeInfo* GetType() const
        {
            return GetType(std::type_index(typeid(T)));
        }
        
        bool HasType(const std::string& name) const;
        bool HasType(std::type_index typeIndex) const;
        
        std::vector<const TypeInfo*> GetAllTypes() const;
        
    private:
        TypeRegistry() = default;
        std::unordered_map<std::string, TypeInfo> m_TypesByName;
        std::unordered_map<std::type_index, TypeInfo> m_TypesByIndex;
    };

    // Type registration helper
    template<typename T>
    class TypeRegistrar
    {
    public:
        TypeRegistrar(const std::string& name)
        {
            m_TypeInfo.Name = name;
            m_TypeInfo.TypeIndex = std::type_index(typeid(T));
            m_TypeInfo.Size = sizeof(T);
            
            // Default constructor
            if constexpr (std::is_default_constructible_v<T>)
            {
                m_TypeInfo.Constructor = []() -> void* {
                    return new T();
                };
            }
            
            // Destructor
            m_TypeInfo.Destructor = [](void* ptr) {
                delete static_cast<T*>(ptr);
            };
        }
        
        ~TypeRegistrar()
        {
            TypeRegistry::Get().RegisterType(m_TypeInfo);
        }
        
        template<typename PropType>
        TypeRegistrar& Property(const std::string& name, PropType T::*member, 
                                PropertyFlags flags = PropertyFlags::Serializable | PropertyFlags::EditorVisible)
        {
            PropertyInfo prop;
            prop.Name = name;
            prop.TypeIndex = std::type_index(typeid(PropType));
            prop.Offset = 0; // Offset hesaplaması member pointer ile yapılamaz
            prop.Size = sizeof(PropType);
            prop.Flags = flags;
            prop.Type = DeducePropertyType<PropType>();
            
            // Getter
            prop.Getter = [member](const void* obj) -> std::any {
                const T* instance = static_cast<const T*>(obj);
                return instance->*member;
            };
            
            // Setter
            prop.Setter = [member](void* obj, const std::any& value) {
                T* instance = static_cast<T*>(obj);
                instance->*member = std::any_cast<PropType>(value);
            };
            
            m_TypeInfo.Properties.push_back(prop);
            return *this;
        }
        
        template<typename... Args>
        TypeRegistrar& Method(const std::string& name, auto (T::*method)(Args...))
        {
            MethodInfo methodInfo;
            methodInfo.Name = name;
            // TODO: Method reflection implementation
            m_TypeInfo.Methods.push_back(methodInfo);
            return *this;
        }
        
        TypeRegistrar& Parent(const TypeInfo* parent)
        {
            m_TypeInfo.Parent = parent;
            return *this;
        }
        
    private:
        template<typename PropType>
        static PropertyType DeducePropertyType()
        {
            if constexpr (std::is_same_v<PropType, bool>) return PropertyType::Bool;
            else if constexpr (std::is_same_v<PropType, int8_t>) return PropertyType::Int8;
            else if constexpr (std::is_same_v<PropType, int16_t>) return PropertyType::Int16;
            else if constexpr (std::is_same_v<PropType, int32_t>) return PropertyType::Int32;
            else if constexpr (std::is_same_v<PropType, int64_t>) return PropertyType::Int64;
            else if constexpr (std::is_same_v<PropType, uint8_t>) return PropertyType::UInt8;
            else if constexpr (std::is_same_v<PropType, uint16_t>) return PropertyType::UInt16;
            else if constexpr (std::is_same_v<PropType, uint32_t>) return PropertyType::UInt32;
            else if constexpr (std::is_same_v<PropType, uint64_t>) return PropertyType::UInt64;
            else if constexpr (std::is_same_v<PropType, float>) return PropertyType::Float;
            else if constexpr (std::is_same_v<PropType, double>) return PropertyType::Double;
            else if constexpr (std::is_same_v<PropType, std::string>) return PropertyType::String;
            else if constexpr (std::is_enum_v<PropType>) return PropertyType::Enum;
            else if constexpr (std::is_pointer_v<PropType>) return PropertyType::Pointer;
            else return PropertyType::Custom;
        }
        
        TypeInfo m_TypeInfo;
    };
}

// Modern reflection macros
#define CQ_REFLECT_TYPE(TypeName) \
    namespace { \
        static Conqueror::Reflection::TypeRegistrar<TypeName> s_##TypeName##_Registrar(#TypeName); \
    }

#define CQ_REFLECT_BEGIN(TypeName) \
    namespace { \
        struct TypeName##_ReflectionRegistrar { \
            TypeName##_ReflectionRegistrar() { \
                using T = TypeName; \
                Conqueror::Reflection::TypeRegistrar<T> registrar(#TypeName);

#define CQ_PROPERTY(PropName, ...) \
                registrar.Property(#PropName, &T::PropName, ##__VA_ARGS__);

#define CQ_METHOD(MethodName) \
                registrar.Method(#MethodName, &T::MethodName);

#define CQ_REFLECT_END() \
            } \
        }; \
        static TypeName##_ReflectionRegistrar s_##TypeName##_Registrar; \
    }
