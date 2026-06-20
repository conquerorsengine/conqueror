#include "CQSCompiler.h"

namespace Conqueror::CQS
{
    std::shared_ptr<CQSChunk> CQSCompiler::Compile(ASTNodePtr root)
    {
        m_Chunk = std::make_shared<CQSChunk>();
        m_HasError = false;
        m_Errors.clear();
        m_State = CompilerState();

        Visit(root);
        
        EmitOpCode(OpCode::Halt, root ? root->Line : 0);
        return m_Chunk;
    }

    void CQSCompiler::EmitByte(uint8_t byte, int line)
    {
        m_Chunk->WriteByte(byte, line);
    }

    void CQSCompiler::EmitOpCode(OpCode op, int line)
    {
        m_Chunk->WriteOpCode(op, line);
    }

    void CQSCompiler::EmitConstant(Value value, int line)
    {
        m_Chunk->WriteConstant(value, line);
    }

    int CQSCompiler::EmitJump(OpCode op, int line)
    {
        return m_Chunk->EmitJump(op, line);
    }

    void CQSCompiler::PatchJump(int offset)
    {
        m_Chunk->PatchJump(offset);
    }

    void CQSCompiler::EmitLoop(int loopStart, int line)
    {
        m_Chunk->EmitLoop(loopStart, line);
    }

    void CQSCompiler::Visit(ASTNodePtr node)
    {
        if (!node) return;

        switch (node->Type)
        {
            case NodeType::Program:         VisitProgram(static_cast<ProgramNode*>(node.get())); break;
            case NodeType::UsingStmt:       VisitUsing(static_cast<UsingNode*>(node.get())); break;
            case NodeType::NamespaceStmt:   VisitNamespace(static_cast<NamespaceNode*>(node.get())); break;
            case NodeType::VarDecl:         VisitVarDecl(static_cast<VarDeclNode*>(node.get())); break;
            case NodeType::FuncDecl:        VisitFuncDecl(static_cast<FuncDeclNode*>(node.get())); break;
            case NodeType::ScriptDecl:      VisitScriptDecl(static_cast<ScriptDeclNode*>(node.get())); break;
            case NodeType::ClassDecl:       VisitClassDecl(static_cast<ClassDeclNode*>(node.get())); break;
            case NodeType::StructDecl:      VisitStructDecl(static_cast<StructDeclNode*>(node.get())); break;
            case NodeType::EnumDecl:        VisitEnumDecl(static_cast<EnumDeclNode*>(node.get())); break;
            case NodeType::InterfaceDecl:   VisitInterfaceDecl(static_cast<InterfaceDeclNode*>(node.get())); break;
            
            case NodeType::BlockStmt:       VisitBlock(static_cast<BlockNode*>(node.get())); break;
            case NodeType::ExpressionStmt:  VisitExpressionStmt(static_cast<ExprStmtNode*>(node.get())); break;
            case NodeType::IfStmt:          VisitIf(static_cast<IfNode*>(node.get())); break;
            case NodeType::WhileStmt:       VisitWhile(static_cast<WhileNode*>(node.get())); break;
            case NodeType::ForStmt:         VisitFor(static_cast<ForNode*>(node.get())); break;
            case NodeType::ForeachStmt:     VisitForeach(static_cast<ForeachNode*>(node.get())); break;
            case NodeType::SwitchStmt:      VisitSwitch(static_cast<SwitchNode*>(node.get())); break;
            case NodeType::ReturnStmt:      VisitReturn(static_cast<ReturnNode*>(node.get())); break;
            case NodeType::BreakStmt:       VisitBreak(static_cast<BreakNode*>(node.get())); break;
            case NodeType::ContinueStmt:    VisitContinue(static_cast<ContinueNode*>(node.get())); break;
            
            case NodeType::LiteralExpr:     VisitLiteral(static_cast<LiteralNode*>(node.get())); break;
            case NodeType::BinaryExpr:      VisitBinary(static_cast<BinaryNode*>(node.get())); break;
            case NodeType::UnaryExpr:       VisitUnary(static_cast<UnaryNode*>(node.get())); break;
            case NodeType::IdentifierExpr:  VisitIdentifier(static_cast<IdentifierNode*>(node.get())); break;
            case NodeType::AssignExpr:      VisitAssign(static_cast<AssignNode*>(node.get())); break;
            case NodeType::CompoundAssignExpr: VisitCompoundAssign(static_cast<CompoundAssignNode*>(node.get())); break;
            case NodeType::CallExpr:        VisitCall(static_cast<CallNode*>(node.get())); break;
            case NodeType::MemberAccessExpr: VisitMemberAccess(static_cast<MemberAccessNode*>(node.get())); break;
            case NodeType::IndexExpr:       VisitIndex(static_cast<IndexNode*>(node.get())); break;
            case NodeType::NewExpr:         VisitNew(static_cast<NewNode*>(node.get())); break;
            case NodeType::ThisExpr:        VisitThis(static_cast<ThisNode*>(node.get())); break;
            case NodeType::ListLiteralExpr: VisitListLiteral(static_cast<ListLiteralNode*>(node.get())); break;
            case NodeType::MapLiteralExpr:  VisitMapLiteral(static_cast<MapLiteralNode*>(node.get())); break;
            case NodeType::TernaryExpr:     VisitTernary(static_cast<TernaryNode*>(node.get())); break;
            case NodeType::LambdaExpr:      VisitLambda(static_cast<LambdaNode*>(node.get())); break;
            case NodeType::InterpolatedStringExpr: VisitInterpolatedString(static_cast<InterpolatedStringNode*>(node.get())); break;
            
            default:
                break;
        }
    }

