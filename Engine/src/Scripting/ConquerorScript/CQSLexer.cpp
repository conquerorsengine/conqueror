#include "CQSLexer.h"

namespace Conqueror::CQS
{
    // ── Keyword Tablosu ──
    std::unordered_map<std::string, TokenType> CQSLexer::s_Keywords = {
        // Declarations
        { "script",     TokenType::Script },
        { "class",      TokenType::Class },
        { "struct",     TokenType::Struct },
        { "enum",       TokenType::Enum },
        { "interface",  TokenType::Interface },
        { "func",       TokenType::Func },
        { "var",        TokenType::Var },

        // Control flow
        { "if",         TokenType::If },
        { "else",       TokenType::Else },
        { "for",        TokenType::For },
        { "foreach",    TokenType::Foreach },
        { "while",      TokenType::While },
        { "switch",     TokenType::Switch },
        { "case",       TokenType::Case },
        { "default",    TokenType::Default },
        { "break",      TokenType::Break },
        { "continue",   TokenType::Continue },
        { "return",     TokenType::Return },

        // Values
        { "null",       TokenType::Null },
        { "true",       TokenType::True },
        { "false",      TokenType::False },
        { "this",       TokenType::This },
        { "new",        TokenType::New },
        { "in",         TokenType::In },
        { "as",         TokenType::As },
        { "is",         TokenType::Is },

        // Access modifiers
        { "public",     TokenType::Public },
        { "private",    TokenType::Private },
        { "static",     TokenType::Static },

        // Advanced
        { "event",      TokenType::Event },
        { "yield",      TokenType::Yield },
        { "try",        TokenType::Try },
        { "catch",      TokenType::Catch },
        { "finally",    TokenType::Finally },
        { "throw",      TokenType::Throw },
        { "using",      TokenType::Using },
        { "namespace",  TokenType::Namespace },
    };

    // ── Main Tokenize Function ──
    std::vector<Token> CQSLexer::Tokenize(const std::string& source)
    {
        m_Source  = source;
        m_Start   = 0;
        m_Current = 0;
        m_Line    = 1;
        m_Column  = 1;
        m_HasError = false;
        m_Errors.clear();

        std::vector<Token> tokens;

        while (!IsAtEnd())
        {
            m_Start = m_Current;
            Token token = ScanToken();
            if (token.Type != TokenType::Error || m_HasError)
            {
                tokens.push_back(token);
            }
        }

        tokens.push_back(MakeToken(TokenType::EndOfFile));
        return tokens;
    }

    // ── Character Helpers ──
    bool CQSLexer::IsAtEnd() const
    {
        return m_Current >= static_cast<int>(m_Source.size());
    }

    char CQSLexer::Advance()
    {
        char c = m_Source[m_Current++];
        m_Column++;
        return c;
    }

    char CQSLexer::Peek() const
    {
        if (IsAtEnd()) return '\0';
        return m_Source[m_Current];
    }

    char CQSLexer::PeekNext() const
    {
        if (m_Current + 1 >= static_cast<int>(m_Source.size())) return '\0';
        return m_Source[m_Current + 1];
    }

    bool CQSLexer::Match(char expected)
    {
        if (IsAtEnd()) return false;
        if (m_Source[m_Current] != expected) return false;
        m_Current++;
        m_Column++;
        return true;
    }

    bool CQSLexer::IsDigit(char c) const
    {
        return c >= '0' && c <= '9';
    }

    bool CQSLexer::IsAlpha(char c) const
    {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_';
    }

    bool CQSLexer::IsAlphaNumeric(char c) const
    {
        return IsAlpha(c) || IsDigit(c);
    }

    // ── Token Creation ──
    Token CQSLexer::MakeToken(TokenType type) const
    {
        std::string lexeme = m_Source.substr(m_Start, m_Current - m_Start);
        return Token(type, lexeme, m_Line, m_Column - static_cast<int>(lexeme.size()));
    }

    Token CQSLexer::ErrorToken(const std::string& message)
    {
        m_HasError = true;
        std::string errorMsg = "Line " + std::to_string(m_Line) + ": " + message;
        m_Errors.push_back(errorMsg);
        return Token(TokenType::Error, message, m_Line, m_Column);
    }

