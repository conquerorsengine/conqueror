#pragma once

// LuaQ derleyicisinin iç yapıları: sözcükler, AST, ayrıştırıcı ve Luau yayıcısı (yalnızca LuaQ*.cpp)

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Conqueror::LuaQ::Detail
{
    struct CompileError
    {
        int Line = 0, Col = 0;
        std::string Message;
    };

    // ───────────────────────────── Sözcükler
    enum class Tok { Eof, Name, Number, String, Interp, Op };

    struct Token
    {
        Tok Type = Tok::Eof;
        std::string Text;          // ad, ham sayı, ham dize (tırnaklarıyla), işleç
        int Line = 1, Col = 1;
    };

    std::vector<Token> Lex(const std::string& source);

    // ───────────────────────────── AST
    struct Expr;
    struct Stat;
    struct Block;
    using ExprP = std::unique_ptr<Expr>;
    using StatP = std::unique_ptr<Stat>;

    struct Param
    {
        std::string Name;
        ExprP Default;             // LuaQ: varsayılan değer
    };

    struct FuncBody
    {
        std::vector<Param> Params;
        bool Vararg = false;
        bool Async = false;
        bool ImplicitSelf = false; // class yöntemleri ve a:b() bildirimleri
        std::unique_ptr<Block> Body;
        int Line = 0;
    };

    enum class EK
    {
        Nil, True, False, Number, String, Vararg, Function, Table, Binary, Unary, Name,
        Field, Index, Call, Method, Paren, IfElse, Interp, Coalesce, Super, Spread, Await
    };

    struct TableItem
    {
        enum Kind { Positional, Named, Keyed, SpreadItem } K = Positional;
        std::string Name;
        ExprP Key, Value;
    };

    struct Expr
    {
        EK Kind = EK::Nil;
        int Line = 0, Col = 0;
        std::string Text;          // ad / sayı / dize / işleç / alan ya da yöntem adı
        ExprP A, B;                // ikili (A op B), tekli (A), alan (A.Text), indeks (A[B]), çağrı (A(...)), paren, ??
        std::vector<ExprP> List;   // çağrı argümanları; if-ifadesi: [k1, d1, k2, d2, ..., değilse]; interp ifadeleri
        std::vector<TableItem> Items;
        std::unique_ptr<FuncBody> Func;
        std::vector<std::string> Parts;  // interp: ham metin parçaları (List.size() + 1)
        bool Optional = false;     // ?. ?[ ?: ?( : nesne nil ise zincirin geri kalanı nil
    };

    enum class SK
    {
        Local, Assign, Compound, CallStat, Do, While, Repeat, If, NumFor, GenFor, Function, LocalFunction,
        Return, Break, Continue, Goto, Label, Class, Match, Defer, Enum, Destructure, Empty
    };

    struct ClassMember
    {
        enum Kind { Method, StaticMethod, Field, StaticField } K = Method;
        std::string Name;
        std::unique_ptr<FuncBody> Func;
        ExprP Value;
        int Line = 0;
        std::vector<std::string> Attributes;
    };

    struct MatchCase
    {
        std::vector<ExprP> Patterns;
        bool Wildcard = false;
        ExprP Guard;
        std::unique_ptr<Block> Body;
        int Line = 0;
    };

    struct Stat
    {
        SK Kind = SK::Empty;
        int Line = 0, Col = 0;
        std::vector<std::string> Names;      // local / for / destructure / enum adları
        std::vector<std::string> Attribs;    // local: "", "const", "close"
        std::vector<ExprP> Targets, Values;  // atama hedefleri / değerler; return; for sınırları; if koşulları
        std::string Op;                      // bileşik işleç; goto / etiket adı
        std::vector<std::string> FuncPath;   // function a.b.c:d
        bool FuncMethod = false;
        std::unique_ptr<FuncBody> Func;
        std::unique_ptr<Block> Body, Else;
        std::vector<std::unique_ptr<Block>> Blocks;  // if dalları
        ExprP Cond;                          // while / repeat koşulu, match konusu
        std::string ClassName;
        ExprP Base;
        std::vector<ClassMember> Members;
        std::vector<MatchCase> Cases;
        std::vector<ExprP> EnumValues;       // Names ile paralel (nullptr = otomatik)
        StatP Inner;                         // defer edilen deyim
        bool ArrayPattern = false;           // local [a, b] = ...
        std::vector<std::string> Attributes; // @native vb.
    };

    struct Block
    {
        std::vector<StatP> Stats;
        int EndLine = 0;
    };

    struct Chunk
    {
        std::unique_ptr<Block> Body;
    };

    Chunk Parse(const std::vector<Token>& tokens);

    struct EmitOptions
    {
        std::string AutoReturnClass;
    };
    std::string Emit(const Chunk& chunk, const EmitOptions& options);
}
