#include "LuaQAst.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

// LuaQ sözcük çözümleyici + ayrıştırıcı. Luau tür söz dizimi (açıklamalar, type, ::) okunur ve atılır.
namespace Conqueror::LuaQ::Detail
{
    namespace
    {
        [[noreturn]] void Fail(int line, int col, const std::string& message)
        {
            throw CompileError{ line, col, message };
        }

        // ═══════════════════════════════════════════════ Sözcük çözümleyici
        class Lexer
        {
        public:
            explicit Lexer(const std::string& source) : m_S(source) {}

            std::vector<Token> Run()
            {
                std::vector<Token> out;
                if (m_S.rfind("#!", 0) == 0)
                    while (m_I < m_S.size() && m_S[m_I] != '\n')
                        ++m_I;
                for (;;)
                {
                    SkipSpaceAndComments();
                    Token t;
                    t.Line = m_Line;
                    t.Col = Col();
                    if (m_I >= m_S.size())
                    {
                        t.Type = Tok::Eof;
                        out.push_back(t);
                        return out;
                    }
                    const char c = m_S[m_I];
                    if (std::isalpha((unsigned char)c) || c == '_')
                    {
                        size_t b = m_I;
                        while (m_I < m_S.size() && (std::isalnum((unsigned char)m_S[m_I]) || m_S[m_I] == '_'))
                            ++m_I;
                        t.Type = Tok::Name;
                        t.Text = m_S.substr(b, m_I - b);
                    }
                    else if (std::isdigit((unsigned char)c) || (c == '.' && m_I + 1 < m_S.size() && std::isdigit((unsigned char)m_S[m_I + 1])))
                    {
                        t.Type = Tok::Number;
                        t.Text = ReadNumber();
                    }
                    else if (c == '"' || c == '\'')
                    {
                        t.Type = Tok::String;
                        t.Text = ReadShortString(c);
                    }
                    else if (c == '[' && LongBracketLevel() >= 0)
                    {
                        t.Type = Tok::String;
                        t.Text = ReadLongBracket();
                    }
                    else if (c == '`')
                    {
                        t.Type = Tok::Interp;
                        t.Text = ReadInterp();
                    }
                    else
                    {
                        t.Type = Tok::Op;
                        t.Text = ReadOp();
                    }
                    out.push_back(std::move(t));
                }
            }

        private:
            int Col() const { return (int)(m_I - m_LineStart) + 1; }

            void NewLine(size_t at)
            {
                ++m_Line;
                m_LineStart = at + 1;
            }

            void SkipSpaceAndComments()
            {
                while (m_I < m_S.size())
                {
                    const char c = m_S[m_I];
                    if (c == '\n') { NewLine(m_I); ++m_I; continue; }
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { ++m_I; continue; }
                    if (c == '-' && m_I + 1 < m_S.size() && m_S[m_I + 1] == '-')
                    {
                        m_I += 2;
                        if (m_I < m_S.size() && m_S[m_I] == '[' && LongBracketLevel() >= 0)
                        {
                            ReadLongBracket();
                            continue;
                        }
                        while (m_I < m_S.size() && m_S[m_I] != '\n')
                            ++m_I;
                        continue;
                    }
                    break;
                }
            }

            // m_I '[' üzerinde: [=*[ ise seviye, değilse -1
            int LongBracketLevel() const
            {
                size_t j = m_I + 1;
                int level = 0;
                while (j < m_S.size() && m_S[j] == '=') { ++level; ++j; }
                return (j < m_S.size() && m_S[j] == '[') ? level : -1;
            }

            std::string ReadLongBracket()
            {
                const int line = m_Line, col = Col();
                const size_t b = m_I;
                const int level = LongBracketLevel();
                m_I += (size_t)level + 2;
                for (;;)
                {
                    if (m_I >= m_S.size())
                        Fail(line, col, "unfinished long string or comment");
                    const char c = m_S[m_I];
                    if (c == '\n') NewLine(m_I);
                    if (c == ']')
                    {
                        size_t j = m_I + 1;
                        int n = 0;
                        while (j < m_S.size() && m_S[j] == '=') { ++n; ++j; }
                        if (n == level && j < m_S.size() && m_S[j] == ']')
                        {
                            m_I = j + 1;
                            return m_S.substr(b, m_I - b);
                        }
                    }
                    ++m_I;
                }
            }

            std::string ReadShortString(char quote)
            {
                const int line = m_Line, col = Col();
                const size_t b = m_I++;
                for (;;)
                {
                    if (m_I >= m_S.size())
                        Fail(line, col, "unfinished string");
                    const char c = m_S[m_I];
                    if (c == '\n')
                        Fail(line, col, "unfinished string");
                    if (c == '\\')
                    {
                        ++m_I;
                        if (m_I < m_S.size())
                        {
                            if (m_S[m_I] == '\n')
                                NewLine(m_I);
                            else if (m_S[m_I] == 'z')
                            {
                                // \z: sonraki boşlukları (satır sonları dahil) atla
                                ++m_I;
                                while (m_I < m_S.size() && std::isspace((unsigned char)m_S[m_I]))
                                {
                                    if (m_S[m_I] == '\n') NewLine(m_I);
                                    ++m_I;
                                }
                                continue;
                            }
                            ++m_I;
                        }
                        continue;
                    }
                    ++m_I;
                    if (c == quote)
                        return m_S.substr(b, m_I - b);
                }
            }

            std::string ReadInterp()
            {
                const int line = m_Line, col = Col();
                const size_t b = m_I++;
                int depth = 0;
                for (;;)
                {
                    if (m_I >= m_S.size())
                        Fail(line, col, "unfinished interpolated string");
                    const char c = m_S[m_I];
                    if (c == '\\') { m_I += 2; continue; }
                    if (c == '\n')
                    {
                        if (depth == 0) Fail(line, col, "unfinished interpolated string");
                        NewLine(m_I);
                    }
                    if (depth > 0 && (c == '"' || c == '\''))
                    {
                        ReadShortString(c);
                        continue;
                    }
                    if (c == '{') ++depth;
                    else if (c == '}' && depth > 0) --depth;
                    else if (c == '`' && depth == 0)
                    {
                        ++m_I;
                        return m_S.substr(b, m_I - b);
                    }
                    ++m_I;
                }
            }