    // ── Whitespace & Comments ──
    void CQSLexer::SkipWhitespace()
    {
        while (!IsAtEnd())
        {
            char c = Peek();
            switch (c)
            {
                case ' ':
                case '\r':
                case '\t':
                    Advance();
                    break;

                case '\n':
                    m_Line++;
                    m_Column = 0;
                    Advance();
                    break;

                case '/':
                    if (PeekNext() == '/')
                    {
                        // Tek satır yorum: // ...
                        while (!IsAtEnd() && Peek() != '\n')
                            Advance();
                    }
                    else if (PeekNext() == '*')
                    {
                        // Blok yorum: /* ... */
                        Advance(); // '/'
                        Advance(); // '*'
                        while (!IsAtEnd())
                        {
                            if (Peek() == '\n')
                            {
                                m_Line++;
                                m_Column = 0;
                            }
                            if (Peek() == '*' && PeekNext() == '/')
                            {
                                Advance(); // '*'
                                Advance(); // '/'
                                break;
                            }
                            Advance();
                        }
                    }
                    else
                    {
                        return;
                    }
                    break;

                default:
                    return;
            }
        }
    }

    // ── Main Scanner ──
    Token CQSLexer::ScanToken()
    {
        SkipWhitespace();
        m_Start = m_Current;

        if (IsAtEnd()) return MakeToken(TokenType::EndOfFile);

        char c = Advance();

        // Identifier veya keyword
        if (IsAlpha(c)) return ScanIdentifier();

        // Sayı
        if (IsDigit(c)) return ScanNumber();

        switch (c)
        {
            // Single-character
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
            case '^': return MakeToken(TokenType::Caret);

            // Dot / DotDot
            case '.':
                if (Match('.')) return MakeToken(TokenType::DotDot);
                return MakeToken(TokenType::Dot);

            // Plus / PlusPlus / PlusEqual
            case '+':
                if (Match('+')) return MakeToken(TokenType::PlusPlus);
                if (Match('=')) return MakeToken(TokenType::PlusEqual);
                return MakeToken(TokenType::Plus);

            // Minus / MinusMinus / MinusEqual / Arrow
            case '-':
                if (Match('-')) return MakeToken(TokenType::MinusMinus);
                if (Match('=')) return MakeToken(TokenType::MinusEqual);
                if (Match('>')) return MakeToken(TokenType::Arrow);
                return MakeToken(TokenType::Minus);

            // Star / StarEqual
            case '*':
                if (Match('=')) return MakeToken(TokenType::StarEqual);
                return MakeToken(TokenType::Star);

            // Slash / SlashEqual (yorumlar SkipWhitespace'de halledildi)
            case '/':
                if (Match('=')) return MakeToken(TokenType::SlashEqual);
                return MakeToken(TokenType::Slash);

            // Percent / PercentEqual
            case '%':
                if (Match('=')) return MakeToken(TokenType::PercentEqual);
                return MakeToken(TokenType::Percent);

            // Equal / EqualEqual / FatArrow
            case '=':
                if (Match('=')) return MakeToken(TokenType::EqualEqual);
                if (Match('>')) return MakeToken(TokenType::FatArrow);
                return MakeToken(TokenType::Equal);

            // Bang / BangEqual
            case '!':
                if (Match('=')) return MakeToken(TokenType::BangEqual);
                return MakeToken(TokenType::Bang);

            // Less / LessEqual / LessLess
            case '<':
                if (Match('=')) return MakeToken(TokenType::LessEqual);
                if (Match('<')) return MakeToken(TokenType::LessLess);
                return MakeToken(TokenType::Less);

            // Greater / GreaterEqual / GreaterGreater
            case '>':
                if (Match('=')) return MakeToken(TokenType::GreaterEqual);
                if (Match('>')) return MakeToken(TokenType::GreaterGreater);
                return MakeToken(TokenType::Greater);

            // Amp / AmpAmp
            case '&':
                if (Match('&')) return MakeToken(TokenType::AmpAmp);
                return MakeToken(TokenType::Amp);

            // Pipe / PipePipe
            case '|':
                if (Match('|')) return MakeToken(TokenType::PipePipe);
                return MakeToken(TokenType::Pipe);

            // Question: ?. / ??
            case '?':
                if (Match('.')) return MakeToken(TokenType::QuestionDot);
                if (Match('?')) return MakeToken(TokenType::QuestionQuestion);
                return MakeToken(TokenType::Question);

            // Dollar (string interpolation prefix)
            case '$':
                if (Peek() == '"') return ScanInterpolatedString();
                return MakeToken(TokenType::Dollar);

            // String literal
            case '"': return ScanString();

            default:
                return ErrorToken("Unexpected character '" + std::string(1, c) + "'");
        }
    }

