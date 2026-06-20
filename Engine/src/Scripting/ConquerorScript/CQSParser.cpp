#include "CQSParser.h"
#include <iostream>

namespace Conqueror::CQS
{
    ASTNodePtr CQSParser::Parse(const std::vector<Token>& tokens)
    {
        m_Tokens = tokens;
        m_Current = 0;
        m_HasError = false;
        m_Errors.clear();

        return ParseProgram();
    }

    const Token& CQSParser::Peek() const
    {
        return m_Tokens[m_Current];
    }

    const Token& CQSParser::Previous() const
    {
        return m_Tokens[m_Current - 1];
    }

    const Token& CQSParser::Advance()
    {
        if (!IsAtEnd()) m_Current++;
        return Previous();
    }

    bool CQSParser::IsAtEnd() const
    {
        return Peek().Type == TokenType::EndOfFile;
    }

    bool CQSParser::Check(TokenType type) const
    {
        if (IsAtEnd()) return false;
        return Peek().Type == type;
    }

    bool CQSParser::Match(TokenType type)
    {
        if (Check(type))
        {
            Advance();
            return true;
        }
        return false;
    }

    bool CQSParser::Match(std::initializer_list<TokenType> types)
    {
        for (auto type : types)
        {
            if (Check(type))
            {
                Advance();
                return true;
            }
        }
        return false;
    }

    const Token& CQSParser::Consume(TokenType type, const std::string& message)
    {
        if (Check(type)) return Advance();
        Error(Peek(), message);
        return Peek(); // Return dummy or handle accordingly
    }

    void CQSParser::Error(const Token& token, const std::string& message)
    {
        m_HasError = true;
        std::string err = "Line " + std::to_string(token.Line) + " at '" + token.Lexeme + "': " + message;
        m_Errors.push_back(err);
    }

    void CQSParser::Synchronize()
    {
        Advance();
        while (!IsAtEnd())
        {
            if (Previous().Type == TokenType::Semicolon) return;

            switch (Peek().Type)
            {
                case TokenType::Class:
                case TokenType::Script:
                case TokenType::Func:
                case TokenType::Var:
                case TokenType::For:
                case TokenType::If:
                case TokenType::While:
                case TokenType::Return:
                    return;
                default: break;
            }
            Advance();
        }
    }

    ASTNodePtr CQSParser::ParseProgram()
    {
        auto program = std::make_shared<ProgramNode>();
        while (!IsAtEnd())
        {
            program->Statements.push_back(ParseDeclaration());
        }
        return program;
    }

    ASTNodePtr CQSParser::ParseDeclaration()
    {
        try
        {
            if (Match(TokenType::Using)) return ParseUsingStatement();
            if (Match(TokenType::Namespace)) return ParseNamespaceDeclaration();

            std::vector<std::string> attributes = ParseAttributes();
            bool isPublic = Match(TokenType::Public);
            bool isPrivate = Match(TokenType::Private);
            bool isStatic = Match(TokenType::Static);
            
            if (Match(TokenType::Script)) return ParseScriptDeclaration(isPublic);
            if (Match(TokenType::Class)) return ParseClassDeclaration(isPublic);
            if (Match(TokenType::Struct)) return ParseStructDeclaration(isPublic);
            if (Match(TokenType::Enum)) return ParseEnumDeclaration(isPublic);
            if (Match(TokenType::Interface)) return ParseInterfaceDeclaration();
            if (Match(TokenType::Func)) return ParseFuncDeclaration(isPublic, isPrivate, isStatic);
            if (Match(TokenType::Var)) return ParseVarDeclaration(isPublic, isStatic, attributes);

            return ParseStatement();
        }
        catch (...)
        {
            Synchronize();
            return nullptr;
        }
    }

    ASTNodePtr CQSParser::ParseUsingStatement()
    {
        Token pathToken = Consume(TokenType::Identifier, "Expect namespace path after 'using'.");
        std::string path = pathToken.Lexeme;
        while (Match(TokenType::Dot))
        {
            path += "." + Consume(TokenType::Identifier, "Expect identifier after '.'. ").Lexeme;
        }
        Consume(TokenType::Semicolon, "Expect ';' after using statement.");
        return std::make_shared<UsingNode>(path, pathToken.Line);
    }

