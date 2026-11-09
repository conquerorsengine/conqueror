#include "CQSEngine.h"
#include <regex>
#include <unordered_set>
#include <filesystem>
#include "Core/Project/Project.h"
#include "CQSCompiler.h"
#include "CQSLexer.h"
#include "CQSParser.h"
#include "Core/Logging/Log.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Conqueror::CQS
{
    std::unique_ptr<CQSVM> CQSEngine::s_VM = nullptr;

    namespace
    {
        uint32_t EntityHook(uint32_t entity)
        {
            return ScriptContext::SetCurrentEntity(entity);
        }

        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return false;
            std::stringstream buffer;
            buffer << file.rdbuf();
            out = buffer.str();
            return true;
        }

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        // Dosyadaki sınıf bildirimlerini (sırasıyla) bul
        void CollectClasses(const ASTNodePtr& node, std::vector<std::string>& out, std::vector<ClassDeclNode*>* decls = nullptr)
        {
            if (!node) return;
            if (node->Type == NodeType::Program)
                for (auto& s : static_cast<ProgramNode*>(node.get())->Statements) CollectClasses(s, out, decls);
            else if (node->Type == NodeType::NamespaceStmt)
                for (auto& s : static_cast<NamespaceNode*>(node.get())->Body) CollectClasses(s, out, decls);
            else if (node->Type == NodeType::ClassDecl || node->Type == NodeType::ScriptDecl || node->Type == NodeType::StructDecl)
            {
                auto* c = static_cast<ClassDeclNode*>(node.get());
                out.push_back(c->Name);
                if (decls) decls->push_back(c);
            }
        }
    }

    void CQSEngine::Init()
    {
        CreateVM();
        CQ_CORE_INFO("[CQS] Engine initialized ({0} native functions, JIT: {1}).",
                     Scripting::NativeRegistry::Get().GetFunctions().size(), s_VM->IsJitEnabled() ? "on" : "off");
    }

    void CQSEngine::Shutdown()
    {
        s_Pins.clear();
        s_LoadedHashes.clear();
        s_VM.reset();
    }

    void CQSEngine::CreateVM()
    {
        s_Pins.clear();
        s_LoadedHashes.clear();
        s_VM.reset();
        s_VM = std::make_unique<CQSVM>();
        s_VM->SetErrorCallback(&CQSEngine::OnScriptError);
        s_VM->SetEntityContextHook(&EntityHook);
        s_VM->SetJitEnabled(s_JitEnabled);
        Scripting::NativeRegistry::Get().BindTo(s_VM.get());
    }

    void CQSEngine::ResetRuntime()
    {
        CreateVM();
    }

    void CQSEngine::SetJitEnabled(bool enabled)
    {
        s_JitEnabled = enabled;
        if (s_VM)
            s_VM->SetJitEnabled(enabled);
    }

    void* CQSEngine::GetActiveScene()
    {
        return ScriptContext::GetActiveScene();
    }

    namespace
    {
        uint64_t s_ErrorCount = 0;
        std::string s_LastError;
    }

    void CQSEngine::CountError(const std::string& message)
    {
        ++s_ErrorCount;
        s_LastError = message;
    }

    uint64_t CQSEngine::GetErrorCount() { return s_ErrorCount; }
    std::string CQSEngine::GetLastError() { return s_LastError; }

    void CQSEngine::OnScriptError(const std::string& message)
    {
        CountError(message);
        // Her karede tekrarlanan hatayı (ör. OnUpdate) 3 saniyede bir raporla
        double now = NowSeconds();
        auto it = s_LastErrorTime.find(message);
        if (it != s_LastErrorTime.end() && now - it->second < 3.0)
            return;
        s_LastErrorTime[message] = now;
        if (s_LastErrorTime.size() > 512)
            s_LastErrorTime.clear();
        CQ_CORE_ERROR("[Script] {0}", message);
    }

    namespace
    {
        void LoadUsedLibraries(const std::string& source, const std::string& self, std::string* error)
        {
            static const std::regex kUsing(R"((^|\n)\s*using\s+([A-Za-z_]\w*)\s*;)");
            static std::unordered_set<std::string> loading;
            const auto assets = Project::GetAssetDirectory();
            if (assets.empty())
                return;
            for (std::sregex_iterator m(source.begin(), source.end(), kUsing), end; m != end; ++m)
            {
                const std::string name = (*m)[2].str();
                if (name == "nativebridge" || name == "System" || loading.count(name))
                    continue;
                std::error_code ec;
                for (std::filesystem::recursive_directory_iterator it(assets, std::filesystem::directory_options::skip_permission_denied, ec), done; it != done; it.increment(ec))
                {
                    if (ec)
                        break;
                    if (it->path().stem().string() != name || it->path().extension() != ".cqs" || it->path().string() == self)
                        continue;
                    loading.insert(name);
                    CQSEngine::LoadScript(it->path().string(), error);
                    loading.erase(name);
                    break;
                }
            }
        }
    }

    bool CQSEngine::LoadScript(const std::string& path, std::string* error)
    {
        if (!s_VM)
            CreateVM();
        std::string source;
        if (!ReadFile(path, source))
        {
            std::string msg = "Cannot open script file: " + path;
            CountError(msg);
            CQ_CORE_ERROR("[Script] {0}", msg);
            if (error) *error = msg;
            return false;
        }
        std::string hash = std::to_string(std::hash<std::string>{}(source));
        auto it = s_LoadedHashes.find(path);
        if (it != s_LoadedHashes.end() && it->second == hash)
            return true;

        // "using Ad;": projedeki Ad.cqs önce yüklenir (Library / Enum / Struct betikleri)
        LoadUsedLibraries(source, path, error);

        std::vector<std::string> errors;
        CQSFunctionObject* fn = s_VM->Compile(source, path, errors);
        if (!fn)
        {
            std::string all;
            for (auto& e : errors)
            {
                CountError(e);
                CQ_CORE_ERROR("[Script] {0}", e);
                all += (all.empty() ? "" : "\n") + e;
            }
            if (error) *error = all;
            return false;
        }
        if (!s_VM->RunModule(fn))
        {
            if (error) *error = s_VM->GetLastError();
            return false;
        }
        s_LoadedHashes[path] = hash;

        // Dosyadaki sınıf adları (Instantiate'te sınıf adı boşsa ilk script sınıfı)
        CQSLexer lexer;
        CQSParser parser;
        auto ast = parser.Parse(lexer.Tokenize(source));
        std::vector<std::string> classes;
        CollectClasses(ast, classes);
        s_FileClasses[path] = classes;
        return true;
    }

    int CQSEngine::HotReload(const std::string& path, std::string* error)
    {
        if (!s_VM)
            return 0;
        // Yalnızca bu VM'de daha önce yüklenmiş dosyalar (oyun sırasında kullanılanlar)
        std::error_code ec;
        std::string key = path;
        if (!s_LoadedHashes.count(key))
        {
            auto canonical = std::filesystem::weakly_canonical(path, ec).string();
            for (auto& [p, h] : s_LoadedHashes)
                if (std::filesystem::weakly_canonical(p, ec).string() == canonical) { key = p; break; }
            if (!s_LoadedHashes.count(key))
                return 0;
        }
        if (!LoadScript(key, error))
            return -1;
        int updated = 0;
        for (auto& [ptr, pin] : s_Pins)
        {
            auto* inst = static_cast<CQSInstanceObject*>(ptr);
            CQSClassObject* fresh = s_VM->FindClass(inst->ClassName);
            if (!fresh || fresh == inst->Class)
                continue;
            // Alanları ada göre taşı; yeni alanlar sabit varsayılanlarını alır
            std::vector<Value> slots(fresh->FieldNames.size());
            for (CQSClassObject* c = fresh; c; c = c->Base)
                for (auto& f : c->OwnFields)
                {
                    auto it = fresh->FieldIndex.find(f.Name);
                    if (it != fresh->FieldIndex.end())
                        slots[(size_t)it->second] = f.DefaultValue;
                }
            if (inst->Class)
            {
                for (size_t i = 0; i < inst->Class->FieldNames.size() && i < inst->Slots.size(); ++i)
                {
                    auto it = fresh->FieldIndex.find(inst->Class->FieldNames[i]);
                    if (it != fresh->FieldIndex.end())
                        slots[(size_t)it->second] = inst->Slots[i];
                }
            }
            inst->Slots = std::move(slots);
            inst->Class = fresh;
            ++updated;
        }
        CQ_CORE_INFO("[Script] Hot reloaded {0} ({1} running instance{2} updated)", std::filesystem::path(path).filename().string(), updated, updated == 1 ? "" : "s");
        return updated;
    }

    bool CQSEngine::ExecuteString(const std::string& source, const std::string& name)
    {
        if (!s_VM)
            CreateVM();
        return s_VM->Interpret(source, name) == VMResult::Success;
    }

    bool CQSEngine::Evaluate(const std::string& source, std::string& output)
    {
        if (!s_VM)
            CreateVM();
        Value result;
        std::string error;
        if (!s_VM->Evaluate(source, result, error))
        {
            output = error;
            return false;
        }
        if (!result.IsNull())
        {
            if (result.IsString())
                output = "\"" + result.AsString()->Value + "\"";
            else if (!s_VM->ValueToString(result, output))
                output = result.ToString();
        }
        else
            output.clear();
        return true;
    }

    std::string CQSEngine::GetFirstScriptClass(const std::string& path)
    {
        auto it = s_FileClasses.find(path);
        if (it == s_FileClasses.end() || it->second.empty())
            return "";
        // Önce "script" ya da ConquerorScript'ten türeyen sınıf
        for (auto& name : it->second)
        {
            CQSClassObject* cls = s_VM ? s_VM->FindClass(name) : nullptr;
            if (cls && cls->IsScript)
                return name;
        }
        return it->second.front();
    }

    void* CQSEngine::Instantiate(const std::string& className, uint32_t entityHandle)
    {
        if (!s_VM)
            return nullptr;
        CQSClassObject* cls = s_VM->FindClass(className);
        if (!cls)
        {
            OnScriptError("Script class '" + className + "' was not found. Make sure the class name matches the script (e.g. 'script " + className + " : ConquerorScript').");
            return nullptr;
        }
        CQSInstanceObject* inst = s_VM->Instantiate(cls, nullptr, 0, entityHandle);
        if (!inst)
            return nullptr;
        s_Pins[inst] = s_VM->Pin(Value::MakeObject(inst));
        return inst;
    }

    void CQSEngine::DestroyInstance(void* instance)
    {
        auto it = s_Pins.find(instance);
        if (it == s_Pins.end())
            return;
        if (s_VM)
            s_VM->Unpin(it->second);
        s_Pins.erase(it);
    }

    bool CQSEngine::HasMethod(void* instance, const std::string& method)
    {
        if (!instance || !s_Pins.count(instance))
            return false;
        auto* inst = static_cast<CQSInstanceObject*>(instance);
        return inst->Class && inst->Class->FindMethod(method);
    }

    bool CQSEngine::InvokeMethod(void* instance, const std::string& methodName, const Value* args, int argc, Value* result)
    {
        if (!s_VM || !instance || !s_Pins.count(instance))
            return false;
        auto* inst = static_cast<CQSInstanceObject*>(instance);
        ScriptContext::EntityScope scope(inst->EntityHandle);
        return s_VM->CallMethod(Value::MakeObject(inst), methodName, args, argc, result);
    }

    void CQSEngine::Invoke(void* instance, const std::string& methodName, float ts, void* scene)
    {
        if (scene)
            ScriptContext::SetActiveScene(static_cast<Scene*>(scene));
        Value arg = Value::MakeFloat((double)ts);
        InvokeMethod(instance, methodName, &arg, 1);
    }

    bool CQSEngine::SetField(void* instance, const std::string& field, const Value& value)
    {
        if (!s_VM || !instance || !s_Pins.count(instance))
            return false;
        bool ok = s_VM->SetProperty(Value::MakeObject(static_cast<CQSInstanceObject*>(instance)), field, value, nullptr);
        if (!ok) s_VM->TakeException();
        return ok;
    }

    bool CQSEngine::GetField(void* instance, const std::string& field, Value& value)
    {
        if (!s_VM || !instance || !s_Pins.count(instance))
            return false;
        Value slot = Value::MakeObject(static_cast<CQSInstanceObject*>(instance));
        bool ok = s_VM->GetProperty(&slot, field, nullptr);
        if (!ok)
        {
            s_VM->TakeException();
            return false;
        }
        value = slot;
        return true;
    }

    void CQSEngine::EndFrame()
    {
        if (s_VM)
            s_VM->MaybeCollect();
    }

    bool CQSEngine::CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors,
                                std::vector<std::string>* warnings)
    {
        CQSVM scratch;
        Scripting::NativeRegistry::Get().BindTo(&scratch);
        return scratch.Compile(source, name, errors, warnings) != nullptr;
    }

    bool CQSEngine::GetScriptClasses(const std::string& path, std::vector<std::string>& classes, std::string* error)
    {
        std::string source;
        if (!ReadFile(path, source))
        {
            if (error) *error = "Cannot open " + path;
            return false;
        }
        CQSNoGCScope noGC;
        CQSLexer lexer;
        CQSParser parser;
        auto ast = parser.Parse(lexer.Tokenize(source));
        if (parser.HasErrors() && error)
            *error = parser.GetErrors().front();
        CollectClasses(ast, classes);
        return !classes.empty();
    }

    bool CQSEngine::GetScriptFields(const std::string& path, const std::string& className, std::vector<CQSScriptField>& fields, std::string* error)
    {
        std::string source;
        if (!ReadFile(path, source))
        {
            if (error) *error = "Cannot open " + path;
            return false;
        }
        // Derleyiciyle sınıf şablonunu üret (çalıştırmadan): alan meta verisi şablondadır
        CQSVM scratch;
        Scripting::NativeRegistry::Get().BindTo(&scratch);
        std::vector<std::string> errors;
        CQSFunctionObject* module = scratch.Compile(source, path, errors);
        if (!module)
        {
            if (error && !errors.empty()) *error = errors.front();
            return false;
        }
        for (const Value& k : module->Constants)
        {
            if (!k.IsClass())
                continue;
            CQSClassObject* templ = k.AsClass();
            if (!className.empty() && templ->Name != className)
                continue;
            for (auto& f : templ->OwnFields)
            {
                if (!f.IsPublic)
                    continue;
                CQSScriptField sf;
                sf.Name = f.Name;
                sf.TypeName = f.TypeName;
                if (f.DefaultValue.IsString())
                {
                    sf.DefaultString = f.DefaultValue.AsString()->Value;
                    sf.IsStringDefault = true;
                }
                else if (!f.DefaultValue.IsObject())
                    sf.DefaultValue = f.DefaultValue;
                sf.HasDefault = f.HasLiteralDefault;
                sf.Attributes = f.Attributes;
                fields.push_back(sf);
            }
            return true;
        }
        if (error) *error = "Class '" + className + "' not found in " + path;
        return false;
    }
}
