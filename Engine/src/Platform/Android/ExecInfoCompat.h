#pragma once

// <execinfo.h> (backtrace, backtrace_symbols, backtrace_symbols_fd) Android'de ancak API 33'te geldi, QNX'te hiç yok.
// Bu sistemlerde aynı işlevler _Unwind_Backtrace + dladdr ile sağlanır; çıktı glibc biçimindedir: "dosya(sembol+0xN) [adres]".
#if (defined(__ANDROID__) && __ANDROID_API__ < 33) || defined(__QNX__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)

#include <dlfcn.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Conqueror::ExecInfoCompat
{
    struct UnwindState
    {
        void** Frames;
        int Max;
        int Count;
    };

    inline _Unwind_Reason_Code Step(_Unwind_Context* context, void* arg)
    {
        auto* state = static_cast<UnwindState*>(arg);
        const uintptr_t pc = _Unwind_GetIP(context);
        if (pc)
        {
            if (state->Count >= state->Max)
                return _URC_END_OF_STACK;
            state->Frames[state->Count++] = reinterpret_cast<void*>(pc);
        }
        return _URC_NO_REASON;
    }

    inline int Format(void* frame, char* out, size_t size)
    {
        Dl_info info{};
        if (dladdr(frame, &info) && info.dli_fname)
        {
            if (info.dli_sname)
                return std::snprintf(out, size, "%s(%s+0x%zx) [%p]", info.dli_fname, info.dli_sname,
                                     (size_t)((uintptr_t)frame - (uintptr_t)info.dli_saddr), frame);
            return std::snprintf(out, size, "%s(+0x%zx) [%p]", info.dli_fname, (size_t)((uintptr_t)frame - (uintptr_t)info.dli_fbase), frame);
        }
        return std::snprintf(out, size, "[%p]", frame);
    }
}

inline int backtrace(void** frames, int max)
{
    Conqueror::ExecInfoCompat::UnwindState state{ frames, max, 0 };
    _Unwind_Backtrace(&Conqueror::ExecInfoCompat::Step, &state);
    return state.Count;
}

// glibc gibi tek bellek bloğu: işaretçi tablosu + dizeler (çağıran free() ile bırakır)
inline char** backtrace_symbols(void* const* frames, int count)
{
    if (count <= 0)
        return nullptr;
    constexpr size_t kLine = 512;
    char* block = static_cast<char*>(std::malloc(sizeof(char*) * (size_t)count + kLine * (size_t)count));
    if (!block)
        return nullptr;
    char** table = reinterpret_cast<char**>(block);
    char* text = block + sizeof(char*) * (size_t)count;
    for (int i = 0; i < count; ++i)
    {
        table[i] = text + kLine * (size_t)i;
        Conqueror::ExecInfoCompat::Format(frames[i], table[i], kLine);
    }
    return table;
}

inline void backtrace_symbols_fd(void* const* frames, int count, int fd)
{
    char line[512];
    for (int i = 0; i < count; ++i)
    {
        int n = Conqueror::ExecInfoCompat::Format(frames[i], line, sizeof(line) - 1);
        if (n < 0)
            continue;
        n = n > (int)sizeof(line) - 2 ? (int)sizeof(line) - 2 : n;
        line[n++] = '\n';
        if (::write(fd, line, (size_t)n) < 0)
            return;
    }
}

#else
    #include <execinfo.h>
#endif
