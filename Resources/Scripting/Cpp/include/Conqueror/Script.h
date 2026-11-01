// Conqueror Engine C++ betik API'si (yalnızca başlık; motor kitaplığına bağlanmaz)
// ------------------------------------------------------------------------------
//   #include <Conqueror/Script.h>
//
//   class Rotator : public cq::Script
//   {
//       CQ_SCRIPT_CLASS(Rotator)
//   public:
//       CQ_FIELD(float, speed, 90.0f)              // Inspector alanı
//       void Start() override { cq::Log("ready"); }
//       void Update(float dt) override
//       {
//           cq::Value r = cq::GetRotation();
//           cq::SetRotation(r.x(), r.y() + speed * dt, r.z());
//       }
//       void OnPlayClicked(float value, const char* kind) { }
//       CQ_METHOD(OnPlayClicked)                    // UI olayları / özel metotlar
//   };
//   CQ_SCRIPT(Rotator)
//
// Motor ile sınır yalnızca C ABI'dır: farklı derleyici/STL sürümleri güvenlidir.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define CQ_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#else
#define CQ_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C"
{
    // Motorun CQNativeValue yapısıyla birebir aynı (64 bayt)
    struct CQNativeValue
    {
        int32_t Type;     // 0 null, 1 bool, 2 int, 3 float, 4 string, 5 vector, 6 json
        int32_t Dim;
        int64_t I;
        double X, Y, Z, W;
        const char* Str;
        int32_t StrLen;
        int32_t Pad;
    };

    struct CQHostApi
    {
        int Version;
        int (*FindNative)(const char* name);
        int (*CallNative)(int index, int argc, const CQNativeValue* args, CQNativeValue* result);
        void (*Log)(int level, const char* message);
    };
}

namespace cq
{
    namespace detail
    {
        inline const CQHostApi*& Host()
        {
            static const CQHostApi* s_Host = nullptr;
            return s_Host;
        }
    }

    // Motorla değiş tokuş edilen değer: sayı, metin, vektör ya da JSON (liste/harita)
    class Value
    {
    public:
        enum class Kind { Null, Bool, Int, Float, String, Vector, Json };

        Value() = default;
        Value(bool b) : m_Kind(Kind::Bool), m_Int(b ? 1 : 0) {}
        Value(int i) : m_Kind(Kind::Int), m_Int(i) {}
        Value(long i) : m_Kind(Kind::Int), m_Int(i) {}
        Value(long long i) : m_Kind(Kind::Int), m_Int(i) {}
        Value(unsigned int i) : m_Kind(Kind::Int), m_Int(i) {}
        Value(float f) : m_Kind(Kind::Float), m_X(f) {}
        Value(double d) : m_Kind(Kind::Float), m_X(d) {}
        Value(const char* s) : m_Kind(s ? Kind::String : Kind::Null), m_Str(s ? s : "") {}
        Value(std::string s) : m_Kind(Kind::String), m_Str(std::move(s)) {}

        static Value Vector(double x, double y, double z = 0.0, double w = 0.0, int dim = 3)
        {
            Value v;
            v.m_Kind = Kind::Vector;
            v.m_X = x; v.m_Y = y; v.m_Z = z; v.m_W = w; v.m_Dim = dim;
            return v;
        }
        static Value Json(std::string json)
        {
            Value v;
            v.m_Kind = Kind::Json;
            v.m_Str = std::move(json);
            return v;
        }

        Kind GetKind() const { return m_Kind; }
        bool IsNull() const { return m_Kind == Kind::Null; }
        double AsDouble() const
        {
            switch (m_Kind)
            {
                case Kind::Bool: case Kind::Int: return (double)m_Int;
                case Kind::Float: return m_X;
                case Kind::String: return std::atof(m_Str.c_str());
                default: return 0.0;
            }
        }
        float AsFloat() const { return (float)AsDouble(); }
        int AsInt() const { return m_Kind == Kind::Int || m_Kind == Kind::Bool ? (int)m_Int : (int)AsDouble(); }
        int64_t AsLong() const { return m_Kind == Kind::Int || m_Kind == Kind::Bool ? m_Int : (int64_t)AsDouble(); }
        bool AsBool() const { return m_Kind == Kind::String ? !m_Str.empty() : (m_Kind == Kind::Float ? m_X != 0.0 : m_Int != 0); }
        const std::string& AsString() const { return m_Str; }
        // Vektör bileşenleri (vektör olmayan değerlerde 0)
        double x() const { return m_Kind == Kind::Vector ? m_X : 0.0; }
        double y() const { return m_Kind == Kind::Vector ? m_Y : 0.0; }
        double z() const { return m_Kind == Kind::Vector ? m_Z : 0.0; }
        double w() const { return m_Kind == Kind::Vector ? m_W : 0.0; }
        int Dimension() const { return m_Dim; }
        // Liste/harita için JSON metni
        const std::string& JsonText() const { return m_Str; }

