#pragma once

#include "CQSOpCode.h"
#include "CQSValue.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::CQS
{
    // C yığınında yaşayan çağrı çerçevesi (yorumlayıcı ve JIT ortak). Hata izleri ve GC kökleri için bağlı liste.
    struct CQSFrame
    {
        CQSClosureObject* Closure = nullptr;
        Value*            Base = nullptr;     // Base[0] = çağrılan / this, Base[1..] = argümanlar
        int               ArgCount = 0;
        const CQSInstr*   IP = nullptr;       // en son senkronize edilen komut (hata satırı)
        CQSFrame*         Parent = nullptr;
    };

    enum class VMResult
    {
        Success,
        CompileError,
        RuntimeError
    };

    struct CQSStats
    {
        uint64_t Calls = 0;
        uint64_t JitCompiledFunctions = 0;
        uint64_t JitCalls = 0;
        uint64_t GCRuns = 0;
        uint64_t GCFreed = 0;
    };

    class CQSVM
    {
    public:
        CQSVM();
        ~CQSVM();
        CQSVM(const CQSVM&) = delete;
        CQSVM& operator=(const CQSVM&) = delete;

        // ── Derleme / çalıştırma ──
        // Kaynağı derler; başarılıysa modül fonksiyonunu döndürür (henüz çalıştırmaz)
        CQSFunctionObject* Compile(const std::string& source, const std::string& sourceName, std::vector<std::string>& errors,
                                   std::vector<std::string>* warnings = nullptr);
        // Modülü çalıştır (sınıf/fonksiyon/global tanımları)
        bool RunModule(CQSFunctionObject* module);
        // Derle + çalıştır
        VMResult Interpret(const std::string& source, const std::string& sourceName = "<string>");
        // Tek ifade değerlendir (konsol/REPL). İfade değilse deyim olarak çalıştırır.
        bool Evaluate(const std::string& source, Value& result, std::string& error);

        // ── Globaller ──
        int   GlobalSlot(const std::string& name);              // yoksa oluşturur (tanımsız)
        int   FindGlobalSlot(const std::string& name) const;    // -1: yok
        bool  IsGlobalDefined(int slot) const { return slot >= 0 && (size_t)slot < m_GlobalCount && m_GlobalDefined[(size_t)slot]; }
        const Value& GetGlobalValue(int slot) const { return m_GlobalValues[(size_t)slot]; }
        const std::string& GetGlobalName(int slot) const { return m_GlobalNames[(size_t)slot]; }
        size_t GetGlobalCount() const { return m_GlobalNames.size(); }
        void  DefineNative(const std::string& name, NativeFn function, int arity);
        void  DefineGlobal(const std::string& name, Value value);
        bool  GetGlobal(const std::string& name, Value& outValue) const;
        CQSClassObject* FindClass(const std::string& name) const;

        // ── Ana programdan çağrı ──
        // callee: closure / native / bound method / sınıf. Hata durumunda false (GetLastError).
        bool Call(Value callee, const Value* args, int argc, Value* result = nullptr);
        // receiver.name(args): metot yoksa ve optional ise sessizce true döner (found=false)
        bool CallMethod(Value receiver, const std::string& name, const Value* args, int argc, Value* result = nullptr, bool* found = nullptr);
        // Sınıf örneği oluştur: alan başlatıcıları + yapıcı
        CQSInstanceObject* Instantiate(CQSClassObject* cls, const Value* args, int argc, uint32_t entityHandle = 0xFFFFFFFF);

        // Ana programın tuttuğu değerleri GC'den korur
        uint32_t Pin(Value v);
        void     Unpin(uint32_t handle);

        // ── Hata ──
        const std::string& GetLastError() const { return m_LastError; }
        void SetErrorCallback(std::function<void(const std::string&)> cb) { m_ErrorCallback = std::move(cb); }
        // Native fonksiyonlardan: betik içinde yakalanabilir hata fırlat (native döndükten sonra yayılır)
        void ThrowError(const std::string& message);

        // ── GC ──
        void CollectGarbage();
        void MaybeCollect() { if (CQSHeap::Get().ShouldCollect()) CollectGarbage(); }

        // ── JIT ──
        void SetJitEnabled(bool enabled) { m_JitEnabled = enabled; }
        bool IsJitEnabled() const { return m_JitEnabled; }
        void SetJitThreshold(uint32_t calls) { m_JitThreshold = calls; }
        const CQSStats& GetStats() const { return m_Stats; }

        // ── Eski API uyumu (native köprüsü) ──
        void SetCurrentInstance(CQSInstanceObject* instance) { m_CurrentInstance = instance; }
        CQSInstanceObject* GetCurrentInstance() const { return m_CurrentInstance; }
        void  Push(Value value) { *m_StackTop++ = value; }
        Value Pop() { return *--m_StackTop; }
        Value Peek(int distance = 0) const { return *(m_StackTop - 1 - distance); }

        // Örnek bir varlığa aitse çağrı süresince "geçerli varlık" değiştirilir (native köprüsü bunu kullanır)
        using EntityContextFn = uint32_t (*)(uint32_t newEntity);   // eskisini döndürür
        void SetEntityContextHook(EntityContextFn fn) { m_EntityContextHook = fn; }

        // ─────────── İç kullanım (yorumlayıcı + JIT yardımcıları) ───────────
        // Base[0] = çağrılan, Base[1..argc] = argümanlar. Sonuç Base[0]'a yazılır. Hata: false (istisna beklemede).
        bool CallValue(Value* base, int argc);
        bool CallClosure(CQSClosureObject* closure, Value* base, int argc);
        bool InvokeMethod(Value* base, int argc, const std::string& name, CQSPropertyCache* cache);
        bool Execute(CQSFrame& frame);

        bool  HasException() const { return m_HasException; }
        Value TakeException();
        // Çalışma zamanı hatası oluştur (Exception örneği) ve beklemeye al
        void  RuntimeError(const char* format, ...);
        void  SetException(Value ex);

        CQSFrame* CurrentFrame() const { return m_CurrentFrame; }
        void      SetCurrentFrame(CQSFrame* f) { m_CurrentFrame = f; }
        Value*    StackLimit() const { return m_StackEnd; }
        Value*&   StackTopRef() { return m_StackTop; }

        CQSUpvalueObject* CaptureUpvalue(Value* local);
        void              CloseUpvalues(Value* last);

        // Yardımcı işlemler (yorumlayıcı yavaş yolu ve JIT tarafından kullanılır)
        bool ArithSlow(OpCode op, Value* a, const Value* b);           // a = a op b
        bool CompareSlow(OpCode op, const Value& a, const Value& b, bool& out);
        bool GetProperty(Value* slot, const std::string& name, CQSPropertyCache* cache);   // slot = slot.name
        bool SetProperty(const Value& obj, const std::string& name, const Value& v, CQSPropertyCache* cache);
        bool GetIndex(Value* slot, const Value& index);                 // slot = slot[index]
        bool SetIndex(const Value& obj, const Value& index, const Value& v);
        bool IterPrep(Value* slot, int vars);
        int  IterNext(Value* seqSlot, Value* out, int vars);           // 1: öğe var, 0: bitti, -1: hata
        bool IsInstanceOfType(const Value& v, const std::string& type);
        bool CastToType(Value* slot, const std::string& type);
        Value MakeStringValue(std::string&& s);
        bool ValueToString(const Value& v, std::string& out);        // ToString() metotlarını çağırır
        bool FormatValue(const Value& v, const std::string& format, std::string& out);
        bool CreateClass(Value* slot, CQSClassObject* templ);
        bool InheritClass(Value* classSlot, const Value& base, bool lenient);
        bool DefineMethod(Value* classSlot, const std::string& name, const Value& method, int kind);
        bool NewArray(Value* slot, const std::string& elementType);
        bool GetDynamic(Value* out, const Value& self, const std::string& name, int globalSlot);
        bool GetSuperMethod(Value* slot, const std::string& name);
        bool SuperInvoke(Value* base, int argc, const std::string& name);

        Value* GlobalValues() { return m_GlobalValues.get(); }
        uint8_t* GlobalDefinedFlags() { return m_GlobalDefined.get(); }

        uint32_t JitThreshold() const { return m_JitThreshold; }
        CQSStats& MutableStats() { return m_Stats; }

        static constexpr int kMaxFrames = 1000;
        static constexpr size_t kMaxGlobals = 65536;
        static constexpr size_t kStackSize = 1 << 18;   // 262144 slot (4 MB)

    private:
        void  DefineBuiltins();
        void  LoadPrelude();
        void  ReportError(const std::string& message);
        bool  FinishHostCall(Value* base, bool ok, Value* result);
        std::string BuildStackTrace() const;
        bool  InvokeBuiltinMethod(Value* base, int argc, const std::string& name, bool& handled);
        bool  CallInstanceMethodWithEntity(CQSInstanceObject* inst, CQSClosureObject* closure, Value* base, int argc);
        bool  ConstructInstance(Value* base, int argc, CQSClassObject* cls);
        void  MarkRoots();
        static std::vector<CQSVM*>& LiveVMs();

        std::unique_ptr<Value[]> m_Stack;
        Value*  m_StackTop = nullptr;
        Value*  m_StackEnd = nullptr;
        CQSFrame* m_CurrentFrame = nullptr;
        int     m_FrameDepth = 0;
        CQSUpvalueObject* m_OpenUpvalues = nullptr;

        // Sabit kapasiteli: adresler değişmez (JIT kodu global adreslerini doğrudan gömer)
        std::unique_ptr<Value[]>   m_GlobalValues;
        std::unique_ptr<uint8_t[]> m_GlobalDefined;
        size_t                     m_GlobalCount = 0;
        std::vector<std::string> m_GlobalNames;
        std::unordered_map<std::string, int> m_GlobalIndex;

        std::vector<CQSFunctionObject*> m_Modules;      // GC kökü
        std::unordered_map<uint32_t, Value> m_Pinned;
        uint32_t m_NextPin = 1;

        bool        m_HasException = false;
        Value       m_Exception;
        std::string m_LastError;
        std::function<void(const std::string&)> m_ErrorCallback;

        CQSInstanceObject* m_CurrentInstance = nullptr;
        EntityContextFn    m_EntityContextHook = nullptr;

        bool     m_HostLenient = false;           // ana programdan gelen çağrı: fazla argümanları kırp
        uint32_t m_PendingEntity = 0xFFFFFFFF;    // Instantiate: oluşturulacak örneğin varlığı

        bool     m_JitEnabled = true;
        uint32_t m_JitThreshold = 2;     // bu kadar çağrıdan sonra derle
        uint32_t m_OsrThreshold = 300;   // bu kadar döngü dönüşünden sonra yorumlayıcıdan yerel koda geç
        CQSStats m_Stats;

        friend class CQSEngine;
        friend class CQSJit;
    };

    // ── Native argüman yardımcıları ──
    inline const Value& NativeArg(int argc, Value* args, int i)
    {
        static const Value s_Null;
        return i < argc ? args[i] : s_Null;
    }
}
