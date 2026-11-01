#include "CQSVM.h"
#include "Core/Profiling/FrameProfiler.h"
#include "CQSCompiler.h"
#include "CQSJit.h"
#include "CQSLexer.h"
#include "CQSParser.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Conqueror::CQS
{
    namespace
    {
        const char* kPrelude = R"CQS(
class Exception
{
    var message = "";
    var type = "Exception";
    var stackTrace = "";
    func init(message = "", type = "Exception")
    {
        this.message = message;
        this.type = type;
    }
    func ToString()
    {
        return type + ": " + message;
    }
}
)CQS";

        inline int64_t WrapAdd(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
        inline int64_t WrapSub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
        inline int64_t WrapMul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }

        // Kaynak adının kısa biçimi (hata mesajları)
        std::string ShortSourceName(const std::string& path)
        {
            size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    // ════════════════════════════════════════
    //  Lifetime
    // ════════════════════════════════════════
    CQSVM::CQSVM()
    {
        m_Stack.reset(new Value[kStackSize]);
        m_StackTop = m_Stack.get();
        m_StackEnd = m_Stack.get() + kStackSize;
        m_GlobalValues.reset(new Value[kMaxGlobals]);
        m_GlobalDefined.reset(new uint8_t[kMaxGlobals]());
        LiveVMs().push_back(this);
        // Test: CQS_JIT_EAGER=1 -> her fonksiyon ilk çağrıda / ilk döngü dönüşünde derlenir
        if (const char* eager = std::getenv("CQS_JIT_EAGER"); eager && eager[0] == '1')
        {
            m_JitThreshold = 1;
            m_OsrThreshold = 1;
        }
        DefineBuiltins();
        LoadPrelude();
    }

    CQSVM::~CQSVM()
    {
        // Nesneler paylaşılan yığında: kökleri bırakıp topla (diğer VM'lerin nesneleri onların kökleriyle korunur)
        for (size_t i = 0; i < m_GlobalCount; ++i)
        {
            m_GlobalValues[i] = Value();
            m_GlobalDefined[i] = 0;
        }
        m_Modules.clear();
        m_Pinned.clear();
        m_StackTop = m_Stack.get();
        m_CurrentInstance = nullptr;
        m_Exception = Value();
        m_OpenUpvalues = nullptr;
        auto& vms = LiveVMs();
        vms.erase(std::remove(vms.begin(), vms.end(), this), vms.end());
        CollectGarbage();
    }

    void CQSVM::LoadPrelude()
    {
        std::vector<std::string> errors;
        CQSFunctionObject* fn = Compile(kPrelude, "<prelude>", errors);
        if (!fn || !RunModule(fn))
            ReportError("CQS prelude failed to load");
    }

    void CQSVM::ReportError(const std::string& message)
    {
        m_LastError = message;
        if (m_ErrorCallback)
            m_ErrorCallback(message);
        else
            std::fprintf(stderr, "[CQS] %s\n", message.c_str());
    }

    // ════════════════════════════════════════
    //  Globals
    // ════════════════════════════════════════
    int CQSVM::GlobalSlot(const std::string& name)
    {
        auto it = m_GlobalIndex.find(name);
        if (it != m_GlobalIndex.end())
            return it->second;
        if (m_GlobalCount >= kMaxGlobals)
        {
            ReportError("Too many global names (limit 65536).");
            return (int)kMaxGlobals - 1;
        }
        int slot = (int)m_GlobalCount++;
        m_GlobalNames.push_back(name);
        m_GlobalIndex.emplace(name, slot);
        return slot;
    }

    int CQSVM::FindGlobalSlot(const std::string& name) const
    {
        auto it = m_GlobalIndex.find(name);
        return it == m_GlobalIndex.end() ? -1 : it->second;
    }

    void CQSVM::DefineGlobal(const std::string& name, Value value)
    {
        int slot = GlobalSlot(name);
        m_GlobalValues[(size_t)slot] = value;
        m_GlobalDefined[(size_t)slot] = 1;
    }

    void CQSVM::DefineNative(const std::string& name, NativeFn function, int arity)
    {
        auto* native = new CQSNativeFunctionObject(name, arity, std::move(function));
        DefineGlobal(name, Value::MakeObject(native));
    }

    bool CQSVM::GetGlobal(const std::string& name, Value& outValue) const
    {
        int slot = FindGlobalSlot(name);
        if (slot < 0 || !m_GlobalDefined[(size_t)slot])
            return false;
        outValue = m_GlobalValues[(size_t)slot];
        return true;
    }

    CQSClassObject* CQSVM::FindClass(const std::string& name) const
    {
        Value v;
        if (GetGlobal(name, v) && v.IsClass())
            return v.AsClass();
        return nullptr;
    }

    // ════════════════════════════════════════
    //  Compile / run
    // ════════════════════════════════════════
    CQSFunctionObject* CQSVM::Compile(const std::string& source, const std::string& sourceName, std::vector<std::string>& errors,
                                      std::vector<std::string>* warnings)
    {
        CQSNoGCScope noGC;
        std::string shortName = ShortSourceName(sourceName);
        CQSLexer lexer;
        auto tokens = lexer.Tokenize(source);
        for (auto& e : lexer.GetErrors())
            errors.push_back(shortName + ": " + e);

        CQSParser parser;
        auto ast = parser.Parse(tokens);
        for (auto& e : parser.GetErrors())
            errors.push_back(shortName + ": " + e);
        if (!errors.empty())
            return nullptr;

        CQSCompiler compiler(this);
        CQSFunctionObject* fn = compiler.Compile(ast, sourceName);
        for (auto& e : compiler.GetErrors())
            errors.push_back(shortName + ": " + e);
        if (warnings)
            for (auto& w : compiler.GetWarnings())
                warnings->push_back(shortName + ": " + w);
        if (!errors.empty())
            return nullptr;
        return fn;
    }

    bool CQSVM::RunModule(CQSFunctionObject* module)
    {
        if (!module)
            return false;
        auto* closure = new CQSClosureObject(module);
        m_Modules.push_back(module);
        return Call(Value::MakeObject(closure), nullptr, 0, nullptr);
    }

    VMResult CQSVM::Interpret(const std::string& source, const std::string& sourceName)
    {
        std::vector<std::string> errors;
        CQSFunctionObject* fn = Compile(source, sourceName, errors);
        if (!fn)
        {
            std::string all;
            for (auto& e : errors)
                all += (all.empty() ? "" : "\n") + e;
            ReportError(all);
            return VMResult::CompileError;
        }
        return RunModule(fn) ? VMResult::Success : VMResult::RuntimeError;
    }

    bool CQSVM::Evaluate(const std::string& source, Value& result, std::string& error)
    {
        result = Value();
        CQSFunctionObject* fn = nullptr;
        {
            CQSNoGCScope noGC;
            CQSLexer lexer;
            auto tokens = lexer.Tokenize(source);
            if (!lexer.HasErrors())
            {
                CQSParser exprParser;
                auto expr = exprParser.ParseSingleExpression(tokens);
                if (expr && !exprParser.HasErrors())
                {
                    CQSCompiler compiler(this);
                    fn = compiler.CompileExpression(expr, "<console>");
                    if (!fn)
                    {
                        error.clear();
                        for (auto& e : compiler.GetErrors()) error += (error.empty() ? "" : "\n") + e;
                        return false;
                    }
                }
            }
        }
        if (fn)
        {
            m_Modules.push_back(fn);
            auto* closure = new CQSClosureObject(fn);
            if (!Call(Value::MakeObject(closure), nullptr, 0, &result))
            {
                error = m_LastError;
                return false;
            }
            return true;
        }
        // Deyim(ler) olarak çalıştır
        std::vector<std::string> errors;
        fn = Compile(source, "<console>", errors);
        if (!fn)
        {
            error.clear();
            for (auto& e : errors) error += (error.empty() ? "" : "\n") + e;
            return false;
        }
        if (!RunModule(fn))
        {
            error = m_LastError;
            return false;
        }
        return true;
    }

    // ════════════════════════════════════════
    //  Host calls
    // ════════════════════════════════════════
    bool CQSVM::FinishHostCall(Value* base, bool ok, Value* result)
    {
        if (ok)
        {
            if (result) *result = base[0];
        }
        else
        {
            Value ex = TakeException();
            std::string message;
            if (ex.IsInstance())
            {
                auto* inst = ex.AsInstance();
                auto get = [&](const char* n) -> std::string {
                    if (inst->Class)
                    {
                        auto it = inst->Class->FieldIndex.find(n);
                        if (it != inst->Class->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                            return inst->Slots[(size_t)it->second].IsNull() ? "" : inst->Slots[(size_t)it->second].ToString();
                    }
                    auto f = inst->Fields.find(n);
                    return f != inst->Fields.end() ? f->second.ToString() : "";
                };
                std::string type = get("type");
                message = (type.empty() ? inst->ClassName : type) + ": " + get("message");
                std::string trace = get("stackTrace");
                if (!trace.empty())
                    message += "\n" + trace;
            }
            else
                message = "Uncaught exception: " + ex.ToString();
            ReportError(message);
            if (result) *result = Value();
        }
        m_StackTop = base;
        return ok;
    }

    bool CQSVM::Call(Value callee, const Value* args, int argc, Value* result)
    {
        Value* base = m_StackTop;
        if (base + argc + 1 + 64 >= m_StackEnd)
        {
            ReportError("Stack overflow (host call).");
            return false;
        }
        base[0] = callee;
        for (int i = 0; i < argc; ++i)
            base[1 + i] = args[i];
        m_StackTop = base + argc + 1;
        m_HostLenient = true;
        bool ok = CallValue(base, argc);
        m_HostLenient = false;
        return FinishHostCall(base, ok, result);
    }

    bool CQSVM::CallMethod(Value receiver, const std::string& name, const Value* args, int argc, Value* result, bool* found)
    {
        if (found) *found = true;
        // Örnek metodu yoksa sessizce geç (OnUpdate tanımlanmamış olabilir)
        if (receiver.IsInstance())
        {
            auto* inst = receiver.AsInstance();
            bool has = inst->Class && inst->Class->FindMethod(name);
            if (!has)
            {
                auto f = inst->Fields.find(name);
                has = f != inst->Fields.end() && f->second.IsCallable();
            }
            if (!has)
            {
                if (found) *found = false;
                if (result) *result = Value();
                return true;
            }
        }
        Value* base = m_StackTop;
        if (base + argc + 1 + 64 >= m_StackEnd)
        {
            ReportError("Stack overflow (host call).");
            return false;
        }
        base[0] = receiver;
        for (int i = 0; i < argc; ++i)
            base[1 + i] = args[i];
        m_StackTop = base + argc + 1;
        m_HostLenient = true;
        bool ok = InvokeMethod(base, argc, name, nullptr);
        m_HostLenient = false;
        return FinishHostCall(base, ok, result);
    }

    CQSInstanceObject* CQSVM::Instantiate(CQSClassObject* cls, const Value* args, int argc, uint32_t entityHandle)
    {
        if (!cls)
            return nullptr;
        Value* base = m_StackTop;
        if (base + argc + 1 + 64 >= m_StackEnd)
        {
            ReportError("Stack overflow (instantiate).");
            return nullptr;
        }
        base[0] = Value::MakeObject(cls);
        for (int i = 0; i < argc; ++i)
            base[1 + i] = args[i];
        m_StackTop = base + argc + 1;
        m_PendingEntity = entityHandle;
        m_HostLenient = true;
        bool ok = CallValue(base, argc);
        m_HostLenient = false;
        m_PendingEntity = 0xFFFFFFFF;
        Value result;
        if (!FinishHostCall(base, ok, &result) || !result.IsInstance())
            return nullptr;
        return result.AsInstance();
    }

    uint32_t CQSVM::Pin(Value v)
    {
        uint32_t h = m_NextPin++;
        if (m_NextPin == 0) m_NextPin = 1;
        m_Pinned[h] = v;
        return h;
    }

    void CQSVM::Unpin(uint32_t handle)
    {
        m_Pinned.erase(handle);
    }

    // ════════════════════════════════════════
    //  Exceptions
    // ════════════════════════════════════════
    Value CQSVM::TakeException()
    {
        Value ex = m_Exception;
        m_Exception = Value();
        m_HasException = false;
        return ex;
    }

    std::string CQSVM::BuildStackTrace() const
    {
        std::string trace;
        int count = 0;
        for (CQSFrame* f = m_CurrentFrame; f; f = f->Parent)
        {
            if (!f->Closure)
                continue;
            const CQSFunctionObject* fn = f->Closure->Function;
            int line = 0;
            if (f->IP && !fn->Code.empty())
            {
                ptrdiff_t pc = f->IP - fn->Code.data();
                if (pc >= 0 && (size_t)pc < fn->Lines.size())
                    line = fn->Lines[(size_t)pc];
            }
            if (++count > 24)
            {
                trace += "  ...\n";
                break;
            }
            trace += "  at " + (fn->Name.empty() ? std::string("<script>") : fn->Name) + " (" + ShortSourceName(fn->SourceName) + ":" + std::to_string(line) + ")\n";
        }
        if (!trace.empty() && trace.back() == '\n')
            trace.pop_back();
        return trace;
    }

    void CQSVM::SetException(Value ex)
    {
        // Exception örneğine iz ekle (yoksa)
        if (ex.IsInstance() && ex.AsInstance()->Class)
        {
            auto* inst = ex.AsInstance();
            auto it = inst->Class->FieldIndex.find("stackTrace");
            if (it != inst->Class->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
            {
                Value& st = inst->Slots[(size_t)it->second];
                if (st.IsNull() || (st.IsString() && st.AsString()->Value.empty()))
                    st = Value::MakeString(BuildStackTrace());
            }
        }
        m_Exception = ex;
        m_HasException = true;
    }

    void CQSVM::RuntimeError(const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        std::string message = buffer;
        std::string type = "Exception";
        // "TypeError: ..." biçimi tür adını taşır
        size_t colon = message.find(": ");
        if (colon != std::string::npos && colon < 40 && message.find(' ') > colon)
        {
            type = message.substr(0, colon);
            message = message.substr(colon + 2);
        }
        // Konum: en içteki çerçevenin satırı
        if (m_CurrentFrame && m_CurrentFrame->Closure && m_CurrentFrame->IP)
        {
            const CQSFunctionObject* fn = m_CurrentFrame->Closure->Function;
            ptrdiff_t pc = m_CurrentFrame->IP - fn->Code.data();
            if (pc >= 0 && (size_t)pc < fn->Lines.size())
                message = ShortSourceName(fn->SourceName) + ":" + std::to_string(fn->Lines[(size_t)pc]) + ": " + message;
        }

        CQSClassObject* cls = FindClass("Exception");
        if (cls)
        {
            auto* inst = new CQSInstanceObject(cls);
            auto set = [&](const char* name, Value v) {
                auto it = cls->FieldIndex.find(name);
                if (it != cls->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                    inst->Slots[(size_t)it->second] = v;
            };
            set("message", Value::MakeString(message));
            set("type", Value::MakeString(type));
            set("stackTrace", Value::MakeString(BuildStackTrace()));
            m_Exception = Value::MakeObject(inst);
        }
        else
            m_Exception = Value::MakeString(type + ": " + message);
        m_HasException = true;
    }

    void CQSVM::ThrowError(const std::string& message)
    {
        RuntimeError("%s", message.c_str());
    }

    // ════════════════════════════════════════
    //  Calls
    // ════════════════════════════════════════
    bool CQSVM::CallValue(Value* base, int argc)
    {
        Value callee = base[0];
        if (callee.Type != ValueType::Object || !callee.ObjectVal)
        {
            RuntimeError("TypeError: Cannot call a value of type '%s'.", callee.TypeName().c_str());
            return false;
        }
        switch (callee.ObjectVal->Type)
        {
            case ObjectType::Closure:
                return CallClosure(callee.AsClosure(), base, argc);
            case ObjectType::NativeFunction:
            {
                auto* native = callee.AsNativeFunction();
                if (native->Arity >= 0 && argc > native->Arity && !m_HostLenient)
                {
                    RuntimeError("ArgumentError: %s() takes %d argument(s) but %d were given.", native->Name.c_str(), native->Arity, argc);
                    return false;
                }
                // Eksik argümanları null ile doldur (native'ler sabit indeksle okuyabilir)
                int padded = argc;
                if (native->Arity > argc)
                {
                    if (base + 1 + native->Arity + 16 >= m_StackEnd)
                    {
                        RuntimeError("StackOverflow: Stack overflow.");
                        return false;
                    }
                    for (int i = argc; i < native->Arity; ++i)
                        base[1 + i] = Value();
                    padded = native->Arity;
                }
                m_StackTop = base + 1 + padded;
                ++m_Stats.Calls;
                Value r = native->Function(argc, base + 1);
                if (m_HasException)
                    return false;
                base[0] = r;
                m_StackTop = base + 1;
                return true;
            }
            case ObjectType::BoundMethod:
            {
                auto* bm = static_cast<CQSBoundMethodObject*>(callee.ObjectVal);
                if (bm->Method.IsObjType(ObjectType::Closure))
                {
                    base[0] = bm->Receiver;
                    if (bm->Receiver.IsInstance())
                        return CallInstanceMethodWithEntity(bm->Receiver.AsInstance(), bm->Method.AsClosure(), base, argc);
                    return CallClosure(bm->Method.AsClosure(), base, argc);
                }
                base[0] = bm->Method;
                return CallValue(base, argc);
            }
            case ObjectType::Class:
                return ConstructInstance(base, argc, callee.AsClass());
            case ObjectType::Function:
            {
                // Çıplak prototip (normalde Closure ile sarılır)
                auto* closure = new CQSClosureObject(callee.AsFunction());
                base[0] = Value::MakeObject(closure);
                return CallClosure(closure, base, argc);
            }
            default:
                break;
        }
        RuntimeError("TypeError: Cannot call a value of type '%s'.", callee.TypeName().c_str());
        return false;
    }

    bool CQSVM::ConstructInstance(Value* base, int argc, CQSClassObject* cls)
    {
        const bool lenient = m_HostLenient;
        m_HostLenient = false;
        auto* inst = new CQSInstanceObject(cls);
        inst->EntityHandle = m_PendingEntity;
        m_PendingEntity = 0xFFFFFFFF;
        base[0] = Value::MakeObject(inst);

        // Varsayılan alan değerleri ve başlatıcılar: kökten yaprağa
        CQSClassObject* chain[64];
        int depth = 0;
        for (CQSClassObject* c = cls; c && depth < 64; c = c->Base)
            chain[depth++] = c;
        for (int i = depth - 1; i >= 0; --i)
        {
            CQSClassObject* c = chain[i];
            for (auto& f : c->OwnFields)
            {
                auto it = cls->FieldIndex.find(f.Name);
                if (it != cls->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                    inst->Slots[(size_t)it->second] = f.DefaultValue;
            }
        }
        // Yapıcı argümanları başlatıcılar çalışırken korunmalı: argümanların üstünü kullan
        Value* top = base + 1 + argc;
        m_StackTop = top;
        for (int i = depth - 1; i >= 0; --i)
        {
            CQSClassObject* c = chain[i];
            if (!c->FieldInitializer.IsObjType(ObjectType::Closure))
                continue;
            top[0] = base[0];
            m_StackTop = top + 1;
            if (!CallInstanceMethodWithEntity(inst, c->FieldInitializer.AsClosure(), top, 0))
                return false;
        }
        m_StackTop = base + 1 + argc;

        if (cls->Constructor.IsObjType(ObjectType::Closure))
        {
            m_HostLenient = lenient;
            if (!CallInstanceMethodWithEntity(inst, cls->Constructor.AsClosure(), base, argc))
                return false;
            base[0] = Value::MakeObject(inst);
        }
        else if (argc > 0)
        {
            if (cls->IsStruct && (size_t)argc <= inst->Slots.size())
            {
                // struct: konumsal alan ataması
                for (int i = 0; i < argc; ++i)
                    inst->Slots[(size_t)i] = base[1 + i];
            }
            else if (!lenient)
            {
                RuntimeError("ArgumentError: Class '%s' has no constructor (init) taking %d argument(s).", cls->Name.c_str(), argc);
                return false;
            }
        }
        m_StackTop = base + 1;
        return true;
    }

    bool CQSVM::CallInstanceMethodWithEntity(CQSInstanceObject* inst, CQSClosureObject* closure, Value* base, int argc)
    {
        CQSInstanceObject* savedInstance = m_CurrentInstance;
        uint32_t savedEntity = 0xFFFFFFFF;
        bool switched = false;
        if (inst->EntityHandle != 0xFFFFFFFF)
        {
            m_CurrentInstance = inst;
            if (m_EntityContextHook)
            {
                savedEntity = m_EntityContextHook(inst->EntityHandle);
                switched = true;
            }
        }
        bool ok = CallClosure(closure, base, argc);
        if (switched)
            m_EntityContextHook(savedEntity);
        m_CurrentInstance = savedInstance;
        return ok;
    }

    bool CQSVM::CallClosure(CQSClosureObject* closure, Value* base, int argc)
    {
        CQSFunctionObject* fn = closure->Function;
        if (argc != fn->Arity)
        {
            if (argc > fn->Arity)
            {
                if (!m_HostLenient)
                {
                    RuntimeError("ArgumentError: %s() takes %d argument(s) but %d were given.", fn->Name.c_str(), fn->Arity, argc);
                    return false;
                }
                argc = fn->Arity; // ana programdan gelen fazla argümanlar (ör. OnUpdate(ts) yerine OnUpdate())
            }
            else if (argc < fn->RequiredArgs && !m_HostLenient)
            {
                RuntimeError("ArgumentError: %s() requires %d argument(s) but %d were given.", fn->Name.c_str(), fn->RequiredArgs, argc);
                return false;
            }
            for (int i = argc; i < fn->Arity; ++i)
                base[1 + i] = Value();
        }
        m_HostLenient = false;
        if (m_FrameDepth >= kMaxFrames || base + fn->MaxStack + 64 >= m_StackEnd)
        {
            RuntimeError("StackOverflow: Stack overflow (too much recursion) in %s().", fn->Name.c_str());
            return false;
        }

        CQSFrame frame;
        frame.Closure = closure;
        frame.Base = base;
        frame.ArgCount = argc;
        frame.Parent = m_CurrentFrame;
        m_CurrentFrame = &frame;
        ++m_FrameDepth;
        ++m_Stats.Calls;

        bool ok;
        if (fn->JitCode)
        {
            ++m_Stats.JitCalls;
            ok = CQSJit::Run(this, &frame);
        }
        else
        {
            if (m_JitEnabled && !fn->JitFailed && ++fn->CallCount >= m_JitThreshold && CQSJit::IsSupported())
            {
                if (CQSJit::Compile(this, fn))
                    ++m_Stats.JitCompiledFunctions;
            }
            if (fn->JitCode)
            {
                ++m_Stats.JitCalls;
                ok = CQSJit::Run(this, &frame);
            }
            else
                ok = Execute(frame);
        }

        --m_FrameDepth;
        m_CurrentFrame = frame.Parent;
        return ok;
    }

    // ════════════════════════════════════════
    //  Upvalues
    // ════════════════════════════════════════
    CQSUpvalueObject* CQSVM::CaptureUpvalue(Value* local)
    {
        CQSUpvalueObject* prev = nullptr;
        CQSUpvalueObject* up = m_OpenUpvalues;
        while (up && up->Location > local)
        {
            prev = up;
            up = up->NextOpen;
        }
        if (up && up->Location == local)
            return up;
        auto* created = new CQSUpvalueObject(local);
        created->NextOpen = up;
        if (prev) prev->NextOpen = created;
        else m_OpenUpvalues = created;
        return created;
    }

    void CQSVM::CloseUpvalues(Value* last)
    {
        while (m_OpenUpvalues && m_OpenUpvalues->Location >= last)
        {
            CQSUpvalueObject* up = m_OpenUpvalues;
            up->Closed = *up->Location;
            up->Location = &up->Closed;
            m_OpenUpvalues = up->NextOpen;
        }
    }

    // ════════════════════════════════════════
    //  Classes
    // ════════════════════════════════════════
    bool CQSVM::CreateClass(Value* slot, CQSClassObject* templ)
    {
        auto* cls = new CQSClassObject(templ->Name);
        cls->BaseName = templ->BaseName;
        cls->IsScript = templ->IsScript;
        cls->IsStruct = templ->IsStruct;
        cls->ExpectedBaseFields = templ->ExpectedBaseFields;
        cls->OwnFields = templ->OwnFields;
        for (auto& f : cls->OwnFields)
            cls->FieldNames.push_back(f.Name);
        for (size_t i = 0; i < cls->FieldNames.size(); ++i)
            cls->FieldIndex[cls->FieldNames[i]] = (int)i;
        *slot = Value::MakeObject(cls);
        return true;
    }

    bool CQSVM::InheritClass(Value* classSlot, const Value& base, bool lenient)
    {
        CQSClassObject* cls = classSlot->AsClass();
        if (!base.IsClass())
        {
            if (lenient)
                return true;
            RuntimeError("TypeError: Base class '%s' of '%s' is not defined (load its script first).", cls->BaseName.c_str(), cls->Name.c_str());
            return false;
        }
        CQSClassObject* b = base.AsClass();
        if (b == cls || b->IsSubclassOf(cls))
        {
            RuntimeError("TypeError: Cyclic inheritance involving '%s'.", cls->Name.c_str());
            return false;
        }
        if (cls->ExpectedBaseFields >= 0 && (size_t)cls->ExpectedBaseFields != b->FieldNames.size())
        {
            RuntimeError("TypeError: Base class '%s' changed after '%s' was compiled; reload the scripts.", b->Name.c_str(), cls->Name.c_str());
            return false;
        }
        cls->Base = b;
        cls->IsScript = cls->IsScript || b->IsScript;
        // Yerleşim: taban alanları önce
        std::vector<std::string> names = b->FieldNames;
        for (auto& f : cls->OwnFields)
        {
            if (std::find(names.begin(), names.end(), f.Name) != names.end())
            {
                RuntimeError("TypeError: Field '%s' in '%s' hides a field of base class '%s'.", f.Name.c_str(), cls->Name.c_str(), b->Name.c_str());
                return false;
            }
            names.push_back(f.Name);
        }
        cls->FieldNames = names;
        cls->FieldIndex.clear();
        for (size_t i = 0; i < names.size(); ++i)
            cls->FieldIndex[names[i]] = (int)i;
        // Metotları kopyala (alt sınıf sonra üzerine yazar)
        for (auto& [name, m] : b->Methods)
            cls->Methods[name] = m;
        if (b->Constructor.IsObjType(ObjectType::Closure) && cls->Constructor.IsNull())
            cls->Constructor = b->Constructor;
        return true;
    }

    bool CQSVM::DefineMethod(Value* classSlot, const std::string& name, const Value& method, int kind)
    {
        CQSClassObject* cls = classSlot->AsClass();
        if (method.IsObjType(ObjectType::Closure))
            method.AsClosure()->Function->OwnerClass = cls;
        switch (kind)
        {
            case 0: cls->Methods[name] = method; break;
            case 1: cls->Statics[name] = method; break;
            case 2:
                cls->Methods[name] = method;
                cls->Constructor = method;
                break;
            case 3: cls->FieldInitializer = method; break;
            default: break;
        }
        return true;
    }

    bool CQSVM::GetSuperMethod(Value* slot, const std::string& name)
    {
        CQSClassObject* owner = m_CurrentFrame && m_CurrentFrame->Closure ? m_CurrentFrame->Closure->Function->OwnerClass : nullptr;
        if (!owner || !owner->Base)
        {
            RuntimeError("TypeError: 'base.%s' used in a class without a base class.", name.c_str());
            return false;
        }
        const Value* m = owner->Base->FindMethod(name);
        if (!m)
        {
            RuntimeError("TypeError: Base class '%s' has no method '%s'.", owner->Base->Name.c_str(), name.c_str());
            return false;
        }
        *slot = Value::MakeObject(new CQSBoundMethodObject(*slot, *m));
        return true;
    }

    bool CQSVM::SuperInvoke(Value* base, int argc, const std::string& name)
    {
        CQSClassObject* owner = m_CurrentFrame && m_CurrentFrame->Closure ? m_CurrentFrame->Closure->Function->OwnerClass : nullptr;
        if (!owner || !owner->Base)
        {
            RuntimeError("TypeError: 'base.%s()' used in a class without a base class.", name.c_str());
            return false;
        }
        const Value* m = owner->Base->FindMethod(name);
        if (!m || !m->IsObjType(ObjectType::Closure))
        {
            RuntimeError("TypeError: Base class '%s' has no method '%s'.", owner->Base->Name.c_str(), name.c_str());
            return false;
        }
        bool isInit = (name == "init");
        Value receiver = base[0];
        bool ok = CallClosure(m->AsClosure(), base, argc);
        if (ok && isInit)
            base[0] = receiver;
        return ok;
    }

    // ════════════════════════════════════════
    //  Method invocation
    // ════════════════════════════════════════
    bool CQSVM::InvokeMethod(Value* base, int argc, const std::string& name, CQSPropertyCache* cache)
    {
        Value receiver = base[0];
        if (receiver.Type == ValueType::Object && receiver.ObjectVal)
        {
            switch (receiver.ObjectVal->Type)
            {
                case ObjectType::Instance:
                {
                    auto* inst = receiver.AsInstance();
                    CQSClassObject* cls = inst->Class;
                    const Value* method = nullptr;
                    if (cache && cache->Class == cls && cls && cache->Method)
                        method = cache->Method;
                    else if (cls)
                    {
                        method = cls->FindMethod(name);
                        if (method && cache)
                        {
                            cache->Class = cls;
                            cache->Method = method;
                            cache->Slot = -1;
                        }
                    }
                    if (method)
                    {
                        if (method->IsObjType(ObjectType::Closure))
                            return CallInstanceMethodWithEntity(inst, method->AsClosure(), base, argc);
                        base[0] = *method;
                        return CallValue(base, argc);
                    }
                    // Alan içinde fonksiyon (ör. callback)
                    if (cls)
                    {
                        auto it = cls->FieldIndex.find(name);
                        if (it != cls->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                        {
                            base[0] = inst->Slots[(size_t)it->second];
                            return CallValue(base, argc);
                        }
                    }
                    auto f = inst->Fields.find(name);
                    if (f != inst->Fields.end())
                    {
                        base[0] = f->second;
                        return CallValue(base, argc);
                    }
                    bool handled = false;
                    bool ok = InvokeBuiltinMethod(base, argc, name, handled);
                    if (handled)
                        return ok;
                    RuntimeError("TypeError: '%s' has no method '%s'.", inst->ClassName.c_str(), name.c_str());
                    return false;
                }
                case ObjectType::Class:
                {
                    auto* cls = receiver.AsClass();
                    for (CQSClassObject* c = cls; c; c = c->Base)
                    {
                        auto it = c->Statics.find(name);
                        if (it != c->Statics.end())
                        {
                            base[0] = it->second;
                            return CallValue(base, argc);
                        }
                    }
                    if (cls->FindMethod(name))
                    {
                        RuntimeError("TypeError: '%s.%s' is an instance method; call it on an instance.", cls->Name.c_str(), name.c_str());
                        return false;
                    }
                    RuntimeError("TypeError: Class '%s' has no static method '%s'.", cls->Name.c_str(), name.c_str());
                    return false;
                }
                case ObjectType::Map:
                {
                    auto* map = receiver.AsMap();
                    auto it = map->Entries.find(name);
                    if (it != map->Entries.end() && it->second.IsCallable())
                    {
                        base[0] = it->second;
                        return CallValue(base, argc);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        else if (receiver.IsNull())
        {
            RuntimeError("NullReferenceException: Cannot call method '%s' on null.", name.c_str());
            return false;
        }
        bool handled = false;
        bool ok = InvokeBuiltinMethod(base, argc, name, handled);
        if (handled)
            return ok;
        RuntimeError("TypeError: Value of type '%s' has no method '%s'.", receiver.TypeName().c_str(), name.c_str());
        return false;
    }

    // ════════════════════════════════════════
    //  Properties
    // ════════════════════════════════════════
    bool CQSVM::GetProperty(Value* slot, const std::string& name, CQSPropertyCache* cache)
    {
        Value obj = *slot;
        if (obj.Type != ValueType::Object || !obj.ObjectVal)
        {
            if (obj.IsNull())
            {
                RuntimeError("NullReferenceException: Cannot read property '%s' of null.", name.c_str());
                return false;
            }
            RuntimeError("TypeError: Value of type '%s' has no property '%s'.", obj.TypeName().c_str(), name.c_str());
            return false;
        }
        switch (obj.ObjectVal->Type)
        {
            case ObjectType::Instance:
            {
                auto* inst = obj.AsInstance();
                CQSClassObject* cls = inst->Class;
                if (cache && cls && cache->Class == cls && cache->Slot >= 0 && (size_t)cache->Slot < inst->Slots.size())
                {
                    *slot = inst->Slots[(size_t)cache->Slot];
                    return true;
                }
                if (cls)
                {
                    auto it = cls->FieldIndex.find(name);
                    if (it != cls->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                    {
                        if (cache)
                        {
                            cache->Class = cls;
                            cache->Slot = it->second;
                            cache->Method = nullptr;
                        }
                        *slot = inst->Slots[(size_t)it->second];
                        return true;
                    }
                }
                auto f = inst->Fields.find(name);
                if (f != inst->Fields.end())
                {
                    *slot = f->second;
                    return true;
                }
                if (cls)
                {
                    if (const Value* m = cls->FindMethod(name))
                    {
                        *slot = Value::MakeObject(new CQSBoundMethodObject(obj, *m));
                        return true;
                    }
                }
                if (name == "entity" || name == "Entity")
                {
                    *slot = inst->EntityHandle == 0xFFFFFFFF ? Value() : Value::MakeInt((int64_t)inst->EntityHandle);
                    return true;
                }
                RuntimeError("TypeError: '%s' has no field or method '%s'.", inst->ClassName.c_str(), name.c_str());
                return false;
            }
            case ObjectType::Map:
            {
                auto* map = obj.AsMap();
                auto it = map->Entries.find(name);
                if (it != map->Entries.end())
                {
                    *slot = it->second;
                    return true;
                }
                if (name == "Count" || name == "Length")
                {
                    *slot = Value::MakeInt((int64_t)map->Entries.size());
                    return true;
                }
                *slot = Value(); // olmayan anahtar: null (dinamik nesneler için)
                return true;
            }
            case ObjectType::List:
            {
                auto* list = obj.AsList();
                if (name == "Count" || name == "Length" || name == "count" || name == "length")
                {
                    *slot = Value::MakeInt((int64_t)list->Elements.size());
                    return true;
                }
                if (name == "IsEmpty") { *slot = Value::MakeBool(list->Elements.empty()); return true; }
                if (name == "First" || name == "Last")
                {
                    if (list->Elements.empty())
                    {
                        RuntimeError("InvalidOperationException: List is empty.");
                        return false;
                    }
                    *slot = name == "First" ? list->Elements.front() : list->Elements.back();
                    return true;
                }
                break;
            }
            case ObjectType::String:
            {
                auto* s = obj.AsString();
                if (name == "Length" || name == "length" || name == "Count")
                {
                    *slot = Value::MakeInt((int64_t)s->Value.size());
                    return true;
                }
                if (name == "IsEmpty") { *slot = Value::MakeBool(s->Value.empty()); return true; }
                break;
            }
            case ObjectType::Range:
            {
                auto* r = obj.AsRange();
                if (name == "Start") { *slot = Value::MakeInt(r->Start); return true; }
                if (name == "End") { *slot = Value::MakeInt(r->End); return true; }
                if (name == "Count" || name == "Length") { *slot = Value::MakeInt(r->Count()); return true; }
                break;
            }
            case ObjectType::Class:
            {
                auto* cls = obj.AsClass();
                for (CQSClassObject* c = cls; c; c = c->Base)
                {
                    auto it = c->Statics.find(name);
                    if (it != c->Statics.end())
                    {
                        *slot = it->second;
                        return true;
                    }
                }
                if (name == "Name") { *slot = Value::MakeString(cls->Name); return true; }
                RuntimeError("TypeError: Class '%s' has no static member '%s'.", cls->Name.c_str(), name.c_str());
                return false;
            }
            case ObjectType::BoundMethod:
            case ObjectType::Closure:
            case ObjectType::NativeFunction:
                if (name == "Name")
                {
                    *slot = Value::MakeString(obj.ToString());
                    return true;
                }
                break;
            default:
                break;
        }
        RuntimeError("TypeError: Value of type '%s' has no property '%s'.", obj.TypeName().c_str(), name.c_str());
        return false;
    }

    bool CQSVM::SetProperty(const Value& obj, const std::string& name, const Value& v, CQSPropertyCache* cache)
    {
        if (obj.Type != ValueType::Object || !obj.ObjectVal)
        {
            if (obj.IsNull())
                RuntimeError("NullReferenceException: Cannot set property '%s' of null.", name.c_str());
            else
                RuntimeError("TypeError: Cannot set property '%s' on a value of type '%s'.", name.c_str(), obj.TypeName().c_str());
            return false;
        }
        switch (obj.ObjectVal->Type)
        {
            case ObjectType::Instance:
            {
                auto* inst = obj.AsInstance();
                CQSClassObject* cls = inst->Class;
                if (cache && cls && cache->Class == cls && cache->Slot >= 0 && (size_t)cache->Slot < inst->Slots.size())
                {
                    inst->Slots[(size_t)cache->Slot] = v;
                    return true;
                }
                if (cls)
                {
                    auto it = cls->FieldIndex.find(name);
                    if (it != cls->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                    {
                        if (cache)
                        {
                            cache->Class = cls;
                            cache->Slot = it->second;
                            cache->Method = nullptr;
                        }
                        inst->Slots[(size_t)it->second] = v;
                        return true;
                    }
                }
                inst->Fields[name] = v;
                return true;
            }
            case ObjectType::Map:
                obj.AsMap()->Entries[name] = v;
                return true;
            case ObjectType::Class:
            {
                auto* cls = obj.AsClass();
                for (CQSClassObject* c = cls; c; c = c->Base)
                {
                    auto it = c->Statics.find(name);
                    if (it != c->Statics.end())
                    {
                        it->second = v;
                        return true;
                    }
                }
                cls->Statics[name] = v;
                return true;
            }
            default:
                break;
        }
        RuntimeError("TypeError: Cannot set property '%s' on a value of type '%s'.", name.c_str(), obj.TypeName().c_str());
        return false;
    }

    bool CQSVM::GetDynamic(Value* out, const Value& self, const std::string& name, int globalSlot)
    {
        if (self.IsInstance())
        {
            auto* inst = self.AsInstance();
            if (inst->Class)
            {
                auto it = inst->Class->FieldIndex.find(name);
                if (it != inst->Class->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                {
                    *out = inst->Slots[(size_t)it->second];
                    return true;
                }
                if (const Value* m = inst->Class->FindMethod(name))
                {
                    *out = Value::MakeObject(new CQSBoundMethodObject(self, *m));
                    return true;
                }
            }
            auto f = inst->Fields.find(name);
            if (f != inst->Fields.end())
            {
                *out = f->second;
                return true;
            }
        }
        if (globalSlot >= 0 && (size_t)globalSlot < m_GlobalCount && m_GlobalDefined[(size_t)globalSlot])
        {
            *out = m_GlobalValues[(size_t)globalSlot];
            return true;
        }
        RuntimeError("NameError: Undefined variable '%s'.", name.c_str());
        return false;
    }

    // ════════════════════════════════════════
    //  Indexing
    // ════════════════════════════════════════
    bool CQSVM::GetIndex(Value* slot, const Value& index)
    {
        Value obj = *slot;
        if (obj.Type == ValueType::Object && obj.ObjectVal)
        {
            switch (obj.ObjectVal->Type)
            {
                case ObjectType::List:
                {
                    auto& elems = obj.AsList()->Elements;
                    if (!index.IsNumber())
                    {
                        RuntimeError("TypeError: List index must be a number, got '%s'.", index.TypeName().c_str());
                        return false;
                    }
                    int64_t i = index.ToInt();
                    if (i < 0) i += (int64_t)elems.size();
                    if (i < 0 || i >= (int64_t)elems.size())
                    {
                        RuntimeError("IndexOutOfRangeException: Index %lld is out of range (list has %zu elements).", (long long)index.ToInt(), elems.size());
                        return false;
                    }
                    *slot = elems[(size_t)i];
                    return true;
                }
                case ObjectType::Map:
                {
                    auto* map = obj.AsMap();
                    auto it = map->Entries.find(index.IsString() ? index.AsString()->Value : index.ToString());
                    *slot = it != map->Entries.end() ? it->second : Value();
                    return true;
                }
                case ObjectType::String:
                {
                    const std::string& s = obj.AsString()->Value;
                    if (!index.IsNumber())
                    {
                        RuntimeError("TypeError: String index must be a number.");
                        return false;
                    }
                    int64_t i = index.ToInt();
                    if (i < 0) i += (int64_t)s.size();
                    if (i < 0 || i >= (int64_t)s.size())
                    {
                        RuntimeError("IndexOutOfRangeException: Index %lld is out of range (string length %zu).", (long long)index.ToInt(), s.size());
                        return false;
                    }
                    *slot = Value::MakeString(std::string(1, s[(size_t)i]));
                    return true;
                }
                case ObjectType::Range:
                {
                    auto* r = obj.AsRange();
                    int64_t i = index.ToInt();
                    if (i < 0) i += r->Count();
                    if (i < 0 || i >= r->Count())
                    {
                        RuntimeError("IndexOutOfRangeException: Index %lld is out of range.", (long long)index.ToInt());
                        return false;
                    }
                    *slot = Value::MakeInt(r->At(i));
                    return true;
                }
                case ObjectType::Instance:
                {
                    if (index.IsString())
                        return GetProperty(slot, index.AsString()->Value, nullptr);
                    break;
                }
                default: break;
            }
        }
        if (obj.IsNull())
        {
            RuntimeError("NullReferenceException: Cannot index null.");
            return false;
        }
        RuntimeError("TypeError: Value of type '%s' cannot be indexed.", obj.TypeName().c_str());
        return false;
    }

    bool CQSVM::SetIndex(const Value& obj, const Value& index, const Value& v)
    {
        if (obj.Type == ValueType::Object && obj.ObjectVal)
        {
            switch (obj.ObjectVal->Type)
            {
                case ObjectType::List:
                {
                    auto& elems = obj.AsList()->Elements;
                    if (!index.IsNumber())
                    {
                        RuntimeError("TypeError: List index must be a number, got '%s'.", index.TypeName().c_str());
                        return false;
                    }
                    int64_t i = index.ToInt();
                    if (i < 0) i += (int64_t)elems.size();
                    if (i < 0 || i >= (int64_t)elems.size())
                    {
                        RuntimeError("IndexOutOfRangeException: Index %lld is out of range (list has %zu elements).", (long long)index.ToInt(), elems.size());
                        return false;
                    }
                    elems[(size_t)i] = v;
                    return true;
                }
                case ObjectType::Map:
                    obj.AsMap()->Entries[index.IsString() ? index.AsString()->Value : index.ToString()] = v;
                    return true;
                case ObjectType::Instance:
                    if (index.IsString())
                        return SetProperty(obj, index.AsString()->Value, v, nullptr);
                    break;
                default: break;
            }
        }
        if (obj.IsNull())
        {
            RuntimeError("NullReferenceException: Cannot index null.");
            return false;
        }
        RuntimeError("TypeError: Value of type '%s' does not support index assignment.", obj.TypeName().c_str());
        return false;
    }

    // ════════════════════════════════════════
    //  Iteration
    // ════════════════════════════════════════
    bool CQSVM::IterPrep(Value* slot, int vars)
    {
        Value v = *slot;
        if (v.IsList() || v.IsRange() || v.IsString())
            return true;
        if (v.IsMap())
        {
            // Anahtarların anlık görüntüsü (döngüde değişiklik güvenli), sıralı
            auto* map = v.AsMap();
            auto* list = new CQSListObject();
            std::vector<const std::string*> keys;
            keys.reserve(map->Entries.size());
            for (auto& [k, val] : map->Entries) keys.push_back(&k);
            std::sort(keys.begin(), keys.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
            for (const std::string* k : keys)
            {
                if (vars == 2)
                {
                    auto* pair = new CQSListObject();
                    pair->Elements.push_back(Value::MakeString(*k));
                    pair->Elements.push_back(map->Entries.at(*k));
                    list->Elements.push_back(Value::MakeObject(pair));
                }
                else
                    list->Elements.push_back(Value::MakeString(*k));
            }
            // vars==2 için çiftler listesi: IterNext çifti açar
            list->IsPairList = (vars == 2);
            *slot = Value::MakeObject(list);
            return true;
        }
        if (v.IsInstance())
        {
            // Örnek alanları üzerinde (debug) ya da Items listesi
            auto* inst = v.AsInstance();
            auto* list = new CQSListObject();
            if (inst->Class)
                for (auto& n : inst->Class->FieldNames) list->Elements.push_back(Value::MakeString(n));
            for (auto& [k, val] : inst->Fields) list->Elements.push_back(Value::MakeString(k));
            *slot = Value::MakeObject(list);
            return true;
        }
        if (v.IsNull())
        {
            RuntimeError("NullReferenceException: Cannot iterate over null.");
            return false;
        }
        RuntimeError("TypeError: Value of type '%s' is not iterable.", v.TypeName().c_str());
        return false;
    }

    int CQSVM::IterNext(Value* seqSlot, Value* out, int vars)
    {
        Value seq = seqSlot[0];
        Value& idxVal = seqSlot[1];
        int64_t i = idxVal.IntVal;
        if (seq.IsList())
        {
            auto& elems = seq.AsList()->Elements;
            if (i >= (int64_t)elems.size())
                return 0;
            const Value& e = elems[(size_t)i];
            if (vars == 2)
            {
                // Harita çiftleri ([k, v]) ya da liste (indeks, öğe)
                if (e.IsList() && e.AsList()->Elements.size() == 2 && seq.AsList()->IsPairList)
                {
                    out[0] = e.AsList()->Elements[0];
                    out[1] = e.AsList()->Elements[1];
                }
                else
                {
                    out[0] = Value::MakeInt(i);
                    out[1] = e;
                }
            }
            else
                out[0] = e;
            idxVal.IntVal = i + 1;
            return 1;
        }
        if (seq.IsRange())
        {
            auto* r = seq.AsRange();
            if (i >= r->Count())
                return 0;
            if (vars == 2)
            {
                out[0] = Value::MakeInt(i);
                out[1] = Value::MakeInt(r->At(i));
            }
            else
                out[0] = Value::MakeInt(r->At(i));
            idxVal.IntVal = i + 1;
            return 1;
        }
        if (seq.IsString())
        {
            const std::string& s = seq.AsString()->Value;
            if (i >= (int64_t)s.size())
                return 0;
            // UTF-8 karakter bütünlüğünü koru
            size_t start = (size_t)i, len = 1;
            unsigned char c = (unsigned char)s[start];
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            len = std::min(len, s.size() - start);
            Value ch = Value::MakeString(s.substr(start, len));
            if (vars == 2)
            {
                out[0] = Value::MakeInt(i);
                out[1] = ch;
            }
            else
                out[0] = ch;
            idxVal.IntVal = i + (int64_t)len;
            return 1;
        }
        RuntimeError("TypeError: Value is not iterable.");
        return -1;
    }

    // ════════════════════════════════════════
    //  Types
    // ════════════════════════════════════════
    bool CQSVM::IsInstanceOfType(const Value& v, const std::string& type)
    {
        if (type == "null") return v.IsNull();
        if (type == "object" || type == "any" || type == "var") return !v.IsNull();
        if (type == "int" || type == "long" || type == "short" || type == "byte" || type == "uint") return v.IsInt();
        if (type == "float" || type == "double") return v.IsFloat();
        if (type == "number") return v.IsNumber();
        if (type == "bool") return v.IsBool();
        if (type == "string" || type == "String") return v.IsString();
        if (type == "List" || type.rfind("List<", 0) == 0 || (type.size() > 2 && type.compare(type.size() - 2, 2, "[]") == 0)) return v.IsList();
        if (type == "Map" || type == "Dictionary" || type.rfind("Dictionary<", 0) == 0) return v.IsMap();
        if (type == "Range") return v.IsRange();
        if (type == "function" || type == "Func" || type == "Action" || type.rfind("Func<", 0) == 0 || type.rfind("Action<", 0) == 0) return v.IsCallable();
        if (v.IsInstance())
        {
            auto* inst = v.AsInstance();
            for (CQSClassObject* c = inst->Class; c; c = c->Base)
                if (c->Name == type) return true;
            if (inst->ClassName == type) return true;
            // Çalışma zamanı hataları: Exception örneğinin "type" alanı
            if (inst->Class)
            {
                auto it = inst->Class->FieldIndex.find("type");
                if (it != inst->Class->FieldIndex.end() && (size_t)it->second < inst->Slots.size())
                {
                    const Value& t = inst->Slots[(size_t)it->second];
                    if (t.IsString() && t.AsString()->Value == type) return true;
                }
            }
            return false;
        }
        if (v.IsClass()) return type == "Class";
        return false;
    }

    bool CQSVM::CastToType(Value* slot, const std::string& type)
    {
        Value v = *slot;
        if (type == "int" || type == "long" || type == "short" || type == "byte" || type == "uint")
        {
            if (v.IsInt()) return true;
            if (v.IsFloat())
            {
                double d = v.AsFloat();
                if (std::isnan(d)) { *slot = Value::MakeInt(0); return true; }
                if (d >= 9.2e18) { *slot = Value::MakeInt(INT64_MAX); return true; }
                if (d <= -9.2e18) { *slot = Value::MakeInt(INT64_MIN); return true; }
                *slot = Value::MakeInt((int64_t)d);
                return true;
            }
            if (v.IsBool()) { *slot = Value::MakeInt(v.AsBool() ? 1 : 0); return true; }
            if (v.IsString())
            {
                const std::string& s = v.AsString()->Value;
                char* end = nullptr;
                long long r = std::strtoll(s.c_str(), &end, 10);
                if (end == s.c_str() || *end != '\0')
                {
                    double d = std::strtod(s.c_str(), &end);
                    if (end == s.c_str())
                    {
                        RuntimeError("FormatException: Cannot convert \"%s\" to int.", s.c_str());
                        return false;
                    }
                    r = (long long)d;
                }
                *slot = Value::MakeInt((int64_t)r);
                return true;
            }
            if (v.IsNull()) { *slot = Value::MakeInt(0); return true; }
            RuntimeError("InvalidCastException: Cannot convert '%s' to int.", v.TypeName().c_str());
            return false;
        }
        if (type == "float" || type == "double")
        {
            if (v.IsFloat()) return true;
            if (v.IsInt()) { *slot = Value::MakeFloat((double)v.AsInt()); return true; }
            if (v.IsBool()) { *slot = Value::MakeFloat(v.AsBool() ? 1.0 : 0.0); return true; }
            if (v.IsString())
            {
                const std::string& s = v.AsString()->Value;
                char* end = nullptr;
                double d = std::strtod(s.c_str(), &end);
                if (end == s.c_str())
                {
                    RuntimeError("FormatException: Cannot convert \"%s\" to float.", s.c_str());
                    return false;
                }
                *slot = Value::MakeFloat(d);
                return true;
            }
            if (v.IsNull()) { *slot = Value::MakeFloat(0.0); return true; }
            RuntimeError("InvalidCastException: Cannot convert '%s' to float.", v.TypeName().c_str());
            return false;
        }
        if (type == "bool")
        {
            if (v.IsString())
            {
                const std::string& s = v.AsString()->Value;
                *slot = Value::MakeBool(s == "true" || s == "True" || s == "1");
                return true;
            }
            *slot = Value::MakeBool(v.IsTruthy());
            return true;
        }
        if (type == "string")
        {
            if (v.IsString()) return true;
            std::string s;
            if (!ValueToString(v, s)) return false;
            *slot = Value::MakeString(s);
            return true;
        }
        // Sınıf / koleksiyon: C# "as" gibi uyumsuzsa null
        if (!IsInstanceOfType(v, type))
            *slot = Value();
        return true;
    }

    bool CQSVM::NewArray(Value* slot, const std::string& elementType)
    {
        if (!slot->IsNumber())
        {
            RuntimeError("TypeError: Array size must be a number.");
            return false;
        }
        int64_t n = slot->ToInt();
        if (n < 0 || n > 100000000)
        {
            RuntimeError("ArgumentOutOfRangeException: Invalid array size %lld.", (long long)n);
            return false;
        }
        Value def;
        if (elementType == "int" || elementType == "long" || elementType == "byte" || elementType == "short" || elementType == "uint") def = Value::MakeInt(0);
        else if (elementType == "float" || elementType == "double") def = Value::MakeFloat(0.0);
        else if (elementType == "bool") def = Value::MakeBool(false);
        else if (elementType == "string") def = Value::MakeString("");
        auto* list = new CQSListObject();
        list->Elements.assign((size_t)n, def);
        *slot = Value::MakeObject(list);
        return true;
    }

    Value CQSVM::MakeStringValue(std::string&& s)
    {
        return Value::MakeObject(new CQSStringObject(std::move(s)));
    }

    // ════════════════════════════════════════
    //  Arithmetic (slow paths)
    // ════════════════════════════════════════
    static bool IsVectorMap(const Value& v, int& dims)
    {
        if (!v.IsMap()) return false;
        auto& e = v.AsMap()->Entries;
        auto num = [&](const char* k) { auto it = e.find(k); return it != e.end() && it->second.IsNumber(); };
        if (!num("x") || !num("y")) return false;
        dims = num("z") ? (num("w") ? 4 : 3) : 2;
        return true;
    }

    bool CQSVM::ArithSlow(OpCode op, Value* a, const Value* b)
    {
        // Sayılar (karışık int/float, bool)
        if ((a->IsNumber() || a->IsBool()) && (b->IsNumber() || b->IsBool()) && !(a->IsBool() && b->IsBool() && op == OpCode::Add))
        {
            bool ints = (a->IsInt() || a->IsBool()) && (b->IsInt() || b->IsBool());
            int64_t ia = a->ToInt(), ib = b->ToInt();
            double fa = a->ToNumber(), fb = b->ToNumber();
            switch (op)
            {
                case OpCode::Add: *a = ints ? Value::MakeInt(WrapAdd(ia, ib)) : Value::MakeFloat(fa + fb); return true;
                case OpCode::Sub: *a = ints ? Value::MakeInt(WrapSub(ia, ib)) : Value::MakeFloat(fa - fb); return true;
                case OpCode::Mul: *a = ints ? Value::MakeInt(WrapMul(ia, ib)) : Value::MakeFloat(fa * fb); return true;
                case OpCode::Div:
                    if (ints && ib == 0)
                    {
                        RuntimeError("DivideByZeroException: Attempted to divide by zero.");
                        return false;
                    }
                    *a = Value::MakeFloat(fa / fb);
                    return true;
                case OpCode::Mod:
                    if (ints)
                    {
                        if (ib == 0)
                        {
                            RuntimeError("DivideByZeroException: Modulo by zero.");
                            return false;
                        }
                        *a = Value::MakeInt(ib == -1 ? 0 : ia % ib);
                    }
                    else
                        *a = Value::MakeFloat(std::fmod(fa, fb));
                    return true;
                case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor: case OpCode::Shl: case OpCode::Shr:
                    if (a->IsBool() && b->IsBool())
                    {
                        bool x = a->AsBool(), y = b->AsBool();
                        if (op == OpCode::BitAnd) { *a = Value::MakeBool(x && y); return true; }
                        if (op == OpCode::BitOr) { *a = Value::MakeBool(x || y); return true; }
                        if (op == OpCode::BitXor) { *a = Value::MakeBool(x != y); return true; }
                    }
                    if (!ints)
                    {
                        RuntimeError("TypeError: Bitwise operators require integers.");
                        return false;
                    }
                    switch (op)
                    {
                        case OpCode::BitAnd: *a = Value::MakeInt(ia & ib); return true;
                        case OpCode::BitOr: *a = Value::MakeInt(ia | ib); return true;
                        case OpCode::BitXor: *a = Value::MakeInt(ia ^ ib); return true;
                        case OpCode::Shl: *a = Value::MakeInt((int64_t)((uint64_t)ia << (ib & 63))); return true;
                        case OpCode::Shr: *a = Value::MakeInt(ia >> (ib & 63)); return true;
                        default: break;
                    }
                    break;
                default: break;
            }
        }
        if (op == OpCode::Add)
        {
            // Metin birleştirme
            if (a->IsString() || b->IsString())
            {
                std::string sa, sb;
                if (!ValueToString(*a, sa) || !ValueToString(*b, sb))
                    return false;
                sa += sb;
                *a = MakeStringValue(std::move(sa));
                return true;
            }
            if (a->IsList() && b->IsList())
            {
                auto* list = new CQSListObject();
                list->Elements = a->AsList()->Elements;
                auto& eb = b->AsList()->Elements;
                list->Elements.insert(list->Elements.end(), eb.begin(), eb.end());
                *a = Value::MakeObject(list);
                return true;
            }
        }
        // Vektör haritaları: {x,y,z} + {x,y,z}, vec * sayı, sayı * vec, vec / sayı
        int da = 0, db = 0;
        bool va = IsVectorMap(*a, da), vb = IsVectorMap(*b, db);
        if ((va || vb) && (op == OpCode::Add || op == OpCode::Sub || op == OpCode::Mul || op == OpCode::Div))
        {
            static const char* comps[] = { "x", "y", "z", "w" };
            int dims = std::max(da, db);
            auto* out = new CQSMapObject();
            for (int i = 0; i < dims; ++i)
            {
                auto get = [&](const Value& v, bool isVec) -> double {
                    if (!isVec) return v.ToNumber();
                    auto& e = v.AsMap()->Entries;
                    auto it = e.find(comps[i]);
                    return it != e.end() ? it->second.ToNumber() : 0.0;
                };
                if ((va && !vb && !b->IsNumber()) || (vb && !va && !a->IsNumber()))
                {
                    RuntimeError("TypeError: Cannot apply operator to vector and '%s'.", (va ? *b : *a).TypeName().c_str());
                    return false;
                }
                double x = get(*a, va), y = get(*b, vb);
                double r = 0.0;
                switch (op)
                {
                    case OpCode::Add: r = x + y; break;
                    case OpCode::Sub: r = x - y; break;
                    case OpCode::Mul: r = x * y; break;
                    case OpCode::Div: r = x / y; break;
                    default: break;
                }
                out->Entries[comps[i]] = Value::MakeFloat(r);
            }
            *a = Value::MakeObject(out);
            return true;
        }
        const char* opName = "?";
        switch (op)
        {
            case OpCode::Add: opName = "+"; break;
            case OpCode::Sub: opName = "-"; break;
            case OpCode::Mul: opName = "*"; break;
            case OpCode::Div: opName = "/"; break;
            case OpCode::Mod: opName = "%"; break;
            case OpCode::BitAnd: opName = "&"; break;
            case OpCode::BitOr: opName = "|"; break;
            case OpCode::BitXor: opName = "^"; break;
            case OpCode::Shl: opName = "<<"; break;
            case OpCode::Shr: opName = ">>"; break;
            default: break;
        }
        RuntimeError("TypeError: Operator '%s' cannot be applied to '%s' and '%s'.", opName, a->TypeName().c_str(), b->TypeName().c_str());
        return false;
    }

    bool CQSVM::CompareSlow(OpCode op, const Value& a, const Value& b, bool& out)
    {
        if (a.IsNumber() && b.IsNumber())
        {
            double x = a.ToNumber(), y = b.ToNumber();
            switch (op)
            {
                case OpCode::Lt: out = x < y; return true;
                case OpCode::Le: out = x <= y; return true;
                case OpCode::Gt: out = x > y; return true;
                case OpCode::Ge: out = x >= y; return true;
                default: break;
            }
        }
        if (a.IsString() && b.IsString())
        {
            int c = a.AsString()->Value.compare(b.AsString()->Value);
            switch (op)
            {
                case OpCode::Lt: out = c < 0; return true;
                case OpCode::Le: out = c <= 0; return true;
                case OpCode::Gt: out = c > 0; return true;
                case OpCode::Ge: out = c >= 0; return true;
                default: break;
            }
        }
        RuntimeError("TypeError: Cannot compare '%s' and '%s'.", a.TypeName().c_str(), b.TypeName().c_str());
        return false;
    }

    // ════════════════════════════════════════
    //  GC
    // ════════════════════════════════════════
    void CQSVM::MarkRoots()
    {
        CQSHeap& heap = CQSHeap::Get();
        for (Value* v = m_Stack.get(); v < m_StackTop; ++v)
            heap.MarkValue(*v);
        for (size_t i = 0; i < m_GlobalCount; ++i)
            heap.MarkValue(m_GlobalValues[i]);
        for (CQSFunctionObject* m : m_Modules)
            heap.MarkObject(m);
        for (auto& [h, v] : m_Pinned)
            heap.MarkValue(v);
        for (CQSFrame* f = m_CurrentFrame; f; f = f->Parent)
            heap.MarkObject(f->Closure);
        for (CQSUpvalueObject* up = m_OpenUpvalues; up; up = up->NextOpen)
            heap.MarkObject(up);
        heap.MarkObject(m_CurrentInstance);
        heap.MarkValue(m_Exception);
    }

    void CQSVM::CollectGarbage()
    {
        CQ_PROFILE_SAMPLE("GC.Collect", GC);
        CQSHeap& heap = CQSHeap::Get();
        if (heap.IsDisabled())
            return;
        // Kalıcı kökler (native köprüsü tabloları) her zaman: hiç VM kalmasa bile sonraki VM'ler kullanır
        heap.MarkPersistentRoots();
        // Tüm canlı VM'lerin köklerini işaretle (yığın paylaşılır)
        for (CQSVM* vm : LiveVMs())
            vm->MarkRoots();
        heap.TraceReferences();
        size_t freed = heap.Sweep();
        ++m_Stats.GCRuns;
        m_Stats.GCFreed += freed;
    }

    std::vector<CQSVM*>& CQSVM::LiveVMs()
    {
        static auto* s_VMs = new std::vector<CQSVM*>(); // süreç sonunda yıkım sırasından bağımsız
        return *s_VMs;
    }
}
