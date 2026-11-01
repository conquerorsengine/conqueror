#include "CQSJit.h"
#include "CQSVM.h"

#include "sljitLir.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ConquerorScript JIT
// ───────────────────
// Bytecode yığın makinesidir; her komutun giriş yığın derinliği statik olarak bilinir
// (derleyici dengeyi garanti eder, burada veri akışı analiziyle doğrulanır). Bu sayede işlenen
// yığını slotları sabit [taban + derinlik*16] adreslerine dönüşür ve sljit ile yerel koda çevrilir:
//   S0 = CQSVM*, S1 = CQSFrame*, S2 = çerçeve tabanı (Value*)
// Sık işlemler (sabitler, yereller, globaller, int/float aritmetik, karşılaştırma+dal birleştirme,
// doğruluk testleri, this alanları, liste indeksleme, return, GC güvenli noktası) satır içi üretilir.
// Diğer her şey yorumlayıcıyla aynı anlamı taşıyan tek bir yardımcı (JitGeneric) üzerinden çalışır.
// İstisnalar statik olarak en içteki try işleyicisine ya da ortak hata çıkışına yönlendirilir.

namespace Conqueror::CQS
{
    namespace
    {
        static_assert(sizeof(Value) == 16, "JIT assumes 16-byte values");

        constexpr sljit_sw kPay = 8;               // Value birliği (payload) ofseti
        constexpr sljit_sw kSlot = (sljit_sw)sizeof(Value);
        constexpr int T_NULL = (int)ValueType::Null;
        constexpr int T_BOOL = (int)ValueType::Bool;
        constexpr int T_INT = (int)ValueType::Int;
        constexpr int T_FLOAT = (int)ValueType::Float;
        constexpr int T_OBJ = (int)ValueType::Object;

        // Nesne yerleşimi (çalışma anında doğrulanır)
        struct Layout
        {
            bool     Valid = false;
            bool     VectorOk = false;   // std::vector {begin, end, ...} düzeninde mi
            sljit_sw ObjType = 0;        // CQSObject::Type
            sljit_sw InstSlots = 0;      // CQSInstanceObject::Slots
            sljit_sw ListElems = 0;      // CQSListObject::Elements
            sljit_sw StackTop = 0;       // CQSVM::m_StackTop
        };

        Layout ComputeLayout(CQSVM* vm)
        {
            Layout L;
            // offsetof eşdeğeri (sanal yıkıcılı sınıflar için offsetof koşullu desteklenir)
            alignas(64) static unsigned char probe[512];
            auto* inst = reinterpret_cast<CQSInstanceObject*>(probe);
            auto* list = reinterpret_cast<CQSListObject*>(probe);
            L.ObjType = (sljit_sw)(reinterpret_cast<unsigned char*>(&static_cast<CQSObject*>(inst)->Type) - probe);
            L.InstSlots = (sljit_sw)(reinterpret_cast<unsigned char*>(&inst->Slots) - probe);
            L.ListElems = (sljit_sw)(reinterpret_cast<unsigned char*>(&list->Elements) - probe);
            L.StackTop = (sljit_sw)(reinterpret_cast<unsigned char*>(&vm->StackTopRef()) - reinterpret_cast<unsigned char*>(vm));

            std::vector<Value> v(3);
            Value* const* words = reinterpret_cast<Value* const*>(&v);
            L.VectorOk = words[0] == v.data() && words[1] == v.data() + 3 && (sljit_sw)sizeof(std::vector<Value>) >= 2 * (sljit_sw)sizeof(void*);
            L.Valid = L.ObjType >= 0 && L.ObjType < 64 && L.InstSlots > 0 && L.ListElems > 0;
            return L;
        }

        bool JitDisabledByEnv()
        {
            const char* e = std::getenv("CQS_JIT");
            return e && (e[0] == '0' || e[0] == 'n' || e[0] == 'N' || e[0] == 'f' || e[0] == 'F');
        }

        // ════════════════════════════════════════
        //  Yardımcılar (C ABI)
        // ════════════════════════════════════════

