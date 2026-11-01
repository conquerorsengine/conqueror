#include "LuaQAst.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <unordered_map>

// LuaQ AST -> Luau kaynağı. Satır numaraları korunur (her deyim kaynaktaki satırına yazılır; indirgemeler aynı
// satırda kalır), böylece Luau hata iletileri LuaQ dosyasının satırlarını gösterir.
//
// goto: etiketin bloğunda "bölgeler" kurulur. İleri atlama: ilk goto içeren deyimden etikete kadar
// repeat ... until true (goto -> break); geri atlama: etiketten blok sonuna while true do ... break end
// (goto -> continue). Aradaki döngüleri / sarmalayıcıları aşan atlamalar bayrak + break zinciriyle iletilir;
// break / continue de aynı mekanizmayı kullanır. defer ve <close> temizlikleri her çıkış noktasında çalışır.
namespace Conqueror::LuaQ::Detail
{
    namespace
    {
        [[noreturn]] void Fail(int line, int col, const std::string& message)
        {
            throw CompileError{ line, col, message };
        }

        // ─── goto taraması: deyim (iç bloklar dahil, fonksiyonlar hariç) bu etikete goto içeriyor mu
        bool BlockDefinesLabel(const Block& b, const std::string& label)
        {
            for (const auto& s : b.Stats)
                if (s->Kind == SK::Label && s->Op == label)
                    return true;
            return false;
        }

        bool ContainsGoto(const Stat& s, const std::string& label);

        bool BlockContainsGoto(const Block* b, const std::string& label)
        {
            if (!b || BlockDefinesLabel(*b, label))
                return false;   // iç blok aynı adı gölgeler
            for (const auto& s : b->Stats)
                if (ContainsGoto(*s, label))
                    return true;
            return false;
        }

        bool ContainsGoto(const Stat& s, const std::string& label)
        {
            if (s.Kind == SK::Goto)
                return s.Op == label;
            if (BlockContainsGoto(s.Body.get(), label) || BlockContainsGoto(s.Else.get(), label))
                return true;
            for (const auto& b : s.Blocks)
                if (BlockContainsGoto(b.get(), label))
                    return true;
            for (const auto& c : s.Cases)
                if (BlockContainsGoto(c.Body.get(), label))
                    return true;
            if (s.Inner && ContainsGoto(*s.Inner, label))
                return true;
            return false;
        }

        struct Region
        {
            size_t Start = 0, End = 0;    // [Start, End) deyim indisleri
            bool Forward = true;
            std::string Label;
        };

        class Emitter
        {
        public:
            explicit Emitter(const EmitOptions& options) : m_Options(options) {}

            std::string Run(const Chunk& chunk)
            {
                m_Frames.push_back({ FrameKind::Barrier });
                PushScope(true);
                const Block& body = *chunk.Body;
                EmitBlockStatements(body);
                bool hasReturn = !body.Stats.empty() && body.Stats.back()->Kind == SK::Return;
                if (!hasReturn && !m_LastTerminator)
                {
                    EmitScopeCleanups(m_Scopes.back());
                    std::string returnClass;
                    if (!m_Options.AutoReturnClass.empty())
                    {
                        if (std::find(m_TopClasses.begin(), m_TopClasses.end(), m_Options.AutoReturnClass) != m_TopClasses.end())
                            returnClass = m_Options.AutoReturnClass;
                        else if (m_TopClasses.size() == 1)
                            returnClass = m_TopClasses.front();
                    }
                    if (!returnClass.empty())
                        Text(" return " + returnClass);
                }
                m_Scopes.pop_back();
                m_Out += '\n';
                return m_Out;
            }

        private:
            // ════════════════════════════════ Çıktı
            void Text(const std::string& s)
            {
                m_Out += s;
                for (char c : s)
                    if (c == '\n')
                        ++m_Line;
            }

            void At(int line)
            {
                while (m_Line < line)
                {
                    m_Out += '\n';
                    ++m_Line;
                }
                if (!m_Out.empty() && m_Out.back() != '\n' && m_Out.back() != ' ')
                    m_Out += ' ';
            }

            std::string Temp(const char* base) { return "__q" + std::string(base) + std::to_string(++m_TempCounter); }

            // ════════════════════════════════ Kapsamlar / çerçeveler
            struct Cleanup
            {
                bool IsClose = false;
                std::string Var;
                const Stat* Deferred = nullptr;
            };
            struct Local
            {
                std::string Name;
                bool Const = false;
            };
            struct Scope
            {
                std::vector<Cleanup> Cleanups;
                std::vector<Local> Locals;
                bool FunctionBody = false;
            };
            enum class FrameKind { Loop, Forward, Backward, Barrier };
            struct Check
            {
                std::string Flag;
                std::string Action;    // "break" | "continue"
                bool Final = false;    // hedefe varıldı (bayrak sıfırlanır)
            };
            struct Frame
            {
                FrameKind Kind = FrameKind::Loop;
                std::string Label;
                size_t ScopeIndex = 0;     // gövde kapsamının indisi
                size_t DeclOffset = 0;     // gövde başı (bayrak yerelleri buraya eklenir)
                std::vector<std::string> Flags;
                std::vector<Check> AfterClose;
                int Id = 0;
            };

            void PushScope(bool functionBody = false)
            {
                Scope s;
                s.FunctionBody = functionBody;
                m_Scopes.push_back(std::move(s));
            }

            void PopScope()
            {
                if (!m_LastTerminator)
                    EmitScopeCleanups(m_Scopes.back());
                m_Scopes.pop_back();
            }