            std::string ReadNumber()
            {
                const size_t b = m_I;
                const bool hex = m_S[m_I] == '0' && m_I + 1 < m_S.size() && (m_S[m_I + 1] == 'x' || m_S[m_I + 1] == 'X');
                const bool bin = m_S[m_I] == '0' && m_I + 1 < m_S.size() && (m_S[m_I + 1] == 'b' || m_S[m_I + 1] == 'B');
                if (hex || bin) m_I += 2;
                bool hexFloat = false;
                while (m_I < m_S.size())
                {
                    const char c = m_S[m_I];
                    const bool expo = hex ? (c == 'p' || c == 'P') : (!bin && (c == 'e' || c == 'E'));
                    if (expo)
                    {
                        hexFloat = hexFloat || hex;
                        ++m_I;
                        if (m_I < m_S.size() && (m_S[m_I] == '+' || m_S[m_I] == '-'))
                            ++m_I;
                        continue;
                    }
                    if (c == '.')
                    {
                        if (m_I + 1 < m_S.size() && m_S[m_I + 1] == '.')
                            break;   // 1..2 birleştirme
                        hexFloat = hexFloat || hex;
                        ++m_I;
                        continue;
                    }
                    if (std::isalnum((unsigned char)c) || c == '_') { ++m_I; continue; }
                    break;
                }
                std::string text = m_S.substr(b, m_I - b);
                if (hexFloat)
                {
                    // Lua 5.4 onaltılık kayan sayı (0x1p4, 0xA.8p0): Luau'da yok -> ondalık
                    std::string clean;
                    for (char c : text) if (c != '_') clean += c;
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.17g", std::strtod(clean.c_str(), nullptr));
                    text = buf;
                }
                return text;
            }

            std::string ReadOp()
            {
                static const char* kOps[] = {
                    "...", "..=", "//=", "<<=", ">>=", "??=",
                    "..", "//", "<<", ">>", "<=", ">=", "==", "~=", "!=", "=>", "->", "::", "&&", "||",
                    "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "??", "?.",
                    "+", "-", "*", "/", "%", "^", "#", "&", "~", "|", "<", ">", "=", "(", ")", "{", "}", "[", "]",
                    ";", ":", ",", ".", "?", "!", "@",
                };
                for (const char* op : kOps)
                {
                    const size_t n = std::strlen(op);
                    if (m_S.compare(m_I, n, op) == 0)
                    {
                        m_I += n;
                        return op;
                    }
                }
                Fail(m_Line, Col(), std::string("unexpected character '") + m_S[m_I] + "'");
            }

            const std::string& m_S;
            size_t m_I = 0, m_LineStart = 0;
            int m_Line = 1;
        };

        // ═══════════════════════════════════════════════ Ayrıştırıcı
        const std::unordered_set<std::string>& Reserved()
        {
            static const std::unordered_set<std::string> s = {
                "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local",
                "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
            };
            return s;
        }

        struct Prio { int Left, Right; };

        bool BinaryPrio(const Token& t, Prio& out)
        {
            if (t.Type == Tok::Name)
            {
                if (t.Text == "or") { out = { 2, 2 }; return true; }
                if (t.Text == "and") { out = { 3, 3 }; return true; }
                return false;
            }
            if (t.Type != Tok::Op)
                return false;
            const std::string& o = t.Text;
            if (o == "??") out = { 1, 1 };
            else if (o == "||") out = { 2, 2 };
            else if (o == "&&") out = { 3, 3 };
            else if (o == "<" || o == ">" || o == "<=" || o == ">=" || o == "~=" || o == "!=" || o == "==") out = { 4, 4 };
            else if (o == "|") out = { 5, 5 };
            else if (o == "~") out = { 6, 6 };
            else if (o == "&") out = { 7, 7 };
            else if (o == "<<" || o == ">>") out = { 8, 8 };
            else if (o == "..") out = { 10, 9 };
            else if (o == "+" || o == "-") out = { 11, 11 };
            else if (o == "*" || o == "/" || o == "//" || o == "%") out = { 12, 12 };
            else if (o == "^") out = { 15, 14 };
            else return false;
            return true;
        }
        constexpr int kUnaryPrio = 13;

        class Parser
        {
        public:
            explicit Parser(const std::vector<Token>& tokens) : m_T(tokens) {}

            Chunk Run()
            {
                Chunk chunk;
                chunk.Body = ParseBlock();
                if (Peek().Type != Tok::Eof)
                    Error(Peek(), "'" + Peek().Text + "' unexpected here (expected end of file)");
                return chunk;
            }

        private:
            // ─── Sözcük yardımcıları
            const Token& Peek(size_t k = 0) const
            {
                const size_t i = std::min(m_P + k, m_T.size() - 1);
                return m_T[i];
            }
            const Token& Next() { const Token& t = m_T[std::min(m_P, m_T.size() - 1)]; if (m_P < m_T.size() - 1) ++m_P; return t; }
            bool IsOp(const char* op, size_t k = 0) const { const Token& t = Peek(k); return t.Type == Tok::Op && t.Text == op; }
            bool IsName(const char* name, size_t k = 0) const { const Token& t = Peek(k); return t.Type == Tok::Name && t.Text == name; }
            bool AcceptOp(const char* op) { if (IsOp(op)) { Next(); return true; } return false; }
            bool AcceptName(const char* name) { if (IsName(name)) { Next(); return true; } return false; }

            [[noreturn]] void Error(const Token& t, const std::string& message) { Fail(t.Line, t.Col, message); }

