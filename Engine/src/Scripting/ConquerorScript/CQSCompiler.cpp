#include "CQSCompiler.h"
#include "CQSVM.h"

#include <algorithm>
#include <cmath>

namespace Conqueror::CQS
{
    namespace
    {
        // Native/motor taban sınıfları: alan yerleşimine katkıları yok
        bool IsNativeBaseClass(const std::string& name)
        {
            static const std::unordered_set<std::string> s_Names = {
                "", "ConquerorScript", "MonoBehaviour", "Behaviour", "Script", "Component", "Object", "ScriptableObject", "DataAsset",
                "CQScript", "EntityScript", "NativeScript",
            };
            return s_Names.count(name) != 0;
        }

        // using/namespace ile önek olarak kullanılabilen, globallere çözülen adlar
        const std::unordered_set<std::string>& BuiltinNamespaces()
        {
            static const std::unordered_set<std::string> s_Names = {
                "nativebridge", "NativeBridge", "Conqueror", "CQ", "System", "UnityEngine", "Engine",
            };
            return s_Names;
        }

        constexpr int kMaxLocals = 60000;
    }

    // ════════════════════════════════════════
    //  Errors
    // ════════════════════════════════════════
    void CQSCompiler::Error(int line, const std::string& message)
    {
        if (line <= 0) line = m_Line;
        m_Errors.push_back("Line " + std::to_string(line) + ": " + message);
    }

    void CQSCompiler::Warning(int line, const std::string& message)
    {
        if (line <= 0) line = m_Line;
        m_Warnings.push_back("Line " + std::to_string(line) + ": " + message);
    }

    // ════════════════════════════════════════
    //  Emit
    // ════════════════════════════════════════
    int CQSCompiler::Emit(OpCode op, int a, int b, int c)
    {
        CQSFunctionObject* fn = m_Fn->Fn;
        CQSInstr ins;
        ins.Op = (uint8_t)op;
        ins.A = (uint8_t)a;
        ins.B = (uint16_t)b;
        ins.C = c;
        fn->Code.push_back(ins);
        fn->Lines.push_back(m_Line);
        if (op == OpCode::GetGlobal || op == OpCode::SetGlobal)
            m_GlobalRefs.emplace(c, m_Line);
        else if (op == OpCode::DefineGlobal)
            m_GlobalDefs.insert(c);
        m_Fn->Depth += StackEffect(op, a, b);
        if (m_Fn->Depth > fn->MaxStack)
            fn->MaxStack = m_Fn->Depth;
        if (m_Fn->Depth < 0)
        {
            // Derleyici hatası: asla olmamalı
            Error(m_Line, std::string("internal compiler error: negative stack depth at ") + OpCodeName(op));
            m_Fn->Depth = 0;
        }
        return (int)fn->Code.size() - 1;
    }

    int CQSCompiler::EmitJump(OpCode op)
    {
        return Emit(op, 0, 0, -1);
    }

    int CQSCompiler::CurrentPos() const
    {
        return (int)m_Fn->Fn->Code.size();
    }

    void CQSCompiler::PatchJump(int at)
    {
        PatchJumpTo(at, CurrentPos());
    }

    void CQSCompiler::PatchJumpTo(int at, int target)
    {
        if (at >= 0)
            m_Fn->Fn->Code[(size_t)at].C = target;
    }

    void CQSCompiler::EmitLoop(int target)
    {
        Emit(OpCode::Loop, 0, 0, target);
    }

    int CQSCompiler::AddConstant(const Value& v)
    {
        auto& consts = m_Fn->Fn->Constants;
        // Sayısal sabitleri tekrar kullan
        if (v.IsInt() || v.IsFloat() || v.IsBool() || v.IsNull())
        {
            for (size_t i = 0; i < consts.size(); ++i)
                if (consts[i].Type == v.Type && consts[i].IntVal == v.IntVal)
                    return (int)i;
        }
        if (consts.size() >= 65535)
        {
            Error(m_Line, "Too many constants in one function (max 65535).");
            return 0;
        }
        consts.push_back(v);
        return (int)consts.size() - 1;
    }

    int CQSCompiler::StringConstant(const std::string& s)
    {
        auto it = m_Fn->ConstStrings.find(s);
        if (it != m_Fn->ConstStrings.end())
            return it->second;
        int idx = AddConstant(Value::MakeString(s));
        m_Fn->ConstStrings[s] = idx;
        return idx;
    }

    int CQSCompiler::NameConstant(const std::string& s)
    {
        auto it = m_Fn->NameIndex.find(s);
        if (it != m_Fn->NameIndex.end())
            return it->second;
        auto& names = m_Fn->Fn->Names;
        if (names.size() >= 65535)
        {
            Error(m_Line, "Too many names in one function.");
            return 0;
        }
        names.push_back(s);
        int idx = (int)names.size() - 1;
        m_Fn->NameIndex[s] = idx;
        return idx;
    }

    void CQSCompiler::EmitConstant(const Value& v)
    {
        if (v.IsNull()) { Emit(OpCode::PushNull); return; }
        if (v.IsBool()) { Emit(v.AsBool() ? OpCode::PushTrue : OpCode::PushFalse); return; }
        if (v.IsInt() && v.AsInt() >= INT32_MIN && v.AsInt() <= INT32_MAX)
        {
            Emit(OpCode::PushInt, 0, 0, (int32_t)v.AsInt());
            return;
        }
        Emit(OpCode::PushConst, 0, AddConstant(v));
    }

    int CQSCompiler::GlobalSlot(const std::string& name)
    {
        return m_VM->GlobalSlot(name);
    }

    // ════════════════════════════════════════
    //  Entry points
    // ════════════════════════════════════════
    void CQSCompiler::BeginFunction(FunctionState& state, const std::string& name, bool isMethod)
    {
        state.Enclosing = m_Fn;
        state.Fn = new CQSFunctionObject(name, 0);
        state.Fn->SourceName = m_SourceName;
        state.Fn->IsMethod = isMethod;
        state.IsMethod = isMethod;
        state.Class = m_Class;
        // slot 0: this (metot) ya da çağrılan fonksiyon
        Local self;
        self.Name = isMethod ? "this" : "";
        self.Slot = 0;
        state.Locals.push_back(self);
        state.Depth = 1;
        state.Fn->MaxStack = 1;
        m_Fn = &state;
    }

    CQSFunctionObject* CQSCompiler::EndFunction()
    {
        CQSFunctionObject* fn = m_Fn->Fn;
        fn->UpvalueCount = (int)m_Fn->Upvalues.size();
        for (auto& up : m_Fn->Upvalues)
        {
            CQSUpvalueDesc d;
            d.IsLocal = up.IsLocal;
            d.Index = up.Index;
            fn->Upvalues.push_back(d);
        }
        m_Fn = m_Fn->Enclosing;
        return fn;
    }

    CQSFunctionObject* CQSCompiler::Compile(const ASTNodePtr& root, const std::string& sourceName)
    {
        m_SourceName = sourceName;
        m_Errors.clear();
        m_Warnings.clear();
        m_Namespaces = BuiltinNamespaces();

        FunctionState module;
        BeginFunction(module, "<module>", false);
        m_Line = 1;
        if (root && root->Type == NodeType::Program)
        {
            for (auto& stmt : static_cast<ProgramNode*>(root.get())->Statements)
                CompileStatement(stmt);
        }
        Emit(OpCode::ReturnNull);
        CQSFunctionObject* fn = EndFunction();

        // Tanımsız adlar: ne bu dosyada ne de VM'de (native, yerleşik, yüklü betikler) tanımlı
        std::vector<std::pair<int, std::string>> unknown;
        for (auto& [slot, line] : m_GlobalRefs)
            if (!m_GlobalDefs.count(slot) && !m_VM->IsGlobalDefined(slot))
                unknown.emplace_back(line, m_VM->GetGlobalName(slot));
        std::sort(unknown.begin(), unknown.end());
        for (auto& [line, name] : unknown)
            Warning(line, "Unknown identifier '" + name + "' (not declared in this script or the engine API).");
        return m_Errors.empty() ? fn : nullptr;
    }

    CQSFunctionObject* CQSCompiler::CompileExpression(const ASTNodePtr& expr, const std::string& sourceName)
    {
        m_SourceName = sourceName;
        m_Errors.clear();
        m_Namespaces = BuiltinNamespaces();
        FunctionState fs;
        BeginFunction(fs, "<eval>", false);
        m_Line = expr ? expr->Line : 1;
        CompileExpr(expr);
        Emit(OpCode::Return);
        CQSFunctionObject* fn = EndFunction();
        return m_Errors.empty() ? fn : nullptr;
    }

    // ════════════════════════════════════════
    //  Scopes & locals
    // ════════════════════════════════════════
    void CQSCompiler::BeginScope()
    {
        ++m_Fn->ScopeDepth;
    }

    void CQSCompiler::EndScope()
    {
        --m_Fn->ScopeDepth;
        auto& locals = m_Fn->Locals;
        int pending = 0;
        auto flush = [&]() {
            if (pending == 1) Emit(OpCode::Pop);
            else if (pending > 1) Emit(OpCode::PopN, 0, pending);
            pending = 0;
        };
        while (!locals.empty() && locals.back().ScopeDepth > m_Fn->ScopeDepth)
        {
            if (locals.back().Captured)
            {
                flush();
                Emit(OpCode::CloseUpval);
            }
            else
                ++pending;
            locals.pop_back();
        }
        flush();
    }

