#pragma once

#include <cstdint>
#include <string>

namespace Conqueror::CQS
{
    enum class TokenType : uint8_t
    {
        // ── Literals ──
        IntLiteral,         // 42, 0xFF, 0b101
        FloatLiteral,       // 3.14, 2.5f, 1e-3
        StringLiteral,      // "hello" (kaçış dizileri çözülmüş)
        InterpolatedString, // $"Hello {name}" (Lexeme: ham içerik)

        Identifier,

        // ── Keywords: Declarations ──
        Script, Class, Struct, Enum, Interface, Func, Var,

        // ── Keywords: Control Flow ──
        If, Else, For, Foreach, While, Do, Switch, Case, Default, Break, Continue, Return,

        // ── Keywords: Types & Values ──
        Null, True, False, This, Base, New, In, As, Is,

        // ── Keywords: Access ──
        Public, Private, Protected, Static,

        // ── Keywords: Advanced ──
        Event, Yield, Try, Catch, Finally, Throw, Using, Namespace,

        // ── Operators ──
        Plus, Minus, Star, Slash, Percent,
        Equal, PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
        AmpEqual, PipeEqual, CaretEqual, LessLessEqual, GreaterGreaterEqual, QuestionQuestionEqual,
        PlusPlus, MinusMinus,
        EqualEqual, BangEqual, Less, Greater, LessEqual, GreaterEqual,
        AmpAmp, PipePipe, Bang,
        Amp, Pipe, Caret, Tilde, LessLess, GreaterGreater,
        Arrow,              // ->
        FatArrow,           // =>
        QuestionDot,        // ?.
        QuestionQuestion,   // ??
        Question,           // ?

        // ── Delimiters ──
        LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
        Semicolon, Colon, Comma, Dot, DotDot, Dollar,

        EndOfFile,
        Error
    };

    struct Token
    {
        TokenType   Type = TokenType::Error;
        std::string Lexeme;
        int         Line = 0;
        int         Column = 0;
        int64_t     IntValue = 0;
        double      FloatValue = 0.0;

        Token() = default;
        Token(TokenType type, std::string lexeme, int line, int col)
            : Type(type), Lexeme(std::move(lexeme)), Line(line), Column(col) {}

        static const char* TypeToString(TokenType type);
    };
}
