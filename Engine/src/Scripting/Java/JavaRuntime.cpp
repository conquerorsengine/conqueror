#include "Scripting/ScriptRuntime.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Utils/JSON.h"

#ifdef CQ_JAVA_ENABLED
#include <jni.h>
#endif

#include <algorithm>
#include <clocale>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
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

// Java betikleri (JVM süreç içinde, JNI)
// ──────────────────────────────────────
//     import conqueror.*;
//     import static conqueror.NativeBridge.*;
//
//     public class Walker extends ScriptBehaviour {
//         @Range(min = 0, max = 10) public float speed = 2f;     // Inspector alanı
//         public void start() { Debug.log("ready"); }             // Start/start, Update/update ...
//         public void update(float dt) {
//             Value p = GetPosition();
//             SetPosition(p.x() + speed * dt, p.y(), p.z());
//         }
//     }
//
// Betikler JDK'nın derleyici API'siyle süreç içinde derlenir (Library/ScriptAssemblies/java) ve her
// yeniden yüklemede yeni bir URLClassLoader ile yüklenir; çalışan örnekler alan adlarıyla taşınır.

namespace Conqueror::Scripting
{
#ifdef CQ_JAVA_ENABLED
    namespace
    {
        namespace fs = std::filesystem;

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

        // ── CQS::Value <-> JSON ──
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

        std::string JStr(JNIEnv* env, jstring s)
        {
            if (!s) return "";
            const char* c = env->GetStringUTFChars(s, nullptr);
            std::string r = c ? c : "";
            env->ReleaseStringUTFChars(s, c);
            return r;
        }

        // ── Java'dan çağrılan yerel metotlar ──
        jstring JNICALL NativeCall(JNIEnv* env, jclass, jint index, jstring argsJson)
        {
            const auto& fns = NativeRegistry::Get().GetFunctions();
            if (index < 0 || index >= (jint)fns.size())
                return nullptr;
            const NativeFunctionInfo& f = fns[(size_t)index];
            JSON::Value args = JSON::Parse(JStr(env, argsJson));
            int argc = (int)args.ListVal.size();
            int slots = std::max(argc, f.Arity < 0 ? argc : f.Arity);
            std::vector<CQS::Value> values((size_t)std::max(slots, 1));
            for (int i = 0; i < argc; ++i)
                values[(size_t)i] = FromJson(args.ListVal[(size_t)i]);
            CQS::Value r = f.Function(argc, values.data());
            return env->NewStringUTF(JSON::Stringify(ToJson(r)).c_str());
        }

        void JNICALL NativeLog(JNIEnv* env, jclass, jint level, jstring message)
        {
            std::string m = JStr(env, message);
            if (level >= 2) CQ_CORE_ERROR("[Java] {0}", m);
            else if (level == 1) CQ_CORE_WARN("[Java] {0}", m);
            else CQ_CORE_INFO("[Java] {0}", m);
        }

        bool IsJavaKeyword(const std::string& s)
        {
            static const std::unordered_set<std::string> k = {
                "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue", "default",
                "do", "double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if", "implements", "import",
                "instanceof", "int", "interface", "long", "native", "new", "package", "private", "protected", "public", "return",
                "short", "static", "strictfp", "super", "switch", "synchronized", "this", "throw", "throws", "transient", "try",
                "void", "volatile", "while", "true", "false", "null", "var", "record", "yield", "sealed", "permits" };
            return k.count(s) != 0;
        }

        struct Instance
        {
            int Handle = 0;
            uint32_t Entity = 0xFFFFFFFF;
        };
    }

    class JavaRuntime final : public ScriptRuntime
    {
    public:
        JavaRuntime() { Locate(); }

        const char* GetName() const override { return "Java"; }
        const char* GetLanguageId() const override { return "java"; }
        std::vector<std::string> GetExtensions() const override { return { ".java" }; }
        bool IsAvailable() const override { return m_Located; }
        std::string GetUnavailableReason() const override { return m_LocateError; }

