#pragma once
#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::URLUtils
{
    CQ_API std::string Encode(const std::string& src);
    CQ_API std::string Decode(const std::string& src);
    
    CQ_API std::string GetProtocol(const std::string& url);
    CQ_API std::string GetHost(const std::string& url);
    CQ_API std::string GetPath(const std::string& url);
    CQ_API std::string GetQuery(const std::string& url);
}
