#include "Scripting/ScriptRuntime.h"
#include "Scripting/NativeModules.h"
#include "Scripting/DataAssets.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Utils/JSON.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#define CQ_POPEN _popen
#define CQ_PCLOSE _pclose
#else
#include <dlfcn.h>
#define CQ_POPEN popen
#define CQ_PCLOSE pclose
#endif

// Yerel (native) betikler: C++, C, Rust ve Zig. Projedeki kaynaklar dilin derleyicisiyle paylaşımlı bir kitaplığa
// derlenip yüklenir. Motorla sınır her dilde aynı C ABI'dır (cq_create / cq_invoke / cq_plugin_describe ...);
// dil başına SDK: Resources/Scripting/{Cpp,C,Rust,Zig}. Değişiklikte arka planda yeniden derlenip örnekler taşınır.

namespace Conqueror::Scripting
{
    namespace
    {
        namespace fs = std::filesystem;

        struct CQNativeValue
        {
            int32_t Type;
            int32_t Dim;
            int64_t I;
            double X, Y, Z, W;
            const char* Str;
            int32_t StrLen;
            int32_t Pad;
        };
        static_assert(sizeof(CQNativeValue) == 64, "must match Conqueror/Script.h");

        struct CQHostApi
        {
            int Version;
            int (*FindNative)(const char* name);
            int (*CallNative)(int index, int argc, const CQNativeValue* args, CQNativeValue* result);
            void (*Log)(int level, const char* message);
        };

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
#ifdef _WIN32
            std::string full = cmd + " 2>&1";
#else
            // Derleyici mesajları İngilizce olsun (ayrıştırma "error:" bekler; GCC yerelleştirir: "hata:")
            std::string full = "LC_ALL=C LANG=C LANGUAGE=en " + cmd + " 2>&1";
#endif
            FILE* pipe = CQ_POPEN(full.c_str(), "r");
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

        std::string Quote(const fs::path& p) { return "\"" + p.string() + "\""; }

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

        thread_local std::string t_Result;

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
                auto& e = v.AsMap()->Entries;
                auto num = [&](const char* k) { auto it = e.find(k); return it != e.end() && it->second.IsNumber(); };
                if (num("x") && num("y"))
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

        // ── Eklentiye verilen API ──
        int HostFindNative(const char* name)
        {
            const auto& fns = NativeRegistry::Get().GetFunctions();
            for (size_t i = 0; i < fns.size(); ++i)
                if (fns[i].Name == name) return (int)i;
            return -1;
        }

        int HostCallNative(int index, int argc, const CQNativeValue* args, CQNativeValue* result)
        {
            const auto& fns = NativeRegistry::Get().GetFunctions();
            if (index < 0 || index >= (int)fns.size()) return 0;
            const NativeFunctionInfo& f = fns[(size_t)index];
            int slots = std::max(argc, f.Arity < 0 ? argc : f.Arity);
            std::vector<CQS::Value> values((size_t)std::max(slots, 1));
            for (int i = 0; i < argc; ++i)
                values[(size_t)i] = FromNative(args[i]);
            CQS::Value r = f.Function(argc, values.data());
            ToNative(r, *result, t_Result);
            return 1;
        }

        // Dil başına günlük öneki ("[C++] ...", "[Rust] ...")
        template<int Lang>
        void HostLog(int level, const char* message)
        {
            static const char* kPrefix[] = { "[C++]", "[C]", "[Rust]", "[Zig]" };
            if (level >= 2) CQ_CORE_ERROR("{0} {1}", kPrefix[Lang], message);
            else if (level == 1) CQ_CORE_WARN("{0} {1}", kPrefix[Lang], message);
            else CQ_CORE_INFO("{0} {1}", kPrefix[Lang], message);
        }

        const CQHostApi g_HostApis[] = {
            { 1, &HostFindNative, &HostCallNative, &HostLog<0> },
            { 1, &HostFindNative, &HostCallNative, &HostLog<1> },
            { 1, &HostFindNative, &HostCallNative, &HostLog<2> },
            { 1, &HostFindNative, &HostCallNative, &HostLog<3> },
        };

        // Paylaşımlı kitaplık
        struct Plugin
        {
            void* Handle = nullptr;
            fs::path Path;
            int (*Version)() = nullptr;
            void (*Init)(const CQHostApi*) = nullptr;
            const char* (*Describe)() = nullptr;
            void* (*Create)(const char*, uint32_t) = nullptr;
            void (*Destroy)(void*) = nullptr;
            int (*HasMethod)(void*, const char*) = nullptr;
            int (*Invoke)(void*, const char*, const CQNativeValue*, int) = nullptr;
            int (*SetField)(void*, const char*, const CQNativeValue*) = nullptr;
            int (*GetField)(void*, const char*, CQNativeValue*, const char**) = nullptr;
            const char* (*LastError)() = nullptr;

            bool Load(const fs::path& path, const CQHostApi* api, std::string& error)
            {
                Path = path;
#ifdef _WIN32
                HMODULE h = LoadLibraryW(path.wstring().c_str());
                Handle = (void*)h;
                auto sym = [&](const char* n) { return h ? (void*)GetProcAddress(h, n) : nullptr; };
#else
                Handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
                auto sym = [&](const char* n) { return Handle ? dlsym(Handle, n) : nullptr; };
                if (!Handle) { error = dlerror(); return false; }
#endif
                if (!Handle) { error = "cannot load " + path.string(); return false; }
                Version = (int (*)())sym("cq_plugin_version");
                Init = (void (*)(const CQHostApi*))sym("cq_plugin_init");
                Describe = (const char* (*)())sym("cq_plugin_describe");
                Create = (void* (*)(const char*, uint32_t))sym("cq_create");
                Destroy = (void (*)(void*))sym("cq_destroy");
                HasMethod = (int (*)(void*, const char*))sym("cq_has_method");
                Invoke = (int (*)(void*, const char*, const CQNativeValue*, int))sym("cq_invoke");
                SetField = (int (*)(void*, const char*, const CQNativeValue*))sym("cq_set_field");
                GetField = (int (*)(void*, const char*, CQNativeValue*, const char**))sym("cq_get_field");
                LastError = (const char* (*)())sym("cq_last_error");
                if (!Version || !Init || !Describe || !Create || !Destroy || !HasMethod || !Invoke || !SetField || !GetField || !LastError)
                {
                    error = "script library is missing entry points";
                    Unload();
                    return false;
                }
                Init(api);
                return true;
            }

            void Unload()
            {
                if (!Handle) return;
#ifdef _WIN32
                FreeLibrary((HMODULE)Handle);
#else
                dlclose(Handle);
#endif
                Handle = nullptr;
            }
        };

        struct Instance
        {
            void* Object = nullptr;
            uint32_t Entity = 0xFFFFFFFF;
            std::string ClassName;
        };
    }

    // ═══════════════════════════════════════════════ Araç zincirleri
    namespace
    {
        enum class NativeLang : int { Cpp = 0, C, Rust, Zig };

