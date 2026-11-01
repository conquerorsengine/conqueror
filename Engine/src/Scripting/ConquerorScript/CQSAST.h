#pragma once

#include "CQSToken.h"
#include "CQSValue.h"

#include <memory>
#include <string>
#include <vector>

namespace Conqueror::CQS
{
    struct ASTNode;
    using ASTNodePtr = std::shared_ptr<ASTNode>;

    enum class NodeType : uint8_t
    {
        Program,

        // Declarations
        ScriptDecl, ClassDecl, StructDecl, EnumDecl, InterfaceDecl, FuncDecl, VarDecl, ParamDecl,

        // Statements
        ExpressionStmt, BlockStmt, IfStmt, WhileStmt, DoWhileStmt, ForStmt, ForeachStmt, SwitchStmt, CaseStmt,
        ReturnStmt, BreakStmt, ContinueStmt, YieldStmt, TryCatchStmt, ThrowStmt, UsingStmt, NamespaceStmt,

        // Expressions
        BinaryExpr, LogicalExpr, UnaryExpr, LiteralExpr, IdentifierExpr, AssignExpr, CompoundAssignExpr,
        CallExpr, MemberAccessExpr, IndexExpr, NewExpr, LambdaExpr, CastExpr, IsExpr, TernaryExpr,
        ListLiteralExpr, MapLiteralExpr, InterpolatedStringExpr, IncDecExpr, ThisExpr, BaseExpr,
    };

    struct ASTNode
    {
        NodeType Type;
        int      Line = 0;
        int      Column = 0;

        ASTNode(NodeType type, int line = 0, int col = 0) : Type(type), Line(line), Column(col) {}
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
        std::string Path;
        UsingNode(const std::string& path, int line) : ASTNode(NodeType::UsingStmt, line), Path(path) {}
    };

    struct NamespaceNode : ASTNode
    {
        std::string Name;
        std::vector<ASTNodePtr> Body;
        NamespaceNode(const std::string& name, int line) : ASTNode(NodeType::NamespaceStmt, line), Name(name) {}
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
        std::string TypeName;
        ASTNodePtr  Initializer;
        bool        IsPublic = false;
        bool        IsPrivate = false;
        bool        IsStatic = false;
        std::vector<std::string> Attributes;
        VarDeclNode(const std::string& name, int line) : ASTNode(NodeType::VarDecl, line), Name(name) {}
    };

    struct FuncDeclNode : ASTNode
    {
        std::string             Name;
        std::vector<ASTNodePtr> Params;
        std::string             ReturnType;
        ASTNodePtr              Body;              // BlockStmt
        bool                    IsPublic = false;
        bool                    IsPrivate = false;
        bool                    IsStatic = false;
        FuncDeclNode(const std::string& name, int line) : ASTNode(NodeType::FuncDecl, line), Name(name) {}
    };

    // script / class / struct ortak gövdesi
    struct ClassDeclNode : ASTNode
    {
        std::string              Name;
        std::string              BaseClass;
        std::vector<std::string> Interfaces;
        std::vector<ASTNodePtr>  Members;   // VarDecl + FuncDecl
        bool                     IsPublic = false;
        bool                     IsScript = false;
        bool                     IsStruct = false;
        ClassDeclNode(const std::string& name, int line, NodeType type = NodeType::ClassDecl) : ASTNode(type, line), Name(name) {}
    };
    using ScriptDeclNode = ClassDeclNode;
    using StructDeclNode = ClassDeclNode;

    struct EnumDeclNode : ASTNode
    {
        std::string              Name;
        std::vector<std::string> Values;
        std::vector<ASTNodePtr>  Initializers;   // null ya da sabit ifade
        bool                     IsPublic = false;
        EnumDeclNode(const std::string& name, int line) : ASTNode(NodeType::EnumDecl, line), Name(name) {}
    };

    struct InterfaceDeclNode : ASTNode
    {
        std::string             Name;
        std::vector<ASTNodePtr> Methods;
        InterfaceDeclNode(const std::string& name, int line) : ASTNode(NodeType::InterfaceDecl, line), Name(name) {}
    };

    // ═══════════════════════════════════════
    //  STATEMENTS
    // ═══════════════════════════════════════

    struct BlockNode : ASTNode
    {
        std::vector<ASTNodePtr> Statements;
        explicit BlockNode(int line) : ASTNode(NodeType::BlockStmt, line) {}
    };

    struct ExprStmtNode : ASTNode
    {
        ASTNodePtr Expression;
        ExprStmtNode(ASTNodePtr expr, int line) : ASTNode(NodeType::ExpressionStmt, line), Expression(std::move(expr)) {}
    };

