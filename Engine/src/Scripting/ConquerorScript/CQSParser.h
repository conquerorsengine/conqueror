#pragma once

#include "CQSToken.h"
#include "CQSAST.h"
#include <vector>
#include <string>

namespace Conqueror::CQS
{
    class CQSParser
    {
    public:
        CQSParser() = default;

        // Token dizisini parse et, AST döndür
        ASTNodePtr Parse(const std::vector<Token>& tokens);

        bool HasErrors() const { return m_HasError; }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }

    private:
        std::vector<Token>       m_Tokens;
        int                      m_Current = 0;
        bool                     m_HasError = false;
        std::vector<std::string> m_Errors;

        // ── Token Navigation ──
        const Token& Peek() const;
        const Token& Previous() const;
        const Token& Advance();
        bool         IsAtEnd() const;
        bool         Check(TokenType type) const;
        bool         Match(TokenType type);
        bool         Match(std::initializer_list<TokenType> types);
        const Token& Consume(TokenType type, const std::string& message);

        // ── Error Handling ──
        void Error(const Token& token, const std::string& message);
        void Synchronize(); // Hata recovery

        // ── Top-Level Parsing ──
        ASTNodePtr ParseProgram();
        ASTNodePtr ParseDeclaration();
        ASTNodePtr ParseUsingStatement();
        ASTNodePtr ParseNamespaceDeclaration();

        // ── Type Declarations ──
        ASTNodePtr ParseScriptDeclaration(bool isPublic);
        ASTNodePtr ParseClassDeclaration(bool isPublic);
        ASTNodePtr ParseStructDeclaration(bool isPublic);
        ASTNodePtr ParseEnumDeclaration(bool isPublic);
        ASTNodePtr ParseInterfaceDeclaration();

        // ── Members ──
        ASTNodePtr ParseMember(bool isPublic, bool isPrivate, bool isStatic,
                               const std::vector<std::string>& attributes);
        ASTNodePtr ParseFuncDeclaration(bool isPublic, bool isPrivate, bool isStatic);
        ASTNodePtr ParseVarDeclaration(bool isPublic, bool isStatic,
                                       const std::vector<std::string>& attributes);
        std::vector<ASTNodePtr> ParseParameters();
        std::string ParseTypeName();

        // ── Attributes ──
        std::vector<std::string> ParseAttributes();

        // ── Statements ──
        ASTNodePtr ParseStatement();
        ASTNodePtr ParseBlock();
        ASTNodePtr ParseIfStatement();
        ASTNodePtr ParseWhileStatement();
        ASTNodePtr ParseForStatement();
        ASTNodePtr ParseForeachStatement();
        ASTNodePtr ParseSwitchStatement();
        ASTNodePtr ParseReturnStatement();
        ASTNodePtr ParseBreakStatement();
        ASTNodePtr ParseContinueStatement();
        ASTNodePtr ParseYieldStatement();
        ASTNodePtr ParseTryCatchStatement();
        ASTNodePtr ParseThrowStatement();
        ASTNodePtr ParseExpressionStatement();

        // ── Expressions (Pratt Parser / Precedence Climbing) ──
        ASTNodePtr ParseExpression();
        ASTNodePtr ParseAssignment();
        ASTNodePtr ParseTernary();
        ASTNodePtr ParseNullCoalesce();
        ASTNodePtr ParseOr();
        ASTNodePtr ParseAnd();
        ASTNodePtr ParseBitwiseOr();
        ASTNodePtr ParseBitwiseXor();
        ASTNodePtr ParseBitwiseAnd();
        ASTNodePtr ParseEquality();
        ASTNodePtr ParseComparison();
        ASTNodePtr ParseShift();
        ASTNodePtr ParseAddition();
        ASTNodePtr ParseMultiplication();
        ASTNodePtr ParseUnary();
        ASTNodePtr ParsePostfix();
        ASTNodePtr ParseCall();
        ASTNodePtr ParsePrimary();

        // ── Expression Helpers ──
        ASTNodePtr ParseListLiteral();
        ASTNodePtr ParseMapLiteral();
        ASTNodePtr FinishCall(ASTNodePtr callee);
    };
}
