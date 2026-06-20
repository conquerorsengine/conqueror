#include "URLUtils.h"
#include <cctype>
#include <cstdio>

namespace Conqueror::URLUtils
{
    std::string Encode(const std::string& src)
    {
        std::string escaped;
        for (char c : src) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') escaped += c;
            else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                escaped += buf;
            }
        }
        return escaped;
    }

    std::string Decode(const std::string& src)
    {
        std::string ret;
        for (size_t i = 0; i < src.length(); ++i) {
            if (src[i] == '%') {
                if (i + 2 < src.length()) {
                    unsigned int val;
                    if (sscanf(src.substr(i + 1, 2).c_str(), "%x", &val) == 1) {
                        ret += (char)val;
                        i += 2;
                    } else ret += '%';
                } else ret += '%';
            } else if (src[i] == '+') ret += ' ';
            else ret += src[i];
        }
        return ret;
    }

    std::string GetProtocol(const std::string& url)
    {
        size_t pos = url.find("://");
        if (pos != std::string::npos) return url.substr(0, pos);
        return "";
    }

    std::string GetHost(const std::string& url)
    {
        size_t start = url.find("://");
        if (start != std::string::npos) start += 3; else start = 0;
        size_t end = url.find('/', start);
        return url.substr(start, end - start);
    }

    std::string GetPath(const std::string& url)
    {
        size_t start = url.find("://");
        if (start != std::string::npos) start += 3; else start = 0;
        size_t pstart = url.find('/', start);
        if (pstart == std::string::npos) return "";
        size_t qstart = url.find('?', pstart);
        return url.substr(pstart, qstart - pstart);
    }

    std::string GetQuery(const std::string& url)
    {
        size_t pos = url.find('?');
        if (pos != std::string::npos) return url.substr(pos + 1);
        return "";
    }
}