            void ExpectOp(const char* op, const char* context = nullptr)
            {
                if (!AcceptOp(op))
                    Error(Peek(), std::string("expected '") + op + "'" + (context ? std::string(" ") + context : "") + " near '" + Describe(Peek()) + "'");
            }
            void ExpectName(const char* keyword, const char* context = nullptr)
            {
                if (!AcceptName(keyword))
                    Error(Peek(), std::string("expected '") + keyword + "'" + (context ? std::string(" ") + context : "") + " near '" + Describe(Peek()) + "'");
            }
            void ExpectBlockEnd(const char* what, const Token& opener)
            {
                if (!AcceptName("end"))
                    Error(Peek(), std::string("expected 'end' to close '") + what + "' at line " + std::to_string(opener.Line) + " near '" + Describe(Peek()) + "'");
            }
            std::string Identifier(const char* context)
            {
                const Token& t = Peek();
                if (t.Type != Tok::Name || Reserved().count(t.Text))
                    Error(t, std::string("expected identifier ") + context + " near '" + Describe(t) + "'");
                Next();
                return t.Text;
            }
            static std::string Describe(const Token& t) { return t.Type == Tok::Eof ? "<eof>" : t.Text; }

            bool BlockFollows() const
            {
                const Token& t = Peek();
                if (t.Type == Tok::Eof) return true;
                if (t.Type != Tok::Name) return false;
                return t.Text == "end" || t.Text == "else" || t.Text == "elseif" || t.Text == "until" || (m_InMatch && t.Text == "case");
            }

            // ─── Türler (Luau): okunur, atılır
            void SkipBalanced(const char* open, const char* close)
            {
                int depth = 0;
                do
                {
                    if (Peek().Type == Tok::Eof)
                        Error(Peek(), std::string("unbalanced '") + open + "'");
                    if (IsOp(open)) ++depth;
                    else if (IsOp(close)) --depth;
                    // Tür içinde '>>' iki kapanış olabilir
                    else if (std::string(close) == ">" && IsOp(">>")) depth -= 2;
                    Next();
                } while (depth > 0);
            }

            void SkipSimpleType()
            {
                if (IsOp("{")) { SkipBalanced("{", "}"); }
                else if (IsOp("("))
                {
                    SkipBalanced("(", ")");
                    if (AcceptOp("->")) SkipType();
                }
                else if (IsOp("<"))
                {
                    // genel fonksiyon türü: <T>(T) -> T
                    SkipBalanced("<", ">");
                    if (IsOp("(")) { SkipBalanced("(", ")"); if (AcceptOp("->")) SkipType(); }
                }
                else if (IsOp("..."))
                {
                    Next();
                    if (!IsOp(",") && !IsOp(")") && !IsOp("=") && !IsOp(">")) SkipSimpleType();
                }
                else if (IsName("typeof") && IsOp("(", 1)) { Next(); SkipBalanced("(", ")"); }
                else if (Peek().Type == Tok::String || IsName("nil") || IsName("true") || IsName("false")) { Next(); }
                else if (Peek().Type == Tok::Name)
                {
                    Next();
                    while (IsOp(".") && Peek(1).Type == Tok::Name) { Next(); Next(); }
                    if (IsOp("<")) SkipBalanced("<", ">");
                }
                else
                    Error(Peek(), "expected type near '" + Describe(Peek()) + "'");
                while (AcceptOp("?")) {}
            }

            void SkipType()
            {
                AcceptOp("|");
                AcceptOp("&");
                SkipSimpleType();
                while (IsOp("|") || IsOp("&"))
                {
                    Next();
                    SkipSimpleType();
                }
            }

            // type Ad<T> = ... / export type / type function
            void SkipTypeAlias()
            {
                AcceptName("export");
                ExpectName("type");
                if (IsName("function"))
                {
                    // type function: gövdeyi normal fonksiyon gibi oku, at
                    Next();
                    Identifier("for type function");
                    ParseFuncBody(false, false);
                    return;
                }
                Identifier("for type alias");
                if (IsOp("<")) SkipBalanced("<", ">");
                ExpectOp("=", "in type alias");
                SkipType();
            }

            // ─── Bloklar ve deyimler
            std::unique_ptr<Block> ParseBlock()
            {
                auto block = std::make_unique<Block>();
                while (!BlockFollows())
                {
                    if (IsName("return"))
                    {
                        block->Stats.push_back(ParseReturn());
                        break;
                    }
                    StatP s = ParseStatement();
                    if (s && s->Kind != SK::Empty)
                        block->Stats.push_back(std::move(s));
                }
                block->EndLine = Peek().Line;
                return block;
            }

            StatP MakeStat(SK kind, const Token& at)
            {
                auto s = std::make_unique<Stat>();
                s->Kind = kind;
                s->Line = at.Line;
                s->Col = at.Col;
                return s;
            }

            StatP ParseReturn()
            {
                const Token& t = Next();
                auto s = MakeStat(SK::Return, t);
                if (!BlockFollows() && !IsOp(";"))
                    s->Values = ParseExprList();
                AcceptOp(";");
                if (!BlockFollows())
                    Error(Peek(), "'return' must be the last statement in a block (near '" + Describe(Peek()) + "')");
                return s;
            }