        // Tek komut (kontrol akışı hariç). sp: komut öncesi ilk boş slot. 0: tamam, 1: istisna.
        sljit_sw SLJIT_FUNC JitGeneric(CQSVM* vm, CQSFrame* frame, Value* sp, const CQSInstr* ins)
        {
            frame->IP = ins;
            vm->StackTopRef() = sp;
            CQSFunctionObject* fn = frame->Closure->Function;
            Value* base = frame->Base;
            switch ((OpCode)ins->Op)
            {
                case OpCode::Dup2: sp[0] = sp[-2]; sp[1] = sp[-1]; return 0;
                case OpCode::Swap: { Value t = sp[-1]; sp[-1] = sp[-2]; sp[-2] = t; return 0; }
                case OpCode::Bury:
                {
                    Value v = sp[-1];
                    int n = ins->B;
                    std::memmove((void*)(sp - n), (const void*)(sp - n - 1), sizeof(Value) * (size_t)n);
                    sp[-n - 1] = v;
                    return 0;
                }
                case OpCode::GetUpval: sp[0] = *frame->Closure->Upvalues[ins->B]->Location; return 0;
                case OpCode::SetUpval: *frame->Closure->Upvalues[ins->B]->Location = sp[-1]; return 0;
                case OpCode::CloseUpval: vm->CloseUpvalues(sp - 1); return 0;
                case OpCode::GetGlobal:
                {
                    size_t g = (size_t)ins->C;
                    if (!vm->GlobalDefinedFlags()[g])
                    {
                        vm->RuntimeError("NameError: Undefined variable '%s'.", vm->GetGlobalName((int)g).c_str());
                        return 1;
                    }
                    sp[0] = vm->GlobalValues()[g];
                    return 0;
                }
                case OpCode::SetGlobal:
                {
                    size_t g = (size_t)ins->C;
                    if (!vm->GlobalDefinedFlags()[g])
                    {
                        vm->RuntimeError("NameError: Undefined variable '%s' (declare it with 'var').", vm->GetGlobalName((int)g).c_str());
                        return 1;
                    }
                    vm->GlobalValues()[g] = sp[-1];
                    return 0;
                }
                case OpCode::DefineGlobal:
                    vm->GlobalValues()[ins->C] = sp[-1];
                    vm->GlobalDefinedFlags()[ins->C] = 1;
                    return 0;
                case OpCode::GetDynamic:
                    return vm->GetDynamic(sp - 1, sp[-1], fn->Names[ins->B], ins->C) ? 0 : 1;
                case OpCode::GetProp:
                    return vm->GetProperty(sp - 1, fn->Names[ins->B], &fn->Caches[(size_t)ins->C]) ? 0 : 1;
                case OpCode::SetProp:
                    if (!vm->SetProperty(sp[-2], fn->Names[ins->B], sp[-1], &fn->Caches[(size_t)ins->C])) return 1;
                    sp[-2] = sp[-1];
                    return 0;
                case OpCode::GetField:
                {
                    Value& o = sp[-1];
                    if (o.IsInstance() && ins->B < o.AsInstance()->Slots.size()) { o = o.AsInstance()->Slots[ins->B]; return 0; }
                    vm->RuntimeError(o.IsNull() ? "NullReferenceException: Cannot read a field of null." : "TypeError: Field access on a non-instance value.");
                    return 1;
                }
                case OpCode::SetField:
                {
                    Value& o = sp[-2];
                    if (o.IsInstance() && ins->B < o.AsInstance()->Slots.size())
                    {
                        o.AsInstance()->Slots[ins->B] = sp[-1];
                        sp[-2] = sp[-1];
                        return 0;
                    }
                    vm->RuntimeError(o.IsNull() ? "NullReferenceException: Cannot set a field of null." : "TypeError: Field assignment on a non-instance value.");
                    return 1;
                }
                case OpCode::GetThisField:
                {
                    const Value& self = base[0];
                    if (self.IsInstance() && ins->B < self.AsInstance()->Slots.size()) { sp[0] = self.AsInstance()->Slots[ins->B]; return 0; }
                    vm->RuntimeError("TypeError: 'this' is not an instance of the expected class.");
                    return 1;
                }
                case OpCode::SetThisField:
                {
                    const Value& self = base[0];
                    if (self.IsInstance() && ins->B < self.AsInstance()->Slots.size()) { self.AsInstance()->Slots[ins->B] = sp[-1]; return 0; }
                    vm->RuntimeError("TypeError: 'this' is not an instance of the expected class.");
                    return 1;
                }
                case OpCode::GetIndex:
                    return vm->GetIndex(sp - 2, sp[-1]) ? 0 : 1;
                case OpCode::SetIndex:
                    if (!vm->SetIndex(sp[-3], sp[-2], sp[-1])) return 1;
                    sp[-3] = sp[-1];
                    return 0;
                case OpCode::GetSuper:
                    return vm->GetSuperMethod(sp - 1, fn->Names[ins->B]) ? 0 : 1;

                case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
                case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor: case OpCode::Shl: case OpCode::Shr:
                {
                    Value* a = sp - 2;
                    const Value* b = sp - 1;
                    OpCode op = (OpCode)ins->Op;
                    // Yorumlayıcıyla aynı hızlı yollar (JIT satır içi yolunu kaçıran durumlar)
                    if (a->IsInt() && b->IsInt())
                    {
                        int64_t x = a->IntVal, y = b->IntVal;
                        switch (op)
                        {
                            case OpCode::Add: a->IntVal = (int64_t)((uint64_t)x + (uint64_t)y); return 0;
                            case OpCode::Sub: a->IntVal = (int64_t)((uint64_t)x - (uint64_t)y); return 0;
                            case OpCode::Mul: a->IntVal = (int64_t)((uint64_t)x * (uint64_t)y); return 0;
                            case OpCode::Mod: if (y != 0) { a->IntVal = (y == -1) ? 0 : x % y; return 0; } break;
                            case OpCode::BitAnd: a->IntVal = x & y; return 0;
                            case OpCode::BitOr: a->IntVal = x | y; return 0;
                            case OpCode::BitXor: a->IntVal = x ^ y; return 0;
                            case OpCode::Shl: a->IntVal = (int64_t)((uint64_t)x << (y & 63)); return 0;
                            case OpCode::Shr: a->IntVal = x >> (y & 63); return 0;
                            default: break;
                        }
                    }
                    else if (a->IsNumber() && b->IsNumber() && (op == OpCode::Add || op == OpCode::Sub || op == OpCode::Mul || op == OpCode::Div))
                    {
                        double x = a->ToNumber(), y = b->ToNumber();
                        double r = op == OpCode::Add ? x + y : op == OpCode::Sub ? x - y : op == OpCode::Mul ? x * y : x / y;
                        *a = Value::MakeFloat(r);
                        return 0;
                    }
                    return vm->ArithSlow(op, a, b) ? 0 : 1;
                }
                case OpCode::Neg:
                {
                    Value& v = sp[-1];
                    if (v.IsInt()) { v.IntVal = (int64_t)(0 - (uint64_t)v.IntVal); return 0; }
                    if (v.IsFloat()) { v.FloatVal = -v.FloatVal; return 0; }
                    Value minusOne = Value::MakeInt(-1);
                    return vm->ArithSlow(OpCode::Mul, &v, &minusOne) ? 0 : 1;
                }
                case OpCode::Not: sp[-1] = Value::MakeBool(!sp[-1].IsTruthy()); return 0;
                case OpCode::BitNot:
                    if (!sp[-1].IsInt()) { vm->RuntimeError("TypeError: '~' requires an integer."); return 1; }
                    sp[-1].IntVal = ~sp[-1].IntVal;
                    return 0;
                case OpCode::Eq: sp[-2] = Value::MakeBool(sp[-2].Equals(sp[-1])); return 0;
                case OpCode::Ne: sp[-2] = Value::MakeBool(!sp[-2].Equals(sp[-1])); return 0;
                case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge:
                {
                    const Value& a = sp[-2];
                    const Value& b = sp[-1];
                    bool r;
                    if (a.IsNumber() && b.IsNumber())
                    {
                        double x = a.ToNumber(), y = b.ToNumber();
                        if (a.IsInt() && b.IsInt())
                        {
                            int64_t ix = a.IntVal, iy = b.IntVal;
                            r = (OpCode)ins->Op == OpCode::Lt ? ix < iy : (OpCode)ins->Op == OpCode::Le ? ix <= iy : (OpCode)ins->Op == OpCode::Gt ? ix > iy : ix >= iy;
                        }
                        else
                            r = (OpCode)ins->Op == OpCode::Lt ? x < y : (OpCode)ins->Op == OpCode::Le ? x <= y : (OpCode)ins->Op == OpCode::Gt ? x > y : x >= y;
                    }
                    else if (!vm->CompareSlow((OpCode)ins->Op, a, b, r))
                        return 1;
                    sp[-2] = Value::MakeBool(r);
                    return 0;
                }
                case OpCode::ToBool: sp[-1] = Value::MakeBool(sp[-1].IsTruthy()); return 0;
                case OpCode::MakeRange:
                    if (!sp[-2].IsNumber() || !sp[-1].IsNumber()) { vm->RuntimeError("TypeError: Range bounds must be numbers."); return 1; }
                    sp[-2] = Value::MakeObject(new CQSRangeObject(sp[-2].ToInt(), sp[-1].ToInt()));
                    return 0;
                case OpCode::IsType: sp[-1] = Value::MakeBool(vm->IsInstanceOfType(sp[-1], fn->Names[ins->B])); return 0;
                case OpCode::CastType: return vm->CastToType(sp - 1, fn->Names[ins->B]) ? 0 : 1;

                case OpCode::Call:
                {
                    Value* callBase = sp - ins->A - 1;
                    if (CQSHeap::Get().ShouldCollect()) vm->CollectGarbage();
                    return vm->CallValue(callBase, ins->A) ? 0 : 1;
                }
                case OpCode::Invoke:
                {
                    Value* callBase = sp - ins->A - 1;
                    if (CQSHeap::Get().ShouldCollect()) vm->CollectGarbage();
                    return vm->InvokeMethod(callBase, ins->A, fn->Names[ins->B], &fn->Caches[(size_t)ins->C]) ? 0 : 1;
                }
                case OpCode::SuperInvoke:
                    return vm->SuperInvoke(sp - ins->A - 1, ins->A, fn->Names[ins->B]) ? 0 : 1;

                case OpCode::Closure:
                {
                    auto* proto = fn->Constants[ins->B].AsFunction();
                    auto* closure = new CQSClosureObject(proto);
                    for (size_t i = 0; i < proto->Upvalues.size(); ++i)
                    {
                        const CQSUpvalueDesc& d = proto->Upvalues[i];
                        closure->Upvalues[i] = d.IsLocal ? vm->CaptureUpvalue(base + d.Index) : frame->Closure->Upvalues[d.Index];
                    }
                    sp[0] = Value::MakeObject(closure);
                    return 0;
                }
                case OpCode::Class: return vm->CreateClass(sp, fn->Constants[ins->B].AsClass()) ? 0 : 1;
                case OpCode::Inherit: return vm->InheritClass(sp - 2, sp[-1], ins->A != 0) ? 0 : 1;
                case OpCode::Method: return vm->DefineMethod(sp - 2, fn->Names[ins->B], sp[-1], ins->A) ? 0 : 1;
                case OpCode::StaticField: sp[-2].AsClass()->Statics[fn->Names[ins->B]] = sp[-1]; return 0;
                case OpCode::NewList:
                {
                    auto* list = new CQSListObject();
                    list->Elements.assign(sp - ins->B, sp);
                    sp[-(int)ins->B] = Value::MakeObject(list);
                    return 0;
                }
                case OpCode::NewMap:
                {
                    auto* map = new CQSMapObject();
                    Value* p = sp - 2 * ins->B;
                    for (int i = 0; i < ins->B; ++i)
                    {
                        const Value& key = p[2 * i];
                        map->Entries[key.IsString() ? key.AsString()->Value : key.ToString()] = p[2 * i + 1];
                    }
                    p[0] = Value::MakeObject(map);
                    return 0;
                }
                case OpCode::IterPrep: return vm->IterPrep(sp - 1, ins->A) ? 0 : 1;
                case OpCode::Throw:
                    vm->SetException(sp[-1]);
                    return 1;
                case OpCode::ToStr:
                {
                    std::string s;
                    bool ok = ins->B == 0xFFFF ? vm->ValueToString(sp[-1], s) : vm->FormatValue(sp[-1], fn->Names[ins->B], s);
                    if (!ok) return 1;
                    sp[-1] = vm->MakeStringValue(std::move(s));
                    return 0;
                }
                case OpCode::Concat:
                {
                    int n = ins->B;
                    std::string out;
                    for (Value* v = sp - n; v < sp; ++v)
                    {
                        if (v->IsString()) out += v->AsString()->Value;
                        else
                        {
                            std::string part;
                            if (!vm->ValueToString(*v, part)) return 1;
                            out += part;
                        }
                    }
                    sp[-n] = vm->MakeStringValue(std::move(out));
                    return 0;
                }
                case OpCode::NewArray: return vm->NewArray(sp - 1, fn->Names[ins->B]) ? 0 : 1;
                default:
                    vm->RuntimeError("InternalError: JIT generic helper cannot execute opcode %s.", OpCodeName((OpCode)ins->Op));
                    return 1;
            }
        }

