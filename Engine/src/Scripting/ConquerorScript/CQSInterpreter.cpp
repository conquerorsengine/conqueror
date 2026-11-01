#include "CQSVM.h"
#include "CQSJit.h"

#include <cmath>
#include <cstring>

namespace Conqueror::CQS
{
    namespace
    {
        inline int64_t WrapAdd(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
        inline int64_t WrapSub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
        inline int64_t WrapMul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
    }

    // Bytecode yorumlayıcısı. Her çağrı C yığınında yeni bir Execute çağrısıdır (JIT ile karışık
    // çağrılara izin verir). İstisnalar 'false' dönüşüyle yayılır; try bölgeleri burada yakalanır.
    bool CQSVM::Execute(CQSFrame& frame)
    {
        CQSFunctionObject* fn = frame.Closure->Function;
        const CQSInstr* code = fn->Code.data();
        const CQSInstr* ip = code;
        Value* base = frame.Base;
        Value* sp = base + 1 + fn->Arity;
        const Value* k = fn->Constants.data();

        // Yardımcı çağrılardan önce: hata satırı ve GC kökleri için durum senkronu
#define SYNC() do { frame.IP = ip - 1; m_StackTop = sp; } while (0)
#define THROW() goto exception_handler

        for (;;)
        {
            const CQSInstr ins = *ip++;
            switch ((OpCode)ins.Op)
            {
                case OpCode::Nop: break;

                case OpCode::PushNull:  *sp++ = Value(); break;
                case OpCode::PushTrue:  *sp++ = Value::MakeBool(true); break;
                case OpCode::PushFalse: *sp++ = Value::MakeBool(false); break;
                case OpCode::PushInt:   *sp++ = Value::MakeInt(ins.C); break;
                case OpCode::PushConst: *sp++ = k[ins.B]; break;

                case OpCode::Pop:  --sp; break;
                case OpCode::PopN: sp -= ins.B; break;
                case OpCode::Dup:  *sp = sp[-1]; ++sp; break;
                case OpCode::Dup2: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; break;
                case OpCode::Swap: { Value t = sp[-1]; sp[-1] = sp[-2]; sp[-2] = t; break; }
                case OpCode::Bury:
                {
                    Value v = sp[-1];
                    int n = ins.B;
                    std::memmove((void*)(sp - n), (const void*)(sp - n - 1), sizeof(Value) * (size_t)n);
                    sp[-n - 1] = v;
                    break;
                }

                case OpCode::GetLocal: *sp++ = base[ins.B]; break;
                case OpCode::SetLocal: base[ins.B] = sp[-1]; break;
                case OpCode::GetUpval: *sp++ = *frame.Closure->Upvalues[ins.B]->Location; break;
                case OpCode::SetUpval: *frame.Closure->Upvalues[ins.B]->Location = sp[-1]; break;
                case OpCode::CloseUpval:
                    CloseUpvalues(sp - 1);
                    --sp;
                    break;

                case OpCode::GetGlobal:
                {
                    size_t g = (size_t)ins.C;
                    if (!m_GlobalDefined[g])
                    {
                        SYNC();
                        RuntimeError("NameError: Undefined variable '%s'.", m_GlobalNames[g].c_str());
                        THROW();
                    }
                    *sp++ = m_GlobalValues[g];
                    break;
                }
                case OpCode::SetGlobal:
                {
                    size_t g = (size_t)ins.C;
                    if (!m_GlobalDefined[g])
                    {
                        SYNC();
                        RuntimeError("NameError: Undefined variable '%s' (declare it with 'var').", m_GlobalNames[g].c_str());
                        THROW();
                    }
                    m_GlobalValues[g] = sp[-1];
                    break;
                }
                case OpCode::DefineGlobal:
                {
                    size_t g = (size_t)ins.C;
                    m_GlobalValues[g] = *--sp;
                    m_GlobalDefined[g] = 1;
                    break;
                }
                case OpCode::GetDynamic:
                    SYNC();
                    if (!GetDynamic(sp - 1, sp[-1], fn->Names[ins.B], ins.C)) THROW();
                    break;

                case OpCode::GetProp:
                    SYNC();
                    if (!GetProperty(sp - 1, fn->Names[ins.B], &fn->Caches[(size_t)ins.C])) THROW();
                    break;
                case OpCode::SetProp:
                    SYNC();
                    if (!SetProperty(sp[-2], fn->Names[ins.B], sp[-1], &fn->Caches[(size_t)ins.C])) THROW();
                    sp[-2] = sp[-1];
                    --sp;
                    break;
                case OpCode::GetField:
                {
                    Value& o = sp[-1];
                    if (o.IsInstance() && ins.B < o.AsInstance()->Slots.size())
                        o = o.AsInstance()->Slots[ins.B];
                    else
                    {
                        SYNC();
                        RuntimeError(o.IsNull() ? "NullReferenceException: Cannot read a field of null." : "TypeError: Field access on a non-instance value.");
                        THROW();
                    }
                    break;
                }
                case OpCode::SetField:
                {
                    Value& o = sp[-2];
                    if (o.IsInstance() && ins.B < o.AsInstance()->Slots.size())
                        o.AsInstance()->Slots[ins.B] = sp[-1];
                    else
                    {
                        SYNC();
                        RuntimeError(o.IsNull() ? "NullReferenceException: Cannot set a field of null." : "TypeError: Field assignment on a non-instance value.");
                        THROW();
                    }
                    sp[-2] = sp[-1];
                    --sp;
                    break;
                }
                case OpCode::GetThisField:
                {
                    const Value& self = base[0];
                    if (self.IsInstance() && ins.B < self.AsInstance()->Slots.size())
                        *sp++ = self.AsInstance()->Slots[ins.B];
                    else
                    {
                        SYNC();
                        RuntimeError("TypeError: 'this' is not an instance of the expected class.");
                        THROW();
                    }
                    break;
                }
                case OpCode::SetThisField:
                {
                    const Value& self = base[0];
                    if (self.IsInstance() && ins.B < self.AsInstance()->Slots.size())
                        self.AsInstance()->Slots[ins.B] = sp[-1];
                    else
                    {
                        SYNC();
                        RuntimeError("TypeError: 'this' is not an instance of the expected class.");
                        THROW();
                    }
                    break;
                }
                case OpCode::GetIndex:
                {
                    Value& o = sp[-2];
                    const Value& i = sp[-1];
                    if (o.IsList() && i.IsInt())
                    {
                        auto& e = o.AsList()->Elements;
                        int64_t idx = i.IntVal;
                        if (idx < 0) idx += (int64_t)e.size();
                        if (idx >= 0 && idx < (int64_t)e.size())
                        {
                            o = e[(size_t)idx];
                            --sp;
                            break;
                        }
                    }
                    SYNC();
                    if (!CQSVM::GetIndex(sp - 2, sp[-1])) THROW();
                    --sp;
                    break;
                }
                case OpCode::SetIndex:
                {
                    Value& o = sp[-3];
                    const Value& i = sp[-2];
                    if (o.IsList() && i.IsInt())
                    {
                        auto& e = o.AsList()->Elements;
                        int64_t idx = i.IntVal;
                        if (idx < 0) idx += (int64_t)e.size();
                        if (idx >= 0 && idx < (int64_t)e.size())
                        {
                            e[(size_t)idx] = sp[-1];
                            sp[-3] = sp[-1];
                            sp -= 2;
                            break;
                        }
                    }
                    SYNC();
                    if (!CQSVM::SetIndex(sp[-3], sp[-2], sp[-1])) THROW();
                    sp[-3] = sp[-1];
                    sp -= 2;
                    break;
                }
                case OpCode::GetSuper:
                    SYNC();
                    if (!GetSuperMethod(sp - 1, fn->Names[ins.B])) THROW();
                    break;

#define ARITH_FAST(OP, INTEXPR, FLTEXPR)                                                          \
                case OpCode::OP:                                                                  \
                {                                                                                 \
                    Value* a = sp - 2;                                                            \
                    const Value* b = sp - 1;                                                      \
                    if (a->Type == ValueType::Int && b->Type == ValueType::Int)                   \
                        a->IntVal = INTEXPR;                                                      \
                    else if (a->Type == ValueType::Float && b->Type == ValueType::Float)          \
                        a->FloatVal = FLTEXPR;                                                    \
                    else if (a->IsNumber() && b->IsNumber())                                      \
                    {                                                                             \
                        double x = a->ToNumber(), y = b->ToNumber();                              \
                        (void)x; (void)y;                                                         \
                        *a = Value::MakeFloat(FLTEXPR_XY_##OP);                                   \
                    }                                                                             \
                    else                                                                          \
                    {                                                                             \
                        SYNC();                                                                   \
                        if (!ArithSlow(OpCode::OP, a, b)) THROW();                                \
                    }                                                                             \
                    --sp;                                                                         \
                    break;                                                                        \
                }
#define FLTEXPR_XY_Add x + y
#define FLTEXPR_XY_Sub x - y
#define FLTEXPR_XY_Mul x * y
                ARITH_FAST(Add, WrapAdd(a->IntVal, b->IntVal), a->FloatVal + b->FloatVal)
                ARITH_FAST(Sub, WrapSub(a->IntVal, b->IntVal), a->FloatVal - b->FloatVal)
                ARITH_FAST(Mul, WrapMul(a->IntVal, b->IntVal), a->FloatVal * b->FloatVal)
#undef ARITH_FAST
#undef FLTEXPR_XY_Add
#undef FLTEXPR_XY_Sub
#undef FLTEXPR_XY_Mul

                case OpCode::Div:
                {
                    Value* a = sp - 2;
                    const Value* b = sp - 1;
                    if (a->IsNumber() && b->IsNumber() && !(b->IsInt() && b->IntVal == 0 && a->IsInt()))
                        *a = Value::MakeFloat(a->ToNumber() / b->ToNumber());
                    else
                    {
                        SYNC();
                        if (!ArithSlow(OpCode::Div, a, b)) THROW();
                    }
                    --sp;
                    break;
                }
                case OpCode::Mod:
                case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor: case OpCode::Shl: case OpCode::Shr:
                {
                    Value* a = sp - 2;
                    const Value* b = sp - 1;
                    if (a->IsInt() && b->IsInt())
                    {
                        int64_t x = a->IntVal, y = b->IntVal;
                        switch ((OpCode)ins.Op)
                        {
                            case OpCode::Mod:
                                if (y == 0) goto arith_slow;
                                a->IntVal = (y == -1) ? 0 : x % y;
                                break;
                            case OpCode::BitAnd: a->IntVal = x & y; break;
                            case OpCode::BitOr: a->IntVal = x | y; break;
                            case OpCode::BitXor: a->IntVal = x ^ y; break;
                            case OpCode::Shl: a->IntVal = (int64_t)((uint64_t)x << (y & 63)); break;
                            case OpCode::Shr: a->IntVal = x >> (y & 63); break;
                            default: break;
                        }
                        --sp;
                        break;
                    }
                arith_slow:
                    SYNC();
                    if (!ArithSlow((OpCode)ins.Op, a, b)) THROW();
                    --sp;
                    break;
                }

                case OpCode::Neg:
                {
                    Value& v = sp[-1];
                    if (v.IsInt()) v.IntVal = WrapSub(0, v.IntVal);
                    else if (v.IsFloat()) v.FloatVal = -v.FloatVal;
                    else
                    {
                        SYNC();
                        Value minusOne = Value::MakeInt(-1);
                        if (!ArithSlow(OpCode::Mul, &v, &minusOne)) THROW();
                    }
                    break;
                }
                case OpCode::Not: sp[-1] = Value::MakeBool(!sp[-1].IsTruthy()); break;
                case OpCode::BitNot:
                    if (!sp[-1].IsInt())
                    {
                        SYNC();
                        RuntimeError("TypeError: '~' requires an integer.");
                        THROW();
                    }
                    sp[-1].IntVal = ~sp[-1].IntVal;
                    break;

                case OpCode::Eq: sp[-2] = Value::MakeBool(sp[-2].Equals(sp[-1])); --sp; break;
                case OpCode::Ne: sp[-2] = Value::MakeBool(!sp[-2].Equals(sp[-1])); --sp; break;
                case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge:
                {
                    const Value& a = sp[-2];
                    const Value& b = sp[-1];
                    bool r;
                    if (a.Type == ValueType::Int && b.Type == ValueType::Int)
                    {
                        switch ((OpCode)ins.Op)
                        {
                            case OpCode::Lt: r = a.IntVal < b.IntVal; break;
                            case OpCode::Le: r = a.IntVal <= b.IntVal; break;
                            case OpCode::Gt: r = a.IntVal > b.IntVal; break;
                            default: r = a.IntVal >= b.IntVal; break;
                        }
                    }
                    else if (a.IsNumber() && b.IsNumber())
                    {
                        double x = a.ToNumber(), y = b.ToNumber();
                        switch ((OpCode)ins.Op)
                        {
                            case OpCode::Lt: r = x < y; break;
                            case OpCode::Le: r = x <= y; break;
                            case OpCode::Gt: r = x > y; break;
                            default: r = x >= y; break;
                        }
                    }
                    else
                    {
                        SYNC();
                        if (!CompareSlow((OpCode)ins.Op, a, b, r)) THROW();
                    }
                    sp[-2] = Value::MakeBool(r);
                    --sp;
                    break;
                }
                case OpCode::ToBool: sp[-1] = Value::MakeBool(sp[-1].IsTruthy()); break;
                case OpCode::MakeRange:
                {
                    if (!sp[-2].IsNumber() || !sp[-1].IsNumber())
                    {
                        SYNC();
                        RuntimeError("TypeError: Range bounds must be numbers.");
                        THROW();
                    }
                    sp[-2] = Value::MakeObject(new CQSRangeObject(sp[-2].ToInt(), sp[-1].ToInt()));
                    --sp;
                    break;
                }
                case OpCode::IsType:
                    SYNC();
                    sp[-1] = Value::MakeBool(IsInstanceOfType(sp[-1], fn->Names[ins.B]));
                    break;
                case OpCode::CastType:
                    SYNC();
                    if (!CastToType(sp - 1, fn->Names[ins.B])) THROW();
                    break;

                case OpCode::Jump: ip = code + ins.C; break;
                case OpCode::JumpIfFalse:
                {
                    const Value& c = *--sp;
                    bool falsy = c.Type == ValueType::Bool ? !c.BoolVal : !c.IsTruthy();
                    if (falsy) ip = code + ins.C;
                    break;
                }
                case OpCode::JumpIfTrue:
                {
                    const Value& c = *--sp;
                    bool truthy = c.Type == ValueType::Bool ? c.BoolVal : c.IsTruthy();
                    if (truthy) ip = code + ins.C;
                    break;
                }
                case OpCode::JumpIfNullKeep:
                    if (sp[-1].IsNull()) ip = code + ins.C;
                    break;
                case OpCode::JumpIfNotNullKeep:
                    if (!sp[-1].IsNull()) ip = code + ins.C;
                    else --sp;
                    break;
                case OpCode::Loop:
                    ip = code + ins.C;
                    if (CQSHeap::Get().ShouldCollect())
                    {
                        SYNC();
                        CollectGarbage();
                    }
                    // Sıcak döngü: fonksiyonu derle ve döngü başlığından yerel koda geç (OSR)
                    if (m_JitEnabled && !fn->JitFailed && ++fn->BackEdges >= m_OsrThreshold && CQSJit::IsSupported())
                    {
                        if (fn->JitCode || CQSJit::Compile(this, fn))
                        {
                            if (!fn->JitCode) break;
                            ++m_Stats.JitCompiledFunctions;
                            frame.IP = ip;
                            m_StackTop = sp;
                            return CQSJit::Run(this, &frame, ins.C);
                        }
                    }
                    break;
                case OpCode::JumpIfArg:
                    if (frame.ArgCount > (int)ins.B) ip = code + ins.C;
                    break;

                case OpCode::Call:
                {
                    Value* callBase = sp - ins.A - 1;
                    SYNC();
                    if (CQSHeap::Get().ShouldCollect())
                        CollectGarbage();
                    if (!CallValue(callBase, ins.A)) THROW();
                    sp = callBase + 1;
                    break;
                }
                case OpCode::Invoke:
                {
                    Value* callBase = sp - ins.A - 1;
                    SYNC();
                    if (CQSHeap::Get().ShouldCollect())
                        CollectGarbage();
                    if (!InvokeMethod(callBase, ins.A, fn->Names[ins.B], &fn->Caches[(size_t)ins.C])) THROW();
                    sp = callBase + 1;
                    break;
                }
                case OpCode::SuperInvoke:
                {
                    Value* callBase = sp - ins.A - 1;
                    SYNC();
                    if (!SuperInvoke(callBase, ins.A, fn->Names[ins.B])) THROW();
                    sp = callBase + 1;
                    break;
                }
                case OpCode::Return:
                {
                    Value result = sp[-1];
                    CloseUpvalues(base);
                    if (!fn->IsInitializer)
                        base[0] = result;
                    m_StackTop = base + 1;
                    return true;
                }
                case OpCode::ReturnNull:
                    CloseUpvalues(base);
                    if (!fn->IsInitializer)
                        base[0] = Value();
                    m_StackTop = base + 1;
                    return true;

                case OpCode::Closure:
                {
                    auto* proto = k[ins.B].AsFunction();
                    SYNC();
                    auto* closure = new CQSClosureObject(proto);
                    for (size_t i = 0; i < proto->Upvalues.size(); ++i)
                    {
                        const CQSUpvalueDesc& d = proto->Upvalues[i];
                        closure->Upvalues[i] = d.IsLocal ? CaptureUpvalue(base + d.Index) : frame.Closure->Upvalues[d.Index];
                    }
                    *sp++ = Value::MakeObject(closure);
                    break;
                }
                case OpCode::Class:
                    SYNC();
                    CreateClass(sp, k[ins.B].AsClass());
                    ++sp;
                    break;
                case OpCode::Inherit:
                    SYNC();
                    if (!InheritClass(sp - 2, sp[-1], ins.A != 0)) THROW();
                    --sp;
                    break;
                case OpCode::Method:
                    DefineMethod(sp - 2, fn->Names[ins.B], sp[-1], ins.A);
                    --sp;
                    break;
                case OpCode::StaticField:
                    sp[-2].AsClass()->Statics[fn->Names[ins.B]] = sp[-1];
                    --sp;
                    break;

                case OpCode::NewList:
                {
                    SYNC();
                    auto* list = new CQSListObject();
                    int n = ins.B;
                    list->Elements.assign(sp - n, sp);
                    sp -= n;
                    *sp++ = Value::MakeObject(list);
                    break;
                }
                case OpCode::NewMap:
                {
                    SYNC();
                    auto* map = new CQSMapObject();
                    int n = ins.B;
                    Value* p = sp - 2 * n;
                    for (int i = 0; i < n; ++i)
                    {
                        const Value& key = p[2 * i];
                        map->Entries[key.IsString() ? key.AsString()->Value : key.ToString()] = p[2 * i + 1];
                    }
                    sp = p;
                    *sp++ = Value::MakeObject(map);
                    break;
                }
                case OpCode::IterPrep:
                    SYNC();
                    if (!IterPrep(sp - 1, ins.A)) THROW();
                    break;
                case OpCode::IterNext:
                {
                    int r = IterNext(base + ins.B, sp, ins.A);
                    if (r > 0) sp += ins.A;
                    else if (r == 0) ip = code + ins.C;
                    else { SYNC(); THROW(); }
                    break;
                }
                case OpCode::Throw:
                    SYNC();
                    SetException(*--sp);
                    THROW();
                case OpCode::ToStr:
                {
                    SYNC();
                    std::string s;
                    bool ok = ins.B == 0xFFFF ? ValueToString(sp[-1], s) : FormatValue(sp[-1], fn->Names[ins.B], s);
                    if (!ok) THROW();
                    sp[-1] = MakeStringValue(std::move(s));
                    break;
                }
                case OpCode::Concat:
                {
                    SYNC();
                    int n = ins.B;
                    std::string out;
                    for (Value* v = sp - n; v < sp; ++v)
                    {
                        if (v->IsString()) out += v->AsString()->Value;
                        else
                        {
                            std::string part;
                            if (!ValueToString(*v, part)) THROW();
                            out += part;
                        }
                    }
                    sp -= n;
                    *sp++ = MakeStringValue(std::move(out));
                    break;
                }
                case OpCode::NewArray:
                    SYNC();
                    if (!NewArray(sp - 1, fn->Names[ins.B])) THROW();
                    break;

                default:
                    SYNC();
                    RuntimeError("InternalError: Invalid opcode %d.", (int)ins.Op);
                    THROW();
            }
            continue;

        exception_handler:
        {
            // İstisna beklemede: bu fonksiyonda kapsayan try bölgesi var mı?
            size_t pc = (size_t)((ip - 1) - code);
            const CQSTryHandler* handler = nullptr;
            for (const CQSTryHandler& h : fn->Handlers)
            {
                if (pc >= h.Start && pc < h.End)
                {
                    handler = &h;
                    break;
                }
            }
            if (!handler)
            {
                frame.IP = ip - 1;
                CloseUpvalues(base);
                return false;
            }
            Value* newTop = base + handler->StackDepth;
            CloseUpvalues(newTop);
            sp = newTop;
            *sp++ = TakeException();
            ip = code + handler->Handler;
            m_StackTop = sp;
        }
        }
#undef SYNC
#undef THROW
    }
}
