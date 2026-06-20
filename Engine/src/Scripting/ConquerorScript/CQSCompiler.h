#pragma once

#include "CQSAST.h"
#include "CQSChunk.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace Conqueror::CQS
{
    // Compiler state for variables
    struct Local
    {
        std::string Name;
        int         Depth;
        bool        IsCaptured = false;
    };

    struct LoopContext
    {
        int LoopStart;
        std::vector<int> BreakJumps;
        std::vector<int> ContinueJumps;  // Forward jumps for continue in for-loops
        bool IsForLoop = false;
    };

    struct CompilerState
    {
        std::vector<Local>  Locals;
        int                 ScopeDepth = 0;
        int                 LocalCount = 0;
        std::vector<LoopContext> LoopStack;
        
        CompilerState() {
             // Slot 0 is usually reserved (e.g., for 'this' or function object)
             Locals.push_back({"", 0, false});
             LocalCount = 1;
        }
    };

    class CQSCompiler
    {
    public:
        CQSCompiler() = default;

        // AST'yi Bytecode'a derle
        std::shared_ptr<CQSChunk> Compile(ASTNodePtr root);

        bool HasErrors() const { return m_HasError; }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }

    private:
        std::shared_ptr<CQSChunk> m_Chunk;
        CompilerState            m_State;
        bool                     m_HasError = false;
        std::vector<std::string> m_Errors;
        std::string              m_CurrentClassName;

        // ── Emit Helpers ──
        void EmitByte(uint8_t byte, int line);
        void EmitOpCode(OpCode op, int line);
        void EmitConstant(Value value, int line);
        int  EmitJump(OpCode op, int line);
        void PatchJump(int offset);
        void EmitLoop(int loopStart, int line);

        // ── AST Visiting ──
        void Visit(ASTNodePtr node);
        
        // Declarations
        void VisitProgram(ProgramNode* node);
        void VisitUsing(UsingNode* node);
        void VisitNamespace(NamespaceNode* node);
        void VisitVarDecl(VarDeclNode* node);
        void VisitFuncDecl(FuncDeclNode* node);
        void VisitScriptDecl(ScriptDeclNode* node);
        void VisitClassDecl(ClassDeclNode* node);
        void VisitStructDecl(StructDeclNode* node);
        void VisitEnumDecl(EnumDeclNode* node);
        void VisitInterfaceDecl(InterfaceDeclNode* node);

        // Statements
        void VisitBlock(BlockNode* node);
        void VisitExpressionStmt(ExprStmtNode* node);
        void VisitIf(IfNode* node);
        void VisitWhile(WhileNode* node);
        void VisitFor(ForNode* node);
        void VisitForeach(ForeachNode* node);
        void VisitSwitch(SwitchNode* node);
        void VisitReturn(ReturnNode* node);
        void VisitBreak(BreakNode* node);
        void VisitContinue(ContinueNode* node);

        // Expressions
        void VisitLiteral(LiteralNode* node);
        void VisitBinary(BinaryNode* node);
        void VisitUnary(UnaryNode* node);
        void VisitIdentifier(IdentifierNode* node);
        void VisitAssign(AssignNode* node);
        void VisitCall(CallNode* node);
        void VisitMemberAccess(MemberAccessNode* node);
        void VisitIndex(IndexNode* node);
        void VisitNew(NewNode* node);
        void VisitThis(ThisNode* node);
        void VisitCompoundAssign(CompoundAssignNode* node);
        void VisitTernary(TernaryNode* node);
        void VisitListLiteral(ListLiteralNode* node);
        void VisitMapLiteral(MapLiteralNode* node);
        void VisitLambda(LambdaNode* node);
        void VisitInterpolatedString(InterpolatedStringNode* node);

        // ── Variable Management ──
        void DeclareVariable(const std::string& name, int line);
        int  ResolveLocal(const std::string& name);
        void BeginScope();
        void EndScope(int line);

        void Error(int line, const std::string& message);
    };
}
