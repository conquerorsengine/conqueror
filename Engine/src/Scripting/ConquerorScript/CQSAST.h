#pragma once

#include "CQSToken.h"
#include "CQSValue.h"
#include <string>

#include <vector>
#include <memory>

namespace Conqueror::CQS
{
    // Forward declare
    struct ASTNode;
    using ASTNodePtr = std::shared_ptr<ASTNode>;

    // ── AST Node Types ──
    enum class NodeType : uint8_t
    {
        // Program
        Program,

        // Declarations
        ScriptDecl,         // script X : ConquerorScript { ... }
        ClassDecl,          // class X { ... }
        StructDecl,         // struct X { ... }
        EnumDecl,           // enum X { A, B, C }
        InterfaceDecl,      // interface IX { ... }
        FuncDecl,           // func Foo(x: int) -> float { ... }
        VarDecl,            // var x: int = 5;
        FieldDecl,          // [SerializeField] var x: int = 5;
        ParamDecl,          // x: int (fonksiyon parametresi)

        // Statements
        ExpressionStmt,     // expression;
        BlockStmt,          // { ... }
        IfStmt,             // if (cond) { } else { }
        WhileStmt,          // while (cond) { }
        ForStmt,            // for (init; cond; incr) { }
        ForeachStmt,        // foreach (var x in list) { }
        SwitchStmt,         // switch (expr) { case A: ... }
        CaseStmt,           // case value: ...
        ReturnStmt,         // return expr;
        BreakStmt,          // break;
        ContinueStmt,       // continue;
        YieldStmt,          // yield return expr;
        TryCatchStmt,       // try { } catch (e: Ex) { }
        ThrowStmt,          // throw expr;
        UsingStmt,          // using Conqueror;
        NamespaceStmt,      // namespace X.Y { ... }

        // Expressions
        BinaryExpr,         // a + b, a == b, etc.
        UnaryExpr,          // -a, !a, ~a
        LiteralExpr,        // 42, 3.14f, "hello", true, null
        IdentifierExpr,     // variableName
        AssignExpr,         // x = expr
        CompoundAssignExpr, // x += expr
        CallExpr,           // foo(a, b)
        MemberAccessExpr,   // obj.field
        IndexExpr,          // arr[i]
        NewExpr,            // new ClassName()
        LambdaExpr,         // (x: int) => expr
        CastExpr,           // expr as Type
        IsExpr,             // expr is Type
        TernaryExpr,        // cond ? a : b
        NullCoalesceExpr,   // a ?? b
        NullConditionalExpr,// a?.b
        ListLiteralExpr,    // [1, 2, 3]
        MapLiteralExpr,     // { "a": 1, "b": 2 }
        InterpolatedStringExpr, // $"Hello {name}"
        IncrementExpr,      // x++ / ++x
        DecrementExpr,      // x-- / --x
        GenericExpr,        // GetComponent<Transform>()
        ThisExpr,           // this
    };

    // ── Base AST Node ──
    struct ASTNode
    {
        NodeType    Type;
        int         Line = 0;
        int         Column = 0;

        ASTNode(NodeType type, int line = 0, int col = 0)
            : Type(type), Line(line), Column(col) {}
        virtual ~ASTNode() = default;
    };

    // ═══════════════════════════════════════
    //  DECLARATIONS
    // ═══════════════════════════════════════

    struct ProgramNode : ASTNode
    {
        std::vector<ASTNodePtr> Statements;
        ProgramNode() : ASTNode(NodeType::Program) {}
    };

    struct UsingNode : ASTNode
    {
        std::string Path;  // "Conqueror.Physics"
        UsingNode(const std::string& path, int line)
            : ASTNode(NodeType::UsingStmt, line), Path(path) {}
    };

    struct NamespaceNode : ASTNode
    {
        std::string Name;
        std::vector<ASTNodePtr> Body;
        NamespaceNode(const std::string& name, int line)
            : ASTNode(NodeType::NamespaceStmt, line), Name(name) {}
    };

    struct ParamNode : ASTNode
    {
        std::string Name;
        std::string TypeName;
        ASTNodePtr  DefaultValue;

        ParamNode(const std::string& name, const std::string& typeName, int line)
            : ASTNode(NodeType::ParamDecl, line), Name(name), TypeName(typeName) {}
    };

    struct VarDeclNode : ASTNode
    {
        std::string Name;
        std::string TypeName;   // Boş olabilir (tip çıkarımı)
        ASTNodePtr  Initializer;
        bool        IsPublic = false;
        bool        IsStatic = false;
        std::vector<std::string> Attributes; // [SerializeField], [Range(0,1)]

        VarDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::VarDecl, line), Name(name) {}
    };

    struct FuncDeclNode : ASTNode
    {
        std::string              Name;
        std::vector<ASTNodePtr>  Params;
        std::string              ReturnType; // Boş = void
        ASTNodePtr               Body;       // BlockStmt
        bool                     IsPublic = false;
        bool                     IsPrivate = false;
        bool                     IsStatic = false;

        FuncDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::FuncDecl, line), Name(name) {}
    };

    struct ScriptDeclNode : ASTNode
    {
        std::string              Name;
        std::string              BaseClass;  // "ConquerorScript"
        std::vector<std::string> Interfaces;
        std::vector<ASTNodePtr>  Members;    // VarDecl + FuncDecl
        bool                     IsPublic = false;

        ScriptDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::ScriptDecl, line), Name(name) {}
    };

    struct ClassDeclNode : ASTNode
    {
        std::string              Name;
        std::string              BaseClass;
        std::vector<std::string> Interfaces;
        std::vector<ASTNodePtr>  Members;
        bool                     IsPublic = false;

        ClassDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::ClassDecl, line), Name(name) {}
    };

    struct StructDeclNode : ASTNode
    {
        std::string              Name;
        std::vector<ASTNodePtr>  Members;
        bool                     IsPublic = false;

        StructDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::StructDecl, line), Name(name) {}
    };

    struct EnumDeclNode : ASTNode
    {
        std::string              Name;
        std::vector<std::string> Values;
        bool                     IsPublic = false;

        EnumDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::EnumDecl, line), Name(name) {}
    };

    struct InterfaceDeclNode : ASTNode
    {
        std::string              Name;
        std::vector<ASTNodePtr>  Methods; // FuncDecl (body'siz)

        InterfaceDeclNode(const std::string& name, int line)
            : ASTNode(NodeType::InterfaceDecl, line), Name(name) {}
    };

    // ═══════════════════════════════════════
    //  STATEMENTS
    // ═══════════════════════════════════════

    struct BlockNode : ASTNode
    {
        std::vector<ASTNodePtr> Statements;
        BlockNode(int line) : ASTNode(NodeType::BlockStmt, line) {}
    };

    struct ExprStmtNode : ASTNode
    {
        ASTNodePtr Expression;
        ExprStmtNode(ASTNodePtr expr, int line)
            : ASTNode(NodeType::ExpressionStmt, line), Expression(std::move(expr)) {}
    };

    struct IfNode : ASTNode
    {
        ASTNodePtr Condition;
        ASTNodePtr ThenBranch;
        ASTNodePtr ElseBranch; // nullable

        IfNode(int line) : ASTNode(NodeType::IfStmt, line) {}
    };

    struct WhileNode : ASTNode
    {
        ASTNodePtr Condition;
        ASTNodePtr Body;

        WhileNode(int line) : ASTNode(NodeType::WhileStmt, line) {}
    };

    struct ForNode : ASTNode
    {
        ASTNodePtr Initializer;
        ASTNodePtr Condition;
        ASTNodePtr Increment;
        ASTNodePtr Body;

        ForNode(int line) : ASTNode(NodeType::ForStmt, line) {}
    };

    struct ForeachNode : ASTNode
    {
        std::string VarName;
        std::string VarType; // opsiyonel
        ASTNodePtr  Iterable;
        ASTNodePtr  Body;

        ForeachNode(int line) : ASTNode(NodeType::ForeachStmt, line) {}
    };

    struct SwitchNode : ASTNode
    {
        ASTNodePtr               Expression;
        std::vector<ASTNodePtr>  Cases;
        ASTNodePtr               DefaultCase;

        SwitchNode(int line) : ASTNode(NodeType::SwitchStmt, line) {}
    };

    struct CaseNode : ASTNode
    {
        ASTNodePtr              Value;
        std::vector<ASTNodePtr> Body;

        CaseNode(int line) : ASTNode(NodeType::CaseStmt, line) {}
    };

    struct ReturnNode : ASTNode
    {
        ASTNodePtr Value; // nullable (void return)
        ReturnNode(int line) : ASTNode(NodeType::ReturnStmt, line) {}
    };

    struct BreakNode : ASTNode
    {
        BreakNode(int line) : ASTNode(NodeType::BreakStmt, line) {}
    };

    struct ContinueNode : ASTNode
    {
        ContinueNode(int line) : ASTNode(NodeType::ContinueStmt, line) {}
    };

    struct YieldNode : ASTNode
    {
        ASTNodePtr Value;
        YieldNode(int line) : ASTNode(NodeType::YieldStmt, line) {}
    };

    struct TryCatchNode : ASTNode
    {
        ASTNodePtr  TryBody;
        std::string CatchVarName;
        std::string CatchTypeName;
        ASTNodePtr  CatchBody;
        ASTNodePtr  FinallyBody; // nullable

        TryCatchNode(int line) : ASTNode(NodeType::TryCatchStmt, line) {}
    };

    struct ThrowNode : ASTNode
    {
        ASTNodePtr Expression;
        ThrowNode(int line) : ASTNode(NodeType::ThrowStmt, line) {}
    };

    // ═══════════════════════════════════════
    //  EXPRESSIONS
    // ═══════════════════════════════════════

    struct LiteralNode : ASTNode
    {
        Value LiteralValue;
        LiteralNode(Value val, int line)
            : ASTNode(NodeType::LiteralExpr, line), LiteralValue(val) {}
    };

    struct IdentifierNode : ASTNode
    {
        std::string Name;
        IdentifierNode(const std::string& name, int line)
            : ASTNode(NodeType::IdentifierExpr, line), Name(name) {}
    };

    struct ThisNode : ASTNode
    {
        ThisNode(int line) : ASTNode(NodeType::ThisExpr, line) {}
    };

    struct BinaryNode : ASTNode
    {
        ASTNodePtr  Left;
        TokenType   Operator;
        ASTNodePtr  Right;

        BinaryNode(ASTNodePtr left, TokenType op, ASTNodePtr right, int line)
            : ASTNode(NodeType::BinaryExpr, line),
              Left(std::move(left)), Operator(op), Right(std::move(right)) {}
    };

    struct UnaryNode : ASTNode
    {
        TokenType   Operator;
        ASTNodePtr  Operand;
        bool        IsPrefix;

        UnaryNode(TokenType op, ASTNodePtr operand, bool prefix, int line)
            : ASTNode(NodeType::UnaryExpr, line),
              Operator(op), Operand(std::move(operand)), IsPrefix(prefix) {}
    };

    struct AssignNode : ASTNode
    {
        ASTNodePtr Target;
        ASTNodePtr Value;

        AssignNode(ASTNodePtr target, ASTNodePtr value, int line)
            : ASTNode(NodeType::AssignExpr, line),
              Target(std::move(target)), Value(std::move(value)) {}
    };

    struct CompoundAssignNode : ASTNode
    {
        ASTNodePtr  Target;
        TokenType   Operator;  // PlusEqual, MinusEqual, etc.
        ASTNodePtr  Value;

        CompoundAssignNode(ASTNodePtr target, TokenType op, ASTNodePtr value, int line)
            : ASTNode(NodeType::CompoundAssignExpr, line),
              Target(std::move(target)), Operator(op), Value(std::move(value)) {}
    };

    struct CallNode : ASTNode
    {
        ASTNodePtr               Callee;
        std::vector<ASTNodePtr>  Arguments;
        std::vector<std::string> GenericArgs; // <Transform, Rigidbody>

        CallNode(ASTNodePtr callee, int line)
            : ASTNode(NodeType::CallExpr, line), Callee(std::move(callee)) {}
    };

    struct MemberAccessNode : ASTNode
    {
        ASTNodePtr  Object;
        std::string Member;

        MemberAccessNode(ASTNodePtr obj, const std::string& member, int line)
            : ASTNode(NodeType::MemberAccessExpr, line),
              Object(std::move(obj)), Member(member) {}
    };

    struct IndexNode : ASTNode
    {
        ASTNodePtr Object;
        ASTNodePtr Index;

        IndexNode(ASTNodePtr obj, ASTNodePtr index, int line)
            : ASTNode(NodeType::IndexExpr, line),
              Object(std::move(obj)), Index(std::move(index)) {}
    };

    struct NewNode : ASTNode
    {
        std::string              ClassName;
        std::vector<ASTNodePtr>  Arguments;

        NewNode(const std::string& className, int line)
            : ASTNode(NodeType::NewExpr, line), ClassName(className) {}
    };

    struct LambdaNode : ASTNode
    {
        std::vector<ASTNodePtr> Params;
        ASTNodePtr              Body; // Expression or Block

        LambdaNode(int line) : ASTNode(NodeType::LambdaExpr, line) {}
    };

    struct TernaryNode : ASTNode
    {
        ASTNodePtr Condition;
        ASTNodePtr TrueExpr;
        ASTNodePtr FalseExpr;

        TernaryNode(int line) : ASTNode(NodeType::TernaryExpr, line) {}
    };

    struct ListLiteralNode : ASTNode
    {
        std::vector<ASTNodePtr> Elements;
        ListLiteralNode(int line) : ASTNode(NodeType::ListLiteralExpr, line) {}
    };

    struct MapLiteralNode : ASTNode
    {
        std::vector<std::pair<ASTNodePtr, ASTNodePtr>> Entries;
        MapLiteralNode(int line) : ASTNode(NodeType::MapLiteralExpr, line) {}
    };

    struct InterpolatedStringNode : ASTNode
    {
        // Alternatif string parçaları ve expression'lar
        std::vector<ASTNodePtr> Parts; // LiteralNode(string) ve expression'lar karışık

        InterpolatedStringNode(int line)
            : ASTNode(NodeType::InterpolatedStringExpr, line) {}
    };
}