        operator double() const { return AsDouble(); }
        operator bool() const { return AsBool(); }

        // ── C ABI dönüşümü ──
        void ToNative(CQNativeValue& n) const
        {
            std::memset(&n, 0, sizeof(n));
            switch (m_Kind)
            {
                case Kind::Null: break;
                case Kind::Bool: n.Type = 1; n.I = m_Int; break;
                case Kind::Int: n.Type = 2; n.I = m_Int; break;
                case Kind::Float: n.Type = 3; n.X = m_X; break;
                case Kind::String: n.Type = 4; n.Str = m_Str.c_str(); n.StrLen = (int32_t)m_Str.size(); break;
                case Kind::Vector: n.Type = 5; n.Dim = m_Dim; n.X = m_X; n.Y = m_Y; n.Z = m_Z; n.W = m_W; break;
                case Kind::Json: n.Type = 6; n.Str = m_Str.c_str(); n.StrLen = (int32_t)m_Str.size(); break;
            }
        }

        static Value FromNative(const CQNativeValue& n)
        {
            switch (n.Type)
            {
                case 1: return Value(n.I != 0);
                case 2: return Value((long long)n.I);
                case 3: return Value(n.X);
                case 4: return Value(std::string(n.Str ? n.Str : "", (size_t)n.StrLen));
                case 5: return Vector(n.X, n.Y, n.Z, n.W, n.Dim);
                case 6: return Json(std::string(n.Str ? n.Str : "null", n.Str ? (size_t)n.StrLen : 4));
                default: return Value();
            }
        }

        std::string ToString() const
        {
            char buf[128];
            switch (m_Kind)
            {
                case Kind::Null: return "null";
                case Kind::Bool: return m_Int ? "true" : "false";
                case Kind::Int: std::snprintf(buf, sizeof(buf), "%lld", (long long)m_Int); return buf;
                case Kind::Float: std::snprintf(buf, sizeof(buf), "%g", m_X); return buf;
                case Kind::String: case Kind::Json: return m_Str;
                case Kind::Vector:
                    if (m_Dim == 2) std::snprintf(buf, sizeof(buf), "(%g, %g)", m_X, m_Y);
                    else if (m_Dim == 4) std::snprintf(buf, sizeof(buf), "(%g, %g, %g, %g)", m_X, m_Y, m_Z, m_W);
                    else std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", m_X, m_Y, m_Z);
                    return buf;
            }
            return "";
        }

    private:
        Kind m_Kind = Kind::Null;
        int64_t m_Int = 0;
        double m_X = 0, m_Y = 0, m_Z = 0, m_W = 0;
        int m_Dim = 0;
        std::string m_Str;
    };

    // nativebridge fonksiyonunu adıyla çağır (dizin önbelleğe alınır)
    inline Value CallNative(int index, std::initializer_list<Value> args)
    {
        const CQHostApi* host = detail::Host();
        if (!host || index < 0) return Value();
        CQNativeValue native[16];
        int argc = 0;
        for (const Value& a : args)
            if (argc < 16) a.ToNative(native[argc++]);
        CQNativeValue result{};
        if (!host->CallNative(index, argc, native, &result)) return Value();
        return Value::FromNative(result);   // dizeler hemen kopyalanır
    }

    inline int ResolveNative(const char* name)
    {
        const CQHostApi* host = detail::Host();
        return host ? host->FindNative(name) : -1;
    }

    inline void Log(const std::string& message) { if (detail::Host()) detail::Host()->Log(0, message.c_str()); }
    inline void LogWarning(const std::string& message) { if (detail::Host()) detail::Host()->Log(1, message.c_str()); }
    inline void LogError(const std::string& message) { if (detail::Host()) detail::Host()->Log(2, message.c_str()); }

