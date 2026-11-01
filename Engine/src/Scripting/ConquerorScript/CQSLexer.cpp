#include "CQSLexer.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>

namespace Conqueror::CQS
{
    const std::unordered_map<std::string, TokenType>& CQSLexer::Keywords()
    {
        static const std::unordered_map<std::string, TokenType> s_Keywords = {
            { "script",    TokenType::Script },
            { "class",     TokenType::Class },
            { "struct",    TokenType::Struct },
            { "enum",      TokenType::Enum },
            { "interface", TokenType::Interface },
            { "func",      TokenType::Func },
            { "function",  TokenType::Func },
            { "var",       TokenType::Var },
            { "let",       TokenType::Var },
            { "const",     TokenType::Var },
            { "if",        TokenType::If },
            { "else",      TokenType::Else },
            { "for",       TokenType::For },
            { "foreach",   TokenType::Foreach },
            { "while",     TokenType::While },
            { "do",        TokenType::Do },
            { "switch",    TokenType::Switch },
            { "case",      TokenType::Case },
            { "default",   TokenType::Default },
            { "break",     TokenType::Break },
            { "continue",  TokenType::Continue },
            { "return",    TokenType::Return },
            { "null",      TokenType::Null },
            { "true",      TokenType::True },
            { "false",     TokenType::False },
            { "this",      TokenType::This },
            { "base",      TokenType::Base },
            { "new",       TokenType::New },
            { "in",        TokenType::In },
            { "as",        TokenType::As },
            { "is",        TokenType::Is },
            { "public",    TokenType::Public },
            { "private",   TokenType::Private },
            { "protected", TokenType::Protected },
            { "static",    TokenType::Static },
            { "event",     TokenType::Event },
            { "yield",     TokenType::Yield },
            { "try",       TokenType::Try },
            { "catch",     TokenType::Catch },
            { "finally",   TokenType::Finally },
            { "throw",     TokenType::Throw },
            { "using",     TokenType::Using },
            { "namespace", TokenType::Namespace },
        };
        return s_Keywords;
    }

    bool CQSLexer::IsKeyword(const std::string& word)
    {
        return Keywords().count(word) != 0;
    }

    std::vector<Token> CQSLexer::Tokenize(const std::string& source, int firstLine)
    {
        m_Source = source;
        m_Start = m_Current = 0;
        m_Line = firstLine;
        m_Column = 1;
        m_Errors.clear();

        // UTF-8 BOM
        if (m_Source.size() >= 3 && (unsigned char)m_Source[0] == 0xEF && (unsigned char)m_Source[1] == 0xBB && (unsigned char)m_Source[2] == 0xBF)
            m_Current = 3;

        std::vector<Token> tokens;
        for (;;)
        {
            if (!SkipWhitespace())
                break;
            m_Start = m_Current;
            m_StartLine = m_Line;
            m_StartColumn = m_Column;
            if (IsAtEnd())
                break;
            Token t = ScanToken();
            if (t.Type != TokenType::Error)
                tokens.push_back(std::move(t));
        }
        m_Start = m_Current;
        m_StartLine = m_Line;
        m_StartColumn = m_Column;
        tokens.push_back(MakeToken(TokenType::EndOfFile));
        tokens.back().Lexeme.clear();
        return tokens;
    }

    char CQSLexer::Advance()
    {
        char c = m_Source[m_Current++];
        if (c == '\n')
        {
            ++m_Line;
            m_Column = 1;
        }
        else if (((unsigned char)c & 0xC0) != 0x80)
            ++m_Column; // UTF-8 devam baytları sütun saymaz
        return c;
    }

    bool CQSLexer::Match(char expected)
    {
        if (IsAtEnd() || m_Source[m_Current] != expected)
            return false;
        Advance();
        return true;
    }

    Token CQSLexer::MakeToken(TokenType type) const
    {
        return Token(type, m_Source.substr(m_Start, m_Current - m_Start), m_StartLine, m_StartColumn);
    }

    Token CQSLexer::ErrorToken(const std::string& message)
    {
        m_Errors.push_back("Line " + std::to_string(m_StartLine) + ":" + std::to_string(m_StartColumn) + ": " + message);
        return Token(TokenType::Error, message, m_StartLine, m_StartColumn);
    }

