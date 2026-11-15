#pragma once

#include "Core/Base/Base.h"

#include <cstdlib>

namespace Conqueror
{
    // Kabuk komutu: iOS / iPadOS'ta kabuk ve system() yok (SDK'da kullanılamaz), orada -1 döner
    inline int RunShell(const char* command)
    {
#if defined(CQ_PLATFORM_IOS)
        (void)command;
        return -1;
#else
        return std::system(command);
#endif
    }
}