            void EmitCleanup(const Cleanup& c)
            {
                if (c.IsClose)
                    Text(" __luaq.close(" + c.Var + ");");
                else if (c.Deferred)
                {
                    const bool saved = m_LastTerminator;
                    EmitStatement(*c.Deferred, false);
                    Text(";");
                    m_LastTerminator = saved;
                }
            }

            void EmitScopeCleanups(const Scope& s)
            {
                for (auto it = s.Cleanups.rbegin(); it != s.Cleanups.rend(); ++it)
                    EmitCleanup(*it);
            }

            bool HasCleanupsFrom(size_t scopeIndex) const
            {
                for (size_t i = scopeIndex; i < m_Scopes.size(); ++i)
                    if (!m_Scopes[i].Cleanups.empty())
                        return true;
                return false;
            }

            void EmitCleanupsFrom(size_t scopeIndex)
            {
                for (size_t i = m_Scopes.size(); i-- > scopeIndex;)
                    EmitScopeCleanups(m_Scopes[i]);
            }

            size_t FunctionScopeIndex() const
            {
                for (size_t i = m_Scopes.size(); i-- > 0;)
                    if (m_Scopes[i].FunctionBody)
                        return i;
                return 0;
            }

            void DeclareLocal(const std::string& name, bool isConst = false)
            {
                m_Scopes.back().Locals.push_back({ name, isConst });
            }

            bool IsConstName(const std::string& name) const
            {
                for (size_t i = m_Scopes.size(); i-- > 0;)
                    for (auto it = m_Scopes[i].Locals.rbegin(); it != m_Scopes[i].Locals.rend(); ++it)
                        if (it->Name == name)
                            return it->Const;
                return false;
            }

            void OpenFrame(FrameKind kind, const std::string& label = {})
            {
                Frame f;
                f.Kind = kind;
                f.Label = label;
                f.ScopeIndex = m_Scopes.size();   // sıradaki PushScope gövdedir
                f.DeclOffset = m_Out.size();
                f.Id = ++m_FrameCounter;
                m_Frames.push_back(std::move(f));
            }

            // Çerçeveyi kapat: bayrak yerellerini gövde başına ekle; yapı kapandıktan sonra çağrılacak denetimleri döndür
            std::vector<Check> CloseFrame()
            {
                Frame f = std::move(m_Frames.back());
                m_Frames.pop_back();
                if (!f.Flags.empty())
                {
                    std::string decl = " local ";
                    std::string init;
                    for (size_t i = 0; i < f.Flags.size(); ++i)
                    {
                        decl += (i ? ", " : "") + f.Flags[i];
                        init += (i ? ", " : "") + std::string("false");
                    }
                    decl += " = " + init + ";";
                    m_Out.insert(f.DeclOffset, decl);
                }
                return f.AfterClose;
            }

            void EmitChecks(const std::vector<Check>& checks)
            {
                std::set<std::string> seen;
                for (const Check& c : checks)
                {
                    if (!seen.insert(c.Flag + c.Action).second)
                        continue;
                    if (c.Final)
                        Text(" if " + c.Flag + " then " + c.Flag + " = false; " + c.Action + " end");
                    else
                        Text(" if " + c.Flag + " then break end");
                }
            }

            // break / continue / goto: hedef çerçeveye kadar temizlik + doğrudan ya da bayrakla atlama
            void EmitJump(const Stat& s, bool last)
            {
                size_t target = 0;
                bool found = false;
                for (size_t i = m_Frames.size(); i-- > 0;)
                {
                    const Frame& f = m_Frames[i];
                    if (f.Kind == FrameKind::Barrier)
                        break;
                    if (s.Kind == SK::Goto ? ((f.Kind == FrameKind::Forward || f.Kind == FrameKind::Backward) && f.Label == s.Op)
                                           : f.Kind == FrameKind::Loop)
                    {
                        target = i;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    if (s.Kind == SK::Goto)
                        Fail(s.Line, s.Col, "no visible label '" + s.Op + "' for goto");
                    Fail(s.Line, s.Col, std::string(s.Kind == SK::Break ? "break" : "continue") + " outside a loop");
                }
                Frame& tf = m_Frames[target];
                std::string action;
                if (tf.Kind == FrameKind::Forward) action = "break";
                else if (tf.Kind == FrameKind::Backward) action = "continue";
                else action = s.Kind == SK::Continue ? "continue" : "break";

                const bool wrap = !last;
                if (wrap) Text(" do");
                EmitCleanupsFrom(tf.ScopeIndex);
                if (target == m_Frames.size() - 1)
                {
                    Text(" " + action);
                }
                else
                {
                    const std::string flag = "__qj" + std::to_string(tf.Id) + (action == "break" ? "b" : "c");
                    if (std::find(tf.Flags.begin(), tf.Flags.end(), flag) == tf.Flags.end())
                        tf.Flags.push_back(flag);
                    // Aradaki her çerçeve kapanınca bayrak denetlenir; hedefin hemen içindeki çerçevede asıl eylem
                    for (size_t k = target + 1; k < m_Frames.size(); ++k)
                        m_Frames[k].AfterClose.push_back({ flag, action, k == target + 1 });
                    Text(" " + flag + " = true; break");
                }
                if (wrap) Text(" end");
                m_LastTerminator = true;
            }

            // ════════════════════════════════ Bloklar
            void EmitBlockStatements(const Block& b)
            {
                // Etiket bölgeleri
                std::vector<Region> regions;
                std::set<std::string> labels;
                for (size_t li = 0; li < b.Stats.size(); ++li)
                {
                    const Stat& s = *b.Stats[li];
                    if (s.Kind != SK::Label)
                        continue;
                    if (!labels.insert(s.Op).second)
                        Fail(s.Line, s.Col, "label '" + s.Op + "' already defined in this block");
                    size_t first = li;
                    for (size_t i = 0; i < li; ++i)
                        if (ContainsGoto(*b.Stats[i], s.Op)) { first = i; break; }
                    if (first < li)
                        regions.push_back({ first, li, true, s.Op });
                    bool backward = false;
                    for (size_t i = li + 1; i < b.Stats.size(); ++i)
                        if (ContainsGoto(*b.Stats[i], s.Op)) { backward = true; break; }
                    if (backward)
                        regions.push_back({ li + 1, b.Stats.size(), false, s.Op });
                }
                // Kesişen bölgeleri iç içe hale getir (ileri bölgenin başı öne çekilir)
                for (bool changed = true; changed;)
                {
                    changed = false;
                    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& c) {
                        return a.Start != c.Start ? a.Start < c.Start : a.End > c.End;
                    });
                    for (size_t i = 0; i < regions.size() && !changed; ++i)
                        for (size_t j = i + 1; j < regions.size(); ++j)
                        {
                            Region& r1 = regions[i];
                            Region& r2 = regions[j];
                            if (r2.Start > r1.Start && r2.Start < r1.End && r2.End > r1.End)
                            {
                                if (!r1.Forward || !r2.Forward)
                                {
                                    const Stat& at = *b.Stats[r2.Start];
                                    Fail(at.Line, at.Col, "unsupported goto layout: jumps to '" + r1.Label + "' and '" + r2.Label + "' cross each other");
                                }
                                r2.Start = r1.Start;
                                changed = true;
                                break;
                            }
                        }
                }
                std::vector<bool> used(regions.size(), false);
                EmitRange(b, 0, b.Stats.size(), regions, used);
            }