    bool CQSLexer::SkipWhitespace()
    {
        while (!IsAtEnd())
        {
            char c = Peek();
            if (c == ' ' || c == '\r' || c == '\t' || c == '\n' || c == '\f' || c == '\v')
            {
                Advance();
            }
            else if (c == '/' && PeekNext() == '/')
            {
                while (!IsAtEnd() && Peek() != '\n')
                    Advance();
            }
            else if (c == '/' && PeekNext() == '*')
            {
                int line = m_Line, col = m_Column;
                Advance();
                Advance();
                int depth = 1; // iç içe blok yorumlar
                while (!IsAtEnd() && depth > 0)
                {
                    if (Peek() == '/' && PeekNext() == '*') { Advance(); Advance(); ++depth; }
                    else if (Peek() == '*' && PeekNext() == '/') { Advance(); Advance(); --depth; }
                    else Advance();
                }
                if (depth > 0)
                {
                    m_StartLine = line;
                    m_StartColumn = col;
                    ErrorToken("Unterminated block comment.");
                    return false;
                }
            }
            else
                break;
        }
        return true;
    }

    Token CQSLexer::ScanToken()
    {
        char c = Advance();

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || ((unsigned char)c & 0x80))
            return ScanIdentifier();
        if (c >= '0' && c <= '9')
            return ScanNumber(c);
        if (c == '.' && Peek() >= '0' && Peek() <= '9')
            return ScanNumber(c);