        bool IsIdentifier(const std::string& name)
        {
            if (name.empty() || !(std::isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
            for (char c : name) if (!std::isalnum((unsigned char)c) && c != '_') return false;
            return true;
        }

        std::string LibExtension()
        {
#ifdef _WIN32
            return ".dll";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return ".so";
#endif
        }

        bool CommandExists(const std::string& cmd)
        {
            int code = 0;
#ifdef _WIN32
            RunCommand("where " + cmd, code);
#else
            RunCommand("command -v " + cmd, code);
#endif
            return code == 0;
        }

        std::string Slashes(const fs::path& p)
        {
            std::string s = p.string();
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }

        // gcc/clang/zig: "dosya:satır:sütun: error: mesaj"  msvc: "dosya(satır): error C1234: mesaj"
        // rustc: "error[E0425]: mesaj" + sonraki " --> dosya:satır:sütun"
        std::vector<std::string> ParseDiagnostics(const std::string& log, const std::string& matchFile, const std::string& displayName)
        {
            std::vector<std::string> out;
            static const std::regex gcc(R"(^(.*?):(\d+):(\d+): (?:fatal )?error: (.*)$)");
            static const std::regex msvc(R"(^(.*?)\((\d+)(?:,(\d+))?\)\s*: (?:fatal )?error \w+: (.*)$)");
            static const std::regex rustError(R"(^error(?:\[\w+\])?: (.*)$)");
            static const std::regex rustLocation(R"(^\s*--> (.*?):(\d+):(\d+)$)");
            std::stringstream ss(log);
            std::string line, pendingRust;
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::smatch m;
                if (std::regex_match(line, m, rustError))
                {
                    pendingRust = m[1].str();
                    continue;
                }
                if (!pendingRust.empty() && std::regex_match(line, m, rustLocation))
                {
                    std::string file = fs::path(m[1].str()).filename().string();
                    if (matchFile.empty() || file == matchFile)
                        out.push_back((displayName.empty() ? file : displayName) + ": Line " + m[2].str() + ":" + m[3].str() + ": " + pendingRust);
                    pendingRust.clear();
                    continue;
                }
                if (std::regex_match(line, m, gcc) || std::regex_match(line, m, msvc))
                {
                    std::string file = fs::path(m[1].str()).filename().string();
                    if (!matchFile.empty() && file != matchFile) continue;
                    out.push_back((displayName.empty() ? file : displayName) + ": Line " + m[2].str() + ":" + (m[3].matched ? m[3].str() : "1") + ": " + m[4].str());
                }
            }
            // Konumsuz rustc hatası (ör. bağlama)
            if (out.empty() && !pendingRust.empty() && matchFile.empty())
                out.push_back(pendingRust);
            return out;
        }

        struct Toolchain
        {
            virtual ~Toolchain() = default;
            virtual NativeLang Lang() const = 0;
            virtual const char* Name() const = 0;
            virtual const char* Id() const = 0;
            virtual std::vector<std::string> Extensions() const = 0;
            virtual bool Locate() = 0;
            virtual std::string Unavailable() const = 0;
            virtual std::string Compiler() const = 0;
            virtual std::string Template(const std::string& className) const = 0;
            // SDK dosyalarının içeriği (değişince yeniden derlenir)
            virtual std::string SdkFingerprint() const = 0;
            // Tüm kaynakları paylaşımlı kitaplığa derle
            virtual bool Build(const std::vector<fs::path>& sources, const fs::path& outDir, const fs::path& lib, std::string& log) = 0;
            // Söz dizimi / tür denetimi: düzenlenen dosya içeriğiyle (gerekirse tüm kaynaklarla); log + eşleşecek dosya adı
            virtual bool Check(const std::vector<fs::path>& sources, const std::string& name, const std::string& source,
                               const fs::path& workDir, std::string& log, std::string& matchFile) = 0;
            // Projedeki başlık dosyaları (değişince yeniden derlenir); C++'ta DataAsset sınıfı tanımlayan başlıklar ayrıca
            // derlemeye eklenir (hiçbir betik içermese de sınıf kayıtlı olur, Data Asset Inspector'ı alanları görür)
            virtual std::vector<std::string> HeaderExtensions() const { return {}; }
            std::vector<fs::path> DataHeaders;
            std::vector<fs::path> ExtraIncludes;   // Native Script Module klasörleri
        };

        // Motor fonksiyonları (dosyaya yazılabilir adlar)
        std::vector<const NativeFunctionInfo*> BridgeFunctions()
        {
            std::vector<const NativeFunctionInfo*> out;
            for (const auto& f : NativeRegistry::Get().GetFunctions())
                if (IsIdentifier(f.Name))
                    out.push_back(&f);
            return out;
        }

        void WriteIfChanged(const fs::path& path, const std::string& content)
        {
            std::string existing;
            ReadFile(path, existing);
            if (existing != content)
                std::ofstream(path, std::ios::binary) << content;
        }

        // ─────────────── C++
        struct CppToolchain final : Toolchain
        {
            std::string m_Compiler;
            bool m_IsMsvc = false;

            NativeLang Lang() const override { return NativeLang::Cpp; }
            const char* Name() const override { return "C++"; }
            const char* Id() const override { return "cpp"; }
            std::vector<std::string> Extensions() const override { return { ".cpp", ".cc", ".cxx" }; }
            std::vector<std::string> HeaderExtensions() const override { return { ".h", ".hpp", ".hh", ".hxx" }; }
            std::string Unavailable() const override { return "No C++ compiler found (install clang/gcc, or set CXX)."; }
            std::string Compiler() const override { return m_Compiler; }

            bool Locate() override
            {
                if (const char* cxx = std::getenv("CXX"); cxx && *cxx)
                {
                    m_Compiler = cxx;
                    m_IsMsvc = std::string(cxx).find("cl") != std::string::npos && std::string(cxx).find("clang++") == std::string::npos;
                    return true;
                }
#ifdef _WIN32
                for (const char* c : { "clang-cl", "cl", "clang++", "g++" })
                    if (CommandExists(c)) { m_Compiler = c; m_IsMsvc = std::strcmp(c, "cl") == 0 || std::strcmp(c, "clang-cl") == 0; return true; }
#else
                for (const char* c : { "c++", "clang++", "g++" })
                    if (CommandExists(c)) { m_Compiler = c; return true; }
#endif
                return false;
            }

            fs::path IncludeDir() const { return FileSystem::ResolveResourcePath("Resources/Scripting/Cpp/include"); }

            std::string SdkFingerprint() const override
            {
                std::string api;
                ReadFile(IncludeDir() / "Conqueror" / "Script.h", api);
                return api;
            }

            std::string Template(const std::string& className) const override
            {
                return "#include <Conqueror/Script.h>\n"
                       "#include \"NativeBridge.g.h\"\n\n"
                       "class " + className + " : public cq::Script\n{\n"
                       "    CQ_SCRIPT_CLASS(" + className + ")\n"
                       "public:\n"
                       "    // Inspector fields\n"
                       "    CQ_FIELD(float, speed, 1.0f)\n\n"
                       "    void Start() override\n    {\n    }\n\n"
                       "    void Update(float dt) override\n    {\n        (void)dt;\n    }\n};\n"
                       "CQ_SCRIPT(" + className + ")\n";
            }

            // NativeBridge.g.h ve eklenti giriş dosyası
            void Generate(const fs::path& dir) const
            {
                std::error_code ec;
                fs::create_directories(dir, ec);
                std::string header = "// Auto-generated: engine nativebridge functions (do not edit)\n#pragma once\n#include <Conqueror/Script.h>\n\nnamespace cq\n{\n";
                for (const NativeFunctionInfo* f : BridgeFunctions())
                {
                    if (f->Name == "Log" || f->Name == "Script" || f->Name == "Value") continue;
                    int arity = f->Arity < 0 ? 8 : std::min(f->Arity, 16);
                    std::string params, args;
                    for (int a = 0; a < arity; ++a)
                    {
                        params += (a ? ", " : "") + std::string("Value a") + std::to_string(a) + " = {}";
                        args += (a ? ", " : "") + std::string("a") + std::to_string(a);
                    }
                    header += "    inline Value " + f->Name + "(" + params + ") { static const int i = ResolveNative(\"" + f->Name + "\"); return CallNative(i, { " + args + " }); }\n";
                }
                for (const auto& [cname, value] : NativeRegistry::Get().GetConstants())
                {
                    if (!value.IsMap()) continue;
                    header += "    namespace " + cname + "\n    {\n";
                    std::vector<std::pair<std::string, const CQS::Value*>> entries;
                    for (auto& [k, v] : value.AsMap()->Entries) entries.emplace_back(k, &v);
                    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
                    for (auto& [k, v] : entries)
                    {
                        if (!IsIdentifier(k) || !v->IsNumber()) continue;
                        if (v->IsInt()) header += "        inline constexpr long long " + k + " = " + std::to_string(v->AsInt()) + ";\n";
                        else
                        {
                            char buf[64];
                            std::snprintf(buf, sizeof(buf), "%.17g", v->AsFloat());
                            header += "        inline constexpr double " + k + " = " + buf + ";\n";
                        }
                    }
                    header += "    }\n";
                }
                // Data Asset: .cqdata dosyasını T olarak yükle (alanlar dosyadaki, yoksa sınıftaki varsayılan değerlerle)
                if (NativeRegistry::Get().Find("DataAssetGet") && NativeRegistry::Get().Find("LoadDataAsset"))
                    header += "    template<typename T>\n"
                              "    T LoadDataAsset(const std::string& path)\n"
                              "    {\n"
                              "        static_assert(std::is_base_of_v<DataAsset, T>, \"LoadDataAsset<T>: T must derive from cq::DataAsset\");\n"
                              "        T asset;\n"
                              "        asset.assetPath = path;\n"
                              "        auto it = detail::Classes().find(detail::ClassAccess<T>::Name());\n"
                              "        if (it == detail::Classes().end()) return asset;\n"
                              "        for (auto& f : it->second.Fields)\n"
                              "        {\n"
                              "            Value v = DataAssetGet(Value(path), Value(f.Name));\n"
                              "            if (!v.IsNull()) f.Set(&asset, v);\n"
                              "        }\n"
                              "        return asset;\n"
                              "    }\n";
                header += "}\n";
                WriteIfChanged(dir / "NativeBridge.g.h", header);
                WriteIfChanged(dir / "__cq_plugin_main.cpp", "// Auto-generated\n#define CQ_PLUGIN_IMPLEMENTATION\n#include <Conqueror/Script.h>\n");
            }

            bool Build(const std::vector<fs::path>& sources, const fs::path& outDir, const fs::path& lib, std::string& log) override
            {
                Generate(outDir);
                // DataAsset sınıfı tanımlayan başlıklar (Unreal'daki gibi sınıf başlıkta; betikler #include ile kullanır)
                std::string dataUnit = "// Auto-generated: Data Asset class headers of the project (for registration)\n";
                for (const auto& h : DataHeaders)
                    dataUnit += "#include \"" + h.generic_string() + "\"\n";
                WriteIfChanged(outDir / "__cq_data_assets.cpp", dataUnit);
                const fs::path includeDir = IncludeDir();
                std::string cmd;
                if (m_IsMsvc)
                {
                    cmd = m_Compiler + " /nologo /std:c++20 /O2 /EHsc /LD /MD /I" + Quote(includeDir) + " /I" + Quote(outDir) + " /Fo" + Quote(outDir / "") + " /Fe" + Quote(lib);
                    for (const auto& inc : ExtraIncludes) cmd += " /I" + Quote(inc);
                    cmd += " " + Quote(outDir / "__cq_plugin_main.cpp") + " " + Quote(outDir / "__cq_data_assets.cpp");
                    for (auto& s : sources) cmd += " /Tp" + Quote(s);
                }
                else
                {
#ifdef __APPLE__
                    const char* shared = "-dynamiclib";
#else
                    const char* shared = "-shared";
#endif
                    cmd = m_Compiler + " -std=c++20 -O2 -g -fPIC " + shared + " -fvisibility=hidden -I" + Quote(includeDir) + " -I" + Quote(outDir);
                    for (const auto& inc : ExtraIncludes) cmd += " -I" + Quote(inc);
                    cmd += " -o " + Quote(lib) + " " + Quote(outDir / "__cq_plugin_main.cpp") + " " + Quote(outDir / "__cq_data_assets.cpp") + " -x c++";
                    for (auto& s : sources) cmd += " " + Quote(s);
                }
                int code = 0;
                log = RunCommand(cmd, code);
                std::error_code ec;
                return code == 0 && fs::exists(lib, ec);
            }

            bool Check(const std::vector<fs::path>&, const std::string& name, const std::string& source, const fs::path& workDir,
                       std::string& log, std::string& matchFile) override
            {
                Generate(workDir.parent_path() / "cpp");
                fs::path file = workDir / fs::path(name).filename();
                std::ofstream(file, std::ios::binary) << source;
                matchFile = file.filename().string();
                const fs::path gen = workDir.parent_path() / "cpp";
                std::string cmd = m_IsMsvc
                    ? m_Compiler + " /nologo /std:c++20 /EHsc /Zs /I" + Quote(IncludeDir()) + " /I" + Quote(gen) + " " + Quote(file)
                    : m_Compiler + " -std=c++20 -fsyntax-only -Wall -Wextra -I" + Quote(IncludeDir()) + " -I" + Quote(gen) + " -x c++ " + Quote(file);
                int code = 0;
                log = RunCommand(cmd, code);
                return code == 0;
            }
        };

        // ─────────────── C (gerçek C: C23, C++ olarak derlenmez)
        struct CToolchain final : Toolchain
        {
            std::string m_Compiler;
            bool m_IsMsvc = false;

            NativeLang Lang() const override { return NativeLang::C; }
            const char* Name() const override { return "C"; }
            const char* Id() const override { return "c"; }
            std::vector<std::string> Extensions() const override { return { ".c" }; }
            std::string Unavailable() const override { return "No C compiler found (install gcc/clang, or set CC)."; }
            std::string Compiler() const override { return m_Compiler; }

            bool Locate() override
            {
                if (const char* cc = std::getenv("CC"); cc && *cc)
                {
                    m_Compiler = cc;
                    m_IsMsvc = std::string(cc) == "cl" || std::string(cc) == "clang-cl";
                    return true;
                }
#ifdef _WIN32
                for (const char* c : { "clang-cl", "cl", "clang", "gcc" })
                    if (CommandExists(c)) { m_Compiler = c; m_IsMsvc = std::strcmp(c, "cl") == 0 || std::strcmp(c, "clang-cl") == 0; return true; }
#else
                for (const char* c : { "cc", "clang", "gcc" })
                    if (CommandExists(c)) { m_Compiler = c; return true; }
#endif
                return false;
            }

            fs::path IncludeDir() const
            {
                std::error_code ec;
                return fs::absolute(FileSystem::ResolveResourcePath("Resources/Scripting/C/include"), ec);
            }

            std::string SdkFingerprint() const override
            {
                std::string api;
                ReadFile(IncludeDir() / "conqueror.h", api);
                return api;
            }

            std::string Template(const std::string& className) const override
            {
                return "#include <conqueror.h>\n"
                       "#include \"nativebridge.h\"\n\n"
                       "// " + className + ": C script. Fields appear in the Inspector; methods are bound to the engine with CQ_METHODS.\n"
                       "typedef struct " + className + "\n{\n"
                       "    CQ_OBJECT;          /* must be the first member */\n"
                       "    float speed;\n"
                       "} " + className + ";\n\n"
                       "static void " + className + "_Start(" + className + "* self, const cq_value* args, int argc)\n{\n"
                       "    (void)self; (void)args; (void)argc;\n}\n\n"
                       "static void " + className + "_Update(" + className + "* self, const cq_value* args, int argc)\n{\n"
                       "    float dt = cq_arg_float(args, argc, 0);\n"
                       "    (void)self; (void)dt;\n}\n\n"
                       "CQ_FIELDS(" + className + ", CQ_FLOAT(" + className + ", speed, 1.0))\n"
                       "CQ_METHODS(" + className + ", CQ_ON(" + className + ", Start), CQ_ON(" + className + ", Update))\n"
                       "CQ_CLASS(" + className + ")\n";
            }

            // nativebridge.h: her motor fonksiyonu bir makro (C sayıları / dizeleri kendiliğinden dönüştürülür)
            void Generate(const fs::path& dir, const std::vector<fs::path>& sources) const
            {
                std::error_code ec;
                fs::create_directories(dir, ec);
                std::string header = "/* Auto-generated: engine nativebridge functions (do not edit) */\n#ifndef CQ_NATIVEBRIDGE_H\n#define CQ_NATIVEBRIDGE_H\n#include <conqueror.h>\n\n";
                for (const NativeFunctionInfo* f : BridgeFunctions())
                {
                    header += "static int cq__i_" + f->Name + " = -2;\n";
                    header += "#define " + f->Name + "(...) CQ__CALL(&cq__i_" + f->Name + ", \"" + f->Name + "\" __VA_OPT__(,) __VA_ARGS__)\n";
                }
                for (const auto& [cname, value] : NativeRegistry::Get().GetConstants())
                {
                    if (!value.IsMap() || !IsIdentifier(cname)) continue;
                    std::vector<std::pair<std::string, const CQS::Value*>> entries;
                    for (auto& [k, v] : value.AsMap()->Entries) entries.emplace_back(k, &v);
                    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
                    for (auto& [k, v] : entries)
                    {
                        if (!IsIdentifier(k) || !v->IsNumber()) continue;
                        char buf[64];
                        if (v->IsInt()) std::snprintf(buf, sizeof(buf), "%lldLL", (long long)v->AsInt());
                        else std::snprintf(buf, sizeof(buf), "%.17g", v->AsFloat());
                        header += "#define " + cname + "_" + k + " (" + buf + ")\n";
                    }
                }
                header += "\n#endif\n";
                WriteIfChanged(dir / "nativebridge.h", header);

                // Sınıf kaydı: kaynaklardaki CQ_CLASS(Ad) bildirimleri
                static const std::regex classRe(R"(CQ_CLASS\(\s*([A-Za-z_]\w*)\s*\))");
                std::vector<std::string> classes;
                for (const auto& s : sources)
                {
                    std::string text;
                    ReadFile(s, text);
                    for (auto it = std::sregex_iterator(text.begin(), text.end(), classRe); it != std::sregex_iterator(); ++it)
                        if (std::find(classes.begin(), classes.end(), (*it)[1].str()) == classes.end())
                            classes.push_back((*it)[1].str());
                }
                std::string main = "/* Auto-generated */\n#define CQ_PLUGIN_IMPLEMENTATION\n#include <conqueror.h>\n\n";
                for (const auto& c : classes)
                    main += "extern const cq_class_desc cq_class_" + c + ";\n";
                main += "const cq_class_desc* const cq__classes[] = { ";
                for (const auto& c : classes)
                    main += "&cq_class_" + c + ", ";
                main += "NULL };\nconst int cq__class_count = " + std::to_string(classes.size()) + ";\n";
                WriteIfChanged(dir / "__cq_main.c", main);
            }

            std::string Flags() const { return m_IsMsvc ? "/std:clatest /Zc:preprocessor" : "-std=c2x"; }

            bool Build(const std::vector<fs::path>& sources, const fs::path& outDir, const fs::path& lib, std::string& log) override
            {
                Generate(outDir, sources);
                std::string cmd;
                if (m_IsMsvc)
                {
                    cmd = m_Compiler + " /nologo " + Flags() + " /O2 /LD /MD /I" + Quote(IncludeDir()) + " /I" + Quote(outDir) + " /Fo" + Quote(outDir / "") + " /Fe" + Quote(lib);
                    for (const auto& inc : ExtraIncludes) cmd += " /I" + Quote(inc);
                    cmd += " /Tc" + Quote(outDir / "__cq_main.c");
                    for (auto& s : sources) cmd += " /Tc" + Quote(s);
                }
                else
                {
#ifdef __APPLE__
                    const char* shared = "-dynamiclib";
#else
                    const char* shared = "-shared";
#endif
                    cmd = m_Compiler + " " + Flags() + " -O2 -g -fPIC " + shared + " -fvisibility=hidden -I" + Quote(IncludeDir()) + " -I" + Quote(outDir);
                    for (const auto& inc : ExtraIncludes) cmd += " -I" + Quote(inc);
                    cmd += " -o " + Quote(lib) + " -x c " + Quote(outDir / "__cq_main.c");
                    for (auto& s : sources) cmd += " " + Quote(s);
                    cmd += " -lm";
                }
                int code = 0;
                log = RunCommand(cmd, code);
                std::error_code ec;
                return code == 0 && fs::exists(lib, ec);
            }

            bool Check(const std::vector<fs::path>& sources, const std::string& name, const std::string& source, const fs::path& workDir,
                       std::string& log, std::string& matchFile) override
            {
                const fs::path gen = workDir.parent_path() / "c";
                Generate(gen, sources);
                fs::path file = workDir / fs::path(name).filename();
                std::ofstream(file, std::ios::binary) << source;
                matchFile = file.filename().string();
                std::string cmd = m_IsMsvc
                    ? m_Compiler + " /nologo " + Flags() + " /Zs /I" + Quote(IncludeDir()) + " /I" + Quote(gen) + " /Tc" + Quote(file)
                    : m_Compiler + " " + Flags() + " -fsyntax-only -Wall -I" + Quote(IncludeDir()) + " -I" + Quote(gen) + " -x c " + Quote(file);
                int code = 0;
                log = RunCommand(cmd, code);
                return code == 0;
            }
        };

        // ─────────────── Rust (rustc ile tek crate; cargo gerekmez)
        struct RustToolchain final : Toolchain
        {
            std::string m_Compiler;

            NativeLang Lang() const override { return NativeLang::Rust; }
            const char* Name() const override { return "Rust"; }
            const char* Id() const override { return "rust"; }
            std::vector<std::string> Extensions() const override { return { ".rs" }; }
            std::string Unavailable() const override { return "No Rust compiler found (install Rust from rustup.rs, or set RUSTC)."; }
            std::string Compiler() const override { return m_Compiler; }

            bool Locate() override
            {
                if (const char* rc = std::getenv("RUSTC"); rc && *rc) { m_Compiler = rc; return true; }
                if (CommandExists("rustc")) { m_Compiler = "rustc"; return true; }
                // rustup varsayılan yeri (PATH'e eklenmemiş olabilir)
                const char* home = std::getenv("HOME");
#ifdef _WIN32
                if (!home) home = std::getenv("USERPROFILE");
                const fs::path candidate = home ? fs::path(home) / ".cargo" / "bin" / "rustc.exe" : fs::path();
#else
                const fs::path candidate = home ? fs::path(home) / ".cargo" / "bin" / "rustc" : fs::path();
#endif
                std::error_code ec;
                if (!candidate.empty() && fs::exists(candidate, ec)) { m_Compiler = Quote(candidate); return true; }
                return false;
            }

            // #[path] öznitelikleri lib.rs'ye göre çözülür: mutlak yol şart
            fs::path SdkDir() const
            {
                std::error_code ec;
                return fs::absolute(FileSystem::ResolveResourcePath("Resources/Scripting/Rust"), ec);
            }

            std::string SdkFingerprint() const override
            {
                std::string api;
                ReadFile(SdkDir() / "conqueror.rs", api);
                return api;
            }

            std::string Template(const std::string& className) const override
            {
                return "use crate::conqueror::*;\n"
                       "use crate::nativebridge::*;\n\n"
                       "// " + className + ": Rust script. Fields inside script! appear in the Inspector.\n"
                       "script! {\n"
                       "    pub struct " + className + " {\n"
                       "        speed: f32 = 1.0,\n"
                       "    }\n"
                       "}\n\n"
                       "impl Script for " + className + " {\n"
                       "    fn start(&mut self) {\n"
                       "    }\n\n"
                       "    fn update(&mut self, dt: f32) {\n"
                       "        let _ = dt;\n"
                       "    }\n"
                       "}\n";
            }

            static std::string ModuleName(const fs::path& source, size_t index)
            {
                std::string stem = source.stem().string();
                std::string out = "script_";
                for (char c : stem) out += std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
                return out + "_" + std::to_string(index);
            }

            // nativebridge.rs + lib.rs (modüller dosyaların kendisine #[path] ile bağlanır: hata satırları doğru dosyayı gösterir)
            void Generate(const fs::path& dir, const std::vector<fs::path>& sources, const fs::path* replaceFrom = nullptr, const fs::path* replaceTo = nullptr) const
            {
                std::error_code ec;
                fs::create_directories(dir, ec);
                std::string bridge = "// Auto-generated: engine nativebridge functions (do not edit)\n"
                                     "#![allow(non_snake_case, dead_code)]\nuse crate::conqueror::{call_cached, Value};\nuse std::sync::atomic::AtomicI32;\n\n";
                for (const NativeFunctionInfo* f : BridgeFunctions())
                    bridge += "pub fn " + f->Name + "(args: &[Value]) -> Value { static I: AtomicI32 = AtomicI32::new(-2); call_cached(&I, \"" + f->Name + "\", args) }\n";
                WriteIfChanged(dir / "nativebridge.rs", bridge);

                static const std::regex structRe(R"(script!\s*\{[^}]*?struct\s+([A-Za-z_]\w*))");
                std::string lib = "// Auto-generated\n#![allow(non_snake_case, non_camel_case_types, dead_code, unused_imports, unused_variables, unused_mut, unused_macros)]\n"
                                  "#[path = \"" + Slashes(SdkDir() / "conqueror.rs") + "\"]\n#[macro_use]\npub mod conqueror;\n"
                                  "#[path = \"" + Slashes(dir / "nativebridge.rs") + "\"]\npub mod nativebridge;\n";
                std::string classes;
                for (size_t i = 0; i < sources.size(); ++i)
                {
                    fs::path src = sources[i];
                    if (replaceFrom && replaceTo && src == *replaceFrom) src = *replaceTo;
                    const std::string mod = ModuleName(sources[i], i);
                    lib += "#[path = \"" + Slashes(src) + "\"]\npub mod " + mod + ";\n";
                    std::string text;
                    ReadFile(src, text);
                    for (auto it = std::sregex_iterator(text.begin(), text.end(), structRe); it != std::sregex_iterator(); ++it)
                        classes += "crate::" + mod + "::" + (*it)[1].str() + ", ";
                }
                lib += "export_plugin!(" + classes + ");\n";
                WriteIfChanged(dir / "lib.rs", lib);
            }

            bool Build(const std::vector<fs::path>& sources, const fs::path& outDir, const fs::path& lib, std::string& log) override
            {
                Generate(outDir, sources);
                std::string cmd = m_Compiler + " --edition 2021 --crate-type cdylib --crate-name cqscripts -C opt-level=2 -C debuginfo=1 --error-format=human -o "
                                  + Quote(lib) + " " + Quote(outDir / "lib.rs");
                int code = 0;
                log = RunCommand(cmd, code);
                std::error_code ec;
                return code == 0 && fs::exists(lib, ec);
            }

            bool Check(const std::vector<fs::path>& sources, const std::string& name, const std::string& source, const fs::path& workDir,
                       std::string& log, std::string& matchFile) override
            {
                // Düzenlenen dosya geçici kopyayla değiştirilip tüm crate yalnızca meta veriyle denetlenir
                const fs::path edited = workDir / fs::path(name).filename();
                std::ofstream(edited, std::ios::binary) << source;
                matchFile = edited.filename().string();
                std::vector<fs::path> all = sources;
                fs::path original;
                for (const auto& s : sources)
                    if (s.filename() == edited.filename()) original = s;
                if (original.empty()) all.push_back(edited);
                Generate(workDir, all, original.empty() ? nullptr : &original, &edited);
                std::string cmd = m_Compiler + " --edition 2021 --crate-type cdylib --crate-name cqcheck --emit=metadata -o "
                                  + Quote(workDir / "check.rmeta") + " " + Quote(workDir / "lib.rs");
                int code = 0;
                log = RunCommand(cmd, code);
                return code == 0;
            }
        };

        // ─────────────── Zig (zig build-lib -dynamic; libc ile)
        struct ZigToolchain final : Toolchain
        {
            std::string m_Compiler;

            NativeLang Lang() const override { return NativeLang::Zig; }
            const char* Name() const override { return "Zig"; }
            const char* Id() const override { return "zig"; }
            std::vector<std::string> Extensions() const override { return { ".zig" }; }
            std::string Unavailable() const override { return "No Zig compiler found (install Zig from ziglang.org or 'pip install ziglang', or set ZIG)."; }
            std::string Compiler() const override { return m_Compiler; }

            bool Locate() override
            {
                if (const char* z = std::getenv("ZIG"); z && *z) { m_Compiler = z; return true; }
                if (CommandExists("zig")) { m_Compiler = "zig"; return true; }
                // pip install ziglang: python paketinin içindeki resmi zig ikilisi
                for (const char* py : { "python3", "python" })
                {
                    int code = 0;
                    std::string out = RunCommand(std::string(py) + " -c \"import ziglang, os; print(os.path.dirname(ziglang.__file__))\"", code);
                    if (code != 0) continue;
                    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
#ifdef _WIN32
                    const fs::path candidate = fs::path(out) / "zig.exe";
#else
                    const fs::path candidate = fs::path(out) / "zig";
#endif
                    std::error_code ec;
                    if (fs::exists(candidate, ec)) { m_Compiler = Quote(candidate); return true; }
                }
                return false;
            }

            fs::path SdkDir() const
            {
                std::error_code ec;
                return fs::absolute(FileSystem::ResolveResourcePath("Resources/Scripting/Zig"), ec);
            }

            std::string SdkFingerprint() const override
            {
                std::string api;
                ReadFile(SdkDir() / "conqueror.zig", api);
                return api;
            }

            std::string Template(const std::string& className) const override
            {
                return "const cq = @import(\"conqueror.zig\");\n"
                       "const nb = @import(\"nativebridge.zig\");\n\n"
                       "// " + className + ": Zig script. Fields appear in the Inspector; pub fn methods are engine events.\n"
                       "pub const " + className + " = struct {\n"
                       "    speed: f32 = 1.0,\n\n"
                       "    pub fn start(self: *" + className + ") void {\n"
                       "        _ = self;\n"
                       "    }\n\n"
                       "    pub fn update(self: *" + className + ", dt: f32) void {\n"
                       "        _ = self;\n"
                       "        _ = dt;\n"
                       "    }\n"
                       "};\n";
            }

            // Zig dosyaları modül kökü dışından içe aktaramaz: SDK, köprü ve betikler aynı dizine kopyalanır
            void Generate(const fs::path& dir, const std::vector<fs::path>& sources, const fs::path* replaceFrom = nullptr, const fs::path* replaceTo = nullptr) const
            {
                std::error_code ec;
                fs::create_directories(dir, ec);
                std::string sdk;
                ReadFile(SdkDir() / "conqueror.zig", sdk);
                WriteIfChanged(dir / "conqueror.zig", sdk);
                std::string bridge = "// Auto-generated: engine nativebridge functions (do not edit)\nconst cq = @import(\"conqueror.zig\");\n\n";
                for (const NativeFunctionInfo* f : BridgeFunctions())
                    bridge += "pub fn " + f->Name + "(args: anytype) cq.Value { const S = struct { var index: i32 = -2; }; return cq.callCached(&S.index, \"" + f->Name + "\", args); }\n";
                WriteIfChanged(dir / "nativebridge.zig", bridge);

                static const std::regex structRe(R"(pub\s+const\s+([A-Za-z_]\w*)\s*=\s*(?:extern\s+|packed\s+)?struct\b)");
                std::string root = "// Auto-generated\nconst cq = @import(\"conqueror.zig\");\n";
                std::string classes;
                for (size_t i = 0; i < sources.size(); ++i)
                {
                    fs::path src = sources[i];
                    if (replaceFrom && replaceTo && src == *replaceFrom) src = *replaceTo;
                    std::string text;
                    ReadFile(src, text);
                    const std::string copyName = sources[i].filename().string();
                    WriteIfChanged(dir / copyName, text);
                    root += "const s" + std::to_string(i) + " = @import(\"" + copyName + "\");\n";
                    for (auto it = std::sregex_iterator(text.begin(), text.end(), structRe); it != std::sregex_iterator(); ++it)
                        classes += "cq.class(s" + std::to_string(i) + "." + (*it)[1].str() + ", \"" + (*it)[1].str() + "\"), ";
                }
                root += "const classes = .{ " + classes + "};\n\n"
                        "export fn cq_plugin_version() c_int { return 1; }\n"
                        "export fn cq_plugin_init(api: *const cq.HostApi) void { cq.init(api); }\n"
                        "export fn cq_plugin_describe() [*:0]const u8 { return cq.describe(classes); }\n"
                        "export fn cq_create(name: [*:0]const u8, entity: u32) ?*anyopaque { return cq.create(classes, name, entity); }\n"
                        "export fn cq_destroy(handle: ?*anyopaque) void { cq.destroy(classes, handle); }\n"
                        "export fn cq_has_method(handle: ?*anyopaque, name: [*:0]const u8) c_int { return cq.hasMethod(classes, handle, name); }\n"
                        "export fn cq_invoke(handle: ?*anyopaque, name: [*:0]const u8, args: ?[*]const cq.RawValue, argc: c_int) c_int { return cq.invoke(classes, handle, name, args, argc); }\n"
                        "export fn cq_set_field(handle: ?*anyopaque, name: [*:0]const u8, value: *const cq.RawValue) c_int { return cq.setField(classes, handle, name, value); }\n"
                        "export fn cq_get_field(handle: ?*anyopaque, name: [*:0]const u8, out: *cq.RawValue, type_name: ?*[*:0]const u8) c_int { return cq.getField(classes, handle, name, out, type_name); }\n"
                        "export fn cq_last_error() [*:0]const u8 { return cq.lastError(); }\n";
                WriteIfChanged(dir / "__cq_root.zig", root);
            }

            bool Build(const std::vector<fs::path>& sources, const fs::path& outDir, const fs::path& lib, std::string& log) override
            {
                Generate(outDir, sources);
                // Genel önbellek varsayılan yerinde (~/.cache/zig): projeler arası paylaşılır, ilk derleme bir kez yavaştır
                std::string cmd = m_Compiler + " build-lib -dynamic -O ReleaseSafe -lc --name cqscripts --cache-dir " + Quote(outDir / "zig-cache")
                                  + " -femit-bin=" + Quote(lib) + " " + Quote(outDir / "__cq_root.zig");
                int code = 0;
                log = RunCommand(cmd, code);
                std::error_code ec;
                return code == 0 && fs::exists(lib, ec);
            }

            bool Check(const std::vector<fs::path>& sources, const std::string& name, const std::string& source, const fs::path& workDir,
                       std::string& log, std::string& matchFile) override
            {
                const fs::path edited = workDir / ("__edit_" + fs::path(name).filename().string());
                std::ofstream(edited, std::ios::binary) << source;
                matchFile = fs::path(name).filename().string();
                std::vector<fs::path> all = sources;
                fs::path original;
                for (const auto& s : sources)
                    if (s.filename() == fs::path(name).filename()) original = s;
                if (original.empty()) all.push_back(fs::path(name));
                Generate(workDir, all, original.empty() ? nullptr : &original, &edited);
                if (original.empty())
                    WriteIfChanged(workDir / fs::path(name).filename(), source);
                std::string cmd = m_Compiler + " build-lib -dynamic -lc -fno-emit-bin --cache-dir " + Quote(workDir / "zig-cache") + " " + Quote(workDir / "__cq_root.zig");
                int code = 0;
                log = RunCommand(cmd, code);
                return code == 0;
            }
        };
    }

