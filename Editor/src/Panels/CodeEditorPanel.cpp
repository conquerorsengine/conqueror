#include "CodeEditorPanel.h"
#include "Core/Logging/Log.h"
#include "Core/ImGui/ImGuiLayer.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Scripting/ConquerorScript/CQSLexer.h"
#include "Scripting/ConquerorScript/CQSParser.h"
#include "Scripting/NativeDocs.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptRuntime.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Conqueror::Editor
{
    namespace
    {
        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        bool IsIdentChar(char c)
        {
            return std::isalnum((unsigned char)c) || c == '_' || ((unsigned char)c & 0x80);
        }

        // ── Dil tanımları ──
        const TextEditor::LanguageDefinition& CQSLanguage()
        {
            static TextEditor::LanguageDefinition def = [] {
                TextEditor::LanguageDefinition d = TextEditor::LanguageDefinition::CPlusPlus();
                d.mName = "ConquerorScript";
                d.mKeywords.clear();
                for (auto& [k, t] : CQS::CQSLexer::Keywords())
                    d.mKeywords.insert(k);
                for (const char* t : { "int", "float", "bool", "string", "void", "double", "long", "override", "virtual", "readonly", "any" })
                    d.mKeywords.insert(t);
                d.mIdentifiers.clear();
                d.mPreprocIdentifiers.clear();
                const auto& docs = Scripting::GetNativeDocs();
                for (const auto& f : Scripting::NativeRegistry::Get().GetFunctions())
                {
                    TextEditor::Identifier id;
                    auto it = docs.find(f.Name);
                    id.mDeclaration = it != docs.end() ? std::string(it->second.Signature) + "\n" + it->second.Description
                                                       : "nativebridge." + f.Name + "(" + (f.Arity < 0 ? std::string("...") : std::to_string(f.Arity) + " args") + ")";
                    d.mIdentifiers.insert({ f.Name, id });
                }
                for (const char* b : { "List", "Map", "Dictionary", "Vector2", "Vector3", "Vector4", "Color", "len", "str", "typeof", "range", "random", "assert", "error", "print", "Exception", "ConquerorScript", "nativebridge" })
                {
                    TextEditor::Identifier id;
                    auto it = docs.find(b);
                    id.mDeclaration = it != docs.end() ? std::string(it->second.Signature) + "\n" + it->second.Description : std::string(b);
                    d.mIdentifiers.insert({ b, id });
                }
                for (const auto& [name, v] : Scripting::NativeRegistry::Get().GetConstants())
                {
                    TextEditor::Identifier id;
                    id.mDeclaration = name + " (constant table)";
                    d.mIdentifiers.insert({ name, id });
                }
                return d;
            }();
            return def;
        }

        TextEditor::LanguageDefinition MakeCLike(const char* name, std::initializer_list<const char*> keywords)
        {
            TextEditor::LanguageDefinition d = TextEditor::LanguageDefinition::CPlusPlus();
            d.mName = name;
            d.mKeywords.clear();
            for (const char* k : keywords) d.mKeywords.insert(k);
            d.mIdentifiers.clear();
            d.mPreprocIdentifiers.clear();
            return d;
        }

        const TextEditor::LanguageDefinition& CSharpLanguage()
        {
            static TextEditor::LanguageDefinition def = MakeCLike("C#", {
                "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue", "decimal",
                "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for",
                "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock", "long", "namespace", "new", "null", "object",
                "operator", "out", "override", "params", "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
                "sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
                "unsafe", "ushort", "using", "virtual", "void", "volatile", "while", "var", "async", "await", "get", "set", "value", "yield", "record" });
            return def;
        }

        const TextEditor::LanguageDefinition& JavaLanguage()
        {
            static TextEditor::LanguageDefinition def = MakeCLike("Java", {
                "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue", "default", "do", "double",
                "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if", "implements", "import", "instanceof", "int", "interface",
                "long", "native", "new", "package", "private", "protected", "public", "return", "short", "static", "strictfp", "super", "switch",
                "synchronized", "this", "throw", "throws", "transient", "try", "void", "volatile", "while", "true", "false", "null", "var", "record" });
            return def;
        }

        // LuaQ: Lua sözcükleri + Luau (continue, type, export) + LuaQ (class, extends, static, super, enum, const, match, case,
        // defer, async, await, goto)
        const TextEditor::LanguageDefinition& LuaQLanguage()
        {
            static TextEditor::LanguageDefinition def = [] {
                TextEditor::LanguageDefinition d = TextEditor::LanguageDefinition::Lua();
                d.mName = "LuaQ";
                for (const char* k : { "continue", "type", "export", "typeof", "class", "extends", "static", "super", "self", "enum", "const",
                                       "match", "case", "defer", "async", "await", "goto" })
                    d.mKeywords.insert(k);
                for (const char* f : { "isinstance", "spawn", "wait", "bit32", "buffer", "vector" })
                {
                    TextEditor::Identifier id;
                    id.mDeclaration = "LuaQ built-in";
                    d.mIdentifiers.insert(std::make_pair(std::string(f), id));
                }
                return d;
            }();
            return def;
        }

        const TextEditor::LanguageDefinition& PythonLanguage()
        {
            static TextEditor::LanguageDefinition def = [] {
                TextEditor::LanguageDefinition d = TextEditor::LanguageDefinition::Lua();
                d.mName = "Python";
                d.mKeywords.clear();
                for (const char* k : { "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
                                       "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
                                       "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "self", "match", "case" })
                    d.mKeywords.insert(k);
                d.mIdentifiers.clear();
                d.mSingleLineComment = "#";
                d.mCommentStart = "\"\"\"";
                d.mCommentEnd = "\"\"\"";
                return d;
            }();
            return def;
        }

        std::string LanguageForPath(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (ext == ".cqs") return "cqs";
            if (ext == ".cs") return "cs";
            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".inl") return "cpp";
            if (ext == ".luaq") return "luaq";
            if (ext == ".lua" || ext == ".luau") return "lua";
            if (ext == ".py") return "py";
            if (ext == ".java") return "java";
            if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".cqsh" || ext == ".cqraytrace") return "glsl";
            if (ext == ".hlsl") return "hlsl";
            return "text";
        }

        const char* LanguageDisplayName(const std::string& lang)
        {
            if (lang == "cqs") return "ConquerorScript";
            if (lang == "cs") return "C#";
            if (lang == "cpp") return "C/C++";
            if (lang == "lua") return "Luau";
            if (lang == "luaq") return "LuaQ";
            if (lang == "py") return "Python";
            if (lang == "java") return "Java";
            if (lang == "glsl") return "GLSL";
            if (lang == "hlsl") return "HLSL";
            return "Plain Text";
        }

        void ApplyLanguage(TextEditor& editor, const std::string& lang)
        {
            if (lang == "cqs") editor.SetLanguageDefinition(CQSLanguage());
            else if (lang == "cs") editor.SetLanguageDefinition(CSharpLanguage());
            else if (lang == "cpp") editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
            else if (lang == "lua") editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
            else if (lang == "luaq") editor.SetLanguageDefinition(LuaQLanguage());
            else if (lang == "py") editor.SetLanguageDefinition(PythonLanguage());
            else if (lang == "java") editor.SetLanguageDefinition(JavaLanguage());
            else if (lang == "glsl") editor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
            else if (lang == "hlsl") editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
            else editor.SetColorizerEnable(false);
        }

        // Görsel sütun <-> karakter indeksi (tab genişliği)
        int ColumnToIndex(const std::string& line, int column, int tabSize)
        {
            int col = 0;
            for (int i = 0; i < (int)line.size(); ++i)
            {
                if (col >= column) return i;
                if (line[i] == '\t') col = (col / tabSize + 1) * tabSize;
                else if (((unsigned char)line[i] & 0xC0) != 0x80) ++col;
            }
            return (int)line.size();
        }

        int IndexToColumn(const std::string& line, int index, int tabSize)
        {
            int col = 0;
            for (int i = 0; i < index && i < (int)line.size(); ++i)
            {
                if (line[i] == '\t') col = (col / tabSize + 1) * tabSize;
                else if (((unsigned char)line[i] & 0xC0) != 0x80) ++col;
            }
            return col;
        }

        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::string cur;
            for (char c : text)
            {
                if (c == '\n') { lines.push_back(cur); cur.clear(); }
                else if (c != '\r') cur += c;
            }
            lines.push_back(cur);
            return lines;
        }

        // Dosyadaki sembol bilgisi (tamamlama için)
        struct FileSymbols
        {
            std::unordered_set<std::string> Identifiers;
            struct ClassInfo
            {
                std::vector<std::string> Fields, Methods, Statics;
                bool IsEnum = false;
            };
            std::unordered_map<std::string, ClassInfo> Classes;
            std::vector<std::string> Functions;
        };

        void CollectSymbols(const CQS::ASTNodePtr& node, FileSymbols& out)
        {
            using namespace CQS;
            if (!node) return;
            switch (node->Type)
            {
                case NodeType::Program:
                    for (auto& s : static_cast<ProgramNode*>(node.get())->Statements) CollectSymbols(s, out);
                    break;
                case NodeType::NamespaceStmt:
                    for (auto& s : static_cast<NamespaceNode*>(node.get())->Body) CollectSymbols(s, out);
                    break;
                case NodeType::FuncDecl:
                    out.Functions.push_back(static_cast<FuncDeclNode*>(node.get())->Name);
                    break;
                case NodeType::EnumDecl:
                {
                    auto* e = static_cast<EnumDeclNode*>(node.get());
                    auto& info = out.Classes[e->Name];
                    info.IsEnum = true;
                    info.Statics = e->Values;
                    break;
                }
                case NodeType::ClassDecl:
                case NodeType::ScriptDecl:
                case NodeType::StructDecl:
                {
                    auto* c = static_cast<ClassDeclNode*>(node.get());
                    auto& info = out.Classes[c->Name];
                    for (auto& m : c->Members)
                    {
                        if (!m) continue;
                        if (m->Type == NodeType::VarDecl)
                        {
                            auto* v = static_cast<VarDeclNode*>(m.get());
                            (v->IsStatic ? info.Statics : info.Fields).push_back(v->Name);
                        }
                        else if (m->Type == NodeType::FuncDecl)
                        {
                            auto* f = static_cast<FuncDeclNode*>(m.get());
                            (f->IsStatic ? info.Statics : info.Methods).push_back(f->Name);
                        }
                    }
                    break;
                }
                default: break;
            }
        }

        FileSymbols AnalyzeFile(const std::string& text)
        {
            FileSymbols sym;
            CQS::CQSNoGCScope noGC;
            CQS::CQSLexer lexer;
            auto tokens = lexer.Tokenize(text);
            for (auto& t : tokens)
                if (t.Type == CQS::TokenType::Identifier && t.Lexeme.size() > 1)
                    sym.Identifiers.insert(t.Lexeme);
            CQS::CQSParser parser;
            CollectSymbols(parser.Parse(tokens), sym);
            return sym;
        }

        const std::vector<const char*>& BuiltinMemberNames()
        {
            static const std::vector<const char*> s_Names = {
                // List
                "Add", "AddRange", "Insert", "RemoveAt", "Remove", "RemoveAll", "Clear", "Contains", "IndexOf", "Count", "Pop", "Peek", "Shift",
                "Reverse", "Sort", "SortBy", "Join", "Slice", "Copy", "Map", "Filter", "Where", "Select", "ForEach", "Find", "FindIndex", "Any",
                "All", "First", "Last", "FirstOrDefault", "Reduce", "Sum", "Min", "Max", "Average", "Shuffle", "Distinct", "Fill", "Resize",
                "IsEmpty", "Swap", "Random", "Length",
                // Map
                "Keys", "Values", "Entries", "ContainsKey", "ContainsValue", "Get", "Set", "TryAdd", "Merge",
                // String
                "ToUpper", "ToLower", "Trim", "TrimStart", "TrimEnd", "StartsWith", "EndsWith", "LastIndexOf", "Substring", "Replace", "Split",
                "Repeat", "PadLeft", "PadRight", "CharAt", "CharCodeAt", "ToInt", "ToFloat", "Lines", "Format",
                // Her değer / vektör
                "ToString", "GetType", "Equals", "x", "y", "z", "w", "r", "g", "b", "a",
            };
            return s_Names;
        }

        struct Snippet { const char* Label; const char* Text; const char* Detail; };
        const std::vector<Snippet>& Snippets()
        {
            static const std::vector<Snippet> s_Snippets = {
                { "Update", "func Update(dt)\n{\n    \n}", "Called every frame" },
                { "Start", "func Start()\n{\n    \n}", "Called once before the first Update" },
                { "Awake", "func Awake()\n{\n    \n}", "Called when the script instance is created" },
                { "LateUpdate", "func LateUpdate(dt)\n{\n    \n}", "Called after all Updates" },
                { "OnDestroy", "func OnDestroy()\n{\n    \n}", "Called when play stops / entity is destroyed" },
                { "script", "script NewScript : ConquerorScript\n{\n    public float speed = 1.0;\n\n    func Update(dt)\n    {\n    }\n}", "Script class" },
                { "class", "class NewClass\n{\n    func init()\n    {\n    }\n}", "Class" },
                { "for", "for (var i = 0; i < count; i++)\n{\n    \n}", "for loop" },
                { "foreach", "foreach (var item in list)\n{\n    \n}", "foreach loop" },
                { "if", "if (condition)\n{\n    \n}", "if statement" },
                { "while", "while (condition)\n{\n    \n}", "while loop" },
                { "try", "try\n{\n    \n}\ncatch (e)\n{\n    LogError(e.message);\n}", "try / catch" },
                { "switch", "switch (value)\n{\n    case 0:\n        break;\n    default:\n        break;\n}", "switch statement" },
            };
            return s_Snippets;
        }
    }

    // ════════════════════════════════════════
    //  Lifetime / documents
    // ════════════════════════════════════════
    CodeEditorPanel::CodeEditorPanel() = default;
    CodeEditorPanel::~CodeEditorPanel() = default;

    CodeEditorPanel::Document* CodeEditorPanel::Active()
    {
        return (m_ActiveIndex >= 0 && m_ActiveIndex < (int)m_Documents.size()) ? m_Documents[(size_t)m_ActiveIndex].get() : nullptr;
    }

    const CodeEditorPanel::Document* CodeEditorPanel::Active() const
    {
        return (m_ActiveIndex >= 0 && m_ActiveIndex < (int)m_Documents.size()) ? m_Documents[(size_t)m_ActiveIndex].get() : nullptr;
    }

    void CodeEditorPanel::LoadDocument(Document& doc)
    {
        std::ifstream file(doc.Path, std::ios::binary);
        std::stringstream ss;
        ss << file.rdbuf();
        std::string text = ss.str();
        doc.UsesCRLF = text.find("\r\n") != std::string::npos;
        auto cursor = doc.Editor->GetCursorPosition();
        doc.Editor->SetText(text);
        doc.Editor->SetCursorPosition(cursor);
        doc.Dirty = false;
        doc.ChangedOnDisk = false;
        std::error_code ec;
        doc.DiskTime = std::filesystem::last_write_time(doc.Path, ec);
        doc.NeedsCheck = true;
        doc.LastEdit = 0.0;
    }

    void CodeEditorPanel::OpenFile(const std::filesystem::path& path, int line)
    {
        if (!std::filesystem::exists(path))
            return;
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        for (size_t i = 0; i < m_Documents.size(); ++i)
        {
            std::error_code ec2;
            if (std::filesystem::weakly_canonical(m_Documents[i]->Path, ec2) == canonical)
            {
                m_FocusIndex = (int)i;
                m_Documents[i]->PendingGotoLine = line;
                m_IsOpen = true;
                return;
            }
        }
        auto doc = std::make_unique<Document>();
        doc->Path = path;
        doc->Language = LanguageForPath(path);
        doc->Editor = std::make_unique<TextEditor>();
        doc->Editor->SetPalette(TextEditor::GetDarkPalette());
        doc->Editor->SetTabSize(4);
        doc->Editor->SetShowWhitespaces(m_ShowWhitespace);
        ApplyLanguage(*doc->Editor, doc->Language);
        LoadDocument(*doc);
        doc->PendingGotoLine = line;
        m_Documents.push_back(std::move(doc));
        m_FocusIndex = (int)m_Documents.size() - 1;
        m_IsOpen = true;
        CQ_CORE_INFO("CodeEditor: Opened {0}", path.string());
    }

    void CodeEditorPanel::SaveFile()
    {
        Document* doc = Active();
        if (!doc || doc->Path.empty())
            return;
        std::string text = doc->Editor->GetText();
        if (doc->UsesCRLF)
        {
            std::string crlf;
            crlf.reserve(text.size() + text.size() / 20);
            for (char c : text)
            {
                if (c == '\n') crlf += '\r';
                crlf += c;
            }
            text = std::move(crlf);
        }
        std::ofstream file(doc->Path, std::ios::binary);
        if (!file.is_open())
        {
            CQ_CORE_ERROR("CodeEditor: cannot write {0}", doc->Path.string());
            return;
        }
        file << text;
        file.close();
        doc->Dirty = false;
        doc->ChangedOnDisk = false;
        std::error_code ec;
        doc->DiskTime = std::filesystem::last_write_time(doc->Path, ec);
        doc->NeedsCheck = true;
        doc->LastEdit = 0.0;
        CQ_CORE_INFO("CodeEditor: Saved {0}", doc->Path.string());
        if (OnFileSaved)
            OnFileSaved(doc->Path);
    }

    void CodeEditorPanel::SaveAll()
    {
        int saved = m_ActiveIndex;
        for (int i = 0; i < (int)m_Documents.size(); ++i)
        {
            if (!m_Documents[(size_t)i]->Dirty) continue;
            m_ActiveIndex = i;
            SaveFile();
        }
        m_ActiveIndex = saved;
    }

    bool CodeEditorPanel::HasUnsavedChanges() const
    {
        for (auto& d : m_Documents)
            if (d->Dirty) return true;
        return false;
    }

    void CodeEditorPanel::CloseDocument(int index)
    {
        if (index < 0 || index >= (int)m_Documents.size())
            return;
        m_Documents.erase(m_Documents.begin() + index);
        if (m_ActiveIndex >= (int)m_Documents.size())
            m_ActiveIndex = (int)m_Documents.size() - 1;
        m_CompletionOpen = false;
    }

    void CodeEditorPanel::CheckExternalChanges()
    {
        double now = Now();
        if (now - m_LastExternalCheck < 1.0)
            return;
        m_LastExternalCheck = now;
        for (auto& doc : m_Documents)
        {
            std::error_code ec;
            auto t = std::filesystem::last_write_time(doc->Path, ec);
            if (ec || t == doc->DiskTime)
                continue;
            if (!doc->Dirty)
                LoadDocument(*doc);
            else
                doc->ChangedOnDisk = true;
        }
    }

    // ════════════════════════════════════════
    //  Diagnostics
    // ════════════════════════════════════════
    void CodeEditorPanel::RunDiagnostics(Document& doc)
    {
        doc.NeedsCheck = false;
        doc.Diagnostics.clear();
        TextEditor::ErrorMarkers markers;
        auto* runtime = Scripting::ScriptRuntimes::ForPath(doc.Path.string());
        if (runtime && runtime->IsAvailable())
        {
            std::vector<std::string> errors, warnings;
            runtime->CheckSyntax(doc.Editor->GetText(), doc.Path.filename().string(), errors, &warnings);
            const size_t errorCount = errors.size();
            errors.insert(errors.end(), warnings.begin(), warnings.end());
            for (size_t ei = 0; ei < errors.size(); ++ei)
            {
                const std::string& e = errors[ei];
                Diagnostic d;
                d.IsWarning = ei >= errorCount;
                size_t p = e.find("Line ");
                if (p != std::string::npos)
                {
                    std::sscanf(e.c_str() + p, "Line %d:%d", &d.Line, &d.Column);
                    size_t colon = e.find(": ", p);
                    d.Message = colon != std::string::npos ? e.substr(colon + 2) : e;
                    // "at 'x': mesaj" biçimi
                    size_t at = d.Message.find("': ");
                    if (d.Message.rfind("at '", 0) == 0 && at != std::string::npos)
                        d.Message = d.Message.substr(at + 3);
                    else if (d.Message.rfind("after '", 0) == 0 && at != std::string::npos)
                        d.Message = d.Message.substr(at + 3);
                }
                else
                    d.Message = e;
                doc.Diagnostics.push_back(d);
                if (d.Line > 0)
                {
                    std::string text = (d.IsWarning ? "Warning: " : "Error: ") + d.Message;
                    auto it = markers.find(d.Line);
                    if (it == markers.end()) markers[d.Line] = text;
                    else it->second += "\n" + text;
                }
            }
        }
        doc.Editor->SetErrorMarkers(markers);
        // Arka plan denetimi sürüyorsa kısa süre sonra tekrar sor
        if (runtime && runtime->HasPendingSyntaxCheck())
        {
            doc.NeedsCheck = true;
            doc.LastEdit = Now() - 0.2;
        }
    }

    int CodeEditorPanel::GetDiagnosticCount() const
    {
        const Document* doc = Active();
        return doc ? (int)doc->Diagnostics.size() : 0;
    }

    std::string CodeEditorPanel::GetActiveText() const
    {
        const Document* doc = Active();
        return doc ? doc->Editor->GetText() : std::string();
    }

    // ════════════════════════════════════════
    //  Completion
    // ════════════════════════════════════════
    void CodeEditorPanel::BuildCompletions(Document& doc, const std::string& prefix, char before, const std::string& objectName)
    {
        m_Completions.clear();
        std::unordered_set<std::string> seen;
        auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        std::string lp = lower(prefix);
        auto add = [&](const std::string& label, const std::string& insert, const std::string& detail, const std::string& docText, int kind) {
            if (label.empty() || seen.count(label + "#" + std::to_string(kind)))
                return;
            int score = 0;
            if (prefix.empty()) score = 1;
            else if (label.rfind(prefix, 0) == 0) score = 4;
            else if (lower(label).rfind(lp, 0) == 0) score = 3;
            else if (lower(label).find(lp) != std::string::npos) score = 1;
            if (score == 0)
                return;
            if (label == prefix && kind != 4)
                return; // zaten tam yazılmış
            seen.insert(label + "#" + std::to_string(kind));
            Completion c;
            c.Label = label;
            c.Insert = insert;
            c.Detail = detail;
            c.Doc = docText;
            c.Kind = kind;
            c.Score = score;
            m_Completions.push_back(std::move(c));
        };
        const auto& docs = Scripting::GetNativeDocs();
        auto nativeEntry = [&](const Scripting::NativeFunctionInfo& f, int kind) {
            auto it = docs.find(f.Name);
            std::string sig = it != docs.end() ? it->second.Signature : f.Name + "(" + (f.Arity < 0 ? std::string("...") : std::to_string(f.Arity) + " args") + ")";
            std::string desc = it != docs.end() ? it->second.Description : "";
            add(f.Name, f.Name + (f.Arity == 0 ? "()" : "("), sig, desc, kind);
        };

        if (doc.Language != "cqs")
        {
            // Diğer diller: anahtar kelimeler + dosyadaki kelimeler
            for (auto& k : doc.Editor->GetLanguageDefinition().mKeywords)
                add(k, k, "keyword", "", 0);
            std::string text = doc.Editor->GetText();
            std::string word;
            for (char c : text + " ")
            {
                if (IsIdentChar(c)) word += c;
                else
                {
                    if (word.size() > 2 && !std::isdigit((unsigned char)word[0])) add(word, word, "", "", 2);
                    word.clear();
                }
            }
        }
        else
        {
            FileSymbols sym = AnalyzeFile(doc.Editor->GetText());
            if (before == '.')
            {
                const auto& registry = Scripting::NativeRegistry::Get();
                bool handled = false;
                for (const auto& [name, value] : registry.GetConstants())
                {
                    if (name == objectName && value.IsMap())
                    {
                        std::vector<std::string> keys;
                        for (auto& [k, v] : value.AsMap()->Entries) keys.push_back(k);
                        std::sort(keys.begin(), keys.end());
                        for (auto& k : keys)
                            add(k, k, objectName + "." + k, "", 5);
                        handled = true;
                    }
                }
                if (objectName == "nativebridge" || objectName == "NativeBridge" || objectName == "Conqueror")
                {
                    for (const auto& f : registry.GetFunctions()) nativeEntry(f, 1);
                    handled = true;
                }
                auto cls = sym.Classes.find(objectName);
                if (cls != sym.Classes.end())
                {
                    for (auto& s : cls->second.Statics) add(s, s, objectName + "." + s, cls->second.IsEnum ? "enum value" : "static", 5);
                    handled = true;
                }
                if (!handled)
                {
                    // Bilinmeyen nesne: dosyadaki sınıf üyeleri + yerleşik metotlar
                    for (auto& [cname, info] : sym.Classes)
                    {
                        for (auto& f : info.Fields) add(f, f, cname + "." + f, "field", 2);
                        for (auto& m : info.Methods) add(m, m + "(", cname + "." + m + "()", "method", 5);
                    }
                    for (const char* m : BuiltinMemberNames())
                        add(m, m, "built-in member", "", 5);
                }
            }
            else
            {
                for (auto& s : Snippets())
                    add(s.Label, s.Text, s.Detail, "snippet", 4);
                for (auto& [k, t] : CQS::CQSLexer::Keywords())
                    add(k, k, "keyword", "", 0);
                for (const char* t : { "int", "float", "bool", "string", "void" })
                    add(t, t, "type", "", 0);
                for (auto& [cname, info] : sym.Classes)
                {
                    add(cname, cname, info.IsEnum ? "enum" : "class", "", 3);
                    // Sınıf içindeyken alanlar ve metotlar çıplak kullanılabilir
                    for (auto& f : info.Fields) add(f, f, cname + "." + f, "field", 2);
                    for (auto& m : info.Methods) add(m, m + "(", cname + "." + m + "()", "method", 1);
                }
                for (auto& f : sym.Functions)
                    add(f, f + "(", f + "()", "function", 1);
                for (auto& id : sym.Identifiers)
                    add(id, id, "", "", 2);
                for (const auto& f : Scripting::NativeRegistry::Get().GetFunctions())
                    nativeEntry(f, 1);
                for (const auto& [name, value] : Scripting::NativeRegistry::Get().GetConstants())
                    add(name, name, "constant table", "", 3);
                for (const char* b : { "List", "Map", "Dictionary", "Vector2", "Vector3", "Vector4", "Color", "len", "str", "typeof", "range", "random", "assert", "print", "Exception", "nativebridge" })
                {
                    auto it = docs.find(b);
                    add(b, std::string(b) + (std::strcmp(b, "nativebridge") == 0 || std::strcmp(b, "Exception") == 0 ? "" : "("),
                        it != docs.end() ? it->second.Signature : b, it != docs.end() ? it->second.Description : "", 1);
                }
            }
        }

        std::stable_sort(m_Completions.begin(), m_Completions.end(), [](const Completion& a, const Completion& b) {
            if (a.Score != b.Score) return a.Score > b.Score;
            // Üyeler ve dosya sembolleri önce, sonra fonksiyonlar, anahtar kelimeler
            static const int order[] = { 3, 2, 1, 2, 4, 0 };
            int oa = order[std::clamp(a.Kind, 0, 5)], ob = order[std::clamp(b.Kind, 0, 5)];
            if (oa != ob) return oa < ob;
            if (a.Label.size() != b.Label.size()) return a.Label.size() < b.Label.size();
            return a.Label < b.Label;
        });
        if (m_Completions.size() > 80)
            m_Completions.resize(80);
    }

    void CodeEditorPanel::UpdateCompletion(Document& doc, bool force)
    {
        TextEditor& ed = *doc.Editor;
        std::string word = ed.GetWordBeforeCursor();
        char before = ed.GetCharBeforeWord();
        std::string line = ed.GetCurrentLineText();
        auto pos = ed.GetCursorPosition();
        int idx = ColumnToIndex(line, pos.mColumn, ed.GetTabSize());

        // Yorum ya da metin içinde öneri gösterme
        std::string head = line.substr(0, (size_t)std::min<int>(idx, (int)line.size()));
        size_t comment = head.find("//");
        int quotes = 0;
        for (size_t i = 0; i < head.size(); ++i)
            if (head[i] == '"' && (i == 0 || head[i - 1] != '\\')) ++quotes;
        if (comment != std::string::npos || (quotes % 2) == 1)
        {
            m_CompletionOpen = false;
            return;
        }
        // Sayı yazılıyor
        if (!word.empty() && std::isdigit((unsigned char)word[0]))
        {
            m_CompletionOpen = false;
            return;
        }
        if (!force && word.empty() && before != '.')
        {
            m_CompletionOpen = false;
            return;
        }
        std::string objectName;
        if (before == '.')
        {
            int end = idx - (int)word.size() - 1; // '.' konumu
            int s = end;
            while (s > 0 && IsIdentChar(line[(size_t)s - 1])) --s;
            if (end > s) objectName = line.substr((size_t)s, (size_t)(end - s));
        }
        m_CompletionPrefix = word;
        BuildCompletions(doc, word, before, objectName);
        m_CompletionOpen = !m_Completions.empty();
        m_CompletionSelected = 0;
    }

    void CodeEditorPanel::Accept(Document& doc, const Completion& c)
    {
        TextEditor& ed = *doc.Editor;
        std::string insert = c.Insert;
        if (insert.find('\n') != std::string::npos)
        {
            // Kod parçası: satır girintisini sonraki satırlara uygula
            std::string line = ed.GetCurrentLineText();
            std::string indent;
            for (char ch : line) { if (ch == ' ' || ch == '\t') indent += ch; else break; }
            std::string out;
            for (char ch : insert)
            {
                out += ch;
                if (ch == '\n') out += indent;
            }
            insert = out;
        }
        ed.ReplaceWordBeforeCursor(insert);
        // "Fonksiyon(" : kapanış parantezi ekle ve imleci içeri al
        if (!insert.empty() && insert.back() == '(')
        {
            ed.InsertText(")");
            ed.MoveLeft(1);
        }
        doc.Dirty = true;
        doc.NeedsCheck = true;
        doc.LastEdit = Now();
        m_CompletionOpen = false;
        m_SuppressCompletion = true;
    }

    void CodeEditorPanel::RenderCompletionPopup(Document& doc)
    {
        if (!m_CompletionOpen || m_Completions.empty() || !doc.Editor->IsCursorOnScreen())
            return;
        TextEditor& ed = *doc.Editor;
        ImVec2 pos = ed.GetCursorScreenPos();
        pos.y += ed.GetLineHeight() + 2.0f;
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(560, 300));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.16f, 0.98f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_Tooltip;
        if (ImGui::Begin("##cqcompletion", nullptr, flags))
        {
            static const char* kindIcons[] = { ICON_FA_KEY, ICON_FA_CUBE, ICON_FA_TAG, ICON_FA_SHAPES, ICON_FA_PUZZLE_PIECE, ICON_FA_CIRCLE_DOT };
            static const ImVec4 kindColors[] = { ImVec4(0.80f, 0.55f, 1.0f, 1), ImVec4(0.95f, 0.80f, 0.35f, 1), ImVec4(0.45f, 0.75f, 1.0f, 1),
                                                 ImVec4(0.35f, 0.85f, 0.65f, 1), ImVec4(0.75f, 0.75f, 0.80f, 1), ImVec4(0.55f, 0.85f, 1.0f, 1) };
            ImGui::BeginChild("##list", ImVec2(300, std::min(240.0f, (float)m_Completions.size() * ImGui::GetTextLineHeightWithSpacing() + 8.0f)), false);
            for (int i = 0; i < (int)m_Completions.size(); ++i)
            {
                const Completion& c = m_Completions[(size_t)i];
                bool selected = i == m_CompletionSelected;
                ImGui::PushID(i);
                ImGui::TextColored(kindColors[std::clamp(c.Kind, 0, 5)], "%s", kindIcons[std::clamp(c.Kind, 0, 5)]);
                ImGui::SameLine(22.0f);
                if (ImGui::Selectable(c.Label.c_str(), selected, ImGuiSelectableFlags_DontClosePopups))
                {
                    m_CompletionSelected = i;
                    Accept(doc, c);
                    ImGui::PopID();
                    break;
                }
                if (selected && ImGui::IsWindowAppearing())
                    ImGui::SetScrollHereY();
                if (selected && m_ScrollCompletion)
                {
                    ImGui::SetScrollHereY();
                    m_ScrollCompletion = false;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (m_CompletionOpen && m_CompletionSelected < (int)m_Completions.size())
            {
                const Completion& c = m_Completions[(size_t)m_CompletionSelected];
                if (!c.Detail.empty() || !c.Doc.empty())
                {
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
                    if (!c.Detail.empty())
                        ImGui::TextColored(UI::Colors::Accent, "%s", c.Kind == 4 ? c.Label.c_str() : c.Detail.c_str());
                    if (c.Kind == 4)
                        ImGui::TextDisabled("%s", c.Detail.c_str());
                    else if (!c.Doc.empty())
                        ImGui::TextWrapped("%s", c.Doc.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ════════════════════════════════════════
    //  Find / replace
    // ════════════════════════════════════════
    bool CodeEditorPanel::FindNext(Document& doc, bool backwards, bool wrap)
    {
        std::string needle = m_FindBuffer;
        if (needle.empty())
            return false;
        TextEditor& ed = *doc.Editor;
        std::vector<std::string> lines = SplitLines(ed.GetText());
        auto lower = [&](std::string s) { if (!m_FindCase) for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        std::string n = lower(needle);
        // Doğrusal metin + satır başlangıçları
        std::string flat;
        std::vector<size_t> starts;
        for (auto& l : lines) { starts.push_back(flat.size()); flat += lower(l); flat += '\n'; }
        auto pos = ed.GetCursorPosition();
        int line = std::clamp(pos.mLine, 0, (int)lines.size() - 1);
        size_t cursor = starts[(size_t)line] + (size_t)ColumnToIndex(lines[(size_t)line], pos.mColumn, ed.GetTabSize());
        size_t found;
        if (!backwards)
        {
            found = flat.find(n, cursor);
            if (found == std::string::npos && wrap) found = flat.find(n);   // başa sar
        }
        else
        {
            // Seçili eşleşmenin başından önce ara
            size_t from = cursor >= n.size() + 1 ? cursor - n.size() - 1 : std::string::npos;
            found = from == std::string::npos ? std::string::npos : flat.rfind(n, from);
            if (found == std::string::npos && wrap) found = flat.rfind(n);
        }
        // Eşleşme sayısı
        int count = 0;
        for (size_t p = flat.find(n); p != std::string::npos; p = flat.find(n, p + 1)) ++count;
        if (found == std::string::npos)
        {
            m_FindStatus = "No results";
            return false;
        }
        size_t l = (size_t)(std::upper_bound(starts.begin(), starts.end(), found) - starts.begin() - 1);
        int startIdx = (int)(found - starts[l]);
        TextEditor::Coordinates a((int)l, IndexToColumn(lines[l], startIdx, ed.GetTabSize()));
        TextEditor::Coordinates b((int)l, IndexToColumn(lines[l], startIdx + (int)n.size(), ed.GetTabSize()));
        ed.SetCursorPosition(b);
        ed.SetSelection(a, b);
        m_FindStatus = std::to_string(count) + (count == 1 ? " result" : " results");
        return true;
    }

    void CodeEditorPanel::ReplaceAll(Document& doc)
    {
        if (m_FindBuffer[0] == 0)
            return;
        TextEditor& ed = *doc.Editor;
        // Baştan sona tek geçiş (başa sarmadan): değiştirilen metin aranan metni içerse bile sonlanır.
        // Her değiştirme ayrı geri alma adımıdır.
        ed.SetSelection(TextEditor::Coordinates(0, 0), TextEditor::Coordinates(0, 0));
        ed.SetCursorPosition(TextEditor::Coordinates(0, 0));
        int replaced = 0;
        while (replaced < 100000 && FindNext(doc, false, false))
        {
            ed.ReplaceSelection(m_ReplaceBuffer);
            ++replaced;
        }
        m_FindStatus = "Replaced " + std::to_string(replaced);
        if (replaced > 0)
        {
            doc.Dirty = true;
            doc.NeedsCheck = true;
            doc.LastEdit = Now();
        }
    }

    void CodeEditorPanel::RenderFindBar(Document& doc)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::SetNextItemWidth(220.0f);
        if (m_FocusFindInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusFindInput = false;
        }
        bool enter = ImGui::InputTextWithHint("##find", ICON_FA_MAGNIFYING_GLASS "  Find", m_FindBuffer, sizeof(m_FindBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter)
        {
            FindNext(doc, ImGui::GetIO().KeyShift);
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_UP "##prev")) FindNext(doc, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Previous (Shift+F3)");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_DOWN "##next")) FindNext(doc, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Next (F3)");
        ImGui::SameLine();
        ImGui::Checkbox("Aa", &m_FindCase);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Match case");
        if (m_ShowReplace)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##replace", "Replace", m_ReplaceBuffer, sizeof(m_ReplaceBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Replace"))
            {
                std::string sel = doc.Editor->GetSelectedText();
                std::string a = sel, b = m_FindBuffer;
                if (!m_FindCase)
                {
                    for (char& c : a) c = (char)std::tolower((unsigned char)c);
                    for (char& c : b) c = (char)std::tolower((unsigned char)c);
                }
                if (!b.empty() && a == b)
                {
                    doc.Editor->ReplaceSelection(m_ReplaceBuffer);
                    doc.Dirty = true;
                    doc.NeedsCheck = true;
                    doc.LastEdit = Now();
                }
                FindNext(doc, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("All"))
                ReplaceAll(doc);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_FindStatus.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::Button(ICON_FA_XMARK "##closefind", ImVec2(ImGui::GetFrameHeight(), 0)))
        {
            m_ShowFind = false;
            m_ShowReplace = false;
        }
        ImGui::PopStyleVar();
    }

    // ════════════════════════════════════════
    //  UI
    // ════════════════════════════════════════
    void CodeEditorPanel::RenderToolbar(Document* doc)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        auto button = [&](const char* icon, const char* tip, bool enabled) {
            ImGui::BeginDisabled(!enabled);
            bool pressed = ImGui::Button(icon);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
            return pressed;
        };
        if (button(ICON_FA_FLOPPY_DISK, "Save (Ctrl+S)", doc && doc->Dirty)) SaveFile();
        if (button(ICON_FA_FILE_ARROW_DOWN, "Save All (Ctrl+Shift+S)", HasUnsavedChanges())) SaveAll();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (button(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)", doc && doc->Editor->CanUndo()))
        {
            doc->Editor->Undo();
            doc->Dirty = true;
            doc->NeedsCheck = true;
        }
        if (button(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)", doc && doc->Editor->CanRedo()))
        {
            doc->Editor->Redo();
            doc->Dirty = true;
            doc->NeedsCheck = true;
        }
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (button(ICON_FA_MAGNIFYING_GLASS, "Find (Ctrl+F)", doc != nullptr)) { m_ShowFind = true; m_ShowReplace = false; m_FocusFindInput = true; }
        if (button(ICON_FA_RIGHT_LEFT, "Replace (Ctrl+H)", doc != nullptr)) { m_ShowFind = true; m_ShowReplace = true; m_FocusFindInput = true; }
        if (button(ICON_FA_ARROW_TURN_DOWN, "Go to Line (Ctrl+G)", doc != nullptr)) m_ShowGoto = true;
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (button(ICON_FA_MAGNIFYING_GLASS_MINUS, "Zoom Out (Ctrl+-)", true)) m_FontScale = std::max(0.6f, m_FontScale - 0.1f);
        if (button(ICON_FA_MAGNIFYING_GLASS_PLUS, "Zoom In (Ctrl+=)", true)) m_FontScale = std::min(2.5f, m_FontScale + 0.1f);
        if (button(m_ShowWhitespace ? ICON_FA_EYE : ICON_FA_EYE_SLASH, "Show Whitespace", doc != nullptr))
        {
            m_ShowWhitespace = !m_ShowWhitespace;
            for (auto& d : m_Documents) d->Editor->SetShowWhitespaces(m_ShowWhitespace);
        }
        if (doc && Scripting::ScriptRuntimes::IsScriptFile(doc->Path.string()))
        {
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (button(ICON_FA_CIRCLE_CHECK, "Check Script (compile without running)", true))
                RunDiagnostics(*doc);
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::NewLine();
    }

    void CodeEditorPanel::RenderProblems(Document& doc)
    {
        if (doc.Diagnostics.empty() || !m_ShowProblems)
            return;
        float h = std::min(120.0f, (float)doc.Diagnostics.size() * ImGui::GetTextLineHeightWithSpacing() + 10.0f);
        ImGui::BeginChild("##problems", ImVec2(0, h), true);
        for (size_t i = 0; i < doc.Diagnostics.size(); ++i)
        {
            const Diagnostic& d = doc.Diagnostics[i];
            ImGui::PushID((int)i);
            if (d.IsWarning) ImGui::TextColored(UI::Colors::Warning, ICON_FA_TRIANGLE_EXCLAMATION);
            else ImGui::TextColored(UI::Colors::Error, ICON_FA_CIRCLE_XMARK);
            ImGui::SameLine();
            std::string label = (d.Line > 0 ? "Ln " + std::to_string(d.Line) + ":  " : std::string()) + d.Message;
            if (ImGui::Selectable(label.c_str()) && d.Line > 0)
            {
                doc.Editor->ScrollToLine(d.Line - 1);
                m_FocusEditor = true;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void CodeEditorPanel::RenderStatusBar(Document* doc)
    {
        ImGui::Separator();
        if (!doc)
            return;
        auto pos = doc->Editor->GetCursorPosition();
        ImGui::TextDisabled("Ln %d, Col %d", pos.mLine + 1, pos.mColumn + 1);
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("%s", LanguageDisplayName(doc->Language));
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("%s", doc->UsesCRLF ? "CRLF" : "LF");
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("%d lines", doc->Editor->GetTotalLines());
        if (Scripting::ScriptRuntimes::IsScriptFile(doc->Path.string()))
        {
            ImGui::SameLine(0, 18);
            if (doc->Diagnostics.empty())
                ImGui::TextColored(UI::Colors::Success, ICON_FA_CIRCLE_CHECK " No problems");
            else
            {
                int errs = 0, warns = 0;
                for (auto& d : doc->Diagnostics) (d.IsWarning ? warns : errs)++;
                if (errs > 0)
                    ImGui::TextColored(UI::Colors::Error, ICON_FA_CIRCLE_XMARK " %d error%s", errs, errs == 1 ? "" : "s");
                if (errs > 0 && warns > 0)
                    ImGui::SameLine();
                if (warns > 0)
                    ImGui::TextColored(UI::Colors::Warning, ICON_FA_TRIANGLE_EXCLAMATION " %d warning%s", warns, warns == 1 ? "" : "s");
                if (ImGui::IsItemClicked())
                    m_ShowProblems = !m_ShowProblems;
            }
        }
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("%d%%", (int)std::lround(m_FontScale * 100.0f));
    }

    void CodeEditorPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;
        CheckExternalChanges();

        ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
        Document* active = Active();
        std::string title = std::string(ICON_FA_CODE "  Code Editor") + (active ? " - " + active->Path.filename().string() + (active->Dirty ? " *" : "") : "") + "###CodeEditor";
        if (!ImGui::Begin(title.c_str(), &m_IsOpen))
        {
            ImGui::End();
            return;
        }
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl || io.KeySuper;

        // Kısayollar
        if (panelFocused && active)
        {
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) SaveAll(); else SaveFile(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) { m_ShowFind = true; m_ShowReplace = false; m_FocusFindInput = true; }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) { m_ShowFind = true; m_ShowReplace = true; m_FocusFindInput = true; }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) m_ShowGoto = true;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) m_ConfirmClose = m_ActiveIndex;
            if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))) m_FontScale = std::min(2.5f, m_FontScale + 0.1f);
            if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))) m_FontScale = std::max(0.6f, m_FontScale - 0.1f);
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) m_FontScale = 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) FindNext(*active, io.KeyShift);
            if (ctrl && io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
                m_FontScale = std::clamp(m_FontScale + io.MouseWheel * 0.1f, 0.6f, 2.5f);
        }

        RenderToolbar(active);

        if (m_Documents.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No file open.");
            ImGui::TextDisabled("Double-click a script in the Content Browser, or use the Inspector's edit button.");
            RenderStatusBar(nullptr);
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##docs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for (int i = 0; i < (int)m_Documents.size(); ++i)
            {
                Document& doc = *m_Documents[(size_t)i];
                bool open = true;
                ImGuiTabItemFlags flags = doc.Dirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
                if (m_FocusIndex == i)
                {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    m_FocusIndex = -1;
                }
                std::string label = doc.Path.filename().string() + "###doc" + std::to_string((uintptr_t)&doc);
                if (ImGui::BeginTabItem(label.c_str(), &open, flags))
                {
                    if (m_ActiveIndex != i)
                        m_CompletionOpen = false;
                    m_ActiveIndex = i;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", doc.Path.string().c_str());
                    ImGui::EndTabItem();
                }
                if (!open)
                {
                    if (doc.Dirty) m_ConfirmClose = i;
                    else
                    {
                        CloseDocument(i);
                        --i;
                    }
                }
            }
            ImGui::EndTabBar();
        }

        active = Active();
        if (active)
        {
            Document& doc = *active;
            if (doc.ChangedOnDisk)
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.28f, 0.10f, 1));
                ImGui::BeginChild("##diskchange", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 6), true);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  This file was changed on disk.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reload")) LoadDocument(doc);
                ImGui::SameLine();
                if (ImGui::SmallButton("Keep Mine"))
                {
                    std::error_code ec;
                    doc.DiskTime = std::filesystem::last_write_time(doc.Path, ec);
                    doc.ChangedOnDisk = false;
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            if (m_ShowFind)
                RenderFindBar(doc);
            if (m_ShowGoto)
            {
                ImGui::SetNextItemWidth(120.0f);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputTextWithHint("##goto", "Line number", m_GotoBuffer, sizeof(m_GotoBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal))
                {
                    int line = std::atoi(m_GotoBuffer);
                    if (line > 0) doc.Editor->ScrollToLine(line - 1);
                    m_ShowGoto = false;
                    m_GotoBuffer[0] = 0;
                    m_FocusEditor = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_ShowGoto = false;
            }
            if (doc.PendingGotoLine >= 0)
            {
                doc.Editor->ScrollToLine(doc.PendingGotoLine);
                doc.PendingGotoLine = -1;
            }

            // Tamamlama penceresi açıkken gezinme tuşları düzenleyiciye gitmez
            bool navigated = false;
            if (m_CompletionOpen && m_EditorFocused)
            {
                int n = (int)m_Completions.size();
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { m_CompletionSelected = (m_CompletionSelected + 1) % n; navigated = true; m_ScrollCompletion = true; }
                else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { m_CompletionSelected = (m_CompletionSelected - 1 + n) % n; navigated = true; m_ScrollCompletion = true; }
                else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) { m_CompletionSelected = std::min(n - 1, m_CompletionSelected + 8); navigated = true; m_ScrollCompletion = true; }
                else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) { m_CompletionSelected = std::max(0, m_CompletionSelected - 8); navigated = true; m_ScrollCompletion = true; }
                else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_CompletionOpen = false; navigated = true; }
                else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Tab))
                {
                    navigated = true;
                    if (m_CompletionSelected < n)
                        Accept(doc, m_Completions[(size_t)m_CompletionSelected]);
                }
            }
            bool forceCompletion = m_EditorFocused && ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false);
            doc.Editor->SetHandleKeyboardInputs(!navigated && !forceCompletion);

            float statusH = ImGui::GetFrameHeightWithSpacing() + 6.0f;
            float problemsH = (doc.Diagnostics.empty() || !m_ShowProblems) ? 0.0f : std::min(120.0f, (float)doc.Diagnostics.size() * ImGui::GetTextLineHeightWithSpacing() + 10.0f) + ImGui::GetStyle().ItemSpacing.y;
            ImVec2 editorSize(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - statusH - problemsH));
            ImGui::SetWindowFontScale(m_FontScale);
            if (m_FocusEditor)
            {
                ImGui::SetNextWindowFocus();
                m_FocusEditor = false;
            }
            auto cursorBefore = doc.Editor->GetCursorPosition();
            ImFont* mono = ImGuiLayer::GetFont(ImGuiLayer::FontRole::Mono);
            if (mono) ImGui::PushFont(mono);
            doc.Editor->Render(("##editor" + std::to_string((uintptr_t)&doc)).c_str(), editorSize);
            if (mono) ImGui::PopFont();
            m_EditorFocused = ImGui::IsItemFocused() || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            ImGui::SetWindowFontScale(1.0f);
            doc.Editor->SetHandleKeyboardInputs(true);

            bool changed = doc.Editor->IsTextChanged();
            if (changed)
            {
                doc.Dirty = true;
                doc.NeedsCheck = true;
                doc.LastEdit = Now();
            }
            // Tamamlama tetikleme
            if (forceCompletion)
                UpdateCompletion(doc, true);
            else if (changed && !m_SuppressCompletion)
            {
                char last = 0;
                std::string line = doc.Editor->GetCurrentLineText();
                auto pos = doc.Editor->GetCursorPosition();
                int idx = ColumnToIndex(line, pos.mColumn, doc.Editor->GetTabSize());
                if (idx > 0 && idx <= (int)line.size()) last = line[(size_t)idx - 1];
                if (IsIdentChar(last) || last == '.')
                    UpdateCompletion(doc, false);
                else
                    m_CompletionOpen = false;
            }
            else if (!changed && !navigated && m_CompletionOpen && !(doc.Editor->GetCursorPosition() == cursorBefore))
                m_CompletionOpen = false; // imleç taşındı
            m_SuppressCompletion = false;
            if (!m_EditorFocused && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                m_CompletionOpen = false;

            RenderCompletionPopup(doc);

            // Teşhis (yazma durduktan 350 ms sonra)
            if (doc.NeedsCheck && Now() - doc.LastEdit > 0.35)
                RunDiagnostics(doc);

            RenderProblems(doc);
            RenderStatusBar(&doc);
        }

        // Kaydedilmemiş belgeyi kapatma onayı
        if (m_ConfirmClose >= 0)
        {
            ImGui::OpenPopup("Unsaved Changes##code");
        }
        if (ImGui::BeginPopupModal("Unsaved Changes##code", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int idx = m_ConfirmClose;
            if (idx >= 0 && idx < (int)m_Documents.size())
            {
                ImGui::Text("Save changes to %s?", m_Documents[(size_t)idx]->Path.filename().string().c_str());
                ImGui::Spacing();
                if (ImGui::Button("Save", ImVec2(90, 0)))
                {
                    int saved = m_ActiveIndex;
                    m_ActiveIndex = idx;
                    SaveFile();
                    m_ActiveIndex = saved;
                    CloseDocument(idx);
                    m_ConfirmClose = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Don't Save", ImVec2(90, 0)))
                {
                    CloseDocument(idx);
                    m_ConfirmClose = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(90, 0)))
                {
                    m_ConfirmClose = -1;
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                m_ConfirmClose = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    // ════════════════════════════════════════
    //  Automation hooks
    // ════════════════════════════════════════
    bool CodeEditorPanel::TypeText(const std::string& text)
    {
        Document* doc = Active();
        if (!doc)
            return false;
        doc->Editor->InsertText(text);
        doc->Dirty = true;
        doc->NeedsCheck = true;
        doc->LastEdit = Now();
        char last = text.empty() ? 0 : text.back();
        if (IsIdentChar(last) || last == '.')
            UpdateCompletion(*doc, false);
        else
            m_CompletionOpen = false;
        return true;
    }

    int CodeEditorPanel::ReplaceAllText(const std::string& find, const std::string& replace, bool matchCase)
    {
        Document* doc = Active();
        if (!doc || find.empty())
            return 0;
        std::strncpy(m_FindBuffer, find.c_str(), sizeof(m_FindBuffer) - 1);
        std::strncpy(m_ReplaceBuffer, replace.c_str(), sizeof(m_ReplaceBuffer) - 1);
        m_FindCase = matchCase;
        ReplaceAll(*doc);
        return std::atoi(m_FindStatus.c_str() + std::strlen("Replaced "));
    }

    std::vector<std::string> CodeEditorPanel::GetCompletionLabels() const
    {
        std::vector<std::string> out;
        if (!m_CompletionOpen)
            return out;
        for (auto& c : m_Completions)
            out.push_back(c.Label);
        return out;
    }

    bool CodeEditorPanel::AcceptCompletion(int index)
    {
        Document* doc = Active();
        if (!doc || !m_CompletionOpen || m_Completions.empty())
            return false;
        int i = index < 0 ? m_CompletionSelected : index;
        if (i < 0 || i >= (int)m_Completions.size())
            return false;
        Accept(*doc, m_Completions[(size_t)i]);
        return true;
    }
}
