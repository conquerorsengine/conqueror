#pragma once

#include "CQSAST.h"
#include "CQSToken.h"

#include <string>
#include <vector>

namespace Conqueror::CQS
{
    // Özyinelemeli iniş ayrıştırıcısı. Hatalarda senkronize olup devam eder
    // (birden çok hata tek seferde raporlanır), her hata durumunda ilerleme garanti edilir.
    class CQSParser
    {
    public:
        CQSParser() = default;

        ASTNodePtr Parse(const std::vector<Token>& tokens);
        // Tek bir ifade (string interpolation, konsol): tüm token'lar tüketilmeli
        ASTNodePtr ParseSingleExpression(const std::vector<Token>& tokens);

        bool HasErrors() const { return !m_Errors.empty(); }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }

    private:
        struct ParseError {};

        std::vector<Token> m_Tokens;
        size_t m_Current = 0;
        std::vector<std::string> m_Errors;
        int m_FunctionDepth = 0;

        // ── Token helpers ──
        const Token& Peek(size_t ahead = 0) const;
        const Token& Previous() const;
        const Token& Advance();
        bool IsAtEnd() const;
        bool Check(TokenType type) const;
        bool CheckAhead(size_t ahead, TokenType type) const;
        bool Match(TokenType type);
        bool Match(std::initializer_list<TokenType> types);
        const Token& Consume(TokenType type, const std::string& message);
        std::string ConsumeIdentifier(const std::string& message);
        [[noreturn]] void ErrorAt(const Token& token, const std::string& message);
        void Report(const Token& token, const std::string& message);
        void Synchronize();

        // ── Declarations ──
        ASTNodePtr ParseDeclaration();
        ASTNodePtr ParseUsing();
        ASTNodePtr ParseNamespace();
        ASTNodePtr ParseClass(NodeType type, bool isPublic);
        ASTNodePtr ParseMember(const std::string& className);
        ASTNodePtr ParseEnum(bool isPublic);
        ASTNodePtr ParseInterface();
        ASTNodePtr ParseFunction(const std::string& name, int line, bool isPublic, bool isPrivate, bool isStatic, const std::string& returnType);
        ASTNodePtr ParseVar(bool isPublic, bool isPrivate, bool isStatic, const std::vector<std::string>& attributes, const std::string& typeName = "");
        ASTNodePtr ParseTypedVar(const std::string& typeName, const std::string& name, int line, bool isPublic, bool isPrivate, bool isStatic, const std::vector<std::string>& attributes);
        std::vector<ASTNodePtr> ParseParameters();
        std::vector<std::string> ParseAttributes();
        std::string ParseTypeName();
        bool TryParseTypeName(std::string& out);      // geri sarabilir
        bool LooksLikeTypedDeclaration(size_t& nameIndex) const;

        // ── Statements ──
        ASTNodePtr ParseStatement();
        ASTNodePtr ParseBlock();
        ASTNodePtr ParseIf();
        ASTNodePtr ParseWhile();
        ASTNodePtr ParseDoWhile();
        ASTNodePtr ParseFor();
        ASTNodePtr ParseForeach(int line, bool hadParen);
        ASTNodePtr ParseSwitch();
        ASTNodePtr ParseReturn();
        ASTNodePtr ParseTry();
        ASTNodePtr ParseThrow();
        ASTNodePtr ParseYield();
        ASTNodePtr ParseExpressionStatement();

        // ── Expressions ──
        ASTNodePtr ParseExpression();
        ASTNodePtr ParseAssignment();
        ASTNodePtr ParseTernary();
        ASTNodePtr ParseNullCoalesce();
        ASTNodePtr ParseOr();
        ASTNodePtr ParseAnd();
        ASTNodePtr ParseBitOr();
        ASTNodePtr ParseBitXor();
        ASTNodePtr ParseBitAnd();
        ASTNodePtr ParseEquality();
        ASTNodePtr ParseRelational();
        ASTNodePtr ParseRange();
        ASTNodePtr ParseShift();
        ASTNodePtr ParseAdditive();
        ASTNodePtr ParseMultiplicative();
        ASTNodePtr ParseUnary();
        ASTNodePtr ParsePostfix();
        ASTNodePtr ParsePrimary();
        ASTNodePtr FinishCall(ASTNodePtr callee, bool nullConditional);
        ASTNodePtr ParseLambda(int line);
        ASTNodePtr ParseListLiteral();
        ASTNodePtr ParseMapLiteral();
        ASTNodePtr ParseInterpolated(const Token& token);
        bool IsLambdaAhead() const;
        bool IsGenericCallAhead(std::vector<std::string>& genericArgs, size_t& endIndex) const;
        bool IsCastAhead() const;
    };
}