    struct IfNode : ASTNode
    {
        ASTNodePtr Condition, ThenBranch, ElseBranch;
        explicit IfNode(int line) : ASTNode(NodeType::IfStmt, line) {}
    };

    struct WhileNode : ASTNode
    {
        ASTNodePtr Condition, Body;
        explicit WhileNode(int line, NodeType type = NodeType::WhileStmt) : ASTNode(type, line) {}
    };

    struct ForNode : ASTNode
    {
        ASTNodePtr Initializer, Condition, Increment, Body;
        explicit ForNode(int line) : ASTNode(NodeType::ForStmt, line) {}
    };

    struct ForeachNode : ASTNode
    {
        std::string VarName;
        std::string ValueVarName;   // foreach (var k, v in map)
        std::string VarType;
        ASTNodePtr  Iterable;
        ASTNodePtr  Body;
        explicit ForeachNode(int line) : ASTNode(NodeType::ForeachStmt, line) {}
    };

    struct CaseNode : ASTNode
    {
        std::vector<ASTNodePtr> Values;    // boş = default
        std::vector<ASTNodePtr> Body;
        bool IsDefault = false;
        explicit CaseNode(int line) : ASTNode(NodeType::CaseStmt, line) {}
    };

    struct SwitchNode : ASTNode
    {
        ASTNodePtr              Expression;
        std::vector<ASTNodePtr> Cases;     // CaseNode (default dahil, kaynak sırasıyla)
        explicit SwitchNode(int line) : ASTNode(NodeType::SwitchStmt, line) {}
    };

    struct ReturnNode : ASTNode
    {
        ASTNodePtr Value;
        explicit ReturnNode(int line) : ASTNode(NodeType::ReturnStmt, line) {}
    };

    struct BreakNode : ASTNode { explicit BreakNode(int line) : ASTNode(NodeType::BreakStmt, line) {} };
    struct ContinueNode : ASTNode { explicit ContinueNode(int line) : ASTNode(NodeType::ContinueStmt, line) {} };

    struct YieldNode : ASTNode
    {
        ASTNodePtr Value;
        explicit YieldNode(int line) : ASTNode(NodeType::YieldStmt, line) {}
    };

    struct TryCatchNode : ASTNode
    {
        ASTNodePtr  TryBody;
        std::string CatchVarName;
        std::string CatchTypeName;
        ASTNodePtr  CatchBody;     // nullable (try/finally)
        ASTNodePtr  FinallyBody;   // nullable
        explicit TryCatchNode(int line) : ASTNode(NodeType::TryCatchStmt, line) {}
    };

    struct ThrowNode : ASTNode
    {
        ASTNodePtr Expression;
        explicit ThrowNode(int line) : ASTNode(NodeType::ThrowStmt, line) {}
    };

    // ═══════════════════════════════════════
    //  EXPRESSIONS
    // ═══════════════════════════════════════

    struct LiteralNode : ASTNode
    {
        Value LiteralValue;          // string ise StringValue kullanılır
        std::string StringValue;
        bool IsString = false;
        LiteralNode(Value val, int line) : ASTNode(NodeType::LiteralExpr, line), LiteralValue(val) {}
        LiteralNode(const std::string& s, int line) : ASTNode(NodeType::LiteralExpr, line), StringValue(s), IsString(true) {}
    };

    struct IdentifierNode : ASTNode
    {
        std::string Name;
        IdentifierNode(const std::string& name, int line, int col = 0) : ASTNode(NodeType::IdentifierExpr, line, col), Name(name) {}
    };

    struct ThisNode : ASTNode { explicit ThisNode(int line) : ASTNode(NodeType::ThisExpr, line) {} };
    struct BaseNode : ASTNode { explicit BaseNode(int line) : ASTNode(NodeType::BaseExpr, line) {} };

    struct BinaryNode : ASTNode
    {
        ASTNodePtr Left;
        TokenType  Operator;
        ASTNodePtr Right;
        BinaryNode(ASTNodePtr left, TokenType op, ASTNodePtr right, int line, NodeType type = NodeType::BinaryExpr)
            : ASTNode(type, line), Left(std::move(left)), Operator(op), Right(std::move(right)) {}
    };

    struct UnaryNode : ASTNode
    {
        TokenType  Operator;
        ASTNodePtr Operand;
        UnaryNode(TokenType op, ASTNodePtr operand, int line) : ASTNode(NodeType::UnaryExpr, line), Operator(op), Operand(std::move(operand)) {}
    };