        sljit_sw SLJIT_FUNC JitTruthy(Value* v)
        {
            return v->IsTruthy() ? 1 : 0;
        }

        // 1: öğe var, 0: bitti, 2: hata
        sljit_sw SLJIT_FUNC JitIterNext(CQSVM* vm, CQSFrame* frame, Value* sp, const CQSInstr* ins)
        {
            frame->IP = ins;
            vm->StackTopRef() = sp;
            int r = vm->IterNext(frame->Base + ins->B, sp, ins->A);
            return r > 0 ? 1 : (r == 0 ? 0 : 2);
        }

        sljit_sw SLJIT_FUNC JitEnterHandler(CQSVM* vm, Value* newTop)
        {
            vm->CloseUpvalues(newTop);
            *newTop = vm->TakeException();
            vm->StackTopRef() = newTop + 1;
            return 0;
        }

        sljit_sw SLJIT_FUNC JitCloseFrame(CQSVM* vm, Value* base)
        {
            vm->CloseUpvalues(base);
            return 0;
        }

        sljit_sw SLJIT_FUNC JitSafepoint(CQSVM* vm, CQSFrame* frame, Value* sp, const CQSInstr* ins)
        {
            frame->IP = ins;
            vm->StackTopRef() = sp;
            if (CQSHeap::Get().ShouldCollect())
                vm->CollectGarbage();
            return 0;
        }

        // ════════════════════════════════════════
        //  Kod üretici
        // ════════════════════════════════════════
        class Codegen
        {
        public:
            Codegen(CQSVM* vm, CQSFunctionObject* fn, const Layout& layout)
                : m_VM(vm), m_Fn(fn), m_L(layout) {}

            ~Codegen()
            {
                if (m_C) sljit_free_compiler(m_C);
            }