    ASTNodePtr CQSParser::ParseNamespaceDeclaration()
    {
        Token nameToken = Consume(TokenType::Identifier, "Expect namespace name.");
        auto node = std::make_shared<NamespaceNode>(nameToken.Lexeme, nameToken.Line);
        Consume(TokenType::LeftBrace, "Expect '{' before namespace body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            node->Body.push_back(ParseDeclaration());
        }
        Consume(TokenType::RightBrace, "Expect '}' after namespace body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseScriptDeclaration(bool isPublic)
    {
        Token name = Consume(TokenType::Identifier, "Expect script name.");
        auto node = std::make_shared<ScriptDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;

        if (Match(TokenType::Colon))
        {
            node->BaseClass = Consume(TokenType::Identifier, "Expect base class name after ':'. ").Lexeme;
            while (Match(TokenType::Comma))
            {
                node->Interfaces.push_back(Consume(TokenType::Identifier, "Expect interface name.").Lexeme);
            }
        }

        Consume(TokenType::LeftBrace, "Expect '{' before script body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            std::vector<std::string> attributes = ParseAttributes();
            bool mPublic = Match(TokenType::Public);
            bool mPrivate = Match(TokenType::Private);
            bool mStatic = Match(TokenType::Static);
            node->Members.push_back(ParseMember(mPublic, mPrivate, mStatic, attributes));
        }
        Consume(TokenType::RightBrace, "Expect '}' after script body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseMember(bool isPublic, bool isPrivate, bool isStatic, const std::vector<std::string>& attributes)
    {
        if (Match(TokenType::Func)) return ParseFuncDeclaration(isPublic, isPrivate, isStatic);
        if (Match(TokenType::Var)) return ParseVarDeclaration(isPublic, isStatic, attributes);
        
        Error(Peek(), "Expect member declaration.");
        return nullptr;
    }

    ASTNodePtr CQSParser::ParseFuncDeclaration(bool isPublic, bool isPrivate, bool isStatic)
    {
        Token name = Consume(TokenType::Identifier, "Expect function name.");
        auto node = std::make_shared<FuncDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;
        node->IsPrivate = isPrivate;
        node->IsStatic = isStatic;

        Consume(TokenType::LeftParen, "Expect '(' after function name.");
        if (!Check(TokenType::RightParen))
        {
            node->Params = ParseParameters();
        }
        Consume(TokenType::RightParen, "Expect ')' after parameters.");

        if (Match(TokenType::Arrow))
        {
            node->ReturnType = ParseTypeName();
        }

        Consume(TokenType::LeftBrace, "Expect '{' before function body.");
        node->Body = ParseBlock();
        return node;
    }

    ASTNodePtr CQSParser::ParseVarDeclaration(bool isPublic, bool isStatic, const std::vector<std::string>& attributes)
    {
        Token name = Consume(TokenType::Identifier, "Expect variable name.");
        auto node = std::make_shared<VarDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;
        node->IsStatic = isStatic;
        node->Attributes = attributes;

        if (Match(TokenType::Colon))
        {
            node->TypeName = ParseTypeName();
        }

        if (Match(TokenType::Equal))
        {
            node->Initializer = ParseExpression();
        }

        Consume(TokenType::Semicolon, "Expect ';' after variable declaration.");
        return node;
    }

    std::vector<ASTNodePtr> CQSParser::ParseParameters()
    {
        std::vector<ASTNodePtr> params;
        do {
            Token pName = Consume(TokenType::Identifier, "Expect parameter name.");
            std::string pType = "any";
            if (Match(TokenType::Colon))
            {
                pType = ParseTypeName();
            }
            params.push_back(std::make_shared<ParamNode>(pName.Lexeme, pType, pName.Line));
        } while (Match(TokenType::Comma));
        return params;
    }

    std::string CQSParser::ParseTypeName()
    {
        std::string type = Consume(TokenType::Identifier, "Expect type name.").Lexeme;
        if (Match(TokenType::Dot)) // Support nested types?
        {
             // Simple for now
        }
        return type;
    }

    std::vector<std::string> CQSParser::ParseAttributes()
    {
        std::vector<std::string> attrs;
        while (Match(TokenType::LeftBracket))
        {
            attrs.push_back(Consume(TokenType::Identifier, "Expect attribute name.").Lexeme);
            Consume(TokenType::RightBracket, "Expect ']' after attribute.");
        }
        return attrs;
    }

    ASTNodePtr CQSParser::ParseClassDeclaration(bool isPublic)
    {
        Token name = Consume(TokenType::Identifier, "Expect class name.");
        auto node = std::make_shared<ClassDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;
        if (Match(TokenType::Colon))
        {
            node->BaseClass = Consume(TokenType::Identifier, "Expect base class name.").Lexeme;
        }
        Consume(TokenType::LeftBrace, "Expect '{' before class body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            bool mPublic = Match(TokenType::Public);
            bool mPrivate = Match(TokenType::Private);
            bool mStatic = Match(TokenType::Static);
            node->Members.push_back(ParseMember(mPublic, mPrivate, mStatic, {}));
        }
        Consume(TokenType::RightBrace, "Expect '}' after class body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseStructDeclaration(bool isPublic)
    {
        Token name = Consume(TokenType::Identifier, "Expect struct name.");
        auto node = std::make_shared<StructDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;
        Consume(TokenType::LeftBrace, "Expect '{' before struct body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            node->Members.push_back(ParseVarDeclaration(true, false, {}));
        }
        Consume(TokenType::RightBrace, "Expect '}' after struct body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseEnumDeclaration(bool isPublic)
    {
        Token name = Consume(TokenType::Identifier, "Expect enum name.");
        auto node = std::make_shared<EnumDeclNode>(name.Lexeme, name.Line);
        node->IsPublic = isPublic;
        Consume(TokenType::LeftBrace, "Expect '{' before enum body.");
        if (!Check(TokenType::RightBrace))
        {
            do {
                node->Values.push_back(Consume(TokenType::Identifier, "Expect enum value.").Lexeme);
            } while (Match(TokenType::Comma));
        }
        Consume(TokenType::RightBrace, "Expect '}' after enum body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseInterfaceDeclaration()
    {
        Token name = Consume(TokenType::Identifier, "Expect interface name.");
        auto node = std::make_shared<InterfaceDeclNode>(name.Lexeme, name.Line);
        Consume(TokenType::LeftBrace, "Expect '{' before interface body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            Consume(TokenType::Func, "Expect 'func' in interface.");
            node->Methods.push_back(ParseFuncDeclaration(true, false, false));
        }
        Consume(TokenType::RightBrace, "Expect '}' after interface body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseStatement()
    {
        if (Match(TokenType::LeftBrace)) return ParseBlock();
        if (Match(TokenType::If)) return ParseIfStatement();
        if (Match(TokenType::While)) return ParseWhileStatement();
        if (Match(TokenType::For)) return ParseForStatement();
        if (Match(TokenType::Foreach)) return ParseForeachStatement();
        if (Match(TokenType::Switch)) return ParseSwitchStatement();
        if (Match(TokenType::Return)) return ParseReturnStatement();
        if (Match(TokenType::Break)) return ParseBreakStatement();
        if (Match(TokenType::Continue)) return ParseContinueStatement();
        if (Match(TokenType::Yield)) return ParseYieldStatement();
        if (Match(TokenType::Try)) return ParseTryCatchStatement();
        if (Match(TokenType::Throw)) return ParseThrowStatement();

        return ParseExpressionStatement();
    }

    ASTNodePtr CQSParser::ParseBlock()
    {
        auto node = std::make_shared<BlockNode>(Previous().Line);
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            node->Statements.push_back(ParseDeclaration());
        }
        Consume(TokenType::RightBrace, "Expect '}' after block.");
        return node;
    }

    ASTNodePtr CQSParser::ParseIfStatement()
    {
        auto node = std::make_shared<IfNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expect '(' after 'if'.");
        node->Condition = ParseExpression();
        Consume(TokenType::RightParen, "Expect ')' after condition.");
        node->ThenBranch = ParseStatement();
        if (Match(TokenType::Else))
        {
            node->ElseBranch = ParseStatement();
        }
        return node;
    }

    ASTNodePtr CQSParser::ParseWhileStatement()
    {
        auto node = std::make_shared<WhileNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expect '(' after 'while'.");
        node->Condition = ParseExpression();
        Consume(TokenType::RightParen, "Expect ')' after condition.");
        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseForStatement()
    {
        auto node = std::make_shared<ForNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expect '(' after 'for'.");
        if (Match(TokenType::Var))
        {
            node->Initializer = ParseVarDeclaration(false, false, {});
        }
        else if (!Match(TokenType::Semicolon))
        {
            node->Initializer = ParseExpressionStatement();
        }

        if (!Check(TokenType::Semicolon))
        {
            node->Condition = ParseExpression();
        }
        Consume(TokenType::Semicolon, "Expect ';' after loop condition.");

        if (!Check(TokenType::RightParen))
        {
            node->Increment = ParseExpression();
        }
        Consume(TokenType::RightParen, "Expect ')' after for clauses.");

        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseForeachStatement()
    {
        auto node = std::make_shared<ForeachNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expect '(' after 'foreach'.");
        if (Match(TokenType::Var))
        {
            node->VarName = Consume(TokenType::Identifier, "Expect variable name.").Lexeme;
            if (Match(TokenType::Colon))
            {
                node->VarType = ParseTypeName();
            }
        }
        else
        {
             node->VarName = Consume(TokenType::Identifier, "Expect variable name.").Lexeme;
        }
        Consume(TokenType::In, "Expect 'in' after variable.");
        node->Iterable = ParseExpression();
        Consume(TokenType::RightParen, "Expect ')' after foreach clauses.");
        node->Body = ParseStatement();
        return node;
    }

    ASTNodePtr CQSParser::ParseSwitchStatement()
    {
        auto node = std::make_shared<SwitchNode>(Previous().Line);
        Consume(TokenType::LeftParen, "Expect '(' after 'switch'.");
        node->Expression = ParseExpression();
        Consume(TokenType::RightParen, "Expect ')' after switch expression.");
        Consume(TokenType::LeftBrace, "Expect '{' before switch body.");
        while (!Check(TokenType::RightBrace) && !IsAtEnd())
        {
            if (Match(TokenType::Case))
            {
                auto cNode = std::make_shared<CaseNode>(Previous().Line);
                cNode->Value = ParseExpression();
                Consume(TokenType::Colon, "Expect ':' after case value.");
                while (!Check(TokenType::Case) && !Check(TokenType::Default) && !Check(TokenType::RightBrace))
                {
                    cNode->Body.push_back(ParseStatement());
                }
                node->Cases.push_back(cNode);
            }
            else if (Match(TokenType::Default))
            {
                Consume(TokenType::Colon, "Expect ':' after default.");
                auto dBlock = std::make_shared<BlockNode>(Previous().Line);
                while (!Check(TokenType::Case) && !Check(TokenType::RightBrace))
                {
                    dBlock->Statements.push_back(ParseStatement());
                }
                node->DefaultCase = dBlock;
            }
        }
        Consume(TokenType::RightBrace, "Expect '}' after switch body.");
        return node;
    }

    ASTNodePtr CQSParser::ParseReturnStatement()
    {
        auto node = std::make_shared<ReturnNode>(Previous().Line);
        if (!Check(TokenType::Semicolon))
        {
            node->Value = ParseExpression();
        }
        Consume(TokenType::Semicolon, "Expect ';' after return value.");
        return node;
    }

    ASTNodePtr CQSParser::ParseBreakStatement()
    {
        auto node = std::make_shared<BreakNode>(Previous().Line);
        Consume(TokenType::Semicolon, "Expect ';' after 'break'.");
        return node;
    }

    ASTNodePtr CQSParser::ParseContinueStatement()
    {
        auto node = std::make_shared<ContinueNode>(Previous().Line);
        Consume(TokenType::Semicolon, "Expect ';' after 'continue'.");
        return node;
    }

    ASTNodePtr CQSParser::ParseYieldStatement()
    {
        auto node = std::make_shared<YieldNode>(Previous().Line);
        Consume(TokenType::Return, "Expect 'return' after 'yield'.");
        node->Value = ParseExpression();
        Consume(TokenType::Semicolon, "Expect ';' after yield statement.");
        return node;
    }

    ASTNodePtr CQSParser::ParseTryCatchStatement()
    {
        auto node = std::make_shared<TryCatchNode>(Previous().Line);
        node->TryBody = ParseBlock();
        Consume(TokenType::Catch, "Expect 'catch' after 'try'.");
        Consume(TokenType::LeftParen, "Expect '(' after 'catch'.");
        node->CatchVarName = Consume(TokenType::Identifier, "Expect variable name.").Lexeme;
        if (Match(TokenType::Colon))
        {
            node->CatchTypeName = ParseTypeName();
        }
        Consume(TokenType::RightParen, "Expect ')' after catch variable.");
        node->CatchBody = ParseBlock();
        if (Match(TokenType::Finally))
        {
            node->FinallyBody = ParseBlock();
        }
        return node;
    }

    ASTNodePtr CQSParser::ParseThrowStatement()
    {
        auto node = std::make_shared<ThrowNode>(Previous().Line);
        node->Expression = ParseExpression();
        Consume(TokenType::Semicolon, "Expect ';' after 'throw'.");
        return node;
    }

    ASTNodePtr CQSParser::ParseExpressionStatement()
    {
        auto expr = ParseExpression();
        if (!expr) return nullptr; // Crash prevention
        Consume(TokenType::Semicolon, "Expect ';' after expression.");
        return std::make_shared<ExprStmtNode>(expr, expr->Line);
    }

    ASTNodePtr CQSParser::ParseExpression()
    {
        return ParseAssignment();
    }

    ASTNodePtr CQSParser::ParseAssignment()
    {
        auto expr = ParseTernary();
        if (Match(TokenType::Equal))
        {
            Token equals = Previous();
            auto value = ParseAssignment();
            return std::make_shared<AssignNode>(expr, value, equals.Line);
        }
        if (Match({TokenType::PlusEqual, TokenType::MinusEqual, TokenType::StarEqual, TokenType::SlashEqual}))
        {
            Token op = Previous();
            auto value = ParseAssignment();
            return std::make_shared<CompoundAssignNode>(expr, op.Type, value, op.Line);
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
            node->TrueExpr = ParseExpression();
            Consume(TokenType::Colon, "Expect ':' in ternary expression.");
            node->FalseExpr = ParseTernary();
            return node;
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseNullCoalesce()
    {
        auto expr = ParseOr();
        while (Match(TokenType::QuestionQuestion))
        {
            Token op = Previous();
            auto right = ParseOr();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseOr()
    {
        auto expr = ParseAnd();
        while (Match(TokenType::PipePipe))
        {
            Token op = Previous();
            auto right = ParseAnd();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseAnd()
    {
        auto expr = ParseBitwiseOr();
        while (Match(TokenType::AmpAmp))
        {
            Token op = Previous();
            auto right = ParseBitwiseOr();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseBitwiseOr()
    {
        auto expr = ParseBitwiseXor();
        while (Match(TokenType::Pipe))
        {
            Token op = Previous();
            auto right = ParseBitwiseXor();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseBitwiseXor()
    {
        auto expr = ParseBitwiseAnd();
        while (Match(TokenType::Caret))
        {
            Token op = Previous();
            auto right = ParseBitwiseAnd();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseBitwiseAnd()
    {
        auto expr = ParseEquality();
        while (Match(TokenType::Amp))
        {
            Token op = Previous();
            auto right = ParseEquality();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseEquality()
    {
        auto expr = ParseComparison();
        while (Match({TokenType::EqualEqual, TokenType::BangEqual}))
        {
            Token op = Previous();
            auto right = ParseComparison();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseComparison()
    {
        auto expr = ParseShift();
        while (Match({TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual, TokenType::Is, TokenType::As}))
        {
            Token op = Previous();
            auto right = ParseShift();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseShift()
    {
        auto expr = ParseAddition();
        while (Match({TokenType::LessLess, TokenType::GreaterGreater}))
        {
            Token op = Previous();
            auto right = ParseAddition();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseAddition()
    {
        auto expr = ParseMultiplication();
        while (Match({TokenType::Plus, TokenType::Minus}))
        {
            Token op = Previous();
            auto right = ParseMultiplication();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseMultiplication()
    {
        auto expr = ParseUnary();
        while (Match({TokenType::Star, TokenType::Slash, TokenType::Percent}))
        {
            Token op = Previous();
            auto right = ParseUnary();
            expr = std::make_shared<BinaryNode>(expr, op.Type, right, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseUnary()
    {
        if (Match({TokenType::Bang, TokenType::Minus, TokenType::Tilde, TokenType::PlusPlus, TokenType::MinusMinus}))
        {
            Token op = Previous();
            auto operand = ParseUnary();
            return std::make_shared<UnaryNode>(op.Type, operand, true, op.Line);
        }
        return ParsePostfix();
    }

    ASTNodePtr CQSParser::ParsePostfix()
    {
        auto expr = ParseCall();
        while (Match({TokenType::PlusPlus, TokenType::MinusMinus}))
        {
            Token op = Previous();
            expr = std::make_shared<UnaryNode>(op.Type, expr, false, op.Line);
        }
        return expr;
    }

    ASTNodePtr CQSParser::ParseCall()
    {
        auto expr = ParsePrimary();
        while (true)
        {
            if (Match(TokenType::LeftParen))
            {
                expr = FinishCall(expr);
            }
            else if (Match(TokenType::Dot))
            {
                Token name = Consume(TokenType::Identifier, "Expect member name after '.'.");
                expr = std::make_shared<MemberAccessNode>(expr, name.Lexeme, name.Line);
            }
            else if (Match(TokenType::LeftBracket))
            {
                auto index = ParseExpression();
                Consume(TokenType::RightBracket, "Expect ']' after index.");
                expr = std::make_shared<IndexNode>(expr, index, Previous().Line);
            }
            else
            {
                break;
            }
        }
        return expr;
    }

    ASTNodePtr CQSParser::FinishCall(ASTNodePtr callee)
    {
        auto node = std::make_shared<CallNode>(callee, Previous().Line);
        if (!Check(TokenType::RightParen))
        {
            do {
                node->Arguments.push_back(ParseExpression());
            } while (Match(TokenType::Comma));
        }
        Consume(TokenType::RightParen, "Expect ')' after arguments.");
        return node;
    }

    ASTNodePtr CQSParser::ParsePrimary()
    {
        if (Match(TokenType::False)) return std::make_shared<LiteralNode>(Value::MakeBool(false), Previous().Line);
        if (Match(TokenType::True)) return std::make_shared<LiteralNode>(Value::MakeBool(true), Previous().Line);
        if (Match(TokenType::Null)) return std::make_shared<LiteralNode>(Value::MakeNull(), Previous().Line);
        if (Match(TokenType::This)) return std::make_shared<ThisNode>(Previous().Line);

        if (Match(TokenType::IntLiteral)) return std::make_shared<LiteralNode>(Value::MakeInt(Previous().IntValue), Previous().Line);
        if (Match(TokenType::FloatLiteral)) return std::make_shared<LiteralNode>(Value::MakeFloat(Previous().FloatValue), Previous().Line);
        if (Match(TokenType::StringLiteral))
        {
             return std::make_shared<LiteralNode>(Value::MakeString(Previous().Lexeme), Previous().Line);
        }

        if (Match(TokenType::Identifier)) return std::make_shared<IdentifierNode>(Previous().Lexeme, Previous().Line);

        if (Match(TokenType::LeftParen))
        {
            auto expr = ParseExpression();
            Consume(TokenType::RightParen, "Expect ')' after expression.");
            return expr;
        }

        if (Match(TokenType::LeftBracket)) return ParseListLiteral();
        if (Match(TokenType::LeftBrace)) return ParseMapLiteral();

        if (Match(TokenType::New))
        {
            Token name = Consume(TokenType::Identifier, "Expect class name after 'new'.");
            auto node = std::make_shared<NewNode>(name.Lexeme, name.Line);
            Consume(TokenType::LeftParen, "Expect '(' after class name.");
            if (!Check(TokenType::RightParen))
            {
                do {
                    node->Arguments.push_back(ParseExpression());
                } while (Match(TokenType::Comma));
            }
            Consume(TokenType::RightParen, "Expect ')' after arguments.");
            return node;
        }

        Error(Peek(), "Expect expression.");
        return nullptr;
    }

    ASTNodePtr CQSParser::ParseListLiteral()
    {
        auto node = std::make_shared<ListLiteralNode>(Previous().Line);
        if (!Check(TokenType::RightBracket))
        {
            do {
                node->Elements.push_back(ParseExpression());
            } while (Match(TokenType::Comma));
        }
        Consume(TokenType::RightBracket, "Expect ']' after list literal.");
        return node;
    }

    ASTNodePtr CQSParser::ParseMapLiteral()
    {
        auto node = std::make_shared<MapLiteralNode>(Previous().Line);
        if (!Check(TokenType::RightBrace))
        {
            do {
                ASTNodePtr key;
                if (Match(TokenType::Identifier)) {
                    key = std::make_shared<LiteralNode>(Value::MakeString(Previous().Lexeme), Previous().Line);
                } else {
                    key = ParseExpression();
                }
                Consume(TokenType::Colon, "Expect ':' after map key.");
                auto val = ParseExpression();
                node->Entries.push_back({key, val});
            } while (Match(TokenType::Comma));
        }
        Consume(TokenType::RightBrace, "Expect '}' after map literal.");
        return node;
    }
}