    void CQSCompiler::EmitPopLocalsAbove(size_t localCount)
    {
        auto& locals = m_Fn->Locals;
        int savedDepth = Depth();
        int pending = 0;
        auto flush = [&]() {
            if (pending == 1) Emit(OpCode::Pop);
            else if (pending > 1) Emit(OpCode::PopN, 0, pending);
            pending = 0;
        };
        // Yerellerin üstünde işlenen değerler olabilir (ör. switch içinde değil; deyim düzeyinde olmalı)
        int extra = Depth() - (locals.empty() ? 1 : locals.back().Slot + 1);
        if (extra > 0)
        {
            pending = extra;
            flush();
        }
        for (size_t i = locals.size(); i > localCount; --i)
        {
            if (locals[i - 1].Captured)
            {
                flush();
                Emit(OpCode::CloseUpval);
            }
            else
                ++pending;
        }
        flush();
        SetDepth(savedDepth);
    }

    int CQSCompiler::DeclareLocal(const std::string& name, int line)
    {
        auto& locals = m_Fn->Locals;
        for (size_t i = locals.size(); i > 0; --i)
        {
            const Local& l = locals[i - 1];
            if (l.ScopeDepth < m_Fn->ScopeDepth)
                break;
            if (!name.empty() && name[0] != '$' && l.Name == name)
            {
                Error(line, "Variable '" + name + "' is already declared in this scope.");
                break;
            }
        }
        if ((int)locals.size() >= kMaxLocals)
        {
            Error(line, "Too many local variables in function.");
            return 0;
        }
        Local l;
        l.Name = name;
        l.ScopeDepth = m_Fn->ScopeDepth;
        l.Slot = Depth() - 1;
        locals.push_back(l);
        return l.Slot;
    }

    int CQSCompiler::AddHiddenLocal(const std::string& name)
    {
        return DeclareLocal(name, m_Line);
    }

    int CQSCompiler::ResolveLocal(FunctionState* fs, const std::string& name)
    {
        for (size_t i = fs->Locals.size(); i > 0; --i)
        {
            if (fs->Locals[i - 1].Name == name)
                return (int)(i - 1);
        }
        return -1;
    }

    int CQSCompiler::AddUpvalue(FunctionState* fs, uint16_t index, bool isLocal, const std::string& name)
    {
        for (size_t i = 0; i < fs->Upvalues.size(); ++i)
            if (fs->Upvalues[i].Index == index && fs->Upvalues[i].IsLocal == isLocal)
                return (int)i;
        if (fs->Upvalues.size() >= 65535)
        {
            Error(m_Line, "Too many captured variables in closure.");
            return 0;
        }
        UpvalueInfo up;
        up.Index = index;
        up.IsLocal = isLocal;
        up.Name = name;
        fs->Upvalues.push_back(up);
        return (int)fs->Upvalues.size() - 1;
    }

    int CQSCompiler::ResolveUpvalue(FunctionState* fs, const std::string& name)
    {
        if (!fs->Enclosing)
            return -1;
        int local = ResolveLocal(fs->Enclosing, name);
        if (local >= 0)
        {
            fs->Enclosing->Locals[(size_t)local].Captured = true;
            return AddUpvalue(fs, (uint16_t)fs->Enclosing->Locals[(size_t)local].Slot, true, name);
        }
        int up = ResolveUpvalue(fs->Enclosing, name);
        if (up >= 0)
            return AddUpvalue(fs, (uint16_t)up, false, name);
        return -1;
    }

    bool CQSCompiler::EmitThis(int line)
    {
        if (m_Fn->IsMethod && !m_Fn->IsStatic)
        {
            Emit(OpCode::GetLocal, 0, 0);
            return true;
        }
        int up = ResolveUpvalue(m_Fn, "this");
        if (up >= 0)
        {
            Emit(OpCode::GetUpval, 0, up);
            return true;
        }
        if (m_Fn->IsStatic)
            Error(line, "'this' cannot be used in a static method.");
        Emit(OpCode::PushNull);
        return false;
    }

    bool CQSCompiler::IsNamespaceName(const std::string& name) const
    {
        return m_Namespaces.count(name) != 0;
    }

    // ════════════════════════════════════════
    //  Variable resolution
    // ════════════════════════════════════════
    CQSCompiler::VarRef CQSCompiler::ResolveVariable(const std::string& name, int line)
    {
        VarRef ref;
        ref.Name = name;
        int local = ResolveLocal(m_Fn, name);
        if (local >= 0)
        {
            ref.Kind = VarKind::Local;
            ref.Index = m_Fn->Locals[(size_t)local].Slot;
            return ref;
        }
        int up = ResolveUpvalue(m_Fn, name);
        if (up >= 0)
        {
            ref.Kind = VarKind::Upvalue;
            ref.Index = up;
            return ref;
        }
        ClassState* cs = m_Fn->Class;
        if (cs)
        {
            // Örnek bağlamı var mı? (metot ya da metot içindeki lambda)
            bool hasThis = (m_Fn->IsMethod && !m_Fn->IsStatic);
            if (!hasThis)
            {
                for (FunctionState* f = m_Fn->Enclosing; f; f = f->Enclosing)
                {
                    if (f->IsMethod) { hasThis = !f->IsStatic; break; }
                }
            }
            if (hasThis)
            {
                auto it = std::find(cs->Layout.begin(), cs->Layout.end(), name);
                if (it != cs->Layout.end())
                {
                    if (cs->LayoutKnown)
                    {
                        ref.Kind = VarKind::ThisField;
                        ref.Index = (int)(it - cs->Layout.begin());
                    }
                    else
                        ref.Kind = VarKind::ThisProp;
                    return ref;
                }
            }
            if (cs->StaticNames.count(name))
            {
                ref.Kind = VarKind::StaticProp;
                return ref;
            }
            if (hasThis && cs->BaseUnknown)
            {
                // Taban sınıf derleme anında bilinmiyor: önce this özelliği, sonra global
                int slot = m_VM->FindGlobalSlot(name);
                bool knownGlobal = (slot >= 0 && m_VM->IsGlobalDefined(slot)) || m_UnitClasses.count(name);
                if (!knownGlobal)
                {
                    ref.Kind = VarKind::Dynamic;
                    ref.Index = GlobalSlot(name);
                    return ref;
                }
            }
        }
        (void)line;
        ref.Kind = VarKind::Global;
        ref.Index = GlobalSlot(name);
        return ref;
    }