            StatP ParseStatement()
            {
                const Token& t = Peek();
                if (t.Type == Tok::Op)
                {
                    if (t.Text == ";") { Next(); return MakeStat(SK::Empty, t); }
                    if (t.Text == "::")
                    {
                        Next();
                        auto s = MakeStat(SK::Label, t);
                        s->Op = Identifier("for label");
                        ExpectOp("::", "after label");
                        return s;
                    }
                    if (t.Text == "@")
                    {
                        std::vector<std::string> attributes = ParseAttributes();
                        StatP s = ParseStatement();
                        if (!s || (s->Kind != SK::Function && s->Kind != SK::LocalFunction))
                            Error(t, "attributes must be followed by a function declaration");
                        s->Attributes = std::move(attributes);
                        return s;
                    }
                }
                if (t.Type == Tok::Name)
                {
                    const std::string& k = t.Text;
                    if (k == "if") return ParseIf();
                    if (k == "while") return ParseWhile();
                    if (k == "do")
                    {
                        Next();
                        auto s = MakeStat(SK::Do, t);
                        s->Body = ParseBlock();
                        ExpectBlockEnd("do", t);
                        return s;
                    }
                    if (k == "for") return ParseFor();
                    if (k == "repeat") return ParseRepeat();
                    if (k == "function") return ParseFunctionStat(false);
                    if (k == "local") return ParseLocal();
                    if (k == "break") { Next(); return MakeStat(SK::Break, t); }
                    if (k == "goto" && Peek(1).Type == Tok::Name && !Reserved().count(Peek(1).Text))
                    {
                        Next();
                        auto s = MakeStat(SK::Goto, t);
                        s->Op = Identifier("after goto");
                        return s;
                    }
                    const bool typeAlias = k == "type" && Peek(1).Type == Tok::Name
                                           && (IsName("function", 1) || (!Reserved().count(Peek(1).Text) && (IsOp("=", 2) || IsOp("<", 2))));
                    if (typeAlias || (k == "export" && IsName("type", 1)))
                    {
                        SkipTypeAlias();
                        return MakeStat(SK::Empty, t);
                    }
                    if (k == "class" && Peek(1).Type == Tok::Name && !Reserved().count(Peek(1).Text))
                        return ParseClass();
                    if (k == "enum" && Peek(1).Type == Tok::Name && IsOp("{", 2))
                        return ParseEnum();
                    if (k == "const" && Peek(1).Type == Tok::Name && (IsOp("=", 2) || IsOp(":", 2)))
                    {
                        Next();
                        auto s = MakeStat(SK::Local, t);
                        s->Names.push_back(Identifier("after const"));
                        s->Attribs.push_back("const");
                        if (AcceptOp(":")) SkipType();
                        ExpectOp("=", "in const declaration");
                        s->Values = ParseExprList();
                        return s;
                    }
                    if (k == "async" && IsName("function", 1))
                    {
                        Next();
                        return ParseFunctionStat(true);
                    }
                    if (k == "defer" && DeferFollows())
                    {
                        Next();
                        auto s = MakeStat(SK::Defer, t);
                        s->Inner = ParseStatement();
                        if (!s->Inner || s->Inner->Kind == SK::Return || s->Inner->Kind == SK::Goto || s->Inner->Kind == SK::Break
                            || s->Inner->Kind == SK::Continue || s->Inner->Kind == SK::Label || s->Inner->Kind == SK::Local)
                            Error(t, "defer needs a statement such as a call or a do ... end block");
                        return s;
                    }
                    if (k == "match")
                    {
                        if (StatP m = TryParseMatch())
                            return m;
                    }
                    if (k == "await" && InAsync())
                    {
                        // await deyimi: await 0.5 / await gorev
                        auto s = MakeStat(SK::CallStat, t);
                        s->Values.push_back(ParseExpr());
                        return s;
                    }
                }
                return ParseExprStatement();
            }

            // defer(...) / defer "x" / defer {...} / defer = ... Lua çağrısı ya da atamasıdır; deyim ancak bir ad / anahtar sözcükle başlar
            bool DeferFollows() const
            {
                const Token& n = Peek(1);
                if (n.Type != Tok::Name)
                    return false;
                return !Reserved().count(n.Text) || n.Text == "do" || n.Text == "if" || n.Text == "for" || n.Text == "while";
            }

            std::vector<std::string> ParseAttributes()
            {
                std::vector<std::string> out;
                while (IsOp("@"))
                {
                    Next();
                    if (IsOp("["))
                    {
                        // @[attr(args)]: ham olarak geçir
                        const size_t b = m_P;
                        SkipBalanced("[", "]");
                        std::string raw = "@";
                        for (size_t i = b; i < m_P; ++i) raw += m_T[i].Text + (i + 1 < m_P ? " " : "");
                        out.push_back(raw);
                    }
                    else
                        out.push_back("@" + Identifier("after '@'"));
                }
                return out;
            }

            StatP ParseIf()
            {
                const Token& t = Next();
                auto s = MakeStat(SK::If, t);
                s->Values.push_back(ParseExpr());
                ExpectName("then", "after if condition");
                s->Blocks.push_back(ParseBlock());
                while (IsName("elseif"))
                {
                    Next();
                    s->Values.push_back(ParseExpr());
                    ExpectName("then", "after elseif condition");
                    s->Blocks.push_back(ParseBlock());
                }
                if (AcceptName("else"))
                    s->Else = ParseBlock();
                ExpectBlockEnd("if", t);
                return s;
            }

            StatP ParseWhile()
            {
                const Token& t = Next();
                auto s = MakeStat(SK::While, t);
                s->Cond = ParseExpr();
                ExpectName("do", "after while condition");
                ++m_LoopDepth;
                s->Body = ParseBlock();
                --m_LoopDepth;
                ExpectBlockEnd("while", t);
                return s;
            }

            StatP ParseRepeat()
            {
                const Token& t = Next();
                auto s = MakeStat(SK::Repeat, t);
                ++m_LoopDepth;
                s->Body = ParseBlock();
                --m_LoopDepth;
                ExpectName("until", "to close 'repeat'");
                s->Cond = ParseExpr();
                return s;
            }

            StatP ParseFor()
            {
                const Token& t = Next();
                std::vector<std::string> names;
                names.push_back(Identifier("in for loop"));
                if (AcceptOp(":")) SkipType();
                if (AcceptOp("="))
                {
                    auto s = MakeStat(SK::NumFor, t);
                    s->Names = names;
                    s->Values.push_back(ParseExpr());
                    ExpectOp(",", "in numeric for");
                    s->Values.push_back(ParseExpr());
                    if (AcceptOp(","))
                        s->Values.push_back(ParseExpr());
                    ExpectName("do", "in for loop");
                    ++m_LoopDepth;
                    s->Body = ParseBlock();
                    --m_LoopDepth;
                    ExpectBlockEnd("for", t);
                    return s;
                }
                while (AcceptOp(","))
                {
                    names.push_back(Identifier("in for loop"));
                    if (AcceptOp(":")) SkipType();
                }
                ExpectName("in", "in for loop");
                auto s = MakeStat(SK::GenFor, t);
                s->Names = names;
                s->Values = ParseExprList();
                ExpectName("do", "in for loop");
                ++m_LoopDepth;
                s->Body = ParseBlock();
                --m_LoopDepth;
                ExpectBlockEnd("for", t);
                return s;
            }

