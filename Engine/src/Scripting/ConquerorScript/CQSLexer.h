#pragma once

#include "CQSToken.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Conqueror::CQS
{
    class CQSLexer
    {
    public:
        CQSLexer() = default;

        // Kaynak kodu tokenize et
        std::vector<Token> Tokenize(const std::string& source);

        // Hata var mı?
        bool HasErrors() const { return m_HasError; }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }

    private:
        // Scanning state
        std::string m_Source;
        int         m_Start   = 0;  // Mevcut token'ın başlangıcı
        int         m_Current = 0;  // Mevcut karakter pozisyonu
        int         m_Line    = 1;  // Satır numarası
        int         m_Column  = 1;  // Sütun numarası

        // Error state
        bool                     m_HasError = false;
        std::vector<std::string> m_Errors;

        // Keyword tablosu
        static std::unordered_map<std::string, TokenType> s_Keywords;

        // ── Scanning Helpers ──
        bool IsAtEnd() const;
        char Advance();
        char Peek() const;
        char PeekNext() const;
        bool Match(char expected);

        // ── Character Classification ──
        bool IsDigit(char c) const;
        bool IsAlpha(char c) const;
        bool IsAlphaNumeric(char c) const;

        // ── Token Creation ──
        Token MakeToken(TokenType type) const;
        Token ErrorToken(const std::string& message);

        // ── Scanning ──
        Token ScanToken();
        Token ScanString();
        Token ScanInterpolatedString();
        Token ScanNumber();
        Token ScanIdentifier();

        // ── Whitespace & Comments ──
        void SkipWhitespace();
    };
}