            void EmitRange(const Block& b, size_t from, size_t to, const std::vector<Region>& regions, std::vector<bool>& used)
            {
                size_t i = from;
                while (i < to)
                {
                    int pick = -1;
                    for (size_t r = 0; r < regions.size(); ++r)
                        if (!used[r] && regions[r].Start == i && regions[r].End <= to)
                        {
                            pick = (int)r;
                            break;
                        }
                    if (pick >= 0)
                    {
                        used[(size_t)pick] = true;
                        const Region& r = regions[(size_t)pick];
                        At(b.Stats[r.Start]->Line);
                        m_LastTerminator = false;
                        if (r.Forward)
                        {
                            Text("repeat");
                            OpenFrame(FrameKind::Forward, r.Label);
                            PushScope();
                            EmitRange(b, r.Start, r.End, regions, used);
                            PopScope();
                            auto checks = CloseFrame();
                            Text(" until true");
                            EmitChecks(checks);
                        }
                        else
                        {
                            Text("while true do");
                            OpenFrame(FrameKind::Backward, r.Label);
                            PushScope();
                            EmitRange(b, r.Start, r.End, regions, used);
                            const bool term = m_LastTerminator;
                            PopScope();
                            if (!term)
                                Text(" break");
                            auto checks = CloseFrame();
                            Text(" end");
                            EmitChecks(checks);
                        }
                        m_LastTerminator = false;
                        i = r.End;
                        continue;
                    }
                    const Stat& s = *b.Stats[i];
                    if (s.Kind != SK::Label)
                    {
                        // Bloğun son (etiket olmayan) deyimi mi: atlamalar ancak sonda yalın yazılabilir
                        bool last = true;
                        for (size_t k = i + 1; k < to; ++k)
                            if (b.Stats[k]->Kind != SK::Label) { last = false; break; }
                        m_LastTerminator = false;
                        EmitStatement(s, last && to == b.Stats.size());
                    }
                    ++i;
                }
            }

            void EmitBody(const Block& b)
            {
                PushScope();
                m_LastTerminator = false;
                EmitBlockStatements(b);
                PopScope();
                At(b.EndLine);
            }

            void EmitLoopBody(const Block& b)
            {
                OpenFrame(FrameKind::Loop);
                PushScope();
                m_LastTerminator = false;
                EmitBlockStatements(b);
                PopScope();
            }

            // ════════════════════════════════ Fonksiyonlar
            void EmitParams(const FuncBody& f)
            {
                Text("(");
                for (size_t i = 0; i < f.Params.size(); ++i)
                    Text((i ? ", " : "") + f.Params[i].Name);
                if (f.Vararg)
                    Text(f.Params.empty() ? "..." : ", ...");
                Text(")");
            }

            void EmitFunctionBody(const FuncBody& f)
            {
                m_Frames.push_back({ FrameKind::Barrier });
                PushScope(true);
                if (f.ImplicitSelf)
                    DeclareLocal("self");
                for (const auto& p : f.Params)
                    DeclareLocal(p.Name);
                // LuaQ varsayılan parametreler
                for (const auto& p : f.Params)
                    if (p.Default)
                    {
                        Text(" if " + p.Name + " == nil then " + p.Name + " = ");
                        EmitExpr(*p.Default);
                        Text(" end;");
                    }
                const bool savedAsync = m_InAsync;
                if (f.Async)
                {
                    // async: gövde bir görev (eşyordam) olarak başlar; çağıran görevi alır
                    Text(f.Vararg ? " return __luaq.async(function(...)" : " return __luaq.async(function()");
                    m_InAsync = true;
                    m_Frames.push_back({ FrameKind::Barrier });
                    PushScope(true);
                }
                m_LastTerminator = false;
                EmitBlockStatements(*f.Body);
                if (f.Async)
                {
                    PopScope();
                    m_Frames.pop_back();
                    Text(f.Vararg ? " end, ...)" : " end)");
                    m_LastTerminator = true;
                }
                PopScope();
                m_Frames.pop_back();
                m_InAsync = savedAsync;
                At(f.Body->EndLine);
                Text("end");
            }

