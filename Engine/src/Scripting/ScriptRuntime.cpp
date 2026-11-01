#include "ScriptRuntime.h"
#include "Core/Logging/Log.h"
#include "Scripting/ConquerorScript/CQSRuntime.h"
#include "Core/Utils/DynamicLibrary.h"

#ifdef CQ_PYTHON_MODULE
    #include <dlfcn.h>
#endif

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Conqueror::Scripting
{
    // Diğer dillerin fabrikaları (her biri kendi dosyasında; derleme seçeneklerine göre boş olabilir)
    std::unique_ptr<ScriptRuntime> CreateLuauRuntime();
    std::unique_ptr<ScriptRuntime> CreateLuaQRuntime();
    std::unique_ptr<ScriptRuntime> CreatePythonRuntime();
    std::unique_ptr<ScriptRuntime> CreateCSharpRuntime();
    std::unique_ptr<ScriptRuntime> CreateJavaRuntime();
    std::unique_ptr<ScriptRuntime> CreateCppRuntime();
    std::unique_ptr<ScriptRuntime> CreateCRuntime();
    std::unique_ptr<ScriptRuntime> CreateRustRuntime();
    std::unique_ptr<ScriptRuntime> CreateZigRuntime();
    std::unique_ptr<ScriptRuntime> CreateVisualScriptRuntime();

#ifdef CQ_PYTHON_MODULE
    // Python ayrı modülde (libCQPython.so, libEngine.so'nun yanında): yalnızca açılabiliyorsa (sistemde libpython
    // varsa) dil etkin olur; yoksa oyun Python'suz çalışır.
    std::unique_ptr<ScriptRuntime> CreatePythonRuntime()
    {
        static DynamicLibrary s_Module;   // süreç boyunca açık kalır (CPython kaldırılamaz)
        if (!s_Module.IsOpen())
        {
            std::vector<std::filesystem::path> candidates;
            Dl_info info{};
            if (dladdr((void*)&CreatePythonRuntime, &info) && info.dli_fname)
                candidates.push_back(std::filesystem::path(info.dli_fname).parent_path() / "libCQPython.so");
            const auto exeDir = DynamicLibrary::ExecutableDirectory();
            candidates.push_back(exeDir / "lib" / "libCQPython.so");
            candidates.push_back(exeDir / ".." / "lib" / "libCQPython.so");
            std::string error;
            if (!DynamicLibrary::OpenFirst(s_Module, candidates, &error))
            {
                CQ_CORE_INFO("Python scripting unavailable: {0}", error);
                return nullptr;
            }
        }
        using Factory = ScriptRuntime* (*)();
        auto factory = (Factory)s_Module.Symbol("CQ_CreatePythonRuntimeModule");
        return factory ? std::unique_ptr<ScriptRuntime>(factory()) : nullptr;
    }
#endif

    namespace
    {
        std::vector<std::unique_ptr<ScriptRuntime>>& Storage()
        {
            static auto* s = new std::vector<std::unique_ptr<ScriptRuntime>>();
            return *s;
        }

        std::vector<ScriptRuntime*>& Views()
        {
            static auto* s = new std::vector<ScriptRuntime*>();
            return *s;
        }

        struct RetiredInstances
        {
            std::vector<std::pair<ScriptRuntime*, void*>> Order;
            std::unordered_set<void*> Set;
        };

        RetiredInstances& Retired()
        {
            static auto* s = new RetiredInstances();
            return *s;
        }

        std::string LowerExt(const std::string& path)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; });
            return ext;
        }
    }

    void ScriptRuntimes::Init()
    {
        if (!Storage().empty())
            return;
        auto add = [](std::unique_ptr<ScriptRuntime> r) {
            if (!r) return;
            Views().push_back(r.get());
            Storage().push_back(std::move(r));
        };
        add(std::make_unique<CQS::CQSRuntime>());
        add(CreateLuauRuntime());
        add(CreateLuaQRuntime());
        add(CreatePythonRuntime());
        add(CreateCSharpRuntime());
        add(CreateJavaRuntime());
        add(CreateCppRuntime());
        add(CreateCRuntime());
        add(CreateRustRuntime());
        add(CreateZigRuntime());
        add(CreateVisualScriptRuntime());
        std::string names;
        for (auto* r : Views())
            names += std::string(names.empty() ? "" : ", ") + r->GetName() + (r->IsAvailable() ? "" : " (unavailable)");
        CQ_CORE_INFO("Script runtimes: {0}", names);
    }

    void ScriptRuntimes::Shutdown()
    {
        Views().clear();
        Storage().clear();
    }

    const std::vector<ScriptRuntime*>& ScriptRuntimes::All()
    {
        return Views();
    }

    ScriptRuntime* ScriptRuntimes::ForPath(const std::string& path)
    {
        std::string ext = LowerExt(path);
        for (auto* r : Views())
            for (auto& e : r->GetExtensions())
                if (e == ext)
                    return r;
        return nullptr;
    }

    ScriptRuntime* ScriptRuntimes::ForLanguage(const std::string& languageId)
    {
        for (auto* r : Views())
            if (languageId == r->GetLanguageId())
                return r;
        return nullptr;
    }

    bool ScriptRuntimes::IsScriptFile(const std::string& path)
    {
        return ForPath(path) != nullptr;
    }

    void ScriptRuntimes::PlayStart()
    {
        Globals().clear();
        for (auto* r : Views())
            if (r->IsAvailable()) r->OnPlayStart();
    }

    std::unordered_map<std::string, ScriptValue>& ScriptRuntimes::Globals()
    {
        static auto* s = new std::unordered_map<std::string, ScriptValue>();
        return *s;
    }

    void ScriptRuntimes::PlayStop()
    {
        for (auto* r : Views())
            if (r->IsAvailable()) r->OnPlayStop();
        Globals().clear();
    }

    void ScriptRuntimes::EndFrame()
    {
        for (auto* r : Views())
            if (r->IsAvailable()) r->EndFrame();
    }

    void ScriptRuntimes::RetireInstance(ScriptRuntime* runtime, void* instance)
    {
        if (!runtime || !instance)
            return;
        auto& r = Retired();
        if (r.Set.insert(instance).second)
            r.Order.emplace_back(runtime, instance);
    }

    bool ScriptRuntimes::IsRetired(void* instance)
    {
        auto& r = Retired();
        return !r.Set.empty() && r.Set.count(instance) != 0;
    }

    void ScriptRuntimes::FlushRetired()
    {
        auto& r = Retired();
        // Yok etme (ör. Python __del__) yeni örnekleri emekliye ayırabilir: liste boşalana kadar
        for (int guard = 0; !r.Order.empty() && guard < 64; ++guard)
        {
            auto batch = std::move(r.Order);
            r.Order.clear();
            for (auto& [runtime, instance] : batch)
                runtime->DestroyInstance(instance);
            for (auto& [runtime, instance] : batch)
                r.Set.erase(instance);
        }
    }

    CQS::Value ScriptRuntimes::ToValue(const ScriptValue& v)
    {
        switch (v.Type)
        {
            case ScriptValue::Kind::Bool: return CQS::Value::MakeBool(v.Bool);
            case ScriptValue::Kind::Int: return CQS::Value::MakeInt(v.Int);
            case ScriptValue::Kind::Float: return CQS::Value::MakeFloat(v.Float);
            case ScriptValue::Kind::String: return CQS::Value::MakeString(v.String);
            default: return CQS::Value();
        }
    }

    ScriptValue ScriptRuntimes::FromValue(const CQS::Value& v, bool* representable)
    {
        if (representable) *representable = true;
        if (v.IsBool()) return ScriptValue::MakeBool(v.AsBool());
        if (v.IsInt()) return ScriptValue::MakeInt(v.AsInt());
        if (v.IsFloat()) return ScriptValue::MakeFloat(v.AsFloat());
        if (v.IsString()) return ScriptValue::MakeString(v.AsString()->Value);
        if (representable) *representable = v.IsNull();
        return ScriptValue();
    }
}
