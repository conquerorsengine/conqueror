#include "Scripting/ScriptRuntime.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Utils/JSON.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#define CQ_POPEN _popen
#define CQ_PCLOSE _pclose
#else
#include <dlfcn.h>
#define CQ_POPEN popen
#define CQ_PCLOSE pclose
#endif

// C# betikleri (.NET, hostfxr ile süreç içinde barındırılır)
// ──────────────────────────────────────────────────────────
//     using Conqueror;
//     using static Conqueror.NativeBridge;
//
//     public class Spinner : ScriptBehaviour
//     {
//         [Range(0, 360)] public float speed = 90f;    // Inspector alanı
//         void Start() { Debug.Log("ready"); }
//         void Update(float dt)
//         {
//             var r = GetRotation();
//             SetRotation(r.x, r.y + speed * dt, r.z);
//         }
//     }
//
// Projedeki tüm .cs dosyaları SDK'daki Roslyn derleyicisiyle (csc) tek bir derlemeye derlenir
// (Library/ScriptAssemblies/Assembly-CSharp.dll). Derleme bellekten, toplanabilir bir
// AssemblyLoadContext'e yüklenir: sıcak yeniden yüklemede örnekler alan adlarıyla yeni türlere taşınır.

namespace Conqueror::Scripting
{
    namespace
    {
        namespace fs = std::filesystem;

#ifdef _WIN32
        using char_t = wchar_t;
        std::wstring ToNative(const std::string& s)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring w((size_t)std::max(n - 1, 0), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
            return w;
        }
#else
        using char_t = char;
        std::string ToNative(const std::string& s) { return s; }
#endif

        // hostfxr / coreclr_delegates işlev türleri (başlık dosyası gerektirmeden)
        using hostfxr_initialize_for_runtime_config_fn = int (*)(const char_t*, const void*, void**);
        using hostfxr_get_runtime_delegate_fn = int (*)(void*, int, void**);
        using hostfxr_close_fn = int (*)(void*);
        using load_assembly_and_get_function_pointer_fn = int (*)(const char_t*, const char_t*, const char_t*, const char_t*, void*, void**);
        constexpr int hdt_load_assembly_and_get_function_pointer = 5;
        const char_t* const UNMANAGEDCALLERSONLY_METHOD = (const char_t*)-1;