        switch (c)
        {
            case '(': return MakeToken(TokenType::LeftParen);
            case ')': return MakeToken(TokenType::RightParen);
            case '{': return MakeToken(TokenType::LeftBrace);
            case '}': return MakeToken(TokenType::RightBrace);
            case '[': return MakeToken(TokenType::LeftBracket);
            case ']': return MakeToken(TokenType::RightBracket);
            case ';': return MakeToken(TokenType::Semicolon);
            case ':': return MakeToken(TokenType::Colon);
            case ',': return MakeToken(TokenType::Comma);
            case '~': return MakeToken(TokenType::Tilde);
            case '.': return MakeToken(Match('.') ? TokenType::DotDot : TokenType::Dot);
            case '+':
                if (Match('+')) return MakeToken(TokenType::PlusPlus);
                if (Match('=')) return MakeToken(TokenType::PlusEqual);
                return MakeToken(TokenType::Plus);
            case '-':
                if (Match('-')) return MakeToken(TokenType::MinusMinus);
                if (Match('=')) return MakeToken(TokenType::MinusEqual);
                if (Match('>')) return MakeToken(TokenType::Arrow);
                return MakeToken(TokenType::Minus);
            case '*': return MakeToken(Match('=') ? TokenType::StarEqual : TokenType::Star);
            case '/': return MakeToken(Match('=') ? TokenType::SlashEqual : TokenType::Slash);
            case '%': return MakeToken(Match('=') ? TokenType::PercentEqual : TokenType::Percent);
            case '^': return MakeToken(Match('=') ? TokenType::CaretEqual : TokenType::Caret);
            case '=':
                if (Match('=')) return MakeToken(TokenType::EqualEqual);
                if (Match('>')) return MakeToken(TokenType::FatArrow);
                return MakeToken(TokenType::Equal);
            case '!': return MakeToken(Match('=') ? TokenType::BangEqual : TokenType::Bang);
            case '<':
                if (Match('=')) return MakeToken(TokenType::LessEqual);
                if (Match('<')) return MakeToken(Match('=') ? TokenType::LessLessEqual : TokenType::LessLess);
                return MakeToken(TokenType::Less);
            case '>':
                if (Match('=')) return MakeToken(TokenType::GreaterEqual);
                if (Match('>')) return MakeToken(Match('=') ? TokenType::GreaterGreaterEqual : TokenType::GreaterGreater);
                return MakeToken(TokenType::Greater);
            case '&':
                if (Match('&')) return MakeToken(TokenType::AmpAmp);
                if (Match('=')) return MakeToken(TokenType::AmpEqual);
                return MakeToken(TokenType::Amp);
            case '|':
                if (Match('|')) return MakeToken(TokenType::PipePipe);
                if (Match('=')) return MakeToken(TokenType::PipeEqual);
                return MakeToken(TokenType::Pipe);
            case '?':
                if (Match('.')) return MakeToken(TokenType::QuestionDot);
                if (Match('?')) return MakeToken(Match('=') ? TokenType::QuestionQuestionEqual : TokenType::QuestionQuestion);
                return MakeToken(TokenType::Question);
            case '$':
                if (Peek() == '"') { Advance(); return ScanInterpolatedString(); }
                return MakeToken(TokenType::Dollar);
            case '@':
                if (Peek() == '"') { Advance(); return ScanVerbatimString(); }
                break;
            case '"': return ScanString('"');
            case '\'': return ScanString('\'');
            default: break;
        }
        return ErrorToken(std::string("Unexpected character '") + c + "'.");
    }

    bool CQSLexer::Unescape(const std::string& raw, std::string& out, std::string& error)
    {
        out.clear();
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
        {
            char c = raw[i];
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (++i >= raw.size())
            {
                error = "Dangling '\\' at end of string.";
                return false;
            }
            switch (raw[i])
            {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '0': out += '\0'; break;
                case 'a': out += '\a'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'v': out += '\v'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case '\'': out += '\''; break;
                case '{': out += '{'; break;
                case '}': out += '}'; break;
                case 'x':
                case 'u':
                case 'U':
                {
                    size_t digits = raw[i] == 'x' ? 2 : (raw[i] == 'u' ? 4 : 8);
                    uint32_t cp = 0;
                    for (size_t k = 1; k <= digits; ++k)
                    {
                        if (i + k >= raw.size()) { error = "Incomplete escape sequence."; return false; }
                        char h = raw[i + k];
                        int d = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                        if (d < 0) { error = "Invalid hex digit in escape sequence."; return false; }
                        cp = cp * 16 + (uint32_t)d;
                    }
                    i += digits;
                    // UTF-8 kodla
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp <= 0x10FFFF) { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    else { error = "Code point out of range."; return false; }
                    break;
                }
                default:
                    error = std::string("Unknown escape sequence '\\") + raw[i] + "'.";
                    return false;
            }
        }
        return true;
    }

    Token CQSLexer::ScanString(char quote)
    {
        std::string raw;
        while (!IsAtEnd() && Peek() != quote)
        {
            if (Peek() == '\n')
                return ErrorToken("Unterminated string (newline in string literal).");
            if (Peek() == '\\' && m_Current + 1 < m_Source.size())
                raw += Advance();
            raw += Advance();
        }
        if (IsAtEnd())
            return ErrorToken("Unterminated string.");
        Advance(); // closing quote
        Token t = MakeToken(TokenType::StringLiteral);
        std::string error;
        if (!Unescape(raw, t.Lexeme, error))
            return ErrorToken(error);
        return t;
    }

    Token CQSLexer::ScanVerbatimString()
    {
        // @"..." : kaçış yok, "" bir tırnak, çok satırlı olabilir
        std::string value;
        for (;;)
        {
            if (IsAtEnd())
                return ErrorToken("Unterminated verbatim string.");
            char c = Advance();
            if (c == '"')
            {
                if (Peek() == '"') { Advance(); value += '"'; continue; }
                break;
            }
            value += c;
        }
        Token t = MakeToken(TokenType::StringLiteral);
        t.Lexeme = value;
        return t;
    }

    Token CQSLexer::ScanInterpolatedString()
    {
        // Ham içerik saklanır; ayrıştırıcı {ifade} parçalarını ayırır
        std::string raw;
        int braceDepth = 0;
        while (!IsAtEnd())
        {
            char c = Peek();
            if (c == '\n' && braceDepth == 0)
                return ErrorToken("Unterminated interpolated string.");
            if (braceDepth == 0 && c == '"')
                break;
            if (c == '\\' && m_Current + 1 < m_Source.size())
            {
                raw += Advance();
                raw += Advance();
                continue;
            }
            if (c == '{')
            {
                if (braceDepth == 0 && PeekNext() == '{') { raw += Advance(); raw += Advance(); continue; }
                ++braceDepth;
            }
            else if (c == '}')
            {
                if (braceDepth == 0 && PeekNext() == '}') { raw += Advance(); raw += Advance(); continue; }
                if (braceDepth > 0) --braceDepth;
            }
            else if (c == '"' && braceDepth > 0)
            {
                // ifade içindeki metin
                raw += Advance();
                while (!IsAtEnd() && Peek() != '"' && Peek() != '\n')
                {
                    if (Peek() == '\\' && m_Current + 1 < m_Source.size()) raw += Advance();
                    raw += Advance();
                }
                if (IsAtEnd() || Peek() == '\n')
                    return ErrorToken("Unterminated string inside interpolation.");
            }
            raw += Advance();
        }
        if (IsAtEnd())
            return ErrorToken("Unterminated interpolated string.");
        Advance(); // closing quote
        Token t = MakeToken(TokenType::InterpolatedString);
        t.Lexeme = raw;
        return t;
    }

    Token CQSLexer::ScanNumber(char first)
    {
        auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        if (first == '0' && (Peek() == 'x' || Peek() == 'X' || Peek() == 'b' || Peek() == 'B'))
        {
            bool hex = (Peek() == 'x' || Peek() == 'X');
            Advance();
            std::string digits;
            while (!IsAtEnd())
            {
                char c = Peek();
                if (c == '_') { Advance(); continue; }
                bool ok = hex ? (isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) : (c == '0' || c == '1');
                if (!ok) break;
                digits += Advance();
            }
            if (digits.empty())
                return ErrorToken(hex ? "Expected hex digits after '0x'." : "Expected binary digits after '0b'.");
            Token t = MakeToken(TokenType::IntLiteral);
            t.IntValue = (int64_t)std::strtoull(digits.c_str(), nullptr, hex ? 16 : 2);
            return t;
        }

        std::string text(1, first);
        bool isFloat = (first == '.');
        while (isDigit(Peek()) || Peek() == '_')
        {
            char c = Advance();
            if (c != '_') text += c;
        }
        if (!isFloat && Peek() == '.' && isDigit(PeekNext()))
        {
            isFloat = true;
            text += Advance();
            while (isDigit(Peek()) || Peek() == '_')
            {
                char c = Advance();
                if (c != '_') text += c;
            }
        }
        if (Peek() == 'e' || Peek() == 'E')
        {
            size_t save = m_Current;
            int saveLine = m_Line, saveCol = m_Column;
            std::string exp(1, Advance());
            if (Peek() == '+' || Peek() == '-') exp += Advance();
            if (isDigit(Peek()))
            {
                while (isDigit(Peek())) exp += Advance();
                text += exp;
                isFloat = true;
            }
            else
            {
                m_Current = save;
                m_Line = saveLine;
                m_Column = saveCol;
            }
        }
        if (Peek() == 'f' || Peek() == 'F' || Peek() == 'd' || Peek() == 'D')
        {
            Advance();
            isFloat = true;
        }
        else if (Peek() == 'L' || Peek() == 'l')
            Advance();

        Token t = MakeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral);
        if (isFloat)
            t.FloatValue = std::strtod(text.c_str(), nullptr);
        else
        {
            errno = 0;
            unsigned long long v = std::strtoull(text.c_str(), nullptr, 10);
            if (errno == ERANGE || v > (unsigned long long)INT64_MAX)
            {
                t.Type = TokenType::FloatLiteral;
                t.FloatValue = std::strtod(text.c_str(), nullptr);
            }
            else
                t.IntValue = (int64_t)v;
        }
        return t;
    }

    Token CQSLexer::ScanIdentifier()
    {
        while (!IsAtEnd())
        {
            char c = Peek();
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || ((unsigned char)c & 0x80))
                Advance();
            else
                break;
        }
        Token t = MakeToken(TokenType::Identifier);
        auto it = Keywords().find(t.Lexeme);
        if (it != Keywords().end())
            t.Type = it->second;
        return t;
    }

    const char* Token::TypeToString(TokenType type)
    {
        switch (type)
        {
            case TokenType::IntLiteral: return "integer";
            case TokenType::FloatLiteral: return "float";
            case TokenType::StringLiteral: return "string";
            case TokenType::InterpolatedString: return "interpolated string";
            case TokenType::Identifier: return "identifier";
            case TokenType::LeftParen: return "'('";
            case TokenType::RightParen: return "')'";
            case TokenType::LeftBrace: return "'{'";
            case TokenType::RightBrace: return "'}'";
            case TokenType::LeftBracket: return "'['";
            case TokenType::RightBracket: return "']'";
            case TokenType::Semicolon: return "';'";
            case TokenType::Colon: return "':'";
            case TokenType::Comma: return "','";
            case TokenType::Dot: return "'.'";
            case TokenType::EndOfFile: return "end of file";
            default: return "token";
        }
    }
}