    // Varlığa takılan C++ betiklerinin temel sınıfı
    class Script
    {
    public:
        virtual ~Script() = default;
        virtual void Awake() {}
        virtual void Start() {}
        virtual void Update(float dt) { (void)dt; }
        virtual void LateUpdate(float dt) { (void)dt; }
        virtual void OnDestroy() {}

        uint32_t entity = 0xFFFFFFFFu;
    };

    namespace detail
    {
        enum class FieldType { Bool, Int, Float, Double, String };

        template<typename T> constexpr FieldType FieldTypeOf()
        {
            if constexpr (std::is_same_v<T, bool>) return FieldType::Bool;
            else if constexpr (std::is_same_v<T, float>) return FieldType::Float;
            else if constexpr (std::is_same_v<T, double>) return FieldType::Double;
            else if constexpr (std::is_same_v<T, std::string>) return FieldType::String;
            else return FieldType::Int;
        }

        struct FieldInfo
        {
            std::string Name;
            FieldType Type;
            std::string Attributes;    // "Range(0, 10)" ...
            std::function<Value(Script*)> Get;
            std::function<void(Script*, const Value&)> Set;
        };

        struct MethodInfo
        {
            std::function<void(Script*, const CQNativeValue*, int)> Invoke;
        };

        struct ClassInfo
        {
            std::string Name;
            std::function<Script*()> Create;
            std::vector<FieldInfo> Fields;
            std::map<std::string, MethodInfo> Methods;
        };

        inline std::map<std::string, ClassInfo>& Classes()
        {
            static std::map<std::string, ClassInfo> s_Classes;
            return s_Classes;
        }

        template<typename T>
        Value ToValue(const T& v)
        {
            if constexpr (std::is_same_v<T, std::string>) return Value(v);
            else if constexpr (std::is_same_v<T, bool>) return Value(v);
            else if constexpr (std::is_floating_point_v<T>) return Value((double)v);
            else return Value((long long)v);
        }

        template<typename T>
        T FromValue(const Value& v)
        {
            if constexpr (std::is_same_v<T, std::string>) return v.GetKind() == Value::Kind::String ? v.AsString() : v.ToString();
            else if constexpr (std::is_same_v<T, bool>) return v.AsBool();
            else if constexpr (std::is_floating_point_v<T>) return (T)v.AsDouble();
            else return (T)v.AsLong();
        }

        template<typename C, typename T>
        struct FieldRegistrar
        {
            FieldRegistrar(const char* className, const char* name, T C::*member, const char* attributes)
            {
                auto& cls = Classes()[className];
                cls.Name = className;
                FieldInfo f;
                f.Name = name;
                f.Type = FieldTypeOf<T>();
                f.Attributes = attributes ? attributes : "";
                f.Get = [member](Script* s) { return ToValue(static_cast<C*>(s)->*member); };
                f.Set = [member](Script* s, const Value& v) { static_cast<C*>(s)->*member = FromValue<T>(v); };
                cls.Fields.push_back(std::move(f));
            }
        };

        template<typename Arg>
        auto ArgAs(const CQNativeValue* args, int argc, int i)
        {
            using A = std::decay_t<Arg>;
            Value v = i < argc ? Value::FromNative(args[i]) : Value();
            if constexpr (std::is_same_v<A, Value>) return v;
            else if constexpr (std::is_same_v<A, const char*>) return std::string(v.AsString());   // ömür: çağrı süresince
            else return FromValue<A>(v);
        }

        template<typename C, typename... Args, std::size_t... I>
        void CallMember(C* obj, void (C::*fn)(Args...), const CQNativeValue* args, int argc, std::index_sequence<I...>)
        {
            auto tuple = std::make_tuple(ArgAs<Args>(args, argc, (int)I)...);
            auto pass = [](auto& a) -> decltype(auto) {
                if constexpr (std::is_same_v<std::decay_t<decltype(a)>, std::string>) return a.c_str();
                else return a;
            };
            (obj->*fn)(pass(std::get<I>(tuple))...);
        }