            void EmitAttributes(const std::vector<std::string>& attributes)
            {
                for (const auto& a : attributes)
                    Text(a + " ");
            }

            // ════════════════════════════════ Deyimler
            void EmitStatement(const Stat& s, bool last)
            {
                At(s.Line);
                switch (s.Kind)
                {
                    case SK::Empty:
                    case SK::Label:
                        break;
                    case SK::Local: EmitLocal(s); break;
                    case SK::Assign:
                    {
                        for (size_t i = 0; i < s.Targets.size(); ++i)
                        {
                            CheckConstTarget(*s.Targets[i]);
                            if (i) Text(", ");
                            EmitExpr(*s.Targets[i]);
                        }
                        Text(" = ");
                        EmitExprList(s.Values);
                        break;
                    }
                    case SK::Compound: EmitCompound(s); break;
                    case SK::CallStat: EmitCallStatement(*s.Values[0]); break;
                    case SK::Do:
                        Text("do");
                        EmitBody(*s.Body);
                        Text("end");
                        break;
                    case SK::While:
                    {
                        Text("while ");
                        EmitExpr(*s.Cond);
                        Text(" do");
                        EmitLoopBody(*s.Body);
                        auto checks = CloseFrame();
                        At(s.Body->EndLine);
                        Text("end");
                        EmitChecks(checks);
                        break;
                    }
                    case SK::Repeat:
                    {
                        Text("repeat");
                        // until koşulu gövde yerellerini görür: kapsamı koşuldan sonra kapat
                        OpenFrame(FrameKind::Loop);
                        PushScope();
                        m_LastTerminator = false;
                        EmitBlockStatements(*s.Body);
                        if (!m_LastTerminator)
                            EmitScopeCleanups(m_Scopes.back());
                        auto checks = CloseFrame();
                        At(s.Body->EndLine);
                        Text("until ");
                        EmitExpr(*s.Cond);
                        m_Scopes.pop_back();
                        EmitChecks(checks);
                        break;
                    }
                    case SK::If:
                    {
                        for (size_t i = 0; i < s.Blocks.size(); ++i)
                        {
                            Text(i ? "elseif " : "if ");
                            EmitExpr(*s.Values[i]);
                            Text(" then");
                            EmitBody(*s.Blocks[i]);
                        }
                        if (s.Else)
                        {
                            Text("else");
                            EmitBody(*s.Else);
                        }
                        Text("end");
                        break;
                    }
                    case SK::NumFor:
                    {
                        Text("for " + s.Names[0] + " = ");
                        EmitExprList(s.Values);
                        Text(" do");
                        OpenFrame(FrameKind::Loop);
                        PushScope();
                        DeclareLocal(s.Names[0]);
                        m_LastTerminator = false;
                        EmitBlockStatements(*s.Body);
                        PopScope();
                        auto checks = CloseFrame();
                        At(s.Body->EndLine);
                        Text("end");
                        EmitChecks(checks);
                        break;
                    }
                    case SK::GenFor:
                    {
                        Text("for ");
                        for (size_t i = 0; i < s.Names.size(); ++i)
                            Text((i ? ", " : "") + s.Names[i]);
                        Text(" in ");
                        EmitExprList(s.Values);
                        Text(" do");
                        OpenFrame(FrameKind::Loop);
                        PushScope();
                        for (const auto& n : s.Names)
                            DeclareLocal(n);
                        m_LastTerminator = false;
                        EmitBlockStatements(*s.Body);
                        PopScope();
                        auto checks = CloseFrame();
                        At(s.Body->EndLine);
                        Text("end");
                        EmitChecks(checks);
                        break;
                    }
                    case SK::Function:
                    {
                        EmitAttributes(s.Attributes);
                        std::string name = s.FuncPath[0];
                        for (size_t i = 1; i < s.FuncPath.size(); ++i)
                            name += (s.FuncMethod && i + 1 == s.FuncPath.size() ? ":" : ".") + s.FuncPath[i];
                        if (s.FuncPath.size() == 1)
                            CheckConstName(s.FuncPath[0], s);
                        Text("function " + name);
                        EmitParams(*s.Func);
                        EmitFunctionBody(*s.Func);
                        break;
                    }
                    case SK::LocalFunction:
                        EmitAttributes(s.Attributes);
                        DeclareLocal(s.Names[0]);
                        Text("local function " + s.Names[0]);
                        EmitParams(*s.Func);
                        EmitFunctionBody(*s.Func);
                        break;
                    case SK::Return: EmitReturn(s); break;
                    case SK::Break:
                    case SK::Continue:
                    case SK::Goto:
                        EmitJump(s, last);
                        return;
                    case SK::Class: EmitClass(s); break;
                    case SK::Match: EmitMatch(s); break;
                    case SK::Defer:
                        m_Scopes.back().Cleanups.push_back({ false, {}, s.Inner.get() });
                        break;
                    case SK::Enum: EmitEnum(s); break;
                    case SK::Destructure:
                    {
                        const std::string tmp = Temp("d");
                        Text("local " + tmp + " = ");
                        EmitExpr(*s.Values[0]);
                        Text("; local ");
                        for (size_t i = 0; i < s.Names.size(); ++i)
                            Text((i ? ", " : "") + s.Names[i]);
                        Text(" = ");
                        for (size_t i = 0; i < s.Names.size(); ++i)
                        {
                            if (i) Text(", ");
                            if (s.ArrayPattern)
                                Text(tmp + "[" + std::to_string(i + 1) + "]");
                            else
                                Text(tmp + "." + s.Names[i]);
                        }
                        for (const auto& n : s.Names)
                            DeclareLocal(n);
                        break;
                    }
                }
                m_LastTerminator = s.Kind == SK::Return;
            }

