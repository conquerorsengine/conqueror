#include "CQSRuntime.h"
#include "CQSEngine.h"

namespace Conqueror::CQS
{
    void CQSRuntime::OnPlayStart() { CQSEngine::ResetRuntime(); }
    void CQSRuntime::OnPlayStop() { CQSEngine::ResetRuntime(); }
    void CQSRuntime::EndFrame() { CQSEngine::EndFrame(); }

    bool CQSRuntime::LoadScript(const std::string& path, std::string* error) { return CQSEngine::LoadScript(path, error); }
    std::string CQSRuntime::GetDefaultClass(const std::string& path) { return CQSEngine::GetFirstScriptClass(path); }

    void* CQSRuntime::CreateInstance(const std::string& path, const std::string& className, uint32_t entity)
    {
        (void)path;
        return CQSEngine::Instantiate(className, entity);
    }

    void CQSRuntime::DestroyInstance(void* instance) { CQSEngine::DestroyInstance(instance); }
    bool CQSRuntime::HasMethod(void* instance, const std::string& method) { return CQSEngine::HasMethod(instance, method); }

    bool CQSRuntime::Invoke(void* instance, const std::string& method, const Value* args, int argc)
    {
        return CQSEngine::InvokeMethod(instance, method, args, argc);
    }

    bool CQSRuntime::InvokeWithResult(void* instance, const std::string& method, const Value* args, int argc, Value* result)
    {
        return CQSEngine::InvokeMethod(instance, method, args, argc, result);
    }

    bool CQSRuntime::SetField(void* instance, const std::string& field, const Scripting::ScriptValue& value)
    {
        return CQSEngine::SetField(instance, field, Scripting::ScriptRuntimes::ToValue(value));
    }

    bool CQSRuntime::GetField(void* instance, const std::string& field, Scripting::ScriptValue& value, std::string* display)
    {
        Value v;
        if (!CQSEngine::GetField(instance, field, v))
            return false;
        bool representable = false;
        value = Scripting::ScriptRuntimes::FromValue(v, &representable);
        if (display)
            *display = v.ToString();
        return true;
    }

    int CQSRuntime::HotReload(const std::string& path, std::string* error) { return CQSEngine::HotReload(path, error); }

    bool CQSRuntime::GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error)
    {
        return CQSEngine::GetScriptClasses(path, classes, error);
    }

    bool CQSRuntime::GetFields(const std::string& path, const std::string& className, std::vector<Scripting::ScriptFieldInfo>& fields, std::string* error)
    {
        std::vector<CQSScriptField> raw;
        if (!CQSEngine::GetScriptFields(path, className, raw, error))
            return false;
        for (auto& f : raw)
        {
            Scripting::ScriptFieldInfo info;
            info.Name = f.Name;
            info.TypeName = f.TypeName;
            info.Attributes = f.Attributes;
            info.HasDefault = f.HasDefault;
            if (f.IsStringDefault)
                info.Default = Scripting::ScriptValue::MakeString(f.DefaultString);
            else
                info.Default = Scripting::ScriptRuntimes::FromValue(f.DefaultValue);
            // Tip adından tür (varsayılanı olmayan alanlar)
            if (info.Default.Type == Scripting::ScriptValue::Kind::Null)
            {
                if (f.TypeName == "int" || f.TypeName == "long") info.Default = Scripting::ScriptValue::MakeInt(0);
                else if (f.TypeName == "float" || f.TypeName == "double") info.Default = Scripting::ScriptValue::MakeFloat(0.0);
                else if (f.TypeName == "bool") info.Default = Scripting::ScriptValue::MakeBool(false);
                else if (f.TypeName == "string") info.Default = Scripting::ScriptValue::MakeString("");
            }
            fields.push_back(std::move(info));
        }
        return true;
    }

    bool CQSRuntime::CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings)
    {
        return CQSEngine::CheckSyntax(source, name, errors, warnings);
    }

    std::string CQSRuntime::GetNewScriptTemplate(const std::string& className) const
    {
        return "using nativebridge;\n\nscript " + className + " : ConquerorScript\n{\n"
               "    // Public fields appear in the Inspector\n"
               "    public float speed = 1.0;\n\n"
               "    func Start()\n    {\n    }\n\n"
               "    func Update(dt)\n    {\n    }\n}\n";
    }
}