            bool Analyze()
            {
                const auto& code = m_Fn->Code;
                size_t n = code.size();
                if (n == 0 || n > 200000)
                    return false;
                m_Depth.assign(n, -1);
                m_Target.assign(n, 0);
                std::vector<size_t> work;
                auto push = [&](size_t pc, int d) -> bool {
                    if (pc >= n || d < 1 || d > m_Fn->MaxStack + 1)
                        return false;
                    if (m_Depth[pc] < 0)
                    {
                        m_Depth[pc] = d;
                        work.push_back(pc);
                        return true;
                    }
                    return m_Depth[pc] == d;
                };
                if (!push(0, 1 + m_Fn->Arity))
                    return false;
                for (const auto& h : m_Fn->Handlers)
                {
                    if (h.Handler >= n) return false;
                    m_Target[h.Handler] = 1;
                    if (!push(h.Handler, (int)h.StackDepth + 1)) return false;
                }
                while (!work.empty())
                {
                    size_t pc = work.back();
                    work.pop_back();
                    const CQSInstr& ins = code[pc];
                    OpCode op = (OpCode)ins.Op;
                    if ((size_t)ins.Op >= (size_t)OpCode::Count_)
                        return false;
                    int d = m_Depth[pc];
                    int after = d + StackEffect(op, ins.A, ins.B);
                    if (after < 1)
                        return false;
                    auto target = [&](int depth) -> bool {
                        if (ins.C < 0 || (size_t)ins.C >= n) return false;
                        m_Target[(size_t)ins.C] = 1;
                        return push((size_t)ins.C, depth);
                    };
                    switch (op)
                    {
                        case OpCode::Return: case OpCode::ReturnNull: case OpCode::Throw:
                            break;
                        case OpCode::Jump: case OpCode::Loop:
                            if (!target(d)) return false;
                            break;
                        case OpCode::JumpIfFalse: case OpCode::JumpIfTrue:
                            if (!target(d - 1) || !push(pc + 1, d - 1)) return false;
                            break;
                        case OpCode::JumpIfNullKeep: case OpCode::JumpIfArg:
                            if (!target(d) || !push(pc + 1, d)) return false;
                            break;
                        case OpCode::JumpIfNotNullKeep:
                            if (!target(d) || !push(pc + 1, d - 1)) return false;
                            break;
                        case OpCode::IterNext:
                            if (!target(d) || !push(pc + 1, d + ins.A)) return false;
                            break;
                        default:
                            if (!push(pc + 1, after)) return false;
                            break;
                    }
                }
                // Yakalanan yerel var mı (return'de upvalue kapatma gerekir)
                for (const Value& k : m_Fn->Constants)
                    if (k.IsObjType(ObjectType::Function))
                        for (auto& u : k.AsFunction()->Upvalues)
                            if (u.IsLocal) m_HasCaptures = true;
                return true;
            }

            void* Generate(size_t& outSize)
            {
                m_C = sljit_create_compiler(nullptr);
                if (!m_C)
                    return nullptr;
                const auto& code = m_Fn->Code;
                size_t n = code.size();
                // (vm, frame, startPc): startPc != 0 -> döngü başlığına giriş (OSR, yorumlayıcıdan sıcak döngü geçişi)
                sljit_emit_enter(m_C, 0, SLJIT_ARGS3(W, P, P, W), 4 | SLJIT_ENTER_FLOAT(2), 3, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R3, 0, SLJIT_S2, 0);
                sljit_emit_op1(m_C, SLJIT_MOV_P, SLJIT_S2, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)offsetof(CQSFrame, Base));
                for (size_t pc = 0; pc < n; ++pc)
                {
                    const CQSInstr& ins = code[pc];
                    if ((OpCode)ins.Op == OpCode::Loop && ins.C >= 0 && (size_t)ins.C < n && m_Depth[(size_t)ins.C] >= 0 && !m_OsrEntries.count(ins.C))
                    {
                        m_OsrEntries.insert(ins.C);
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ins.C), ins.C);
                    }
                }