            void EmitLocal(const Stat& s)
            {
                Text("local ");
                for (size_t i = 0; i < s.Names.size(); ++i)
                    Text((i ? ", " : "") + s.Names[i]);
                if (!s.Values.empty())
                {
                    Text(" = ");
                    EmitExprList(s.Values);
                }
                for (size_t i = 0; i < s.Names.size(); ++i)
                {
                    const std::string& a = i < s.Attribs.size() ? s.Attribs[i] : std::string();
                    if (a == "const" && s.Values.empty())
                        Fail(s.Line, s.Col, "const variable '" + s.Names[i] + "' needs a value");
                    DeclareLocal(s.Names[i], a == "const" || a == "close");
                    if (a == "close")
                        m_Scopes.back().Cleanups.push_back({ true, s.Names[i], nullptr });
                }
            }

            void CheckConstName(const std::string& name, const Stat& s)
            {
                if (IsConstName(name))
                    Fail(s.Line, s.Col, "attempt to assign to const variable '" + name + "'");
            }

            void CheckConstTarget(const Expr& e)
            {
                if (e.Kind == EK::Name && IsConstName(e.Text))
                    Fail(e.Line, e.Col, "attempt to assign to const variable '" + e.Text + "'");
            }

            void EmitCompound(const Stat& s)
            {
                const Expr& target = *s.Targets[0];
                CheckConstTarget(target);
                static const std::unordered_map<std::string, std::string> kBit = {
                    { "&=", "bit32.band" }, { "|=", "bit32.bor" }, { "<<=", "bit32.lshift" }, { ">>=", "bit32.rshift" },
                };
                const auto bit = kBit.find(s.Op);
                if (bit == kBit.end() && s.Op != "??=")
                {
                    // Luau yerel bileşik atama (hedef bir kez değerlendirilir)
                    EmitExpr(target);
                    Text(" " + s.Op + " ");
                    EmitExpr(*s.Values[0]);
                    return;
                }
                // Hedef yan etkili olabilir: nesne / anahtar bir kez değerlendirilir
                std::string ref;
                bool block = false;
                if (target.Kind == EK::Name)
                    ref = target.Text;
                else if (IsPure(*target.A) && (target.Kind == EK::Field || IsPure(*target.B)))
                    ref = Render(target);
                else
                {
                    block = true;
                    const std::string obj = Temp("o");
                    Text("do local " + obj + " = ");
                    EmitExpr(*target.A);
                    if (target.Kind == EK::Field)
                        ref = obj + "." + target.Text;
                    else
                    {
                        const std::string key = Temp("k");
                        Text("; local " + key + " = ");
                        EmitExpr(*target.B);
                        ref = obj + "[" + key + "]";
                    }
                    Text(";");
                }
                if (s.Op == "??=")
                {
                    Text(" if " + ref + " == nil then " + ref + " = ");
                    EmitExpr(*s.Values[0]);
                    Text(" end");
                }
                else
                {
                    Text(" " + ref + " = " + bit->second + "(" + ref + ", ");
                    EmitExpr(*s.Values[0]);
                    Text(")");
                }
                if (block)
                    Text(" end");
            }

            void EmitCallStatement(const Expr& e)
            {
                // Saf zincirde ?. çağrısı: if nesne ~= nil then çağrı end (ifade deyimi olamaz)
                std::string guard;
                if (HasOptional(e) && OptionalGuard(e, guard))
                {
                    Text("if " + guard + " then ");
                    EmitChainPlain(e);
                    Text(" end");
                    return;
                }
                // "(function() ... end)()" önceki satırın çağrısı sanılmasın
                if (HasOptional(e))
                    Text(";");
                EmitExpr(e);
            }

            void EmitReturn(const Stat& s)
            {
                const size_t fn = FunctionScopeIndex();
                if (!HasCleanupsFrom(fn))
                {
                    Text("return");
                    if (!s.Values.empty())
                    {
                        Text(" ");
                        EmitExprList(s.Values);
                    }
                    return;
                }
                // Değerler temizlikten önce hesaplanır
                const std::string tmp = Temp("r");
                Text("do local " + tmp + " = table.pack(");
                EmitExprList(s.Values);
                Text(");");
                EmitCleanupsFrom(fn);
                Text(" return table.unpack(" + tmp + ", 1, " + tmp + ".n) end");
            }

