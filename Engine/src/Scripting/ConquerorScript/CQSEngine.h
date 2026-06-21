#pragma once

#include "CQSLexer.h"
#include "CQSParser.h"
#include "CQSCompiler.h"
#include "CQSVM.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace Conqueror::CQS
{
    class CQSEngine
    {
    public:
        static void Init();
        static void Shutdown();

        static bool ExecuteString(const std::string& source);
        static bool ExecuteFile(const std::string& path);
        
        static void* Instantiate(const std::string& className, uint32_t entityHandle);
        static void Invoke(void* instance, const std::string& methodName, float ts, void* scene = nullptr);
        static CQSVM* GetVM() { return s_VM.get(); }
        static void* GetActiveScene() { return s_ActiveScene; }

    private:
        static std::unique_ptr<CQSVM> s_VM;
        static void* s_ActiveScene;

        struct CachedScript
        {
            std::shared_ptr<CQSChunk> Chunk;
            std::string SourceHash;
        };
        static inline std::unordered_map<std::string, CachedScript> s_Cache;
    };
}