        template<typename C, typename... Args>
        struct MethodRegistrar
        {
            MethodRegistrar(const char* className, const char* name, void (C::*fn)(Args...))
            {
                auto& cls = Classes()[className];
                cls.Name = className;
                cls.Methods[name].Invoke = [fn](Script* s, const CQNativeValue* args, int argc) {
                    CallMember(static_cast<C*>(s), fn, args, argc, std::index_sequence_for<Args...>{});
                };
            }
        };

        template<typename C>
        struct ClassRegistrar
        {
            explicit ClassRegistrar(const char* name)
            {
                auto& cls = Classes()[name];
                cls.Name = name;
                cls.Create = []() -> Script* { return new C(); };
            }
        };
    }
}

// Sınıf gövdesinin başına: alan/metot kayıtları için tür adı
#define CQ_SCRIPT_CLASS(Class) \
    using ThisScriptClass = Class; \
    static constexpr const char* ThisScriptName = #Class;

// Inspector alanı: tür, ad, varsayılan
#define CQ_FIELD(Type, Name, Default) \
    Type Name = Default; \
    static inline ::cq::detail::FieldRegistrar<ThisScriptClass, Type> s_CQField_##Name{ ThisScriptName, #Name, &ThisScriptClass::Name, "" };

// Aralıklı alan: Inspector'da kaydırıcı
#define CQ_FIELD_RANGE(Type, Name, Default, Min, Max) \
    Type Name = Default; \
    static inline ::cq::detail::FieldRegistrar<ThisScriptClass, Type> s_CQField_##Name{ ThisScriptName, #Name, &ThisScriptClass::Name, "Range(" #Min ", " #Max ")" };

// UI olayları ve özel çağrılar için metot kaydı
#define CQ_METHOD(Name) \
    static inline ::cq::detail::MethodRegistrar s_CQMethod_##Name{ ThisScriptName, #Name, &ThisScriptClass::Name };

// Dosya sonunda: sınıfı motora kaydet
#define CQ_SCRIPT(Class) \
    static ::cq::detail::ClassRegistrar<Class> s_CQClassRegistrar_##Class{ #Class };

// ── Eklenti giriş noktaları (motorun ürettiği tek bir dosyada tanımlanır) ──
#ifdef CQ_PLUGIN_IMPLEMENTATION
namespace cq::detail
{
    inline thread_local std::string t_Error;
    inline thread_local std::string t_Result;

    inline std::string JsonEscape(const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += c;
        }
        return o;
    }

    struct Instance
    {
        Script* Object;
        ClassInfo* Class;
    };
}

CQ_SCRIPT_EXPORT int cq_plugin_version() { return 1; }

CQ_SCRIPT_EXPORT void cq_plugin_init(const CQHostApi* api) { cq::detail::Host() = api; }

CQ_SCRIPT_EXPORT const char* cq_plugin_describe()
{
    using namespace cq::detail;
    std::string& out = t_Result;
    out = "[";
    bool firstClass = true;
    for (auto& [name, cls] : Classes())
    {
        if (!cls.Create) continue;
        if (!firstClass) out += ",";
        firstClass = false;
        out += "{\"name\":\"" + JsonEscape(name) + "\",\"fields\":[";
        std::unique_ptr<cq::Script> probe(cls.Create());
        for (size_t i = 0; i < cls.Fields.size(); ++i)
        {
            auto& f = cls.Fields[i];
            static const char* types[] = { "bool", "int", "float", "double", "string" };
            cq::Value def = f.Get(probe.get());
            std::string defJson;
            switch (f.Type)
            {
                case FieldType::Bool: defJson = def.AsBool() ? "true" : "false"; break;
                case FieldType::String: defJson = "\"" + JsonEscape(def.AsString()) + "\""; break;
                default:
                {
                    char b[64];
                    if (f.Type == FieldType::Int) std::snprintf(b, sizeof(b), "%lld", (long long)def.AsLong());
                    else std::snprintf(b, sizeof(b), "%.17g", def.AsDouble());
                    defJson = b;
                }
            }
            if (i) out += ",";
            out += "{\"name\":\"" + JsonEscape(f.Name) + "\",\"type\":\"" + types[(int)f.Type] + "\",\"default\":" + defJson + ",\"attributes\":[";
            if (!f.Attributes.empty()) out += "\"" + JsonEscape(f.Attributes) + "\"";
            out += "]}";
        }
        out += "]}";
    }
    out += "]";
    return out.c_str();
}