            void EmitClass(const Stat& s)
            {
                if (m_Scopes.size() == 1)
                    m_TopClasses.push_back(s.ClassName);
                CheckConstName(s.ClassName, s);
                Text("local " + s.ClassName + " = __luaq.class(\"" + s.ClassName + "\", ");
                if (s.Base) EmitExpr(*s.Base); else Text("nil");
                Text(")");
                DeclareLocal(s.ClassName);
                m_ClassStack.push_back(s.ClassName);
                for (const auto& m : s.Members)
                {
                    At(m.Line);
                    switch (m.K)
                    {
                        case ClassMember::Field:
                        case ClassMember::StaticField:
                            Text(s.ClassName + "." + m.Name + " = ");
                            if (m.Value) EmitExpr(*m.Value); else Text("nil");
                            Text(";");
                            break;
                        case ClassMember::Method:
                        case ClassMember::StaticMethod:
                            EmitAttributes(m.Attributes);
                            Text("function " + s.ClassName + (m.K == ClassMember::Method ? ":" : ".") + m.Name);
                            EmitParams(*m.Func);
                            EmitFunctionBody(*m.Func);
                            Text(";");
                            break;
                    }
                }
                m_ClassStack.pop_back();
            }

            void EmitMatch(const Stat& s)
            {
                const std::string subject = Temp("m");
                Text("do local " + subject + " = ");
                EmitExpr(*s.Cond);
                Text(";");
                bool first = true;
                for (const auto& c : s.Cases)
                {
                    At(c.Line);
                    Text(first ? "if " : "elseif ");
                    first = false;
                    std::string cond;
                    if (!c.Wildcard)
                    {
                        Text("(");
                        for (size_t i = 0; i < c.Patterns.size(); ++i)
                        {
                            Text(i ? " or " + subject + " == " : subject + " == ");
                            EmitExpr(*c.Patterns[i]);
                        }
                        Text(")");
                    }
                    if (c.Guard)
                    {
                        Text(c.Wildcard ? "(" : " and (");
                        EmitExpr(*c.Guard);
                        Text(")");
                    }
                    else if (c.Wildcard)
                        Text("true");
                    Text(" then");
                    EmitBody(*c.Body);
                }
                if (s.Else)
                {
                    Text(s.Cases.empty() ? "if true then" : "else");
                    EmitBody(*s.Else);
                }
                if (!s.Cases.empty() || s.Else)
                    Text("end");
                Text(" end");
            }

            void EmitEnum(const Stat& s)
            {
                CheckConstName(s.ClassName, s);
                Text("local " + s.ClassName + " = table.freeze({");
                double next = 0.0;
                for (size_t i = 0; i < s.Names.size(); ++i)
                {
                    Text((i ? ", " : "") + s.Names[i] + " = ");
                    if (s.EnumValues[i])
                    {
                        EmitExpr(*s.EnumValues[i]);
                        if (s.EnumValues[i]->Kind == EK::Number)
                            next = std::strtod(s.EnumValues[i]->Text.c_str(), nullptr) + 1.0;
                    }
                    else
                    {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.17g", next);
                        Text(buf);
                        next += 1.0;
                    }
                }
                Text("})");
                DeclareLocal(s.ClassName, true);
            }

            // ════════════════════════════════ İfadeler
            void EmitExprList(const std::vector<ExprP>& list)
            {
                for (size_t i = 0; i < list.size(); ++i)
                {
                    if (i) Text(", ");
                    EmitExpr(*list[i]);
                }
            }

            static bool IsPure(const Expr& e)
            {
                switch (e.Kind)
                {
                    case EK::Name: case EK::Nil: case EK::True: case EK::False: case EK::Number: case EK::String: case EK::Super:
                        return true;
                    case EK::Field: return IsPure(*e.A);
                    case EK::Index: return IsPure(*e.A) && IsPure(*e.B);
                    case EK::Paren: return IsPure(*e.A);
                    default: return false;
                }
            }

            static bool HasOptional(const Expr& e)
            {
                const Expr* p = &e;
                while (p)
                {
                    if ((p->Kind == EK::Field || p->Kind == EK::Index || p->Kind == EK::Call || p->Kind == EK::Method) && p->Optional)
                        return true;
                    if (p->Kind == EK::Field || p->Kind == EK::Index || p->Kind == EK::Call || p->Kind == EK::Method)
                        p = p->A.get();
                    else
                        break;
                }
                return false;
            }

            // Zincirin saf önekleri için "a ~= nil and a.b ~= nil" koşulu; önek saf değilse false
            bool OptionalGuard(const Expr& e, std::string& guard)
            {
                std::vector<const Expr*> chain;
                for (const Expr* p = &e; p; p = (p->Kind == EK::Field || p->Kind == EK::Index || p->Kind == EK::Call || p->Kind == EK::Method) ? p->A.get() : nullptr)
                    chain.push_back(p);
                std::reverse(chain.begin(), chain.end());   // taban önce
                guard.clear();
                for (size_t i = 1; i < chain.size(); ++i)
                {
                    if (!chain[i]->Optional)
                        continue;
                    const Expr& prefix = *chain[i]->A;
                    if (!IsPurePlain(prefix))
                        return false;
                    guard += (guard.empty() ? "" : " and ") + RenderPlain(prefix) + " ~= nil";
                }
                return !guard.empty();
            }

            // ?. işaretleri yok sayılarak saflık (yalnızca ad / alan / sabit indeks)
            static bool IsPurePlain(const Expr& e)
            {
                switch (e.Kind)
                {
                    case EK::Name: case EK::Super: case EK::Number: case EK::String: return true;
                    case EK::Field: return IsPurePlain(*e.A);
                    case EK::Index: return IsPurePlain(*e.A) && IsPurePlain(*e.B);
                    case EK::Paren: return IsPurePlain(*e.A);
                    default: return false;
                }
            }

