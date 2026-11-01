#include "CQSParser.h"
#include "CQSLexer.h"

#include <unordered_set>

namespace Conqueror::CQS
{
    namespace
    {
        constexpr size_t kMaxErrors = 60;

        bool IsPrimitiveCastType(const std::string& s)
        {
            return s == "int" || s == "float" || s == "double" || s == "string" || s == "bool" || s == "long" || s == "short" || s == "byte" || s == "uint";
        }

        // Üye adı olarak anahtar kelimeleri de kabul et (obj.default, map.in ...)
        bool IsNameToken(TokenType t)
        {
            if (t == TokenType::Identifier) return true;
            switch (t)
            {
                case TokenType::Script: case TokenType::Class: case TokenType::Struct: case TokenType::Enum: case TokenType::Interface:
                case TokenType::Default: case TokenType::Event: case TokenType::In: case TokenType::Is: case TokenType::As:
                case TokenType::New: case TokenType::Base: case TokenType::Using: case TokenType::Namespace: case TokenType::Yield:
                case TokenType::Public: case TokenType::Private: case TokenType::Protected: case TokenType::Static:
                case TokenType::Case: case TokenType::Do: case TokenType::Try: case TokenType::Catch: case TokenType::Finally: case TokenType::Throw:
                case TokenType::Func: case TokenType::Var:
                    return true;
                default: return false;
            }
        }
    }

    // ════════════════════════════════════════
    //  Entry points
    // ════════════════════════════════════════
    ASTNodePtr CQSParser::Parse(const std::vector<Token>& tokens)
    {
        m_Tokens = tokens;
        if (m_Tokens.empty() || m_Tokens.back().Type != TokenType::EndOfFile)
            m_Tokens.emplace_back(TokenType::EndOfFile, "", m_Tokens.empty() ? 1 : m_Tokens.back().Line, 0);
        m_Current = 0;
        m_Errors.clear();
        m_FunctionDepth = 0;

        auto program = std::make_shared<ProgramNode>();
        while (!IsAtEnd() && m_Errors.size() < kMaxErrors)
        {
            if (Check(TokenType::RightBrace))
            {
                Report(Peek(), "Unexpected '}'.");
                Advance();
                continue;
            }
            if (auto decl = ParseDeclaration())
                program->Statements.push_back(decl);
        }
        return program;
    }

    ASTNodePtr CQSParser::ParseSingleExpression(const std::vector<Token>& tokens)
    {
        m_Tokens = tokens;
        if (m_Tokens.empty() || m_Tokens.back().Type != TokenType::EndOfFile)
            m_Tokens.emplace_back(TokenType::EndOfFile, "", m_Tokens.empty() ? 1 : m_Tokens.back().Line, 0);
        m_Current = 0;
        m_Errors.clear();
        try
        {
            auto expr = ParseExpression();
            if (!IsAtEnd())
                ErrorAt(Peek(), "Unexpected token after expression.");
            return expr;
        }
        catch (const ParseError&)
        {
            return nullptr;
        }
    }

    // ════════════════════════════════════════
    //  Token helpers
    // ════════════════════════════════════════
    const Token& CQSParser::Peek(size_t ahead) const
    {
        size_t i = m_Current + ahead;
        return i < m_Tokens.size() ? m_Tokens[i] : m_Tokens.back();
    }

    const Token& CQSParser::Previous() const
    {
        return m_Tokens[m_Current > 0 ? m_Current - 1 : 0];
    }

    const Token& CQSParser::Advance()
    {
        if (!IsAtEnd()) ++m_Current;
        return Previous();
    }

    bool CQSParser::IsAtEnd() const { return Peek().Type == TokenType::EndOfFile; }
    bool CQSParser::Check(TokenType type) const { return Peek().Type == type; }
    bool CQSParser::CheckAhead(size_t ahead, TokenType type) const { return Peek(ahead).Type == type; }

    bool CQSParser::Match(TokenType type)
    {
        if (!Check(type)) return false;
        Advance();
        return true;
    }

    bool CQSParser::Match(std::initializer_list<TokenType> types)
    {
        for (TokenType t : types)
            if (Check(t)) { Advance(); return true; }
        return false;
    }

    const Token& CQSParser::Consume(TokenType type, const std::string& message)
    {
        if (Check(type)) return Advance();
        // Unutulan ';' / ')' gibi kapanışlar: hatayı önceki token'ın satırında göster
        if ((type == TokenType::Semicolon || type == TokenType::RightParen || type == TokenType::RightBracket) &&
            m_Current > 0 && Previous().Line < Peek().Line)
        {
            const Token& prev = Previous();
            m_Errors.push_back("Line " + std::to_string(prev.Line) + ":" + std::to_string(prev.Column + (int)prev.Lexeme.size()) +
                               " after '" + prev.Lexeme + "': " + message);
            throw ParseError{};
        }
        ErrorAt(Peek(), message);
    }

    std::string CQSParser::ConsumeIdentifier(const std::string& message)
    {
        if (Check(TokenType::Identifier)) return Advance().Lexeme;
        ErrorAt(Peek(), message);
    }

    void CQSParser::Report(const Token& token, const std::string& message)
    {
        std::string where = token.Type == TokenType::EndOfFile ? "at end" : "at '" + token.Lexeme + "'";
        m_Errors.push_back("Line " + std::to_string(token.Line) + ":" + std::to_string(token.Column) + " " + where + ": " + message);
    }

    void CQSParser::ErrorAt(const Token& token, const std::string& message)
    {
        Report(token, message);
        throw ParseError{};
    }

    void CQSParser::Synchronize()
    {
        // Hata token'ını geç (bir deyim başlatmıyorsa); ardından bir sonraki deyim sınırına kadar ilerle
        auto startsStatement = [](TokenType t) {
            switch (t)
            {
                case TokenType::Class: case TokenType::Script: case TokenType::Struct: case TokenType::Enum:
                case TokenType::Func: case TokenType::Var: case TokenType::For: case TokenType::Foreach:
                case TokenType::If: case TokenType::While: case TokenType::Do: case TokenType::Return:
                case TokenType::Switch: case TokenType::Try: case TokenType::Throw: case TokenType::RightBrace:
                case TokenType::Public: case TokenType::Private: case TokenType::Static:
                    return true;
                default: return false;
            }
        };
        if (!IsAtEnd() && !startsStatement(Peek().Type))
            Advance();
        else
            return;
        while (!IsAtEnd())
        {
            if (Previous().Type == TokenType::Semicolon)
                return;
            switch (Peek().Type)
            {
                case TokenType::Class: case TokenType::Script: case TokenType::Struct: case TokenType::Enum:
                case TokenType::Func: case TokenType::Var: case TokenType::For: case TokenType::Foreach:
                case TokenType::If: case TokenType::While: case TokenType::Do: case TokenType::Return:
                case TokenType::Switch: case TokenType::Try: case TokenType::Throw: case TokenType::RightBrace:
                case TokenType::Public: case TokenType::Private: case TokenType::Static:
                    return;
                default: break;
            }
            Advance();
        }
    }