    void CQSCompiler::EmitGet(const VarRef& ref)
    {
        switch (ref.Kind)
        {
            case VarKind::Local: Emit(OpCode::GetLocal, 0, ref.Index); break;
            case VarKind::Upvalue: Emit(OpCode::GetUpval, 0, ref.Index); break;
            case VarKind::ThisField:
                if (m_Fn->IsMethod && !m_Fn->IsStatic)
                    Emit(OpCode::GetThisField, 0, ref.Index);
                else
                {
                    EmitThis(m_Line);
                    Emit(OpCode::GetField, 0, ref.Index);
                }
                break;
            case VarKind::ThisProp:
                EmitThis(m_Line);
                Emit(OpCode::GetProp, 0, NameConstant(ref.Name), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                break;
            case VarKind::StaticProp:
                Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(m_Fn->Class->Name));
                Emit(OpCode::GetProp, 0, NameConstant(ref.Name), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                break;
            case VarKind::Global: Emit(OpCode::GetGlobal, 0, 0, ref.Index); break;
            case VarKind::Dynamic:
                EmitThis(m_Line);
                Emit(OpCode::GetDynamic, 0, NameConstant(ref.Name), ref.Index);   // [this] -> [v]
                break;
        }
    }

    void CQSCompiler::EmitSet(const VarRef& ref)
    {
        switch (ref.Kind)
        {
            case VarKind::Local: Emit(OpCode::SetLocal, 0, ref.Index); break;
            case VarKind::Upvalue: Emit(OpCode::SetUpval, 0, ref.Index); break;
            case VarKind::ThisField:
                if (m_Fn->IsMethod && !m_Fn->IsStatic)
                    Emit(OpCode::SetThisField, 0, ref.Index);
                else
                {
                    EmitThis(m_Line);
                    Emit(OpCode::Swap);
                    Emit(OpCode::SetField, 0, ref.Index);
                }
                break;
            case VarKind::ThisProp:
            case VarKind::Dynamic:
                EmitThis(m_Line);
                Emit(OpCode::Swap);
                Emit(OpCode::SetProp, 0, NameConstant(ref.Name), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                break;
            case VarKind::StaticProp:
                Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(m_Fn->Class->Name));
                Emit(OpCode::Swap);
                Emit(OpCode::SetProp, 0, NameConstant(ref.Name), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                break;
            case VarKind::Global: Emit(OpCode::SetGlobal, 0, 0, ref.Index); break;
        }
    }

    bool CQSCompiler::ResolveNamespacePath(ASTNode* node, std::string& globalName)
    {
        // nativebridge.GetPosition / Game.Utils.Clamp -> global ad
        if (node->Type != NodeType::MemberAccessExpr)
            return false;
        std::vector<std::string> parts;
        ASTNode* n = node;
        while (n->Type == NodeType::MemberAccessExpr)
        {
            auto* m = static_cast<MemberAccessNode*>(n);
            if (m->NullConditional) return false;
            parts.push_back(m->Member);
            n = m->Object.get();
        }
        if (n->Type != NodeType::IdentifierExpr)
            return false;
        const std::string& root = static_cast<IdentifierNode*>(n)->Name;
        if (!IsNamespaceName(root) || ResolveLocal(m_Fn, root) >= 0)
            return false;
        std::reverse(parts.begin(), parts.end());
        // En uzun namespace önekini atla: kalan ilk parça global ad olmalı (tam olarak bir parça kalmalı)
        std::string prefix = root;
        size_t i = 0;
        while (i + 1 < parts.size() && IsNamespaceName(prefix + "." + parts[i]))
        {
            prefix += "." + parts[i];
            ++i;
        }
        if (i + 1 != parts.size())
            return false;
        globalName = parts[i];
        return true;
    }

    // ════════════════════════════════════════
    //  Statements
    // ════════════════════════════════════════
    void CQSCompiler::CompileStatement(const ASTNodePtr& node)
    {
        if (!node)
            return;
        if (node->Line > 0)
            m_Line = node->Line;
        const bool global = (m_Fn->Enclosing == nullptr && m_Fn->ScopeDepth == 0 && m_Fn->Fn->Name == "<module>");

        switch (node->Type)
        {
            case NodeType::UsingStmt: break;
            case NodeType::NamespaceStmt:
            {
                auto* ns = static_cast<NamespaceNode*>(node.get());
                // Game.Utils -> "Game" ve "Game.Utils"
                std::string acc;
                size_t start = 0;
                for (size_t i = 0; i <= ns->Name.size(); ++i)
                {
                    if (i == ns->Name.size() || ns->Name[i] == '.')
                    {
                        acc += (acc.empty() ? "" : ".") + ns->Name.substr(start, i - start);
                        m_Namespaces.insert(acc);
                        start = i + 1;
                    }
                }
                for (auto& s : ns->Body)
                    CompileStatement(s);
                break;
            }
            case NodeType::VarDecl: CompileVarDecl(static_cast<VarDeclNode*>(node.get()), global); break;
            case NodeType::FuncDecl: CompileFuncDecl(static_cast<FuncDeclNode*>(node.get()), global); break;
            case NodeType::ClassDecl:
            case NodeType::ScriptDecl:
            case NodeType::StructDecl:
                if (!global)
                {
                    Error(node->Line, "Classes can only be declared at the top level.");
                    break;
                }
                CompileClass(static_cast<ClassDeclNode*>(node.get()));
                break;
            case NodeType::EnumDecl:
                if (!global)
                {
                    Error(node->Line, "Enums can only be declared at the top level.");
                    break;
                }
                CompileEnum(static_cast<EnumDeclNode*>(node.get()));
                break;
            case NodeType::InterfaceDecl: break;
            case NodeType::BlockStmt:
            {
                auto* b = static_cast<BlockNode*>(node.get());
                CompileBlock(b, b->Column != -1);
                break;
            }
            case NodeType::ExpressionStmt:
            {
                auto* e = static_cast<ExprStmtNode*>(node.get());
                // Deyim düzeyinde x++ / x-- önek gibi derlenir (eski değer gerekmez)
                if (e->Expression && e->Expression->Type == NodeType::IncDecExpr)
                    static_cast<IncDecNode*>(e->Expression.get())->Prefix = true;
                CompileExpr(e->Expression);
                Emit(OpCode::Pop);
                break;
            }
            case NodeType::IfStmt: CompileIf(static_cast<IfNode*>(node.get())); break;
            case NodeType::WhileStmt: CompileWhile(static_cast<WhileNode*>(node.get())); break;
            case NodeType::DoWhileStmt: CompileDoWhile(static_cast<WhileNode*>(node.get())); break;
            case NodeType::ForStmt: CompileFor(static_cast<ForNode*>(node.get())); break;
            case NodeType::ForeachStmt: CompileForeach(static_cast<ForeachNode*>(node.get())); break;
            case NodeType::SwitchStmt: CompileSwitch(static_cast<SwitchNode*>(node.get())); break;
            case NodeType::ReturnStmt: CompileReturn(static_cast<ReturnNode*>(node.get())); break;
            case NodeType::BreakStmt: CompileBreakContinue(true, node->Line); break;
            case NodeType::ContinueStmt: CompileBreakContinue(false, node->Line); break;
            case NodeType::TryCatchStmt: CompileTry(static_cast<TryCatchNode*>(node.get())); break;
            case NodeType::ThrowStmt: CompileThrow(static_cast<ThrowNode*>(node.get())); break;
            case NodeType::YieldStmt:
                Error(node->Line, "'yield return' is only supported inside coroutines started with StartCoroutine (not available yet).");
                break;
            default:
                // Çıplak ifade (REPL)
                CompileExpr(node);
                Emit(OpCode::Pop);
                break;
        }
    }

    Value CQSCompiler::DefaultValueForType(const std::string& typeName, bool& isList)
    {
        isList = false;
        std::string t = typeName;
        if (!t.empty() && t.back() == '?')
            return Value::MakeNull();
        if (t == "int" || t == "long" || t == "short" || t == "byte" || t == "uint") return Value::MakeInt(0);
        if (t == "float" || t == "double") return Value::MakeFloat(0.0);
        if (t == "bool") return Value::MakeBool(false);
        if (t.size() > 2 && t.compare(t.size() - 2, 2, "[]") == 0) { isList = true; return Value(); }
        if (t.rfind("List<", 0) == 0) { isList = true; return Value(); }
        return Value::MakeNull();
    }

    void CQSCompiler::CompileVarDecl(VarDeclNode* node, bool global)
    {
        m_Line = node->Line;
        if (node->Initializer)
            CompileExpr(node->Initializer);
        else
        {
            bool isList = false;
            Value def = DefaultValueForType(node->TypeName, isList);
            if (node->TypeName == "string")
                Emit(OpCode::PushConst, 0, StringConstant(""));
            else if (isList)
                Emit(OpCode::NewList, 0, 0);
            else
                EmitConstant(def);
        }
        if (global)
            Emit(OpCode::DefineGlobal, 0, 0, GlobalSlot(node->Name));
        else
            DeclareLocal(node->Name, node->Line);
    }

    void CQSCompiler::CompileFunctionBody(FunctionState& state, const std::vector<ASTNodePtr>& params, const ASTNodePtr& body, bool expressionBody)
    {
        CQSFunctionObject* fn = state.Fn;
        fn->Arity = (int)params.size();
        fn->RequiredArgs = 0;
        if (params.size() > 250)
            Error(m_Line, "Too many parameters (max 250).");
        BeginScope();
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* param = static_cast<ParamNode*>(params[i].get());
            // Parametreler çağıran tarafından yığına konmuş kabul edilir
            ++state.Depth;
            if (state.Depth > fn->MaxStack) fn->MaxStack = state.Depth;
            DeclareLocal(param->Name, param->Line);
            if (!param->DefaultValue)
                fn->RequiredArgs = (int)i + 1;
        }
        // Varsayılan değerler: argüman verilmediyse hesapla
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* param = static_cast<ParamNode*>(params[i].get());
            if (!param->DefaultValue)
                continue;
            int skip = Emit(OpCode::JumpIfArg, 0, (int)i, -1);
            CompileExpr(param->DefaultValue);
            Emit(OpCode::SetLocal, 0, (int)i + 1);
            Emit(OpCode::Pop);
            PatchJump(skip);
        }
        if (expressionBody)
        {
            CompileExpr(body);
            Emit(OpCode::Return);
        }
        else if (body)
        {
            if (body->Type == NodeType::BlockStmt)
            {
                for (auto& s : static_cast<BlockNode*>(body.get())->Statements)
                    CompileStatement(s);
            }
            else
                CompileStatement(body);
            Emit(OpCode::ReturnNull);
        }
        else
            Emit(OpCode::ReturnNull);
        // Kapsamı kapatma kodu gereksiz (Return yerelleri temizler); yalnızca durumu düzelt
        --state.ScopeDepth;
    }

    void CQSCompiler::EmitClosure(FunctionState& state)
    {
        CQSFunctionObject* fn = state.Fn;
        int k = AddConstant(Value::MakeObject(fn));
        Emit(OpCode::Closure, 0, k);
    }

    void CQSCompiler::CompileFuncDecl(FuncDeclNode* node, bool global)
    {
        m_Line = node->Line;
        int localSlot = -1;
        if (!global)
        {
            // Özyineleme için önce yereli bildir
            Emit(OpCode::PushNull);
            localSlot = DeclareLocal(node->Name, node->Line);
        }
        FunctionState fs;
        ClassState* savedClass = m_Class;
        m_Class = m_Fn->Class;
        BeginFunction(fs, node->Name, false);
        m_Class = savedClass;
        CompileFunctionBody(fs, node->Params, node->Body, false);
        EndFunction();
        int k = AddConstant(Value::MakeObject(fs.Fn));
        Emit(OpCode::Closure, 0, k);
        if (global)
            Emit(OpCode::DefineGlobal, 0, 0, GlobalSlot(node->Name));
        else
        {
            Emit(OpCode::SetLocal, 0, localSlot);
            Emit(OpCode::Pop);
        }
    }

