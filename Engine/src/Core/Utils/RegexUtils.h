#pragma once
#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::RegexUtils
{
    CQ_API bool IsMatch(const std::string& input, const std::string& pattern);
    CQ_API bool Search(const std::string& input, const std::string& pattern);
    CQ_API std::string Replace(const std::string& input, const std::string& pattern, const std::string& replacement);
    CQ_API std::string Extract(const std::string& input, const std::string& pattern);
}
