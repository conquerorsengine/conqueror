#pragma once

#include "CQSToken.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::CQS
{
    class CQSLexer
    {
    public:
        CQSLexer() = default;

        // Kaynak kodu token'lara ayır (sonda her zaman EndOfFile bulunur)
        std::vector<Token> Tokenize(const std::string& source, int firstLine = 1);

        bool HasErrors() const { return !m_Errors.empty(); }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }

        // "\n", "\t", "ç" gibi kaçış dizilerini çöz (hata varsa false)
        static bool Unescape(const std::string& raw, std::string& out, std::string& error);
        static bool IsKeyword(const std::string& word);
        static const std::unordered_map<std::string, TokenType>& Keywords();

    private:
        std::string m_Source;
        size_t      m_Start = 0;
        size_t      m_Current = 0;
        int         m_Line = 1;
        int         m_Column = 1;
        int         m_StartLine = 1;
        int         m_StartColumn = 1;
        std::vector<std::string> m_Errors;

        bool IsAtEnd() const { return m_Current >= m_Source.size(); }
        char Advance();
        char Peek() const { return IsAtEnd() ? '\0' : m_Source[m_Current]; }
        char PeekNext() const { return m_Current + 1 >= m_Source.size() ? '\0' : m_Source[m_Current + 1]; }
        bool Match(char expected);

        Token MakeToken(TokenType type) const;
        Token ErrorToken(const std::string& message);

        Token ScanToken();
        Token ScanString(char quote);
        Token ScanVerbatimString();
        Token ScanInterpolatedString();
        Token ScanNumber(char first);
        Token ScanIdentifier();
        bool  SkipWhitespace();
    };
}
