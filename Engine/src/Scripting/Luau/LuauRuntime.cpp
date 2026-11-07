#include "Scripting/ScriptRuntime.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Scripting/LuaQ/LuaQCompiler.h"

#ifdef CQ_LUAU_ENABLED
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "luacodegen.h"
#endif

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Luau betikleri (Roblox'un Lua lehçesi; Lua 5.1 ile uyumlu, .lua da kabul edilir)
// ───────────────────────────────────────────────
// Bir betik, sınıf tablosu döndüren bir modüldür:
//
//     local Spinner = {}
//     Spinner.speed = 90            -- Inspector'da görünen alan ('_' ile başlayanlar gizli)
//     function Spinner:Update(dt)
//         local r = GetRotation()   -- nativebridge fonksiyonları global (ya da nativebridge.GetRotation)
//         SetRotation(r.x, r.y + self.speed * dt, r.z)
//     end
//     return Spinner
//
// Her varlık için bir örnek tablosu oluşturulur (metatable __index = sınıf), böylece alan değerleri
// varlık başınadır ve sıcak yeniden yüklemede korunur.

namespace Conqueror::Scripting
{
#ifdef CQ_LUAU_ENABLED
    namespace
    {
        constexpr double kCallBudgetSeconds = 2.0;     // tek bir çağrı (ör. Update) bu süreyi aşarsa durdurulur

        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        // ── Değer dönüşümü ──
        void PushValue(lua_State* L, const CQS::Value& v, int depth = 0);

        CQS::Value ToValue(lua_State* L, int idx, int depth = 0)
        {
            using CQS::Value;
            switch (lua_type(L, idx))
            {
                case LUA_TNIL: return Value();
                case LUA_TBOOLEAN: return Value::MakeBool(lua_toboolean(L, idx) != 0);
                case LUA_TNUMBER:
                {
                    double d = lua_tonumber(L, idx);
                    if (std::floor(d) == d && std::fabs(d) < 9.0e15) return Value::MakeInt((int64_t)d);
                    return Value::MakeFloat(d);
                }
                case LUA_TSTRING:
                {
                    size_t len = 0;
                    const char* s = lua_tolstring(L, idx, &len);
                    return Value::MakeString(std::string(s, len));
                }
                case LUA_TVECTOR:
                {
                    const float* vec = lua_tovector(L, idx);
                    auto* m = new CQS::CQSMapObject();
                    m->Entries["x"] = Value::MakeFloat(vec[0]);
                    m->Entries["y"] = Value::MakeFloat(vec[1]);
                    m->Entries["z"] = Value::MakeFloat(vec[2]);
                    return Value::MakeObject(m);
                }
                case LUA_TTABLE:
                {
                    if (depth > 16) return Value();
                    idx = lua_absindex(L, idx);
                    int n = lua_objlen(L, idx);
                    if (n > 0)
                    {
                        auto* list = new CQS::CQSListObject();
                        for (int i = 1; i <= n; ++i)
                        {
                            lua_rawgeti(L, idx, i);
                            list->Elements.push_back(ToValue(L, -1, depth + 1));
                            lua_pop(L, 1);
                        }
                        return Value::MakeObject(list);
                    }
                    auto* map = new CQS::CQSMapObject();
                    lua_pushnil(L);
                    while (lua_next(L, idx) != 0)
                    {
                        if (lua_type(L, -2) == LUA_TSTRING)
                            map->Entries[lua_tostring(L, -2)] = ToValue(L, -1, depth + 1);
                        else if (lua_type(L, -2) == LUA_TNUMBER)
                        {
                            lua_pushvalue(L, -2);
                            map->Entries[lua_tostring(L, -1)] = ToValue(L, -2, depth + 1);
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                    }
                    return Value::MakeObject(map);
                }
                default:
                    return Value();
            }
        }

        void PushValue(lua_State* L, const CQS::Value& v, int depth)
        {
            using CQS::ObjectType;
            switch (v.Type)
            {
                case CQS::ValueType::Null: lua_pushnil(L); return;
                case CQS::ValueType::Bool: lua_pushboolean(L, v.AsBool()); return;
                case CQS::ValueType::Int: lua_pushnumber(L, (double)v.AsInt()); return;
                case CQS::ValueType::Float: lua_pushnumber(L, v.AsFloat()); return;
                default: break;
            }
            if (!v.IsObject() || depth > 16)
            {
                lua_pushnil(L);
                return;
            }
            switch (v.ObjectVal->Type)
            {
                case ObjectType::String:
                {
                    const std::string& s = v.AsString()->Value;
                    lua_pushlstring(L, s.data(), s.size());
                    return;
                }
                case ObjectType::List:
                {
                    const auto& e = v.AsList()->Elements;
                    lua_createtable(L, (int)e.size(), 0);
                    for (size_t i = 0; i < e.size(); ++i)
                    {
                        PushValue(L, e[i], depth + 1);
                        lua_rawseti(L, -2, (int)i + 1);
                    }
                    return;
                }
                case ObjectType::Map:
                {
                    const auto& m = v.AsMap()->Entries;
                    lua_createtable(L, 0, (int)m.size());
                    for (auto& [k, val] : m)
                    {
                        PushValue(L, val, depth + 1);
                        lua_setfield(L, -2, k.c_str());
                    }
                    return;
                }
                default:
                    lua_pushstring(L, v.ToString().c_str());
                    return;
            }
        }

        // nativebridge fonksiyonu: upvalue 1 = kayıt indeksi
        int NativeThunk(lua_State* L)
        {
            int index = (int)lua_tointeger(L, lua_upvalueindex(1));
            const auto& fns = NativeRegistry::Get().GetFunctions();
            if (index < 0 || index >= (int)fns.size())
                luaL_error(L, "invalid native function");
            const NativeFunctionInfo& f = fns[(size_t)index];
            int argc = lua_gettop(L);
            if (f.Arity >= 0 && argc > f.Arity)
                argc = f.Arity; // Lua geleneği: fazla argümanlar yok sayılır
            int slots = std::max(argc, f.Arity < 0 ? argc : f.Arity);
            std::vector<CQS::Value> args((size_t)std::max(slots, 1));
            for (int i = 0; i < argc; ++i)
                args[(size_t)i] = ToValue(L, i + 1);
            CQS::Value r = f.Function(argc, args.data());
            PushValue(L, r);
            return 1;
        }

        struct Instance
        {
            int Ref = LUA_NOREF;     // örnek tablosu
            std::string Path;
            uint32_t Entity = 0xFFFFFFFF;
        };
    }

    // Aynı sınıf iki dili çalıştırır: Luau (.luau/.lua) ve LuaQ (.luaq; kaynak LuaQ derleyicisiyle Luau'ya indirgenir,
    // durum açılırken LuaQ çalışma zamanı kitaplığı yüklenir, async görevler her karede ilerletilir)
    class LuauRuntime final : public ScriptRuntime
    {
    public:
        explicit LuauRuntime(bool luaq = false) : m_LuaQ(luaq) {}
        ~LuauRuntime() override { Close(); }

        const char* GetName() const override { return m_LuaQ ? "LuaQ" : "Luau"; }
        const char* GetLanguageId() const override { return m_LuaQ ? "luaq" : "luau"; }
        std::vector<std::string> GetExtensions() const override
        {
            if (m_LuaQ) return { ".luaq" };
            return { ".luau", ".lua" };
        }

        void OnPlayStart() override { Close(); Open(); }
        void OnPlayStop() override { Close(); }

        void EndFrame() override
        {
            if (!m_L)
                return;
            if (m_LuaQ)
            {
                // async / await / wait görevleri
                lua_getglobal(m_L, "__luaq");
                if (lua_istable(m_L, -1))
                {
                    lua_getfield(m_L, -1, "tick");
                    m_CallStart = Now();
                    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK)
                    {
                        ReportError(lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "async tick error");
                        lua_pop(m_L, 1);
                    }
                    m_CallStart = 0.0;
                }
                lua_pop(m_L, 1);
            }
            lua_gc(m_L, LUA_GCSTEP, 0);
        }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            if (!m_L) Open();
            std::string source;
            if (!ReadFile(path, source))
            {
                if (error) *error = "Cannot open " + path;
                return false;
            }
            size_t hash = std::hash<std::string>{}(source);
            auto it = m_Modules.find(path);
            if (it != m_Modules.end() && it->second.Hash == hash)
                return true;
            int ref = LUA_NOREF;
            if (!RunModule(m_L, source, path, &ref, error, m_LuaQ))
                return false;
            if (it != m_Modules.end() && it->second.ClassRef != LUA_NOREF)
                lua_unref(m_L, it->second.ClassRef);
            m_Modules[path] = { ref, hash };
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override
        {
            return std::filesystem::path(path).stem().string();
        }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)className;
            auto it = m_Modules.find(path);
            if (!m_L || it == m_Modules.end())
                return nullptr;
            lua_State* L = m_L;
            lua_newtable(L);
            const int instIdx = lua_gettop(L);
            lua_getref(L, it->second.ClassRef);
            const int clsIdx = lua_gettop(L);
            // Tablo değerli alanların sığ kopyası: örnekler aynı listeyi paylaşmasın
            lua_pushnil(L);
            while (lua_next(L, clsIdx) != 0)                // [.. anahtar değer]
            {
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TTABLE)
                {
                    const int srcIdx = lua_gettop(L);
                    lua_newtable(L);
                    const int copyIdx = lua_gettop(L);
                    lua_pushnil(L);
                    while (lua_next(L, srcIdx) != 0)        // [.. kopya k v]
                    {
                        lua_pushvalue(L, -2);
                        lua_insert(L, -2);                  // [.. kopya k k v]
                        lua_rawset(L, copyIdx);             // [.. kopya k]
                    }
                    lua_pushvalue(L, srcIdx - 1);           // anahtar
                    lua_insert(L, -2);                      // [.. anahtar değer anahtar kopya]
                    lua_rawset(L, instIdx);
                }
                lua_pop(L, 1);                              // değeri at, anahtar kalır
            }
            lua_newtable(L);                                // metatable { __index = sınıf }
            lua_pushvalue(L, clsIdx);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, instIdx);
            lua_settop(L, instIdx);                         // [örnek]
            // Kolaylık: self.entity
            lua_pushnumber(L, (double)entity);
            lua_setfield(L, -2, "entity");
            auto* inst = new Instance();
            inst->Ref = lua_ref(L, -1);
            inst->Path = path;
            inst->Entity = entity;
            lua_pop(L, 1);
            m_Instances.push_back(inst);
            return inst;
        }

        void DestroyInstance(void* instance) override
        {
            auto* inst = static_cast<Instance*>(instance);
            auto it = std::find(m_Instances.begin(), m_Instances.end(), inst);
            if (it == m_Instances.end())
                return;
            if (m_L && inst->Ref != LUA_NOREF)
                lua_unref(m_L, inst->Ref);
            m_Instances.erase(it);
            delete inst;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            lua_getref(m_L, inst->Ref);
            lua_getfield(m_L, -1, method.c_str());
            bool has = lua_isfunction(m_L, -1);
            lua_pop(m_L, 2);
            return has;
        }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            lua_State* L = m_L;
            int top = lua_gettop(L);
            lua_getref(L, inst->Ref);
            lua_getfield(L, -1, method.c_str());
            if (!lua_isfunction(L, -1))
            {
                lua_settop(L, top);
                return true; // metot yok: sessizce geç (Unity gibi)
            }
            lua_pushvalue(L, -2);                           // self
            for (int i = 0; i < argc; ++i)
                PushValue(L, args[i]);
            ScriptContext::EntityScope scope(inst->Entity);
            m_CallStart = Now();
            int status = lua_pcall(L, argc + 1, 0, 0);
            m_CallStart = 0.0;
            bool ok = status == LUA_OK;
            if (!ok)
                ReportError(lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
            lua_settop(L, top);
            return ok;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            lua_getref(m_L, inst->Ref);
            PushValue(m_L, ScriptRuntimes::ToValue(value));
            lua_setfield(m_L, -2, field.c_str());
            lua_pop(m_L, 1);
            return true;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            lua_getref(m_L, inst->Ref);
            lua_getfield(m_L, -1, field.c_str());
            CQS::Value v = ToValue(m_L, -1);
            lua_pop(m_L, 2);
            value = ScriptRuntimes::FromValue(v);
            // Luau sayıları double'dır: tam sayı alanlar float olarak gösterilsin istenirse sınıf varsayılanına uyulur
            if (display) *display = v.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            if (!m_L || !m_Modules.count(path))
                return 0;
            if (!LoadScript(path, error))
                return -1;
            int updated = 0;
            lua_State* L = m_L;
            for (Instance* inst : m_Instances)
            {
                if (inst->Path != path) continue;
                lua_getref(L, inst->Ref);
                lua_newtable(L);
                lua_getref(L, m_Modules[path].ClassRef);
                lua_setfield(L, -2, "__index");
                lua_setmetatable(L, -2);
                lua_pop(L, 1);
                ++updated;
            }
            CQ_CORE_INFO("[Luau] Hot reloaded {0} ({1} instance{2})", std::filesystem::path(path).filename().string(), updated, updated == 1 ? "" : "s");
            return updated;
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            (void)error;
            classes.push_back(GetDefaultClass(path));
            return true;
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)className;
            std::string source;
            if (!ReadFile(path, source))
            {
                if (error) *error = "Cannot open " + path;
                return false;
            }
            // Ayrı bir durumda modülü çalıştır ve sınıf tablosunun ilkel alanlarını oku
            lua_State* L = NewState(m_LuaQ);
            int ref = LUA_NOREF;
            bool ok = RunModule(L, source, path, &ref, error, m_LuaQ);
            if (ok)
            {
                lua_getref(L, ref);
                std::vector<std::pair<std::string, ScriptValue>> found;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0)
                {
                    if (lua_type(L, -2) == LUA_TSTRING)
                    {
                        std::string key = lua_tostring(L, -2);
                        int t = lua_type(L, -1);
                        if (!key.empty() && key[0] != '_' && (t == LUA_TNUMBER || t == LUA_TBOOLEAN || t == LUA_TSTRING))
                        {
                            ScriptValue v;
                            if (t == LUA_TBOOLEAN) v = ScriptValue::MakeBool(lua_toboolean(L, -1) != 0);
                            else if (t == LUA_TSTRING) v = ScriptValue::MakeString(lua_tostring(L, -1));
                            else
                            {
                                double d = lua_tonumber(L, -1);
                                // Kaynakta ondalık yazıldıysa ("5.0") float: bunu ayırt edemeyiz; tam sayılar int gösterilir
                                v = (std::floor(d) == d && std::fabs(d) < 1e15) ? ScriptValue::MakeInt((int64_t)d) : ScriptValue::MakeFloat(d);
                                if (v.Type == ScriptValue::Kind::Int && LooksLikeFloatLiteral(source, key))
                                    v = ScriptValue::MakeFloat(d);
                            }
                            found.emplace_back(key, v);
                        }
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
                // Kaynaktaki sıraya göre diz
                std::sort(found.begin(), found.end(), [&](auto& a, auto& b) { return SourcePos(source, a.first) < SourcePos(source, b.first); });
                for (auto& [k, v] : found)
                {
                    ScriptFieldInfo info;
                    info.Name = k;
                    info.Default = v;
                    info.HasDefault = true;
                    info.TypeName = v.Type == ScriptValue::Kind::Bool ? "bool" : v.Type == ScriptValue::Kind::String ? "string" : v.Type == ScriptValue::Kind::Int ? "int" : "float";
                    fields.push_back(info);
                }
            }
            lua_close(L);
            return ok;
        }

        bool CheckSyntax(const std::string& sourceIn, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            (void)warnings;
            std::string source = sourceIn;
            if (m_LuaQ)
            {
                std::string translated;
                std::vector<LuaQ::Diagnostic> diags;
                LuaQ::CompileOptions options;
                options.ChunkName = name;
                if (!LuaQ::Compile(sourceIn, options, translated, diags))
                {
                    for (const auto& d : diags)
                        errors.push_back(name + ": Line " + std::to_string(d.Line) + ": " + d.Message);
                    return false;
                }
                source = std::move(translated);
            }
            lua_CompileOptions opts = {};
            opts.optimizationLevel = 1;
            opts.debugLevel = 1;
            size_t size = 0;
            char* bytecode = luau_compile(source.data(), source.size(), &opts, &size);
            bool ok = size > 0 && bytecode[0] != 0;
            if (!ok && size > 1)
            {
                // Hata baytkodu: 0 + ":satır: mesaj"
                std::string msg(bytecode + 1, size - 1);
                int line = 0;
                if (!msg.empty() && msg[0] == ':')
                {
                    line = std::atoi(msg.c_str() + 1);
                    size_t c = msg.find(':', 1);
                    if (c != std::string::npos) msg = msg.substr(c + 1);
                    while (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                }
                errors.push_back(name + ": Line " + std::to_string(line) + ": " + msg);
            }
            std::free(bytecode);
            return ok;
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            if (m_LuaQ)
                return "-- " + className + ": LuaQ script (Lua 5.4 + Luau + classes, ?. ??, match, enum, defer, async/await).\n"
                       "-- Class fields appear in the Inspector (names starting with '_' are hidden); the file returns the class automatically.\n"
                       "class " + className + "\n"
                       "    speed = 1.0\n\n"
                       "    function Start()\n"
                       "    end\n\n"
                       "    function Update(dt: number)\n"
                       "    end\n"
                       "end\n";
            return "--!strict\n"
                   "-- " + className + ": Luau script. Public fields appear in the Inspector (names starting with '_' are hidden).\n"
                   "local " + className + " = {}\n\n"
                   + className + ".speed = 1.0\n\n"
                   "function " + className + ":Start()\n"
                   "end\n\n"
                   "function " + className + ":Update(dt: number)\n"
                   "end\n\n"
                   "return " + className + "\n";
        }

    private:
        struct Module
        {
            int ClassRef = LUA_NOREF;
            size_t Hash = 0;
        };

        bool m_LuaQ = false;
        lua_State* m_L = nullptr;
        std::unordered_map<std::string, Module> m_Modules;
        std::vector<Instance*> m_Instances;
        static inline double m_CallStart = 0.0;
        std::unordered_map<std::string, double> m_LastError;

        Instance* Valid(void* instance)
        {
            if (!m_L || !instance) return nullptr;
            auto* inst = static_cast<Instance*>(instance);
            return std::find(m_Instances.begin(), m_Instances.end(), inst) != m_Instances.end() ? inst : nullptr;
        }

        // Alanın atandığı yer: "X.anahtar =" (Luau) ya da satır başında "anahtar =" (LuaQ sınıf gövdesi)
        static size_t FieldAssignPos(const std::string& source, const std::string& key)
        {
            size_t p = source.find("." + key);
            if (p != std::string::npos)
                return p;
            for (size_t q = source.find(key); q != std::string::npos; q = source.find(key, q + 1))
            {
                size_t b = q;
                while (b > 0 && (source[b - 1] == ' ' || source[b - 1] == '\t')) --b;
                if (b > 0 && source[b - 1] != '\n')
                    continue;
                size_t e = q + key.size();
                while (e < source.size() && (source[e] == ' ' || source[e] == '\t')) ++e;
                if (e < source.size() && (source[e] == '=' || source[e] == ':') && (e + 1 >= source.size() || source[e + 1] != '='))
                    return q;
            }
            return std::string::npos;
        }

        static bool LooksLikeFloatLiteral(const std::string& source, const std::string& key)
        {
            // "X.key = 5.0" gibi bir atamada ondalık nokta var mı
            size_t first = FieldAssignPos(source, key);
            if (first != std::string::npos && source.compare(first, key.size() + 1, "." + key) != 0)
            {
                size_t eq = source.find('=', first);
                size_t nl = source.find('\n', first);
                if (eq != std::string::npos && eq < nl)
                {
                    size_t i = eq + 1;
                    while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) ++i;
                    size_t j = i;
                    while (j < source.size() && (std::isdigit((unsigned char)source[j]) || source[j] == '.' || source[j] == '-')) ++j;
                    return source.substr(i, j - i).find('.') != std::string::npos;
                }
            }
            size_t p = source.find("." + key);
            while (p != std::string::npos)
            {
                size_t eq = source.find('=', p);
                size_t nl = source.find('\n', p);
                if (eq != std::string::npos && eq < nl)
                {
                    size_t i = eq + 1;
                    while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) ++i;
                    size_t j = i;
                    while (j < source.size() && (std::isdigit((unsigned char)source[j]) || source[j] == '.' || source[j] == '-')) ++j;
                    return source.substr(i, j - i).find('.') != std::string::npos;
                }
                p = source.find("." + key, p + 1);
            }
            return false;
        }

        static size_t SourcePos(const std::string& source, const std::string& key)
        {
            size_t p = FieldAssignPos(source, key);
            return p == std::string::npos ? source.size() : p;
        }

        void ReportError(const std::string& message)
        {
            double now = Now();
            auto it = m_LastError.find(message);
            if (it != m_LastError.end() && now - it->second < 3.0)
                return;
            m_LastError[message] = now;
            if (m_LastError.size() > 256) m_LastError.clear();
            CQ_CORE_ERROR("[{0}] {1}", m_LuaQ ? "LuaQ" : "Luau", message);
        }

        static void Interrupt(lua_State* L, int gc)
        {
            if (gc >= 0)
                return;
            if (m_CallStart > 0.0 && Now() - m_CallStart > kCallBudgetSeconds)
            {
                m_CallStart = 0.0;
                luaL_error(L, "script exceeded the %.0f second time budget (infinite loop?)", kCallBudgetSeconds);
            }
        }

        static int Print(lua_State* L)
        {
            int n = lua_gettop(L);
            std::string line;
            for (int i = 1; i <= n; ++i)
            {
                size_t len = 0;
                const char* s = luaL_tolstring(L, i, &len);
                line += (i > 1 ? " " : "") + std::string(s, len);
                lua_pop(L, 1);
            }
            CQ_CORE_INFO("[Luau] {0}", line);
            return 0;
        }

        static lua_State* NewState(bool luaq)
        {
            lua_State* L = luaL_newstate();
            luaL_openlibs(L);
            lua_callbacks(L)->interrupt = &LuauRuntime::Interrupt;
            if (luau_codegen_supported())
                luau_codegen_create(L);
            // nativebridge: hem tablo hem global
            const auto& registry = NativeRegistry::Get();
            lua_newtable(L);
            const auto& fns = registry.GetFunctions();
            for (size_t i = 0; i < fns.size(); ++i)
            {
                lua_pushinteger(L, (int)i);
                lua_pushcclosure(L, NativeThunk, fns[i].Name.c_str(), 1);
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, fns[i].Name.c_str());
                // Luau'nun kendi globallerini (print, type, error ...) gölgeleme: yalnızca nativebridge.X ile erişilir
                lua_getglobal(L, fns[i].Name.c_str());
                bool exists = !lua_isnil(L, -1);
                lua_pop(L, 1);
                if (exists)
                    lua_pop(L, 1);
                else
                    lua_setglobal(L, fns[i].Name.c_str());
            }
            for (const auto& [name, value] : registry.GetConstants())
            {
                PushValue(L, value);
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, name.c_str());
                lua_setglobal(L, name.c_str());
            }
            lua_setglobal(L, "nativebridge");
            lua_pushcfunction(L, Print, "print");
            lua_setglobal(L, "print");
            if (luaq)
            {
                // LuaQ çalışma zamanı kitaplığı: __luaq, isinstance, spawn, wait
                const std::string prelude = LuaQ::Prelude();
                size_t size = 0;
                char* bytecode = luau_compile(prelude.data(), prelude.size(), nullptr, &size);
                const int status = luau_load(L, "=luaq_prelude", bytecode, size, 0);
                std::free(bytecode);
                if (status != 0 || lua_pcall(L, 0, 0, 0) != LUA_OK)
                {
                    CQ_CORE_ERROR("[LuaQ] runtime library failed: {0}", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
                    lua_pop(L, 1);
                }
            }
            return L;
        }

        static bool RunModule(lua_State* L, const std::string& sourceIn, const std::string& path, int* outRef, std::string* error, bool luaq)
        {
            const char* tag = luaq ? "[LuaQ]" : "[Luau]";
            std::string source = sourceIn;
            if (luaq)
            {
                // LuaQ -> Luau (satırlar korunur); dosyada return yoksa dosya adıyla aynı sınıf döner
                LuaQ::CompileOptions options;
                options.ChunkName = std::filesystem::path(path).filename().string();
                options.AutoReturnClass = std::filesystem::path(path).stem().string();
                std::vector<LuaQ::Diagnostic> diags;
                std::string translated;
                if (!LuaQ::Compile(sourceIn, options, translated, diags))
                {
                    std::string msg = options.ChunkName + ":" + std::to_string(diags.empty() ? 0 : diags[0].Line) + ": "
                                      + (diags.empty() ? std::string("compile error") : diags[0].Message);
                    CQ_CORE_ERROR("{0} {1}", tag, msg);
                    if (error) *error = msg;
                    return false;
                }
                source = std::move(translated);
            }
            lua_CompileOptions opts = {};
            opts.optimizationLevel = 2;
            opts.debugLevel = 1;
            size_t size = 0;
            char* bytecode = luau_compile(source.data(), source.size(), &opts, &size);
            std::string chunk = "@" + std::filesystem::path(path).filename().string();
            int status = luau_load(L, chunk.c_str(), bytecode, size, 0);
            std::free(bytecode);
            if (status != 0)
            {
                std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
                lua_pop(L, 1);
                CQ_CORE_ERROR("{0} {1}", tag, msg);
                if (error) *error = msg;
                return false;
            }
            if (luau_codegen_supported())
                luau_codegen_compile(L, -1);
            m_CallStart = Now();
            status = lua_pcall(L, 0, 1, 0);
            m_CallStart = 0.0;
            if (status != LUA_OK)
            {
                std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "runtime error";
                lua_pop(L, 1);
                CQ_CORE_ERROR("{0} {1}", tag, msg);
                if (error) *error = msg;
                return false;
            }
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                std::string msg = std::filesystem::path(path).filename().string()
                                  + (luaq ? ": script must declare a class named like the file (or 'return' a table)" : ": script must 'return' its class table");
                CQ_CORE_ERROR("{0} {1}", tag, msg);
                if (error) *error = msg;
                return false;
            }
            *outRef = lua_ref(L, -1);
            lua_pop(L, 1);
            return true;
        }

        void Open()
        {
            if (!m_L)
                m_L = NewState(m_LuaQ);
        }

        void Close()
        {
            for (Instance* inst : m_Instances)
                delete inst;
            m_Instances.clear();
            m_Modules.clear();
            if (m_L)
            {
                lua_close(m_L);
                m_L = nullptr;
            }
        }
    };

    std::unique_ptr<ScriptRuntime> CreateLuauRuntime()
    {
        return std::make_unique<LuauRuntime>(false);
    }

    std::unique_ptr<ScriptRuntime> CreateLuaQRuntime()
    {
        return std::make_unique<LuauRuntime>(true);
    }
#else
    std::unique_ptr<ScriptRuntime> CreateLuauRuntime()
    {
        return nullptr;
    }

    std::unique_ptr<ScriptRuntime> CreateLuaQRuntime()
    {
        return nullptr;
    }
#endif
}