            StatP ParseFunctionStat(bool async)
            {
                const Token& t = Next();   // function
                auto s = MakeStat(SK::Function, t);
                s->FuncPath.push_back(Identifier("for function name"));
                while (AcceptOp("."))
                    s->FuncPath.push_back(Identifier("in function name"));
                if (AcceptOp(":"))
                {
                    s->FuncPath.push_back(Identifier("for method name"));
                    s->FuncMethod = true;
                }
                s->Func = ParseFuncBody(s->FuncMethod, async);
                return s;
            }

            StatP ParseLocal()
            {
                const Token& t = Next();   // local
                const bool async = IsName("async") && IsName("function", 1);
                if (async)
                    Next();
                if (IsName("function"))
                {
                    Next();
                    auto s = MakeStat(SK::LocalFunction, t);
                    s->Names.push_back(Identifier("for local function"));
                    s->Func = ParseFuncBody(false, async);
                    return s;
                }
                if (async)
                    Error(Peek(), "expected 'function' after 'local async'");
                if (IsOp("{") || IsOp("["))
                {
                    // LuaQ çözümleme: local {a, b} = t  /  local [x, y] = t
                    auto s = MakeStat(SK::Destructure, t);
                    s->ArrayPattern = IsOp("[");
                    const char* close = s->ArrayPattern ? "]" : "}";
                    Next();
                    do
                    {
                        s->Names.push_back(Identifier("in destructuring"));
                        if (AcceptOp(":")) SkipType();
                    } while (AcceptOp(","));
                    ExpectOp(close, "to close destructuring pattern");
                    ExpectOp("=", "after destructuring pattern");
                    s->Values.push_back(ParseExpr());
                    return s;
                }
                auto s = MakeStat(SK::Local, t);
                do
                {
                    s->Names.push_back(Identifier("in local declaration"));
                    if (AcceptOp(":")) SkipType();
                    std::string attrib;
                    if (IsOp("<") && Peek(1).Type == Tok::Name && IsOp(">", 2))
                    {
                        Next();
                        attrib = Next().Text;
                        Next();
                        if (attrib != "const" && attrib != "close")
                            Error(Peek(), "unknown attribute '" + attrib + "'");
                    }
                    s->Attribs.push_back(attrib);
                } while (AcceptOp(","));
                if (AcceptOp("="))
                    s->Values = ParseExprList();
                return s;
            }

            StatP ParseClass()
            {
                const Token& t = Next();   // class
                auto s = MakeStat(SK::Class, t);
                s->ClassName = Identifier("for class name");
                if (AcceptName("extends"))
                    s->Base = ParseSuffixedExpr();
                ++m_ClassDepth;
                while (!IsName("end"))
                {
                    if (Peek().Type == Tok::Eof)
                        Error(Peek(), "expected 'end' to close class '" + s->ClassName + "' at line " + std::to_string(t.Line));
                    if (AcceptOp(";")) continue;
                    ClassMember m;
                    m.Line = Peek().Line;
                    if (IsOp("@")) m.Attributes = ParseAttributes();
                    const bool isStatic = AcceptName("static");
                    const bool async = AcceptName("async");
                    if (AcceptName("function"))
                    {
                        m.K = isStatic ? ClassMember::StaticMethod : ClassMember::Method;
                        m.Name = Identifier("for method name");
                        m_InMethod.push_back(!isStatic);
                        m.Func = ParseFuncBody(!isStatic, async);
                        m_InMethod.pop_back();
                    }
                    else
                    {
                        if (async) Error(Peek(), "expected 'function' after 'async'");
                        m.K = isStatic ? ClassMember::StaticField : ClassMember::Field;
                        m.Name = Identifier("for class field");
                        if (AcceptOp(":")) SkipType();
                        if (AcceptOp("="))
                            m.Value = ParseExpr();
                    }
                    s->Members.push_back(std::move(m));
                }
                --m_ClassDepth;
                Next();   // end
                return s;
            }

            StatP ParseEnum()
            {
                const Token& t = Next();   // enum
                auto s = MakeStat(SK::Enum, t);
                s->ClassName = Identifier("for enum name");
                ExpectOp("{", "after enum name");
                while (!IsOp("}"))
                {
                    s->Names.push_back(Identifier("for enum member"));
                    s->EnumValues.push_back(AcceptOp("=") ? ParseExpr() : nullptr);
                    if (!AcceptOp(",") && !AcceptOp(";"))
                        break;
                }
                ExpectOp("}", "to close enum");
                return s;
            }

            StatP TryParseMatch()
            {
                // "match" bir fonksiyon adı da olabilir (Lua uyumu): konu ifadesinden sonra 'case' gelmiyorsa geri sar
                const size_t save = m_P;
                const Token& t = Peek();
                try
                {
                    Next();
                    ExprP subject = ParseExpr();
                    if (!IsName("case"))
                    {
                        m_P = save;
                        return nullptr;
                    }
                    auto s = MakeStat(SK::Match, t);
                    s->Cond = std::move(subject);
                    const bool prev = m_InMatch;
                    m_InMatch = true;
                    while (IsName("case"))
                    {
                        MatchCase c;
                        c.Line = Next().Line;
                        do
                        {
                            if (IsName("_") && (IsOp(",", 1) || IsName("then", 1) || IsName("if", 1)))
                            {
                                Next();
                                c.Wildcard = true;
                            }
                            else
                                c.Patterns.push_back(ParseExpr());
                        } while (AcceptOp(","));
                        if (AcceptName("if"))
                            c.Guard = ParseExpr();
                        ExpectName("then", "after case pattern");
                        c.Body = ParseBlock();
                        s->Cases.push_back(std::move(c));
                    }
                    m_InMatch = prev;
                    if (AcceptName("else"))
                        s->Else = ParseBlock();
                    ExpectBlockEnd("match", t);
                    return s;
                }
                catch (const CompileError&)
                {
                    if (m_P > save && IsName("case")) throw;
                    m_P = save;
                    return nullptr;
                }
            }

