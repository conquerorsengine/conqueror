#pragma once

#include <cstddef>
#include <cstdint>

namespace Conqueror::CQS
{
    // Sabit genişlikli komut: { Op, A(u8), B(u16), C(i32) }
    // Yığın etkileri açıklamalarda [önce] -> [sonra] biçimindedir.
    enum class OpCode : uint8_t
    {
        Nop,

        PushNull, PushTrue, PushFalse,
        PushInt,            // C = değer                         [] -> [int]
        PushConst,          // B = sabit indeksi                 [] -> [k]

        Pop,                //                                   [a] -> []
        PopN,               // B = adet
        Dup,                //                                   [a] -> [a a]
        Dup2,               //                                   [a b] -> [a b a b]
        Swap,               //                                   [a b] -> [b a]
        Bury,               // B = n: üstteki değeri n slot aşağı taşı   [x1..xn v] -> [v x1..xn]

        GetLocal,           // B = slot                          [] -> [v]
        SetLocal,           // B = slot                          [v] -> [v]
        GetUpval,           // B = indeks
        SetUpval,           // B = indeks                        [v] -> [v]
        CloseUpval,         // üstteki slotu kapat ve at          [v] -> []

        GetGlobal,          // C = global indeksi
        SetGlobal,          // C                                 [v] -> [v]
        DefineGlobal,       // C                                 [v] -> []
        GetDynamic,         // B = ad, C = global: this özelliği ya da global   [this] -> [v]

        GetProp,            // B = ad, C = önbellek              [obj] -> [v]
        SetProp,            // B = ad, C = önbellek              [obj v] -> [v]
        GetField,           // B = slot                          [obj] -> [v]
        SetField,           // B = slot                          [obj v] -> [v]
        GetThisField,       // B = slot                          [] -> [v]
        SetThisField,       // B = slot                          [v] -> [v]
        GetIndex,           //                                   [obj i] -> [v]
        SetIndex,           //                                   [obj i v] -> [v]
        GetSuper,           // B = ad                            [this] -> [bound]

        Add, Sub, Mul, Div, Mod,      //                         [a b] -> [r]
        Neg, Not, BitNot,             //                         [a] -> [r]
        BitAnd, BitOr, BitXor, Shl, Shr,
        Eq, Ne, Lt, Le, Gt, Ge,
        ToBool,             //                                   [a] -> [bool]
        MakeRange,          //                                   [a b] -> [range]
        IsType,             // B = tip adı                       [v] -> [bool]
        CastType,           // B = tip adı                       [v] -> [v']

        Jump,               // C = hedef
        JumpIfFalse,        // C                                 [c] -> []
        JumpIfTrue,         // C                                 [c] -> []
        JumpIfNullKeep,     // C: null ise kalır ve atlar, değilse dokunmaz
        JumpIfNotNullKeep,  // C: null değilse kalır ve atlar, null ise atılır
        Loop,               // C = hedef (geri kenar, GC güvenli noktası)
        JumpIfArg,          // B = parametre indeksi, C: argüman verilmişse atla

        Call,               // A = argc                          [f a1..an] -> [r]
        Invoke,             // A = argc, B = ad, C = önbellek    [o a1..an] -> [r]
        SuperInvoke,        // A = argc, B = ad                  [this a1..an] -> [r]
        Return,             //                                   [v] -> (çağırana)
        ReturnNull,

        Closure,            // B = sabit (fonksiyon prototipi)   [] -> [closure]
        Class,              // B = sabit (sınıf şablonu)         [] -> [class]
        Inherit,            // A = 1: taban bulunamazsa yok say  [class base] -> [class]
        Method,             // A = tür (0 metot, 1 statik, 2 yapıcı, 3 alan başlatıcı), B = ad   [class f] -> [class]
        StaticField,        // B = ad                            [class v] -> [class]

        NewList,            // B = adet                          [e1..en] -> [list]
        NewMap,             // B = çift sayısı                   [k1 v1 ..] -> [map]
        IterPrep,           // A = değişken sayısı (1/2)         [x] -> [seq]
        IterNext,           // A = değişken sayısı, B = seq slotu (indeks slotu B+1), C = çıkış   [] -> [e] / [k v]
        Throw,              //                                   [v] -> (yayılır)
        ToStr,              // B = biçim sabiti (0xFFFF: yok)    [v] -> [str]
        Concat,             // B = adet                          [s1..sn] -> [str]
        NewArray,           // B = öğe tipi adı                  [n] -> [list]

        Count_
    };