    // ═══════════════════════════════════════════════ Çalışma zamanı
    class NativeRuntime final : public ScriptRuntime
    {
    public:
        explicit NativeRuntime(std::unique_ptr<Toolchain> toolchain) : m_Tool(std::move(toolchain)) { m_Available = m_Tool->Locate(); }
        ~NativeRuntime() override
        {
            if (m_CheckThread.joinable()) m_CheckThread.join();
            ClearInstances();
            for (auto& p : m_Plugins) p->Unload();
        }

        const char* GetName() const override { return m_Tool->Name(); }
        const char* GetLanguageId() const override { return m_Tool->Id(); }
        std::vector<std::string> GetExtensions() const override { return m_Tool->Extensions(); }
        bool IsAvailable() const override { return m_Available; }
        std::string GetUnavailableReason() const override { return m_Tool->Unavailable(); }

        void OnPlayStart() override
        {
            ClearInstances();
            m_Loaded = false;
        }

        void OnPlayStop() override
        {
            ClearInstances();
            m_Loaded = false;
        }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!EnsureBuilt(error))
                return false;
            if (!m_Loaded)
            {
                if (!LoadLatest(error))
                    return false;
                m_Loaded = true;
            }
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override { return fs::path(path).stem().string(); }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)path;
            Plugin* p = Current();
            if (!p) return nullptr;
            void* obj;
            {
                ScriptContext::EntityScope scope(entity);
                obj = p->Create(className.c_str(), entity);
            }
            if (!obj)
            {
                Report(p->LastError());
                return nullptr;
            }
            auto* inst = new Instance{ obj, entity, className };
            m_Instances.push_back(inst);
            return inst;
        }

        void DestroyInstance(void* instance) override
        {
            auto* inst = static_cast<Instance*>(instance);
            auto it = std::find(m_Instances.begin(), m_Instances.end(), inst);
            if (it == m_Instances.end()) return;
            if (Plugin* p = Current()) p->Destroy(inst->Object);
            m_Instances.erase(it);
            delete inst;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* inst = Valid(instance);
            return inst && Current()->HasMethod(inst->Object, method.c_str()) != 0;
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
                ok = Current()->Invoke(inst->Object, method.c_str(), native.data(), argc);
            }
            if (!ok) Report(Current()->LastError());
            return ok != 0;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            CQNativeValue n;
            std::string storage;
            ToNative(ScriptRuntimes::ToValue(value), n, storage);
            return Current()->SetField(inst->Object, field.c_str(), &n) != 0;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            CQNativeValue n{};
            const char* type = nullptr;
            if (!Current()->GetField(inst->Object, field.c_str(), &n, &type)) return false;
            CQS::Value v = FromNative(n);
            value = ScriptRuntimes::FromValue(v);
            if (type && (std::strcmp(type, "float") == 0 || std::strcmp(type, "double") == 0) && value.Type == ScriptValue::Kind::Int)
                value = ScriptValue::MakeFloat((double)value.Int);
            if (display) *display = v.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            (void)path;
            if (!m_Loaded) return 0;
            Plugin* old = Current();
            if (!EnsureBuilt(error)) return -1;
            if (m_Plugins.empty() || m_Plugins.back()->Path == m_LibPath)
                return 0; // değişiklik yok
            if (!LoadLatest(error)) return -1;
            Plugin* fresh = Current();
            // Örnekleri yeni kitaplığa taşı (alanlar ada göre); eski nesneleri eski kitaplıkla yok et
            JSON::Value desc = JSON::Parse(fresh->Describe());
            int migrated = 0;
            for (Instance* inst : m_Instances)
            {
                void* obj = fresh->Create(inst->ClassName.c_str(), inst->Entity);
                if (!obj) continue;
                for (auto& cls : desc.ListVal)
                {
                    if (!cls.MapVal.count("name") || cls.MapVal.at("name").StringVal != inst->ClassName) continue;
                    for (auto& f : cls.MapVal.at("fields").ListVal)
                    {
                        const std::string& name = f.MapVal.at("name").StringVal;
                        CQNativeValue v{};
                        const char* type = nullptr;
                        if (old->GetField(inst->Object, name.c_str(), &v, &type))
                        {
                            std::string copy = v.Type == 4 && v.Str ? std::string(v.Str, (size_t)v.StrLen) : std::string();
                            if (v.Type == 4) { v.Str = copy.c_str(); v.StrLen = (int32_t)copy.size(); }
                            fresh->SetField(obj, name.c_str(), &v);
                        }
                    }
                }
                old->Destroy(inst->Object);
                inst->Object = obj;
                ++migrated;
            }
            old->Unload();
            CQ_CORE_INFO("[{0}] Hot reloaded script library ({1} instances migrated)", m_Tool->Name(), migrated);
            return migrated;
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            if (!DescribeAll(error)) return false;
            std::string stem = fs::path(path).stem().string();
            for (auto& [name, fields] : m_Description)
                if (name == stem) classes.push_back(name);
            if (classes.empty())
                for (auto& [name, fields] : m_Description) classes.push_back(name);
            return !classes.empty();
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)path;
            if (!DescribeAll(error)) return false;
            for (auto& [name, list] : m_Description)
                if (name == className) { fields = list; return true; }
            if (error) *error = "Class '" + className + "' not found in the " + std::string(m_Tool->Name()) + " scripts";
            return false;
        }

        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            (void)warnings;
            if (!m_Available) return true;
            size_t hash = std::hash<std::string>{}(source + name);
            {
                std::lock_guard<std::mutex> lock(m_CheckMutex);
                auto it = m_CheckResults.find(hash);
                if (it != m_CheckResults.end())
                {
                    errors = it->second;
                    return errors.empty();
                }
                if (m_CheckRunning)
                {
                    m_CheckPending = true;
                    return true;
                }
            }
            // Arka planda derleyici denetimi
            if (m_CheckThread.joinable()) m_CheckThread.join();
            m_CheckRunning = true;
            m_CheckPending = true;
            std::error_code ec;
            fs::path work = fs::temp_directory_path(ec) / "ConquerorEngine" / (std::string(m_Tool->Id()) + "-check");
            fs::create_directories(work, ec);
            std::vector<fs::path> sources = CollectSources();
            m_CheckThread = std::thread([this, hash, name, source, work, sources]() {
                std::string log, matchFile;
                m_Tool->Check(sources, name, source, work, log, matchFile);
                std::vector<std::string> errs = ParseDiagnostics(log, matchFile, name);
                std::lock_guard<std::mutex> lock(m_CheckMutex);
                if (m_CheckResults.size() > 64) m_CheckResults.clear();
                m_CheckResults[hash] = errs;
                m_CheckRunning = false;
            });
            return true;
        }

        bool HasPendingSyntaxCheck() const override
        {
            std::lock_guard<std::mutex> lock(m_CheckMutex);
            bool pending = m_CheckPending;
            const_cast<NativeRuntime*>(this)->m_CheckPending = m_CheckRunning;
            return pending;
        }

        std::string GetNewScriptTemplate(const std::string& className) const override { return m_Tool->Template(className); }

    private:
        std::unique_ptr<Toolchain> m_Tool;
        bool m_Available = false;
        std::vector<std::unique_ptr<Plugin>> m_Plugins;
        fs::path m_OutDir, m_LibPath;
        size_t m_SourceHash = 0;
        int m_BuildCounter = 0;
        bool m_Built = false;
        bool m_Loaded = false;
        std::vector<Instance*> m_Instances;
        std::vector<std::pair<std::string, std::vector<ScriptFieldInfo>>> m_Description;
        size_t m_DescribedHash = 0;
        std::unordered_map<std::string, double> m_LastError;

        mutable std::mutex m_CheckMutex;
        std::thread m_CheckThread;
        std::atomic<bool> m_CheckRunning{ false };
        bool m_CheckPending = false;
        std::unordered_map<size_t, std::vector<std::string>> m_CheckResults;

        Plugin* Current() { return m_Plugins.empty() || !m_Plugins.back()->Handle ? nullptr : m_Plugins.back().get(); }

        Instance* Valid(void* instance)
        {
            if (!instance || !Current()) return nullptr;
            auto* inst = static_cast<Instance*>(instance);
            return std::find(m_Instances.begin(), m_Instances.end(), inst) != m_Instances.end() ? inst : nullptr;
        }

        void ClearInstances()
        {
            Plugin* p = Current();
            for (Instance* i : m_Instances)
            {
                if (p) p->Destroy(i->Object);
                delete i;
            }
            m_Instances.clear();
        }

        void Report(const std::string& message)
        {
            double now = Now();
            auto it = m_LastError.find(message);
            if (it != m_LastError.end() && now - it->second < 3.0) return;
            m_LastError[message] = now;
            if (m_LastError.size() > 256) m_LastError.clear();
            CQ_CORE_ERROR("[{0}] {1}", m_Tool->Name(), message);
        }

        fs::path GeneratedDir() const
        {
            fs::path projectDir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            return (projectDir.empty() ? fs::temp_directory_path(ec) / "ConquerorEngine" : projectDir / "Library") / "ScriptAssemblies" / m_Tool->Id();
        }

        std::vector<fs::path> CollectSources() const { return CollectFiles(m_Tool->Extensions()); }

        std::vector<fs::path> CollectFiles(const std::vector<std::string>& exts) const
        {
            std::vector<fs::path> sources;
            if (exts.empty())
                return sources;
            fs::path projectDir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            fs::path assets = projectDir / "Assets";
            if (projectDir.empty() || !fs::exists(assets, ec))
                return sources;
            for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                std::string ext = it->path().extension().string();
                for (char& c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                if (it->is_regular_file(ec) && std::find(exts.begin(), exts.end(), ext) != exts.end())
                    sources.push_back(it->path());
            }
            std::sort(sources.begin(), sources.end());
            return sources;
        }

        // Projedeki bu dilin betiklerini derle (içerik değiştiyse)
        bool EnsureBuilt(std::string* error)
        {
            if (!m_Available)
            {
                if (error) *error = GetUnavailableReason();
                return false;
            }
            std::vector<fs::path> sources = CollectSources();
            std::string all = m_Tool->SdkFingerprint();
            for (auto& s : sources) { std::string t; ReadFile(s, t); all += s.string() + "\n" + t; }
            // Başlıklar: değişince yeniden derle; DataAsset sınıfı tanımlayanlar derlemeye eklenir
            m_Tool->DataHeaders.clear();
            for (auto& h : CollectFiles(m_Tool->HeaderExtensions()))
            {
                std::string t;
                ReadFile(h, t);
                all += h.string() + "\n" + t;
                if (!DataAssets::DataClassesInText(t, m_Tool->Id()).empty())
                    m_Tool->DataHeaders.push_back(h);
            }
            const bool usesModules = std::string(m_Tool->Id()) == "cpp" || std::string(m_Tool->Id()) == "c";
            std::vector<NativeModules::Module> modules;
            if (usesModules)
            {
                modules = NativeModules::Resolve();
                all += NativeModules::Fingerprint(modules);
            }
            size_t hash = std::hash<std::string>{}(all);
            if (m_Built && hash == m_SourceHash)
                return true;
            if (sources.empty() && m_Tool->DataHeaders.empty())
            {
                if (error) *error = std::string("No ") + m_Tool->Name() + " scripts in the project";
                return false;
            }
            m_OutDir = GeneratedDir();
            std::error_code ec;
            fs::create_directories(m_OutDir, ec);
            // Eski derlemeleri temizle (yüklü olanlar hariç)
            for (auto& e : fs::directory_iterator(m_OutDir, ec))
            {
                std::string n = e.path().filename().string();
                bool loaded = false;
                for (auto& p : m_Plugins) if (p->Handle && p->Path == e.path()) loaded = true;
                if (!loaded && n.rfind("CQScripts_", 0) == 0) fs::remove(e.path(), ec);
            }
            fs::path lib = m_OutDir / ("CQScripts_" + std::to_string(++m_BuildCounter) + "_" + std::to_string(hash % 100000) + LibExtension());
            double t0 = Now();
            std::string log;
            if (usesModules)
                sources = NativeModules::ApplyModules(modules, sources, m_OutDir, std::string(m_Tool->Id()) == "cpp" ? ".cpp" : ".c", m_Tool->ExtraIncludes);
            if (!m_Tool->Build(sources, m_OutDir, lib, log))
            {
                auto errors = ParseDiagnostics(log, "", "");
                for (size_t i = 0; i < errors.size() && i < 20; ++i) CQ_CORE_ERROR("[{0}] {1}", m_Tool->Name(), errors[i]);
                if (errors.empty()) CQ_CORE_ERROR("[{0}] Compilation failed:\n{1}", m_Tool->Name(), log);
                if (error) *error = errors.empty() ? std::string(m_Tool->Name()) + " compilation failed" : errors.front();
                m_Built = false;
                return false;
            }
            CQ_CORE_INFO("[{0}] Compiled {1} script file{2} in {3:.2f}s ({4})", m_Tool->Name(), sources.size(), sources.size() == 1 ? "" : "s",
                         Now() - t0, m_Tool->Compiler());
            m_LibPath = lib;
            m_SourceHash = hash;
            m_Built = true;
            return true;
        }

        bool LoadLatest(std::string* error)
        {
            if (!m_Plugins.empty() && m_Plugins.back()->Handle && m_Plugins.back()->Path == m_LibPath)
                return true;
            auto plugin = std::make_unique<Plugin>();
            std::string err;
            if (!plugin->Load(m_LibPath, &g_HostApis[(int)m_Tool->Lang()], err))
            {
                Report("Cannot load script library: " + err);
                if (error) *error = err;
                return false;
            }
            m_Plugins.push_back(std::move(plugin));
            return true;
        }

        bool DescribeAll(std::string* error)
        {
            if (!EnsureBuilt(error)) return false;
            if (m_DescribedHash == m_SourceHash && !m_Description.empty()) return true;
            if (!LoadLatest(error)) return false;
            JSON::Value list = JSON::Parse(Current()->Describe());
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
                        info.Name = f.MapVal.at("name").StringVal;
                        info.TypeName = f.MapVal.at("type").StringVal;
                        if (f.MapVal.count("attributes"))
                            for (auto& a : f.MapVal.at("attributes").ListVal) info.Attributes.push_back(a.StringVal);
                        CQS::Value def = f.MapVal.count("default") ? FromJson(f.MapVal.at("default")) : CQS::Value();
                        info.Default = ScriptRuntimes::FromValue(def);
                        if ((info.TypeName == "float" || info.TypeName == "double") && info.Default.Type == ScriptValue::Kind::Int)
                            info.Default = ScriptValue::MakeFloat((double)info.Default.Int);
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

    std::unique_ptr<ScriptRuntime> CreateCppRuntime() { return std::make_unique<NativeRuntime>(std::make_unique<CppToolchain>()); }
    std::unique_ptr<ScriptRuntime> CreateCRuntime() { return std::make_unique<NativeRuntime>(std::make_unique<CToolchain>()); }
    std::unique_ptr<ScriptRuntime> CreateRustRuntime() { return std::make_unique<NativeRuntime>(std::make_unique<RustToolchain>()); }
    std::unique_ptr<ScriptRuntime> CreateZigRuntime() { return std::make_unique<NativeRuntime>(std::make_unique<ZigToolchain>()); }
}
