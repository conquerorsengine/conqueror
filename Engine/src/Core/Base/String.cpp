#include "String.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace Conqueror::StringUtils
{
    std::string ToLower(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::string ToUpper(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    std::string Trim(const std::string& str)
    {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        
        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }

    std::string TrimLeft(const std::string& str)
    {
        size_t start = str.find_first_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : str.substr(start);
    }

    std::string TrimRight(const std::string& str)
    {
        size_t end = str.find_last_not_of(" \t\n\r");
        return (end == std::string::npos) ? "" : str.substr(0, end + 1);
    }

    bool StartsWith(const std::string& str, const std::string& prefix)
    {
        return str.size() >= prefix.size() && 
               str.compare(0, prefix.size(), prefix) == 0;
    }

    bool EndsWith(const std::string& str, const std::string& suffix)
    {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool Contains(const std::string& str, const std::string& substr)
    {
        return str.find(substr) != std::string::npos;
    }

    int64_t IndexOf(const std::string& str, const std::string& substr)
    {
        size_t pos = str.find(substr);
        return (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);
    }

    int64_t LastIndexOf(const std::string& str, const std::string& substr)
    {
        size_t pos = str.rfind(substr);
        return (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);
    }

    int Count(const std::string& str, const std::string& substr)
    {
        if (substr.empty()) return 0;
        int count = 0;
        size_t pos = 0;
        while ((pos = str.find(substr, pos)) != std::string::npos) { count++; pos += substr.length(); }
        return count;
    }

    bool EqualsIgnoreCase(const std::string& a, const std::string& b)
    {
        return ToLower(a) == ToLower(b);
    }

    std::vector<std::string> Split(const std::string& str, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        
        while (std::getline(ss, item, delimiter))
        {
            result.push_back(item);
        }
        
        return result;
    }

    std::vector<std::string> Split(const std::string& str, const std::string& delimiter)
    {
        std::vector<std::string> result;
        if (delimiter.empty()) {
            for (char c : str) {
                result.push_back(std::string(1, c));
            }
            return result;
        }
        
        size_t start = 0;
        size_t end = str.find(delimiter);
        
        while (end != std::string::npos)
        {
            result.push_back(str.substr(start, end - start));
            start = end + delimiter.length();
            end = str.find(delimiter, start);
        }
        
        result.push_back(str.substr(start));
        return result;
    }

    std::string Join(const std::vector<std::string>& strings, const std::string& separator)
    {
        if (strings.empty()) return "";
        
        std::string result = strings[0];
        for (size_t i = 1; i < strings.size(); ++i)
        {
            result += separator + strings[i];
        }
        
        return result;
    }

    std::string Replace(const std::string& str, const std::string& from, const std::string& to)
    {
        std::string result = str;
        size_t pos = result.find(from);
        
        if (pos != std::string::npos)
        {
            result.replace(pos, from.length(), to);
        }
        
        return result;
    }

    std::string ReplaceAll(const std::string& str, const std::string& from, const std::string& to)
    {
        std::string result = str;
        size_t pos = 0;
        
        while ((pos = result.find(from, pos)) != std::string::npos)
        {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        
        return result;
    }

    std::string PadLeft(const std::string& str, size_t totalWidth, char paddingChar)
    {
        if (str.length() >= totalWidth) return str;
        return std::string(totalWidth - str.length(), paddingChar) + str;
    }

    std::string PadRight(const std::string& str, size_t totalWidth, char paddingChar)
    {
        if (str.length() >= totalWidth) return str;
        return str + std::string(totalWidth - str.length(), paddingChar);
    }

    bool IsAlpha(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (!std::isalpha(static_cast<unsigned char>(c))) return false;
        return true;
    }

    bool IsDigit(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    }

    bool IsAlnum(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (!std::isalnum(static_cast<unsigned char>(c))) return false;
        return true;
    }

    bool IsNumeric(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    }

    bool IsAlphaNumeric(const std::string& str)
    {
        return IsAlnum(str);
    }

    std::string Center(const std::string& str, size_t width, char fillchar)
    {
        if (width <= str.length()) return str;
        size_t pad = width - str.length();
        size_t left = pad / 2;
        size_t right = pad - left;
        return std::string(left, fillchar) + str + std::string(right, fillchar);
    }

    std::string LJust(const std::string& str, size_t width, char fillchar)
    {
        if (width <= str.length()) return str;
        return str + std::string(width - str.length(), fillchar);
    }

    std::string RJust(const std::string& str, size_t width, char fillchar)
    {
        if (width <= str.length()) return str;
        return std::string(width - str.length(), fillchar) + str;
    }

    std::string ExpandTabs(const std::string& str, size_t tabsize)
    {
        std::string res;
        for (char c : str) {
            if (c == '\t') res += std::string(tabsize, ' ');
            else res += c;
        }
        return res;
    }

    std::string ZFill(const std::string& str, size_t width)
    {
        if (width <= str.length()) return str;
        return std::string(width - str.length(), '0') + str;
    }

    std::string Slice(const std::string& str, int start, int end)
    {
        if (start < 0) start = 0;
        if (end > static_cast<int>(str.length())) end = static_cast<int>(str.length());
        if (start >= end) return "";
        return str.substr(start, end - start);
    }

    std::string Reverse(const std::string& str)
    {
        std::string result = str;
        std::reverse(result.begin(), result.end());
        return result;
    }

    std::string Substring(const std::string& str, size_t start, size_t length)
    {
        if (start >= str.length()) return "";
        return str.substr(start, length);
    }

    std::string Repeat(const std::string& str, int count)
    {
        std::string result;
        for (int i = 0; i < count; ++i) result += str;
        return result;
    }

    std::string Capitalize(const std::string& str)
    {
        std::string result = str;
        if (!result.empty()) result[0] = std::toupper(result[0]);
        return result;
    }

    std::string CharAt(const std::string& str, size_t index)
    {
        if (index >= str.length()) return "";
        return std::string(1, str[index]);
    }

    int CharCodeAt(const std::string& str, size_t index)
    {
        if (index >= str.length()) return 0;
        return (int)(unsigned char)str[index];
    }

    std::string FromCharCode(int code)
    {
        return std::string(1, (char)code);
    }
    
    std::string Left(const std::string& str, size_t n)
    {
        return str.substr(0, std::min(n, str.length()));
    }
    
    std::string Right(const std::string& str, size_t n)
    {
        return str.substr(str.length() - std::min(n, str.length()));
    }
    
    bool IsWhitespace(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (!std::isspace(c)) return false;
        return true;
    }
    
    bool IsLowerCase(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (std::isalpha(c) && !std::islower(c)) return false;
        return true;
    }
    
    bool IsUpperCase(const std::string& str)
    {
        if (str.empty()) return false;
        for (char c : str) if (std::isalpha(c) && !std::isupper(c)) return false;
        return true;
    }
    
    std::string RemovePrefix(const std::string& str, const std::string& prefix)
    {
        if (str.rfind(prefix, 0) == 0) return str.substr(prefix.length());
        return str;
    }
    
    std::string RemoveSuffix(const std::string& str, const std::string& suffix)
    {
        if (str.length() >= suffix.length() && str.substr(str.length() - suffix.length()) == suffix)
            return str.substr(0, str.length() - suffix.length());
        return str;
    }
    
    int CountWords(const std::string& str)
    {
        int count = 0; bool inWord = false;
        for (char c : str) {
            if (std::isspace(c)) inWord = false;
            else if (!inWord) { inWord = true; count++; }
        }
        return count;
    }
    
    std::string ToTitleCase(const std::string& str)
    {
        std::string s = str;
        bool nextIsUpper = true;
        for (char& c : s) {
            if (std::isspace(c)) nextIsUpper = true;
            else if (nextIsUpper) { c = std::toupper(c); nextIsUpper = false; }
            else c = std::tolower(c);
        }
        return s;
    }
    
    std::string SwapCase(const std::string& str)
    {
        std::string s = str;
        for (char& c : s) {
            if (std::isupper(c)) c = std::tolower(c);
            else if (std::islower(c)) c = std::toupper(c);
        }
        return s;
    }
    
    bool IsAscii(const std::string& str)
    {
        for (unsigned char c : str) if (c > 127) return false;
        return true;
    }
    
    std::string ReverseWords(const std::string& str)
    {
        std::vector<std::string> words;
        std::string word;
        for (char c : str) {
            if (std::isspace(c)) { if (!word.empty()) words.push_back(word); word.clear(); }
            else word += c;
        }
        if (!word.empty()) words.push_back(word);
        std::reverse(words.begin(), words.end());
        std::string res;
        for (size_t i = 0; i < words.size(); ++i) { res += words[i]; if (i < words.size() - 1) res += " "; }
        return res;
    }

    int ToInt(const std::string& str, int defaultValue)
    {
        try
        {
            return std::stoi(str);
        }
        catch (...)
        {
            return defaultValue;
        }
    }

    float ToFloat(const std::string& str, float defaultValue)
    {
        try
        {
            return std::stof(str);
        }
        catch (...)
        {
            return defaultValue;
        }
    }

    double ToDouble(const std::string& str, double defaultValue)
    {
        try
        {
            return std::stod(str);
        }
        catch (...)
        {
            return defaultValue;
        }
    }

    bool ToBool(const std::string& str, bool defaultValue)
    {
        std::string lower = ToLower(str);
        if (lower == "true" || lower == "1" || lower == "yes")
            return true;
        if (lower == "false" || lower == "0" || lower == "no")
            return false;
        return defaultValue;
    }

    std::string ToString(int value)
    {
        return std::to_string(value);
    }

    std::string ToString(float value, int precision)
    {
        std::ostringstream oss;
        oss.precision(precision);
        oss << std::fixed << value;
        return oss.str();
    }

    std::string ToString(double value, int precision)
    {
        std::ostringstream oss;
        oss.precision(precision);
        oss << std::fixed << value;
        return oss.str();
    }

    std::string ToString(bool value)
    {
        return value ? "true" : "false";
    }

    int64_t BinaryToDecimal(const std::string& str) {
        return std::strtoll(str.c_str(), nullptr, 2);
    }
    
    std::string DecimalToBinary(int64_t val) {
        if (val == 0) return "0";
        std::string res = "";
        while (val > 0) { res += (val % 2 == 0 ? "0" : "1"); val /= 2; }
        std::reverse(res.begin(), res.end());
        return res;
    }
    
    int64_t HexToDecimal(const std::string& str) {
        return std::strtoll(str.c_str(), nullptr, 16);
    }
    
    std::string DecimalToHex(int64_t val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)val);
        return std::string(buf);
    }

    std::string GetFileName(const std::string& path)
    {
        size_t pos = path.find_last_of("/\\");
        return (pos == std::string::npos) ? path : path.substr(pos + 1);
    }

    std::string GetFileExtension(const std::string& path)
    {
        size_t pos = path.find_last_of('.');
        return (pos == std::string::npos) ? "" : path.substr(pos);
    }

    std::string GetFileNameWithoutExtension(const std::string& path)
    {
        std::string filename = GetFileName(path);
        size_t pos = filename.find_last_of('.');
        return (pos == std::string::npos) ? filename : filename.substr(0, pos);
    }

    std::string GetDirectoryPath(const std::string& path)
    {
        size_t pos = path.find_last_of("/\\");
        return (pos == std::string::npos) ? "" : path.substr(0, pos);
    }

    std::string NormalizePath(const std::string& path)
    {
        std::string result = path;
        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
    }

    uint32_t Hash(const std::string& str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    uint64_t Hash64(const std::string& str)
    {
        uint64_t hash = 14695981039346656037ull;
        for (char c : str)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // StringBuilder implementation
    StringBuilder& StringBuilder::Append(const std::string& str)
    {
        m_Buffer += str;
        return *this;
    }

    StringBuilder& StringBuilder::Append(const char* str)
    {
        m_Buffer += str;
        return *this;
    }

    StringBuilder& StringBuilder::Append(char c)
    {
        m_Buffer += c;
        return *this;
    }

    StringBuilder& StringBuilder::Append(int value)
    {
        m_Buffer += std::to_string(value);
        return *this;
    }

    StringBuilder& StringBuilder::Append(float value)
    {
        m_Buffer += std::to_string(value);
        return *this;
    }

    StringBuilder& StringBuilder::Append(double value)
    {
        m_Buffer += std::to_string(value);
        return *this;
    }

    StringBuilder& StringBuilder::Append(bool value)
    {
        m_Buffer += value ? "true" : "false";
        return *this;
    }

    StringBuilder& StringBuilder::AppendLine(const std::string& str)
    {
        m_Buffer += str + "\n";
        return *this;
    }

    StringBuilder& StringBuilder::AppendFormat(const std::string& format, ...)
    {
        // Basit implementation - variadic args kullanmıyoruz
        m_Buffer += format;
        return *this;
    }

    void StringBuilder::Clear()
    {
        m_Buffer.clear();
    }

    std::string StringBuilder::ToString() const
    {
        return m_Buffer;
    }

    // String view utilities (stub implementations)
    std::string_view TrimView(std::string_view str)
    {
        size_t start = 0;
        while (start < str.size() && std::isspace(str[start])) ++start;
        
        size_t end = str.size();
        while (end > start && std::isspace(str[end - 1])) --end;
        
        return str.substr(start, end - start);
    }

    std::vector<std::string_view> SplitView(std::string_view str, char delimiter)
    {
        std::vector<std::string_view> result;
        size_t start = 0;
        size_t end = str.find(delimiter);
        
        while (end != std::string_view::npos)
        {
            result.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delimiter, start);
        }
        
        result.push_back(str.substr(start));
        return result;
    }

    // Wide string conversion (stub)
    std::wstring ToWideString(const std::string& str)
    {
        return std::wstring(str.begin(), str.end());
    }

    std::string FromWideString(const std::wstring& wstr)
    {
        return std::string(wstr.begin(), wstr.end());
    }

    // UTF-8 utilities (stub)
    size_t UTF8Length(const std::string& str)
    {
        return str.length(); // Basit implementation
    }

    std::string UTF8Substring(const std::string& str, size_t start, size_t length)
    {
        return str.substr(start, length); // Basit implementation
    }

    bool IsValidUTF8(const std::string& str)
    {
        return true; // Basit implementation
    }
}
