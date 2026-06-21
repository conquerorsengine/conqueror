#include "CQSEngine.h"
#include "CQSNativeBridge.h"
#include "Core/Logging/Log.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace Conqueror::CQS
{
    std::unique_ptr<CQSVM> CQSEngine::s_VM = nullptr;
    void* CQSEngine::s_ActiveScene = nullptr;

    void CQSEngine::Init()
    {
        s_VM = std::make_unique<CQSVM>();
        CQSNativeBridge::RegisterGlobals(s_VM.get());
        CQ_CORE_INFO("[CQS] Engine initialized.");
    }

    void CQSEngine::Shutdown()
    {
        s_VM.reset();
    }

    bool CQSEngine::ExecuteFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            CQ_CORE_ERROR("[CQS] Dosya acilamadi: {0}", path);
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        std::string sourceHash = std::to_string(std::hash<std::string>{}(source));

        auto it = s_Cache.find(path);
        if (it != s_Cache.end() && it->second.SourceHash == sourceHash)
        {
            CQ_CORE_INFO("[CQS] Script cache hit: {0}", path);
            return s_VM->Run(it->second.Chunk) == VMResult::Success;
        }

        CQ_CORE_INFO("[CQS] Script yukleniyor: {0}", path);

        CQSLexer lexer;
        auto tokens = lexer.Tokenize(source);
        if (lexer.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Lexer hatalari:");
            for (auto& err : lexer.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        CQSParser parser;
        auto ast = parser.Parse(tokens);
        if (parser.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Parser hatalari:");
            for (auto& err : parser.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        CQSCompiler compiler;
        auto chunk = compiler.Compile(ast);
        if (compiler.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Compiler hatalari:");
            for (const auto& err : compiler.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        s_Cache[path] = { chunk, sourceHash };
        return s_VM->Run(chunk) == VMResult::Success;
    }

    bool CQSEngine::ExecuteString(const std::string& source)
    {
        if (!s_VM) return false;

        CQSLexer lexer;
        auto tokens = lexer.Tokenize(source);
        if (lexer.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Lexer hatalari:");
            for (auto& err : lexer.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        CQSParser parser;
        auto ast = parser.Parse(tokens);
        if (parser.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Parser hatalari:");
            for (auto& err : parser.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        CQSCompiler compiler;
        auto chunk = compiler.Compile(ast);
        if (compiler.HasErrors())
        {
            CQ_CORE_ERROR("[CQS] Compiler hatalari:");
            for (const auto& err : compiler.GetErrors()) CQ_CORE_ERROR("  - {0}", err);
            return false;
        }

        return s_VM->Run(chunk) == VMResult::Success;
    }

    void* CQSEngine::Instantiate(const std::string& className, uint32_t entityHandle)
    {
        if (!s_VM) return nullptr;
        
        CQSInstanceObject* instance = nullptr;

        // If className is provided, try to find the class object
        if (!className.empty())
        {
            Value classVal;
            if (s_VM->GetGlobal(className, classVal))
            {
                CQ_CORE_INFO("[CQS] Class instantiated: {0} for entity {1}", className, entityHandle);
                instance = new CQSInstanceObject(className);
            }
            else
            {
                CQ_CORE_WARN("[CQS] Class '{0}' not found in globals! Check script name.", className);
            }
        }
        else
        {
            CQ_CORE_WARN("[CQS] ClassName is empty for entity {0}! Falling back to Global.", entityHandle);
        }
        
        if (!instance)
            instance = new CQSInstanceObject("Global");

        instance->EntityHandle = entityHandle;
        s_VM->m_Objects.push_back(instance);
        return (void*)instance;
    }

    void CQSEngine::Invoke(void* instance, const std::string& methodName, float ts, void* scene)
    {
        if (!s_VM || !instance) return;
        
        CQSInstanceObject* inst = static_cast<CQSInstanceObject*>(instance);

        // Set context
        s_VM->SetCurrentInstance(inst);
        s_ActiveScene = scene;

        // Push arguments (for now just ts)
        s_VM->Push(Value::MakeFloat((double)ts));
        
        // Invoke method (try class prefixed first)
        std::string fullMethodName = inst->ClassName + "." + methodName;
        Value temp;
        if (s_VM->GetGlobal(fullMethodName, temp))
        {
            s_VM->Invoke(fullMethodName, 1);
        }
        else
        {
            s_VM->Invoke(methodName, 1);
        }
    }
}
