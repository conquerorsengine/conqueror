#pragma once

#include "CQSChunk.h"
#include "CQSValue.h"
#include <vector>
#include <stack>
#include <memory>
#include <unordered_map>

namespace Conqueror::CQS
{
    // Call frame for function calls
    struct CallFrame
    {
        CQSFunctionObject* Function;
        uint8_t*           IP;      // Instruction Pointer
        Value*             Slots;   // Start of this frame's stack slots
    };

    enum class VMResult
    {
        Success,
        CompileError,
        RuntimeError
    };

    class CQSVM
    {
    public:
        CQSVM();
        ~CQSVM();

        // Bytecode chunk'ı çalıştır
        VMResult Run(std::shared_ptr<CQSChunk> chunk);

        void  DefineNative(const std::string& name, NativeFn function, int arity);
        void  DefineGlobal(const std::string& name, Value value);
        bool  GetGlobal(const std::string& name, Value& outValue);
        
        // Instance management
        void SetCurrentInstance(CQSInstanceObject* instance) { m_CurrentInstance = instance; }
        CQSInstanceObject* GetCurrentInstance() const { return m_CurrentInstance; }

        // Invoke a method on an object
        bool  Invoke(const std::string& name, int argCount);

        void  Push(Value value);
        Value Pop();
        Value Peek(int distance = 0);

    private:
        static constexpr int STACK_MAX = 256;
        static constexpr int FRAMES_MAX = 64;

        Value      m_Stack[STACK_MAX];
        Value*     m_StackTop;
        
        CallFrame  m_Frames[FRAMES_MAX];
        int        m_FrameCount = 0;

        CQSInstanceObject* m_CurrentInstance = nullptr;
        std::unordered_map<std::string, Value> m_Globals;

        // ── VM Operations ──
        VMResult Execute();
        
        bool  Call(CQSFunctionObject* function, int argCount);
        bool  CallValue(Value callee, int argCount);

        void  RuntimeError(const char* format, ...);

        // ── Memory Management ──
        std::vector<CQSObject*> m_Objects; // All allocated objects for GC
        void CollectGarbage();

        friend class CQSEngine;
    };
}
