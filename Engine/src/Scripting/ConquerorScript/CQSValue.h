#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <variant>

namespace Conqueror::CQS
{
    // Forward declarations
    class CQSInstance;
    class CQSFunction;
    struct CQSChunk;

    // ── Value Type Tag ──
    enum class ValueType : uint8_t
    {
        Null,
        Bool,
        Int,
        Float,
        String,
        Object      // Heap-allocated: Instance, List, Map, Function, etc.
    };

    // ── Object Types (heap-allocated) ──
    enum class ObjectType : uint8_t
    {
        String,
        Function,
        NativeFunction,
        Instance,
        List,
        Map
    };

    // ── Base Object (heap-allocated reference type) ──
    struct CQSObject
    {
        ObjectType Type;
        bool       IsMarked = false;    // GC için

        CQSObject(ObjectType type) : Type(type) {}
        virtual ~CQSObject() = default;
    };

    // ── String Object ──
    struct CQSStringObject : CQSObject
    {
        std::string Value;

        CQSStringObject(const std::string& value)
            : CQSObject(ObjectType::String), Value(value) {}
    };

    // ── Function Object (bytecode fonksiyon) ──
    struct CQSFunctionObject : CQSObject
    {
        std::string             Name;
        int                     Arity = 0;          // Parametre sayısı
        std::shared_ptr<CQSChunk> ChunkData;        // Bytecode
        int                     UpvalueCount = 0;

        CQSFunctionObject()
            : CQSObject(ObjectType::Function) {}

        CQSFunctionObject(const std::string& name, int arity)
            : CQSObject(ObjectType::Function), Name(name), Arity(arity) {}
    };

    // ── Native Function (C++ fonksiyon) ──
    class Value; // Forward declare
    using NativeFn = std::function<Value(int argCount, Value* args)>;

    struct CQSNativeFunctionObject : CQSObject
    {
        std::string Name;
        int         Arity;
        NativeFn    Function;

        CQSNativeFunctionObject(const std::string& name, int arity, NativeFn fn)
            : CQSObject(ObjectType::NativeFunction), Name(name), Arity(arity), Function(std::move(fn)) {}
    };

    // ── Instance Object (script/class instance) ──
    struct CQSInstanceObject : CQSObject
    {
        std::string                             ClassName;
        std::unordered_map<std::string, Value>  Fields;
        uint32_t                                EntityHandle = 0xFFFFFFFF; // entt::null

        CQSInstanceObject(const std::string& className)
            : CQSObject(ObjectType::Instance), ClassName(className) {}
    };

    // ── List Object ──
    struct CQSListObject : CQSObject
    {
        std::vector<Value> Elements;

        CQSListObject() : CQSObject(ObjectType::List) {}
    };

    // ── Map Object ──
    struct CQSMapObject : CQSObject
    {
        std::unordered_map<std::string, Value> Entries;

        CQSMapObject() : CQSObject(ObjectType::Map) {}
    };

    // ── Runtime Value ──
    // Stack-based VM'de her slot bir Value tutar
    class Value
    {
    public:
        ValueType Type;

        union
        {
            bool        BoolVal;
            int64_t     IntVal;
            double      FloatVal;
            CQSObject*  ObjectVal;
        };

        // ── Constructors ──
        Value() : Type(ValueType::Null), IntVal(0) {}

        static Value MakeNull()                     { Value v; v.Type = ValueType::Null; v.IntVal = 0; return v; }
        static Value MakeBool(bool val)             { Value v; v.Type = ValueType::Bool; v.BoolVal = val; return v; }
        static Value MakeInt(int64_t val)            { Value v; v.Type = ValueType::Int; v.IntVal = val; return v; }
        static Value MakeFloat(double val)           { Value v; v.Type = ValueType::Float; v.FloatVal = val; return v; }
        static Value MakeObject(CQSObject* obj)      { Value v; v.Type = ValueType::Object; v.ObjectVal = obj; return v; }
        static Value MakeString(const std::string& val) { return MakeObject(new CQSStringObject(val)); }

        // ── Type Checks ──
        bool IsNull()   const { return Type == ValueType::Null; }
        bool IsBool()   const { return Type == ValueType::Bool; }
        bool IsInt()    const { return Type == ValueType::Int; }
        bool IsFloat()  const { return Type == ValueType::Float; }
        bool IsNumber() const { return Type == ValueType::Int || Type == ValueType::Float; }
        bool IsObject() const { return Type == ValueType::Object; }

        bool IsString()         const { return IsObject() && ObjectVal->Type == ObjectType::String; }
        bool IsFunction()       const { return IsObject() && ObjectVal->Type == ObjectType::Function; }
        bool IsNativeFunction() const { return IsObject() && ObjectVal->Type == ObjectType::NativeFunction; }
        bool IsInstance()       const { return IsObject() && ObjectVal->Type == ObjectType::Instance; }
        bool IsList()           const { return IsObject() && ObjectVal->Type == ObjectType::List; }
        bool IsMap()            const { return IsObject() && ObjectVal->Type == ObjectType::Map; }

        // ── Value Accessors ──
        bool                    AsBool()      const { return BoolVal; }
        int64_t                 AsInt()        const { return IntVal; }
        double                  AsFloat()      const { return FloatVal; }
        CQSObject*              AsObject()     const { return ObjectVal; }
        CQSStringObject*        AsString()     const { return static_cast<CQSStringObject*>(ObjectVal); }
        CQSFunctionObject*      AsFunction()   const { return static_cast<CQSFunctionObject*>(ObjectVal); }
        CQSNativeFunctionObject* AsNativeFunction() const { return static_cast<CQSNativeFunctionObject*>(ObjectVal); }
        CQSInstanceObject*      AsInstance()   const { return static_cast<CQSInstanceObject*>(ObjectVal); }
        CQSListObject*          AsList()       const { return static_cast<CQSListObject*>(ObjectVal); }
        CQSMapObject*           AsMap()        const { return static_cast<CQSMapObject*>(ObjectVal); }

        // ── Numeric Conversion ──
        double ToNumber() const
        {
            if (Type == ValueType::Int) return static_cast<double>(IntVal);
            if (Type == ValueType::Float) return FloatVal;
            return 0.0;
        }

        // ── Truthiness ──
        bool IsTruthy() const
        {
            switch (Type)
            {
                case ValueType::Null:   return false;
                case ValueType::Bool:   return BoolVal;
                case ValueType::Int:    return IntVal != 0;
                case ValueType::Float:  return FloatVal != 0.0;
                case ValueType::String: return !AsString()->Value.empty();
                default:                return true; // Objects are truthy
            }
        }

        // ── Equality ──
        bool Equals(const Value& other) const
        {
            if (Type != other.Type) 
            {
                if (IsNumber() && other.IsNumber()) return ToNumber() == other.ToNumber();
                return false;
            }
            switch (Type)
            {
                case ValueType::Null:   return true;
                case ValueType::Bool:   return BoolVal == other.BoolVal;
                case ValueType::Int:    return IntVal == other.IntVal;
                case ValueType::Float:  return FloatVal == other.FloatVal;
                case ValueType::Object:
                    if (IsString() && other.IsString())
                        return AsString()->Value == other.AsString()->Value;
                    return ObjectVal == other.ObjectVal; // Reference equality
                default: return false;
            }
        }

        bool operator==(const Value& other) const { return Equals(other); }

        // ── Debug: String representation ──
        std::string ToString() const;
    };
}