        struct CQNativeValue
        {
            int32_t Type;     // 0 null, 1 bool, 2 int, 3 float, 4 string, 5 vector, 6 json
            int32_t Dim;
            int64_t I;
            double X, Y, Z, W;
            const char* Str;
            int32_t StrLen;
            int32_t Pad;
        };
        static_assert(sizeof(CQNativeValue) == 64, "must match the managed NativeValue layout");

        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        bool ReadFile(const fs::path& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        // "10.0.12" sürüm karşılaştırması
        std::vector<int> ParseVersion(const std::string& v)
        {
            std::vector<int> parts;
            std::stringstream ss(v);
            std::string p;
            while (std::getline(ss, p, '.'))
                parts.push_back(std::atoi(p.c_str()));
            return parts;
        }

        fs::path HighestVersionDir(const fs::path& parent, const std::function<bool(const fs::path&)>& accept = nullptr)
        {
            std::error_code ec;
            fs::path best;
            std::vector<int> bestV;
            if (!fs::is_directory(parent, ec)) return best;
            for (auto& e : fs::directory_iterator(parent, ec))
            {
                if (!e.is_directory()) continue;
                std::string name = e.path().filename().string();
                if (name.empty() || !std::isdigit((unsigned char)name[0])) continue;
                if (name.find('-') != std::string::npos) continue; // önizleme sürümlerini atla
                if (accept && !accept(e.path())) continue;
                auto v = ParseVersion(name);
                if (best.empty() || v > bestV)
                {
                    best = e.path();
                    bestV = v;
                }
            }
            return best;
        }

        std::string RunCommand(const std::string& cmd, int& exitCode)
        {
            std::string output;
            FILE* pipe = CQ_POPEN((cmd + " 2>&1").c_str(), "r");
            if (!pipe)
            {
                exitCode = -1;
                return "failed to start process";
            }
            char buf[4096];
            while (std::fgets(buf, sizeof(buf), pipe))
                output += buf;
            exitCode = CQ_PCLOSE(pipe);
            return output;
        }

        std::string Quote(const fs::path& p)
        {
            return "\"" + p.string() + "\"";
        }

        // ── Değer dönüşümü ──
        CQS::Value FromJson(const JSON::Value& j)
        {
            using CQS::Value;
            switch (j.Type)
            {
                case JSON::ValueType::Null: return Value();
                case JSON::ValueType::Bool: return Value::MakeBool(j.BoolVal);
                case JSON::ValueType::Number:
                    if (std::floor(j.NumberVal) == j.NumberVal && std::fabs(j.NumberVal) < 9e15) return Value::MakeInt((int64_t)j.NumberVal);
                    return Value::MakeFloat(j.NumberVal);
                case JSON::ValueType::String: return Value::MakeString(j.StringVal);
                case JSON::ValueType::List:
                {
                    auto* l = new CQS::CQSListObject();
                    for (auto& e : j.ListVal) l->Elements.push_back(FromJson(e));
                    return Value::MakeObject(l);
                }
                case JSON::ValueType::Map:
                {
                    auto* m = new CQS::CQSMapObject();
                    for (auto& [k, v] : j.MapVal) m->Entries[k] = FromJson(v);
                    return Value::MakeObject(m);
                }
            }
            return Value();
        }

        JSON::Value ToJson(const CQS::Value& v, int depth = 0)
        {
            if (v.IsNull() || depth > 16) return JSON::Value::MakeNull();
            if (v.IsBool()) return JSON::Value::MakeBool(v.AsBool());
            if (v.IsNumber()) return JSON::Value::MakeNumber(v.ToNumber());
            if (v.IsString()) return JSON::Value::MakeString(v.AsString()->Value);
            if (v.IsList())
            {
                std::vector<JSON::Value> l;
                for (auto& e : v.AsList()->Elements) l.push_back(ToJson(e, depth + 1));
                return JSON::Value::MakeList(l);
            }
            if (v.IsMap())
            {
                std::unordered_map<std::string, JSON::Value> m;
                for (auto& [k, e] : v.AsMap()->Entries) m[k] = ToJson(e, depth + 1);
                return JSON::Value::MakeMap(m);
            }
            return JSON::Value::MakeString(v.ToString());
        }

        CQS::Value FromNative(const CQNativeValue& n)
        {
            using CQS::Value;
            switch (n.Type)
            {
                case 1: return Value::MakeBool(n.I != 0);
                case 2: return Value::MakeInt(n.I);
                case 3: return Value::MakeFloat(n.X);
                case 4: return Value::MakeString(n.Str ? std::string(n.Str, (size_t)n.StrLen) : std::string());
                case 5:
                {
                    auto* m = new CQS::CQSMapObject();
                    m->Entries["x"] = Value::MakeFloat(n.X);
                    m->Entries["y"] = Value::MakeFloat(n.Y);
                    if (n.Dim >= 3) m->Entries["z"] = Value::MakeFloat(n.Z);
                    if (n.Dim >= 4) m->Entries["w"] = Value::MakeFloat(n.W);
                    return Value::MakeObject(m);
                }
                case 6: return FromJson(JSON::Parse(n.Str ? std::string(n.Str, (size_t)n.StrLen) : "null"));
                default: return Value();
            }
        }

        // Dönen dizelerin ömrü: bir sonraki çağrıya kadar
        thread_local std::string t_ResultBuffer;
        thread_local std::vector<std::string> t_ArgStrings;

        void ToNative(const CQS::Value& v, CQNativeValue& n, std::string& storage)
        {
            std::memset(&n, 0, sizeof(n));
            if (v.IsNull()) return;
            if (v.IsBool()) { n.Type = 1; n.I = v.AsBool() ? 1 : 0; return; }
            if (v.IsInt()) { n.Type = 2; n.I = v.AsInt(); return; }
            if (v.IsFloat()) { n.Type = 3; n.X = v.AsFloat(); return; }
            if (v.IsString())
            {
                storage = v.AsString()->Value;
                n.Type = 4;
                n.Str = storage.c_str();
                n.StrLen = (int32_t)storage.size();
                return;
            }
            if (v.IsMap())
            {
                // Yalnızca sayısal x,y(,z(,w)) içeren harita -> vektör (hızlı yol)
                auto& e = v.AsMap()->Entries;
                auto num = [&](const char* k) { auto it = e.find(k); return it != e.end() && it->second.IsNumber(); };
                if (num("x") && num("y") && e.size() <= 4)
                {
                    int dim = num("z") ? (num("w") ? 4 : 3) : 2;
                    if ((size_t)dim == e.size())
                    {
                        n.Type = 5;
                        n.Dim = dim;
                        n.X = e.at("x").ToNumber();
                        n.Y = e.at("y").ToNumber();
                        if (dim >= 3) n.Z = e.at("z").ToNumber();
                        if (dim >= 4) n.W = e.at("w").ToNumber();
                        return;
                    }
                }
            }
            storage = JSON::Stringify(ToJson(v));
            n.Type = 6;
            n.Str = storage.c_str();
            n.StrLen = (int32_t)storage.size();
        }

        // ── Yönetilen koddan çağrılan yerel geri çağırmalar ──
        int CallNativeCallback(int index, int argc, CQNativeValue* args, CQNativeValue* result)
        {
            const auto& fns = NativeRegistry::Get().GetFunctions();
            if (index < 0 || index >= (int)fns.size())
                return 0;
            const NativeFunctionInfo& f = fns[(size_t)index];
            int slots = std::max(argc, f.Arity < 0 ? argc : f.Arity);
            std::vector<CQS::Value> values((size_t)std::max(slots, 1));
            for (int i = 0; i < argc; ++i)
                values[(size_t)i] = FromNative(args[i]);
            CQS::Value r = f.Function(argc, values.data());
            ToNative(r, *result, t_ResultBuffer);
            return 1;
        }

        void LogCallback(int level, const char* message)
        {
            if (level >= 2) CQ_CORE_ERROR("[C#] {0}", message);
            else if (level == 1) CQ_CORE_WARN("[C#] {0}", message);
            else CQ_CORE_INFO("[C#] {0}", message);
        }

        void SetResultCallback(const char* utf8, int len)
        {
            t_ResultBuffer.assign(utf8 ? utf8 : "", (size_t)std::max(len, 0));
        }

        bool IsCSharpKeyword(const std::string& s)
        {
            static const std::unordered_set<std::string> k = {
                "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
                "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
                "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
                "long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected",
                "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string",
                "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
                "using", "virtual", "void", "volatile", "while" };
            return k.count(s) != 0;
        }

        struct Instance
        {
            int Handle = 0;
            uint32_t Entity = 0xFFFFFFFF;
            std::string ClassName;
        };
    }