    // ── String Literal ──
    Token CQSLexer::ScanString()
    {
        std::string result = "";
        while (!IsAtEnd() && Peek() != '"')
        {
            char c = Peek();
            if (c == '\n')
            {
                m_Line++;
                m_Column = 0;
            }
            
            if (c == '\\')
            {
                Advance(); // Skip '\'
                if (IsAtEnd()) return ErrorToken("Unterminated string after escape");
                char escape = Peek();
                switch (escape) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    default: result += escape; break; // if unknown, just keep the char
                }
            }
            else
            {
                result += c;
            }
            Advance();
        }

        if (IsAtEnd())
        {
            return ErrorToken("Unterminated string");
        }

        Advance(); // Kapanış tırnak

        Token token = MakeToken(TokenType::StringLiteral);
        token.Lexeme = result;
        return token;
    }

    // ── Interpolated String: $"Hello {name}" ──
    Token CQSLexer::ScanInterpolatedString()
    {
        Advance(); // '"' karakterini atla

        std::string result = "";
        while (!IsAtEnd() && Peek() != '"')
        {
            char c = Peek();
            if (c == '\n')
            {
                m_Line++;
                m_Column = 0;
            }
            
            if (c == '\\')
            {
                Advance(); // Skip '\'
                if (IsAtEnd()) return ErrorToken("Unterminated interpolated string after escape");
                char escape = Peek();
                switch (escape) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    default: result += escape; break;
                }
            }
            else
            {
                result += c;
            }
            Advance();
        }

        if (IsAtEnd())
        {
            return ErrorToken("Unterminated interpolated string");
        }

        Advance(); // Kapanış tırnak

        Token token = MakeToken(TokenType::StringLiteral);
        token.Lexeme = result;
        return token;
    }

    // ── Number Literal ──
    Token CQSLexer::ScanNumber()
    {
        bool isFloat = false;

        while (!IsAtEnd() && IsDigit(Peek()))
            Advance();

        // Ondalık kısım
        if (Peek() == '.' && IsDigit(PeekNext()))
        {
            isFloat = true;
            Advance(); // '.'
            while (!IsAtEnd() && IsDigit(Peek()))
                Advance();
        }

        // 'f' suffix (3.14f)
        if (Peek() == 'f' || Peek() == 'F')
        {
            isFloat = true;
            Advance();
        }

        Token token = MakeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral);

        if (isFloat)
        {
            std::string numStr = token.Lexeme;
            // 'f' suffix'i kaldır
            if (!numStr.empty() && (numStr.back() == 'f' || numStr.back() == 'F'))
                numStr.pop_back();
            token.FloatValue = std::stod(numStr);
        }
        else
        {
            token.IntValue = std::stoll(token.Lexeme);
        }

        return token;
    }

    // ── Identifier / Keyword ──
    Token CQSLexer::ScanIdentifier()
    {
        while (!IsAtEnd() && IsAlphaNumeric(Peek()))
            Advance();

        std::string text = m_Source.substr(m_Start, m_Current - m_Start);

        // Keyword mi kontrol et
        auto it = s_Keywords.find(text);
        if (it != s_Keywords.end())
        {
            return MakeToken(it->second);
        }

        return MakeToken(TokenType::Identifier);
    }

    // ── Token Type to String (debug) ──
    const char* Token::TypeToString(TokenType type)
    {
        switch (type)
        {
            case TokenType::IntLiteral:       return "INT";
            case TokenType::FloatLiteral:     return "FLOAT";
            case TokenType::StringLiteral:    return "STRING";
            case TokenType::Identifier:       return "IDENTIFIER";
            case TokenType::Script:           return "SCRIPT";
            case TokenType::Class:            return "CLASS";
            case TokenType::Struct:           return "STRUCT";
            case TokenType::Enum:             return "ENUM";
            case TokenType::Interface:        return "INTERFACE";
            case TokenType::Func:             return "FUNC";
            case TokenType::Var:              return "VAR";
            case TokenType::If:               return "IF";
            case TokenType::Else:             return "ELSE";
            case TokenType::For:              return "FOR";
            case TokenType::Foreach:          return "FOREACH";
            case TokenType::While:            return "WHILE";
            case TokenType::Switch:           return "SWITCH";
            case TokenType::Case:             return "CASE";
            case TokenType::Default:          return "DEFAULT";
            case TokenType::Break:            return "BREAK";
            case TokenType::Continue:         return "CONTINUE";
            case TokenType::Return:           return "RETURN";
            case TokenType::Null:             return "NULL";
            case TokenType::True:             return "TRUE";
            case TokenType::False:            return "FALSE";
            case TokenType::This:             return "THIS";
            case TokenType::New:              return "NEW";
            case TokenType::In:               return "IN";
            case TokenType::As:              return "AS";
            case TokenType::Is:              return "IS";
            case TokenType::Public:           return "PUBLIC";
            case TokenType::Private:          return "PRIVATE";
            case TokenType::Static:           return "STATIC";
            case TokenType::Event:            return "EVENT";
            case TokenType::Yield:            return "YIELD";
            case TokenType::Try:              return "TRY";
            case TokenType::Catch:            return "CATCH";
            case TokenType::Finally:          return "FINALLY";
            case TokenType::Throw:            return "THROW";
            case TokenType::Using:            return "USING";
            case TokenType::Namespace:        return "NAMESPACE";
            case TokenType::Plus:             return "PLUS";
            case TokenType::Minus:            return "MINUS";
            case TokenType::Star:             return "STAR";
            case TokenType::Slash:            return "SLASH";
            case TokenType::Percent:          return "PERCENT";
            case TokenType::Equal:            return "EQUAL";
            case TokenType::PlusEqual:        return "PLUS_EQUAL";
            case TokenType::MinusEqual:       return "MINUS_EQUAL";
            case TokenType::StarEqual:        return "STAR_EQUAL";
            case TokenType::SlashEqual:       return "SLASH_EQUAL";
            case TokenType::PercentEqual:     return "PERCENT_EQUAL";
            case TokenType::PlusPlus:         return "PLUS_PLUS";
            case TokenType::MinusMinus:       return "MINUS_MINUS";
            case TokenType::EqualEqual:       return "EQUAL_EQUAL";
            case TokenType::BangEqual:        return "BANG_EQUAL";
            case TokenType::Less:             return "LESS";
            case TokenType::Greater:          return "GREATER";
            case TokenType::LessEqual:        return "LESS_EQUAL";
            case TokenType::GreaterEqual:     return "GREATER_EQUAL";
            case TokenType::AmpAmp:           return "AMP_AMP";
            case TokenType::PipePipe:         return "PIPE_PIPE";
            case TokenType::Bang:             return "BANG";
            case TokenType::Amp:              return "AMP";
            case TokenType::Pipe:             return "PIPE";
            case TokenType::Caret:            return "CARET";
            case TokenType::Tilde:            return "TILDE";
            case TokenType::LessLess:         return "LESS_LESS";
            case TokenType::GreaterGreater:   return "GREATER_GREATER";
            case TokenType::Arrow:            return "ARROW";
            case TokenType::FatArrow:         return "FAT_ARROW";
            case TokenType::QuestionDot:      return "QUESTION_DOT";
            case TokenType::QuestionQuestion: return "QUESTION_QUESTION";
            case TokenType::Question:         return "QUESTION";
            case TokenType::LeftParen:        return "LEFT_PAREN";
            case TokenType::RightParen:       return "RIGHT_PAREN";
            case TokenType::LeftBrace:        return "LEFT_BRACE";
            case TokenType::RightBrace:       return "RIGHT_BRACE";
            case TokenType::LeftBracket:      return "LEFT_BRACKET";
            case TokenType::RightBracket:     return "RIGHT_BRACKET";
            case TokenType::Semicolon:        return "SEMICOLON";
            case TokenType::Colon:            return "COLON";
            case TokenType::Comma:            return "COMMA";
            case TokenType::Dot:              return "DOT";
            case TokenType::DotDot:           return "DOT_DOT";
            case TokenType::Dollar:           return "DOLLAR";
            case TokenType::EndOfFile:        return "EOF";
            case TokenType::Error:            return "ERROR";
            default:                          return "UNKNOWN";
        }
    }
}