    // ════════════════════════════════════════
    //  Types & attributes
    // ════════════════════════════════════════
    bool CQSParser::TryParseTypeName(std::string& out)
    {
        size_t save = m_Current;
        if (!Check(TokenType::Identifier))
            return false;
        out = Advance().Lexeme;
        while (Check(TokenType::Dot) && CheckAhead(1, TokenType::Identifier))
        {
            Advance();
            out += "." + Advance().Lexeme;
        }
        if (Check(TokenType::Less))
        {
            // Generic: List<int>, Dictionary<string, List<int>>
            size_t inner = m_Current;
            int depth = 0;
            std::string generic;
            bool ok = false;
            while (inner < m_Tokens.size())
            {
                const Token& t = m_Tokens[inner];
                if (t.Type == TokenType::Less) ++depth;
                else if (t.Type == TokenType::Greater) { if (--depth == 0) { generic += ">"; ok = true; break; } }
                else if (t.Type == TokenType::GreaterGreater) { depth -= 2; if (depth <= 0) { generic += ">>"; ok = depth == 0; break; } }
                else if (t.Type != TokenType::Identifier && t.Type != TokenType::Comma && t.Type != TokenType::Dot &&
                         t.Type != TokenType::LeftBracket && t.Type != TokenType::RightBracket && t.Type != TokenType::Question)
                    break;
                generic += t.Lexeme;
                ++inner;
            }
            if (!ok)
            {
                m_Current = save;
                return false;
            }
            out += generic;
            m_Current = inner + 1;
        }
        while (Check(TokenType::LeftBracket) && CheckAhead(1, TokenType::RightBracket))
        {
            Advance();
            Advance();
            out += "[]";
        }
        if (Check(TokenType::Question) && (CheckAhead(1, TokenType::Identifier)))
        {
            Advance();
            out += "?";
        }
        return true;
    }

    std::string CQSParser::ParseTypeName()
    {
        std::string type;
        if (!TryParseTypeName(type))
            ErrorAt(Peek(), "Expected a type name.");
        return type;
    }

    bool CQSParser::LooksLikeTypedDeclaration(size_t& nameIndex) const
    {
        // Tip Ad ( = | ; | ( | , )   ör. "float speed = 5;", "void Update(float dt)"
        CQSParser& self = const_cast<CQSParser&>(*this);
        size_t save = self.m_Current;
        std::string type;
        bool result = false;
        if (self.TryParseTypeName(type) && self.Check(TokenType::Identifier))
        {
            TokenType next = self.Peek(1).Type;
            if (next == TokenType::Equal || next == TokenType::Semicolon || next == TokenType::LeftParen || next == TokenType::Comma)
            {
                nameIndex = self.m_Current;
                result = true;
            }
        }
        self.m_Current = save;
        return result;
    }

    std::vector<std::string> CQSParser::ParseAttributes()
    {
        std::vector<std::string> attrs;
        while (Check(TokenType::LeftBracket) && CheckAhead(1, TokenType::Identifier) &&
               (CheckAhead(2, TokenType::RightBracket) || CheckAhead(2, TokenType::LeftParen) || CheckAhead(2, TokenType::Comma)))
        {
            Advance(); // [
            do
            {
                std::string attr = ConsumeIdentifier("Expected attribute name.");
                if (Match(TokenType::LeftParen))
                {
                    // [Range(0, 10)], [Tooltip("...")]: argümanları metin olarak sakla
                    attr += "(";
                    int depth = 1;
                    bool first = true;
                    while (!IsAtEnd() && depth > 0)
                    {
                        const Token& t = Advance();
                        if (t.Type == TokenType::LeftParen) ++depth;
                        else if (t.Type == TokenType::RightParen && --depth == 0) break;
                        if (!first && t.Type != TokenType::Comma && Previous().Type != TokenType::Comma) {}
                        attr += t.Type == TokenType::StringLiteral ? "\"" + t.Lexeme + "\"" : t.Lexeme;
                        first = false;
                    }
                    attr += ")";
                }
                attrs.push_back(attr);
            } while (Match(TokenType::Comma));
            Consume(TokenType::RightBracket, "Expected ']' after attribute.");
        }
        return attrs;
    }

    // ════════════════════════════════════════
    //  Declarations
    // ════════════════════════════════════════
    ASTNodePtr CQSParser::ParseDeclaration()
    {
        size_t start = m_Current;
        try
        {
            if (Match(TokenType::Using)) return ParseUsing();
            if (Match(TokenType::Namespace)) return ParseNamespace();

            std::vector<std::string> attributes;
            if (m_FunctionDepth == 0)
                attributes = ParseAttributes();

            bool isPublic = false, isPrivate = false, isStatic = false, anyModifier = false;
            for (;;)
            {
                if (Match(TokenType::Public)) { isPublic = true; anyModifier = true; }
                else if (Match(TokenType::Private) || Match(TokenType::Protected)) { isPrivate = true; anyModifier = true; }
                else if (Match(TokenType::Static)) { isStatic = true; anyModifier = true; }
                else break;
            }

            if (Match(TokenType::Script)) return ParseClass(NodeType::ScriptDecl, isPublic);
            if (Match(TokenType::Class)) return ParseClass(NodeType::ClassDecl, isPublic);
            if (Match(TokenType::Struct)) return ParseClass(NodeType::StructDecl, isPublic);
            if (Match(TokenType::Enum)) return ParseEnum(isPublic);
            if (Match(TokenType::Interface)) return ParseInterface();
            if (Check(TokenType::Func) && CheckAhead(1, TokenType::Identifier))
            {
                Advance();
                const Token& name = Advance();
                return ParseFunction(name.Lexeme, name.Line, isPublic, isPrivate, isStatic, "");
            }
            if (Match(TokenType::Var)) return ParseVar(isPublic, isPrivate, isStatic, attributes);

            size_t nameIndex = 0;
            if (Check(TokenType::Identifier) && LooksLikeTypedDeclaration(nameIndex))
            {
                std::string type = ParseTypeName();
                const Token& name = Advance();
                if (Check(TokenType::LeftParen))
                    return ParseFunction(name.Lexeme, name.Line, isPublic, isPrivate, isStatic, type);
                return ParseTypedVar(type, name.Lexeme, name.Line, isPublic, isPrivate, isStatic, attributes);
            }
            if (anyModifier || !attributes.empty())
                ErrorAt(Peek(), "Expected a declaration after modifiers.");

            return ParseStatement();
        }
        catch (const ParseError&)
        {
            Synchronize();
            if (m_Current == start && !IsAtEnd() && !Check(TokenType::RightBrace))
                Advance(); // ilerleme garantisi
            return nullptr;
        }
    }

    ASTNodePtr CQSParser::ParseUsing()
    {
        int line = Previous().Line;
        std::string path;
        if (Check(TokenType::Static)) Advance();
        if (!IsNameToken(Peek().Type))
            ErrorAt(Peek(), "Expected namespace name after 'using'.");
        path = Advance().Lexeme;
        while (Match(TokenType::Dot))
        {
            if (!IsNameToken(Peek().Type)) ErrorAt(Peek(), "Expected identifier after '.'.");
            path += "." + Advance().Lexeme;
        }
        Consume(TokenType::Semicolon, "Expected ';' after using directive.");
        return std::make_shared<UsingNode>(path, line);
    }

