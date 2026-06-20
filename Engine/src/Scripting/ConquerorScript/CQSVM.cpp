#include "CQSVM.h"
#include <iostream>
#include <stdarg.h>
#include <cmath>

namespace Conqueror::CQS
{
    CQSVM::CQSVM()
    {
        m_StackTop = m_Stack;
        m_FrameCount = 0;
    }

    CQSVM::~CQSVM()
    {
        // Free objects
        for (auto* obj : m_Objects) delete obj;
    }

    VMResult CQSVM::Run(std::shared_ptr<CQSChunk> chunk)
    {
        // Root script as a function
        auto* script = new CQSFunctionObject("<script>", 0);
        script->ChunkData = chunk;
        m_Objects.push_back(script);

        Value* previousStackTop = m_StackTop;
        int previousFrameCount = m_FrameCount;

        Push(Value::MakeObject(script));
        
        if (!Call(script, 0))
        {
            m_StackTop = previousStackTop;
            m_FrameCount = previousFrameCount;
            return VMResult::RuntimeError;
        }

        VMResult result = Execute();
        if (result != VMResult::Success)
        {
            m_StackTop = previousStackTop;
            m_FrameCount = previousFrameCount;
        }
        return result;
    }

    void CQSVM::Push(Value value)
    {
        *m_StackTop = value;
        m_StackTop++;
    }

    Value CQSVM::Pop()
    {
        m_StackTop--;
        return *m_StackTop;
    }

    Value CQSVM::Peek(int distance)
    {
        return *(m_StackTop - 1 - distance);
    }

    bool CQSVM::Call(CQSFunctionObject* function, int argCount)
    {
        if (m_FrameCount == FRAMES_MAX)
        {
            RuntimeError("Stack overflow.");
            return false;
        }

        CallFrame* frame = &m_Frames[m_FrameCount++];
        frame->Function = function;
        frame->IP = function->ChunkData->Code.data();
        frame->Slots = m_StackTop - argCount - 1;
        return true;
    }

    void CQSVM::DefineNative(const std::string& name, NativeFn function, int arity)
    {
        auto* native = new CQSNativeFunctionObject(name, arity, function);
        m_Objects.push_back(native);
        DefineGlobal(name, Value::MakeObject(native));
    }

    void CQSVM::DefineGlobal(const std::string& name, Value value)
    {
        m_Globals[name] = value;
    }

    bool CQSVM::GetGlobal(const std::string& name, Value& outValue)
    {
        auto it = m_Globals.find(name);
        if (it != m_Globals.end())
        {
            outValue = it->second;
            return true;
        }
        return false;
    }

    bool CQSVM::CallValue(Value callee, int argCount)
    {
        if (callee.IsObject())
        {
            switch (callee.AsObject()->Type)
            {
                case ObjectType::NativeFunction:
                {
                    auto* native = callee.AsNativeFunction();
                    if (argCount != native->Arity)
                    {
                        RuntimeError("Expected %d arguments but got %d.", native->Arity, argCount);
                        return false;
                    }
                    Value result = native->Function(argCount, m_StackTop - argCount);
                    m_StackTop -= argCount + 1;
                    Push(result);
                    return true;
                }
                case ObjectType::Function:
                    return Call(callee.AsFunction(), argCount);
                default: break;
            }
        }
        RuntimeError("Can only call functions and classes.");
        return false;
    }

    bool CQSVM::Invoke(const std::string& name, int argCount)
    {
        Value method;
        if (!GetGlobal(name, method)) 
        {
            m_StackTop -= argCount;
            return true; 
        }

        // Sadece çağrılabilir bir nesneyse (fonksiyon/native) çağır
        if (!method.IsObject() || 
            (method.AsObject()->Type != ObjectType::Function && 
             method.AsObject()->Type != ObjectType::NativeFunction))
        {
            m_StackTop -= argCount; // Argümanları temizle
            return true;
        }

        // Arity mismatch düzeltmesi: fazla argümanları kırp, eksikleri null ile doldur
        int expectedArity = 0;
        if (method.AsObject()->Type == ObjectType::Function)
            expectedArity = method.AsFunction()->Arity;
        else
            expectedArity = method.AsNativeFunction()->Arity;

        while (argCount > expectedArity) { Pop(); argCount--; }
        while (argCount < expectedArity) { Push(Value::MakeNull()); argCount++; }

        // CallValue, stack'te argümanlardan önce fonksiyon objesinin olmasını bekler.
        // Argümanları bir üst slota kaydırıp fonksiyon objesini yerleştiriyoruz.
        Value* targetSlot = m_StackTop - argCount;
        for (int i = 0; i < argCount; i++)
        {
            *(m_StackTop - i) = *(m_StackTop - i - 1);
        }
        *targetSlot = method;
        m_StackTop++;

        Value* previousStackTop = targetSlot; 
        int previousFrameCount = m_FrameCount;

        if (!CallValue(method, argCount)) 
        {
            m_StackTop = previousStackTop;
            m_FrameCount = previousFrameCount;
            return false;
        }
        
        VMResult result = Execute();
        if (result != VMResult::Success)
        {
            m_StackTop = previousStackTop;
            m_FrameCount = previousFrameCount;
            return false;
        }

        return true;
    }


