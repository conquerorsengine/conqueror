#pragma once

#include <cstddef>

namespace Conqueror::CQS
{
    class CQSVM;
    struct CQSFrame;
    struct CQSFunctionObject;

    // Bytecode -> yerel makine kodu (sljit). Desteklenmeyen platformda yorumlayıcı kullanılır.
    class CQSJit
    {
    public:
        static bool IsSupported();
        static bool Compile(CQSVM* vm, CQSFunctionObject* fn);
        // startPc > 0: yorumlayıcıdan sıcak döngü başlığında geçiş (OSR); yığın düzeni aynıdır
        static bool Run(CQSVM* vm, CQSFrame* frame, int startPc = 0);
        static void FreeCode(void* code);
        static const char* GetTargetName();
    };
}