                m_Labels.assign(n, nullptr);
                m_HandlerJumps.assign(m_Fn->Handlers.size(), {});
                for (size_t pc = 0; pc < n; ++pc)
                {
                    m_Labels[pc] = sljit_emit_label(m_C);
                    if (m_Depth[pc] < 0)
                        continue; // erişilemez
                    if (!EmitInstruction(pc))
                        return nullptr;
                    if (sljit_get_compiler_error(m_C))
                        return nullptr;
                }
                // Hata çıkışı
                sljit_label* errorLabel = sljit_emit_label(m_C);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S2, 0);
                sljit_emit_icall(m_C, SLJIT_CALL, SLJIT_ARGS2(W, P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(JitCloseFrame));
                sljit_emit_return(m_C, SLJIT_MOV, SLJIT_IMM, 0);
                for (sljit_jump* j : m_ErrorJumps)
                    sljit_set_label(j, errorLabel);
                // try işleyicileri: yığını işleyici derinliğine indir, istisnayı it, işleyiciye atla
                for (size_t h = 0; h < m_Fn->Handlers.size(); ++h)
                {
                    const CQSTryHandler& th = m_Fn->Handlers[h];
                    sljit_label* hl = sljit_emit_label(m_C);
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
                    sljit_emit_op2(m_C, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S2, 0, SLJIT_IMM, (sljit_sw)th.StackDepth * kSlot);
                    sljit_emit_icall(m_C, SLJIT_CALL, SLJIT_ARGS2(W, P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(JitEnterHandler));
                    sljit_jump* j = sljit_emit_jump(m_C, SLJIT_JUMP);
                    m_Pending.emplace_back(j, (int)th.Handler);
                    for (sljit_jump* e : m_HandlerJumps[h])
                        sljit_set_label(e, hl);
                }
                for (auto& [jump, pc] : m_Pending)
                    sljit_set_label(jump, m_Labels[(size_t)pc]);
                if (sljit_get_compiler_error(m_C))
                    return nullptr;
                void* entry = sljit_generate_code(m_C, 0, nullptr);
                outSize = entry ? (size_t)sljit_get_generated_code_size(m_C) : 0;
                return entry;
            }

        private:
            CQSVM* m_VM;
            CQSFunctionObject* m_Fn;
            Layout m_L;
            sljit_compiler* m_C = nullptr;
            std::vector<int> m_Depth;
            std::vector<char> m_Target;
            std::vector<sljit_label*> m_Labels;
            std::vector<std::pair<sljit_jump*, int>> m_Pending;
            std::vector<sljit_jump*> m_ErrorJumps;
            std::vector<std::vector<sljit_jump*>> m_HandlerJumps;
            bool m_HasCaptures = false;

            static sljit_sw Off(int slot) { return (sljit_sw)slot * kSlot; }

            // pc'deki bir hatanın gideceği yer
            void BranchToError(sljit_jump* j, size_t pc)
            {
                for (size_t h = 0; h < m_Fn->Handlers.size(); ++h)
                {
                    const CQSTryHandler& th = m_Fn->Handlers[h];
                    if (pc >= th.Start && pc < th.End)
                    {
                        m_HandlerJumps[h].push_back(j);
                        return;
                    }
                }
                m_ErrorJumps.push_back(j);
            }

            void JumpTo(sljit_s32 type, int targetPc)
            {
                m_Pending.emplace_back(sljit_emit_jump(m_C, type), targetPc);
            }

            // R0 = helper(vm, frame, base + depth, ins)
            void CallHelper(sljit_sw fnAddr, int depth, size_t pc)
            {
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S1, 0);
                sljit_emit_op2(m_C, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S2, 0, SLJIT_IMM, Off(depth));
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)&m_Fn->Code[pc]);
                sljit_emit_icall(m_C, SLJIT_CALL, SLJIT_ARGS4(W, P, P, P, P), SLJIT_IMM, fnAddr);
            }

            // Genel yardımcı + hata denetimi
            void Generic(size_t pc)
            {
                CallHelper(SLJIT_FUNC_ADDR(JitGeneric), m_Depth[pc], pc);
                BranchToError(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0), pc);
            }

            // ── Değer kopyalama ──
            void CopySlot(int dst, int src)
            {
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(src));
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(src) + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(dst), SLJIT_R0, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(dst) + kPay, SLJIT_R1, 0);
            }

            void CopyFromAbs(int dst, const Value* src)
            {
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)src);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM0(), (sljit_sw)src + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(dst), SLJIT_R0, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(dst) + kPay, SLJIT_R1, 0);
            }

            void CopyToAbs(Value* dst, int src)
            {
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(src));
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(src) + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM0(), (sljit_sw)dst, SLJIT_R0, 0);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM0(), (sljit_sw)dst + kPay, SLJIT_R1, 0);
            }

            void StoreType(int slot, int type)
            {
                sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_S2), Off(slot), SLJIT_IMM, type);
            }

            void StoreBoolFromR(int slot, sljit_s32 reg)
            {
                StoreType(slot, T_BOOL);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(slot) + kPay, SLJIT_IMM, 0);
                sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_S2), Off(slot) + kPay, reg, 0);
            }

            void LoadType(sljit_s32 reg, int slot)
            {
                sljit_emit_op1(m_C, SLJIT_MOV_U8, reg, 0, SLJIT_MEM1(SLJIT_S2), Off(slot));
            }

            // ── Aritmetik: int/int ve float/float satır içi ──
            void EmitArith(size_t pc, OpCode op)
            {
                int d = m_Depth[pc];
                int a = d - 2, b = d - 1;
                sljit_jump* slow[4];
                int nslow = 0;
                sljit_jump* done[2];
                int ndone = 0;
                LoadType(SLJIT_R0, a);
                LoadType(SLJIT_R1, b);
                if (op != OpCode::Div)
                {
                    sljit_jump* notIntA = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT);
                    slow[nslow++] = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, T_INT);
                    sljit_s32 iop = op == OpCode::Add ? SLJIT_ADD : op == OpCode::Sub ? SLJIT_SUB : SLJIT_MUL;
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                    sljit_emit_op2(m_C, iop, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay, SLJIT_R2, 0);
                    done[ndone++] = sljit_emit_jump(m_C, SLJIT_JUMP);
                    sljit_set_label(notIntA, sljit_emit_label(m_C));
                }
                // float / float  (Div: int/int da float'a çevrilerek)
                sljit_jump* aNotFloat = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_FLOAT);
                slow[nslow++] = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, T_FLOAT);
                sljit_s32 fop = op == OpCode::Add ? SLJIT_ADD_F64 : op == OpCode::Sub ? SLJIT_SUB_F64 : op == OpCode::Mul ? SLJIT_MUL_F64 : SLJIT_DIV_F64;
                sljit_emit_fop1(m_C, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                sljit_emit_fop2(m_C, fop, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                sljit_emit_fop1(m_C, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay, SLJIT_FR0, 0);
                done[ndone++] = sljit_emit_jump(m_C, SLJIT_JUMP);
                sljit_label* aNotFloatL = sljit_emit_label(m_C);
                sljit_set_label(aNotFloat, aNotFloatL);
                if (op == OpCode::Div)
                {
                    // int / int (bölen sıfır değilse) -> float
                    slow[nslow++] = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT);
                    slow[nslow++] = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, T_INT);
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                    sljit_jump* zero = sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
                    sljit_emit_fop1(m_C, SLJIT_CONV_F64_FROM_SW, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                    sljit_emit_fop1(m_C, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R2, 0);
                    sljit_emit_fop2(m_C, SLJIT_DIV_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
                    sljit_emit_fop1(m_C, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay, SLJIT_FR0, 0);
                    StoreType(a, T_FLOAT);
                    sljit_jump* d2 = sljit_emit_jump(m_C, SLJIT_JUMP);
                    sljit_label* slowL = sljit_emit_label(m_C);
                    sljit_set_label(zero, slowL);
                    for (int i = 0; i < nslow; ++i) sljit_set_label(slow[i], slowL);
                    Generic(pc);
                    sljit_label* doneL = sljit_emit_label(m_C);
                    sljit_set_label(d2, doneL);
                    for (int i = 0; i < ndone; ++i) sljit_set_label(done[i], doneL);
                    return;
                }
                sljit_label* slowL = aNotFloatL; // a ne int ne float: yavaş yol
                for (int i = 0; i < nslow; ++i) sljit_set_label(slow[i], slowL);
                Generic(pc);
                sljit_label* doneL = sljit_emit_label(m_C);
                for (int i = 0; i < ndone; ++i) sljit_set_label(done[i], doneL);
            }

            // Yalnızca int/int satır içi (bit işlemleri, mod); diğerleri genel yardımcı
            void EmitIntBinary(size_t pc, OpCode op)
            {
                int d = m_Depth[pc];
                int a = d - 2, b = d - 1;
                std::vector<sljit_jump*> slow;
                LoadType(SLJIT_R0, a);
                slow.push_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT));
                LoadType(SLJIT_R0, b);
                slow.push_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT));
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                switch (op)
                {
                    case OpCode::BitAnd: sljit_emit_op2(m_C, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0); break;
                    case OpCode::BitOr:  sljit_emit_op2(m_C, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0); break;
                    case OpCode::BitXor: sljit_emit_op2(m_C, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0); break;
                    case OpCode::Shl:
                        sljit_emit_op2(m_C, SLJIT_AND, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 63);
                        sljit_emit_op2(m_C, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
                        break;
                    case OpCode::Shr:
                        sljit_emit_op2(m_C, SLJIT_AND, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 63);
                        sljit_emit_op2(m_C, SLJIT_ASHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
                        break;
                    case OpCode::Mod:
                    {
                        // y == 0 -> hata (genel yol); y == -1 -> 0 (INT_MIN % -1 taşmasını önle)
                        slow.push_back(sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0));
                        sljit_jump* notMinusOne = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, -1);
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
                        sljit_jump* store = sljit_emit_jump(m_C, SLJIT_JUMP);
                        sljit_set_label(notMinusOne, sljit_emit_label(m_C));
                        // R0 = R0 % R1 (işaretli; DIVMOD kalanı R1'e yazar)
                        sljit_emit_op0(m_C, SLJIT_DIVMOD_SW);
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_R1, 0);
                        sljit_set_label(store, sljit_emit_label(m_C));
                        break;
                    }
                    default: break;
                }
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay, SLJIT_R0, 0);
                sljit_jump* done = sljit_emit_jump(m_C, SLJIT_JUMP);
                sljit_label* slowL = sljit_emit_label(m_C);
                for (auto* j : slow) sljit_set_label(j, slowL);
                Generic(pc);
                sljit_set_label(done, sljit_emit_label(m_C));
            }

            static sljit_s32 IntCond(OpCode op)
            {
                switch (op)
                {
                    case OpCode::Lt: return SLJIT_SIG_LESS;
                    case OpCode::Le: return SLJIT_SIG_LESS_EQUAL;
                    case OpCode::Gt: return SLJIT_SIG_GREATER;
                    case OpCode::Ge: return SLJIT_SIG_GREATER_EQUAL;
                    case OpCode::Eq: return SLJIT_EQUAL;
                    default: return SLJIT_NOT_EQUAL;
                }
            }

            static sljit_s32 FloatCond(OpCode op)
            {
                // NaN ile her sıralı karşılaştırma yanlış, != doğru (IEEE)
                switch (op)
                {
                    case OpCode::Lt: return SLJIT_ORDERED_LESS;
                    case OpCode::Le: return SLJIT_ORDERED_LESS_EQUAL;
                    case OpCode::Gt: return SLJIT_ORDERED_GREATER;
                    case OpCode::Ge: return SLJIT_ORDERED_GREATER_EQUAL;
                    case OpCode::Eq: return SLJIT_ORDERED_EQUAL;
                    default: return SLJIT_UNORDERED_OR_NOT_EQUAL;
                }
            }

            // Karşılaştırma + (isteğe bağlı) dal: branchPc >= 0 ise jumpIfTrue'ya göre hedefe atlar
            void EmitCompare(size_t pc, OpCode op, int branchTarget, bool jumpIfTrue)
            {
                int d = m_Depth[pc];
                int a = d - 2, b = d - 1;
                bool fused = branchTarget >= 0;
                LoadType(SLJIT_R0, a);
                LoadType(SLJIT_R1, b);
                sljit_jump* notIntA = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT);
                sljit_jump* slow1 = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, T_INT);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                sljit_s32 cond = IntCond(op);
                std::vector<sljit_jump*> done;
                if (fused)
                {
                    m_Pending.emplace_back(sljit_emit_cmp(m_C, jumpIfTrue ? cond : (cond ^ 1), SLJIT_R2, 0, SLJIT_R3, 0), branchTarget);
                    done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                }
                else
                {
                    sljit_s32 setFlag = (cond == SLJIT_EQUAL || cond == SLJIT_NOT_EQUAL) ? SLJIT_SET_Z :
                                        (cond == SLJIT_SIG_LESS || cond == SLJIT_SIG_GREATER_EQUAL) ? SLJIT_SET_SIG_LESS : SLJIT_SET_SIG_GREATER;
                    sljit_emit_op2u(m_C, SLJIT_SUB | setFlag, SLJIT_R2, 0, SLJIT_R3, 0);
                    sljit_emit_op_flags(m_C, SLJIT_MOV, SLJIT_R2, 0, cond);
                    StoreBoolFromR(a, SLJIT_R2);
                    done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                }
                // float / float
                sljit_set_label(notIntA, sljit_emit_label(m_C));
                sljit_jump* slow2 = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_FLOAT);
                sljit_jump* slow3 = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, T_FLOAT);
                sljit_emit_fop1(m_C, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                sljit_emit_fop1(m_C, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S2), Off(b) + kPay);
                sljit_s32 fcond = FloatCond(op);
                if (fused)
                {
                    m_Pending.emplace_back(sljit_emit_fcmp(m_C, jumpIfTrue ? fcond : (fcond ^ 1), SLJIT_FR0, 0, SLJIT_FR1, 0), branchTarget);
                    done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                }
                else
                {
                    // Bayrak yerine dal ile 0/1 üret (platformlar arası güvenli)
                    sljit_jump* t = sljit_emit_fcmp(m_C, fcond, SLJIT_FR0, 0, SLJIT_FR1, 0);
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, 0);
                    sljit_jump* j = sljit_emit_jump(m_C, SLJIT_JUMP);
                    sljit_set_label(t, sljit_emit_label(m_C));
                    sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, 1);
                    sljit_set_label(j, sljit_emit_label(m_C));
                    StoreBoolFromR(a, SLJIT_R2);
                    done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                }
                // yavaş yol: genel karşılaştırma (Bool üretir), sonra dal
                sljit_label* slowL = sljit_emit_label(m_C);
                sljit_set_label(slow1, slowL);
                sljit_set_label(slow2, slowL);
                sljit_set_label(slow3, slowL);
                Generic(pc);
                if (fused)
                {
                    sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(a) + kPay);
                    m_Pending.emplace_back(sljit_emit_cmp(m_C, jumpIfTrue ? SLJIT_NOT_EQUAL : SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0), branchTarget);
                }
                sljit_label* doneL = sljit_emit_label(m_C);
                for (sljit_jump* j : done) sljit_set_label(j, doneL);
            }

            // Koşul dalı (pop): slot doğruysa/yanlışsa hedefe
            void EmitCondJump(int slot, bool jumpIfTrue, int targetPc)
            {
                std::vector<sljit_jump*> done;
                LoadType(SLJIT_R0, slot);
                // bool
                sljit_jump* notBool = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_BOOL);
                sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(slot) + kPay);
                m_Pending.emplace_back(sljit_emit_cmp(m_C, jumpIfTrue ? SLJIT_NOT_EQUAL : SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0), targetPc);
                done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                sljit_set_label(notBool, sljit_emit_label(m_C));
                // null: her zaman yanlış
                sljit_jump* notNull = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_NULL);
                if (jumpIfTrue)
                    done.push_back(sljit_emit_jump(m_C, SLJIT_JUMP));
                else
                    m_Pending.emplace_back(sljit_emit_jump(m_C, SLJIT_JUMP), targetPc);
                sljit_set_label(notNull, sljit_emit_label(m_C));
                // diğer türler: IsTruthy
                sljit_emit_op2(m_C, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S2, 0, SLJIT_IMM, Off(slot));
                sljit_emit_icall(m_C, SLJIT_CALL, SLJIT_ARGS1(W, P), SLJIT_IMM, SLJIT_FUNC_ADDR(JitTruthy));
                m_Pending.emplace_back(sljit_emit_cmp(m_C, jumpIfTrue ? SLJIT_NOT_EQUAL : SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0), targetPc);
                sljit_label* doneL = sljit_emit_label(m_C);
                for (sljit_jump* jd : done) sljit_set_label(jd, doneL);
            }

            // R1 = nesne işaretçisi; nesne türü kind değilse slowJumps'a ekler
            void CheckObjectKind(int slot, ObjectType kind, std::vector<sljit_jump*>& slow)
            {
                LoadType(SLJIT_R0, slot);
                slow.push_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_OBJ));
                sljit_emit_op1(m_C, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(slot) + kPay);
                sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R1), m_L.ObjType);
                slow.push_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)kind));
            }

            // R2 = vektör başlangıcı (R1 + vecOff); bayt indeksi R3 sınırın dışındaysa slow
            void LoadVectorBounds(sljit_sw vecOff, sljit_s32 byteIndexReg, sljit_sw byteIndexImm, std::vector<sljit_jump*>& slow)
            {
                sljit_emit_op1(m_C, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1), vecOff);
                sljit_emit_op1(m_C, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R1), vecOff + (sljit_sw)sizeof(void*));
                sljit_emit_op2(m_C, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R2, 0);   // boyut (bayt)
                // index (bayt) < boyut  (işaretsiz: negatifler de yakalanır)
                slow.push_back(sljit_emit_cmp(m_C, SLJIT_LESS_EQUAL, SLJIT_R0, 0, byteIndexReg, byteIndexImm));
            }

            bool EmitInstruction(size_t pc)
            {
                const CQSInstr& ins = m_Fn->Code[pc];
                OpCode op = (OpCode)ins.Op;
                int d = m_Depth[pc];
                switch (op)
                {
                    case OpCode::Nop: return true;
                    case OpCode::PushNull: StoreType(d, T_NULL); sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(d) + kPay, SLJIT_IMM, 0); return true;
                    case OpCode::PushTrue:
                    case OpCode::PushFalse:
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, op == OpCode::PushTrue ? 1 : 0);
                        StoreBoolFromR(d, SLJIT_R2);
                        return true;
                    case OpCode::PushInt:
                        StoreType(d, T_INT);
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(d) + kPay, SLJIT_IMM, (sljit_sw)ins.C);
                        return true;
                    case OpCode::PushConst: CopyFromAbs(d, &m_Fn->Constants[ins.B]); return true;
                    case OpCode::Pop: case OpCode::PopN: return true;
                    case OpCode::Dup: CopySlot(d, d - 1); return true;
                    case OpCode::GetLocal: CopySlot(d, ins.B); return true;
                    case OpCode::SetLocal: CopySlot(ins.B, d - 1); return true;

                    case OpCode::GetGlobal:
                    case OpCode::SetGlobal:
                    {
                        sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)&m_VM->GlobalDefinedFlags()[ins.C]);
                        sljit_jump* undefined = sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
                        if (op == OpCode::GetGlobal) CopyFromAbs(d, &m_VM->GlobalValues()[ins.C]);
                        else CopyToAbs(&m_VM->GlobalValues()[ins.C], d - 1);
                        sljit_jump* done = sljit_emit_jump(m_C, SLJIT_JUMP);
                        sljit_set_label(undefined, sljit_emit_label(m_C));
                        Generic(pc); // hata üretir
                        sljit_set_label(done, sljit_emit_label(m_C));
                        return true;
                    }

                    case OpCode::GetThisField:
                    case OpCode::SetThisField:
                    {
                        if (!m_L.VectorOk) { Generic(pc); return true; }
                        std::vector<sljit_jump*> slow;
                        CheckObjectKind(0, ObjectType::Instance, slow);
                        LoadVectorBounds(m_L.InstSlots, SLJIT_IMM, (sljit_sw)ins.B * kSlot, slow);
                        // R2 = slot dizisi
                        if (op == OpCode::GetThisField)
                        {
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), (sljit_sw)ins.B * kSlot);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R2), (sljit_sw)ins.B * kSlot + kPay);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(d), SLJIT_R0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(d) + kPay, SLJIT_R1, 0);
                        }
                        else
                        {
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(d - 1));
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(d - 1) + kPay);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_R2), (sljit_sw)ins.B * kSlot, SLJIT_R0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_R2), (sljit_sw)ins.B * kSlot + kPay, SLJIT_R1, 0);
                        }
                        sljit_jump* done = sljit_emit_jump(m_C, SLJIT_JUMP);
                        sljit_label* slowL = sljit_emit_label(m_C);
                        for (auto* j : slow) sljit_set_label(j, slowL);
                        Generic(pc);
                        sljit_set_label(done, sljit_emit_label(m_C));
                        return true;
                    }

                    case OpCode::GetIndex:
                    case OpCode::SetIndex:
                    {
                        if (!m_L.VectorOk) { Generic(pc); return true; }
                        int objSlot = op == OpCode::GetIndex ? d - 2 : d - 3;
                        int idxSlot = objSlot + 1;
                        std::vector<sljit_jump*> slow;
                        LoadType(SLJIT_R0, idxSlot);
                        slow.push_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_INT));
                        CheckObjectKind(objSlot, ObjectType::List, slow);
                        // R3 = indeks * 16
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S2), Off(idxSlot) + kPay);
                        sljit_emit_op2(m_C, SLJIT_SHL, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, 4);
                        LoadVectorBounds(m_L.ListElems, SLJIT_R3, 0, slow);
                        sljit_emit_op2(m_C, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_R3, 0);
                        if (op == OpCode::GetIndex)
                        {
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R2), kPay);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(objSlot), SLJIT_R0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(objSlot) + kPay, SLJIT_R1, 0);
                        }
                        else
                        {
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S2), Off(d - 1));
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(d - 1) + kPay);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_R2), 0, SLJIT_R0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_R2), kPay, SLJIT_R1, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(objSlot), SLJIT_R0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), Off(objSlot) + kPay, SLJIT_R1, 0);
                        }
                        sljit_jump* done = sljit_emit_jump(m_C, SLJIT_JUMP);
                        sljit_label* slowL = sljit_emit_label(m_C);
                        for (auto* j : slow) sljit_set_label(j, slowL);
                        Generic(pc);
                        sljit_set_label(done, sljit_emit_label(m_C));
                        return true;
                    }

                    case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div:
                        EmitArith(pc, op);
                        return true;
                    case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor: case OpCode::Shl: case OpCode::Shr: case OpCode::Mod:
                        EmitIntBinary(pc, op);
                        return true;

                    case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge: case OpCode::Eq: case OpCode::Ne:
                    {
                        // Ardından JumpIfFalse/True geliyorsa (ve o bir dal hedefi değilse) birleştir
                        size_t next = pc + 1;
                        if (next < m_Fn->Code.size() && !m_Target[next])
                        {
                            OpCode nop = (OpCode)m_Fn->Code[next].Op;
                            if (nop == OpCode::JumpIfFalse || nop == OpCode::JumpIfTrue)
                            {
                                m_Fused[next] = true;
                                EmitCompare(pc, op, m_Fn->Code[next].C, nop == OpCode::JumpIfTrue);
                                return true;
                            }
                        }
                        EmitCompare(pc, op, -1, false);
                        return true;
                    }

                    case OpCode::Not:
                    {
                        LoadType(SLJIT_R0, d - 1);
                        sljit_jump* slow = sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_BOOL);
                        sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S2), Off(d - 1) + kPay);
                        sljit_emit_op2(m_C, SLJIT_XOR, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 1);
                        sljit_emit_op1(m_C, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_S2), Off(d - 1) + kPay, SLJIT_R1, 0);
                        sljit_jump* done = sljit_emit_jump(m_C, SLJIT_JUMP);
                        sljit_set_label(slow, sljit_emit_label(m_C));
                        Generic(pc);
                        sljit_set_label(done, sljit_emit_label(m_C));
                        return true;
                    }

                    case OpCode::Jump:
                        JumpTo(SLJIT_JUMP, ins.C);
                        return true;
                    case OpCode::Loop:
                    {
                        // GC güvenli noktası: ayırma sayacı eşiği aştıysa topla
                        CQSHeap& heap = CQSHeap::Get();
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)heap.SinceGCAddress());
                        sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM0(), (sljit_sw)heap.ThresholdAddress());
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_LESS, SLJIT_R0, 0, SLJIT_R1, 0), ins.C);
                        CallHelper(SLJIT_FUNC_ADDR(JitSafepoint), d, pc);
                        JumpTo(SLJIT_JUMP, ins.C);
                        return true;
                    }
                    case OpCode::JumpIfFalse:
                    case OpCode::JumpIfTrue:
                        if (m_Fused.count(pc))
                            return true; // karşılaştırmayla birleştirildi
                        EmitCondJump(d - 1, op == OpCode::JumpIfTrue, ins.C);
                        return true;
                    case OpCode::JumpIfNullKeep:
                        LoadType(SLJIT_R0, d - 1);
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_NULL), ins.C);
                        return true;
                    case OpCode::JumpIfNotNullKeep:
                        LoadType(SLJIT_R0, d - 1);
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, T_NULL), ins.C);
                        return true;
                    case OpCode::JumpIfArg:
                        sljit_emit_op1(m_C, SLJIT_MOV_S32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1), (sljit_sw)offsetof(CQSFrame, ArgCount));
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_SIG_GREATER, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ins.B), ins.C);
                        return true;
                    case OpCode::IterNext:
                    {
                        CallHelper(SLJIT_FUNC_ADDR(JitIterNext), d, pc);
                        BranchToError(sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 2), pc);
                        m_Pending.emplace_back(sljit_emit_cmp(m_C, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0), ins.C);
                        return true;
                    }
                    case OpCode::Return:
                    case OpCode::ReturnNull:
                    {
                        // Önce upvalue'ları kapat: base[0] (this) yakalanmış olabilir, sonuç onu ezmeden önce kopyalanmalı
                        if (m_HasCaptures)
                        {
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
                            sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S2, 0);
                            sljit_emit_icall(m_C, SLJIT_CALL, SLJIT_ARGS2(W, P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(JitCloseFrame));
                        }
                        if (!m_Fn->IsInitializer)
                        {
                            if (op == OpCode::Return) CopySlot(0, d - 1);
                            else
                            {
                                StoreType(0, T_NULL);
                                sljit_emit_op1(m_C, SLJIT_MOV, SLJIT_MEM1(SLJIT_S2), kPay, SLJIT_IMM, 0);
                            }
                        }
                        // vm->m_StackTop = base + 1
                        sljit_emit_op2(m_C, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S2, 0, SLJIT_IMM, kSlot);
                        sljit_emit_op1(m_C, SLJIT_MOV_P, SLJIT_MEM1(SLJIT_S0), m_L.StackTop, SLJIT_R0, 0);
                        sljit_emit_return(m_C, SLJIT_MOV, SLJIT_IMM, 1);
                        return true;
                    }
                    case OpCode::Throw:
                    {
                        CallHelper(SLJIT_FUNC_ADDR(JitGeneric), d, pc);
                        BranchToError(sljit_emit_jump(m_C, SLJIT_JUMP), pc);
                        return true;
                    }
                    default:
                        Generic(pc);
                        return true;
                }
            }

            std::unordered_map<size_t, bool> m_Fused;
            std::unordered_set<int> m_OsrEntries;
        };

        Layout* s_Layout = nullptr;
    }

    // ════════════════════════════════════════
    //  Public API
    // ════════════════════════════════════════
    bool CQSJit::IsSupported()
    {
        static const bool s_Supported = !JitDisabledByEnv();
        return s_Supported;
    }

    const char* CQSJit::GetTargetName()
    {
        return sljit_get_platform_name();
    }

    bool CQSJit::Compile(CQSVM* vm, CQSFunctionObject* fn)
    {
        if (fn->JitCode || fn->JitFailed)
            return fn->JitCode != nullptr;
        if (!s_Layout)
        {
            s_Layout = new Layout(ComputeLayout(vm));
            if (!s_Layout->Valid)
                fprintf(stderr, "[CQS] JIT disabled: unexpected object layout.\n");
        }
        // StackTop ofseti VM örneğine göre aynıdır (aynı sınıf), ama yine de doğrula
        if (!s_Layout->Valid)
        {
            fn->JitFailed = true;
            return false;
        }
        Codegen cg(vm, fn, *s_Layout);
        if (!cg.Analyze())
        {
            fn->JitFailed = true;
            return false;
        }
        size_t size = 0;
        void* code = cg.Generate(size);
        if (!code)
        {
            fn->JitFailed = true;
            return false;
        }
        fn->JitCode = code;
        fn->JitSize = size;
        return true;
    }

    bool CQSJit::Run(CQSVM* vm, CQSFrame* frame, int startPc)
    {
        using JitFn = sljit_sw (SLJIT_FUNC*)(CQSVM*, CQSFrame*, sljit_sw);
        auto entry = reinterpret_cast<JitFn>(frame->Closure->Function->JitCode);
        return entry(vm, frame, (sljit_sw)startPc) != 0;
    }

    void CQSJit::FreeCode(void* code)
    {
        if (code)
            sljit_free_code(code, nullptr);
    }
}