CQ_SCRIPT_EXPORT void* cq_create(const char* className, uint32_t entity)
{
    using namespace cq::detail;
    auto it = Classes().find(className ? className : "");
    if (it == Classes().end() || !it->second.Create)
    {
        t_Error = std::string("C++ script class '") + (className ? className : "") + "' not found (did you add CQ_SCRIPT(" + (className ? className : "") + ")?)";
        return nullptr;
    }
    try
    {
        cq::Script* s = it->second.Create();
        s->entity = entity;
        return new Instance{ s, &it->second };
    }
    catch (const std::exception& e) { t_Error = e.what(); }
    catch (...) { t_Error = "unknown C++ exception in constructor"; }
    return nullptr;
}

CQ_SCRIPT_EXPORT void cq_destroy(void* handle)
{
    auto* inst = static_cast<cq::detail::Instance*>(handle);
    if (!inst) return;
    delete inst->Object;
    delete inst;
}

CQ_SCRIPT_EXPORT int cq_has_method(void* handle, const char* name)
{
    auto* inst = static_cast<cq::detail::Instance*>(handle);
    if (!inst || !name) return 0;
    static const char* lifecycle[] = { "Awake", "Start", "Update", "LateUpdate", "OnDestroy" };
    for (const char* l : lifecycle) if (std::strcmp(l, name) == 0) return 1;
    return inst->Class->Methods.count(name) ? 1 : 0;
}

CQ_SCRIPT_EXPORT int cq_invoke(void* handle, const char* name, const CQNativeValue* args, int argc)
{
    auto* inst = static_cast<cq::detail::Instance*>(handle);
    if (!inst || !name) return 1;
    try
    {
        float dt = argc > 0 ? (float)cq::Value::FromNative(args[0]).AsDouble() : 0.0f;
        if (std::strcmp(name, "Update") == 0) inst->Object->Update(dt);
        else if (std::strcmp(name, "LateUpdate") == 0) inst->Object->LateUpdate(dt);
        else if (std::strcmp(name, "Start") == 0) inst->Object->Start();
        else if (std::strcmp(name, "Awake") == 0) inst->Object->Awake();
        else if (std::strcmp(name, "OnDestroy") == 0) inst->Object->OnDestroy();
        else
        {
            auto it = inst->Class->Methods.find(name);
            if (it != inst->Class->Methods.end()) it->second.Invoke(inst->Object, args, argc);
        }
        return 1;
    }
    catch (const std::exception& e) { cq::detail::t_Error = std::string(name) + ": " + e.what(); }
    catch (...) { cq::detail::t_Error = std::string(name) + ": unknown C++ exception"; }
    return 0;
}

CQ_SCRIPT_EXPORT int cq_set_field(void* handle, const char* name, const CQNativeValue* value)
{
    auto* inst = static_cast<cq::detail::Instance*>(handle);
    if (!inst || !name || !value) return 0;
    for (auto& f : inst->Class->Fields)
        if (f.Name == name) { f.Set(inst->Object, cq::Value::FromNative(*value)); return 1; }
    return 0;
}

// Değer + tür adı (out->Type == 4 ise dize bir sonraki çağrıya kadar geçerli)
CQ_SCRIPT_EXPORT int cq_get_field(void* handle, const char* name, CQNativeValue* out, const char** typeName)
{
    auto* inst = static_cast<cq::detail::Instance*>(handle);
    if (!inst || !name || !out) return 0;
    static const char* types[] = { "bool", "int", "float", "double", "string" };
    for (auto& f : inst->Class->Fields)
    {
        if (f.Name != name) continue;
        cq::Value v = f.Get(inst->Object);
        if (v.GetKind() == cq::Value::Kind::String)
        {
            cq::detail::t_Result = v.AsString();
            std::memset(out, 0, sizeof(*out));
            out->Type = 4;
            out->Str = cq::detail::t_Result.c_str();
            out->StrLen = (int32_t)cq::detail::t_Result.size();
        }
        else
            v.ToNative(*out);
        if (typeName) *typeName = types[(int)f.Type];
        return 1;
    }
    return 0;
}

CQ_SCRIPT_EXPORT const char* cq_last_error() { return cq::detail::t_Error.c_str(); }
#endif
