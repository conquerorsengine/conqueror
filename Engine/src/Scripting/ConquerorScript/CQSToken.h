#pragma once

#include <string>
#include <cstdint>

namespace Conqueror::CQS
{
    enum class TokenType : uint8_t
    {
        // ── Literals ──
        IntLiteral,         // 42
        FloatLiteral,       // 3.14f
        StringLiteral,      // "hello"

        // ── Identifier ──
        Identifier,         // playerSpeed, MyClass, etc.

        // ── Keywords: Declarations ──
        Script,             // script
        Class,              // class
        Struct,             // struct
        Enum,               // enum
        Interface,          // interface
        Func,               // func
        Var,                // var

        // ── Keywords: Control Flow ──
        If,                 // if
        Else,               // else
        For,                // for
        Foreach,            // foreach
        While,              // while
        Switch,             // switch
        Case,               // case
        Default,            // default
        Break,              // break
        Continue,           // continue
        Return,             // return

        // ── Keywords: Types & Values ──
        Null,               // null
        True,               // true
        False,              // false
        This,               // this
        New,                // new
        In,                 // in
        As,                 // as
        Is,                 // is

        // ── Keywords: Access ──
        Public,             // public
        Private,            // private
        Static,             // static

        // ── Keywords: Advanced ──
        Event,              // event
        Yield,              // yield
        Try,                // try
        Catch,              // catch
        Finally,            // finally
        Throw,              // throw
        Using,              // using
        Namespace,          // namespace

        // ── Operators: Arithmetic ──
        Plus,               // +
        Minus,              // -
        Star,               // *
        Slash,              // /
        Percent,            // %

        // ── Operators: Assignment ──
        Equal,              // =
        PlusEqual,          // +=
        MinusEqual,         // -=
        StarEqual,          // *=
        SlashEqual,         // /=
        PercentEqual,       // %=

        // ── Operators: Increment/Decrement ──
        PlusPlus,           // ++
        MinusMinus,         // --

        // ── Operators: Comparison ──
        EqualEqual,         // ==
        BangEqual,          // !=
        Less,               // <
        Greater,            // >
        LessEqual,          // <=
        GreaterEqual,       // >=

        // ── Operators: Logical ──
        AmpAmp,             // &&
        PipePipe,           // ||
        Bang,               // !

        // ── Operators: Bitwise ──
        Amp,                // &
        Pipe,               // |
        Caret,              // ^
        Tilde,              // ~
        LessLess,           // <<
        GreaterGreater,     // >>

        // ── Operators: Special ──
        Arrow,              // ->  (return type)
        FatArrow,           // =>  (lambda)
        QuestionDot,        // ?.  (null-conditional)
        QuestionQuestion,   // ??  (null-coalescing)
        Question,           // ?   (ternary)

        // ── Delimiters ──
        LeftParen,          // (
        RightParen,         // )
        LeftBrace,          // {
        RightBrace,         // }
        LeftBracket,        // [
        RightBracket,       // ]

        // ── Punctuation ──
        Semicolon,          // ;
        Colon,              // :
        Comma,              // ,
        Dot,                // .
        DotDot,             // ..  (range, opsiyonel)
        Dollar,             // $   (string interpolation)

        // ── Special ──
        EndOfFile,
        Error
    };

    struct Token
    {
        TokenType   Type;
        std::string Lexeme;     // Kaynak koddaki orijinal metin
        int         Line;       // Satır numarası (hata raporlama)
        int         Column;     // Sütun numarası

        // Literal değerler (opsiyonel)
        union
        {
            int64_t IntValue;
            double  FloatValue;
        };

        Token() : Type(TokenType::Error), Line(0), Column(0), IntValue(0) {}

        Token(TokenType type, const std::string& lexeme, int line, int col)
            : Type(type), Lexeme(lexeme), Line(line), Column(col), IntValue(0) {}

        // Debug: Token'ı string'e çevir
        static const char* TypeToString(TokenType type);
    };
}