            std::string RenderPlain(const Expr& e)
            {
                std::string saved;
                saved.swap(m_Out);
                const int savedLine = m_Line;
                EmitChainPlain(e);
                std::string r;
                r.swap(m_Out);
                m_Out.swap(saved);
                m_Line = savedLine;
                return r;
            }

            std::string Render(const Expr& e)
            {
                std::string saved;
                saved.swap(m_Out);
                const int savedLine = m_Line;
                EmitExpr(e);
                std::string r;
                r.swap(m_Out);
                m_Out.swap(saved);
                m_Line = savedLine;
                return r;
            }

            // ?. işaretlerini yok sayarak zinciri yaz (koşul ayrıca sağlandığında)
            void EmitChainPlain(const Expr& e)
            {
                switch (e.Kind)
                {
                    case EK::Field: EmitChainPlain(*e.A); Text("." + e.Text); return;
                    case EK::Index: EmitChainPlain(*e.A); Text("["); EmitExpr(*e.B); Text("]"); return;
                    case EK::Method: EmitMethodCall(e, true); return;
                    case EK::Call: EmitCall(e, true); return;
                    default: EmitExpr(e); return;
                }
            }

            void EmitArgs(const std::vector<ExprP>& args)
            {
                Text("(");
                for (size_t i = 0; i < args.size(); ++i)
                {
                    if (i) Text(", ");
                    const Expr& a = *args[i];
                    if (a.Kind == EK::Spread)
                    {
                        if (i + 1 != args.size())
                            Fail(a.Line, a.Col, "spread argument (...list) must be the last argument");
                        Text("table.unpack(");
                        EmitExpr(*a.A);
                        Text(")");
                    }
                    else
                        EmitExpr(a);
                }
                Text(")");
            }

            void EmitCall(const Expr& e, bool plain)
            {
                if (plain) EmitChainPlain(*e.A); else EmitExpr(*e.A);
                EmitArgs(e.List);
            }

            void EmitMethodCall(const Expr& e, bool plain)
            {
                if (e.A->Kind == EK::Super)
                {
                    // super:m(a) -> Sınıf.__super.m(self, a)
                    Text(SuperRef(e) + "." + e.Text + "(self");
                    for (const auto& a : e.List)
                    {
                        Text(", ");
                        EmitExpr(*a);
                    }
                    Text(")");
                    return;
                }
                if (plain) EmitChainPlain(*e.A); else EmitExpr(*e.A);
                Text(":" + e.Text);
                EmitArgs(e.List);
            }

            std::string SuperRef(const Expr& e)
            {
                if (m_ClassStack.empty())
                    Fail(e.Line, e.Col, "'super' is only valid inside class methods");
                return m_ClassStack.back() + ".__super";
            }

            // ?. içeren zincir: saf önekte if-ifadesi, değilse adım adım geçici değişkenli IIFE
            void EmitOptionalChain(const Expr& e)
            {
                std::string guard;
                if (OptionalGuard(e, guard))
                {
                    Text("(if " + guard + " then ");
                    EmitChainPlain(e);
                    Text(" else nil)");
                    return;
                }
                std::vector<const Expr*> chain;
                for (const Expr* p = &e; p; p = (p->Kind == EK::Field || p->Kind == EK::Index || p->Kind == EK::Call || p->Kind == EK::Method) ? p->A.get() : nullptr)
                    chain.push_back(p);
                std::reverse(chain.begin(), chain.end());
                const std::string t = Temp("t");
                Text("(function() local " + t + " = ");
                EmitExpr(*chain[0]);
                for (size_t i = 1; i < chain.size(); ++i)
                {
                    const Expr& step = *chain[i];
                    if (step.Optional)
                        Text("; if " + t + " == nil then return nil end");
                    Text(i + 1 == chain.size() ? "; return " : "; " + t + " = ");
                    switch (step.Kind)
                    {
                        case EK::Field: Text(t + "." + step.Text); break;
                        case EK::Index: Text(t + "["); EmitExpr(*step.B); Text("]"); break;
                        case EK::Method: Text(t + ":" + step.Text); EmitArgs(step.List); break;
                        default: Text(t); EmitArgs(step.List); break;
                    }
                }
                Text(" end)()");
            }

            void EmitTable(const Expr& e)
            {
                bool spread = false;
                for (const auto& it : e.Items)
                    spread = spread || it.K == TableItem::SpreadItem;
                if (!spread)
                {
                    Text("{");
                    for (size_t i = 0; i < e.Items.size(); ++i)
                    {
                        const TableItem& it = e.Items[i];
                        if (i) Text(", ");
                        if (it.K == TableItem::Named) Text(it.Name + " = ");
                        else if (it.K == TableItem::Keyed) { Text("["); EmitExpr(*it.Key); Text("] = "); }
                        EmitExpr(*it.Value);
                    }
                    Text("}");
                    return;
                }
                // {...a, x, k = v}: sırayla kur (dizi kısmı eklenir, anahtarlar atanır)
                const std::string t = Temp("tb");
                Text("(function() local " + t + " = {}");
                for (const auto& it : e.Items)
                {
                    switch (it.K)
                    {
                        case TableItem::SpreadItem: Text("; __luaq.spreadinto(" + t + ", "); EmitExpr(*it.Value); Text(")"); break;
                        case TableItem::Positional: Text("; " + t + "[#" + t + " + 1] = "); EmitExpr(*it.Value); break;
                        case TableItem::Named: Text("; " + t + "." + it.Name + " = "); EmitExpr(*it.Value); break;
                        case TableItem::Keyed: Text("; " + t + "["); EmitExpr(*it.Key); Text("] = "); EmitExpr(*it.Value); break;
                    }
                }
                Text("; return " + t + " end)()");
            }