    void CQSCompiler::CompileClass(ClassDeclNode* node)
    {
        m_Line = node->Line;
        if (m_UnitClasses.count(node->Name))
            Error(node->Line, "Class '" + node->Name + "' is already declared in this file.");

        auto* templ = new CQSClassObject(node->Name);
        templ->BaseName = node->BaseClass.substr(0, node->BaseClass.find('<'));
        templ->IsScript = (node->Type == NodeType::ScriptDecl) || node->BaseClass == "ConquerorScript" || node->BaseClass == "MonoBehaviour";
        templ->IsStruct = node->IsStruct;

        ClassState cs;
        cs.Name = node->Name;
        cs.Template = templ;
        cs.Enclosing = m_Class;

        // Taban sınıf yerleşimi
        std::vector<std::string> baseLayout;
        bool hasBase = !IsNativeBaseClass(templ->BaseName);
        if (hasBase)
        {
            if (templ->BaseName == node->Name)
                Error(node->Line, "A class cannot inherit from itself.");
            auto unit = m_UnitClasses.find(templ->BaseName);
            CQSClassObject* baseCls = unit != m_UnitClasses.end() ? unit->second : m_VM->FindClass(templ->BaseName);
            if (baseCls)
            {
                baseLayout = baseCls->FieldNames;
                for (auto& [mname, mv] : baseCls->Methods) cs.Methods.insert(mname);
                // Birim içi şablonların metot listesi şablonda tutulmaz: ayrıca kaydedilir
                auto um = m_UnitMethods.find(templ->BaseName);
                if (um != m_UnitMethods.end())
                    cs.Methods.insert(um->second.begin(), um->second.end());
            }
            else
            {
                cs.BaseUnknown = true;
                cs.LayoutKnown = false;
            }
        }

        // Kendi alanları
        std::vector<VarDeclNode*> fields, statics;
        std::vector<FuncDeclNode*> methods;
        for (auto& m : node->Members)
        {
            if (!m) continue;
            if (m->Type == NodeType::VarDecl)
            {
                auto* v = static_cast<VarDeclNode*>(m.get());
                if (v->IsStatic) { statics.push_back(v); cs.StaticNames.insert(v->Name); }
                else fields.push_back(v);
            }
            else if (m->Type == NodeType::FuncDecl)
            {
                auto* f = static_cast<FuncDeclNode*>(m.get());
                methods.push_back(f);
                if (f->IsStatic) cs.StaticNames.insert(f->Name);
                else cs.Methods.insert(f->Name);
            }
            else if (m->Type == NodeType::BlockStmt)
            {
                // var a, b; -> çoklu alan
                for (auto& s : static_cast<BlockNode*>(m.get())->Statements)
                {
                    if (s && s->Type == NodeType::VarDecl)
                    {
                        auto* v = static_cast<VarDeclNode*>(s.get());
                        if (v->IsStatic) { statics.push_back(v); cs.StaticNames.insert(v->Name); }
                        else fields.push_back(v);
                    }
                }
            }
        }
        std::unordered_set<std::string> seen(baseLayout.begin(), baseLayout.end());
        for (VarDeclNode* f : fields)
        {
            if (!seen.insert(f->Name).second)
            {
                Error(f->Line, "Field '" + f->Name + "' is already declared in class '" + node->Name + "' or its base class.");
                continue;
            }
            CQSFieldInfo info;
            info.Name = f->Name;
            info.TypeName = f->TypeName;
            bool serialize = std::find(f->Attributes.begin(), f->Attributes.end(), "SerializeField") != f->Attributes.end();
            bool hide = std::find(f->Attributes.begin(), f->Attributes.end(), "HideInInspector") != f->Attributes.end();
            info.IsPublic = !hide && (f->IsPublic || serialize || !f->IsPrivate);
            info.Attributes = f->Attributes;
            Value lit;
            if (f->Initializer && TryFoldConstant(f->Initializer, lit))
            {
                info.DefaultValue = lit;
                info.HasLiteralDefault = true;
            }
            else if (f->Initializer && f->Initializer->Type == NodeType::LiteralExpr && static_cast<LiteralNode*>(f->Initializer.get())->IsString)
            {
                info.DefaultValue = Value::MakeString(static_cast<LiteralNode*>(f->Initializer.get())->StringValue);
                info.HasLiteralDefault = true;
            }
            else if (!f->Initializer)
            {
                bool isList = false;
                info.DefaultValue = DefaultValueForType(f->TypeName, isList);
                info.HasLiteralDefault = !isList;
                if (f->TypeName == "string")
                    info.DefaultValue = Value::MakeString("");
            }
            templ->OwnFields.push_back(info);
        }
        cs.Layout = baseLayout;
        for (auto& f : templ->OwnFields)
            cs.Layout.push_back(f.Name);
        templ->FieldNames = cs.LayoutKnown ? cs.Layout : std::vector<std::string>();
        if (!cs.LayoutKnown)
            for (auto& f : templ->OwnFields) templ->FieldNames.push_back(f.Name);
        templ->ExpectedBaseFields = (hasBase && !cs.BaseUnknown) ? (int)baseLayout.size() : (hasBase ? -1 : 0);
        // Metot adlarını kaydet (alt sınıflar derleme anında bilsin)
        m_UnitMethods[node->Name] = cs.Methods;
        m_UnitClasses[node->Name] = templ;

        // ── Kod ──
        Emit(OpCode::Class, 0, AddConstant(Value::MakeObject(templ)));
        if (hasBase)
        {
            // Namespace.Base
            std::string bname = templ->BaseName;
            size_t dot = bname.rfind('.');
            if (dot != std::string::npos) bname = bname.substr(dot + 1);
            Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(bname));
            Emit(OpCode::Inherit, 0);
        }

        ClassState* savedClass = m_Class;
        m_Class = &cs;

        // Statik alanlar
        for (VarDeclNode* s : statics)
        {
            m_Line = s->Line;
            if (s->Initializer)
            {
                // Statik başlatıcı sınıf bağlamında (diğer statiklere erişebilir) ama örneksiz derlenir
                ClassState* fsClass = m_Fn->Class;
                m_Fn->Class = &cs;
                CompileExpr(s->Initializer);
                m_Fn->Class = fsClass;
            }
            else
            {
                bool isList = false;
                Value def = DefaultValueForType(s->TypeName, isList);
                if (s->TypeName == "string") Emit(OpCode::PushConst, 0, StringConstant(""));
                else if (isList) Emit(OpCode::NewList, 0, 0);
                else EmitConstant(def);
            }
            Emit(OpCode::StaticField, 0, NameConstant(s->Name));
        }

        // Alan başlatıcı (yalnızca bu sınıfın alanları)
        bool needsInit = false;
        for (VarDeclNode* f : fields)
        {
            if (f->Initializer) { needsInit = true; break; }
            bool isList = false;
            DefaultValueForType(f->TypeName, isList);
            if (isList || f->TypeName == "string") { needsInit = true; break; }
        }
        if (needsInit)
        {
            FunctionState fs;
            BeginFunction(fs, node->Name + ".<fields>", true);
            fs.Fn->Arity = 0;
            BeginScope();
            for (VarDeclNode* f : fields)
            {
                m_Line = f->Line;
                bool isList = false;
                Value def = DefaultValueForType(f->TypeName, isList);
                if (f->Initializer)
                    CompileExpr(f->Initializer);
                else if (isList)
                    Emit(OpCode::NewList, 0, 0);
                else if (f->TypeName == "string")
                    Emit(OpCode::PushConst, 0, StringConstant(""));
                else
                    continue; // varsayılan değer sınıf oluşturulurken atanır
                VarRef ref;
                ref.Name = f->Name;
                auto it = std::find(cs.Layout.begin(), cs.Layout.end(), f->Name);
                if (cs.LayoutKnown && it != cs.Layout.end())
                {
                    ref.Kind = VarKind::ThisField;
                    ref.Index = (int)(it - cs.Layout.begin());
                }
                else
                    ref.Kind = VarKind::ThisProp;
                EmitSet(ref);
                Emit(OpCode::Pop);
            }
            Emit(OpCode::ReturnNull);
            --fs.ScopeDepth;
            EndFunction();
            Emit(OpCode::Closure, 0, AddConstant(Value::MakeObject(fs.Fn)));
            Emit(OpCode::Method, 3, NameConstant("<fields>"));
        }

        // Metotlar
        std::unordered_set<std::string> methodNames;
        for (FuncDeclNode* f : methods)
        {
            m_Line = f->Line;
            bool isCtor = (f->Name == "init" || f->Name == "constructor" || f->Name == node->Name) && !f->IsStatic;
            std::string mname = isCtor ? "init" : f->Name;
            if (!methodNames.insert((f->IsStatic ? "static:" : "") + mname).second)
            {
                Error(f->Line, "Method '" + mname + "' is already declared in class '" + node->Name + "' (overloads are not supported).");
                continue;
            }
            FunctionState fs;
            BeginFunction(fs, node->Name + "." + mname, !f->IsStatic);
            fs.IsStatic = f->IsStatic;
            fs.IsInitializer = isCtor;
            fs.Fn->IsInitializer = isCtor;
            CompileFunctionBody(fs, f->Params, f->Body, false);
            EndFunction();
            Emit(OpCode::Closure, 0, AddConstant(Value::MakeObject(fs.Fn)));
            Emit(OpCode::Method, f->IsStatic ? 1 : (isCtor ? 2 : 0), NameConstant(mname));
        }