            StatP ParseExprStatement()
            {
                const Token& t = Peek();
                ExprP e = ParseSuffixedExpr();
                if (IsOp("=") || IsOp(","))
                {
                    auto s = MakeStat(SK::Assign, t);
                    CheckAssignable(e.get());
                    s->Targets.push_back(std::move(e));
                    while (AcceptOp(","))
                    {
                        ExprP target = ParseSuffixedExpr();
                        CheckAssignable(target.get());
                        s->Targets.push_back(std::move(target));
                    }
                    ExpectOp("=", "in assignment");
                    s->Values = ParseExprList();
                    return s;
                }
                static const char* kCompound[] = { "+=", "-=", "*=", "/=", "//=", "%=", "^=", "..=", "&=", "|=", "<<=", ">>=", "??=" };
                for (const char* op : kCompound)
                    if (IsOp(op))
                    {
                        Next();
                        CheckAssignable(e.get());
                        auto s = MakeStat(SK::Compound, t);
                        s->Op = op;
                        s->Targets.push_back(std::move(e));
                        s->Values.push_back(ParseExpr());
                        return s;
                    }
                if (e->Kind == EK::Name && e->Text == "continue")
                    return MakeStat(SK::Continue, t);
                if (e->Kind != EK::Call && e->Kind != EK::Method)
                    Error(t, "syntax error: expression is not a statement (near '" + Describe(Peek()) + "')");
                auto s = MakeStat(SK::CallStat, t);
                s->Values.push_back(std::move(e));
                return s;
            }

            void CheckAssignable(const Expr* e)
            {
                if ((e->Kind != EK::Name && e->Kind != EK::Field && e->Kind != EK::Index) || e->Optional)
                    Fail(e->Line, e->Col, "cannot assign to this expression");
            }

            // ─── Fonksiyonlar
            std::unique_ptr<FuncBody> ParseFuncBody(bool implicitSelf, bool async)
            {
                auto f = std::make_unique<FuncBody>();
                f->Line = Peek().Line;
                f->ImplicitSelf = implicitSelf;
                f->Async = async;
                if (IsOp("<")) SkipBalanced("<", ">");
                ExpectOp("(", "to start parameter list");
                ParseParams(*f, ")");
                if (AcceptOp(":")) SkipType();
                m_FuncAsync.push_back(async);
                const int savedLoop = m_LoopDepth;
                m_LoopDepth = 0;
                f->Body = ParseBlock();
                m_LoopDepth = savedLoop;
                m_FuncAsync.pop_back();
                const Token& end = Peek();
                if (!AcceptName("end"))
                    Error(end, "expected 'end' to close function at line " + std::to_string(f->Line) + " near '" + Describe(end) + "'");
                return f;
            }

            void ParseParams(FuncBody& f, const char* close)
            {
                if (AcceptOp(close))
                    return;
                for (;;)
                {
                    if (AcceptOp("..."))
                    {
                        f.Vararg = true;
                        if (AcceptOp(":")) SkipType();
                        ExpectOp(close, "after '...'");
                        return;
                    }
                    Param p;
                    p.Name = Identifier("for parameter");
                    if (AcceptOp(":")) SkipType();
                    if (AcceptOp("="))
                        p.Default = ParseExpr();
                    f.Params.push_back(std::move(p));
                    if (AcceptOp(","))
                        continue;
                    ExpectOp(close, "to close parameter list");
                    return;
                }
            }

            // Ok fonksiyonu: x => ifade, (a, b) => ifade, (a) => do ... end
            bool ArrowAhead() const
            {
                if (Peek().Type == Tok::Name && !Reserved().count(Peek().Text) && IsOp("=>", 1))
                    return true;
                if (!IsOp("("))
                    return false;
                int depth = 0;
                for (size_t k = 0; m_P + k < m_T.size(); ++k)
                {
                    const Token& t = Peek(k);
                    if (t.Type == Tok::Eof) return false;
                    if (t.Type == Tok::Op && (t.Text == "(" || t.Text == "{" || t.Text == "[")) ++depth;
                    else if (t.Type == Tok::Op && (t.Text == ")" || t.Text == "}" || t.Text == "]"))
                    {
                        if (--depth == 0)
                            return IsOp("=>", k + 1);
                    }
                }
                return false;
            }

            ExprP ParseArrow()
            {
                const Token& t = Peek();
                auto e = MakeExpr(EK::Function, t);
                e->Func = std::make_unique<FuncBody>();
                e->Func->Line = t.Line;
                if (t.Type == Tok::Name)
                {
                    Param p;
                    p.Name = Next().Text;
                    e->Func->Params.push_back(std::move(p));
                }
                else
                {
                    Next();
                    ParseParams(*e->Func, ")");
                }
                ExpectOp("=>", "in arrow function");
                m_FuncAsync.push_back(false);
                const int savedLoop = m_LoopDepth;
                m_LoopDepth = 0;
                if (IsName("do"))
                {
                    const Token& d = Next();
                    e->Func->Body = ParseBlock();
                    ExpectBlockEnd("do", d);
                }
                else
                {
                    // ifade gövdesi: return ifade
                    const Token& r = Peek();
                    e->Func->Body = std::make_unique<Block>();
                    auto ret = MakeStat(SK::Return, r);
                    ret->Values.push_back(ParseExpr());
                    e->Func->Body->Stats.push_back(std::move(ret));
                    e->Func->Body->EndLine = r.Line;
                }
                m_LoopDepth = savedLoop;
                m_FuncAsync.pop_back();
                return e;
            }

            // ─── İfadeler
            ExprP MakeExpr(EK kind, const Token& at)
            {
                auto e = std::make_unique<Expr>();
                e->Kind = kind;
                e->Line = at.Line;
                e->Col = at.Col;
                return e;
            }

            std::vector<ExprP> ParseExprList()
            {
                std::vector<ExprP> out;
                out.push_back(ParseExpr());
                while (AcceptOp(","))
                    out.push_back(ParseExpr());
                return out;
            }

