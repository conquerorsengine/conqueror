#pragma once
// AMD FidelityFX FSR 2 SDK (External/FidelityFX-FSR2) GCC / Clang uyumluluğu: SDK MSVC'ye özgü wcscpy_s ve _countof kullanır.
// Yalnız SDK kaynaklarına -include ile eklenir (Engine/CMakeLists.txt).
#include <cstddef>
#include <cstring>
#include <cwchar>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src)
{
    std::wcsncpy(dst, src, N - 1);
    dst[N - 1] = 0;
    return 0;
}

inline int wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src)
{
    std::wcsncpy(dst, src, n - 1);
    dst[n - 1] = 0;
    return 0;
}