    VMResult CQSVM::Execute()
    {
        CallFrame* frame = &m_Frames[m_FrameCount - 1];

        #define READ_BYTE() (*frame->IP++)
        #define READ_SHORT() (frame->IP += 2, (uint16_t)((frame->IP[-2] << 8) | frame->IP[-1]))
        #define READ_CONSTANT() (frame->Function->ChunkData->Constants[READ_BYTE()])
        #define READ_STRING() (frame->Function->ChunkData->Constants[READ_SHORT()].AsString()->Value)

        
        #define BINARY_OP(op) \
            do { \
                if (!Peek(0).IsNumber() || !Peek(1).IsNumber()) { \
                    RuntimeError("Operands must be numbers."); \
                    return VMResult::RuntimeError; \
                } \
                Value bVal = Pop(); \
                Value aVal = Pop(); \
                if (aVal.IsFloat() || bVal.IsFloat()) { \
                    Push(Value::MakeFloat(aVal.ToNumber() op bVal.ToNumber())); \
                } else { \
                    Push(Value::MakeInt(aVal.AsInt() op bVal.AsInt())); \
                } \
            } while (false)

        for (;;)
        {
            OpCode instruction = static_cast<OpCode>(READ_BYTE());
            switch (instruction)
            {
                case OpCode::Constant: 
                {
                    uint8_t index = READ_BYTE();
                    Value value = frame->Function->ChunkData->Constants[index];
                    Push(value); 
                    break;
                }
                case OpCode::ConstantLong:
                {
                    uint16_t index = READ_SHORT();
                    Value value = frame->Function->ChunkData->Constants[index];
                    Push(value);
                    break;
                }
                case OpCode::Null:     Push(Value::MakeNull()); break;
                case OpCode::True:     Push(Value::MakeBool(true)); break;
                case OpCode::False:    Push(Value::MakeBool(false)); break;
                case OpCode::Zero:     Push(Value::MakeInt(0)); break;
                case OpCode::One:      Push(Value::MakeInt(1)); break;

                case OpCode::Pop:      Pop(); break;

                case OpCode::Add:
                {
                    Value b = Pop();
                    Value a = Pop();

                    if (a.IsString() || b.IsString())
                    {
                        std::string resStr = a.ToString() + b.ToString();
                        CQSStringObject* obj = new CQSStringObject(resStr);
                        m_Objects.push_back(obj);
                        Push(Value::MakeObject(obj));
                    }
                    else if (a.IsNumber() && b.IsNumber())
                    {
                        if (a.IsFloat() || b.IsFloat())
                            Push(Value::MakeFloat(a.ToNumber() + b.ToNumber()));
                        else
                            Push(Value::MakeInt(a.AsInt() + b.AsInt()));
                    }
                    else
                    {
                        RuntimeError("Operands must be numbers or strings.");
                        return VMResult::RuntimeError;
                    }
                    break;
                }
                case OpCode::Subtract: BINARY_OP(-); break;
                case OpCode::Multiply: BINARY_OP(*); break;
                case OpCode::Divide:   
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber()) {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    Value bVal = Pop();
                    Value aVal = Pop();
                    if (bVal.ToNumber() == 0.0) {
                        RuntimeError("Division by zero.");
                        return VMResult::RuntimeError;
                    }
                    if (aVal.IsFloat() || bVal.IsFloat()) {
                        Push(Value::MakeFloat(aVal.ToNumber() / bVal.ToNumber()));
                    } else {
                        Push(Value::MakeFloat((double)aVal.AsInt() / (double)bVal.AsInt())); // Division usually returns float.
                    }
                    break;
                }
                case OpCode::Modulo:
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber())
                    {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    Value bVal = Pop();
                    Value aVal = Pop();
                    if (aVal.IsFloat() || bVal.IsFloat()) {
                         Push(Value::MakeFloat(fmod(aVal.ToNumber(), bVal.ToNumber())));
                    } else {
                         if (bVal.AsInt() == 0) {
                             RuntimeError("Modulo by zero.");
                             return VMResult::RuntimeError;
                         }
                         Push(Value::MakeInt(aVal.AsInt() % bVal.AsInt()));
                    }
                    break;
                }

                case OpCode::Negate:
                {
                    if (!Peek(0).IsNumber())
                    {
                        RuntimeError("Operand must be a number.");
                        return VMResult::RuntimeError;
                    }
                    Value val = Pop();
                    if (val.IsFloat())
                        Push(Value::MakeFloat(-val.AsFloat()));
                    else
                        Push(Value::MakeInt(-val.AsInt()));
                    break;
                }

                case OpCode::Not:
                {
                    Push(Value::MakeBool(!Pop().IsTruthy()));
                    break;
                }

                case OpCode::Equal:    
                {
                    Value b = Pop();
                    Value a = Pop();
                    Push(Value::MakeBool(a.Equals(b)));
                    break;
                }

                case OpCode::NotEqual:
                {
                    Value b = Pop();
                    Value a = Pop();
                    Push(Value::MakeBool(!a.Equals(b)));
                    break;
                }

                case OpCode::Less:
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber())
                    {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    double b = Pop().ToNumber();
                    double a = Pop().ToNumber();
                    Push(Value::MakeBool(a < b));
                    break;
                }