    inline const char* OpCodeName(OpCode op)
    {
        static const char* const names[] = {
            "NOP", "PUSH_NULL", "PUSH_TRUE", "PUSH_FALSE", "PUSH_INT", "PUSH_CONST",
            "POP", "POPN", "DUP", "DUP2", "SWAP", "BURY",
            "GET_LOCAL", "SET_LOCAL", "GET_UPVAL", "SET_UPVAL", "CLOSE_UPVAL",
            "GET_GLOBAL", "SET_GLOBAL", "DEFINE_GLOBAL", "GET_DYNAMIC",
            "GET_PROP", "SET_PROP", "GET_FIELD", "SET_FIELD", "GET_THIS_FIELD", "SET_THIS_FIELD", "GET_INDEX", "SET_INDEX", "GET_SUPER",
            "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT", "BIT_NOT",
            "BIT_AND", "BIT_OR", "BIT_XOR", "SHL", "SHR",
            "EQ", "NE", "LT", "LE", "GT", "GE",
            "TO_BOOL", "MAKE_RANGE", "IS_TYPE", "CAST_TYPE",
            "JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "JUMP_IF_NULL_KEEP", "JUMP_IF_NOT_NULL_KEEP", "LOOP", "JUMP_IF_ARG",
            "CALL", "INVOKE", "SUPER_INVOKE", "RETURN", "RETURN_NULL",
            "CLOSURE", "CLASS", "INHERIT", "METHOD", "STATIC_FIELD",
            "NEW_LIST", "NEW_MAP", "ITER_PREP", "ITER_NEXT", "THROW", "TO_STR", "CONCAT", "NEW_ARRAY",
        };
        static_assert(sizeof(names) / sizeof(names[0]) == (size_t)OpCode::Count_, "OpCodeName table out of sync");
        return (size_t)op < (size_t)OpCode::Count_ ? names[(size_t)op] : "???";
    }

    // Komutun düşmeyen (fall-through) yoldaki yığın etkisi.
    // Atlama hedefindeki derinlik farklı olan komutlar: JumpIfNotNullKeep (hedef: +0, düşen: -1),
    // IterNext (hedef/çıkış: +0, düşen: +A). Return/ReturnNull/Throw sonlandırıcıdır.
    inline int StackEffect(OpCode op, int a, int b)
    {
        switch (op)
        {
            case OpCode::Nop: return 0;
            case OpCode::PushNull: case OpCode::PushTrue: case OpCode::PushFalse: case OpCode::PushInt: case OpCode::PushConst: return 1;
            case OpCode::Pop: return -1;
            case OpCode::PopN: return -b;
            case OpCode::Dup: return 1;
            case OpCode::Dup2: return 2;
            case OpCode::Swap: case OpCode::Bury: return 0;
            case OpCode::GetLocal: return 1;
            case OpCode::SetLocal: return 0;
            case OpCode::GetUpval: return 1;
            case OpCode::SetUpval: return 0;
            case OpCode::CloseUpval: return -1;
            case OpCode::GetGlobal: return 1;
            case OpCode::SetGlobal: return 0;
            case OpCode::DefineGlobal: return -1;
            case OpCode::GetDynamic: return 0;
            case OpCode::GetProp: return 0;
            case OpCode::SetProp: return -1;
            case OpCode::GetField: return 0;
            case OpCode::SetField: return -1;
            case OpCode::GetThisField: return 1;
            case OpCode::SetThisField: return 0;
            case OpCode::GetIndex: return -1;
            case OpCode::SetIndex: return -2;
            case OpCode::GetSuper: return 0;
            case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
            case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor: case OpCode::Shl: case OpCode::Shr:
            case OpCode::Eq: case OpCode::Ne: case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge:
            case OpCode::MakeRange:
                return -1;
            case OpCode::Neg: case OpCode::Not: case OpCode::BitNot: case OpCode::ToBool: case OpCode::IsType: case OpCode::CastType: return 0;
            case OpCode::Jump: return 0;
            case OpCode::JumpIfFalse: case OpCode::JumpIfTrue: return -1;
            case OpCode::JumpIfNullKeep: return 0;
            case OpCode::JumpIfNotNullKeep: return -1;
            case OpCode::Loop: case OpCode::JumpIfArg: return 0;
            case OpCode::Call: case OpCode::Invoke: case OpCode::SuperInvoke: return -a;
            case OpCode::Return: return -1;
            case OpCode::ReturnNull: return 0;
            case OpCode::Closure: case OpCode::Class: return 1;
            case OpCode::Inherit: case OpCode::Method: case OpCode::StaticField: return -1;
            case OpCode::NewList: return 1 - b;
            case OpCode::NewMap: return 1 - 2 * b;
            case OpCode::IterPrep: return 0;
            case OpCode::IterNext: return a;
            case OpCode::Throw: return -1;
            case OpCode::ToStr: return 0;
            case OpCode::Concat: return 1 - b;
            case OpCode::NewArray: return 0;
            default: return 0;
        }
    }

    inline bool IsTerminator(OpCode op)
    {
        return op == OpCode::Return || op == OpCode::ReturnNull || op == OpCode::Throw || op == OpCode::Jump || op == OpCode::Loop;
    }

    inline bool IsBranch(OpCode op)
    {
        switch (op)
        {
            case OpCode::Jump: case OpCode::JumpIfFalse: case OpCode::JumpIfTrue: case OpCode::JumpIfNullKeep:
            case OpCode::JumpIfNotNullKeep: case OpCode::Loop: case OpCode::JumpIfArg: case OpCode::IterNext:
                return true;
            default: return false;
        }
    }

    // Atlama hedefine ulaşıldığında (komut öncesi derinliğe göre) yığın etkisi
    inline int BranchStackEffect(OpCode op)
    {
        switch (op)
        {
            case OpCode::JumpIfFalse: case OpCode::JumpIfTrue: return -1;
            default: return 0;  // JumpIfNullKeep/NotNullKeep hedefte değeri tutar, IterNext çıkışı iter. değeri itmez
        }
    }
}
