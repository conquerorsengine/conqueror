#pragma once

#include <cstdint>

namespace Conqueror::CQS
{
    enum class OpCode : uint8_t
    {
        // Constants
        Constant,           // 1 byte index
        ConstantLong,       // 2 byte index

        // Literals
        Null, True, False, Zero, One,

        // Arithmetic
        Add, Subtract, Multiply, Divide, Modulo, Negate,
        Increment, Decrement,

        // Comparison
        Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,

        // Logical
        Not, And, Or,

        // Bitwise
        BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight,

        // Type Operations
        CastToInt, CastToFloat, CastToString,
        TypeCheck,          // 1 byte

        // Variables
        GetLocal,           // 1 byte slot
        SetLocal,           // 1 byte slot
        GetGlobal,          // 1 byte constant index
        SetGlobal,          // 1 byte constant index
        DefineGlobal,       // 1 byte constant index

        // Upvalues
        GetUpvalue,         // 1 byte index
        SetUpvalue,         // 1 byte index
        CloseUpvalue,

        // Properties
        GetProperty,        // 1 byte constant index
        SetProperty,        // 1 byte constant index

        // List / Map
        NewList,            // 1 byte initial count
        GetIndex, SetIndex,
        NewMap,             // 1 byte initial count
        GetKey, SetKey,

        // Stack
        Pop, PopN, Dup, Swap,

        // Control Flow
        Jump,               // 2 byte offset
        JumpIfFalse,        // 2 byte offset
        JumpIfTrue,         // 2 byte offset
        Loop,               // 2 byte offset

        // Functions
        Call,               // 1 byte arg count
        Return,

        // Objects
        NewInstance,         // 1 byte constant index
        Invoke,             // 1 byte name index, 1 byte arg count
        Closure,            // 1 byte constant index

        // Special
        Print, Halt,
        New, This
    };

    struct OpCodeInfo
    {
        const char* Name;
        int OperandBytes;
    };

    inline OpCodeInfo GetOpCodeInfo(OpCode code)
    {
        switch (code)
        {
            case OpCode::Constant:      return { "CONSTANT", 1 };
            case OpCode::ConstantLong:  return { "CONSTANT_LONG", 2 };
            case OpCode::Null:          return { "NULL", 0 };
            case OpCode::True:          return { "TRUE", 0 };
            case OpCode::False:         return { "FALSE", 0 };
            case OpCode::Zero:          return { "ZERO", 0 };
            case OpCode::One:           return { "ONE", 0 };
            case OpCode::Add:           return { "ADD", 0 };
            case OpCode::Subtract:      return { "SUBTRACT", 0 };
            case OpCode::Multiply:      return { "MULTIPLY", 0 };
            case OpCode::Divide:        return { "DIVIDE", 0 };
            case OpCode::Modulo:        return { "MODULO", 0 };
            case OpCode::Negate:        return { "NEGATE", 0 };
            case OpCode::Increment:     return { "INCREMENT", 0 };
            case OpCode::Decrement:     return { "DECREMENT", 0 };
            case OpCode::Equal:         return { "EQUAL", 0 };
            case OpCode::NotEqual:      return { "NOT_EQUAL", 0 };
            case OpCode::Less:          return { "LESS", 0 };
            case OpCode::LessEqual:     return { "LESS_EQUAL", 0 };
            case OpCode::Greater:       return { "GREATER", 0 };
            case OpCode::GreaterEqual:  return { "GREATER_EQUAL", 0 };
            case OpCode::Not:           return { "NOT", 0 };
            case OpCode::And:           return { "AND", 0 };
            case OpCode::Or:            return { "OR", 0 };
            case OpCode::BitAnd:        return { "BIT_AND", 0 };
            case OpCode::BitOr:         return { "BIT_OR", 0 };
            case OpCode::BitXor:        return { "BIT_XOR", 0 };
            case OpCode::BitNot:        return { "BIT_NOT", 0 };
            case OpCode::ShiftLeft:     return { "SHIFT_LEFT", 0 };
            case OpCode::ShiftRight:    return { "SHIFT_RIGHT", 0 };
            case OpCode::CastToInt:     return { "CAST_INT", 0 };
            case OpCode::CastToFloat:   return { "CAST_FLOAT", 0 };
            case OpCode::CastToString:  return { "CAST_STRING", 0 };
            case OpCode::TypeCheck:     return { "TYPE_CHECK", 1 };
            case OpCode::GetLocal:      return { "GET_LOCAL", 1 };
            case OpCode::SetLocal:      return { "SET_LOCAL", 1 };
            case OpCode::GetGlobal:     return { "GET_GLOBAL", 1 };
            case OpCode::SetGlobal:     return { "SET_GLOBAL", 1 };
            case OpCode::DefineGlobal:  return { "DEFINE_GLOBAL", 1 };
            case OpCode::GetUpvalue:    return { "GET_UPVALUE", 1 };
            case OpCode::SetUpvalue:    return { "SET_UPVALUE", 1 };
            case OpCode::CloseUpvalue:  return { "CLOSE_UPVALUE", 0 };
            case OpCode::GetProperty:   return { "GET_PROPERTY", 1 };
            case OpCode::SetProperty:   return { "SET_PROPERTY", 1 };
            case OpCode::NewList:       return { "NEW_LIST", 1 };
            case OpCode::GetIndex:      return { "GET_INDEX", 0 };
            case OpCode::SetIndex:      return { "SET_INDEX", 0 };
            case OpCode::NewMap:        return { "NEW_MAP", 1 };
            case OpCode::GetKey:        return { "GET_KEY", 0 };
            case OpCode::SetKey:        return { "SET_KEY", 0 };
            case OpCode::Pop:           return { "POP", 0 };
            case OpCode::PopN:          return { "POP_N", 1 };
            case OpCode::Dup:           return { "DUP", 0 };
            case OpCode::Swap:          return { "SWAP", 0 };
            case OpCode::Jump:          return { "JUMP", 2 };
            case OpCode::JumpIfFalse:   return { "JUMP_IF_FALSE", 2 };
            case OpCode::JumpIfTrue:    return { "JUMP_IF_TRUE", 2 };
            case OpCode::Loop:          return { "LOOP", 2 };
            case OpCode::Call:          return { "CALL", 1 };
            case OpCode::Return:        return { "RETURN", 0 };
            case OpCode::NewInstance:   return { "NEW_INSTANCE", 1 };
            case OpCode::Invoke:        return { "INVOKE", 2 };
            case OpCode::Closure:       return { "CLOSURE", 1 };
            case OpCode::Print:         return { "PRINT", 0 };
            case OpCode::Halt:          return { "HALT", 0 };
            case OpCode::New:           return { "NEW", 2 }; // name index + arg count
            case OpCode::This:          return { "THIS", 0 };
            default:                    return { "UNKNOWN", 0 };
        }
    }
}