                case OpCode::LessEqual:
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber())
                    {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    double b = Pop().ToNumber();
                    double a = Pop().ToNumber();
                    Push(Value::MakeBool(a <= b));
                    break;
                }

                case OpCode::Greater:
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber())
                    {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    double b = Pop().ToNumber();
                    double a = Pop().ToNumber();
                    Push(Value::MakeBool(a > b));
                    break;
                }

                case OpCode::GreaterEqual:
                {
                    if (!Peek(0).IsNumber() || !Peek(1).IsNumber())
                    {
                        RuntimeError("Operands must be numbers.");
                        return VMResult::RuntimeError;
                    }
                    double b = Pop().ToNumber();
                    double a = Pop().ToNumber();
                    Push(Value::MakeBool(a >= b));
                    break;
                }

                case OpCode::And:
                {
                    Value b = Pop();
                    Value a = Pop();
                    Push(Value::MakeBool(a.IsTruthy() && b.IsTruthy()));
                    break;
                }

                case OpCode::Or:
                {
                    Value b = Pop();
                    Value a = Pop();
                    Push(Value::MakeBool(a.IsTruthy() || b.IsTruthy()));
                    break;
                }

                case OpCode::GetLocal:
                {
                    uint8_t slot = READ_BYTE();
                    Push(frame->Slots[slot]);
                    break;
                }

                case OpCode::SetLocal:
                {
                    uint8_t slot = READ_BYTE();
                    frame->Slots[slot] = Peek(0);
                    break;
                }

                case OpCode::Jump:
                {
                    uint16_t offset = READ_SHORT();
                    frame->IP += offset;
                    break;
                }

                case OpCode::JumpIfFalse:
                {
                    uint16_t offset = READ_SHORT();
                    if (!Peek(0).IsTruthy()) frame->IP += offset;
                    break;
                }

                case OpCode::JumpIfTrue:
                {
                    uint16_t offset = READ_SHORT();
                    if (Peek(0).IsTruthy()) frame->IP += offset;
                    break;
                }

                case OpCode::Loop:
                {
                    uint16_t offset = READ_SHORT();
                    frame->IP -= offset;
                    break;
                }

                case OpCode::GetGlobal:
                {
                    std::string name = READ_STRING();
                    Value value;
                    if (!GetGlobal(name, value))
                    {
                        // Fallback: Check if it's a method of the current script class
                        if (frame && frame->Function) {
                            std::string currentFunc = frame->Function->Name;
                            size_t dotPos = currentFunc.find('.');
                            if (dotPos != std::string::npos) {
                                std::string className = currentFunc.substr(0, dotPos);
                                if (GetGlobal(className + "." + name, value)) {
                                    Push(value);
                                    break;
                                }
                            }
                        }

                        RuntimeError("Undefined variable '%s'.", name.c_str());
                        return VMResult::RuntimeError;
                    }
                    Push(value);
                    break;
                }

                case OpCode::SetGlobal:
                {
                    std::string name = READ_STRING();
                    if (m_Globals.find(name) == m_Globals.end())
                    {
                        RuntimeError("Undefined variable '%s'.", name.c_str());
                        return VMResult::RuntimeError;
                    }
                    m_Globals[name] = Peek(0);
                    break;
                }

                case OpCode::DefineGlobal:
                {
                    std::string name = READ_STRING();
                    DefineGlobal(name, Pop());
                    break;
                }

                case OpCode::GetProperty:
                {
                    std::string name = READ_STRING();
                    Value object = Pop();
                    if (object.IsMap())
                    {
                        auto* mapObj = object.AsMap();
                        auto it = mapObj->Entries.find(name);
                        if (it != mapObj->Entries.end()) {
                            Push(it->second);
                        } else {
                            RuntimeError("Undefined property '%s'.", name.c_str());
                            return VMResult::RuntimeError;
                        }
                    }
                    else if (object.IsInstance())
                    {
                        auto* instObj = object.AsInstance();
                        auto it = instObj->Fields.find(name);
                        if (it != instObj->Fields.end()) {
                            Push(it->second);
                        } else {
                            RuntimeError("Undefined property '%s'.", name.c_str());
                            return VMResult::RuntimeError;
                        }
                    }
                    else
                    {
                        RuntimeError("Only instances and maps have properties.");
                        return VMResult::RuntimeError;
                    }
                    break;
                }

                case OpCode::SetProperty:
                {
                    std::string name = READ_STRING();
                    Value value = Pop();
                    Value object = Pop();
                    if (object.IsMap())
                    {
                        object.AsMap()->Entries[name] = value;
                        Push(value);
                    }
                    else if (object.IsInstance())
                    {
                        object.AsInstance()->Fields[name] = value;
                        Push(value);
                    }
                    else
                    {
                        RuntimeError("Only instances and maps have properties.");
                        return VMResult::RuntimeError;
                    }
                    break;
                }

                case OpCode::Call:
                {
                    int argCount = READ_BYTE();
                    if (!CallValue(Peek(argCount), argCount))
                    {
                        return VMResult::RuntimeError;
                    }
                    frame = &m_Frames[m_FrameCount - 1];
                    break;
                }

                case OpCode::Return:
                {
                    Value result = Pop();
                    m_FrameCount--;
                    if (m_FrameCount == 0)
                    {
                        // Clean up the entire frame including arguments
                        m_StackTop = frame->Slots; 
                        return VMResult::Success;
                    }
                    m_StackTop = frame->Slots;
                    Push(result);
                    frame = &m_Frames[m_FrameCount - 1];
                    break;
                }

                case OpCode::Increment:
                {
                    if (Peek(0).IsInt())
                        Push(Value::MakeInt(Pop().AsInt() + 1));
                    else if (Peek(0).IsFloat())
                        Push(Value::MakeFloat(Pop().AsFloat() + 1.0));
                    else { RuntimeError("Operand must be a number."); return VMResult::RuntimeError; }
                    break;
                }

                case OpCode::Decrement:
                {
                    if (Peek(0).IsInt())
                        Push(Value::MakeInt(Pop().AsInt() - 1));
                    else if (Peek(0).IsFloat())
                        Push(Value::MakeFloat(Pop().AsFloat() - 1.0));
                    else { RuntimeError("Operand must be a number."); return VMResult::RuntimeError; }
                    break;
                }

                case OpCode::BitAnd:
                {
                    int64_t b = (int64_t)Pop().ToNumber();
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(a & b));
                    break;
                }
                case OpCode::BitOr:
                {
                    int64_t b = (int64_t)Pop().ToNumber();
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(a | b));
                    break;
                }
                case OpCode::BitXor:
                {
                    int64_t b = (int64_t)Pop().ToNumber();
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(a ^ b));
                    break;
                }
                case OpCode::BitNot:
                {
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(~a));
                    break;
                }
                case OpCode::ShiftLeft:
                {
                    int64_t b = (int64_t)Pop().ToNumber();
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(a << b));
                    break;
                }
                case OpCode::ShiftRight:
                {
                    int64_t b = (int64_t)Pop().ToNumber();
                    int64_t a = (int64_t)Pop().ToNumber();
                    Push(Value::MakeInt(a >> b));
                    break;
                }

                case OpCode::CastToInt:
                {
                    Value val = Pop();
                    if (val.IsInt()) Push(val);
                    else if (val.IsFloat()) Push(Value::MakeInt(static_cast<int64_t>(val.AsFloat())));
                    else if (val.IsBool()) Push(Value::MakeInt(val.AsBool() ? 1 : 0));
                    else if (val.IsString()) {
                        try { Push(Value::MakeInt(std::stoll(val.AsString()->Value))); }
                        catch (...) { Push(Value::MakeInt(0)); }
                    }
                    else Push(Value::MakeInt(0));
                    break;
                }
                case OpCode::CastToFloat:
                {
                    Value val = Pop();
                    if (val.IsFloat()) Push(val);
                    else if (val.IsInt()) Push(Value::MakeFloat(static_cast<double>(val.AsInt())));
                    else if (val.IsBool()) Push(Value::MakeFloat(val.AsBool() ? 1.0 : 0.0));
                    else if (val.IsString()) {
                        try { Push(Value::MakeFloat(std::stod(val.AsString()->Value))); }
                        catch (...) { Push(Value::MakeFloat(0.0)); }
                    }
                    else Push(Value::MakeFloat(0.0));
                    break;
                }
                case OpCode::CastToString:
                {
                    Value val = Pop();
                    if (val.IsString()) Push(val);
                    else {
                        auto* obj = new CQSStringObject(val.ToString());
                        m_Objects.push_back(obj);
                        Push(Value::MakeObject(obj));
                    }
                    break;
                }

                case OpCode::TypeCheck:
                {
                    uint8_t typeTag = READ_BYTE();
                    Value val = Peek(0);
                    Push(Value::MakeBool(static_cast<uint8_t>(val.Type) == typeTag));
                    break;
                }

                case OpCode::PopN:
                {
                    uint8_t count = READ_BYTE();
                    for (int i = 0; i < count; i++) Pop();
                    break;
                }
                case OpCode::Dup:
                {
                    Push(Peek(0));
                    break;
                }
                case OpCode::Swap:
                {
                    Value a = Pop();
                    Value b = Pop();
                    Push(a);
                    Push(b);
                    break;
                }

                case OpCode::NewList:
                {
                    uint8_t count = READ_BYTE();
                    auto* list = new CQSListObject();
                    m_Objects.push_back(list);
                    // Elements are on stack in order, pop them
                    list->Elements.resize(count);
                    for (int i = count - 1; i >= 0; i--)
                        list->Elements[i] = Pop();
                    Push(Value::MakeObject(list));
                    break;
                }
                case OpCode::GetIndex:
                {
                    Value index = Pop();
                    Value object = Pop();
                    if (object.IsList())
                    {
                        int64_t idx = static_cast<int64_t>(index.ToNumber());
                        auto* list = object.AsList();
                        if (idx < 0 || idx >= static_cast<int64_t>(list->Elements.size()))
                            Push(Value::MakeNull());
                        else
                            Push(list->Elements[idx]);
                    }
                    else if (object.IsString())
                    {
                        int64_t idx = static_cast<int64_t>(index.ToNumber());
                        auto* str = object.AsString();
                        if (idx < 0 || idx >= static_cast<int64_t>(str->Value.size()))
                            Push(Value::MakeNull());
                        else {
                            auto* ch = new CQSStringObject(std::string(1, str->Value[idx]));
                            m_Objects.push_back(ch);
                            Push(Value::MakeObject(ch));
                        }
                    }
                    else
                    {
                        Push(Value::MakeNull());
                    }
                    break;
                }
                case OpCode::SetIndex:
                {
                    Value value = Pop();
                    Value index = Pop();
                    Value object = Pop();
                    if (object.IsList())
                    {
                        int64_t idx = static_cast<int64_t>(index.ToNumber());
                        auto* list = object.AsList();
                        if (idx >= 0 && idx < static_cast<int64_t>(list->Elements.size()))
                            list->Elements[idx] = value;
                    }
                    Push(value);
                    break;
                }

                case OpCode::NewMap:
                {
                    uint8_t count = READ_BYTE();
                    auto* map = new CQSMapObject();
                    m_Objects.push_back(map);
                    // Stack has key, value pairs
                    std::vector<std::pair<std::string, Value>> pairs(count);
                    for (int i = count - 1; i >= 0; i--)
                    {
                        Value val = Pop();
                        Value key = Pop();
                        pairs[i] = {key.ToString(), val};
                    }
                    for (auto& [k, v] : pairs)
                        map->Entries[k] = v;
                    Push(Value::MakeObject(map));
                    break;
                }
                case OpCode::GetKey:
                {
                    Value key = Pop();
                    Value object = Pop();
                    if (object.IsMap())
                    {
                        auto* map = object.AsMap();
                        auto it = map->Entries.find(key.ToString());
                        Push(it != map->Entries.end() ? it->second : Value::MakeNull());
                    }
                    else Push(Value::MakeNull());
                    break;
                }
                case OpCode::SetKey:
                {
                    Value value = Pop();
                    Value key = Pop();
                    Value object = Pop();
                    if (object.IsMap())
                        object.AsMap()->Entries[key.ToString()] = value;
                    Push(value);
                    break;
                }

                case OpCode::NewInstance:
                {
                    std::string className = READ_STRING();
                    auto* inst = new CQSInstanceObject(className);
                    m_Objects.push_back(inst);
                    Push(Value::MakeObject(inst));
                    break;
                }

                case OpCode::Invoke:
                {
                    std::string name = READ_STRING();
                    uint8_t argCount = READ_BYTE();
                    if (!Invoke(name, argCount))
                        return VMResult::RuntimeError;
                    frame = &m_Frames[m_FrameCount - 1];
                    break;
                }

                case OpCode::Closure:
                {
                    uint8_t funcIndex = READ_BYTE();
                    Value funcVal = frame->Function->ChunkData->Constants[funcIndex];
                    // For now, just push the function — full upvalue support would wrap it
                    Push(funcVal);
                    break;
                }
                case OpCode::GetUpvalue:
                {
                    // Stub: treat as null for now
                    READ_BYTE();
                    Push(Value::MakeNull());
                    break;
                }
                case OpCode::SetUpvalue:
                {
                    READ_BYTE();
                    break;
                }
                case OpCode::CloseUpvalue:
                {
                    Pop();
                    break;
                }

                case OpCode::Print:
                {
                    Value val = Pop();
                    std::cout << "[Script] " << val.ToString() << std::endl;
                    break;
                }

                case OpCode::New:
                {
                    std::string className = READ_STRING();
                    uint8_t argCount = READ_BYTE();
                    auto* inst = new CQSInstanceObject(className);
                    m_Objects.push_back(inst);
                    // Pop arguments (constructor args - not used yet)
                    for (int i = 0; i < argCount; i++) Pop();
                    Push(Value::MakeObject(inst));
                    break;
                }

                case OpCode::This:
                {
                    if (m_CurrentInstance)
                        Push(Value::MakeObject(m_CurrentInstance));
                    else
                        Push(Value::MakeNull());
                    break;
                }

                case OpCode::Halt: 
                {
                    m_FrameCount = 0;
                    m_StackTop = m_Stack;
                    return VMResult::Success;
                }
                default: break;
            }
        }

        #undef READ_BYTE
        #undef READ_SHORT
        #undef READ_CONSTANT
        #undef BINARY_OP
    }

    void CQSVM::RuntimeError(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fputs("\n", stderr);

        for (int i = m_FrameCount - 1; i >= 0; i--)
        {
            CallFrame* frame = &m_Frames[i];
            CQSFunctionObject* function = frame->Function;
            size_t offset = frame->IP - function->ChunkData->Code.data() - 1;
            fprintf(stderr, "[line %d] in %s\n", function->ChunkData->Lines[offset], function->Name.empty() ? "script" : function->Name.c_str());
        }
    }

    void CQSVM::CollectGarbage()
    {
        // Mark phase: mark all reachable objects
        // Mark globals
        for (auto& [name, val] : m_Globals)
        {
            if (val.IsObject() && val.AsObject())
                val.AsObject()->IsMarked = true;
        }
        // Mark stack
        for (Value* slot = m_Stack; slot < m_StackTop; slot++)
        {
            if (slot->IsObject() && slot->AsObject())
                slot->AsObject()->IsMarked = true;
        }
        // Mark current instance
        if (m_CurrentInstance)
            m_CurrentInstance->IsMarked = true;

        // Sweep phase: free unmarked objects
        auto it = m_Objects.begin();
        while (it != m_Objects.end())
        {
            if (!(*it)->IsMarked)
            {
                delete *it;
                it = m_Objects.erase(it);
            }
            else
            {
                (*it)->IsMarked = false; // Reset for next GC cycle
                ++it;
            }
        }
    }
}