    void CQSCompiler::VisitProgram(ProgramNode* node)
    {
        for (auto& stmt : node->Statements)
        {
            Visit(stmt);
        }
    }

    void CQSCompiler::VisitVarDecl(VarDeclNode* node)
    {
        if (node->Initializer)
        {
            Visit(node->Initializer);
        }
        else
        {
            EmitOpCode(OpCode::Null, node->Line);
        }

        if (m_State.ScopeDepth > 0)
        {
            DeclareVariable(node->Name, node->Line);
        }
        else
        {
            // Global variable: name must be stored as a constant string
            uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->Name)); 
            EmitByte(static_cast<uint8_t>(OpCode::DefineGlobal), node->Line);
            m_Chunk->WriteShort(nameIndex, node->Line);
        }
    }

    void CQSCompiler::VisitFuncDecl(FuncDeclNode* node)
    {
        std::string fullFuncName = node->Name;
        if (!m_CurrentClassName.empty())
            fullFuncName = m_CurrentClassName + "." + fullFuncName;

        // 1. Yeni fonksiyon objesi oluştur
        auto* function = new CQSFunctionObject(fullFuncName, node->Params.size());
        
        // 2. Mevcut chunk'ı ve durumu (state) kaydet, yeni bir tane aç
        auto enclosingChunk = m_Chunk;
        auto enclosingState = m_State;
        
        m_Chunk = std::make_shared<CQSChunk>();
        m_Chunk->Name = fullFuncName;
        function->ChunkData = m_Chunk;
        
        m_State = CompilerState(); // Local sayacını sıfırla

        // 3. Fonksiyon için yeni bir kapsam (scope) başlat
        BeginScope();

        // 4. Parametreleri yerel değişken olarak kaydet
        for (auto& paramPtr : node->Params)
        {
            auto* param = static_cast<ParamNode*>(paramPtr.get());
            DeclareVariable(param->Name, node->Line);
        }

        // 5. Fonksiyon gövdesini derle
        if (node->Body)
        {
            Visit(node->Body);
        }

        // 6. Fonksiyonun sonuna gizli bir return (null) ekle
        EmitOpCode(OpCode::Null, node->Line);
        EmitOpCode(OpCode::Return, node->Line);

        // Kapsamı kapat
        EndScope(node->Line);

        // 7. Eski chunk'a ve state'e dön
        m_Chunk = enclosingChunk;
        m_State = enclosingState;

        // 8. Fonksiyon objesini sabitler (constants) arasına ekle ve stack'e al
        uint8_t funcIndex = (uint8_t)m_Chunk->AddConstant(Value::MakeObject(function));
        EmitByte(static_cast<uint8_t>(OpCode::Constant), node->Line);
        EmitByte(funcIndex, node->Line);

        // 9. Fonksiyon adını Global veya Yerel tabloya kaydet
        if (m_State.ScopeDepth == 0)
        {
             uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(fullFuncName));
             EmitByte(static_cast<uint8_t>(OpCode::DefineGlobal), node->Line);
             m_Chunk->WriteShort(nameIndex, node->Line);
        }
        else
        {
            DeclareVariable(node->Name, node->Line);
        }
    }

    void CQSCompiler::VisitUsing(UsingNode* node)
    {
        // Using support placeholder
    }

    void CQSCompiler::VisitNamespace(NamespaceNode* node)
    {
        for (auto& stmt : node->Body) Visit(stmt);
    }

    void CQSCompiler::VisitClassDecl(ClassDeclNode* node)
    {
        uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->Name));
        EmitByte(static_cast<uint8_t>(OpCode::DefineGlobal), node->Line);
        m_Chunk->WriteShort(nameIndex, node->Line);
        for (auto& member : node->Members) Visit(member);
    }

    void CQSCompiler::VisitStructDecl(StructDeclNode* node)
    {
        for (auto& member : node->Members) Visit(member);
    }

    void CQSCompiler::VisitEnumDecl(EnumDeclNode* node)
    {
        // Enum implementation
    }

    void CQSCompiler::VisitInterfaceDecl(InterfaceDeclNode* node)
    {
        // Interface implementation
    }

    void CQSCompiler::VisitScriptDecl(ScriptDeclNode* node)
    {
        // Define the script class (placeholder null value for now)
        EmitOpCode(OpCode::Null, node->Line);

        uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->Name));
        EmitByte(static_cast<uint8_t>(OpCode::DefineGlobal), node->Line);
        m_Chunk->WriteShort(nameIndex, node->Line);
        
        std::string oldClassName = m_CurrentClassName;
        m_CurrentClassName = node->Name;

        // Visit members (methods and fields)
        for (auto& member : node->Members)
        {
            Visit(member);
        }

        m_CurrentClassName = oldClassName;
    }

    void CQSCompiler::VisitBlock(BlockNode* node)
    {
        BeginScope();
        for (auto& stmt : node->Statements)
        {
            Visit(stmt);
        }
        EndScope(node->Line);
    }

    void CQSCompiler::VisitExpressionStmt(ExprStmtNode* node)
    {
        Visit(node->Expression);
        EmitOpCode(OpCode::Pop, node->Line);
    }

    void CQSCompiler::VisitIf(IfNode* node)
    {
        Visit(node->Condition);
        int thenJump = EmitJump(OpCode::JumpIfFalse, node->Line);
        EmitOpCode(OpCode::Pop, node->Line); // Condition pop

        Visit(node->ThenBranch);
        int elseJump = EmitJump(OpCode::Jump, node->Line);

        PatchJump(thenJump);
        EmitOpCode(OpCode::Pop, node->Line); // Condition pop for else

        if (node->ElseBranch)
        {
            Visit(node->ElseBranch);
        }
        PatchJump(elseJump);
    }

    void CQSCompiler::VisitWhile(WhileNode* node)
    {
        int loopStart = m_Chunk->Size();
        m_State.LoopStack.push_back({loopStart, {}});

        Visit(node->Condition);
        int exitJump = EmitJump(OpCode::JumpIfFalse, node->Line);
        EmitOpCode(OpCode::Pop, node->Line);

        Visit(node->Body);
        EmitLoop(loopStart, node->Line);

        PatchJump(exitJump);
        EmitOpCode(OpCode::Pop, node->Line);

        // Patch all break jumps
        auto& ctx = m_State.LoopStack.back();
        for (int bj : ctx.BreakJumps) PatchJump(bj);
        m_State.LoopStack.pop_back();
    }

    void CQSCompiler::VisitReturn(ReturnNode* node)
    {
        if (node->Value)
        {
            Visit(node->Value);
        }
        else
        {
            EmitOpCode(OpCode::Null, node->Line);
        }
        EmitOpCode(OpCode::Return, node->Line);
    }
    void CQSCompiler::VisitBreak(BreakNode* node)
    {
        if (m_State.LoopStack.empty()) {
            Error(node->Line, "Cannot use 'break' outside of a loop.");
            return;
        }
        int jump = EmitJump(OpCode::Jump, node->Line);
        m_State.LoopStack.back().BreakJumps.push_back(jump);
    }

    void CQSCompiler::VisitContinue(ContinueNode* node)
    {
        if (m_State.LoopStack.empty()) {
            Error(node->Line, "Cannot use 'continue' outside of a loop.");
            return;
        }
        auto& ctx = m_State.LoopStack.back();
        if (ctx.IsForLoop) {
            // For loops: forward jump to increment section, patched later
            int jump = EmitJump(OpCode::Jump, node->Line);
            ctx.ContinueJumps.push_back(jump);
        } else {
            // While loops: backward jump to condition
            EmitLoop(ctx.LoopStart, node->Line);
        }
    }

    void CQSCompiler::VisitFor(ForNode* node)
    {
        BeginScope();
        if (node->Initializer) Visit(node->Initializer);
        
        int loopStart = m_Chunk->Size();
        m_State.LoopStack.push_back({loopStart, {}, {}, true});

        int exitJump = -1;
        if (node->Condition)
        {
            Visit(node->Condition);
            exitJump = EmitJump(OpCode::JumpIfFalse, node->Line);
            EmitOpCode(OpCode::Pop, node->Line);
        }

        Visit(node->Body);

        // Patch continue jumps to here (before increment)
        auto& ctx = m_State.LoopStack.back();
        for (int cj : ctx.ContinueJumps) PatchJump(cj);

        if (node->Increment)
        {
            Visit(node->Increment);
            EmitOpCode(OpCode::Pop, node->Line);
        }

        EmitLoop(loopStart, node->Line);

        if (exitJump != -1)
        {
            PatchJump(exitJump);
            EmitOpCode(OpCode::Pop, node->Line);
        }

        // Patch all break jumps
        for (int bj : ctx.BreakJumps) PatchJump(bj);
        m_State.LoopStack.pop_back();

        EndScope(node->Line);
    }

    void CQSCompiler::VisitForeach(ForeachNode* node)
    {
        BeginScope();
        // Evaluate iterable and store it
        Visit(node->Iterable);
        DeclareVariable("__iterable", node->Line);

        // Create index variable initialized to 0
        EmitOpCode(OpCode::Zero, node->Line);
        DeclareVariable("__index", node->Line);

        // Create the user's variable
        EmitOpCode(OpCode::Null, node->Line);
        DeclareVariable(node->VarName, node->Line);

        int loopStart = m_Chunk->Size();
        m_State.LoopStack.push_back({loopStart, {}});

        // Condition: __index < len(__iterable) — simplified: use GetIndex, if null break
        // Load __index
        int idxSlot = ResolveLocal("__index");
        int iterSlot = ResolveLocal("__iterable");
        int varSlot = ResolveLocal(node->VarName);

        // Get element: __iterable[__index]
        EmitByte(static_cast<uint8_t>(OpCode::GetLocal), node->Line);
        EmitByte(static_cast<uint8_t>(iterSlot), node->Line);
        EmitByte(static_cast<uint8_t>(OpCode::GetLocal), node->Line);
        EmitByte(static_cast<uint8_t>(idxSlot), node->Line);
        EmitOpCode(OpCode::GetIndex, node->Line);

        // If null, exit loop
        EmitOpCode(OpCode::Dup, node->Line);
        EmitOpCode(OpCode::Null, node->Line);
        EmitOpCode(OpCode::Equal, node->Line);
        int exitJump = EmitJump(OpCode::JumpIfTrue, node->Line);
        EmitOpCode(OpCode::Pop, node->Line); // pop the bool

        // Store element in user variable
        EmitByte(static_cast<uint8_t>(OpCode::SetLocal), node->Line);
        EmitByte(static_cast<uint8_t>(varSlot), node->Line);
        EmitOpCode(OpCode::Pop, node->Line);

        // Body
        Visit(node->Body);

        // Increment __index
        EmitByte(static_cast<uint8_t>(OpCode::GetLocal), node->Line);
        EmitByte(static_cast<uint8_t>(idxSlot), node->Line);
        EmitOpCode(OpCode::Increment, node->Line);
        EmitByte(static_cast<uint8_t>(OpCode::SetLocal), node->Line);
        EmitByte(static_cast<uint8_t>(idxSlot), node->Line);
        EmitOpCode(OpCode::Pop, node->Line);

        EmitLoop(loopStart, node->Line);

        PatchJump(exitJump);
        EmitOpCode(OpCode::Pop, node->Line); // pop the bool
        EmitOpCode(OpCode::Pop, node->Line); // pop the null element

        auto& ctx = m_State.LoopStack.back();
        for (int bj : ctx.BreakJumps) PatchJump(bj);
        m_State.LoopStack.pop_back();

        EndScope(node->Line);
    }

    void CQSCompiler::VisitSwitch(SwitchNode* node)
    {
        Visit(node->Expression);
        std::vector<int> caseEndJumps;

        for (auto& caseNode : node->Cases)
        {
            auto* cNode = static_cast<CaseNode*>(caseNode.get());
            // Duplicate the switch value for comparison
            EmitOpCode(OpCode::Dup, node->Line);
            Visit(cNode->Value);
            EmitOpCode(OpCode::Equal, cNode->Line);

            int skipJump = EmitJump(OpCode::JumpIfFalse, cNode->Line);
            EmitOpCode(OpCode::Pop, cNode->Line); // pop bool

            for (auto& stmt : cNode->Body) Visit(stmt);

            caseEndJumps.push_back(EmitJump(OpCode::Jump, cNode->Line));
            PatchJump(skipJump);
            EmitOpCode(OpCode::Pop, cNode->Line); // pop bool
        }

        // Default case
        if (node->DefaultCase) Visit(node->DefaultCase);

        for (int j : caseEndJumps) PatchJump(j);

        EmitOpCode(OpCode::Pop, node->Line); // pop switch value
    }
    void CQSCompiler::VisitLiteral(LiteralNode* node)
    {
        switch (node->LiteralValue.Type)
        {
            case ValueType::Null:  EmitOpCode(OpCode::Null, node->Line); break;
            case ValueType::Bool:  EmitOpCode(node->LiteralValue.BoolVal ? OpCode::True : OpCode::False, node->Line); break;
            case ValueType::Int:
                if (node->LiteralValue.IntVal == 0) EmitOpCode(OpCode::Zero, node->Line);
                else if (node->LiteralValue.IntVal == 1) EmitOpCode(OpCode::One, node->Line);
                else EmitConstant(node->LiteralValue, node->Line);
                break;
            default:
                EmitConstant(node->LiteralValue, node->Line);
                break;
        }
    }

    void CQSCompiler::VisitBinary(BinaryNode* node)
    {
        Visit(node->Left);
        Visit(node->Right);

        switch (node->Operator)
        {
            case TokenType::Plus:         EmitOpCode(OpCode::Add, node->Line); break;
            case TokenType::Minus:        EmitOpCode(OpCode::Subtract, node->Line); break;
            case TokenType::Star:         EmitOpCode(OpCode::Multiply, node->Line); break;
            case TokenType::Slash:        EmitOpCode(OpCode::Divide, node->Line); break;
            case TokenType::Percent:      EmitOpCode(OpCode::Modulo, node->Line); break;
            case TokenType::EqualEqual:   EmitOpCode(OpCode::Equal, node->Line); break;
            case TokenType::BangEqual:    EmitOpCode(OpCode::NotEqual, node->Line); break;
            case TokenType::Less:         EmitOpCode(OpCode::Less, node->Line); break;
            case TokenType::LessEqual:    EmitOpCode(OpCode::LessEqual, node->Line); break;
            case TokenType::Greater:      EmitOpCode(OpCode::Greater, node->Line); break;
            case TokenType::GreaterEqual: EmitOpCode(OpCode::GreaterEqual, node->Line); break;
            case TokenType::AmpAmp:       EmitOpCode(OpCode::And, node->Line); break;
            case TokenType::PipePipe:     EmitOpCode(OpCode::Or, node->Line); break;
            case TokenType::Amp:          EmitOpCode(OpCode::BitAnd, node->Line); break;
            case TokenType::Pipe:         EmitOpCode(OpCode::BitOr, node->Line); break;
            case TokenType::Caret:        EmitOpCode(OpCode::BitXor, node->Line); break;
            case TokenType::LessLess:     EmitOpCode(OpCode::ShiftLeft, node->Line); break;
            case TokenType::GreaterGreater: EmitOpCode(OpCode::ShiftRight, node->Line); break;
            default: break;
        }
    }

    void CQSCompiler::VisitUnary(UnaryNode* node)
    {
        Visit(node->Operand);
        switch (node->Operator)
        {
            case TokenType::Minus:      EmitOpCode(OpCode::Negate, node->Line); break;
            case TokenType::Bang:       EmitOpCode(OpCode::Not, node->Line); break;
            case TokenType::Tilde:      EmitOpCode(OpCode::BitNot, node->Line); break;
            case TokenType::PlusPlus:   EmitOpCode(OpCode::Increment, node->Line); break;
            case TokenType::MinusMinus: EmitOpCode(OpCode::Decrement, node->Line); break;
            default: break;
        }
    }

    void CQSCompiler::VisitIdentifier(IdentifierNode* node)
    {
        int arg = ResolveLocal(node->Name);
        if (arg != -1)
        {
            EmitByte(static_cast<uint8_t>(OpCode::GetLocal), node->Line);
            EmitByte(static_cast<uint8_t>(arg), node->Line);
        }
        else
        {
            // Global
            uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->Name));
            EmitByte(static_cast<uint8_t>(OpCode::GetGlobal), node->Line);
            m_Chunk->WriteShort(nameIndex, node->Line);
        }
    }

    void CQSCompiler::VisitAssign(AssignNode* node)
    {
        Visit(node->Value);
        if (node->Target->Type == NodeType::IdentifierExpr)
        {
            auto* id = static_cast<IdentifierNode*>(node->Target.get());
            int arg = ResolveLocal(id->Name);
            if (arg != -1)
            {
                EmitByte(static_cast<uint8_t>(OpCode::SetLocal), node->Line);
                EmitByte(static_cast<uint8_t>(arg), node->Line);
            }
            else
            {
                // Global variable
                uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(id->Name));
                EmitByte(static_cast<uint8_t>(OpCode::SetGlobal), node->Line);
                m_Chunk->WriteShort(nameIndex, node->Line);
            }
        }
        else if (node->Target->Type == NodeType::MemberAccessExpr)
        {
            auto* member = static_cast<MemberAccessNode*>(node->Target.get());
            Visit(member->Object);
            uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(member->Member));
            EmitByte(static_cast<uint8_t>(OpCode::SetProperty), node->Line);
            m_Chunk->WriteShort(nameIndex, node->Line);
        }
        else if (node->Target->Type == NodeType::IndexExpr)
        {
            auto* idx = static_cast<IndexNode*>(node->Target.get());
            Visit(idx->Object);
            Visit(idx->Index);
            EmitOpCode(OpCode::SetIndex, node->Line);
        }
    }

    void CQSCompiler::VisitCall(CallNode* node)
    {
        Visit(node->Callee);
        for (auto& arg : node->Arguments)
        {
            Visit(arg);
        }
        EmitByte(static_cast<uint8_t>(OpCode::Call), node->Line);
        EmitByte(static_cast<uint8_t>(node->Arguments.size()), node->Line);
    }

    void CQSCompiler::VisitMemberAccess(MemberAccessNode* node)
    {
        Visit(node->Object);
        uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->Member));
        EmitByte(static_cast<uint8_t>(OpCode::GetProperty), node->Line);
        m_Chunk->WriteShort(nameIndex, node->Line);
    }
    void CQSCompiler::VisitIndex(IndexNode* node)
    {
        Visit(node->Object);
        Visit(node->Index);
        EmitOpCode(OpCode::GetIndex, node->Line);
    }

    void CQSCompiler::VisitNew(NewNode* node)
    {
        for (auto& arg : node->Arguments) Visit(arg);
        uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(node->ClassName));
        EmitByte(static_cast<uint8_t>(OpCode::New), node->Line);
        m_Chunk->WriteShort(nameIndex, node->Line);
        EmitByte(static_cast<uint8_t>(node->Arguments.size()), node->Line);
    }

    void CQSCompiler::VisitThis(ThisNode* node)
    {
        EmitOpCode(OpCode::This, node->Line);
    }
    void CQSCompiler::DeclareVariable(const std::string& name, int line)
    {
        for (int i = m_State.LocalCount - 1; i >= 0; i--)
        {
            if (m_State.Locals[i].Depth != -1 && m_State.Locals[i].Depth < m_State.ScopeDepth) break;
            if (m_State.Locals[i].Name == name)
            {
                Error(line, "Variable with this name already declared in this scope.");
            }
        }
        m_State.Locals.push_back({name, m_State.ScopeDepth, false});
        m_State.LocalCount++;
    }

    int CQSCompiler::ResolveLocal(const std::string& name)
    {
        for (int i = m_State.LocalCount - 1; i >= 0; i--)
        {
            if (m_State.Locals[i].Name == name) return i;
        }
        return -1;
    }

    void CQSCompiler::BeginScope()
    {
        m_State.ScopeDepth++;
    }

    void CQSCompiler::EndScope(int line)
    {
        m_State.ScopeDepth--;
        while (m_State.LocalCount > 0 && m_State.Locals[m_State.LocalCount - 1].Depth > m_State.ScopeDepth)
        {
            EmitOpCode(OpCode::Pop, line);
            m_State.Locals.pop_back();
            m_State.LocalCount--;
        }
    }

    void CQSCompiler::Error(int line, const std::string& message)
    {
        m_HasError = true;
        m_Errors.push_back("Line " + std::to_string(line) + ": " + message);
    }

    // ═══════════════════════════════════════
    //  NEW VISITORS
    // ═══════════════════════════════════════

    void CQSCompiler::VisitCompoundAssign(CompoundAssignNode* node)
    {
        // Load the target value
        Visit(node->Target);
        // Load the right-hand side
        Visit(node->Value);
        // Emit the operator
        switch (node->Operator)
        {
            case TokenType::PlusEqual:    EmitOpCode(OpCode::Add, node->Line); break;
            case TokenType::MinusEqual:   EmitOpCode(OpCode::Subtract, node->Line); break;
            case TokenType::StarEqual:    EmitOpCode(OpCode::Multiply, node->Line); break;
            case TokenType::SlashEqual:   EmitOpCode(OpCode::Divide, node->Line); break;
            case TokenType::PercentEqual: EmitOpCode(OpCode::Modulo, node->Line); break;
            default: break;
        }
        // Store back
        if (node->Target->Type == NodeType::IdentifierExpr)
        {
            auto* id = static_cast<IdentifierNode*>(node->Target.get());
            int arg = ResolveLocal(id->Name);
            if (arg != -1) {
                EmitByte(static_cast<uint8_t>(OpCode::SetLocal), node->Line);
                EmitByte(static_cast<uint8_t>(arg), node->Line);
            } else {
                uint16_t nameIndex = (uint16_t)m_Chunk->AddConstant(Value::MakeString(id->Name));
                EmitByte(static_cast<uint8_t>(OpCode::SetGlobal), node->Line);
                m_Chunk->WriteShort(nameIndex, node->Line);
            }
        }
    }

    void CQSCompiler::VisitTernary(TernaryNode* node)
    {
        Visit(node->Condition);
        int thenJump = EmitJump(OpCode::JumpIfFalse, node->Line);
        EmitOpCode(OpCode::Pop, node->Line);
        Visit(node->TrueExpr);
        int elseJump = EmitJump(OpCode::Jump, node->Line);
        PatchJump(thenJump);
        EmitOpCode(OpCode::Pop, node->Line);
        Visit(node->FalseExpr);
        PatchJump(elseJump);
    }

    void CQSCompiler::VisitListLiteral(ListLiteralNode* node)
    {
        for (auto& elem : node->Elements)
            Visit(elem);
        EmitByte(static_cast<uint8_t>(OpCode::NewList), node->Line);
        EmitByte(static_cast<uint8_t>(node->Elements.size()), node->Line);
    }

    void CQSCompiler::VisitMapLiteral(MapLiteralNode* node)
    {
        for (auto& [key, val] : node->Entries)
        {
            Visit(key);
            Visit(val);
        }
        EmitByte(static_cast<uint8_t>(OpCode::NewMap), node->Line);
        EmitByte(static_cast<uint8_t>(node->Entries.size()), node->Line);
    }

    void CQSCompiler::VisitLambda(LambdaNode* node)
    {
        // Compile lambda as anonymous function
        auto* function = new CQSFunctionObject("<lambda>", node->Params.size());
        auto enclosingChunk = m_Chunk;
        auto enclosingState = m_State;

        m_Chunk = std::make_shared<CQSChunk>();
        m_Chunk->Name = "<lambda>";
        function->ChunkData = m_Chunk;
        m_State = CompilerState();

        BeginScope();
        for (auto& paramPtr : node->Params)
        {
            auto* param = static_cast<ParamNode*>(paramPtr.get());
            DeclareVariable(param->Name, node->Line);
        }

        Visit(node->Body);

        // If body is an expression (not a block), return its value
        if (node->Body && node->Body->Type != NodeType::BlockStmt)
        {
            EmitOpCode(OpCode::Return, node->Line);
        }
        else
        {
            EmitOpCode(OpCode::Null, node->Line);
            EmitOpCode(OpCode::Return, node->Line);
        }

        EndScope(node->Line);
        m_Chunk = enclosingChunk;
        m_State = enclosingState;

        uint8_t funcIndex = (uint8_t)m_Chunk->AddConstant(Value::MakeObject(function));
        EmitByte(static_cast<uint8_t>(OpCode::Constant), node->Line);
        EmitByte(funcIndex, node->Line);
    }

    void CQSCompiler::VisitInterpolatedString(InterpolatedStringNode* node)
    {
        // Concatenate all parts: push first part, then add each subsequent part
        if (node->Parts.empty())
        {
            EmitConstant(Value::MakeString(""), node->Line);
            return;
        }

        Visit(node->Parts[0]);
        // Ensure it's a string
        EmitOpCode(OpCode::CastToString, node->Line);

        for (size_t i = 1; i < node->Parts.size(); i++)
        {
            Visit(node->Parts[i]);
            EmitOpCode(OpCode::CastToString, node->Line);
            EmitOpCode(OpCode::Add, node->Line);
        }
    }
}