    // ++x, x++, --x, x--
    struct IncDecNode : ASTNode
    {
        ASTNodePtr Target;
        bool       Increment;
        bool       Prefix;
        IncDecNode(ASTNodePtr target, bool inc, bool prefix, int line)
            : ASTNode(NodeType::IncDecExpr, line), Target(std::move(target)), Increment(inc), Prefix(prefix) {}
    };

    struct AssignNode : ASTNode
    {
        ASTNodePtr Target;
        ASTNodePtr Value;
        AssignNode(ASTNodePtr target, ASTNodePtr value, int line) : ASTNode(NodeType::AssignExpr, line), Target(std::move(target)), Value(std::move(value)) {}
    };

    struct CompoundAssignNode : ASTNode
    {
        ASTNodePtr Target;
        TokenType  Operator;   // ikili işleç (Plus, Minus, ...)
        ASTNodePtr Value;
        CompoundAssignNode(ASTNodePtr target, TokenType op, ASTNodePtr value, int line)
            : ASTNode(NodeType::CompoundAssignExpr, line), Target(std::move(target)), Operator(op), Value(std::move(value)) {}
    };

    struct CallNode : ASTNode
    {
        ASTNodePtr               Callee;
        std::vector<ASTNodePtr>  Arguments;
        std::vector<std::string> GenericArgs;
        bool                     NullConditional = false;   // a?.b()
        CallNode(ASTNodePtr callee, int line) : ASTNode(NodeType::CallExpr, line), Callee(std::move(callee)) {}
    };

    struct MemberAccessNode : ASTNode
    {
        ASTNodePtr  Object;
        std::string Member;
        bool        NullConditional = false;                // a?.b
        MemberAccessNode(ASTNodePtr obj, const std::string& member, int line)
            : ASTNode(NodeType::MemberAccessExpr, line), Object(std::move(obj)), Member(member) {}
    };

    struct IndexNode : ASTNode
    {
        ASTNodePtr Object;
        ASTNodePtr Index;
        IndexNode(ASTNodePtr obj, ASTNodePtr index, int line) : ASTNode(NodeType::IndexExpr, line), Object(std::move(obj)), Index(std::move(index)) {}
    };

    struct NewNode : ASTNode
    {
        ASTNodePtr              ClassExpr;   // Identifier ya da MemberAccess (Namespace.Class)
        std::vector<ASTNodePtr> Arguments;
        std::vector<std::pair<std::string, ASTNodePtr>> Initializers;   // new X() { a = 1, b = 2 }
        ASTNodePtr              ArraySize;   // new int[n]
        std::string             ElementType;
        NewNode(ASTNodePtr classExpr, int line) : ASTNode(NodeType::NewExpr, line), ClassExpr(std::move(classExpr)) {}
    };

    struct LambdaNode : ASTNode
    {
        std::vector<ASTNodePtr> Params;
        ASTNodePtr              Body;          // BlockStmt ya da ifade
        bool                    ExpressionBody = false;
        std::string             Name;
        explicit LambdaNode(int line) : ASTNode(NodeType::LambdaExpr, line) {}
    };

    // expr as Type / expr is Type
    struct TypeTestNode : ASTNode
    {
        ASTNodePtr  Operand;
        std::string TypeName;
        TypeTestNode(NodeType type, ASTNodePtr operand, const std::string& typeName, int line)
            : ASTNode(type, line), Operand(std::move(operand)), TypeName(typeName) {}
    };

    struct TernaryNode : ASTNode
    {
        ASTNodePtr Condition, TrueExpr, FalseExpr;
        explicit TernaryNode(int line) : ASTNode(NodeType::TernaryExpr, line) {}
    };

    struct ListLiteralNode : ASTNode
    {
        std::vector<ASTNodePtr> Elements;
        explicit ListLiteralNode(int line) : ASTNode(NodeType::ListLiteralExpr, line) {}
    };

    struct MapLiteralNode : ASTNode
    {
        std::vector<std::pair<ASTNodePtr, ASTNodePtr>> Entries;
        explicit MapLiteralNode(int line) : ASTNode(NodeType::MapLiteralExpr, line) {}
    };

    struct InterpolatedStringNode : ASTNode
    {
        // Parts: LiteralNode(string) ya da ifade; Formats[i]: ifade için biçim ("F2", "D3", "X" ...)
        std::vector<ASTNodePtr>  Parts;
        std::vector<std::string> Formats;
        explicit InterpolatedStringNode(int line) : ASTNode(NodeType::InterpolatedStringExpr, line) {}
    };
}