    ASTNodePtr CQSParser::ParseNamespace()
    {
        int line = Previous().Line;
        std::string name = ConsumeIdentifier("Expected namespace name.");
        while (Match(TokenType::Dot))
            name += "." + ConsumeIdentifier("Expected identifier after '.'.");
        auto node = std::make_shared<NamespaceNode>(name, line);
        if (Match(TokenType::Semicolon))
        {
            // dosya kapsamlı namespace: geri kalan her şey bu namespace'te
            while (!IsAtEnd())
                if (auto d = ParseDeclaration()) node->Body.push_back(d);
            return node;
        }
        Consume(TokenType::LeftBrace, "Expected '{' after namespace name.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
            if (auto d = ParseDeclaration()) node->Body.push_back(d);
        Consume(TokenType::RightBrace, "Expected '}' after namespace body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseClass(NodeType type, bool isPublic)
    {
        const Token& nameTok = Consume(TokenType::Identifier, "Expected a name.");
        auto node = std::make_shared<ClassDeclNode>(nameTok.Lexeme, nameTok.Line, type);
        node->IsPublic = isPublic;
        node->IsScript = (type == NodeType::ScriptDecl);
        node->IsStruct = (type == NodeType::StructDecl);
        if (Match(TokenType::Colon))
        {
            node->BaseClass = ParseTypeName();
            while (Match(TokenType::Comma))
                node->Interfaces.push_back(ParseTypeName());
        }
        Consume(TokenType::LeftBrace, "Expected '{' before class body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd() && m_Errors.size() < kMaxErrors)
        {
            size_t start = m_Current;
            try
            {
                if (auto m = ParseMember(node->Name))
                    node->Members.push_back(m);
            }
            catch (const ParseError&)
            {
                Synchronize();
                if (m_Current == start && !IsAtEnd() && !Check(TokenType::RightBrace))
                    Advance();
            }
        }
        Consume(TokenType::RightBrace, "Expected '}' after class body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseMember(const std::string& className)
    {
        std::vector<std::string> attributes = ParseAttributes();
        bool isPublic = false, isPrivate = false, isStatic = false;
        for (;;)
        {
            if (Match(TokenType::Public)) isPublic = true;
            else if (Match(TokenType::Private) || Match(TokenType::Protected)) isPrivate = true;
            else if (Match(TokenType::Static)) isStatic = true;
            else if (Check(TokenType::Identifier) && (Peek().Lexeme == "override" || Peek().Lexeme == "virtual" || Peek().Lexeme == "readonly" ||
                                                       Peek().Lexeme == "abstract" || Peek().Lexeme == "sealed" || Peek().Lexeme == "async") &&
                     (CheckAhead(1, TokenType::Identifier) || CheckAhead(1, TokenType::Func) || CheckAhead(1, TokenType::Var)))
                Advance();
            else break;
        }
        if (Match(TokenType::Semicolon))
            return nullptr;
        if (Check(TokenType::Func) && IsNameToken(Peek(1).Type))
        {
            Advance();
            const Token& name = Advance();
            return ParseFunction(name.Lexeme, name.Line, isPublic, isPrivate, isStatic, "");
        }
        if (Match(TokenType::Var))
            return ParseVar(isPublic, isPrivate, isStatic, attributes);
        if (Match(TokenType::Event))
        {
            // event Action OnDeath;  -> alan
            std::string type;
            TryParseTypeName(type);
            const Token& name = Consume(TokenType::Identifier, "Expected event name.");
            return ParseTypedVar(type, name.Lexeme, name.Line, isPublic, isPrivate, isStatic, attributes);
        }
        // C# tarzı yapıcı: ClassName(...) { }
        if (Check(TokenType::Identifier) && Peek().Lexeme == className && CheckAhead(1, TokenType::LeftParen))
        {
            const Token& name = Advance();
            return ParseFunction("init", name.Line, true, false, false, "");
        }
        size_t nameIndex = 0;
        if (Check(TokenType::Identifier) && LooksLikeTypedDeclaration(nameIndex))
        {
            std::string type = ParseTypeName();
            const Token& name = Advance();
            if (Check(TokenType::LeftParen))
                return ParseFunction(name.Lexeme, name.Line, isPublic, isPrivate, isStatic, type);
            return ParseTypedVar(type, name.Lexeme, name.Line, isPublic, isPrivate, isStatic, attributes);
        }
        ErrorAt(Peek(), "Expected a field ('var x = ...;') or method ('func Name() { }') declaration.");
    }

    ASTNodePtr CQSParser::ParseEnum(bool isPublic)
    {
        const Token& nameTok = Consume(TokenType::Identifier, "Expected enum name.");
        auto node = std::make_shared<EnumDeclNode>(nameTok.Lexeme, nameTok.Line);
        node->IsPublic = isPublic;
        if (Match(TokenType::Colon))
            ParseTypeName(); // enum E : int
        Consume(TokenType::LeftBrace, "Expected '{' before enum body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            node->Values.push_back(ConsumeIdentifier("Expected enum member name."));
            node->Initializers.push_back(Match(TokenType::Equal) ? ParseExpression() : nullptr);
            if (!Match(TokenType::Comma))
                break;
        }
        Consume(TokenType::RightBrace, "Expected '}' after enum body.");
        Match(TokenType::Semicolon);
        return node;
    }

    ASTNodePtr CQSParser::ParseInterface()
    {
        const Token& nameTok = Consume(TokenType::Identifier, "Expected interface name.");
        auto node = std::make_shared<InterfaceDeclNode>(nameTok.Lexeme, nameTok.Line);
        if (Match(TokenType::Colon))
        {
            ParseTypeName();
            while (Match(TokenType::Comma)) ParseTypeName();
        }
        Consume(TokenType::LeftBrace, "Expected '{' before interface body.");
        // Gövdesiz imzaları atla (yalnızca belgeleme amaçlı)
        int depth = 1;
        while (!IsAtEnd() && depth > 0)
        {
            const Token& t = Advance();
            if (t.Type == TokenType::LeftBrace) ++depth;
            else if (t.Type == TokenType::RightBrace) --depth;
        }
        if (depth > 0)
            ErrorAt(Peek(), "Expected '}' after interface body.");
        return node;
    }

    std::vector<ASTNodePtr> CQSParser::ParseParameters()
    {
        std::vector<ASTNodePtr> params;
        std::unordered_set<std::string> seen;
        if (Check(TokenType::RightParen))
            return params;
        do
        {
            // "ref"/"out"/"params" gibi C# değiştiricilerini yok say
            while (Check(TokenType::Identifier) && (Peek().Lexeme == "ref" || Peek().Lexeme == "out" || Peek().Lexeme == "params" || Peek().Lexeme == "in") &&
                   CheckAhead(1, TokenType::Identifier))
                Advance();
            std::string name, type = "any";
            int line = Peek().Line;
            size_t nameIndex = 0;
            (void)nameIndex;
            // "Tip ad" mı "ad: Tip" mi?
            size_t save = m_Current;
            std::string maybeType;
            if (TryParseTypeName(maybeType) && Check(TokenType::Identifier))
            {
                type = maybeType;
                name = Advance().Lexeme;
            }
            else
            {
                m_Current = save;
                name = ConsumeIdentifier("Expected parameter name.");
                if (Match(TokenType::Colon))
                    type = ParseTypeName();
            }
            if (!seen.insert(name).second)
                ErrorAt(Previous(), "Duplicate parameter '" + name + "'.");
            auto p = std::make_shared<ParamNode>(name, type, line);
            if (Match(TokenType::Equal))
                p->DefaultValue = ParseTernary();
            else if (!params.empty() && static_cast<ParamNode*>(params.back().get())->DefaultValue)
                ErrorAt(Previous(), "Parameters without default values must come before optional parameters.");
            params.push_back(p);
        } while (Match(TokenType::Comma));
        return params;
    }

    ASTNodePtr CQSParser::ParseFunction(const std::string& name, int line, bool isPublic, bool isPrivate, bool isStatic, const std::string& returnType)
    {
        auto node = std::make_shared<FuncDeclNode>(name, line);
        node->IsPublic = isPublic;
        node->IsPrivate = isPrivate;
        node->IsStatic = isStatic;
        node->ReturnType = returnType;
        Consume(TokenType::LeftParen, "Expected '(' after function name.");
        node->Params = ParseParameters();
        Consume(TokenType::RightParen, "Expected ')' after parameters.");
        if (Match(TokenType::Arrow) || Match(TokenType::Colon))
            node->ReturnType = ParseTypeName();

        ++m_FunctionDepth;
        struct DepthGuard { int& d; ~DepthGuard() { --d; } } guard{ m_FunctionDepth };
        if (Match(TokenType::FatArrow))
        {
            // func Twice(x) => x * 2;
            int exprLine = Peek().Line;
            auto expr = ParseExpression();
            Consume(TokenType::Semicolon, "Expected ';' after expression-bodied function.");
            auto block = std::make_shared<BlockNode>(exprLine);
            auto ret = std::make_shared<ReturnNode>(exprLine);
            ret->Value = expr;
            block->Statements.push_back(ret);
            node->Body = block;
            return node;
        }
        Consume(TokenType::LeftBrace, "Expected '{' before function body.");
        node->Body = ParseBlock();
        return node;
    }

    ASTNodePtr CQSParser::ParseVar(bool isPublic, bool isPrivate, bool isStatic, const std::vector<std::string>& attributes, const std::string& typeName)
    {
        const Token& nameTok = Consume(TokenType::Identifier, "Expected variable name.");
        std::string type = typeName;
        if (Match(TokenType::Colon))
            type = ParseTypeName();
        return ParseTypedVar(type, nameTok.Lexeme, nameTok.Line, isPublic, isPrivate, isStatic, attributes);
    }

    ASTNodePtr CQSParser::ParseTypedVar(const std::string& typeName, const std::string& name, int line, bool isPublic, bool isPrivate, bool isStatic,
                                        const std::vector<std::string>& attributes)
    {
        auto makeDecl = [&](const std::string& n, int l) {
            auto node = std::make_shared<VarDeclNode>(n, l);
            node->TypeName = typeName;
            node->IsPublic = isPublic;
            node->IsPrivate = isPrivate;
            node->IsStatic = isStatic;
            node->Attributes = attributes;
            if (Match(TokenType::Equal))
                node->Initializer = ParseExpression();
            return node;
        };
        auto first = makeDecl(name, line);
        if (!Check(TokenType::Comma))
        {
            Consume(TokenType::Semicolon, "Expected ';' after variable declaration.");
            return first;
        }
        // var a = 1, b = 2;  -> blok olmadan çoklu bildirim (aynı kapsama eklenir)
        auto block = std::make_shared<BlockNode>(line);
        block->Statements.push_back(first);
        while (Match(TokenType::Comma))
        {
            const Token& n = Consume(TokenType::Identifier, "Expected variable name.");
            block->Statements.push_back(makeDecl(n.Lexeme, n.Line));
        }
        Consume(TokenType::Semicolon, "Expected ';' after variable declaration.");
        block->Column = -1; // işaret: kapsam açmayan blok
        return block;
    }

    // ════════════════════════════════════════
    //  Statements
    // ════════════════════════════════════════
    ASTNodePtr CQSParser::ParseStatement()
    {
        if (Match(TokenType::Semicolon)) return std::make_shared<BlockNode>(Previous().Line);
        if (Match(TokenType::LeftBrace)) return ParseBlock();
        if (Match(TokenType::If)) return ParseIf();
        if (Match(TokenType::While)) return ParseWhile();
        if (Match(TokenType::Do)) return ParseDoWhile();
        if (Match(TokenType::For)) return ParseFor();
        if (Match(TokenType::Foreach))
        {
            int line = Previous().Line;
            Consume(TokenType::LeftParen, "Expected '(' after 'foreach'.");
            return ParseForeach(line, true);
        }
        if (Match(TokenType::Switch)) return ParseSwitch();
        if (Match(TokenType::Return)) return ParseReturn();
        if (Match(TokenType::Break))
        {
            auto n = std::make_shared<BreakNode>(Previous().Line);
            Consume(TokenType::Semicolon, "Expected ';' after 'break'.");
            return n;
        }
        if (Match(TokenType::Continue))
        {
            auto n = std::make_shared<ContinueNode>(Previous().Line);
            Consume(TokenType::Semicolon, "Expected ';' after 'continue'.");
            return n;
        }
        if (Match(TokenType::Yield)) return ParseYield();
        if (Match(TokenType::Try)) return ParseTry();
        if (Match(TokenType::Throw)) return ParseThrow();
        return ParseExpressionStatement();
    }

    ASTNodePtr CQSParser::ParseBlock()
    {
        auto node = std::make_shared<BlockNode>(Previous().Line);
        while (!Check(TokenType::RightBrace) && !IsAtEnd() && m_Errors.size() < kMaxErrors)
        {
            if (auto s = ParseDeclaration())
                node->Statements.push_back(s);
        }
        Consume(TokenType::RightBrace, "Expected '}' to close block.");
        return node;
    }

    ASTNodePtr CQSParser::ParseIf()
    {
        auto node = std::make_shared<IfNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expected '(' after 'if'.");
        node->Condition = ParseExpression();
        Consume(TokenType::RightParen, "Expected ')' after if condition.");
        node->ThenBranch = ParseStatement();
        if (Match(TokenType::Else))
            node->ElseBranch = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseWhile()
    {
        auto node = std::make_shared<WhileNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expected '(' after 'while'.");
        node->Condition = ParseExpression();
        Consume(TokenType::RightParen, "Expected ')' after while condition.");
        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseDoWhile()
    {
        auto node = std::make_shared<WhileNode>(Previous().Line, NodeType::DoWhileStmt);
        node->Body = ParseStatement();
        Consume(TokenType::While, "Expected 'while' after do-body.");
        Consume(TokenType::LeftParen, "Expected '(' after 'while'.");
        node->Condition = ParseExpression();
        Consume(TokenType::RightParen, "Expected ')' after condition.");
        Consume(TokenType::Semicolon, "Expected ';' after do-while.");
        return node;
    }

    ASTNodePtr CQSParser::ParseFor()
    {
        int line = Previous().Line;
        Consume(TokenType::LeftParen, "Expected '(' after 'for'.");

        // for (var x in list) / for (x in list) / for (int x in list)
        {
            size_t i = 0;
            if (Check(TokenType::Var)) i = 1;
            else if (Check(TokenType::Identifier) && CheckAhead(1, TokenType::Identifier) && CheckAhead(2, TokenType::In)) i = 1;
            if (Peek(i).Type == TokenType::Identifier && (Peek(i + 1).Type == TokenType::In || (Peek(i + 1).Type == TokenType::Comma && Peek(i + 3).Type == TokenType::In)))
                return ParseForeach(line, true);
        }

        auto node = std::make_shared<ForNode>(line);
        if (Match(TokenType::Semicolon)) {}
        else if (Match(TokenType::Var))
            node->Initializer = ParseVar(false, false, false, {});
        else
        {
            size_t nameIndex = 0;
            if (Check(TokenType::Identifier) && LooksLikeTypedDeclaration(nameIndex))
            {
                std::string type = ParseTypeName();
                const Token& n = Advance();
                node->Initializer = ParseTypedVar(type, n.Lexeme, n.Line, false, false, false, {});
            }
            else
                node->Initializer = ParseExpressionStatement();
        }
        if (!Check(TokenType::Semicolon))
            node->Condition = ParseExpression();
        Consume(TokenType::Semicolon, "Expected ';' after loop condition.");
        if (!Check(TokenType::RightParen))
        {
            // for (...; ...; i++, j--)
            auto inc = ParseExpression();
            if (Check(TokenType::Comma))
            {
                auto block = std::make_shared<BlockNode>(inc->Line);
                block->Statements.push_back(std::make_shared<ExprStmtNode>(inc, inc->Line));
                while (Match(TokenType::Comma))
                {
                    auto e = ParseExpression();
                    block->Statements.push_back(std::make_shared<ExprStmtNode>(e, e->Line));
                }
                node->Increment = block;
            }
            else
                node->Increment = inc;
        }
        Consume(TokenType::RightParen, "Expected ')' after for clauses.");
        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseForeach(int line, bool hadParen)
    {
        auto node = std::make_shared<ForeachNode>(line);
        if (Match(TokenType::Var)) {}
        else if (Check(TokenType::Identifier) && CheckAhead(1, TokenType::Identifier))
            node->VarType = Advance().Lexeme;
        node->VarName = ConsumeIdentifier("Expected loop variable name.");
        if (Match(TokenType::Colon))
            node->VarType = ParseTypeName();
        if (Match(TokenType::Comma))
            node->ValueVarName = ConsumeIdentifier("Expected second loop variable name.");
        Consume(TokenType::In, "Expected 'in' after loop variable.");
        node->Iterable = ParseExpression();
        if (hadParen)
            Consume(TokenType::RightParen, "Expected ')' after foreach clause.");
        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseSwitch()
    {
        auto node = std::make_shared<SwitchNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expected '(' after 'switch'.");
        node->Expression = ParseExpression();
        Consume(TokenType::RightParen, "Expected ')' after switch expression.");
        Consume(TokenType::LeftBrace, "Expected '{' before switch body.");
        bool hasDefault = false;
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            auto c = std::make_shared<CaseNode>(Peek().Line);
            // Ardışık etiketler aynı gövdeyi paylaşır: case 1: case 2: ...
            bool any = false;
            while (Check(TokenType::Case) || Check(TokenType::Default))
            {
                any = true;
                if (Match(TokenType::Default))
                {
                    if (hasDefault) ErrorAt(Previous(), "Duplicate 'default' label.");
                    hasDefault = true;
                    c->IsDefault = true;
                }
                else
                {
                    Advance();
                    do { c->Values.push_back(ParseTernary()); } while (Match(TokenType::Comma));
                }
                Consume(TokenType::Colon, "Expected ':' after case label.");
            }
            if (!any)
                ErrorAt(Peek(), "Expected 'case' or 'default' in switch body.");
            while (!Check(TokenType::Case) && !Check(TokenType::Default) && !Check(TokenType::RightBrace) && !IsAtEnd())
                if (auto s = ParseDeclaration()) c->Body.push_back(s);
            node->Cases.push_back(c);
        }
        Consume(TokenType::RightBrace, "Expected '}' after switch body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseReturn()
    {
        auto node = std::make_shared<ReturnNode>(Previous().Line);
        if (!Check(TokenType::Semicolon))
            node->Value = ParseExpression();
        Consume(TokenType::Semicolon, "Expected ';' after return.");
        return node;
    }

    ASTNodePtr CQSParser::ParseYield()
    {
        auto node = std::make_shared<YieldNode>(Previous().Line);
        if (Match(TokenType::Break))
        {
            Consume(TokenType::Semicolon, "Expected ';' after 'yield break'.");
            auto r = std::make_shared<ReturnNode>(node->Line);
            return r;
        }
        Consume(TokenType::Return, "Expected 'return' after 'yield'.");
        if (!Check(TokenType::Semicolon))
            node->Value = ParseExpression();
        Consume(TokenType::Semicolon, "Expected ';' after yield.");
        return node;
    }

    ASTNodePtr CQSParser::ParseTry()
    {
        auto node = std::make_shared<TryCatchNode>(Previous().Line);
        Consume(TokenType::LeftBrace, "Expected '{' after 'try'.");
        node->TryBody = ParseBlock();
        if (Match(TokenType::Catch))
        {
            if (Match(TokenType::LeftParen))
            {
                // catch (e) / catch (e: Type) / catch (Type e) / catch (Type)
                std::string a = ConsumeIdentifier("Expected exception variable or type.");
                if (Check(TokenType::Identifier))
                {
                    node->CatchTypeName = a;
                    node->CatchVarName = Advance().Lexeme;
                }
                else if (Match(TokenType::Colon))
                {
                    node->CatchVarName = a;
                    node->CatchTypeName = ParseTypeName();
                }
                else if (!a.empty() && a[0] >= 'A' && a[0] <= 'Z' && (a == "Exception" || a.size() > 9))
                    node->CatchTypeName = a;
                else
                    node->CatchVarName = a;
                Consume(TokenType::RightParen, "Expected ')' after catch clause.");
            }
            Consume(TokenType::LeftBrace, "Expected '{' after catch.");
            node->CatchBody = ParseBlock();
        }
        if (Match(TokenType::Finally))
        {
            Consume(TokenType::LeftBrace, "Expected '{' after 'finally'.");
            node->FinallyBody = ParseBlock();
        }
        if (!node->CatchBody && !node->FinallyBody)
            ErrorAt(Peek(), "Expected 'catch' or 'finally' after try block.");
        return node;
    }

    ASTNodePtr CQSParser::ParseThrow()
    {
        auto node = std::make_shared<ThrowNode>(Previous().Line);
        if (!Check(TokenType::Semicolon))
            node->Expression = ParseExpression();
        Consume(TokenType::Semicolon, "Expected ';' after throw.");
        return node;
    }

    ASTNodePtr CQSParser::ParseExpressionStatement()
    {
        const Token& start = Peek();
        auto expr = ParseExpression();
        if (!Check(TokenType::Semicolon))
        {
            // Yaygın hata: noktalı virgül unutulmuş -> hatayı önceki token'ın satırında göster
            const Token& prev = Previous();
            Report(prev.Line < Peek().Line ? prev : Peek(), "Expected ';' after expression.");
            throw ParseError{};
        }
        Advance();
        return std::make_shared<ExprStmtNode>(expr, start.Line);
    }

    // ════════════════════════════════════════
    //  Expressions
    // ════════════════════════════════════════
    ASTNodePtr CQSParser::ParseExpression()
    {
        return ParseAssignment();
    }

    ASTNodePtr CQSParser::ParseAssignment()
    {
        auto expr = ParseTernary();
        if (Match(TokenType::Equal))
        {
            int line = Previous().Line;
            auto value = ParseAssignment();
            return std::make_shared<AssignNode>(expr, value, line);
        }
        static const std::pair<TokenType, TokenType> compound[] = {
            { TokenType::PlusEqual, TokenType::Plus }, { TokenType::MinusEqual, TokenType::Minus },
            { TokenType::StarEqual, TokenType::Star }, { TokenType::SlashEqual, TokenType::Slash },
            { TokenType::PercentEqual, TokenType::Percent }, { TokenType::AmpEqual, TokenType::Amp },
            { TokenType::PipeEqual, TokenType::Pipe }, { TokenType::CaretEqual, TokenType::Caret },
            { TokenType::LessLessEqual, TokenType::LessLess }, { TokenType::GreaterGreaterEqual, TokenType::GreaterGreater },
            { TokenType::QuestionQuestionEqual, TokenType::QuestionQuestion },
        };
        for (auto& [tok, op] : compound)
        {
            if (Match(tok))
            {
                int line = Previous().Line;
                auto value = ParseAssignment();
                return std::make_shared<CompoundAssignNode>(expr, op, value, line);
            }
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseTernary()
    {
        auto expr = ParseNullCoalesce();
        if (Match(TokenType::Question))
        {
            auto node = std::make_shared<TernaryNode>(Previous().Line);
            node->Condition = expr;
            node->TrueExpr = ParseAssignment();
            Consume(TokenType::Colon, "Expected ':' in conditional expression.");
            node->FalseExpr = ParseAssignment();
            return node;
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseNullCoalesce()
    {
        auto expr = ParseOr();
        if (Match(TokenType::QuestionQuestion))
        {
            int line = Previous().Line;
            auto right = ParseNullCoalesce(); // sağdan birleşir
            return std::make_shared<BinaryNode>(expr, TokenType::QuestionQuestion, right, line, NodeType::LogicalExpr);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseOr()
    {
        auto expr = ParseAnd();
        while (Match(TokenType::PipePipe))
        {
            int line = Previous().Line;
            expr = std::make_shared<BinaryNode>(expr, TokenType::PipePipe, ParseAnd(), line, NodeType::LogicalExpr);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseAnd()
    {
        auto expr = ParseBitOr();
        while (Match(TokenType::AmpAmp))
        {
            int line = Previous().Line;
            expr = std::make_shared<BinaryNode>(expr, TokenType::AmpAmp, ParseBitOr(), line, NodeType::LogicalExpr);
        }
        return expr;
    }

#define CQS_BINARY_LEVEL(Name, Next, ...)                                                   \
    ASTNodePtr CQSParser::Name()                                                            \
    {                                                                                       \
        auto expr = Next();                                                                 \
        while (Match({ __VA_ARGS__ }))                                                      \
        {                                                                                   \
            TokenType op = Previous().Type;                                                 \
            int line = Previous().Line;                                                     \
            expr = std::make_shared<BinaryNode>(expr, op, Next(), line);                    \
        }                                                                                   \
        return expr;                                                                        \
    }

    CQS_BINARY_LEVEL(ParseBitOr, ParseBitXor, TokenType::Pipe)
    CQS_BINARY_LEVEL(ParseBitXor, ParseBitAnd, TokenType::Caret)
    CQS_BINARY_LEVEL(ParseBitAnd, ParseEquality, TokenType::Amp)
    CQS_BINARY_LEVEL(ParseEquality, ParseRelational, TokenType::EqualEqual, TokenType::BangEqual)
    CQS_BINARY_LEVEL(ParseShift, ParseAdditive, TokenType::LessLess, TokenType::GreaterGreater)
    CQS_BINARY_LEVEL(ParseAdditive, ParseMultiplicative, TokenType::Plus, TokenType::Minus)
    CQS_BINARY_LEVEL(ParseMultiplicative, ParseUnary, TokenType::Star, TokenType::Slash, TokenType::Percent)
#undef CQS_BINARY_LEVEL

    ASTNodePtr CQSParser::ParseRelational()
    {
        auto expr = ParseRange();
        for (;;)
        {
            if (Match({ TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual }))
            {
                TokenType op = Previous().Type;
                int line = Previous().Line;
                expr = std::make_shared<BinaryNode>(expr, op, ParseRange(), line);
            }
            else if (Match(TokenType::Is))
            {
                int line = Previous().Line;
                bool negate = false;
                if (Check(TokenType::Identifier) && Peek().Lexeme == "not") { Advance(); negate = true; }
                std::string type;
                if (Match(TokenType::Null)) type = "null";
                else type = ParseTypeName();
                ASTNodePtr test = std::make_shared<TypeTestNode>(NodeType::IsExpr, expr, type, line);
                expr = negate ? std::make_shared<UnaryNode>(TokenType::Bang, test, line) : test;
            }
            else if (Match(TokenType::As))
            {
                int line = Previous().Line;
                expr = std::make_shared<TypeTestNode>(NodeType::CastExpr, expr, ParseTypeName(), line);
            }
            else
                break;
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseRange()
    {
        auto expr = ParseShift();
        if (Match(TokenType::DotDot))
        {
            int line = Previous().Line;
            expr = std::make_shared<BinaryNode>(expr, TokenType::DotDot, ParseShift(), line);
        }
        return expr;
    }

    bool CQSParser::IsCastAhead() const
    {
        // (int)x, (float)(a / b)
        if (!Check(TokenType::LeftParen) || !CheckAhead(1, TokenType::Identifier) || !CheckAhead(2, TokenType::RightParen))
            return false;
        if (!IsPrimitiveCastType(Peek(1).Lexeme))
            return false;
        switch (Peek(3).Type)
        {
            case TokenType::Identifier: case TokenType::IntLiteral: case TokenType::FloatLiteral: case TokenType::StringLiteral:
            case TokenType::LeftParen: case TokenType::Minus: case TokenType::Bang: case TokenType::Tilde: case TokenType::This:
            case TokenType::True: case TokenType::False: case TokenType::Null: case TokenType::New: case TokenType::InterpolatedString:
                return true;
            default:
                return false;
        }
    }

    ASTNodePtr CQSParser::ParseUnary()
    {
        if (Match({ TokenType::Bang, TokenType::Minus, TokenType::Tilde, TokenType::Plus }))
        {
            TokenType op = Previous().Type;
            int line = Previous().Line;
            auto operand = ParseUnary();
            if (op == TokenType::Plus)
                return operand;
            // Sabit katlama: -5 doğrudan sabit olsun
            if (op == TokenType::Minus && operand->Type == NodeType::LiteralExpr)
            {
                auto* lit = static_cast<LiteralNode*>(operand.get());
                if (lit->LiteralValue.IsInt()) return std::make_shared<LiteralNode>(Value::MakeInt(-lit->LiteralValue.AsInt()), line);
                if (lit->LiteralValue.IsFloat()) return std::make_shared<LiteralNode>(Value::MakeFloat(-lit->LiteralValue.AsFloat()), line);
            }
            return std::make_shared<UnaryNode>(op, operand, line);
        }
        if (Match({ TokenType::PlusPlus, TokenType::MinusMinus }))
        {
            bool inc = Previous().Type == TokenType::PlusPlus;
            int line = Previous().Line;
            return std::make_shared<IncDecNode>(ParseUnary(), inc, true, line);
        }
        if (IsCastAhead())
        {
            int line = Peek().Line;
            Advance();
            std::string type = Advance().Lexeme;
            Advance();
            return std::make_shared<TypeTestNode>(NodeType::CastExpr, ParseUnary(), type, line);
        }
        return ParsePostfix();
    }

    bool CQSParser::IsGenericCallAhead(std::vector<std::string>& genericArgs, size_t& endIndex) const
    {
        // < Tip (, Tip)* > (
        if (!Check(TokenType::Less))
            return false;
        size_t i = m_Current + 1;
        std::vector<std::string> args;
        std::string current;
        int depth = 1;
        while (i < m_Tokens.size())
        {
            const Token& t = m_Tokens[i];
            if (t.Type == TokenType::Identifier || t.Type == TokenType::Dot || t.Type == TokenType::LeftBracket || t.Type == TokenType::RightBracket)
                current += t.Lexeme;
            else if (t.Type == TokenType::Comma && depth == 1)
            {
                if (current.empty()) return false;
                args.push_back(current);
                current.clear();
            }
            else if (t.Type == TokenType::Less) { ++depth; current += "<"; }
            else if (t.Type == TokenType::Greater)
            {
                if (--depth == 0) break;
                current += ">";
            }
            else
                return false;
            ++i;
        }
        if (depth != 0 || current.empty() || i + 1 >= m_Tokens.size() || m_Tokens[i + 1].Type != TokenType::LeftParen)
            return false;
        args.push_back(current);
        genericArgs = args;
        endIndex = i + 1; // '(' konumu
        return true;
    }

    ASTNodePtr CQSParser::ParsePostfix()
    {
        auto expr = ParsePrimary();
        for (;;)
        {
            if (Match(TokenType::LeftParen))
                expr = FinishCall(expr, false);
            else if (Match(TokenType::Dot) || Match(TokenType::QuestionDot))
            {
                bool nullCond = Previous().Type == TokenType::QuestionDot;
                if (nullCond && Match(TokenType::LeftParen))
                {
                    expr = FinishCall(expr, true);
                    continue;
                }
                if (!IsNameToken(Peek().Type))
                    ErrorAt(Peek(), "Expected member name after '.'.");
                const Token& name = Advance();
                auto member = std::make_shared<MemberAccessNode>(expr, name.Lexeme, name.Line);
                member->NullConditional = nullCond;
                member->Column = name.Column;
                expr = member;
            }
            else if (Match(TokenType::LeftBracket))
            {
                int line = Previous().Line;
                auto index = ParseExpression();
                Consume(TokenType::RightBracket, "Expected ']' after index.");
                expr = std::make_shared<IndexNode>(expr, index, line);
            }
            else if (Check(TokenType::Less) && (expr->Type == NodeType::IdentifierExpr || expr->Type == NodeType::MemberAccessExpr))
            {
                std::vector<std::string> genericArgs;
                size_t parenIndex = 0;
                if (!IsGenericCallAhead(genericArgs, parenIndex))
                    break;
                m_Current = parenIndex + 1;
                auto call = std::static_pointer_cast<CallNode>(FinishCall(expr, false));
                call->GenericArgs = genericArgs;
                expr = call;
            }
            else if (Match({ TokenType::PlusPlus, TokenType::MinusMinus }))
            {
                bool inc = Previous().Type == TokenType::PlusPlus;
                expr = std::make_shared<IncDecNode>(expr, inc, false, Previous().Line);
            }
            else
                break;
        }
        return expr;
    }

    ASTNodePtr CQSParser::FinishCall(ASTNodePtr callee, bool nullConditional)
    {
        auto node = std::make_shared<CallNode>(callee, Previous().Line);
        node->NullConditional = nullConditional;
        if (!Check(TokenType::RightParen))
        {
            do
            {
                // Adlandırılmış argümanlar (ad: değer) desteklenmez; "out"/"ref" yok sayılır
                while (Check(TokenType::Identifier) && (Peek().Lexeme == "ref" || Peek().Lexeme == "out") && CheckAhead(1, TokenType::Identifier))
                    Advance();
                node->Arguments.push_back(ParseExpression());
            } while (Match(TokenType::Comma));
        }
        Consume(TokenType::RightParen, "Expected ')' after arguments.");
        if (node->Arguments.size() > 250)
            ErrorAt(Previous(), "Too many arguments (max 250).");
        return node;
    }

    bool CQSParser::IsLambdaAhead() const
    {
        // x => ...
        if (Check(TokenType::Identifier) && CheckAhead(1, TokenType::FatArrow))
            return true;
        if (!Check(TokenType::LeftParen))
            return false;
        // ( ... ) =>  : parantezleri eşle
        size_t i = m_Current;
        int depth = 0;
        while (i < m_Tokens.size())
        {
            TokenType t = m_Tokens[i].Type;
            if (t == TokenType::LeftParen) ++depth;
            else if (t == TokenType::RightParen && --depth == 0) break;
            else if (t == TokenType::EndOfFile) return false;
            ++i;
        }
        return i + 1 < m_Tokens.size() && m_Tokens[i + 1].Type == TokenType::FatArrow;
    }

    ASTNodePtr CQSParser::ParseLambda(int line)
    {
        auto node = std::make_shared<LambdaNode>(line);
        if (Check(TokenType::Identifier))
        {
            const Token& p = Advance();
            node->Params.push_back(std::make_shared<ParamNode>(p.Lexeme, "any", p.Line));
        }
        else
        {
            Consume(TokenType::LeftParen, "Expected '(' for lambda parameters.");
            node->Params = ParseParameters();
            Consume(TokenType::RightParen, "Expected ')' after lambda parameters.");
        }
        Consume(TokenType::FatArrow, "Expected '=>' in lambda.");
        ++m_FunctionDepth;
        struct DepthGuard { int& d; ~DepthGuard() { --d; } } guard{ m_FunctionDepth };
        if (Match(TokenType::LeftBrace))
            node->Body = ParseBlock();
        else
        {
            node->Body = ParseAssignment();
            node->ExpressionBody = true;
        }
        return node;
    }

    ASTNodePtr CQSParser::ParsePrimary()
    {
        const Token& tok = Peek();
        switch (tok.Type)
        {
            case TokenType::False: Advance(); return std::make_shared<LiteralNode>(Value::MakeBool(false), tok.Line);
            case TokenType::True:  Advance(); return std::make_shared<LiteralNode>(Value::MakeBool(true), tok.Line);
            case TokenType::Null:  Advance(); return std::make_shared<LiteralNode>(Value::MakeNull(), tok.Line);
            case TokenType::This:  Advance(); return std::make_shared<ThisNode>(tok.Line);
            case TokenType::Base:  Advance(); return std::make_shared<BaseNode>(tok.Line);
            case TokenType::IntLiteral:   Advance(); return std::make_shared<LiteralNode>(Value::MakeInt(tok.IntValue), tok.Line);
            case TokenType::FloatLiteral: Advance(); return std::make_shared<LiteralNode>(Value::MakeFloat(tok.FloatValue), tok.Line);
            case TokenType::StringLiteral:
            {
                Advance();
                std::string s = tok.Lexeme;
                // Bitişik metinler birleşir: "a" "b"
                while (Check(TokenType::StringLiteral))
                    s += Advance().Lexeme;
                return std::make_shared<LiteralNode>(s, tok.Line);
            }
            case TokenType::InterpolatedString:
            {
                Token t = Advance();
                return ParseInterpolated(t);
            }
            default: break;
        }

        if (IsLambdaAhead())
            return ParseLambda(tok.Line);

        if (Match(TokenType::Identifier))
        {
            auto id = std::make_shared<IdentifierNode>(tok.Lexeme, tok.Line, tok.Column);
            return id;
        }

        if (Match(TokenType::Func))
        {
            // Anonim fonksiyon: func (a, b) { ... }
            auto node = std::make_shared<LambdaNode>(tok.Line);
            Consume(TokenType::LeftParen, "Expected '(' after 'func'.");
            node->Params = ParseParameters();
            Consume(TokenType::RightParen, "Expected ')' after parameters.");
            if (Match(TokenType::Arrow) || Match(TokenType::Colon))
                ParseTypeName();
            Consume(TokenType::LeftBrace, "Expected '{' before function body.");
            ++m_FunctionDepth;
            struct DepthGuard { int& d; ~DepthGuard() { --d; } } guard{ m_FunctionDepth };
            node->Body = ParseBlock();
            return node;
        }

        if (Match(TokenType::LeftParen))
        {
            auto expr = ParseExpression();
            Consume(TokenType::RightParen, "Expected ')' after expression.");
            return expr;
        }

        if (Match(TokenType::LeftBracket)) return ParseListLiteral();
        if (Match(TokenType::LeftBrace)) return ParseMapLiteral();

        if (Match(TokenType::New))
        {
            int line = Previous().Line;
            std::string typeName;
            const Token& nameTok = Peek();
            if (!Check(TokenType::Identifier))
                ErrorAt(Peek(), "Expected type name after 'new'.");
            // new int[10]
            if (CheckAhead(1, TokenType::LeftBracket) && !CheckAhead(2, TokenType::RightBracket))
            {
                auto node = std::make_shared<NewNode>(nullptr, line);
                node->ElementType = Advance().Lexeme;
                Advance(); // [
                node->ArraySize = ParseExpression();
                Consume(TokenType::RightBracket, "Expected ']' after array size.");
                return node;
            }
            typeName = ParseTypeName();
            // Generic argümanlarını ve dizi işaretini at: List<int> -> List
            std::string baseName = typeName.substr(0, typeName.find('<'));
            bool isArray = typeName.size() >= 2 && typeName.compare(typeName.size() - 2, 2, "[]") == 0;
            ASTNodePtr classExpr;
            {
                // Namespace.Class -> üye erişimi zinciri
                size_t start = 0;
                std::string part;
                for (size_t i = 0; i <= baseName.size(); ++i)
                {
                    if (i == baseName.size() || baseName[i] == '.')
                    {
                        part = baseName.substr(start, i - start);
                        if (!classExpr) classExpr = std::make_shared<IdentifierNode>(part, nameTok.Line, nameTok.Column);
                        else classExpr = std::make_shared<MemberAccessNode>(classExpr, part, nameTok.Line);
                        start = i + 1;
                    }
                }
            }
            if (isArray)
                classExpr = std::make_shared<IdentifierNode>("List", nameTok.Line, nameTok.Column);
            auto node = std::make_shared<NewNode>(classExpr, line);
            if (Match(TokenType::LeftParen))
            {
                if (!Check(TokenType::RightParen))
                {
                    do { node->Arguments.push_back(ParseExpression()); } while (Match(TokenType::Comma));
                }
                Consume(TokenType::RightParen, "Expected ')' after constructor arguments.");
            }
            else if (!Check(TokenType::LeftBrace))
                ErrorAt(Peek(), "Expected '(' after type name in 'new' expression.");
            if (Match(TokenType::LeftBrace))
            {
                // Nesne başlatıcı: new X { a = 1, b = 2 }  ya da koleksiyon: new List<int> { 1, 2 }
                while (!Check(TokenType::RightBrace) && !IsAtEnd())
                {
                    if (Check(TokenType::Identifier) && CheckAhead(1, TokenType::Equal))
                    {
                        std::string field = Advance().Lexeme;
                        Advance();
                        node->Initializers.emplace_back(field, ParseExpression());
                    }
                    else
                        node->Initializers.emplace_back("", ParseExpression());
                    if (!Match(TokenType::Comma))
                        break;
                }
                Consume(TokenType::RightBrace, "Expected '}' after initializer.");
            }
            return node;
        }

        ErrorAt(tok, "Expected an expression.");
    }

    ASTNodePtr CQSParser::ParseListLiteral()
    {
        auto node = std::make_shared<ListLiteralNode>(Previous().Line);
        while (!Check(TokenType::RightBracket) && !IsAtEnd())
        {
            node->Elements.push_back(ParseExpression());
            if (!Match(TokenType::Comma))
                break;
        }
        Consume(TokenType::RightBracket, "Expected ']' after list elements.");
        return node;
    }

    ASTNodePtr CQSParser::ParseMapLiteral()
    {
        auto node = std::make_shared<MapLiteralNode>(Previous().Line);
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            ASTNodePtr key;
            if (IsNameToken(Peek().Type) && (CheckAhead(1, TokenType::Colon) || CheckAhead(1, TokenType::Equal)))
                key = std::make_shared<LiteralNode>(Advance().Lexeme, Previous().Line);
            else if (Check(TokenType::LeftBracket))
            {
                Advance();
                key = ParseExpression();
                Consume(TokenType::RightBracket, "Expected ']' after computed key.");
            }
            else
                key = ParseTernary();
            if (!Match(TokenType::Colon) && !Match(TokenType::Equal))
                ErrorAt(Peek(), "Expected ':' after map key.");
            node->Entries.emplace_back(key, ParseExpression());
            if (!Match(TokenType::Comma))
                break;
        }
        Consume(TokenType::RightBrace, "Expected '}' after map entries.");
        return node;
    }

    ASTNodePtr CQSParser::ParseInterpolated(const Token& token)
    {
        auto node = std::make_shared<InterpolatedStringNode>(token.Line);
        const std::string& raw = token.Lexeme;
        std::string literal;
        auto flushLiteral = [&]() {
            if (literal.empty()) return;
            std::string value, error;
            if (!CQSLexer::Unescape(literal, value, error))
                ErrorAt(token, error);
            node->Parts.push_back(std::make_shared<LiteralNode>(value, token.Line));
            node->Formats.emplace_back();
            literal.clear();
        };
        for (size_t i = 0; i < raw.size(); ++i)
        {
            char c = raw[i];
            if (c == '\\' && i + 1 < raw.size())
            {
                literal += c;
                literal += raw[++i];
                continue;
            }
            if (c == '{' && i + 1 < raw.size() && raw[i + 1] == '{') { literal += '{'; ++i; continue; }
            if (c == '}' && i + 1 < raw.size() && raw[i + 1] == '}') { literal += '}'; ++i; continue; }
            if (c == '}')
                ErrorAt(token, "Unmatched '}' in interpolated string (use '}}').");
            if (c != '{')
            {
                literal += c;
                continue;
            }
            // { ifade[:biçim] }
            size_t start = i + 1, j = start;
            int depth = 1, parens = 0;
            bool inString = false, hasTernary = false;
            size_t formatColon = std::string::npos;
            for (; j < raw.size(); ++j)
            {
                char d = raw[j];
                if (inString)
                {
                    if (d == '\\') { ++j; continue; }
                    if (d == '"') inString = false;
                    continue;
                }
                if (d == '"') inString = true;
                else if (d == '(' || d == '[') ++parens;
                else if (d == ')' || d == ']') --parens;
                else if (d == '{') ++depth;
                else if (d == '}' && --depth == 0) break;
                else if (d == '?' && parens == 0 && depth == 1 && j + 1 < raw.size() && raw[j + 1] != '?' && raw[j + 1] != '.' && (j == 0 || raw[j - 1] != '?'))
                    hasTernary = true;
                else if (d == ':' && parens == 0 && depth == 1 && !hasTernary)
                    formatColon = j;
            }
            if (j >= raw.size())
                ErrorAt(token, "Unterminated '{' in interpolated string.");
            std::string exprText = raw.substr(start, (formatColon != std::string::npos ? formatColon : j) - start);
            std::string format = formatColon != std::string::npos ? raw.substr(formatColon + 1, j - formatColon - 1) : "";
            flushLiteral();
            CQSLexer lexer;
            auto tokens = lexer.Tokenize(exprText, token.Line);
            for (auto& e : lexer.GetErrors())
                m_Errors.push_back("In interpolated string: " + e);
            CQSParser sub;
            auto expr = sub.ParseSingleExpression(tokens);
            for (auto& e : sub.GetErrors())
                m_Errors.push_back("In interpolated string: " + e);
            if (!expr)
                throw ParseError{};
            node->Parts.push_back(expr);
            node->Formats.push_back(format);
            i = j;
        }
        flushLiteral();
        return node;
    }
}