            void EmitExpr(const Expr& e)
            {
                switch (e.Kind)
                {
                    case EK::Nil: Text("nil"); return;
                    case EK::True: Text("true"); return;
                    case EK::False: Text("false"); return;
                    case EK::Number:
                    case EK::String: Text(e.Text); return;
                    case EK::Vararg: Text("..."); return;
                    case EK::Name: Text(e.Text); return;
                    case EK::Super:
                        Fail(e.Line, e.Col, "'super' must be followed by .method or :method(...)");
                    case EK::Paren: Text("("); EmitExpr(*e.A); Text(")"); return;
                    case EK::Function:
                        Text("function");
                        EmitParams(*e.Func);
                        EmitFunctionBody(*e.Func);
                        return;
                    case EK::Table: EmitTable(e); return;
                    case EK::Interp:
                    {
                        Text("`");
                        for (size_t i = 0; i < e.List.size(); ++i)
                        {
                            Text(e.Parts[i] + "{");
                            EmitExpr(*e.List[i]);
                            Text("}");
                        }
                        Text(e.Parts.back() + "`");
                        return;
                    }
                    case EK::IfElse:
                    {
                        Text("(if ");
                        for (size_t i = 0; i + 1 < e.List.size(); i += 2)
                        {
                            if (i) Text(" elseif ");
                            EmitExpr(*e.List[i]);
                            Text(" then ");
                            EmitExpr(*e.List[i + 1]);
                        }
                        Text(" else ");
                        EmitExpr(*e.List.back());
                        Text(")");
                        return;
                    }
                    case EK::Unary:
                        if (e.Text == "~")
                        {
                            Text("bit32.bnot(");
                            EmitExpr(*e.A);
                            Text(")");
                            return;
                        }
                        Text("(" + std::string(e.Text == "not" ? "not " : e.Text));
                        EmitExpr(*e.A);
                        Text(")");
                        return;
                    case EK::Binary:
                    {
                        static const std::unordered_map<std::string, std::string> kBit = {
                            { "&", "bit32.band" }, { "|", "bit32.bor" }, { "~", "bit32.bxor" }, { "<<", "bit32.lshift" }, { ">>", "bit32.rshift" },
                        };
                        if (auto it = kBit.find(e.Text); it != kBit.end())
                        {
                            Text(it->second + "(");
                            EmitExpr(*e.A);
                            Text(", ");
                            EmitExpr(*e.B);
                            Text(")");
                            return;
                        }
                        Text("(");
                        EmitExpr(*e.A);
                        Text(" " + e.Text + " ");
                        EmitExpr(*e.B);
                        Text(")");
                        return;
                    }
                    case EK::Coalesce:
                    {
                        if (IsPure(*e.A))
                        {
                            const std::string a = Render(*e.A);
                            Text("(if " + a + " == nil then ");
                            EmitExpr(*e.B);
                            Text(" else " + a + ")");
                            return;
                        }
                        const std::string v = Temp("v");
                        Text("(function(" + v + ") if " + v + " == nil then return ");
                        EmitExpr(*e.B);
                        Text(" end return " + v + " end)(");
                        EmitExpr(*e.A);
                        Text(")");
                        return;
                    }
                    case EK::Await:
                        if (!m_InAsync)
                            Fail(e.Line, e.Col, "'await' is only valid inside async functions");
                        Text("__luaq.await(");
                        EmitExpr(*e.A);
                        Text(")");
                        return;
                    case EK::Spread:
                        Fail(e.Line, e.Col, "'...expr' spread is only valid as the last call argument or inside a table");
                    case EK::Field:
                    case EK::Index:
                    case EK::Call:
                    case EK::Method:
                        if (HasOptional(e))
                        {
                            EmitOptionalChain(e);
                            return;
                        }
                        if (e.Kind == EK::Field)
                        {
                            if (e.A->Kind == EK::Super)
                                Text(SuperRef(e) + "." + e.Text);
                            else if (e.A->Kind == EK::Name && e.A->Text == "math" && (e.Text == "type" || e.Text == "tointeger" || e.Text == "ult"))
                                Text("__luaq.math_" + e.Text);   // Lua 5.4 matematik eklentileri
                            else
                            {
                                EmitExpr(*e.A);
                                Text("." + e.Text);
                            }
                        }
                        else if (e.Kind == EK::Index)
                        {
                            EmitExpr(*e.A);
                            Text("[");
                            EmitExpr(*e.B);
                            Text("]");
                        }
                        else if (e.Kind == EK::Method)
                            EmitMethodCall(e, false);
                        else
                        {
                            if (e.A->Kind == EK::Super)
                            {
                                // super(...) -> taban init
                                Text(SuperRef(e) + ".init(self");
                                for (const auto& a : e.List) { Text(", "); EmitExpr(*a); }
                                Text(")");
                                return;
                            }
                            EmitCall(e, false);
                        }
                        return;
                }
            }

            const EmitOptions& m_Options;
            std::string m_Out;
            int m_Line = 1;
            int m_TempCounter = 0;
            int m_FrameCounter = 0;
            bool m_LastTerminator = false;
            bool m_InAsync = false;
            std::vector<Scope> m_Scopes;
            std::vector<Frame> m_Frames;
            std::vector<std::string> m_ClassStack;
            std::vector<std::string> m_TopClasses;
        };
    }

    std::string Emit(const Chunk& chunk, const EmitOptions& options)
    {
        return Emitter(options).Run(chunk);
    }
}
