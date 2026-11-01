#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::CQS
{
    class Value;
    struct CQSClassObject;

    // ── Value Type Tag ──
    enum class ValueType : uint8_t
    {
        Null,
        Bool,
        Int,
        Float,
        String,     // kullanılmıyor (metinler Object/String nesnesidir); eski kodla uyum için duruyor
        Object      // Heap nesnesi: String, Instance, List, Map, Function, ...
    };

    // ── Object Types (heap-allocated) ──
    enum class ObjectType : uint8_t
    {
        String,
        Function,        // derlenmiş fonksiyon prototipi (bytecode)
        NativeFunction,
        Instance,
        List,
        Map,
        Closure,         // çalıştırılabilir fonksiyon (prototip + yakalanan değişkenler)
        Upvalue,
        Class,
        BoundMethod,
        Range
    };

    // ── Base Object ──
    // Tüm nesneler oluşturulduklarında CQSHeap'e kaydolur; erişilemeyenler çöp toplayıcı
    // tarafından (yalnızca VM'nin güvenli noktalarında) serbest bırakılır.
    struct CQSObject
    {
        ObjectType Type;
        bool       IsMarked = false;
        CQSObject* NextObject = nullptr;

        explicit CQSObject(ObjectType type);
        virtual ~CQSObject() = default;
        CQSObject(const CQSObject&) = delete;
        CQSObject& operator=(const CQSObject&) = delete;
    };

    // ── Runtime Value ──
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

        Value() : Type(ValueType::Null), IntVal(0) {}

        static Value MakeNull()                     { Value v; v.Type = ValueType::Null; v.IntVal = 0; return v; }
        static Value MakeBool(bool val)             { Value v; v.Type = ValueType::Bool; v.IntVal = 0; v.BoolVal = val; return v; }
        static Value MakeInt(int64_t val)           { Value v; v.Type = ValueType::Int; v.IntVal = val; return v; }
        static Value MakeFloat(double val)          { Value v; v.Type = ValueType::Float; v.FloatVal = val; return v; }
        static Value MakeObject(CQSObject* obj)     { Value v; v.Type = obj ? ValueType::Object : ValueType::Null; v.ObjectVal = obj; return v; }
        static Value MakeString(const std::string& val);

        bool IsNull()   const { return Type == ValueType::Null; }
        bool IsBool()   const { return Type == ValueType::Bool; }
        bool IsInt()    const { return Type == ValueType::Int; }
        bool IsFloat()  const { return Type == ValueType::Float; }
        bool IsNumber() const { return Type == ValueType::Int || Type == ValueType::Float; }
        bool IsObject() const { return Type == ValueType::Object; }

        bool IsObjType(ObjectType t) const { return Type == ValueType::Object && ObjectVal->Type == t; }
        bool IsString()         const { return IsObjType(ObjectType::String); }
        bool IsFunction()       const { return IsObjType(ObjectType::Closure) || IsObjType(ObjectType::Function) || IsObjType(ObjectType::BoundMethod); }
        bool IsNativeFunction() const { return IsObjType(ObjectType::NativeFunction); }
        bool IsCallable()       const { return IsFunction() || IsNativeFunction() || IsObjType(ObjectType::Class); }
        bool IsInstance()       const { return IsObjType(ObjectType::Instance); }
        bool IsList()           const { return IsObjType(ObjectType::List); }
        bool IsMap()            const { return IsObjType(ObjectType::Map); }
        bool IsClass()          const { return IsObjType(ObjectType::Class); }
        bool IsRange()          const { return IsObjType(ObjectType::Range); }

        bool                            AsBool()     const { return BoolVal; }
        int64_t                         AsInt()      const { return IntVal; }
        double                          AsFloat()    const { return FloatVal; }
        CQSObject*                      AsObject()   const { return ObjectVal; }
        struct CQSStringObject*         AsString()   const;
        struct CQSFunctionObject*       AsFunction() const;
        struct CQSNativeFunctionObject* AsNativeFunction() const;
        struct CQSInstanceObject*       AsInstance() const;
        struct CQSListObject*           AsList()     const;
        struct CQSMapObject*            AsMap()      const;
        struct CQSClosureObject*        AsClosure()  const;
        CQSClassObject*                 AsClass()    const;
        struct CQSRangeObject*          AsRange()    const;

        double ToNumber() const
        {
            if (Type == ValueType::Int) return static_cast<double>(IntVal);
            if (Type == ValueType::Float) return FloatVal;
            if (Type == ValueType::Bool) return BoolVal ? 1.0 : 0.0;
            return 0.0;
        }
        int64_t ToInt() const
        {
            if (Type == ValueType::Int) return IntVal;
            if (Type == ValueType::Float) return static_cast<int64_t>(FloatVal);
            if (Type == ValueType::Bool) return BoolVal ? 1 : 0;
            return 0;
        }

        bool IsTruthy() const;
        bool Equals(const Value& other) const;
        bool operator==(const Value& other) const { return Equals(other); }
        bool operator!=(const Value& other) const { return !Equals(other); }

        std::string ToString() const;
        // Hata mesajları için kısa tür adı ("int", "string", "List", sınıf adı...)
        std::string TypeName() const;
    };

    // ── String ──
    struct CQSStringObject : CQSObject
    {
        std::string Value;
        explicit CQSStringObject(const std::string& value) : CQSObject(ObjectType::String), Value(value) {}
        explicit CQSStringObject(std::string&& value) : CQSObject(ObjectType::String), Value(std::move(value)) {}
    };

    // try/catch bölgesi: [Start, End) aralığındaki bir hata Handler'a atlar
    struct CQSTryHandler
    {
        uint32_t Start = 0, End = 0, Handler = 0;
        uint32_t StackDepth = 0;   // handler'a girerken taban üstündeki slot sayısı
    };

    struct CQSUpvalueDesc
    {
        bool     IsLocal = false;
        uint16_t Index = 0;
    };

    struct CQSInstr
    {
        uint8_t  Op = 0;
        uint8_t  A = 0;
        uint16_t B = 0;
        int32_t  C = 0;
    };

    // Özellik erişimi satır içi önbelleği (sınıf -> alan slotu)
    struct CQSPropertyCache
    {
        CQSClassObject* Class = nullptr;
        int32_t         Slot = -1;          // alan slotu (-1: yok)
        const Value*    Method = nullptr;   // metot (Methods tablosundaki kalıcı adres)
    };

    // ── Function prototype (bytecode) ──
    struct CQSFunctionObject : CQSObject
    {
        std::string                   Name;
        std::string                   SourceName;      // dosya yolu (hata mesajları)
        int                           Arity = 0;
        int                           RequiredArgs = 0; // varsayılan değeri olmayan parametre sayısı
        int                           UpvalueCount = 0;
        int                           MaxStack = 0;     // taban üstü en çok slot (yerel + işlenen)
        bool                          IsMethod = false; // slot 0 = this
        bool                          IsInitializer = false;
        CQSClassObject*               OwnerClass = nullptr; // metotlar: base.X() çözümü için
        std::vector<CQSInstr>         Code;
        std::vector<int>              Lines;
        std::vector<Value>            Constants;
        std::vector<std::string>      Names;            // özellik/metot/tip adları
        std::vector<CQSUpvalueDesc>   Upvalues;
        std::vector<CQSTryHandler>    Handlers;         // iç içe olanlar önce
        std::vector<CQSPropertyCache> Caches;

        // JIT
        void*    JitCode = nullptr;
        size_t   JitSize = 0;
        uint32_t CallCount = 0;
        uint32_t BackEdges = 0;          // sıcak döngü sayacı (OSR)
        bool     JitFailed = false;

        CQSFunctionObject() : CQSObject(ObjectType::Function) {}
        CQSFunctionObject(const std::string& name, int arity) : CQSObject(ObjectType::Function), Name(name), Arity(arity), RequiredArgs(arity) {}
        ~CQSFunctionObject() override;
    };

    // ── Native Function ──
    using NativeFn = std::function<Value(int argCount, Value* args)>;

    struct CQSNativeFunctionObject : CQSObject
    {
        std::string Name;
        int         Arity;
        NativeFn    Function;

        CQSNativeFunctionObject(const std::string& name, int arity, NativeFn fn)
            : CQSObject(ObjectType::NativeFunction), Name(name), Arity(arity), Function(std::move(fn)) {}
    };

    struct CQSUpvalueObject : CQSObject
    {
        Value*            Location;
        Value             Closed;
        CQSUpvalueObject* NextOpen = nullptr;
        explicit CQSUpvalueObject(Value* slot) : CQSObject(ObjectType::Upvalue), Location(slot) {}
    };

    struct CQSClosureObject : CQSObject
    {
        CQSFunctionObject*             Function;
        std::vector<CQSUpvalueObject*> Upvalues;
        explicit CQSClosureObject(CQSFunctionObject* fn)
            : CQSObject(ObjectType::Closure), Function(fn), Upvalues((size_t)fn->UpvalueCount, nullptr) {}
    };

    struct CQSFieldInfo
    {
        std::string              Name;
        std::string              TypeName;
        bool                     IsPublic = false;
        std::vector<std::string> Attributes;
        Value                    DefaultValue;      // yalnızca sabit (literal) başlangıç değerleri
        bool                     HasLiteralDefault = false;
    };

    struct CQSClassObject : CQSObject
    {
        std::string                            Name;
        std::string                            BaseName;
        CQSClassObject*                        Base = nullptr;
        bool                                   IsScript = false;   // script X : ConquerorScript
        bool                                   IsStruct = false;
        int                                    ExpectedBaseFields = -1;  // derleme anında varsayılan taban alan sayısı (-1: bilinmiyor)
        std::vector<CQSFieldInfo>              OwnFields;
        std::vector<std::string>               FieldNames;          // taban + kendi (yerleşim)
        std::unordered_map<std::string, int>   FieldIndex;
        std::unordered_map<std::string, Value> Methods;             // closure'lar (tabandan kopyalanmış dahil)
        std::unordered_map<std::string, Value> Statics;
        Value                                  FieldInitializer;    // closure (yalnızca bu sınıfın alanları)
        Value                                  Constructor;         // init / sınıf adıyla aynı metot

        explicit CQSClassObject(const std::string& name) : CQSObject(ObjectType::Class), Name(name) {}
        bool IsSubclassOf(const CQSClassObject* other) const
        {
            for (const CQSClassObject* c = this; c; c = c->Base)
                if (c == other) return true;
            return false;
        }
        const Value* FindMethod(const std::string& name) const
        {
            auto it = Methods.find(name);
            return it != Methods.end() ? &it->second : nullptr;
        }
    };

    // ── Instance ──
    struct CQSInstanceObject : CQSObject
    {
        std::string                            ClassName;
        CQSClassObject*                        Class = nullptr;
        std::vector<Value>                     Slots;      // sınıf alanları (FieldIndex sırası)
        std::unordered_map<std::string, Value> Fields;     // sonradan eklenen dinamik alanlar
        uint32_t                               EntityHandle = 0xFFFFFFFF;

        explicit CQSInstanceObject(const std::string& className) : CQSObject(ObjectType::Instance), ClassName(className) {}
        explicit CQSInstanceObject(CQSClassObject* cls);
    };

    struct CQSBoundMethodObject : CQSObject
    {
        Value Receiver;
        Value Method;     // closure ya da native
        CQSBoundMethodObject(Value receiver, Value method) : CQSObject(ObjectType::BoundMethod), Receiver(receiver), Method(method) {}
    };

    struct CQSListObject : CQSObject
    {
        std::vector<Value> Elements;
        bool IsPairList = false;   // foreach (k, v in map) için [k, v] çiftleri
        CQSListObject() : CQSObject(ObjectType::List) {}
    };

    struct CQSMapObject : CQSObject
    {
        std::unordered_map<std::string, Value> Entries;
        CQSMapObject() : CQSObject(ObjectType::Map) {}
    };

    // a..b (b hariç), adım 1 ya da -1
    struct CQSRangeObject : CQSObject
    {
        int64_t Start, End;
        CQSRangeObject(int64_t s, int64_t e) : CQSObject(ObjectType::Range), Start(s), End(e) {}
        int64_t Count() const { return End >= Start ? End - Start : Start - End; }
        int64_t At(int64_t i) const { return End >= Start ? Start + i : Start - i; }
    };

    inline CQSStringObject*         Value::AsString()   const { return static_cast<CQSStringObject*>(ObjectVal); }
    inline CQSFunctionObject*       Value::AsFunction() const { return static_cast<CQSFunctionObject*>(ObjectVal); }
    inline CQSNativeFunctionObject* Value::AsNativeFunction() const { return static_cast<CQSNativeFunctionObject*>(ObjectVal); }
    inline CQSInstanceObject*       Value::AsInstance() const { return static_cast<CQSInstanceObject*>(ObjectVal); }
    inline CQSListObject*           Value::AsList()     const { return static_cast<CQSListObject*>(ObjectVal); }
    inline CQSMapObject*            Value::AsMap()      const { return static_cast<CQSMapObject*>(ObjectVal); }
    inline CQSClosureObject*        Value::AsClosure()  const { return static_cast<CQSClosureObject*>(ObjectVal); }
    inline CQSClassObject*          Value::AsClass()    const { return static_cast<CQSClassObject*>(ObjectVal); }
    inline CQSRangeObject*          Value::AsRange()    const { return static_cast<CQSRangeObject*>(ObjectVal); }

    // ── Heap / GC ──
    // Tek iş parçacıklı: betikler ana iş parçacığında çalışır.
    class CQSHeap
    {
    public:
        static CQSHeap& Get();

        void Register(CQSObject* obj);
        size_t GetObjectCount() const { return m_ObjectCount; }
        size_t GetAllocationsSinceGC() const { return m_SinceGC; }
        bool ShouldCollect() const { return m_SinceGC >= m_Threshold && m_Disabled == 0; }
        // JIT: güvenli nokta denetimini satır içi yapmak için sayaç adresleri
        const size_t* SinceGCAddress() const { return &m_SinceGC; }
        const size_t* ThresholdAddress() const { return &m_Threshold; }

        // İşaretleme: kök kümesini işaretledikten sonra Sweep çağrılır
        void MarkValue(const Value& v);
        void MarkObject(CQSObject* obj);
        void TraceReferences();
        size_t Sweep();

        // Kalıcı kökler (ör. native köprüsünün Key/Mouse tabloları)
        void AddPersistentRoot(const Value& v) { m_PersistentRoots.push_back(v); }
        void MarkPersistentRoots();

        void Disable() { ++m_Disabled; }
        void Enable() { if (m_Disabled > 0) --m_Disabled; }
        bool IsDisabled() const { return m_Disabled > 0; }

        // Tüm nesneleri serbest bırak (kapanış)
        void FreeAll();

    private:
        CQSObject*              m_Objects = nullptr;
        size_t                  m_ObjectCount = 0;
        size_t                  m_SinceGC = 0;
        size_t                  m_Threshold = 200000;
        int                     m_Disabled = 0;
        std::vector<CQSObject*> m_Gray;
        std::vector<Value>      m_PersistentRoots;
    };

    // GC'yi bir kapsam boyunca kapatır (derleme, toplu nesne oluşturma)
    struct CQSNoGCScope
    {
        CQSNoGCScope() { CQSHeap::Get().Disable(); }
        ~CQSNoGCScope() { CQSHeap::Get().Enable(); }
    };
}
