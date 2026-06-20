#include "RegexUtils.h"
#include <regex>

namespace Conqueror::RegexUtils
{
    bool IsMatch(const std::string& input, const std::string& pattern)
    {
        try {
            std::regex re(pattern);
            return std::regex_match(input, re);
        } catch (...) {
            return false;
        }
    }

    bool Search(const std::string& input, const std::string& pattern)
    {
        try {
            std::regex re(pattern);
            return std::regex_search(input, re);
        } catch (...) {
            return false;
        }
    }

    std::string Replace(const std::string& input, const std::string& pattern, const std::string& replacement)
    {
        try {
            std::regex re(pattern);
            return std::regex_replace(input, re, replacement);
        } catch (...) {
            return input;
        }
    }

    std::string Extract(const std::string& input, const std::string& pattern)
    {
        try {
            std::regex re(pattern);
            std::smatch match;
            if(std::regex_search(input, match, re)) {
                if(match.size() > 1) return match[1].str();
                return match[0].str();
            }
        } catch(...) {}
        return "";
    }
}
