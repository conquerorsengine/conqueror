#pragma once

#include "CQSAST.h"
#include "CQSOpCode.h"
#include "CQSValue.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Conqueror::CQS
{
    class CQSVM;

    // AST -> bytecode. Global adları derleme anında VM global slotlarına çözülür.
    class CQSCompiler
    {
    public:
        explicit CQSCompiler(CQSVM* vm) : m_VM(vm) {}

        // Modül fonksiyonu döndürür (hata varsa nullptr)
        CQSFunctionObject* Compile(const ASTNodePtr& root, const std::string& sourceName);
        // REPL: tek ifadeyi "return <ifade>" fonksiyonuna derler
        CQSFunctionObject* CompileExpression(const ASTNodePtr& expr, const std::string& sourceName);

        bool HasErrors() const { return !m_Errors.empty(); }
        const std::vector<std::string>& GetErrors() const { return m_Errors; }
        const std::vector<std::string>& GetWarnings() const { return m_Warnings; }

    private:
        struct Local
        {
            std::string Name;
            int  ScopeDepth = 0;
            int  Slot = 0;
            bool Captured = false;
        };

        struct UpvalueInfo
        {
            std::string Name;
            uint16_t    Index = 0;
            bool        IsLocal = false;
        };

        struct LoopState
        {
            bool IsSwitch = false;
            int  ContinueTarget = -1;            // bilinmiyorsa ileri atlamalar yamalanır
            std::vector<int> BreakJumps;
            std::vector<int> ContinueJumps;
            size_t LocalCount = 0;               // döngü başındaki yerel sayısı
            int    Depth = 0;
            size_t TryCount = 0;
        };

        struct TryState
        {
            ASTNode* FinallyBody = nullptr;      // null: yalnızca catch
            size_t   LocalCount = 0;
        };

        struct ClassState
        {
            std::string Name;
            CQSClassObject* Template = nullptr;
            std::vector<std::string> Layout;     // tam alan yerleşimi (taban + kendi)
            bool LayoutKnown = true;
            bool BaseUnknown = false;            // taban derleme anında bilinmiyor
            std::unordered_set<std::string> Methods;
            std::unordered_set<std::string> StaticNames;
            ClassState* Enclosing = nullptr;
        };

        struct FunctionState
        {
            FunctionState*           Enclosing = nullptr;
            CQSFunctionObject*       Fn = nullptr;
            std::vector<Local>       Locals;
            std::vector<UpvalueInfo> Upvalues;
            std::vector<LoopState>   Loops;
            std::vector<TryState>    Trys;
            std::vector<int>         CatchSlots;          // "throw;" için en yakın catch değişkeni
            int  ScopeDepth = 0;
            int  Depth = 0;                      // taban üstündeki slot sayısı (slot 0 dahil)
            bool IsMethod = false;               // slot 0 = this
            bool IsStatic = false;
            bool IsInitializer = false;
            ClassState* Class = nullptr;
            std::unordered_map<std::string, int> ConstStrings;
            std::unordered_map<std::string, int> NameIndex;
        };

        CQSVM* m_VM;
        std::string m_SourceName;
        FunctionState* m_Fn = nullptr;
        ClassState* m_Class = nullptr;
        int m_Line = 0;
        std::vector<std::string> m_Errors;
        std::vector<std::string> m_Warnings;
        std::unordered_set<std::string> m_Namespaces;
        std::unordered_map<std::string, CQSClassObject*> m_UnitClasses;   // bu birimde tanımlanan sınıflar (şablon)
        std::unordered_map<std::string, std::unordered_set<std::string>> m_UnitMethods;
        std::unordered_map<int, int> m_GlobalRefs;      // global slot -> ilk kullanım satırı (bilinmeyen ad uyarısı)
        std::unordered_set<int> m_GlobalDefs;

        // ── Hata ──
        void Error(int line, const std::string& message);
        void Warning(int line, const std::string& message);

        // ── Emit ──
        int  Emit(OpCode op, int a = 0, int b = 0, int c = 0);
        int  EmitJump(OpCode op);
        void PatchJump(int at);              // hedef = mevcut konum
        void PatchJumpTo(int at, int target);
        void EmitLoop(int target);
        int  CurrentPos() const;
        void SetDepth(int d) { m_Fn->Depth = d; }
        int  Depth() const { return m_Fn->Depth; }
        int  AddConstant(const Value& v);
        int  StringConstant(const std::string& s);
        int  NameConstant(const std::string& s);
        void EmitConstant(const Value& v);
        int  GlobalSlot(const std::string& name);

        // ── Fonksiyonlar ──
        void BeginFunction(FunctionState& state, const std::string& name, bool isMethod);
        CQSFunctionObject* EndFunction();
        void CompileFunctionBody(FunctionState& state, const std::vector<ASTNodePtr>& params, const ASTNodePtr& body, bool expressionBody);
        void EmitClosure(FunctionState& state);

        // ── Kapsam / yereller ──
        void BeginScope();
        void EndScope();
        int  DeclareLocal(const std::string& name, int line);   // üstteki değeri yerel yapar
        int  AddHiddenLocal(const std::string& name);
        void EmitPopLocalsAbove(size_t localCount);              // derleme durumunu değiştirmeden (break/continue/return)
        int  ResolveLocal(FunctionState* fs, const std::string& name);
        int  ResolveUpvalue(FunctionState* fs, const std::string& name);
        int  AddUpvalue(FunctionState* fs, uint16_t index, bool isLocal, const std::string& name);
        bool EmitThis(int line);                                  // this'i yığına koy

        // ── Bildirimler ──
        void CompileStatement(const ASTNodePtr& node);
        void CompileVarDecl(VarDeclNode* node, bool global);
        void CompileFuncDecl(FuncDeclNode* node, bool global);
        void CompileClass(ClassDeclNode* node);
        void CompileEnum(EnumDeclNode* node);
        void CompileBlock(BlockNode* node, bool newScope = true);
        void CompileIf(IfNode* node);
        void CompileWhile(WhileNode* node);
        void CompileDoWhile(WhileNode* node);
        void CompileFor(ForNode* node);
        void CompileForeach(ForeachNode* node);
        void CompileSwitch(SwitchNode* node);
        void CompileReturn(ReturnNode* node);
        void CompileBreakContinue(bool isBreak, int line);
        void CompileTry(TryCatchNode* node);
        void CompileThrow(ThrowNode* node);
        void EmitFinallyBlocks(size_t downToTryCount);

        // ── İfadeler ──
        void CompileExpr(const ASTNodePtr& node);
        void CompileLiteral(LiteralNode* node);
        void CompileIdentifier(IdentifierNode* node);
        void CompileBinary(BinaryNode* node);
        void CompileLogical(BinaryNode* node);
        void CompileUnary(UnaryNode* node);
        void CompileAssign(AssignNode* node);
        void CompileCompoundAssign(CompoundAssignNode* node);
        void CompileIncDec(IncDecNode* node);
        void CompileCall(CallNode* node);
        void CompileMember(MemberAccessNode* node);
        void CompileIndex(IndexNode* node);
        void CompileNew(NewNode* node);
        void CompileLambda(LambdaNode* node);
        void CompileTernary(TernaryNode* node);
        void CompileList(ListLiteralNode* node);
        void CompileMap(MapLiteralNode* node);
        void CompileInterpolated(InterpolatedStringNode* node);
        void CompileTypeTest(TypeTestNode* node);

        // Değişken hedefleri (okuma/yazma ortak çözümleme)
        enum class VarKind { Local, Upvalue, ThisField, ThisProp, StaticProp, Global, Dynamic };
        struct VarRef
        {
            VarKind Kind = VarKind::Global;
            int     Index = 0;          // slot / upvalue / alan slotu / global
            std::string Name;
        };
        VarRef ResolveVariable(const std::string& name, int line);
        void   EmitGet(const VarRef& ref);
        void   EmitSet(const VarRef& ref);                       // [v] -> [v]
        bool   ResolveNamespacePath(ASTNode* node, std::string& globalName);
        bool   IsNamespaceName(const std::string& name) const;
        bool   TryFoldConstant(const ASTNodePtr& node, Value& out);
        OpCode BinaryOpFor(TokenType t, int line);
        Value  DefaultValueForType(const std::string& typeName, bool& isList);
    };
}