        void OnPlayStart() override
        {
            if (m_Env) { CallVoid(m_Reset); }
            ClearInstances();
            m_ClassesLoaded = false;
        }

        void OnPlayStop() override { OnPlayStart(); }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!EnsureJvm(error) || !EnsureCompiled(error))
                return false;
            if (!m_ClassesLoaded)
            {
                jstring dir = m_Env->NewStringUTF(m_ClassDir.string().c_str());
                jboolean ok = m_Env->CallStaticBooleanMethod(m_Host, m_LoadClasses, dir);
                m_Env->DeleteLocalRef(dir);
                if (CheckException() || !ok)
                {
                    std::string msg = LastError();
                    Report(msg);
                    if (error) *error = msg;
                    return false;
                }
                m_ClassesLoaded = true;
            }
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override { return fs::path(path).stem().string(); }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)path;
            if (!m_Env || !m_ClassesLoaded) return nullptr;
            jstring name = m_Env->NewStringUTF(className.c_str());
            jint handle;
            {
                ScriptContext::EntityScope scope(entity);
                handle = m_Env->CallStaticIntMethod(m_Host, m_CreateInstance, name, (jlong)entity);
            }
            m_Env->DeleteLocalRef(name);
            if (CheckException() || handle == 0)
            {
                Report(LastError());
                return nullptr;
            }
            auto* inst = new Instance{ handle, entity };
            m_Instances.push_back(inst);
            return inst;
        }

        void DestroyInstance(void* instance) override
        {
            auto* inst = static_cast<Instance*>(instance);
            auto it = std::find(m_Instances.begin(), m_Instances.end(), inst);
            if (it == m_Instances.end()) return;
            if (m_Env)
            {
                m_Env->CallStaticVoidMethod(m_Host, m_DestroyInstance, (jint)inst->Handle);
                CheckException();
            }
            m_Instances.erase(it);
            delete inst;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            jstring name = m_Env->NewStringUTF(method.c_str());
            jboolean has = m_Env->CallStaticBooleanMethod(m_Host, m_HasMethod, (jint)inst->Handle, name);
            m_Env->DeleteLocalRef(name);
            return !CheckException() && has;
        }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            std::vector<JSON::Value> list;
            for (int i = 0; i < argc; ++i) list.push_back(ToJson(args[i]));
            jstring name = m_Env->NewStringUTF(method.c_str());
            jstring json = m_Env->NewStringUTF(JSON::Stringify(JSON::Value::MakeList(list)).c_str());
            jboolean ok;
            {
                ScriptContext::EntityScope scope(inst->Entity);
                ok = m_Env->CallStaticBooleanMethod(m_Host, m_Invoke, (jint)inst->Handle, name, json);
            }
            m_Env->DeleteLocalRef(name);
            m_Env->DeleteLocalRef(json);
            if (CheckException() || !ok)
            {
                Report(LastError());
                return false;
            }
            return true;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            jstring name = m_Env->NewStringUTF(field.c_str());
            jstring json = m_Env->NewStringUTF(JSON::Stringify(ToJson(ScriptRuntimes::ToValue(value))).c_str());
            jboolean ok = m_Env->CallStaticBooleanMethod(m_Host, m_SetField, (jint)inst->Handle, name, json);
            m_Env->DeleteLocalRef(name);
            m_Env->DeleteLocalRef(json);
            return !CheckException() && ok;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            jstring name = m_Env->NewStringUTF(field.c_str());
            auto result = (jstring)m_Env->CallStaticObjectMethod(m_Host, m_GetField, (jint)inst->Handle, name);
            m_Env->DeleteLocalRef(name);
            if (CheckException() || !result) return false;
            JSON::Value j = JSON::Parse(JStr(m_Env, result));
            m_Env->DeleteLocalRef(result);
            auto vt = j.MapVal.find("v");
            auto tt = j.MapVal.find("t");
            auto st = j.MapVal.find("s");
            CQS::Value v = vt != j.MapVal.end() ? FromJson(vt->second) : CQS::Value();
            value = ScriptRuntimes::FromValue(v);
            if (tt != j.MapVal.end() && (tt->second.StringVal == "float" || tt->second.StringVal == "double") && value.Type == ScriptValue::Kind::Int)
                value = ScriptValue::MakeFloat((double)value.Int);
            if (display) *display = st != j.MapVal.end() ? st->second.StringVal : v.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!m_Env || !m_ClassesLoaded) return 0;
            if (!EnsureCompiled(error)) return -1;
            jstring dir = m_Env->NewStringUTF(m_ClassDir.string().c_str());
            jboolean ok = m_Env->CallStaticBooleanMethod(m_Host, m_LoadClasses, dir);
            m_Env->DeleteLocalRef(dir);
            if (CheckException() || !ok)
            {
                std::string msg = LastError();
                Report(msg);
                if (error) *error = msg;
                return -1;
            }
            CQ_CORE_INFO("[Java] Hot reloaded scripts ({0} instances migrated)", m_Instances.size());
            return (int)m_Instances.size();
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            if (!Describe(error)) return false;
            std::string stem = fs::path(path).stem().string();
            for (auto& [name, f] : m_Description)
                if (name == stem) classes.push_back(name);
            if (classes.empty())
                for (auto& [name, f] : m_Description) classes.push_back(name);
            return !classes.empty();
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)path;
            if (!Describe(error)) return false;
            for (auto& [name, list] : m_Description)
                if (name == className) { fields = list; return true; }
            if (error) *error = "Class '" + className + "' not found (public, default package, extends ScriptBehaviour)";
            return false;
        }

        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            if (!EnsureJvm(nullptr)) return true;
            jstring src = m_Env->NewStringUTF(source.c_str());
            jstring nm = m_Env->NewStringUTF(name.c_str());
            auto result = (jstring)m_Env->CallStaticObjectMethod(m_Host, m_CheckSyntax, src, nm);
            m_Env->DeleteLocalRef(src);
            m_Env->DeleteLocalRef(nm);
            if (CheckException() || !result) return true;
            JSON::Value j = JSON::Parse(JStr(m_Env, result));
            m_Env->DeleteLocalRef(result);
            return CollectDiagnostics(j, name, errors, warnings);
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            return "import conqueror.*;\n"
                   "import static conqueror.NativeBridge.*;\n\n"
                   "public class " + className + " extends ScriptBehaviour {\n"
                   "    // Public fields appear in the Inspector\n"
                   "    public float speed = 1f;\n\n"
                   "    public void start() {\n    }\n\n"
                   "    public void update(float dt) {\n    }\n}\n";
        }

    private:
        bool m_Located = false;
        std::string m_LocateError;
        fs::path m_JavaHome, m_Javac, m_LibJvm;

        JavaVM* m_Jvm = nullptr;
        JNIEnv* m_Env = nullptr;
        bool m_JvmFailed = false;
        fs::path m_HostDir;
        jclass m_Host = nullptr;
        jmethodID m_Compile = nullptr, m_CheckSyntax = nullptr, m_LoadClasses = nullptr, m_Reset = nullptr, m_CreateInstance = nullptr,
                  m_DestroyInstance = nullptr, m_HasMethod = nullptr, m_Invoke = nullptr, m_SetField = nullptr, m_GetField = nullptr,
                  m_Describe = nullptr, m_GetLastError = nullptr;

        fs::path m_ClassDir;
        size_t m_SourceHash = 0;
        bool m_Compiled = false;
        bool m_ClassesLoaded = false;
        std::vector<Instance*> m_Instances;
        std::vector<std::pair<std::string, std::vector<ScriptFieldInfo>>> m_Description;
        size_t m_DescribedHash = 0;
        std::unordered_map<std::string, double> m_LastError;

        Instance* Valid(void* instance)
        {
            if (!m_Env || !instance) return nullptr;
            auto* inst = static_cast<Instance*>(instance);
            return std::find(m_Instances.begin(), m_Instances.end(), inst) != m_Instances.end() ? inst : nullptr;
        }

        void ClearInstances()
        {
            for (Instance* i : m_Instances) delete i;
            m_Instances.clear();
        }

        void CallVoid(jmethodID m)
        {
            m_Env->CallStaticVoidMethod(m_Host, m);
            CheckException();
        }

        // Beklenmeyen Java istisnası: yaz ve temizle
        bool CheckException()
        {
            if (!m_Env || !m_Env->ExceptionCheck()) return false;
            jthrowable ex = m_Env->ExceptionOccurred();
            m_Env->ExceptionClear();
            jclass cls = m_Env->FindClass("java/lang/Object");
            jmethodID toString = m_Env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
            auto s = (jstring)m_Env->CallObjectMethod(ex, toString);
            m_Env->ExceptionClear();
            Report("Unhandled JVM exception: " + JStr(m_Env, s));
            m_Env->DeleteLocalRef(ex);
            m_Env->DeleteLocalRef(cls);
            if (s) m_Env->DeleteLocalRef(s);
            return true;
        }

        std::string LastError()
        {
            if (!m_Env) return "JVM not running";
            auto s = (jstring)m_Env->CallStaticObjectMethod(m_Host, m_GetLastError);
            if (m_Env->ExceptionCheck()) { m_Env->ExceptionClear(); return "unknown Java error"; }
            std::string r = JStr(m_Env, s);
            if (s) m_Env->DeleteLocalRef(s);
            return r.empty() ? "unknown Java error" : r;
        }

        void Report(const std::string& message)
        {
            double now = Now();
            auto it = m_LastError.find(message);
            if (it != m_LastError.end() && now - it->second < 3.0) return;
            m_LastError[message] = now;
            if (m_LastError.size() > 256) m_LastError.clear();
            CQ_CORE_ERROR("[Java] {0}", message);
        }

        bool CollectDiagnostics(const JSON::Value& j, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings)
        {
            bool ok = true;
            auto diags = j.MapVal.find("diagnostics");
            if (diags == j.MapVal.end()) return true;
            for (auto& d : diags->second.ListVal)
            {
                auto get = [&](const char* k) -> const JSON::Value& {
                    static const JSON::Value s_Null;
                    auto it = d.MapVal.find(k);
                    return it != d.MapVal.end() ? it->second : s_Null;
                };
                std::string file = get("file").StringVal.empty() ? name : get("file").StringVal;
                std::string text = file + ": Line " + std::to_string((int)get("line").NumberVal) + ":" + std::to_string((int)get("col").NumberVal) + ": " + get("message").StringVal;
                if (get("kind").StringVal == "ERROR")
                {
                    errors.push_back(text);
                    ok = false;
                }
                else if (warnings)
                    warnings->push_back(text);
            }
            return ok;
        }

        // ── JDK'yı bul ──
        void Locate()
        {
            std::vector<fs::path> homes;
            if (const char* env = std::getenv("JAVA_HOME")) homes.emplace_back(env);
            std::error_code ec;
#ifdef _WIN32
            const char* javacName = "javac.exe";
            for (const char* base : { "C:/Program Files/Java", "C:/Program Files/Eclipse Adoptium", "C:/Program Files/Microsoft" })
                if (fs::is_directory(base, ec))
                    for (auto& e : fs::directory_iterator(base, ec)) homes.push_back(e.path());
#else
            const char* javacName = "javac";
            // PATH'teki javac (sembolik bağları çöz)
            int code = 0;
            std::string which = RunCommand("command -v javac", code);
            while (!which.empty() && (which.back() == '\n' || which.back() == '\r')) which.pop_back();
            if (code == 0 && !which.empty())
            {
                fs::path real = fs::canonical(which, ec);
                if (!ec) homes.push_back(real.parent_path().parent_path());
            }
#ifdef __APPLE__
            std::string javaHome = RunCommand("/usr/libexec/java_home", code);
            while (!javaHome.empty() && (javaHome.back() == '\n')) javaHome.pop_back();
            if (code == 0 && !javaHome.empty()) homes.emplace_back(javaHome);
#endif
            for (const char* base : { "/usr/lib/jvm", "/usr/java", "/opt/java" })
                if (fs::is_directory(base, ec))
                    for (auto& e : fs::directory_iterator(base, ec)) homes.push_back(e.path());
#endif
            for (auto& home : homes)
            {
#ifdef _WIN32
                fs::path jvm = home / "bin" / "server" / "jvm.dll";
#elif defined(__APPLE__)
                fs::path jvm = home / "lib" / "server" / "libjvm.dylib";
#else
                fs::path jvm = home / "lib" / "server" / "libjvm.so";
#endif
                if (fs::exists(home / "bin" / javacName, ec) && fs::exists(jvm, ec) && fs::exists(home / "include" / "jni.h", ec))
                {
                    m_JavaHome = home;
                    m_Javac = home / "bin" / javacName;
                    m_LibJvm = jvm;
                    m_Located = true;
                    return;
                }
            }
            m_LocateError = "A Java Development Kit (JDK 17+) was not found. Install a JDK or set JAVA_HOME.";
        }

        std::string GenerateBridgeSource()
        {
            std::string src = "// Auto-generated: engine nativebridge functions\npackage conqueror;\n\n"
                              "public final class NativeBridge {\n    private NativeBridge() {}\n";
            const auto& fns = NativeRegistry::Get().GetFunctions();
            for (size_t i = 0; i < fns.size(); ++i)
            {
                const auto& f = fns[i];
                bool valid = !f.Name.empty() && (std::isalpha((unsigned char)f.Name[0]) || f.Name[0] == '_');
                for (char c : f.Name) if (!std::isalnum((unsigned char)c) && c != '_') valid = false;
                if (!valid || IsJavaKeyword(f.Name)) continue;
                if (f.Arity < 0)
                {
                    src += "    public static Value " + f.Name + "(Object... args) { Value[] v = new Value[args.length]; for (int i = 0; i < args.length; ++i) v[i] = Value.from(args[i]); return Host.call(" + std::to_string(i) + ", v); }\n";
                    continue;
                }
                std::string params, args;
                for (int a = 0; a < f.Arity; ++a)
                {
                    params += (a ? ", " : "") + std::string("Object a") + std::to_string(a);
                    args += ", Value.from(a" + std::to_string(a) + ")";
                }
                src += "    public static Value " + f.Name + "(" + params + ") { return Host.call(" + std::to_string(i) + args + "); }\n";
            }
            for (const auto& [cname, value] : NativeRegistry::Get().GetConstants())
            {
                if (!value.IsMap()) continue;
                src += "    public static final class " + cname + " {\n        private " + cname + "() {}\n";
                std::vector<std::pair<std::string, const CQS::Value*>> entries;
                for (auto& [k, v] : value.AsMap()->Entries) entries.emplace_back(k, &v);
                std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
                for (auto& [k, v] : entries)
                {
                    bool valid = !k.empty() && (std::isalpha((unsigned char)k[0]) || k[0] == '_');
                    for (char c : k) if (!std::isalnum((unsigned char)c) && c != '_') valid = false;
                    if (!valid || IsJavaKeyword(k) || !v->IsNumber()) continue;
                    if (v->IsInt()) src += "        public static final int " + k + " = " + std::to_string(v->AsInt()) + ";\n";
                    else
                    {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.17g", v->AsFloat());
                        src += "        public static final double " + k + " = " + buf + ";\n";
                    }
                }
                src += "    }\n";
            }
            src += "}\n";
            return src;
        }

        bool EnsureJvm(std::string* error)
        {
            if (m_Env) return true;
            if (m_JvmFailed || !m_Located)
            {
                if (error) *error = m_Located ? "JVM failed to start (see Log)" : m_LocateError;
                return false;
            }
            m_JvmFailed = true;
            // Barındırıcı sınıfları (bir kez derlenir, önbellekte)
            fs::path srcDir = FileSystem::ResolveResourcePath("Resources/Scripting/Java/conqueror");
            std::error_code ec;
            std::vector<fs::path> sources;
            std::string all;
            for (auto& e : fs::directory_iterator(srcDir, ec))
            {
                if (e.path().extension() != ".java") continue;
                sources.push_back(e.path());
            }
            std::sort(sources.begin(), sources.end());
            for (auto& s : sources) { std::string t; ReadFile(s, t); all += t; }
            if (sources.empty())
            {
                CQ_CORE_ERROR("[Java] Host sources not found in {0}", srcDir.string());
                if (error) *error = "Java host sources missing";
                return false;
            }
            std::string bridge = GenerateBridgeSource();
            size_t hash = std::hash<std::string>{}(all + bridge + m_JavaHome.string());
            m_HostDir = fs::temp_directory_path(ec) / "ConquerorEngine" / ("java-host-" + std::to_string(hash));
            if (!fs::exists(m_HostDir / "conqueror" / "Host.class", ec))
            {
                fs::create_directories(m_HostDir / "src", ec);
                std::ofstream(m_HostDir / "src" / "NativeBridge.java") << bridge;
                std::string cmd = "\"" + m_Javac.string() + "\" -J-Duser.language=en -encoding UTF-8 -Xlint:none -nowarn -d \"" + m_HostDir.string() + "\"";
                for (auto& s : sources) cmd += " \"" + s.string() + "\"";
                cmd += " \"" + (m_HostDir / "src" / "NativeBridge.java").string() + "\"";
                int code = 0;
                double t0 = Now();
                std::string log = RunCommand(cmd, code);
                if (code != 0)
                {
                    CQ_CORE_ERROR("[Java] Host compilation failed:\n{0}", log);
                    if (error) *error = "Java host compilation failed";
                    return false;
                }
                CQ_CORE_INFO("[Java] Host classes built in {0:.2f}s", Now() - t0);
            }

            // libjvm
#ifdef _WIN32
            HMODULE lib = LoadLibraryW(m_LibJvm.wstring().c_str());
            void* create = lib ? (void*)GetProcAddress(lib, "JNI_CreateJavaVM") : nullptr;
#else
            void* lib = dlopen(m_LibJvm.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
            void* create = lib ? dlsym(lib, "JNI_CreateJavaVM") : nullptr;
#endif
            if (!create)
            {
                CQ_CORE_ERROR("[Java] Cannot load {0}", m_LibJvm.string());
                if (error) *error = "libjvm could not be loaded";
                return false;
            }
            using CreateFn = jint (JNICALL*)(JavaVM**, void**, void*);
            std::string classPath = "-Djava.class.path=" + m_HostDir.string();
            std::string userLang = "-Duser.language=en";
            JavaVMOption options[4];
            options[0].optionString = classPath.data();
            options[1].optionString = const_cast<char*>("-Xrs");                 // motorun sinyallerine dokunma
            options[2].optionString = userLang.data();
            options[3].optionString = const_cast<char*>("-Dfile.encoding=UTF-8");
            JavaVMInitArgs args{};
            args.version = JNI_VERSION_10;
            args.nOptions = 4;
            args.options = options;
            args.ignoreUnrecognized = JNI_TRUE;
            // JVM süreç yerel ayarını (setlocale(LC_ALL, "")) değiştirir: motor sayı biçimlemesi/ayrıştırması
            // "C" yerel ayarına bağlıdır ("1.5", "1,5" değil) -> oluşturduktan sonra geri yükle
            std::string savedLocale = std::setlocale(LC_ALL, nullptr) ? std::setlocale(LC_ALL, nullptr) : "C";
            jint created = ((CreateFn)create)(&m_Jvm, (void**)&m_Env, &args);
            std::setlocale(LC_ALL, savedLocale.c_str());
            if (created != JNI_OK || !m_Env)
            {
                CQ_CORE_ERROR("[Java] JNI_CreateJavaVM failed");
                m_Env = nullptr;
                if (error) *error = "JVM could not be created";
                return false;
            }
            jclass local = m_Env->FindClass("conqueror/Host");
            if (!local || m_Env->ExceptionCheck())
            {
                m_Env->ExceptionClear();
                CQ_CORE_ERROR("[Java] conqueror.Host not found on class path");
                m_Env = nullptr;
                if (error) *error = "Java host class missing";
                return false;
            }
            m_Host = (jclass)m_Env->NewGlobalRef(local);
            m_Env->DeleteLocalRef(local);
            JNINativeMethod natives[] = {
                { const_cast<char*>("callNative"), const_cast<char*>("(ILjava/lang/String;)Ljava/lang/String;"), (void*)&NativeCall },
                { const_cast<char*>("log"), const_cast<char*>("(ILjava/lang/String;)V"), (void*)&NativeLog },
            };
            if (m_Env->RegisterNatives(m_Host, natives, 2) != 0)
            {
                m_Env->ExceptionClear();
                CQ_CORE_ERROR("[Java] RegisterNatives failed");
                m_Env = nullptr;
                return false;
            }
            auto method = [&](const char* name, const char* sig) {
                jmethodID id = m_Env->GetStaticMethodID(m_Host, name, sig);
                if (!id) { m_Env->ExceptionClear(); CQ_CORE_ERROR("[Java] Host method {0}{1} not found", name, sig); }
                return id;
            };
            m_Compile = method("compile", "([Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
            m_CheckSyntax = method("checkSyntax", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
            m_LoadClasses = method("loadClasses", "(Ljava/lang/String;)Z");
            m_Reset = method("reset", "()V");
            m_CreateInstance = method("createInstance", "(Ljava/lang/String;J)I");
            m_DestroyInstance = method("destroyInstance", "(I)V");
            m_HasMethod = method("hasMethod", "(ILjava/lang/String;)Z");
            m_Invoke = method("invoke", "(ILjava/lang/String;Ljava/lang/String;)Z");
            m_SetField = method("setField", "(ILjava/lang/String;Ljava/lang/String;)Z");
            m_GetField = method("getField", "(ILjava/lang/String;)Ljava/lang/String;");
            m_Describe = method("describe", "(Ljava/lang/String;)Ljava/lang/String;");
            m_GetLastError = method("getLastError", "()Ljava/lang/String;");
            if (!m_Compile || !m_CheckSyntax || !m_LoadClasses || !m_Reset || !m_CreateInstance || !m_DestroyInstance || !m_HasMethod ||
                !m_Invoke || !m_SetField || !m_GetField || !m_Describe || !m_GetLastError)
            {
                m_Env = nullptr;
                if (error) *error = "Java host methods missing";
                return false;
            }
            m_JvmFailed = false;
            CQ_CORE_INFO("[Java] JVM started ({0})", m_JavaHome.string());
            return true;
        }

        // Projedeki .java dosyalarını derle (içerik değiştiyse)
        bool EnsureCompiled(std::string* error)
        {
            fs::path projectDir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            std::vector<fs::path> sources;
            fs::path assets = projectDir / "Assets";
            if (!projectDir.empty() && fs::exists(assets, ec))
                for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file(ec) && it->path().extension() == ".java")
                        sources.push_back(it->path());
            std::sort(sources.begin(), sources.end());
            std::string all;
            for (auto& s : sources) { std::string t; ReadFile(s, t); all += s.string() + "\n" + t; }
            size_t hash = std::hash<std::string>{}(all + m_HostDir.string());
            m_ClassDir = (projectDir.empty() ? fs::temp_directory_path(ec) / "ConquerorEngine" : projectDir / "Library") / "ScriptAssemblies" / "java";
            if (m_Compiled && hash == m_SourceHash)
                return true;
            if (sources.empty())
            {
                if (error) *error = "No Java scripts in the project";
                return false;
            }
            // Eski sınıfları temizle (silinmiş kaynaklar kalmasın)
            fs::remove_all(m_ClassDir, ec);
            fs::create_directories(m_ClassDir, ec);
            jclass stringClass = m_Env->FindClass("java/lang/String");
            jobjectArray array = m_Env->NewObjectArray((jsize)sources.size(), stringClass, nullptr);
            for (size_t i = 0; i < sources.size(); ++i)
            {
                jstring s = m_Env->NewStringUTF(sources[i].string().c_str());
                m_Env->SetObjectArrayElement(array, (jsize)i, s);
                m_Env->DeleteLocalRef(s);
            }
            jstring out = m_Env->NewStringUTF(m_ClassDir.string().c_str());
            jstring cp = m_Env->NewStringUTF(m_HostDir.string().c_str());
            double t0 = Now();
            auto result = (jstring)m_Env->CallStaticObjectMethod(m_Host, m_Compile, array, out, cp);
            m_Env->DeleteLocalRef(array);
            m_Env->DeleteLocalRef(out);
            m_Env->DeleteLocalRef(cp);
            m_Env->DeleteLocalRef(stringClass);
            if (CheckException() || !result)
            {
                if (error) *error = "Java compilation failed";
                return false;
            }
            JSON::Value j = JSON::Parse(JStr(m_Env, result));
            m_Env->DeleteLocalRef(result);
            std::vector<std::string> errors;
            CollectDiagnostics(j, "", errors, nullptr);
            bool ok = j.MapVal.count("ok") && j.MapVal.at("ok").BoolVal;
            if (!ok)
            {
                for (size_t i = 0; i < errors.size() && i < 20; ++i) CQ_CORE_ERROR("[Java] {0}", errors[i]);
                if (errors.empty()) CQ_CORE_ERROR("[Java] Compilation failed: {0}", LastError());
                if (error) *error = errors.empty() ? "Java compilation failed" : errors.front();
                m_Compiled = false;
                return false;
            }
            CQ_CORE_INFO("[Java] Compiled {0} script file{1} in {2:.2f}s", sources.size(), sources.size() == 1 ? "" : "s", Now() - t0);
            m_SourceHash = hash;
            m_Compiled = true;
            return true;
        }

        bool Describe(std::string* error)
        {
            if (!EnsureJvm(error) || !EnsureCompiled(error)) return false;
            if (m_DescribedHash == m_SourceHash && !m_Description.empty()) return true;
            jstring dir = m_Env->NewStringUTF(m_ClassDir.string().c_str());
            auto result = (jstring)m_Env->CallStaticObjectMethod(m_Host, m_Describe, dir);
            m_Env->DeleteLocalRef(dir);
            if (CheckException() || !result)
            {
                if (error) *error = LastError();
                return false;
            }
            JSON::Value list = JSON::Parse(JStr(m_Env, result));
            m_Env->DeleteLocalRef(result);
            m_Description.clear();
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
            m_DescribedHash = m_SourceHash;
            return true;
        }
    };

    std::unique_ptr<ScriptRuntime> CreateJavaRuntime()
    {
        return std::make_unique<JavaRuntime>();
    }
#else
    std::unique_ptr<ScriptRuntime> CreateJavaRuntime()
    {
        return nullptr;
    }
#endif
}
