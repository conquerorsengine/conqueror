#include "CQSValue.h"
#include "CQSJit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace Conqueror::CQS
{
    // ════════════════════════════════════════
    //  Objects
    // ════════════════════════════════════════
    CQSObject::CQSObject(ObjectType type)
        : Type(type)
    {
        CQSHeap::Get().Register(this);
    }

    CQSFunctionObject::~CQSFunctionObject()
    {
        if (JitCode)
            CQSJit::FreeCode(JitCode);
    }

    CQSInstanceObject::CQSInstanceObject(CQSClassObject* cls)
        : CQSObject(ObjectType::Instance), ClassName(cls ? cls->Name : "Object"), Class(cls)
    {
        if (cls)
            Slots.resize(cls->FieldNames.size());
    }

    Value Value::MakeString(const std::string& val)
    {
        return MakeObject(new CQSStringObject(val));
    }

    // ════════════════════════════════════════
    //  Value helpers
    // ════════════════════════════════════════
    bool Value::IsTruthy() const
    {
        switch (Type)
        {
            case ValueType::Null:   return false;
            case ValueType::Bool:   return BoolVal;
            case ValueType::Int:    return IntVal != 0;
            case ValueType::Float:  return FloatVal != 0.0;
            case ValueType::Object:
                if (!ObjectVal) return false;
                if (ObjectVal->Type == ObjectType::String) return !AsString()->Value.empty();
                return true;
            default: return false;
        }
    }

    bool Value::Equals(const Value& other) const
    {
        if (Type != other.Type)
        {
            if (IsNumber() && other.IsNumber())
                return ToNumber() == other.ToNumber();
            return false;
        }
        switch (Type)
        {
            case ValueType::Null:   return true;
            case ValueType::Bool:   return BoolVal == other.BoolVal;
            case ValueType::Int:    return IntVal == other.IntVal;
            case ValueType::Float:  return FloatVal == other.FloatVal;
            case ValueType::Object:
                if (ObjectVal == other.ObjectVal) return true;
                if (!ObjectVal || !other.ObjectVal) return false;
                if (ObjectVal->Type == ObjectType::String && other.ObjectVal->Type == ObjectType::String)
                    return AsString()->Value == other.AsString()->Value;
                if (ObjectVal->Type == ObjectType::Range && other.ObjectVal->Type == ObjectType::Range)
                    return AsRange()->Start == other.AsRange()->Start && AsRange()->End == other.AsRange()->End;
                return false;
            default: return false;
        }
    }

    static std::string FormatFloat(double v)
    {
        if (std::isnan(v)) return "NaN";
        if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
        char buf[64];
        // En kısa gidiş-dönüş gösterimi (%.17g gereksiz basamak üretir)
        for (int precision = 6; precision <= 17; ++precision)
        {
            std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
            if (std::strtod(buf, nullptr) == v)
                break;
        }
        std::string s = buf;
        if (s.find_first_of(".eEn") == std::string::npos)
            s += ".0";
        return s;
    }

    static void AppendValue(std::string& out, const Value& v, int depth);

    static void AppendQuoted(std::string& out, const Value& v, int depth)
    {
        if (v.IsString())
        {
            out += '"';
            out += v.AsString()->Value;
            out += '"';
        }
        else
            AppendValue(out, v, depth);
    }

    static void AppendValue(std::string& out, const Value& v, int depth)
    {
        switch (v.Type)
        {
            case ValueType::Null:  out += "null"; return;
            case ValueType::Bool:  out += v.BoolVal ? "true" : "false"; return;
            case ValueType::Int:   out += std::to_string(v.IntVal); return;
            case ValueType::Float: out += FormatFloat(v.FloatVal); return;
            case ValueType::Object: break;
            default: out += "<unknown>"; return;
        }
        if (!v.ObjectVal) { out += "null"; return; }
        if (depth > 8) { out += "..."; return; }
        switch (v.ObjectVal->Type)
        {
            case ObjectType::String: out += v.AsString()->Value; return;
            case ObjectType::Function:
            {
                const auto* fn = v.AsFunction();
                out += fn->Name.empty() ? "<script>" : "<func " + fn->Name + ">";
                return;
            }
            case ObjectType::Closure:
            {
                const auto* fn = v.AsClosure()->Function;
                out += fn->Name.empty() ? "<func>" : "<func " + fn->Name + ">";
                return;
            }
            case ObjectType::NativeFunction: out += "<native " + v.AsNativeFunction()->Name + ">"; return;
            case ObjectType::BoundMethod:
            {
                auto* bm = static_cast<CQSBoundMethodObject*>(v.ObjectVal);
                out += "<method ";
                if (bm->Method.IsObjType(ObjectType::Closure)) out += bm->Method.AsClosure()->Function->Name;
                else if (bm->Method.IsNativeFunction()) out += bm->Method.AsNativeFunction()->Name;
                out += ">";
                return;
            }
            case ObjectType::Class: out += "<class " + v.AsClass()->Name + ">"; return;
            case ObjectType::Instance:
            {
                auto* inst = v.AsInstance();
                // ToString() metodu VM tarafından çağrılır; burada yalnızca alanlar
                out += inst->ClassName;
                out += " {";
                bool first = true;
                if (inst->Class)
                {
                    for (size_t i = 0; i < inst->Class->FieldNames.size() && i < inst->Slots.size(); ++i)
                    {
                        out += first ? " " : ", ";
                        first = false;
                        out += inst->Class->FieldNames[i];
                        out += ": ";
                        AppendQuoted(out, inst->Slots[i], depth + 1);
                    }
                }
                for (auto& [k, val] : inst->Fields)
                {
                    out += first ? " " : ", ";
                    first = false;
                    out += k + ": ";
                    AppendQuoted(out, val, depth + 1);
                }
                out += first ? "}" : " }";
                return;
            }
            case ObjectType::List:
            {
                auto* list = v.AsList();
                out += "[";
                for (size_t i = 0; i < list->Elements.size(); i++)
                {
                    if (i > 0) out += ", ";
                    AppendQuoted(out, list->Elements[i], depth + 1);
                }
                out += "]";
                return;
            }
            case ObjectType::Map:
            {
                auto* map = v.AsMap();
                // Deterministik çıktı için anahtarları sırala
                std::vector<const std::string*> keys;
                keys.reserve(map->Entries.size());
                for (auto& [key, val] : map->Entries) keys.push_back(&key);
                std::sort(keys.begin(), keys.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
                out += "{";
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    if (i > 0) out += ", ";
                    out += *keys[i];
                    out += ": ";
                    AppendQuoted(out, map->Entries.at(*keys[i]), depth + 1);
                }
                out += "}";
                return;
            }
            case ObjectType::Range:
                out += std::to_string(v.AsRange()->Start) + ".." + std::to_string(v.AsRange()->End);
                return;
            case ObjectType::Upvalue: out += "<upvalue>"; return;
        }
        out += "<object>";
    }

    std::string Value::ToString() const
    {
        std::string out;
        AppendValue(out, *this, 0);
        return out;
    }

    std::string Value::TypeName() const
    {
        switch (Type)
        {
            case ValueType::Null:  return "null";
            case ValueType::Bool:  return "bool";
            case ValueType::Int:   return "int";
            case ValueType::Float: return "float";
            case ValueType::Object: break;
            default: return "unknown";
        }
        if (!ObjectVal) return "null";
        switch (ObjectVal->Type)
        {
            case ObjectType::String: return "string";
            case ObjectType::Function:
            case ObjectType::Closure:
            case ObjectType::BoundMethod:
            case ObjectType::NativeFunction: return "function";
            case ObjectType::Instance: return AsInstance()->ClassName;
            case ObjectType::List: return "List";
            case ObjectType::Map: return "Map";
            case ObjectType::Class: return "Class";
            case ObjectType::Range: return "Range";
            case ObjectType::Upvalue: return "upvalue";
        }
        return "object";
    }

    // ════════════════════════════════════════
    //  Heap
    // ════════════════════════════════════════
    CQSHeap& CQSHeap::Get()
    {
        // Bilerek yok edilmez: statik VM'ler (CQSEngine) süreç sonunda yığından sonra yıkılabilir
        static CQSHeap* s_Heap = new CQSHeap();
        return *s_Heap;
    }

    void CQSHeap::Register(CQSObject* obj)
    {
        obj->NextObject = m_Objects;
        m_Objects = obj;
        ++m_ObjectCount;
        ++m_SinceGC;
    }

    void CQSHeap::MarkObject(CQSObject* obj)
    {
        if (!obj || obj->IsMarked)
            return;
        obj->IsMarked = true;
        m_Gray.push_back(obj);
    }

    void CQSHeap::MarkValue(const Value& v)
    {
        if (v.Type == ValueType::Object)
            MarkObject(v.ObjectVal);
    }

    void CQSHeap::MarkPersistentRoots()
    {
        for (const Value& v : m_PersistentRoots)
            MarkValue(v);
    }

    void CQSHeap::TraceReferences()
    {
        while (!m_Gray.empty())
        {
            CQSObject* obj = m_Gray.back();
            m_Gray.pop_back();
            switch (obj->Type)
            {
                case ObjectType::String:
                case ObjectType::NativeFunction:
                case ObjectType::Range:
                    break;
                case ObjectType::Function:
                {
                    auto* fn = static_cast<CQSFunctionObject*>(obj);
                    for (const Value& c : fn->Constants) MarkValue(c);
                    MarkObject(fn->OwnerClass);
                    break;
                }
                case ObjectType::Closure:
                {
                    auto* cl = static_cast<CQSClosureObject*>(obj);
                    MarkObject(cl->Function);
                    for (auto* up : cl->Upvalues) MarkObject(up);
                    break;
                }
                case ObjectType::Upvalue:
                    MarkValue(static_cast<CQSUpvalueObject*>(obj)->Closed);
                    break;
                case ObjectType::Class:
                {
                    auto* cls = static_cast<CQSClassObject*>(obj);
                    MarkObject(cls->Base);
                    for (auto& [k, v] : cls->Methods) MarkValue(v);
                    for (auto& [k, v] : cls->Statics) MarkValue(v);
                    for (auto& f : cls->OwnFields) MarkValue(f.DefaultValue);
                    MarkValue(cls->FieldInitializer);
                    MarkValue(cls->Constructor);
                    break;
                }
                case ObjectType::Instance:
                {
                    auto* inst = static_cast<CQSInstanceObject*>(obj);
                    MarkObject(inst->Class);
                    for (const Value& v : inst->Slots) MarkValue(v);
                    for (auto& [k, v] : inst->Fields) MarkValue(v);
                    break;
                }
                case ObjectType::BoundMethod:
                {
                    auto* bm = static_cast<CQSBoundMethodObject*>(obj);
                    MarkValue(bm->Receiver);
                    MarkValue(bm->Method);
                    break;
                }
                case ObjectType::List:
                    for (const Value& v : static_cast<CQSListObject*>(obj)->Elements) MarkValue(v);
                    break;
                case ObjectType::Map:
                    for (auto& [k, v] : static_cast<CQSMapObject*>(obj)->Entries) MarkValue(v);
                    break;
            }
        }
    }

    size_t CQSHeap::Sweep()
    {
        size_t freed = 0;
        CQSObject* previous = nullptr;
        CQSObject* obj = m_Objects;
        while (obj)
        {
            if (obj->IsMarked)
            {
                obj->IsMarked = false;
                previous = obj;
                obj = obj->NextObject;
            }
            else
            {
                CQSObject* unreached = obj;
                obj = obj->NextObject;
                if (previous) previous->NextObject = obj;
                else m_Objects = obj;
                delete unreached;
                ++freed;
            }
        }
        m_ObjectCount -= freed;
        m_SinceGC = 0;
        m_Threshold = std::max<size_t>(200000, m_ObjectCount * 2);
        return freed;
    }

    void CQSHeap::FreeAll()
    {
        CQSObject* obj = m_Objects;
        while (obj)
        {
            CQSObject* next = obj->NextObject;
            delete obj;
            obj = next;
        }
        m_Objects = nullptr;
        m_ObjectCount = 0;
        m_SinceGC = 0;
        m_PersistentRoots.clear();
    }
}