            bool InAsync() const { return !m_FuncAsync.empty() && m_FuncAsync.back(); }

            ExprP ParseExpr(int limit = 0)
            {
                ExprP left;
                const Token& t = Peek();
                const bool unary = (t.Type == Tok::Name && t.Text == "not")
                                   || (t.Type == Tok::Op && (t.Text == "-" || t.Text == "#" || t.Text == "~" || t.Text == "!"));
                if (unary)
                {
                    Next();
                    left = MakeExpr(EK::Unary, t);
                    left->Text = t.Text == "!" ? "not" : t.Text;
                    left->A = ParseExpr(kUnaryPrio);
                }
                else if (t.Type == Tok::Name && t.Text == "await" && InAsync())
                {
                    Next();
                    left = MakeExpr(EK::Await, t);
                    left->A = ParseExpr(kUnaryPrio);
                }
                else
                    left = ParseSimpleExpr();
                Prio prio;
                while (BinaryPrio(Peek(), prio) && prio.Left > limit)
                {
                    const Token& op = Next();
                    ExprP right = ParseExpr(prio.Right);
                    ExprP bin = MakeExpr(op.Text == "??" ? EK::Coalesce : EK::Binary, op);
                    std::string o = op.Text;
                    if (o == "!=") o = "~=";
                    else if (o == "&&") o = "and";
                    else if (o == "||") o = "or";
                    bin->Text = o;
                    bin->A = std::move(left);
                    bin->B = std::move(right);
                    left = std::move(bin);
                }
                return left;
            }

            ExprP ParseSimpleExpr()
            {
                const Token& t = Peek();
                if (t.Type == Tok::Number) { Next(); auto e = MakeExpr(EK::Number, t); e->Text = t.Text; return e; }
                if (t.Type == Tok::String) { Next(); auto e = MakeExpr(EK::String, t); e->Text = t.Text; return e; }
                if (t.Type == Tok::Interp) { Next(); return ParseInterp(t); }
                if (t.Type == Tok::Name)
                {
                    if (t.Text == "nil") { Next(); return MakeExpr(EK::Nil, t); }
                    if (t.Text == "true") { Next(); return MakeExpr(EK::True, t); }
                    if (t.Text == "false") { Next(); return MakeExpr(EK::False, t); }
                    if (t.Text == "function")
                    {
                        Next();
                        auto e = MakeExpr(EK::Function, t);
                        e->Func = ParseFuncBody(false, false);
                        return e;
                    }
                    if (t.Text == "async" && IsName("function", 1))
                    {
                        Next(); Next();
                        auto e = MakeExpr(EK::Function, t);
                        e->Func = ParseFuncBody(false, true);
                        return e;
                    }
                    if (t.Text == "if") return ParseIfExpr();
                }
                if (t.Type == Tok::Op)
                {
                    if (t.Text == "...")
                    {
                        Next();
                        return MakeExpr(EK::Vararg, t);
                    }
                    if (t.Text == "{") return ParseTable();
                }
                if (ArrowAhead())
                    return ParseArrow();
                return ParseSuffixedExpr();
            }

            ExprP ParseIfExpr()
            {
                const Token& t = Next();   // if
                auto e = MakeExpr(EK::IfElse, t);
                e->List.push_back(ParseExpr());
                ExpectName("then", "in if-expression");
                e->List.push_back(ParseExpr());
                while (AcceptName("elseif"))
                {
                    e->List.push_back(ParseExpr());
                    ExpectName("then", "in if-expression");
                    e->List.push_back(ParseExpr());
                }
                ExpectName("else", "in if-expression (if-expressions need an else branch)");
                e->List.push_back(ParseExpr());
                return e;
            }

            ExprP ParseInterp(const Token& t)
            {
                // `metin {ifade} metin`: parçalar ham kalır, ifadeler ayrı ayrıştırılır
                auto e = MakeExpr(EK::Interp, t);
                const std::string& raw = t.Text;
                std::string part;
                size_t i = 1;
                const size_t end = raw.size() - 1;
                while (i < end)
                {
                    const char c = raw[i];
                    if (c == '\\' && i + 1 < end) { part += c; part += raw[i + 1]; i += 2; continue; }
                    if (c == '{')
                    {
                        int depth = 1;
                        size_t j = i + 1;
                        while (j < end && depth > 0)
                        {
                            if (raw[j] == '"' || raw[j] == '\'')
                            {
                                const char q = raw[j++];
                                while (j < end && raw[j] != q) { if (raw[j] == '\\') ++j; ++j; }
                            }
                            else if (raw[j] == '{') ++depth;
                            else if (raw[j] == '}') --depth;
                            ++j;
                        }
                        const std::string inner = raw.substr(i + 1, j - i - 2);
                        std::vector<Token> tokens;
                        try { tokens = Lex(inner); }
                        catch (const CompileError& err) { Fail(t.Line, t.Col + (int)i, "in interpolated string: " + err.Message); }
                        for (auto& tok : tokens) { tok.Line = t.Line; tok.Col += t.Col + (int)i; }
                        Parser sub(tokens);
                        sub.m_FuncAsync = m_FuncAsync;
                        sub.m_InMethod = m_InMethod;
                        sub.m_ClassDepth = m_ClassDepth;
                        ExprP inner_e = sub.ParseExpr();
                        if (sub.Peek().Type != Tok::Eof)
                            Fail(t.Line, t.Col, "malformed expression in interpolated string");
                        e->Parts.push_back(part);
                        part.clear();
                        e->List.push_back(std::move(inner_e));
                        i = j;
                        continue;
                    }
                    part += c;
                    ++i;
                }
                e->Parts.push_back(part);
                return e;
            }

