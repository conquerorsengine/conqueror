// cqs: ConquerorScript komut satırı aracı
//   cqs <dosya.cqs> [--nojit] [--stats]   betiği çalıştır
//   cqs -e "<ifade>"                      ifadeyi değerlendir
//   cqs                                   etkileşimli REPL
#include "Scripting/ConquerorScript/CQSVM.h"
#include "Scripting/ConquerorScript/CQSJit.h"
#include "Scripting/NativeRegistry.h"
#include "Core/Logging/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace Conqueror::CQS;

int main(int argc, char** argv)
{
    bool jit = true, stats = false, noNatives = false, recreate = false;
    std::string file, expr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--nojit")) jit = false;
        else if (!std::strcmp(argv[i], "--stats")) stats = true;
        else if (!std::strcmp(argv[i], "--nonatives")) noNatives = true;
        else if (!std::strcmp(argv[i], "--recreate-vm")) recreate = true;   // regresyon: VM yok et/yeniden oluştur (editör başlangıcı gibi)
        else if (!std::strcmp(argv[i], "-e") && i + 1 < argc) expr = argv[++i];
        else file = argv[i];
    }

    // Motor günlüğü (native köprüsü kullanır): yalnızca konsol, dosya/izleme yok
    {
        Conqueror::LogConfig cfg;
        cfg.UseAsyncLogger = false;
        cfg.EnableFile = false;
        cfg.EnableHtmlExport = false;
        cfg.EnableJsonExport = false;
        cfg.EnableChromeTracing = false;
        Conqueror::Log::Init(cfg);
        Conqueror::Log::GetCoreLogger()->set_level(spdlog::level::warn);
        if (Conqueror::Log::GetClientLogger())
            Conqueror::Log::GetClientLogger()->set_level(spdlog::level::warn);
    }

    int exitCode = 0;
    if (recreate)
    {
        CQSVM first;
        Conqueror::Scripting::NativeRegistry::Get().BindTo(&first);
        first.Interpret("var warm = [1, 2, 3].Map(x => x * 2);", "<warmup>");
    }
    {
        CQSVM vm;
        vm.SetJitEnabled(jit);
        vm.SetErrorCallback([&](const std::string& msg) { std::fprintf(stderr, "%s\n", msg.c_str()); std::fflush(stderr); });
        if (!noNatives)
        {
            Conqueror::Scripting::NativeRegistry::Get().BindTo(&vm);
            // Konsol aracında print stdout'a yazsın (köprüdeki print motor günlüğüne yazar)
            vm.DefineNative("print", [&vm](int n, Value* args) -> Value {
                std::string line;
                for (int i = 0; i < n; ++i)
                {
                    std::string s;
                    if (!vm.ValueToString(args[i], s)) return Value();
                    line += (i ? " " : "") + s;
                }
                std::printf("%s\n", line.c_str());
                std::fflush(stdout);
                return Value();
            }, -1);
        }

        auto t0 = std::chrono::steady_clock::now();
        if (!expr.empty())
        {
            Value r;
            std::string err;
            if (vm.Evaluate(expr, r, err))
            {
                std::string s;
                vm.ValueToString(r, s);
                std::printf("%s\n", s.c_str());
            }
            else
            {
                std::fprintf(stderr, "%s\n", err.c_str());
                exitCode = 1;
            }
        }
        else if (!file.empty())
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                std::fprintf(stderr, "cqs: cannot open %s\n", file.c_str());
                return 2;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            VMResult r = vm.Interpret(ss.str(), file);
            exitCode = r == VMResult::Success ? 0 : (r == VMResult::CompileError ? 65 : 70);
        }
        else
        {
            std::printf("ConquerorScript REPL (JIT: %s). Ctrl+D to exit.\n", jit && CQSJit::IsSupported() ? CQSJit::GetTargetName() : "off");
            std::string line;
            while (std::printf("> "), std::fflush(stdout), std::getline(std::cin, line))
            {
                if (line.empty()) continue;
                Value r;
                std::string err;
                if (vm.Evaluate(line, r, err))
                {
                    if (!r.IsNull())
                    {
                        std::string s;
                        vm.ValueToString(r, s);
                        std::printf("%s\n", r.IsString() ? ("\"" + s + "\"").c_str() : s.c_str());
                    }
                }
                else
                    std::fprintf(stderr, "%s\n", err.c_str());
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        if (stats)
        {
            const auto& st = vm.GetStats();
            std::fprintf(stderr, "[stats] time=%.3f ms calls=%llu jit_funcs=%llu jit_calls=%llu gc_runs=%llu gc_freed=%llu objects=%zu\n",
                         std::chrono::duration<double, std::milli>(t1 - t0).count(),
                         (unsigned long long)st.Calls, (unsigned long long)st.JitCompiledFunctions, (unsigned long long)st.JitCalls,
                         (unsigned long long)st.GCRuns, (unsigned long long)st.GCFreed, CQSHeap::Get().GetObjectCount());
        }
    }
    return exitCode;
}