    class CSharpRuntime final : public ScriptRuntime
    {
    public:
        CSharpRuntime() { Locate(); }

        const char* GetName() const override { return "C#"; }
        const char* GetLanguageId() const override { return "cs"; }
        std::vector<std::string> GetExtensions() const override { return { ".cs" }; }
        bool IsAvailable() const override { return m_Located; }
        std::string GetUnavailableReason() const override { return m_LocateError; }

        void OnPlayStart() override
        {
            if (m_HostReady)
                m_Reset();
            ClearInstances();
            m_AssemblyLoaded = false;
        }

        void OnPlayStop() override
        {
            if (m_HostReady)
                m_Reset();
            ClearInstances();
            m_AssemblyLoaded = false;
        }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!EnsureHost(error))
                return false;
            if (!EnsureUserAssembly(error))
                return false;
            if (!m_AssemblyLoaded)
            {
                if (!m_LoadUserAssembly(m_UserDll.string().c_str()))
                {
                    std::string msg = LastError();
                    Report(msg);
                    if (error) *error = msg;
                    return false;
                }
                m_AssemblyLoaded = true;
            }
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override
        {
            return fs::path(path).stem().string();
        }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)path;
            if (!m_HostReady || !m_AssemblyLoaded) return nullptr;
            int handle;
            {
                ScriptContext::EntityScope scope(entity);
                handle = m_CreateInstance(className.c_str(), entity);
            }
            if (handle == 0)
            {
                Report(LastError());
                return nullptr;
            }
            auto* inst = new Instance{ handle, entity, className };
            m_Instances.push_back(inst);
            return inst;
        }

        void DestroyInstance(void* instance) override
        {
            auto* inst = static_cast<Instance*>(instance);
            auto it = std::find(m_Instances.begin(), m_Instances.end(), inst);
            if (it == m_Instances.end()) return;
            if (m_HostReady) m_DestroyInstance(inst->Handle);
            m_Instances.erase(it);
            delete inst;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* inst = Valid(instance);
            return inst && m_HasMethod(inst->Handle, method.c_str()) != 0;
        }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            std::vector<CQNativeValue> native((size_t)std::max(argc, 1));
            std::vector<std::string> storage((size_t)std::max(argc, 1));
            for (int i = 0; i < argc; ++i)
                ToNative(args[i], native[(size_t)i], storage[(size_t)i]);
            int ok;
            {
                ScriptContext::EntityScope scope(inst->Entity);
                ok = m_Invoke(inst->Handle, method.c_str(), native.data(), argc);
            }
            if (!ok)
                Report(LastError());
            return ok != 0;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            CQNativeValue n;
            std::string storage;
            ToNative(ScriptRuntimes::ToValue(value), n, storage);
            return m_SetField(inst->Handle, field.c_str(), &n) != 0;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* inst = Valid(instance);
            if (!inst || !m_GetField(inst->Handle, field.c_str())) return false;
            JSON::Value j = JSON::Parse(t_ResultBuffer);
            auto vt = j.MapVal.find("v");
            auto tt = j.MapVal.find("t");
            auto st = j.MapVal.find("s");
            CQS::Value v = vt != j.MapVal.end() ? FromJson(vt->second) : CQS::Value();
            value = ScriptRuntimes::FromValue(v);
            // float alan tam sayı değer taşıyabilir (5f) -> float göster
            if (tt != j.MapVal.end() && (tt->second.StringVal == "float" || tt->second.StringVal == "double") && value.Type == ScriptValue::Kind::Int)
                value = ScriptValue::MakeFloat((double)value.Int);
            if (display) *display = st != j.MapVal.end() ? st->second.StringVal : v.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!m_HostReady || !m_AssemblyLoaded)
                return 0;
            if (!EnsureUserAssembly(error))
                return -1;
            if (!m_LoadUserAssembly(m_UserDll.string().c_str()))
            {
                std::string msg = LastError();
                Report(msg);
                if (error) *error = msg;
                return -1;
            }
            CQ_CORE_INFO("[C#] Hot reloaded script assembly ({0} instances migrated)", m_Instances.size());
            return (int)m_Instances.size();
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            if (!Describe(error)) return false;
            std::string stem = fs::path(path).stem().string();
            // Bu dosyadaki sınıf: dosya adıyla aynı olan (Unity kuralı); yoksa tümü
            for (auto& [name, fields] : m_Description)
                if (name == stem) classes.push_back(name);
            if (classes.empty())
                for (auto& [name, fields] : m_Description) classes.push_back(name);
            return !classes.empty();
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)path;
            if (!Describe(error)) return false;
            for (auto& [name, list] : m_Description)
            {
                if (name != className) continue;
                fields = list;
                return true;
            }
            if (error) *error = "Class '" + className + "' not found (it must be public and derive from ScriptBehaviour)";
            return false;
        }

        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            if (!EnsureHost(nullptr))
                return true;
            if (!m_CheckSyntax(source.c_str(), name.c_str()))
            {
                Report("Syntax check failed: " + LastError());
                return true;
            }
            JSON::Value list = JSON::Parse(t_ResultBuffer);
            bool ok = true;
            for (auto& d : list.ListVal)
            {
                auto get = [&](const char* k) -> const JSON::Value& {
                    static const JSON::Value s_Null;
                    auto it = d.MapVal.find(k);
                    return it != d.MapVal.end() ? it->second : s_Null;
                };
                std::string text = name + ": Line " + std::to_string((int)get("line").NumberVal) + ":" + std::to_string((int)get("col").NumberVal) +
                                   ": " + get("id").StringVal + " " + get("message").StringVal;
                if (get("severity").StringVal == "Error")
                {
                    errors.push_back(text);
                    ok = false;
                }
                else if (warnings)
                    warnings->push_back(text);
            }
            return ok;
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            return "using Conqueror;\n"
                   "using static Conqueror.NativeBridge;\n\n"
                   "public class " + className + " : ScriptBehaviour\n{\n"
                   "    // Public fields appear in the Inspector\n"
                   "    public float speed = 1f;\n\n"
                   "    void Start()\n    {\n    }\n\n"
                   "    void Update(float dt)\n    {\n    }\n}\n";
        }

    private:
        // Konumlar
        bool m_Located = false;
        std::string m_LocateError;
        fs::path m_DotnetRoot, m_DotnetExe, m_HostFxr, m_Csc, m_RoslynDir, m_RefDir;
        std::string m_FrameworkVersion, m_Tfm;

        // Barındırıcı
        bool m_HostReady = false;
        bool m_HostFailed = false;
        fs::path m_HostDll;
        using InitializeFn = int (*)(void*, void*, void*, const char*);
        using LoadFn = int (*)(const char*);
        using ResetFn = void (*)();
        using CreateFn = int (*)(const char*, uint32_t);
        using DestroyFn = void (*)(int);
        using HasMethodFn = int (*)(int, const char*);
        using InvokeFn = int (*)(int, const char*, CQNativeValue*, int);
        using SetFieldFn = int (*)(int, const char*, CQNativeValue*);
        using GetFieldFn = int (*)(int, const char*);
        using DescribeFn = int (*)(const char*);
        using CheckFn = int (*)(const char*, const char*);
        using LastErrorFn = int (*)();
        LoadFn m_LoadUserAssembly = nullptr;
        ResetFn m_Reset = nullptr;
        CreateFn m_CreateInstance = nullptr;
        DestroyFn m_DestroyInstance = nullptr;
        HasMethodFn m_HasMethod = nullptr;
        InvokeFn m_Invoke = nullptr;
        SetFieldFn m_SetField = nullptr;
        GetFieldFn m_GetField = nullptr;
        DescribeFn m_Describe = nullptr;
        CheckFn m_CheckSyntax = nullptr;
        LastErrorFn m_GetLastError = nullptr;

        // Kullanıcı derlemesi
        fs::path m_UserDll;
        size_t m_UserHash = 0;
        bool m_UserBuilt = false;
        bool m_AssemblyLoaded = false;
        std::vector<Instance*> m_Instances;
        std::vector<std::pair<std::string, std::vector<ScriptFieldInfo>>> m_Description;
        size_t m_DescribedHash = 0;
        std::unordered_map<std::string, double> m_LastError;

        Instance* Valid(void* instance)
        {
            if (!m_HostReady || !instance) return nullptr;
            auto* inst = static_cast<Instance*>(instance);
            return std::find(m_Instances.begin(), m_Instances.end(), inst) != m_Instances.end() ? inst : nullptr;
        }

        void ClearInstances()
        {
            for (Instance* i : m_Instances) delete i;
            m_Instances.clear();
        }

        std::string LastError()
        {
            if (!m_GetLastError || !m_GetLastError()) return "unknown C# error";
            return t_ResultBuffer;
        }

        void Report(const std::string& message)
        {
            double now = Now();
            auto it = m_LastError.find(message);
            if (it != m_LastError.end() && now - it->second < 3.0) return;
            m_LastError[message] = now;
            if (m_LastError.size() > 256) m_LastError.clear();
            CQ_CORE_ERROR("[C#] {0}", message);
        }

        // ── .NET kurulumunu bul ──
        void Locate()
        {
            std::vector<fs::path> roots;
            if (const char* env = std::getenv("DOTNET_ROOT")) roots.emplace_back(env);
#ifdef _WIN32
            if (const char* pf = std::getenv("ProgramFiles")) roots.emplace_back(fs::path(pf) / "dotnet");
            roots.emplace_back("C:/Program Files/dotnet");
            const char* exeName = "dotnet.exe";
            const char* fxrName = "hostfxr.dll";
#elif defined(__APPLE__)
            roots.emplace_back("/usr/local/share/dotnet");
            roots.emplace_back("/opt/homebrew/opt/dotnet/libexec");
            if (const char* home = std::getenv("HOME")) roots.emplace_back(fs::path(home) / ".dotnet");
            const char* exeName = "dotnet";
            const char* fxrName = "libhostfxr.dylib";
#else
            roots.emplace_back("/usr/lib64/dotnet");
            roots.emplace_back("/usr/lib/dotnet");
            roots.emplace_back("/usr/share/dotnet");
            roots.emplace_back("/opt/dotnet");
            if (const char* home = std::getenv("HOME")) roots.emplace_back(fs::path(home) / ".dotnet");
            const char* exeName = "dotnet";
            const char* fxrName = "libhostfxr.so";
#endif
            std::error_code ec;
            for (auto& root : roots)
            {
                if (!fs::exists(root / exeName, ec)) continue;
                fs::path fxrDir = HighestVersionDir(root / "host" / "fxr", [&](const fs::path& d) { return fs::exists(d / fxrName); });
                fs::path sdk = HighestVersionDir(root / "sdk", [](const fs::path& d) { return fs::exists(d / "Roslyn" / "bincore" / "csc.dll"); });
                fs::path framework = HighestVersionDir(root / "shared" / "Microsoft.NETCore.App");
                if (fxrDir.empty() || framework.empty())
                    continue;
                if (sdk.empty())
                {
                    m_LocateError = ".NET SDK not found in " + root.string() + " (only the runtime is installed). Install the .NET SDK to use C# scripts.";
                    continue;
                }
                // Çatı sürümüyle aynı ana sürümdeki başvuru paketi
                std::string fwVersion = framework.filename().string();
                auto fv = ParseVersion(fwVersion);
                fs::path refPack = HighestVersionDir(root / "packs" / "Microsoft.NETCore.App.Ref", [&](const fs::path& d) {
                    auto v = ParseVersion(d.filename().string());
                    return !v.empty() && !fv.empty() && v[0] == fv[0];
                });
                if (refPack.empty())
                {
                    m_LocateError = "Microsoft.NETCore.App.Ref targeting pack not found (install the .NET SDK).";
                    continue;
                }
                std::string tfm = "net" + std::to_string(fv[0]) + "." + std::to_string(fv.size() > 1 ? fv[1] : 0);
                fs::path refDir = refPack / "ref" / tfm;
                if (!fs::exists(refDir, ec))
                {
                    m_LocateError = "Reference assemblies for " + tfm + " not found.";
                    continue;
                }
                m_DotnetRoot = root;
                m_DotnetExe = root / exeName;
                m_HostFxr = fxrDir / fxrName;
                m_RoslynDir = sdk / "Roslyn" / "bincore";
                m_Csc = m_RoslynDir / "csc.dll";
                m_RefDir = refDir;
                m_FrameworkVersion = fwVersion;
                m_Tfm = tfm;
                m_Located = true;
                m_LocateError.clear();
                return;
            }
            if (m_LocateError.empty())
                m_LocateError = ".NET not found. Install the .NET SDK (https://dot.net) or set DOTNET_ROOT.";
        }

        // csc ile derle; hataları "dosya(satır,sütun): error CSxxxx: mesaj" biçiminde döndürür
        bool Compile(const std::vector<fs::path>& sources, const fs::path& output, const std::vector<fs::path>& extraRefs, std::string& log, bool allowUnsafe)
        {
            std::error_code ec;
            fs::create_directories(output.parent_path(), ec);
            fs::path rsp = output.parent_path() / (output.stem().string() + ".rsp");
            {
                std::ofstream r(rsp);
                // Script Debugging / Debug Mode: optimizasyonsuz (adım adım hata ayıklama), aksi halde optimize
                r << "-nologo\n-preferreduilang:en-US\n-target:library\n-langversion:latest\n-nullable:disable\n" << (ScriptDebugMode() ? "-optimize-\n-define:DEBUG\n" : "-optimize+\n")
                  << "-debug:portable\n-deterministic\n";
                if (allowUnsafe) r << "-unsafe+\n";
                r << "-out:\"" << output.string() << "\"\n";
                for (auto& e : fs::directory_iterator(m_RefDir, ec))
                    if (e.path().extension() == ".dll")
                        r << "-r:\"" << e.path().string() << "\"\n";
                for (auto& extra : extraRefs)
                    r << "-r:\"" << extra.string() << "\"\n";
                for (auto& s : sources)
                    r << "\"" << s.string() << "\"\n";
            }
            int code = 0;
            log = RunCommand(Quote(m_DotnetExe) + " " + Quote(m_Csc) + " @" + Quote(rsp), code);
            return code == 0 && fs::exists(output, ec);
        }

        // Yerel kayıttan NativeBridge.g.cs üret
        std::string GenerateBridgeSource()
        {
            std::string src = "// Auto-generated: engine nativebridge functions\n"
                              "namespace Conqueror\n{\n    public static partial class NativeBridge\n    {\n";
            const auto& fns = NativeRegistry::Get().GetFunctions();
            for (size_t i = 0; i < fns.size(); ++i)
            {
                const auto& f = fns[i];
                bool validIdent = !f.Name.empty() && (std::isalpha((unsigned char)f.Name[0]) || f.Name[0] == '_');
                for (char c : f.Name) if (!std::isalnum((unsigned char)c) && c != '_') validIdent = false;
                if (!validIdent) continue;
                std::string name = IsCSharpKeyword(f.Name) ? "@" + f.Name : f.Name;
                if (f.Arity < 0)
                {
                    src += "        public static Value " + name + "(params Value[] args) => Conqueror.Hosting.Host.CallNative(" + std::to_string(i) + ", args);\n";
                    continue;
                }
                std::string params, args;
                for (int a = 0; a < f.Arity; ++a)
                {
                    params += (a ? ", " : "") + std::string("Value a") + std::to_string(a) + " = default";
                    args += (a ? ", " : "") + std::string("a") + std::to_string(a);
                }
                src += "        public static Value " + name + "(" + params + ") => Conqueror.Hosting.Host.CallNative(" + std::to_string(i) +
                       ", " + (f.Arity == 0 ? std::string("System.Array.Empty<Value>()") : "new Value[] { " + args + " }") + ");\n";
            }
            // Sabit tablolar (Key, Mouse, Math): Value haritası olarak
            for (const auto& [cname, value] : NativeRegistry::Get().GetConstants())
            {
                if (!value.IsMap()) continue;
                src += "        public static class " + cname + "\n        {\n";
                std::vector<std::pair<std::string, const CQS::Value*>> entries;
                for (auto& [k, v] : value.AsMap()->Entries) entries.emplace_back(k, &v);
                std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
                for (auto& [k, v] : entries)
                {
                    bool valid = !k.empty() && (std::isalpha((unsigned char)k[0]) || k[0] == '_');
                    for (char c : k) if (!std::isalnum((unsigned char)c) && c != '_') valid = false;
                    if (!valid || !v->IsNumber()) continue;
                    std::string id = IsCSharpKeyword(k) ? "@" + k : k;
                    if (v->IsInt()) src += "            public const int " + id + " = " + std::to_string(v->AsInt()) + ";\n";
                    else
                    {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.17g", v->AsFloat());
                        src += "            public const double " + id + " = " + buf + ";\n";
                    }
                }
                src += "        }\n";
            }
            src += "    }\n}\n";
            return src;
        }

        // ── Barındırıcıyı derle ve .NET'i başlat ──
        bool EnsureHost(std::string* error)
        {
            if (m_HostReady) return true;
            if (m_HostFailed || !m_Located)
            {
                if (error) *error = m_Located ? "C# host failed to start (see Log)" : m_LocateError;
                return false;
            }
            m_HostFailed = true; // başarıya kadar
            fs::path apiSrc = FileSystem::ResolveResourcePath("Resources/Scripting/CSharp/Api.cs");
            fs::path hostSrc = FileSystem::ResolveResourcePath("Resources/Scripting/CSharp/Host.cs");
            std::string api, host;
            if (!ReadFile(apiSrc, api) || !ReadFile(hostSrc, host))
            {
                CQ_CORE_ERROR("[C#] Host sources not found ({0})", apiSrc.string());
                if (error) *error = "C# host sources missing";
                return false;
            }
            std::string bridge = GenerateBridgeSource();
            size_t hash = std::hash<std::string>{}(api + host + bridge + m_FrameworkVersion);
            std::error_code ec;
            fs::path dir = fs::temp_directory_path(ec) / "ConquerorEngine" / ("csharp-host-" + std::to_string(hash));
            fs::create_directories(dir, ec);
            m_HostDll = dir / "ConquerorHost.dll";
            if (!fs::exists(m_HostDll, ec))
            {
                std::ofstream(dir / "NativeBridge.g.cs") << bridge;
                std::string log;
                double t0 = Now();
                if (!Compile({ apiSrc, hostSrc, dir / "NativeBridge.g.cs" }, m_HostDll, {}, log, true))
                {
                    CQ_CORE_ERROR("[C#] Host compilation failed:\n{0}", log);
                    if (error) *error = "C# host compilation failed";
                    return false;
                }
                CQ_CORE_INFO("[C#] Host assembly built in {0:.2f}s", Now() - t0);
            }
            // runtimeconfig
            fs::path config = dir / "ConquerorHost.runtimeconfig.json";
            std::ofstream(config) << "{\n  \"runtimeOptions\": {\n    \"tfm\": \"" << m_Tfm << "\",\n"
                                  << "    \"framework\": { \"name\": \"Microsoft.NETCore.App\", \"version\": \"" << m_FrameworkVersion << "\" },\n"
                                  << "    \"configProperties\": { \"System.Runtime.TieredPGO\": true }\n  }\n}\n";

            // hostfxr
#ifdef _WIN32
            HMODULE lib = LoadLibraryW(m_HostFxr.wstring().c_str());
            auto sym = [&](const char* n) { return lib ? (void*)GetProcAddress(lib, n) : nullptr; };
#else
            void* lib = dlopen(m_HostFxr.string().c_str(), RTLD_LAZY | RTLD_LOCAL);
            auto sym = [&](const char* n) { return lib ? dlsym(lib, n) : nullptr; };
#endif
            auto init = (hostfxr_initialize_for_runtime_config_fn)sym("hostfxr_initialize_for_runtime_config");
            auto getDelegate = (hostfxr_get_runtime_delegate_fn)sym("hostfxr_get_runtime_delegate");
            auto close = (hostfxr_close_fn)sym("hostfxr_close");
            if (!init || !getDelegate || !close)
            {
                CQ_CORE_ERROR("[C#] Cannot load hostfxr from {0}", m_HostFxr.string());
                if (error) *error = "hostfxr could not be loaded";
                return false;
            }
            void* ctx = nullptr;
            auto nativeConfig = ToNative(config.string());
            int rc = init(nativeConfig.c_str(), nullptr, &ctx);
            if (rc < 0 || !ctx)
            {
                CQ_CORE_ERROR("[C#] hostfxr_initialize_for_runtime_config failed (0x{0:x})", (unsigned)rc);
                if (ctx) close(ctx);
                if (error) *error = ".NET runtime failed to initialize";
                return false;
            }
            load_assembly_and_get_function_pointer_fn loadFn = nullptr;
            rc = getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, (void**)&loadFn);
            close(ctx);
            if (rc != 0 || !loadFn)
            {
                CQ_CORE_ERROR("[C#] hostfxr_get_runtime_delegate failed (0x{0:x})", (unsigned)rc);
                if (error) *error = ".NET runtime delegate unavailable";
                return false;
            }
            auto dllPath = ToNative(m_HostDll.string());
            auto typeName = ToNative("Conqueror.Hosting.Host, ConquerorHost");
            auto get = [&](const char* method) -> void* {
                void* fn = nullptr;
                auto m = ToNative(method);
                int r = loadFn(dllPath.c_str(), typeName.c_str(), m.c_str(), UNMANAGEDCALLERSONLY_METHOD, nullptr, &fn);
                if (r != 0 || !fn)
                    CQ_CORE_ERROR("[C#] Host entry point '{0}' not found (0x{1:x})", method, (unsigned)r);
                return fn;
            };
            auto initialize = (InitializeFn)get("Initialize");
            m_LoadUserAssembly = (LoadFn)get("LoadUserAssembly");
            m_Reset = (ResetFn)get("Reset");
            m_CreateInstance = (CreateFn)get("CreateInstance");
            m_DestroyInstance = (DestroyFn)get("DestroyInstance");
            m_HasMethod = (HasMethodFn)get("HasMethod");
            m_Invoke = (InvokeFn)get("Invoke");
            m_SetField = (SetFieldFn)get("SetField");
            m_GetField = (GetFieldFn)get("GetField");
            m_Describe = (DescribeFn)get("Describe");
            m_CheckSyntax = (CheckFn)get("CheckSyntax");
            m_GetLastError = (LastErrorFn)get("GetLastError");
            if (!initialize || !m_LoadUserAssembly || !m_Reset || !m_CreateInstance || !m_DestroyInstance || !m_HasMethod || !m_Invoke ||
                !m_SetField || !m_GetField || !m_Describe || !m_CheckSyntax || !m_GetLastError)
            {
                if (error) *error = "C# host entry points missing";
                return false;
            }
            initialize((void*)&CallNativeCallback, (void*)&LogCallback, (void*)&SetResultCallback, m_RoslynDir.string().c_str());
            m_HostReady = true;
            m_HostFailed = false;
            CQ_CORE_INFO("[C#] .NET {0} runtime started (hostfxr)", m_FrameworkVersion);
            return true;
        }

        // Projedeki tüm .cs dosyalarını derle (içerik değiştiyse)
        bool EnsureUserAssembly(std::string* error)
        {
            fs::path projectDir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            std::vector<fs::path> sources;
            fs::path assets = projectDir / "Assets";
            if (!projectDir.empty() && fs::exists(assets, ec))
            {
                for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file(ec) && it->path().extension() == ".cs")
                        sources.push_back(it->path());
            }
            std::sort(sources.begin(), sources.end());
            std::string all;
            for (auto& s : sources)
            {
                std::string text;
                ReadFile(s, text);
                all += s.string() + "\n" + text;
            }
            size_t hash = std::hash<std::string>{}(all + m_HostDll.string() + (ScriptDebugMode() ? "#debug" : "#release"));
            fs::path outDir = (projectDir.empty() ? fs::temp_directory_path(ec) / "ConquerorEngine" : projectDir / "Library") / "ScriptAssemblies";
            fs::path dll = outDir / "Assembly-CSharp.dll";
            if (m_UserBuilt && hash == m_UserHash && fs::exists(dll, ec))
                return true;
            if (sources.empty())
            {
                if (error) *error = "No C# scripts in the project";
                return false;
            }
            std::string log;
            double t0 = Now();
            if (!Compile(sources, dll, { m_HostDll }, log, true))
            {
                // Hataları ayrı satırlar halinde raporla
                std::stringstream ss(log);
                std::string line, first;
                int count = 0;
                while (std::getline(ss, line))
                {
                    if (line.find(": error ") == std::string::npos) continue;
                    if (first.empty()) first = line;
                    if (++count <= 20) CQ_CORE_ERROR("[C#] {0}", line);
                }
                if (count == 0) CQ_CORE_ERROR("[C#] Compilation failed:\n{0}", log);
                if (error) *error = first.empty() ? "C# compilation failed" : first;
                m_UserBuilt = false;
                return false;
            }
            CQ_CORE_INFO("[C#] Compiled {0} script file{1} in {2:.2f}s", sources.size(), sources.size() == 1 ? "" : "s", Now() - t0);
            m_UserDll = dll;
            m_UserHash = hash;
            m_UserBuilt = true;
            return true;
        }

        bool Describe(std::string* error)
        {
            if (!EnsureHost(error) || !EnsureUserAssembly(error))
                return false;
            if (m_DescribedHash == m_UserHash && !m_Description.empty())
                return true;
            if (!m_Describe(m_UserDll.string().c_str()))
            {
                if (error) *error = LastError();
                return false;
            }
            m_Description.clear();
            JSON::Value list = JSON::Parse(t_ResultBuffer);
            for (auto& cls : list.ListVal)
            {
                std::string name = cls.MapVal.count("name") ? cls.MapVal.at("name").StringVal : "";
                std::vector<ScriptFieldInfo> fields;
                if (cls.MapVal.count("fields"))
                {
                    for (auto& f : cls.MapVal.at("fields").ListVal)
                    {
                        ScriptFieldInfo info;
                        info.Name = f.MapVal.count("name") ? f.MapVal.at("name").StringVal : "";
                        info.TypeName = f.MapVal.count("type") ? f.MapVal.at("type").StringVal : "";
                        if (f.MapVal.count("attributes"))
                            for (auto& a : f.MapVal.at("attributes").ListVal) info.Attributes.push_back(a.StringVal);
                        CQS::Value def = f.MapVal.count("default") ? FromJson(f.MapVal.at("default")) : CQS::Value();
                        info.Default = ScriptRuntimes::FromValue(def);
                        if ((info.TypeName == "float" || info.TypeName == "double") && info.Default.Type == ScriptValue::Kind::Int)
                            info.Default = ScriptValue::MakeFloat((double)info.Default.Int);
                        if (info.TypeName == "string" && info.Default.Type == ScriptValue::Kind::Null)
                            info.Default = ScriptValue::MakeString("");
                        info.HasDefault = true;
                        fields.push_back(info);
                    }
                }
                m_Description.emplace_back(name, fields);
            }
            m_DescribedHash = m_UserHash;
            return true;
        }
    };

    std::unique_ptr<ScriptRuntime> CreateCSharpRuntime()
    {
        return std::make_unique<CSharpRuntime>();
    }
}
