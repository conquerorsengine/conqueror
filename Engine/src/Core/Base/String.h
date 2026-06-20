#pragma once

#include "Base.h"
#include <string>
#include <vector>
#include <string_view>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Conqueror::StringUtils
{
    // String manipulation
    CQ_API std::string ToLower(const std::string& str);
    CQ_API std::string ToUpper(const std::string& str);
    CQ_API std::string Trim(const std::string& str);
    CQ_API std::string TrimLeft(const std::string& str);
    CQ_API std::string TrimRight(const std::string& str);
    
    // String queries
    CQ_API bool StartsWith(const std::string& str, const std::string& prefix);
    CQ_API bool EndsWith(const std::string& str, const std::string& suffix);
    CQ_API bool Contains(const std::string& str, const std::string& substr);
    CQ_API int64_t IndexOf(const std::string& str, const std::string& substr);
    CQ_API int64_t LastIndexOf(const std::string& str, const std::string& substr);
    CQ_API bool EqualsIgnoreCase(const std::string& a, const std::string& b);
    CQ_API int Count(const std::string& str, const std::string& substr);
    
    // String splitting/joining
    CQ_API std::vector<std::string> Split(const std::string& str, char delimiter);
    CQ_API std::vector<std::string> Split(const std::string& str, const std::string& delimiter);
    CQ_API std::string Join(const std::vector<std::string>& strings, const std::string& separator);
    
    // String replacement
    CQ_API std::string Replace(const std::string& str, const std::string& from, const std::string& to);
    CQ_API std::string ReplaceAll(const std::string& str, const std::string& from, const std::string& to);
    
    // String manipulation extended
    CQ_API std::string PadLeft(const std::string& str, size_t totalWidth, char paddingChar = ' ');
    CQ_API std::string PadRight(const std::string& str, size_t totalWidth, char paddingChar = ' ');
    CQ_API bool IsAlpha(const std::string& str);
    CQ_API bool IsDigit(const std::string& str);
    CQ_API bool IsAlnum(const std::string& str);
    CQ_API bool IsNumeric(const std::string& str);
    CQ_API bool IsAlphaNumeric(const std::string& str);
    
    // Advanced String Manipulation
    CQ_API std::string Center(const std::string& str, size_t width, char fillchar = ' ');
    CQ_API std::string LJust(const std::string& str, size_t width, char fillchar = ' ');
    CQ_API std::string RJust(const std::string& str, size_t width, char fillchar = ' ');
    CQ_API std::string ExpandTabs(const std::string& str, size_t tabsize = 8);
    CQ_API std::string ZFill(const std::string& str, size_t width);
    CQ_API std::string Slice(const std::string& str, int start, int end);
    CQ_API std::string Substring(const std::string& str, size_t start, size_t length);
    CQ_API std::string Reverse(const std::string& str);
    CQ_API std::string Repeat(const std::string& str, int count);
    CQ_API std::string Capitalize(const std::string& str);
    CQ_API std::string CharAt(const std::string& str, size_t index);
    CQ_API int CharCodeAt(const std::string& str, size_t index);
    CQ_API std::string FromCharCode(int code);
    
    // String extras
    CQ_API std::string Left(const std::string& str, size_t n);
    CQ_API std::string Right(const std::string& str, size_t n);
    CQ_API bool IsWhitespace(const std::string& str);
    CQ_API bool IsLowerCase(const std::string& str);
    CQ_API bool IsUpperCase(const std::string& str);
    CQ_API std::string RemovePrefix(const std::string& str, const std::string& prefix);
    CQ_API std::string RemoveSuffix(const std::string& str, const std::string& suffix);
    CQ_API int CountWords(const std::string& str);
    CQ_API std::string ToTitleCase(const std::string& str);
    CQ_API std::string SwapCase(const std::string& str);
    CQ_API bool IsAscii(const std::string& str);
    CQ_API std::string ReverseWords(const std::string& str);
    
    
    // Format implementation helpers (must be before Format function)
    namespace Detail
    {
        inline void FormatImpl(std::ostringstream& oss, const std::string& format)
        {
            oss << format;
        }
        
        template<typename T, typename... Args>
        void FormatImpl(std::ostringstream& oss, const std::string& format, T&& value, Args&&... args)
        {
            size_t pos = format.find("{}");
            if (pos != std::string::npos)
            {
                oss << format.substr(0, pos) << value;
                FormatImpl(oss, format.substr(pos + 2), std::forward<Args>(args)...);
            }
            else
            {
                oss << format;
            }
        }
    }
    
    // String formatting
    template<typename... Args>
    std::string Format(const std::string& format, Args&&... args)
    {
        std::ostringstream oss;
        Detail::FormatImpl(oss, format, std::forward<Args>(args)...);
        return oss.str();
    }
    
    // Number conversion
    CQ_API int ToInt(const std::string& str, int defaultValue = 0);
    CQ_API float ToFloat(const std::string& str, float defaultValue = 0.0f);
    CQ_API double ToDouble(const std::string& str, double defaultValue = 0.0);
    CQ_API bool ToBool(const std::string& str, bool defaultValue = false);
    
    CQ_API std::string ToString(int value);
    CQ_API std::string ToString(float value, int precision = 2);
    CQ_API std::string ToString(double value, int precision = 2);
    CQ_API std::string ToString(bool value);
    
    // Base conversion
    CQ_API int64_t BinaryToDecimal(const std::string& str);
    CQ_API std::string DecimalToBinary(int64_t val);
    CQ_API int64_t HexToDecimal(const std::string& str);
    CQ_API std::string DecimalToHex(int64_t val);
    
    // Path utilities
    CQ_API std::string GetFileName(const std::string& path);
    CQ_API std::string GetFileExtension(const std::string& path);
    CQ_API std::string GetFileNameWithoutExtension(const std::string& path);
    CQ_API std::string GetDirectoryPath(const std::string& path);
    CQ_API std::string NormalizePath(const std::string& path);
    
    // Hash
    CQ_API uint32_t Hash(const std::string& str);
    CQ_API uint64_t Hash64(const std::string& str);
    
    // String builder (efficient string concatenation)
    class CQ_API StringBuilder
    {
    public:
        StringBuilder() = default;
        
        StringBuilder& Append(const std::string& str);
        StringBuilder& Append(const char* str);
        StringBuilder& Append(char c);
        StringBuilder& Append(int value);
        StringBuilder& Append(float value);
        StringBuilder& Append(double value);
        StringBuilder& Append(bool value);
        
        StringBuilder& AppendLine(const std::string& str = "");
        StringBuilder& AppendFormat(const std::string& format, ...);
        
        void Clear();
        std::string ToString() const;
        size_t Length() const { return m_Buffer.length(); }
        bool IsEmpty() const { return m_Buffer.empty(); }
        
    private:
        std::string m_Buffer;
    };

    // String view utilities
    CQ_API std::string_view TrimView(std::string_view str);
    CQ_API std::vector<std::string_view> SplitView(std::string_view str, char delimiter);
    
    // Wide string conversion
    CQ_API std::wstring ToWideString(const std::string& str);
    CQ_API std::string FromWideString(const std::wstring& wstr);
    
    // UTF-8 utilities
    CQ_API size_t UTF8Length(const std::string& str);
    CQ_API std::string UTF8Substring(const std::string& str, size_t start, size_t length);
    CQ_API bool IsValidUTF8(const std::string& str);
}