            ExprP ParseTable()
            {
                const Token& t = Next();   // {
                auto e = MakeExpr(EK::Table, t);
                while (!IsOp("}"))
                {
                    TableItem item;
                    if (IsOp("["))
                    {
                        Next();
                        item.K = TableItem::Keyed;
                        item.Key = ParseExpr();
                        ExpectOp("]", "in table key");
                        ExpectOp("=", "after table key");
                        item.Value = ParseExpr();
                    }
                    else if (Peek().Type == Tok::Name && !Reserved().count(Peek().Text) && IsOp("=", 1))
                    {
                        item.K = TableItem::Named;
                        item.Name = Next().Text;
                        Next();
                        item.Value = ParseExpr();
                    }
                    else if (IsOp("...") && !IsOp(",", 1) && !IsOp("}", 1) && !IsOp(";", 1))
                    {
                        Next();
                        item.K = TableItem::SpreadItem;
                        item.Value = ParseExpr();
                    }
                    else
                    {
                        item.K = TableItem::Positional;
                        item.Value = ParseExpr();
                    }
                    e->Items.push_back(std::move(item));
                    if (!AcceptOp(",") && !AcceptOp(";"))
                        break;
                }
                ExpectOp("}", "to close table");
                return e;
            }

            std::vector<ExprP> ParseCallArgs()
            {
                std::vector<ExprP> args;
                if (Peek().Type == Tok::String)
                {
                    const Token& s = Next();
                    auto e = MakeExpr(EK::String, s);
                    e->Text = s.Text;
                    args.push_back(std::move(e));
                    return args;
                }
                if (IsOp("{"))
                {
                    args.push_back(ParseTable());
                    return args;
                }
                if (Peek().Type == Tok::Interp)
                {
                    const Token& s = Next();
                    args.push_back(ParseInterp(s));
                    return args;
                }
                ExpectOp("(", "for call arguments");
                while (!IsOp(")"))
                {
                    if (IsOp("...") && !IsOp(",", 1) && !IsOp(")", 1))
                    {
                        const Token& sp = Next();
                        auto e = MakeExpr(EK::Spread, sp);
                        e->A = ParseExpr();
                        args.push_back(std::move(e));
                    }
                    else
                        args.push_back(ParseExpr());
                    if (!AcceptOp(","))
                        break;
                }
                ExpectOp(")", "to close call arguments");
                return args;
            }

            bool CallArgsAhead() const
            {
                return IsOp("(") || IsOp("{") || Peek().Type == Tok::String || Peek().Type == Tok::Interp;
            }

            ExprP ParsePrimary()
            {
                const Token& t = Peek();
                if (t.Type == Tok::Name && t.Text == "await" && !InAsync() && Peek(1).Line == t.Line
                    && (Peek(1).Type == Tok::Name || Peek(1).Type == Tok::Number || Peek(1).Type == Tok::String) && !Reserved().count(Peek(1).Text))
                    Error(t, "'await' is only valid inside async functions (declare the function with 'async function')");
                if (t.Type == Tok::Name && t.Text == "super" && !m_InMethod.empty())
                {
                    Next();
                    return MakeExpr(EK::Super, t);
                }
                if (t.Type == Tok::Name && !Reserved().count(t.Text))
                {
                    Next();
                    auto e = MakeExpr(EK::Name, t);
                    e->Text = t.Text;
                    return e;
                }
                if (IsOp("("))
                {
                    Next();
                    auto e = MakeExpr(EK::Paren, t);
                    e->A = ParseExpr();
                    ExpectOp(")", "to close parenthesis");
                    return e;
                }
                Error(t, "unexpected '" + Describe(t) + "'");
            }

            ExprP ParseSuffixedExpr()
            {
                ExprP e = ParsePrimary();
                for (;;)
                {
                    const Token& t = Peek();
                    bool optional = false;
                    if (IsOp("?.") || (IsOp("?") && (IsOp(":", 1) || IsOp("[", 1) || IsOp("(", 1))))
                    {
                        optional = true;
                        if (IsOp("?."))
                        {
                            Next();
                            if (!IsOp("[") && !IsOp("("))
                            {
                                auto f = MakeExpr(EK::Field, t);
                                f->Text = Identifier("after '?.'");
                                f->A = std::move(e);
                                f->Optional = true;
                                e = std::move(f);
                                continue;
                            }
                        }
                        else
                            Next();   // '?'
                    }
                    if (IsOp("."))
                    {
                        Next();
                        auto f = MakeExpr(EK::Field, t);
                        f->Text = Identifier("after '.'");
                        f->A = std::move(e);
                        e = std::move(f);
                    }
                    else if (IsOp("["))
                    {
                        Next();
                        auto f = MakeExpr(EK::Index, t);
                        f->A = std::move(e);
                        f->B = ParseExpr();
                        f->Optional = optional;
                        ExpectOp("]", "to close index");
                        e = std::move(f);
                    }
                    else if (IsOp(":") && Peek(1).Type == Tok::Name)
                    {
                        Next();
                        auto f = MakeExpr(EK::Method, t);
                        f->Text = Identifier("for method call");
                        f->A = std::move(e);
                        f->Optional = optional;
                        f->List = ParseCallArgs();
                        e = std::move(f);
                    }
                    else if (CallArgsAhead())
                    {
                        auto f = MakeExpr(EK::Call, t);
                        f->A = std::move(e);
                        f->Optional = optional;
                        f->List = ParseCallArgs();
                        e = std::move(f);
                    }
                    else if (IsOp("::") && !(Peek(1).Type == Tok::Name && IsOp("::", 2)))
                    {
                        // Luau tür dönüşümü: atılır (::ad:: ise sonraki deyimin etiketi)
                        Next();
                        SkipType();
                    }
                    else
                    {
                        if (optional)
                            Error(t, "expected field, index, method or call after '?'");
                        return e;
                    }
                }
            }

            const std::vector<Token>& m_T;
            size_t m_P = 0;
            int m_LoopDepth = 0;
            int m_ClassDepth = 0;
            bool m_InMatch = false;
            std::vector<bool> m_FuncAsync;
            std::vector<bool> m_InMethod;
        };
    }

    std::vector<Token> Lex(const std::string& source)
    {
        return Lexer(source).Run();
    }

    Chunk Parse(const std::vector<Token>& tokens)
    {
        return Parser(tokens).Run();
    }
}