        m_Class = savedClass;
        Emit(OpCode::DefineGlobal, 0, 0, GlobalSlot(node->Name));
    }

    void CQSCompiler::CompileEnum(EnumDeclNode* node)
    {
        m_Line = node->Line;
        int64_t next = 0;
        std::unordered_set<std::string> seen;
        for (size_t i = 0; i < node->Values.size(); ++i)
        {
            if (!seen.insert(node->Values[i]).second)
                Error(node->Line, "Duplicate enum member '" + node->Values[i] + "'.");
            Value v;
            if (node->Initializers[i])
            {
                if (TryFoldConstant(node->Initializers[i], v) && v.IsNumber())
                    next = v.ToInt();
                else
                    Error(node->Line, "Enum member values must be integer constants.");
            }
            Emit(OpCode::PushConst, 0, StringConstant(node->Values[i]));
            EmitConstant(Value::MakeInt(next));
            ++next;
        }
        Emit(OpCode::NewMap, 0, (int)node->Values.size());
        Emit(OpCode::DefineGlobal, 0, 0, GlobalSlot(node->Name));
    }

    void CQSCompiler::CompileBlock(BlockNode* node, bool newScope)
    {
        if (newScope) BeginScope();
        for (auto& s : node->Statements)
            CompileStatement(s);
        if (newScope) EndScope();
    }

    void CQSCompiler::CompileIf(IfNode* node)
    {
        CompileExpr(node->Condition);
        int elseJump = EmitJump(OpCode::JumpIfFalse);
        int depth = Depth();
        BeginScope();
        CompileStatement(node->ThenBranch);
        EndScope();
        if (node->ElseBranch)
        {
            int endJump = EmitJump(OpCode::Jump);
            PatchJump(elseJump);
            SetDepth(depth);
            BeginScope();
            CompileStatement(node->ElseBranch);
            EndScope();
            PatchJump(endJump);
        }
        else
            PatchJump(elseJump);
        SetDepth(depth);
    }

    void CQSCompiler::CompileWhile(WhileNode* node)
    {
        int loopStart = CurrentPos();
        LoopState loop;
        loop.ContinueTarget = loopStart;
        loop.LocalCount = m_Fn->Locals.size();
        loop.Depth = Depth();
        loop.TryCount = m_Fn->Trys.size();
        m_Fn->Loops.push_back(loop);

        CompileExpr(node->Condition);
        int exitJump = EmitJump(OpCode::JumpIfFalse);
        BeginScope();
        CompileStatement(node->Body);
        EndScope();
        EmitLoop(loopStart);
        PatchJump(exitJump);

        LoopState done = m_Fn->Loops.back();
        m_Fn->Loops.pop_back();
        for (int j : done.BreakJumps) PatchJump(j);
        SetDepth(done.Depth);
    }

    void CQSCompiler::CompileDoWhile(WhileNode* node)
    {
        int loopStart = CurrentPos();
        LoopState loop;
        loop.ContinueTarget = -1; // koşula ileri atlar
        loop.LocalCount = m_Fn->Locals.size();
        loop.Depth = Depth();
        loop.TryCount = m_Fn->Trys.size();
        m_Fn->Loops.push_back(loop);

        BeginScope();
        CompileStatement(node->Body);
        EndScope();
        for (int j : m_Fn->Loops.back().ContinueJumps) PatchJump(j);
        CompileExpr(node->Condition);
        int exitJump = EmitJump(OpCode::JumpIfFalse);
        EmitLoop(loopStart);
        PatchJump(exitJump);

        LoopState done = m_Fn->Loops.back();
        m_Fn->Loops.pop_back();
        for (int j : done.BreakJumps) PatchJump(j);
        SetDepth(done.Depth);
    }

    void CQSCompiler::CompileFor(ForNode* node)
    {
        BeginScope();
        if (node->Initializer)
            CompileStatement(node->Initializer);

        int loopStart = CurrentPos();
        LoopState loop;
        loop.ContinueTarget = -1;
        loop.LocalCount = m_Fn->Locals.size();
        loop.Depth = Depth();
        loop.TryCount = m_Fn->Trys.size();
        m_Fn->Loops.push_back(loop);

        int exitJump = -1;
        if (node->Condition)
        {
            CompileExpr(node->Condition);
            exitJump = EmitJump(OpCode::JumpIfFalse);
        }
        BeginScope();
        CompileStatement(node->Body);
        EndScope();

        for (int j : m_Fn->Loops.back().ContinueJumps) PatchJump(j);
        if (node->Increment)
        {
            if (node->Increment->Type == NodeType::BlockStmt)
                CompileStatement(node->Increment);
            else
            {
                if (node->Increment->Type == NodeType::IncDecExpr)
                    static_cast<IncDecNode*>(node->Increment.get())->Prefix = true;
                CompileExpr(node->Increment);
                Emit(OpCode::Pop);
            }
        }
        EmitLoop(loopStart);
        if (exitJump >= 0)
            PatchJump(exitJump);

        LoopState done = m_Fn->Loops.back();
        m_Fn->Loops.pop_back();
        for (int j : done.BreakJumps) PatchJump(j);
        SetDepth(done.Depth);
        EndScope();
    }

    void CQSCompiler::CompileForeach(ForeachNode* node)
    {
        int vars = node->ValueVarName.empty() ? 1 : 2;
        BeginScope();
        CompileExpr(node->Iterable);
        Emit(OpCode::IterPrep, vars);
        int seqSlot = AddHiddenLocal("$seq");
        Emit(OpCode::PushInt, 0, 0, 0);
        AddHiddenLocal("$idx");

        int loopStart = CurrentPos();
        LoopState loop;
        loop.ContinueTarget = loopStart;
        loop.LocalCount = m_Fn->Locals.size();
        loop.Depth = Depth();
        loop.TryCount = m_Fn->Trys.size();
        m_Fn->Loops.push_back(loop);

        int exitJump = Emit(OpCode::IterNext, vars, seqSlot, -1);
        BeginScope();
        // IterNext değer(ler)i itti: [k] ya da [k v]
        if (vars == 1)
            DeclareLocal(node->VarName, node->Line);
        else
        {
            // İki yerel: önce k sonra v (k derinlik-2'de)
            Local k;
            k.Name = node->VarName;
            k.ScopeDepth = m_Fn->ScopeDepth;
            k.Slot = Depth() - 2;
            m_Fn->Locals.push_back(k);
            DeclareLocal(node->ValueVarName, node->Line);
        }
        CompileStatement(node->Body);
        EndScope();
        EmitLoop(loopStart);
        PatchJump(exitJump);
        SetDepth(loop.Depth);

        LoopState done = m_Fn->Loops.back();
        m_Fn->Loops.pop_back();
        for (int j : done.BreakJumps) PatchJump(j);
        SetDepth(done.Depth);
        EndScope();
    }

    void CQSCompiler::CompileSwitch(SwitchNode* node)
    {
        BeginScope();
        CompileExpr(node->Expression);
        int subject = AddHiddenLocal("$switch");

        LoopState sw;
        sw.IsSwitch = true;
        sw.LocalCount = m_Fn->Locals.size();
        sw.Depth = Depth();
        sw.TryCount = m_Fn->Trys.size();
        m_Fn->Loops.push_back(sw);

        // Test zinciri: her case değeri için eşitse gövdeye atla
        std::vector<std::vector<int>> hits(node->Cases.size());
        int defaultIndex = -1;
        for (size_t i = 0; i < node->Cases.size(); ++i)
        {
            auto* c = static_cast<CaseNode*>(node->Cases[i].get());
            if (c->IsDefault)
                defaultIndex = (int)i;
            for (auto& v : c->Values)
            {
                m_Line = v->Line;
                Emit(OpCode::GetLocal, 0, subject);
                CompileExpr(v);
                Emit(OpCode::Eq);
                hits[i].push_back(EmitJump(OpCode::JumpIfTrue));
            }
        }
        int noMatch = EmitJump(OpCode::Jump);

        // Gövdeler (C# gibi alt case'e düşme yok: her gövde sona atlar)
        std::vector<int> endJumps;
        for (size_t i = 0; i < node->Cases.size(); ++i)
        {
            auto* c = static_cast<CaseNode*>(node->Cases[i].get());
            int bodyStart = CurrentPos();
            for (int j : hits[i])
                PatchJumpTo(j, bodyStart);
            if ((int)i == defaultIndex)
                PatchJumpTo(noMatch, bodyStart);
            SetDepth(sw.Depth);
            BeginScope();
            for (auto& s : c->Body)
                CompileStatement(s);
            EndScope();
            endJumps.push_back(EmitJump(OpCode::Jump));
        }
        if (defaultIndex < 0)
            PatchJump(noMatch);
        for (int j : endJumps)
            PatchJump(j);

        LoopState done = m_Fn->Loops.back();
        m_Fn->Loops.pop_back();
        for (int j : done.BreakJumps)
            PatchJump(j);
        SetDepth(done.Depth);
        EndScope();
    }

    void CQSCompiler::EmitFinallyBlocks(size_t downToTryCount)
    {
        auto saved = m_Fn->Trys;
        for (size_t i = saved.size(); i > downToTryCount; --i)
        {
            if (!saved[i - 1].FinallyBody)
                continue;
            m_Fn->Trys.resize(i - 1);
            BeginScope();
            CompileStatement(std::shared_ptr<ASTNode>(saved[i - 1].FinallyBody, [](ASTNode*) {}));
            EndScope();
        }
        m_Fn->Trys = saved;
    }

    void CQSCompiler::CompileReturn(ReturnNode* node)
    {
        m_Line = node->Line;
        if (node->Value)
        {
            if (m_Fn->IsInitializer)
                Error(node->Line, "Constructors cannot return a value.");
            CompileExpr(node->Value);
            if (!m_Fn->Trys.empty())
                EmitFinallyBlocks(0);
            Emit(OpCode::Return);
        }
        else
        {
            if (!m_Fn->Trys.empty())
                EmitFinallyBlocks(0);
            Emit(OpCode::ReturnNull);
        }
    }

    void CQSCompiler::CompileBreakContinue(bool isBreak, int line)
    {
        // continue: switch'leri atlayarak en yakın döngü
        int target = -1;
        for (int i = (int)m_Fn->Loops.size() - 1; i >= 0; --i)
        {
            if (isBreak || !m_Fn->Loops[(size_t)i].IsSwitch)
            {
                target = i;
                break;
            }
        }
        if (target < 0)
        {
            Error(line, isBreak ? "'break' outside of a loop or switch." : "'continue' outside of a loop.");
            return;
        }
        LoopState& loop = m_Fn->Loops[(size_t)target];
        EmitFinallyBlocks(loop.TryCount);
        EmitPopLocalsAbove(loop.LocalCount);
        int saved = Depth();
        if (isBreak)
            loop.BreakJumps.push_back(EmitJump(OpCode::Jump));
        else if (loop.ContinueTarget >= 0)
            EmitLoop(loop.ContinueTarget);
        else
            loop.ContinueJumps.push_back(EmitJump(OpCode::Jump));
        SetDepth(saved);
    }

    void CQSCompiler::CompileTry(TryCatchNode* node)
    {
        int tryDepth = Depth();
        if (node->FinallyBody)
        {
            TryState t;
            t.FinallyBody = node->FinallyBody.get();
            t.LocalCount = m_Fn->Locals.size();
            m_Fn->Trys.push_back(t);
        }
        int start = CurrentPos();
        CompileStatement(node->TryBody);
        int end = CurrentPos();
        int protectedEnd = end;

        if (node->CatchBody)
        {
            int skip = EmitJump(OpCode::Jump);
            int handler = CurrentPos();
            SetDepth(tryDepth + 1);
            m_Line = node->CatchBody->Line;
            // Tür filtresi: catch (MyError e)
            const std::string& type = node->CatchTypeName;
            if (!type.empty() && type != "Exception" && type != "System.Exception" && type != "Error" && type != "any")
            {
                Emit(OpCode::Dup);
                Emit(OpCode::IsType, 0, NameConstant(type));
                int match = EmitJump(OpCode::JumpIfTrue);
                Emit(OpCode::Throw);
                SetDepth(tryDepth + 1);
                PatchJump(match);
            }
            BeginScope();
            int exSlot = DeclareLocal(node->CatchVarName.empty() ? "$ex" : node->CatchVarName, node->Line);
            m_Fn->CatchSlots.push_back(exSlot);
            CompileStatement(node->CatchBody);
            m_Fn->CatchSlots.pop_back();
            EndScope();
            protectedEnd = CurrentPos();
            PatchJump(skip);
            CQSTryHandler h;
            h.Start = (uint32_t)start;
            h.End = (uint32_t)end;
            h.Handler = (uint32_t)handler;
            h.StackDepth = (uint32_t)tryDepth;
            m_Fn->Fn->Handlers.push_back(h);
            SetDepth(tryDepth);
        }

        if (node->FinallyBody)
        {
            m_Fn->Trys.pop_back();
            // Normal yol
            CompileStatement(node->FinallyBody);
            int skip = EmitJump(OpCode::Jump);
            // İstisna yolu: finally çalıştır, yeniden fırlat
            int handler = CurrentPos();
            SetDepth(tryDepth + 1);
            BeginScope();
            int exSlot = DeclareLocal("$finally_ex", node->Line);
            CompileStatement(node->FinallyBody);
            Emit(OpCode::GetLocal, 0, exSlot);
            Emit(OpCode::Throw);
            SetDepth(tryDepth + 1);
            EndScope();
            PatchJump(skip);
            SetDepth(tryDepth);
            CQSTryHandler h;
            h.Start = (uint32_t)start;
            h.End = (uint32_t)protectedEnd;
            h.Handler = (uint32_t)handler;
            h.StackDepth = (uint32_t)tryDepth;
            m_Fn->Fn->Handlers.push_back(h);
        }
    }

    void CQSCompiler::CompileThrow(ThrowNode* node)
    {
        m_Line = node->Line;
        if (node->Expression)
            CompileExpr(node->Expression);
        else if (!m_Fn->CatchSlots.empty())
            Emit(OpCode::GetLocal, 0, m_Fn->CatchSlots.back());   // catch içinde "throw;" -> yeniden fırlat
        else
        {
            Error(node->Line, "'throw;' without an expression is only allowed inside a catch block.");
            Emit(OpCode::PushNull);
        }
        Emit(OpCode::Throw);
    }

    // ════════════════════════════════════════
    //  Expressions
    // ════════════════════════════════════════
    bool CQSCompiler::TryFoldConstant(const ASTNodePtr& node, Value& out)
    {
        if (!node)
            return false;
        if (node->Type == NodeType::LiteralExpr)
        {
            auto* lit = static_cast<LiteralNode*>(node.get());
            if (lit->IsString) return false;
            out = lit->LiteralValue;
            return true;
        }
        if (node->Type == NodeType::UnaryExpr)
        {
            auto* u = static_cast<UnaryNode*>(node.get());
            Value v;
            if (!TryFoldConstant(u->Operand, v)) return false;
            if (u->Operator == TokenType::Minus && v.IsInt()) { out = Value::MakeInt(-v.AsInt()); return true; }
            if (u->Operator == TokenType::Minus && v.IsFloat()) { out = Value::MakeFloat(-v.AsFloat()); return true; }
            if (u->Operator == TokenType::Bang) { out = Value::MakeBool(!v.IsTruthy()); return true; }
            if (u->Operator == TokenType::Tilde && v.IsInt()) { out = Value::MakeInt(~v.AsInt()); return true; }
            return false;
        }
        if (node->Type == NodeType::BinaryExpr)
        {
            auto* b = static_cast<BinaryNode*>(node.get());
            Value l, r;
            if (!TryFoldConstant(b->Left, l) || !TryFoldConstant(b->Right, r)) return false;
            if (!l.IsNumber() || !r.IsNumber()) return false;
            bool ints = l.IsInt() && r.IsInt();
            switch (b->Operator)
            {
                case TokenType::Plus:  out = ints ? Value::MakeInt(l.AsInt() + r.AsInt()) : Value::MakeFloat(l.ToNumber() + r.ToNumber()); return true;
                case TokenType::Minus: out = ints ? Value::MakeInt(l.AsInt() - r.AsInt()) : Value::MakeFloat(l.ToNumber() - r.ToNumber()); return true;
                case TokenType::Star:  out = ints ? Value::MakeInt(l.AsInt() * r.AsInt()) : Value::MakeFloat(l.ToNumber() * r.ToNumber()); return true;
                case TokenType::Slash:
                    if (r.ToNumber() == 0.0) return false;
                    out = Value::MakeFloat(l.ToNumber() / r.ToNumber());
                    return true;
                case TokenType::LessLess:
                    if (!ints) return false;
                    out = Value::MakeInt(l.AsInt() << (r.AsInt() & 63));
                    return true;
                case TokenType::Pipe:
                    if (!ints) return false;
                    out = Value::MakeInt(l.AsInt() | r.AsInt());
                    return true;
                default: return false;
            }
        }
        return false;
    }

    OpCode CQSCompiler::BinaryOpFor(TokenType t, int line)
    {
        switch (t)
        {
            case TokenType::Plus: return OpCode::Add;
            case TokenType::Minus: return OpCode::Sub;
            case TokenType::Star: return OpCode::Mul;
            case TokenType::Slash: return OpCode::Div;
            case TokenType::Percent: return OpCode::Mod;
            case TokenType::Amp: return OpCode::BitAnd;
            case TokenType::Pipe: return OpCode::BitOr;
            case TokenType::Caret: return OpCode::BitXor;
            case TokenType::LessLess: return OpCode::Shl;
            case TokenType::GreaterGreater: return OpCode::Shr;
            case TokenType::EqualEqual: return OpCode::Eq;
            case TokenType::BangEqual: return OpCode::Ne;
            case TokenType::Less: return OpCode::Lt;
            case TokenType::LessEqual: return OpCode::Le;
            case TokenType::Greater: return OpCode::Gt;
            case TokenType::GreaterEqual: return OpCode::Ge;
            case TokenType::DotDot: return OpCode::MakeRange;
            default:
                Error(line, "Unsupported binary operator.");
                return OpCode::Add;
        }
    }

    void CQSCompiler::CompileExpr(const ASTNodePtr& node)
    {
        if (!node)
        {
            Emit(OpCode::PushNull);
            return;
        }
        int savedLine = m_Line;
        if (node->Line > 0)
            m_Line = node->Line;
        switch (node->Type)
        {
            case NodeType::LiteralExpr: CompileLiteral(static_cast<LiteralNode*>(node.get())); break;
            case NodeType::IdentifierExpr: CompileIdentifier(static_cast<IdentifierNode*>(node.get())); break;
            case NodeType::ThisExpr: EmitThis(node->Line); break;
            case NodeType::BaseExpr:
                Error(node->Line, "'base' can only be used to call a base class method: base.Method(...).");
                Emit(OpCode::PushNull);
                break;
            case NodeType::BinaryExpr:
            {
                Value folded;
                if (TryFoldConstant(node, folded))
                    EmitConstant(folded);
                else
                    CompileBinary(static_cast<BinaryNode*>(node.get()));
                break;
            }
            case NodeType::LogicalExpr: CompileLogical(static_cast<BinaryNode*>(node.get())); break;
            case NodeType::UnaryExpr: CompileUnary(static_cast<UnaryNode*>(node.get())); break;
            case NodeType::AssignExpr: CompileAssign(static_cast<AssignNode*>(node.get())); break;
            case NodeType::CompoundAssignExpr: CompileCompoundAssign(static_cast<CompoundAssignNode*>(node.get())); break;
            case NodeType::IncDecExpr: CompileIncDec(static_cast<IncDecNode*>(node.get())); break;
            case NodeType::CallExpr: CompileCall(static_cast<CallNode*>(node.get())); break;
            case NodeType::MemberAccessExpr: CompileMember(static_cast<MemberAccessNode*>(node.get())); break;
            case NodeType::IndexExpr: CompileIndex(static_cast<IndexNode*>(node.get())); break;
            case NodeType::NewExpr: CompileNew(static_cast<NewNode*>(node.get())); break;
            case NodeType::LambdaExpr: CompileLambda(static_cast<LambdaNode*>(node.get())); break;
            case NodeType::TernaryExpr: CompileTernary(static_cast<TernaryNode*>(node.get())); break;
            case NodeType::ListLiteralExpr: CompileList(static_cast<ListLiteralNode*>(node.get())); break;
            case NodeType::MapLiteralExpr: CompileMap(static_cast<MapLiteralNode*>(node.get())); break;
            case NodeType::InterpolatedStringExpr: CompileInterpolated(static_cast<InterpolatedStringNode*>(node.get())); break;
            case NodeType::IsExpr:
            case NodeType::CastExpr: CompileTypeTest(static_cast<TypeTestNode*>(node.get())); break;
            default:
                Error(node->Line, "This construct cannot be used as an expression.");
                Emit(OpCode::PushNull);
                break;
        }
        m_Line = savedLine;
    }

    void CQSCompiler::CompileLiteral(LiteralNode* node)
    {
        if (node->IsString)
            Emit(OpCode::PushConst, 0, StringConstant(node->StringValue));
        else
            EmitConstant(node->LiteralValue);
    }

    void CQSCompiler::CompileIdentifier(IdentifierNode* node)
    {
        if (IsNamespaceName(node->Name) && ResolveLocal(m_Fn, node->Name) < 0 && ResolveUpvalue(m_Fn, node->Name) < 0)
        {
            int slot = m_VM->FindGlobalSlot(node->Name);
            if (slot < 0 || !m_VM->IsGlobalDefined(slot))
            {
                Error(node->Line, "'" + node->Name + "' is a namespace; use " + node->Name + ".Member.");
                Emit(OpCode::PushNull);
                return;
            }
        }
        EmitGet(ResolveVariable(node->Name, node->Line));
    }

    void CQSCompiler::CompileBinary(BinaryNode* node)
    {
        CompileExpr(node->Left);
        CompileExpr(node->Right);
        Emit(BinaryOpFor(node->Operator, node->Line));
    }

    void CQSCompiler::CompileLogical(BinaryNode* node)
    {
        int depth = Depth();
        if (node->Operator == TokenType::QuestionQuestion)
        {
            CompileExpr(node->Left);
            int done = EmitJump(OpCode::JumpIfNotNullKeep);
            CompileExpr(node->Right);
            PatchJump(done);
            SetDepth(depth + 1);
            return;
        }
        bool isAnd = node->Operator == TokenType::AmpAmp;
        CompileExpr(node->Left);
        int shortJump = EmitJump(isAnd ? OpCode::JumpIfFalse : OpCode::JumpIfTrue);
        CompileExpr(node->Right);
        Emit(OpCode::ToBool);
        int endJump = EmitJump(OpCode::Jump);
        PatchJump(shortJump);
        SetDepth(depth);
        Emit(isAnd ? OpCode::PushFalse : OpCode::PushTrue);
        PatchJump(endJump);
        SetDepth(depth + 1);
    }

    void CQSCompiler::CompileUnary(UnaryNode* node)
    {
        CompileExpr(node->Operand);
        switch (node->Operator)
        {
            case TokenType::Minus: Emit(OpCode::Neg); break;
            case TokenType::Bang: Emit(OpCode::Not); break;
            case TokenType::Tilde: Emit(OpCode::BitNot); break;
            default: Error(node->Line, "Unsupported unary operator."); break;
        }
    }

    void CQSCompiler::CompileAssign(AssignNode* node)
    {
        ASTNode* target = node->Target.get();
        if (!target)
        {
            Emit(OpCode::PushNull);
            return;
        }
        switch (target->Type)
        {
            case NodeType::IdentifierExpr:
            {
                auto* id = static_cast<IdentifierNode*>(target);
                VarRef ref = ResolveVariable(id->Name, id->Line);
                CompileExpr(node->Value);
                EmitSet(ref);
                return;
            }
            case NodeType::MemberAccessExpr:
            {
                auto* m = static_cast<MemberAccessNode*>(target);
                std::string globalName;
                if (ResolveNamespacePath(m, globalName))
                {
                    CompileExpr(node->Value);
                    Emit(OpCode::SetGlobal, 0, 0, GlobalSlot(globalName));
                    return;
                }
                // this.x = v -> doğrudan alan
                if (m->Object->Type == NodeType::ThisExpr && m_Fn->Class)
                {
                    auto& layout = m_Fn->Class->Layout;
                    auto it = std::find(layout.begin(), layout.end(), m->Member);
                    if (m_Fn->Class->LayoutKnown && it != layout.end())
                    {
                        VarRef ref;
                        ref.Kind = VarKind::ThisField;
                        ref.Index = (int)(it - layout.begin());
                        ref.Name = m->Member;
                        CompileExpr(node->Value);
                        EmitSet(ref);
                        return;
                    }
                }
                CompileExpr(m->Object);
                CompileExpr(node->Value);
                Emit(OpCode::SetProp, 0, NameConstant(m->Member), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                return;
            }
            case NodeType::IndexExpr:
            {
                auto* ix = static_cast<IndexNode*>(target);
                CompileExpr(ix->Object);
                CompileExpr(ix->Index);
                CompileExpr(node->Value);
                Emit(OpCode::SetIndex);
                return;
            }
            default:
                Error(node->Line, "Invalid assignment target.");
                CompileExpr(node->Value);
                return;
        }
    }

    void CQSCompiler::CompileCompoundAssign(CompoundAssignNode* node)
    {
        ASTNode* target = node->Target.get();
        bool coalesce = node->Operator == TokenType::QuestionQuestion;
        auto emitOp = [&]() {
            Emit(BinaryOpFor(node->Operator, node->Line));
        };
        switch (target ? target->Type : NodeType::Program)
        {
            case NodeType::IdentifierExpr:
            {
                auto* id = static_cast<IdentifierNode*>(target);
                VarRef ref = ResolveVariable(id->Name, id->Line);
                EmitGet(ref);
                if (coalesce)
                {
                    int depth = Depth();
                    int done = EmitJump(OpCode::JumpIfNotNullKeep);
                    CompileExpr(node->Value);
                    EmitSet(ref);
                    PatchJump(done);
                    SetDepth(depth);
                    return;
                }
                CompileExpr(node->Value);
                emitOp();
                EmitSet(ref);
                return;
            }
            case NodeType::MemberAccessExpr:
            {
                auto* m = static_cast<MemberAccessNode*>(target);
                std::string globalName;
                if (ResolveNamespacePath(m, globalName))
                {
                    VarRef ref;
                    ref.Kind = VarKind::Global;
                    ref.Index = GlobalSlot(globalName);
                    EmitGet(ref);
                    CompileExpr(node->Value);
                    emitOp();
                    EmitSet(ref);
                    return;
                }
                int name = NameConstant(m->Member);
                CompileExpr(m->Object);
                Emit(OpCode::Dup);
                Emit(OpCode::GetProp, 0, name, (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                if (coalesce)
                {
                    // [obj cur] -> cur null değilse: [obj cur] -> Swap Pop -> [cur]
                    int depth = Depth();
                    int done = EmitJump(OpCode::JumpIfNotNullKeep);
                    CompileExpr(node->Value);
                    Emit(OpCode::SetProp, 0, name, (int)m_Fn->Fn->Caches.size());
                    m_Fn->Fn->Caches.emplace_back();
                    int end = EmitJump(OpCode::Jump);
                    PatchJump(done);
                    SetDepth(depth);
                    Emit(OpCode::Swap);
                    Emit(OpCode::Pop);
                    PatchJump(end);
                    SetDepth(depth - 1);
                    return;
                }
                CompileExpr(node->Value);
                emitOp();
                Emit(OpCode::SetProp, 0, name, (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                return;
            }
            case NodeType::IndexExpr:
            {
                auto* ix = static_cast<IndexNode*>(target);
                CompileExpr(ix->Object);
                CompileExpr(ix->Index);
                Emit(OpCode::Dup2);
                Emit(OpCode::GetIndex);
                if (coalesce)
                {
                    int depth = Depth();
                    int done = EmitJump(OpCode::JumpIfNotNullKeep);
                    CompileExpr(node->Value);
                    Emit(OpCode::SetIndex);
                    int end = EmitJump(OpCode::Jump);
                    PatchJump(done);
                    SetDepth(depth);
                    // [obj idx cur] -> [cur]
                    Emit(OpCode::Bury, 0, 2);
                    Emit(OpCode::Pop);
                    Emit(OpCode::Pop);
                    PatchJump(end);
                    SetDepth(depth - 2);
                    return;
                }
                CompileExpr(node->Value);
                emitOp();
                Emit(OpCode::SetIndex);
                return;
            }
            default:
                Error(node->Line, "Invalid compound assignment target.");
                Emit(OpCode::PushNull);
                return;
        }
    }

    void CQSCompiler::CompileIncDec(IncDecNode* node)
    {
        ASTNode* target = node->Target.get();
        OpCode op = node->Increment ? OpCode::Add : OpCode::Sub;
        switch (target ? target->Type : NodeType::Program)
        {
            case NodeType::IdentifierExpr:
            {
                auto* id = static_cast<IdentifierNode*>(target);
                VarRef ref = ResolveVariable(id->Name, id->Line);
                EmitGet(ref);
                if (!node->Prefix)
                    Emit(OpCode::Dup);
                Emit(OpCode::PushInt, 0, 0, 1);
                Emit(op);
                EmitSet(ref);
                if (!node->Prefix)
                    Emit(OpCode::Pop);
                return;
            }
            case NodeType::MemberAccessExpr:
            {
                auto* m = static_cast<MemberAccessNode*>(target);
                int name = NameConstant(m->Member);
                CompileExpr(m->Object);
                Emit(OpCode::Dup);
                Emit(OpCode::GetProp, 0, name, (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                if (!node->Prefix)
                {
                    Emit(OpCode::Dup);
                    Emit(OpCode::Bury, 0, 2);   // [old obj old]
                }
                Emit(OpCode::PushInt, 0, 0, 1);
                Emit(op);
                Emit(OpCode::SetProp, 0, name, (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
                if (!node->Prefix)
                    Emit(OpCode::Pop);
                return;
            }
            case NodeType::IndexExpr:
            {
                auto* ix = static_cast<IndexNode*>(target);
                CompileExpr(ix->Object);
                CompileExpr(ix->Index);
                Emit(OpCode::Dup2);
                Emit(OpCode::GetIndex);
                if (!node->Prefix)
                {
                    Emit(OpCode::Dup);
                    Emit(OpCode::Bury, 0, 3);   // [old obj idx old]
                }
                Emit(OpCode::PushInt, 0, 0, 1);
                Emit(op);
                Emit(OpCode::SetIndex);
                if (!node->Prefix)
                    Emit(OpCode::Pop);
                return;
            }
            default:
                Error(node->Line, "'++'/'--' requires a variable, field or element.");
                Emit(OpCode::PushNull);
                return;
        }
    }

    void CQSCompiler::CompileCall(CallNode* node)
    {
        size_t argc = node->Arguments.size();
        auto compileArgs = [&]() {
            for (auto& a : node->Arguments)
                CompileExpr(a);
        };
        // Generic: GetComponent<Rigidbody>() -> GetComponent("Rigidbody")
        auto pushGenericArgs = [&]() {
            for (auto& g : node->GenericArgs)
                Emit(OpCode::PushConst, 0, StringConstant(g));
            return node->GenericArgs.size();
        };

        ASTNode* callee = node->Callee.get();
        if (callee->Type == NodeType::MemberAccessExpr)
        {
            auto* m = static_cast<MemberAccessNode*>(callee);
            std::string globalName;
            if (ResolveNamespacePath(m, globalName))
            {
                Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(globalName));
                size_t g = pushGenericArgs();
                compileArgs();
                Emit(OpCode::Call, (int)(argc + g));
                return;
            }
            if (m->Object->Type == NodeType::BaseExpr)
            {
                if (!m_Fn->Class)
                    Error(node->Line, "'base' used outside of a class.");
                EmitThis(node->Line);
                size_t g = pushGenericArgs();
                compileArgs();
                Emit(OpCode::SuperInvoke, (int)(argc + g), NameConstant(m->Member));
                return;
            }
            int depth = Depth();
            CompileExpr(m->Object);
            int nullJump = -1;
            if (m->NullConditional)
                nullJump = EmitJump(OpCode::JumpIfNullKeep);
            size_t g = pushGenericArgs();
            compileArgs();
            Emit(OpCode::Invoke, (int)(argc + g), NameConstant(m->Member), (int)m_Fn->Fn->Caches.size());
            m_Fn->Fn->Caches.emplace_back();
            if (nullJump >= 0)
            {
                PatchJump(nullJump);
                SetDepth(depth + 1);
            }
            return;
        }
        if (callee->Type == NodeType::IdentifierExpr && m_Fn->Class)
        {
            // Sınıf içinde çıplak metot çağrısı -> this.Metot()
            auto* id = static_cast<IdentifierNode*>(callee);
            ClassState* cs = m_Fn->Class;
            bool shadowed = ResolveLocal(m_Fn, id->Name) >= 0 || ResolveUpvalue(m_Fn, id->Name) >= 0;
            if (!shadowed)
            {
                bool hasThis = m_Fn->IsMethod && !m_Fn->IsStatic;
                if (!hasThis)
                    for (FunctionState* f = m_Fn->Enclosing; f; f = f->Enclosing)
                        if (f->IsMethod) { hasThis = !f->IsStatic; break; }
                if (hasThis && cs->Methods.count(id->Name))
                {
                    EmitThis(node->Line);
                    size_t g = pushGenericArgs();
                    compileArgs();
                    Emit(OpCode::Invoke, (int)(argc + g), NameConstant(id->Name), (int)m_Fn->Fn->Caches.size());
                    m_Fn->Fn->Caches.emplace_back();
                    return;
                }
                if (cs->StaticNames.count(id->Name))
                {
                    Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(cs->Name));
                    size_t g = pushGenericArgs();
                    compileArgs();
                    Emit(OpCode::Invoke, (int)(argc + g), NameConstant(id->Name), (int)m_Fn->Fn->Caches.size());
                    m_Fn->Fn->Caches.emplace_back();
                    return;
                }
            }
        }
        int depth = Depth();
        CompileExpr(node->Callee);
        int nullJump = -1;
        if (node->NullConditional)
            nullJump = EmitJump(OpCode::JumpIfNullKeep);
        size_t g = pushGenericArgs();
        compileArgs();
        Emit(OpCode::Call, (int)(argc + g));
        if (nullJump >= 0)
        {
            PatchJump(nullJump);
            SetDepth(depth + 1);
        }
    }

    void CQSCompiler::CompileMember(MemberAccessNode* node)
    {
        std::string globalName;
        if (ResolveNamespacePath(node, globalName))
        {
            Emit(OpCode::GetGlobal, 0, 0, GlobalSlot(globalName));
            return;
        }
        if (node->Object->Type == NodeType::BaseExpr)
        {
            EmitThis(node->Line);
            Emit(OpCode::GetSuper, 0, NameConstant(node->Member));
            return;
        }
        if (node->Object->Type == NodeType::ThisExpr && m_Fn->Class && m_Fn->Class->LayoutKnown)
        {
            auto& layout = m_Fn->Class->Layout;
            auto it = std::find(layout.begin(), layout.end(), node->Member);
            if (it != layout.end())
            {
                VarRef ref;
                ref.Kind = VarKind::ThisField;
                ref.Index = (int)(it - layout.begin());
                ref.Name = node->Member;
                EmitGet(ref);
                return;
            }
        }
        int depth = Depth();
        CompileExpr(node->Object);
        int nullJump = -1;
        if (node->NullConditional)
            nullJump = EmitJump(OpCode::JumpIfNullKeep);
        Emit(OpCode::GetProp, 0, NameConstant(node->Member), (int)m_Fn->Fn->Caches.size());
        m_Fn->Fn->Caches.emplace_back();
        if (nullJump >= 0)
        {
            PatchJump(nullJump);
            SetDepth(depth + 1);
        }
    }

    void CQSCompiler::CompileIndex(IndexNode* node)
    {
        CompileExpr(node->Object);
        CompileExpr(node->Index);
        Emit(OpCode::GetIndex);
    }

    void CQSCompiler::CompileNew(NewNode* node)
    {
        if (node->ArraySize)
        {
            CompileExpr(node->ArraySize);
            Emit(OpCode::NewArray, 0, NameConstant(node->ElementType));
            return;
        }
        CompileExpr(node->ClassExpr);
        for (auto& a : node->Arguments)
            CompileExpr(a);
        Emit(OpCode::Call, (int)node->Arguments.size());
        for (auto& [field, expr] : node->Initializers)
        {
            Emit(OpCode::Dup);
            CompileExpr(expr);
            if (field.empty())
            {
                // Koleksiyon başlatıcı: new List<int> { 1, 2 } / new Map { ["a"] = 1 } desteklenmez
                Emit(OpCode::Invoke, 1, NameConstant("Add"), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
            }
            else
            {
                Emit(OpCode::SetProp, 0, NameConstant(field), (int)m_Fn->Fn->Caches.size());
                m_Fn->Fn->Caches.emplace_back();
            }
            Emit(OpCode::Pop);
        }
    }

    void CQSCompiler::CompileLambda(LambdaNode* node)
    {
        FunctionState fs;
        ClassState* savedClass = m_Class;
        m_Class = m_Fn->Class;
        BeginFunction(fs, node->Name.empty() ? "<lambda>" : node->Name, false);
        m_Class = savedClass;
        CompileFunctionBody(fs, node->Params, node->Body, node->ExpressionBody);
        EndFunction();
        Emit(OpCode::Closure, 0, AddConstant(Value::MakeObject(fs.Fn)));
    }

    void CQSCompiler::CompileTernary(TernaryNode* node)
    {
        int depth = Depth();
        CompileExpr(node->Condition);
        int elseJump = EmitJump(OpCode::JumpIfFalse);
        CompileExpr(node->TrueExpr);
        int endJump = EmitJump(OpCode::Jump);
        PatchJump(elseJump);
        SetDepth(depth);
        CompileExpr(node->FalseExpr);
        PatchJump(endJump);
        SetDepth(depth + 1);
    }

    void CQSCompiler::CompileList(ListLiteralNode* node)
    {
        if (node->Elements.size() > 60000)
        {
            Error(node->Line, "List literal too large.");
            Emit(OpCode::NewList, 0, 0);
            return;
        }
        for (auto& e : node->Elements)
            CompileExpr(e);
        Emit(OpCode::NewList, 0, (int)node->Elements.size());
    }

    void CQSCompiler::CompileMap(MapLiteralNode* node)
    {
        if (node->Entries.size() > 30000)
        {
            Error(node->Line, "Map literal too large.");
            Emit(OpCode::NewMap, 0, 0);
            return;
        }
        for (auto& [k, v] : node->Entries)
        {
            CompileExpr(k);
            CompileExpr(v);
        }
        Emit(OpCode::NewMap, 0, (int)node->Entries.size());
    }

    void CQSCompiler::CompileInterpolated(InterpolatedStringNode* node)
    {
        if (node->Parts.empty())
        {
            Emit(OpCode::PushConst, 0, StringConstant(""));
            return;
        }
        for (size_t i = 0; i < node->Parts.size(); ++i)
        {
            auto& part = node->Parts[i];
            if (part->Type == NodeType::LiteralExpr && static_cast<LiteralNode*>(part.get())->IsString)
            {
                CompileLiteral(static_cast<LiteralNode*>(part.get()));
                continue;
            }
            CompileExpr(part);
            const std::string& fmt = node->Formats[i];
            Emit(OpCode::ToStr, 0, fmt.empty() ? 0xFFFF : NameConstant(fmt));
        }
        if (node->Parts.size() > 1)
            Emit(OpCode::Concat, 0, (int)node->Parts.size());
    }

    void CQSCompiler::CompileTypeTest(TypeTestNode* node)
    {
        CompileExpr(node->Operand);
        Emit(node->Type == NodeType::IsExpr ? OpCode::IsType : OpCode::CastType, 0, NameConstant(node->TypeName));
    }
}
