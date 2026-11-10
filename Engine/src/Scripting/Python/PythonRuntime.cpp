#include "Scripting/ScriptRuntime.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#ifdef CQ_PYTHON_ENABLED
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if defined(CQ_PYTHON_MODULE) && !defined(_WIN32)
#include <dlfcn.h>
#endif
#endif

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Python betikleri (CPython gömülü)
// ─────────────────────────────────
//     from nativebridge import *
//
//     class Spinner:
//         speed = 90.0              # Inspector alanı ('_' ile başlayanlar gizli)
//
//         def start(self):          # Start/start, Update/update, OnDestroy/on_destroy ... ikisi de olur
//             print("ready", self.speed)
//
//         def update(self, dt):
//             r = GetRotation()
//             SetRotation(r["x"], r["y"] + self.speed * dt, r["z"])

namespace Conqueror::Scripting
{
#ifdef CQ_PYTHON_ENABLED
    namespace
    {
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

        // CamelCase -> snake_case (Update -> update, OnDestroy -> on_destroy)
        std::string SnakeCase(const std::string& name)
        {
            std::string out;
            for (size_t i = 0; i < name.size(); ++i)
            {
                char c = name[i];
                if (std::isupper((unsigned char)c))
                {
                    if (i > 0) out += '_';
                    out += (char)std::tolower((unsigned char)c);
                }
                else
                    out += c;
            }
            return out;
        }

        // Python sözlüğü {x, y, z} için öznitelik erişimi de sağlayan tür (r.x ve r["x"])
        PyObject* g_AttrDictType = nullptr;

        PyObject* ToPy(const CQS::Value& v, int depth = 0)
        {
            using CQS::ObjectType;
            switch (v.Type)
            {
                case CQS::ValueType::Null: Py_RETURN_NONE;
                case CQS::ValueType::Bool: return PyBool_FromLong(v.AsBool() ? 1 : 0);
                case CQS::ValueType::Int: return PyLong_FromLongLong((long long)v.AsInt());
                case CQS::ValueType::Float: return PyFloat_FromDouble(v.AsFloat());
                default: break;
            }
            if (!v.IsObject() || depth > 16) Py_RETURN_NONE;
            switch (v.ObjectVal->Type)
            {
                case ObjectType::String:
                {
                    const std::string& s = v.AsString()->Value;
                    return PyUnicode_DecodeUTF8(s.data(), (Py_ssize_t)s.size(), "replace");
                }
                case ObjectType::List:
                {
                    const auto& e = v.AsList()->Elements;
                    PyObject* list = PyList_New((Py_ssize_t)e.size());
                    for (size_t i = 0; i < e.size(); ++i)
                        PyList_SET_ITEM(list, (Py_ssize_t)i, ToPy(e[i], depth + 1));
                    return list;
                }
                case ObjectType::Map:
                {
                    PyObject* dict = g_AttrDictType ? PyObject_CallNoArgs(g_AttrDictType) : PyDict_New();
                    for (auto& [k, val] : v.AsMap()->Entries)
                    {
                        PyObject* item = ToPy(val, depth + 1);
                        PyDict_SetItemString(dict, k.c_str(), item);
                        Py_DECREF(item);
                    }
                    return dict;
                }
                default:
                    return PyUnicode_FromString(v.ToString().c_str());
            }
        }

        CQS::Value FromPy(PyObject* o, int depth = 0)
        {
            using CQS::Value;
            if (!o || o == Py_None) return Value();
            if (PyBool_Check(o)) return Value::MakeBool(o == Py_True);
            if (PyLong_Check(o)) return Value::MakeInt((int64_t)PyLong_AsLongLong(o));
            if (PyFloat_Check(o)) return Value::MakeFloat(PyFloat_AsDouble(o));
            if (PyUnicode_Check(o))
            {
                Py_ssize_t len = 0;
                const char* s = PyUnicode_AsUTF8AndSize(o, &len);
                return Value::MakeString(s ? std::string(s, (size_t)len) : std::string());
            }
            if (depth > 16) return Value();
            if (PyList_Check(o) || PyTuple_Check(o))
            {
                auto* list = new CQS::CQSListObject();
                Py_ssize_t n = PySequence_Size(o);
                for (Py_ssize_t i = 0; i < n; ++i)
                {
                    PyObject* item = PySequence_GetItem(o, i);
                    list->Elements.push_back(FromPy(item, depth + 1));
                    Py_XDECREF(item);
                }
                return Value::MakeObject(list);
            }
            if (PyDict_Check(o))
            {
                auto* map = new CQS::CQSMapObject();
                PyObject *key, *val;
                Py_ssize_t pos = 0;
                while (PyDict_Next(o, &pos, &key, &val))
                {
                    PyObject* ks = PyObject_Str(key);
                    if (ks)
                    {
                        map->Entries[PyUnicode_AsUTF8(ks)] = FromPy(val, depth + 1);
                        Py_DECREF(ks);
                    }
                }
                return Value::MakeObject(map);
            }
            PyObject* s = PyObject_Str(o);
            Value r = s ? Value::MakeString(PyUnicode_AsUTF8(s)) : Value();
            Py_XDECREF(s);
            if (!s) PyErr_Clear();
            return r;
        }

        // nativebridge fonksiyonu: self = kayıt indeksi (PyLong)
        PyObject* NativeThunk(PyObject* self, PyObject* args)
        {
            long index = PyLong_AsLong(self);
            const auto& fns = NativeRegistry::Get().GetFunctions();
            if (index < 0 || index >= (long)fns.size())
            {
                PyErr_SetString(PyExc_RuntimeError, "invalid native function");
                return nullptr;
            }
            const NativeFunctionInfo& f = fns[(size_t)index];
            Py_ssize_t argc = PyTuple_Size(args);
            if (f.Arity >= 0 && argc > f.Arity)
            {
                PyErr_Format(PyExc_TypeError, "%s() takes %d argument(s) but %zd were given", f.Name.c_str(), f.Arity, argc);
                return nullptr;
            }
            int slots = std::max((int)argc, f.Arity < 0 ? (int)argc : f.Arity);
            std::vector<CQS::Value> values((size_t)std::max(slots, 1));
            for (Py_ssize_t i = 0; i < argc; ++i)
                values[(size_t)i] = FromPy(PyTuple_GetItem(args, i));
            CQS::Value r = f.Function((int)argc, values.data());
            return ToPy(r);
        }

        PyObject* LogWrite(PyObject*, PyObject* args)
        {
            const char* text = nullptr;
            int isError = 0;
            if (!PyArg_ParseTuple(args, "s|p", &text, &isError))
                return nullptr;
            static std::string s_Pending[2];
            std::string& buf = s_Pending[isError ? 1 : 0];
            buf += text;
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos)
            {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (line.empty()) continue;
                if (isError) CQ_CORE_ERROR("[Python] {0}", line);
                else CQ_CORE_INFO("[Python] {0}", line);
            }
            Py_RETURN_NONE;
        }

        PyMethodDef g_LogDef = { "_cq_write", LogWrite, METH_VARARGS, "engine log sink" };
        std::vector<PyMethodDef> g_NativeDefs;

        PyModuleDef g_ModuleDef = { PyModuleDef_HEAD_INIT, "nativebridge", "Conqueror Engine API", -1, nullptr, nullptr, nullptr, nullptr, nullptr };

        PyObject* CreateNativeModule()
        {
            PyObject* module = PyModule_Create(&g_ModuleDef);
            if (!module) return nullptr;
            const auto& registry = NativeRegistry::Get();
            const auto& fns = registry.GetFunctions();
            g_NativeDefs.resize(fns.size());
            // Python yerleşikleriyle çakışan adlar (print, len, ...) eklenmez: "from nativebridge import *" onları gölgelemesin
            PyObject* builtins = PyImport_ImportModule("builtins");
            for (size_t i = 0; i < fns.size(); ++i)
            {
                if (builtins && PyObject_HasAttrString(builtins, fns[i].Name.c_str()))
                    continue;
                g_NativeDefs[i] = { fns[i].Name.c_str(), NativeThunk, METH_VARARGS, nullptr };
                PyObject* idx = PyLong_FromLong((long)i);
                PyObject* fn = PyCFunction_NewEx(&g_NativeDefs[i], idx, nullptr);
                Py_DECREF(idx);
                PyModule_AddObject(module, fns[i].Name.c_str(), fn);
            }
            for (const auto& [name, value] : registry.GetConstants())
                PyModule_AddObject(module, name.c_str(), ToPy(value));
            Py_XDECREF(builtins);
            PyErr_Clear();
            // Data Asset temel sınıfı (Unity ScriptableObject, Unreal UDataAsset): "class WeaponData(DataAsset)" sınıf
            // öznitelikleri veri şablonudur; oyunda WeaponData.load("Assets/Sword.cqdata") alanları dolu bir nesne verir
            static const char* kDataAsset =
                "class DataAsset:\n"
                "    \"\"\"Base of Data Asset classes: class attributes are the data template, values live in the .cqdata file.\"\"\"\n"
                "    _asset_path = None\n"
                "    @classmethod\n"
                "    def load(cls, path):\n"
                "        \"\"\"Load a .cqdata file as this class (None if it cannot be read).\"\"\"\n"
                "        data = LoadDataAsset(path)\n"
                "        if data is None:\n"
                "            return None\n"
                "        obj = cls()\n"
                "        for k, v in data.items():\n"
                "            setattr(obj, k, v)\n"
                "        obj._asset_path = path\n"
                "        return obj\n"
                "    @property\n"
                "    def asset_path(self):\n"
                "        return self._asset_path\n";
            PyObject* dict = PyModule_GetDict(module);
            if (!PyDict_GetItemString(dict, "__builtins__"))
                PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
            if (PyObject* r = PyRun_String(kDataAsset, Py_file_input, dict, dict))
                Py_DECREF(r);
            else
            {
                CQ_CORE_ERROR("[Python] DataAsset class could not be defined");
                PyErr_Clear();
            }
            return module;
        }

        PyObject* InitNativeModule()
        {
            return CreateNativeModule();
        }

        // Python istisnasını (izle) metne çevir ve temizle
        std::string FetchError()
        {
            PyObject* exc = PyErr_GetRaisedException();
            if (!exc) return "unknown Python error";
            std::string text;
            PyObject* tbmod = PyImport_ImportModule("traceback");
            if (tbmod)
            {
                PyObject* lines = PyObject_CallMethod(tbmod, "format_exception", "O", exc);
                if (lines)
                {
                    PyObject* empty = PyUnicode_FromString("");
                    PyObject* joined = PyUnicode_Join(empty, lines);
                    if (joined) { text = PyUnicode_AsUTF8(joined); Py_DECREF(joined); }
                    Py_DECREF(empty);
                    Py_DECREF(lines);
                }
                Py_DECREF(tbmod);
            }
            if (text.empty())
            {
                PyObject* s = PyObject_Str(exc);
                if (s) { text = PyUnicode_AsUTF8(s); Py_DECREF(s); }
            }
            Py_DECREF(exc);
            PyErr_Clear();
            while (!text.empty() && text.back() == '\n') text.pop_back();
            return text;
        }

        struct Instance
        {
            PyObject* Object = nullptr;
            std::string Path;
            std::string ClassName;
            uint32_t Entity = 0xFFFFFFFF;
        };
    }

    class PythonRuntime final : public ScriptRuntime
    {
    public:
        ~PythonRuntime() override
        {
            if (m_Initialized && Py_IsInitialized())
                ClearAll();
            // Py_Finalize bilerek çağrılmaz (C uzantıları kapanışta güvenli değil); süreç bitince işletim sistemi temizler
        }

        const char* GetName() const override { return "Python"; }
        const char* GetLanguageId() const override { return "py"; }
        std::vector<std::string> GetExtensions() const override { return { ".py" }; }

        // Yorumlayıcı ilk Python betiği yüklenirken başlar (LoadScript): Python betiği olmayan projede Play'in ilk karesi ~0,2 sn
        // yorumlayıcı açılışını beklemesin
        void OnPlayStart() override { if (m_Initialized) ClearAll(); }
        void OnPlayStop() override { if (m_Initialized) ClearAll(); }

        bool LoadScript(const std::string& path, std::string* error) override
        {
            if (!EnsureInit())
            {
                if (error) *error = "Python could not be initialized";
                return false;
            }
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
            PyObject* module = ExecModule(source, path, error);
            if (!module)
                return false;
            if (it != m_Modules.end() && it->second.Object)
            {
                ForgetModule(it->second.Object);
                Py_XDECREF(it->second.Object);
            }
            m_Modules[path] = { module, hash };
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override
        {
            auto it = m_Modules.find(path);
            if (it != m_Modules.end())
            {
                std::vector<std::string> classes;
                CollectClasses(it->second.Object, classes);
                std::string stem = std::filesystem::path(path).stem().string();
                for (auto& c : classes) if (c == stem) return c;
                if (!classes.empty()) return classes.front();
            }
            return std::filesystem::path(path).stem().string();
        }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            auto it = m_Modules.find(path);
            if (it == m_Modules.end()) return nullptr;
            PyObject* cls = PyObject_GetAttrString(it->second.Object, className.c_str());
            if (!cls || !PyType_Check(cls))
            {
                Py_XDECREF(cls);
                PyErr_Clear();
                ReportError("Class '" + className + "' not found in " + std::filesystem::path(path).filename().string());
                return nullptr;
            }
            PyObject* obj;
            {
                ScriptContext::EntityScope scope(entity);
                obj = PyObject_CallNoArgs(cls);
            }
            Py_DECREF(cls);
            if (!obj)
            {
                ReportError(FetchError());
                return nullptr;
            }
            PyObject* ent = PyLong_FromUnsignedLong(entity);
            PyObject_SetAttrString(obj, "entity", ent);
            Py_DECREF(ent);
            PyErr_Clear();
            auto* inst = new Instance{ obj, path, className, entity };
            m_Instances.push_back(inst);
            return inst;
        }

        void DestroyInstance(void* instance) override
        {
            auto* inst = static_cast<Instance*>(instance);
            auto it = std::find(m_Instances.begin(), m_Instances.end(), inst);
            if (it == m_Instances.end()) return;
            Py_XDECREF(inst->Object);
            m_Instances.erase(it);
            delete inst;
        }

        bool HasMethod(void* instance, const std::string& method) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            PyObject* m = FindMethod(inst->Object, method);
            bool has = m != nullptr;
            Py_XDECREF(m);
            return has;
        }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            PyObject* m = FindMethod(inst->Object, method);
            if (!m) return true;
            // Python, eksik/fazla argümanda hata verir: metot imzasına göre kırp
            int want = ParamCount(m);
            int n = want >= 0 ? std::min(argc, want) : argc;
            PyObject* tuple = PyTuple_New(n);
            for (int i = 0; i < n; ++i)
                PyTuple_SET_ITEM(tuple, i, ToPy(args[i]));
            PyObject* r;
            {
                ScriptContext::EntityScope scope(inst->Entity);
                r = PyObject_CallObject(m, tuple);
            }
            Py_DECREF(tuple);
            Py_DECREF(m);
            if (!r)
            {
                ReportError(FetchError());
                return false;
            }
            Py_DECREF(r);
            return true;
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            PyObject* v = ToPy(ScriptRuntimes::ToValue(value));
            int rc = PyObject_SetAttrString(inst->Object, field.c_str(), v);
            Py_DECREF(v);
            if (rc != 0) PyErr_Clear();
            return rc == 0;
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            auto* inst = Valid(instance);
            if (!inst) return false;
            PyObject* v = PyObject_GetAttrString(inst->Object, field.c_str());
            if (!v)
            {
                PyErr_Clear();
                return false;
            }
            CQS::Value cv = FromPy(v);
            Py_DECREF(v);
            value = ScriptRuntimes::FromValue(cv);
            if (display) *display = cv.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            if (!m_Initialized || !m_Modules.count(path))
                return 0;
            if (!LoadScript(path, error))
                return -1;
            int updated = 0;
            PyObject* module = m_Modules[path].Object;
            for (Instance* inst : m_Instances)
            {
                if (inst->Path != path) continue;
                PyObject* cls = PyObject_GetAttrString(module, inst->ClassName.c_str());
                if (cls && PyType_Check(cls) && PyObject_SetAttrString(inst->Object, "__class__", cls) == 0)
                    ++updated;
                else
                    PyErr_Clear();
                Py_XDECREF(cls);
            }
            CQ_CORE_INFO("[Python] Hot reloaded {0} ({1} instance{2})", std::filesystem::path(path).filename().string(), updated, updated == 1 ? "" : "s");
            return updated;
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            PyObject* module = InspectModule(path, error);
            if (!module) return false;
            CollectClasses(module, classes);
            // Dosya adıyla aynı sınıf önce
            std::string stem = std::filesystem::path(path).stem().string();
            auto it = std::find(classes.begin(), classes.end(), stem);
            if (it != classes.end()) std::rotate(classes.begin(), it, it + 1);
            ForgetModule(module);
            Py_DECREF(module);
            return !classes.empty();
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            PyObject* module = InspectModule(path, error);
            if (!module) return false;
            PyObject* cls = PyObject_GetAttrString(module, className.c_str());
            if (!cls || !PyType_Check(cls))
            {
                Py_XDECREF(cls);
                PyErr_Clear();
                ForgetModule(module);
                Py_DECREF(module);
                if (error) *error = "Class '" + className + "' not found";
                return false;
            }
            PyObject* dict = PyObject_GetAttrString(cls, "__dict__");
            PyObject* items = dict ? PyMapping_Items(dict) : nullptr;
            Py_ssize_t n = items ? PyList_Size(items) : 0;
            for (Py_ssize_t i = 0; i < n; ++i)
            {
                PyObject* pair = PyList_GetItem(items, i);
                PyObject* key = PyTuple_GetItem(pair, 0);
                PyObject* val = PyTuple_GetItem(pair, 1);
                const char* name = PyUnicode_AsUTF8(key);
                if (!name || name[0] == '_') continue;
                ScriptFieldInfo info;
                info.Name = name;
                info.HasDefault = true;
                if (PyBool_Check(val)) { info.Default = ScriptValue::MakeBool(val == Py_True); info.TypeName = "bool"; }
                else if (PyLong_Check(val)) { info.Default = ScriptValue::MakeInt(PyLong_AsLongLong(val)); info.TypeName = "int"; }
                else if (PyFloat_Check(val)) { info.Default = ScriptValue::MakeFloat(PyFloat_AsDouble(val)); info.TypeName = "float"; }
                else if (PyUnicode_Check(val)) { info.Default = ScriptValue::MakeString(PyUnicode_AsUTF8(val)); info.TypeName = "str"; }
                else continue;
                fields.push_back(info);
            }
            Py_XDECREF(items);
            Py_XDECREF(dict);
            Py_DECREF(cls);
            ForgetModule(module);
            Py_DECREF(module);
            PyErr_Clear();
            return true;
        }

        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            (void)warnings;
            if (!EnsureInit()) return true;
            PyObject* code = Py_CompileString(source.c_str(), name.c_str(), Py_file_input);
            if (code)
            {
                Py_DECREF(code);
                return true;
            }
            PyObject* exc = PyErr_GetRaisedException();
            int line = 0;
            std::string msg = "syntax error";
            if (exc)
            {
                PyObject* lineno = PyObject_GetAttrString(exc, "lineno");
                if (lineno && PyLong_Check(lineno)) line = (int)PyLong_AsLong(lineno);
                Py_XDECREF(lineno);
                PyObject* m = PyObject_GetAttrString(exc, "msg");
                if (m && PyUnicode_Check(m)) msg = PyUnicode_AsUTF8(m);
                Py_XDECREF(m);
                Py_DECREF(exc);
            }
            PyErr_Clear();
            errors.push_back(name + ": Line " + std::to_string(line) + ": " + msg);
            return false;
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            return "from nativebridge import *\n\n\n"
                   "class " + className + ":\n"
                   "    # Class attributes appear in the Inspector (names starting with '_' are hidden)\n"
                   "    speed = 1.0\n\n"
                   "    def start(self):\n"
                   "        pass\n\n"
                   "    def update(self, dt):\n"
                   "        pass\n";
        }

    private:
        struct Module
        {
            // Üye adı yapının adıyla aynı olamaz (MSVC onu yapıcı sanıyor: C2461 / C7624)
            PyObject* Object = nullptr;
            size_t Hash = 0;
        };

        bool m_Initialized = false;
        bool m_InitFailed = false;
        std::unordered_map<std::string, Module> m_Modules;
        std::vector<Instance*> m_Instances;
        std::unordered_map<std::string, double> m_LastError;
        int m_ModuleCounter = 0;

        Instance* Valid(void* instance)
        {
            if (!m_Initialized || !instance) return nullptr;
            auto* inst = static_cast<Instance*>(instance);
            return std::find(m_Instances.begin(), m_Instances.end(), inst) != m_Instances.end() ? inst : nullptr;
        }

        static PyObject* FindMethod(PyObject* obj, const std::string& name)
        {
            for (const std::string& n : { name, SnakeCase(name) })
            {
                PyObject* m = PyObject_GetAttrString(obj, n.c_str());
                if (m && PyCallable_Check(m)) return m;
                Py_XDECREF(m);
                PyErr_Clear();
            }
            return nullptr;
        }

        // Bağlı metodun self hariç pozisyonel parametre sayısı (*args: -1)
        static int ParamCount(PyObject* method)
        {
            PyObject* func = PyObject_GetAttrString(method, "__func__");
            if (!func) { PyErr_Clear(); return -1; }
            PyObject* code = PyObject_GetAttrString(func, "__code__");
            Py_DECREF(func);
            if (!code) { PyErr_Clear(); return -1; }
            int count = -1;
            PyObject* argcount = PyObject_GetAttrString(code, "co_argcount");
            PyObject* flags = PyObject_GetAttrString(code, "co_flags");
            if (argcount && flags)
            {
                long f = PyLong_AsLong(flags);
                count = (f & 0x04) ? -1 : (int)PyLong_AsLong(argcount) - 1; // CO_VARARGS
            }
            Py_XDECREF(argcount);
            Py_XDECREF(flags);
            Py_DECREF(code);
            PyErr_Clear();
            return count;
        }

        void ReportError(const std::string& message)
        {
            double now = Now();
            auto it = m_LastError.find(message);
            if (it != m_LastError.end() && now - it->second < 3.0) return;
            m_LastError[message] = now;
            if (m_LastError.size() > 256) m_LastError.clear();
            CQ_CORE_ERROR("[Python] {0}", message);
        }

        bool EnsureInit()
        {
            if (m_Initialized) return true;
            if (m_InitFailed) return false;
            if (!Py_IsInitialized())
            {
#if defined(CQ_PYTHON_MODULE) && !defined(_WIN32)
                // Modül RTLD_LOCAL yüklendiğinden libpython sembolleri genel değil; lib-dynload altındaki
                // eklenti modülleri (math, _socket ...) PyFloat_Type gibi sembolleri bulabilsin diye yükselt
                Dl_info pyInfo{};
                if (dladdr(reinterpret_cast<void*>(&Py_IsInitialized), &pyInfo) && pyInfo.dli_fname)
                    dlopen(pyInfo.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
#endif
                // Python varsayılan olarak setlocale(LC_CTYPE, "") çağırır; süreç yerel ayarına
                // dokunmasın (Türkçe 'I' anahtar eşlemesini bozar), metin yine de hep UTF-8 olsun.
                PyPreConfig preconfig;
                PyPreConfig_InitPythonConfig(&preconfig);
                preconfig.configure_locale = 0;
                preconfig.utf8_mode = 1;
                Py_PreInitialize(&preconfig);
                PyImport_AppendInittab("nativebridge", &InitNativeModule);
                PyConfig config;
                PyConfig_InitPythonConfig(&config);
                config.install_signal_handlers = 0;
                config.parse_argv = 0;
                PyStatus status = Py_InitializeFromConfig(&config);
                PyConfig_Clear(&config);
                if (PyStatus_Exception(status))
                {
                    m_InitFailed = true;
                    CQ_CORE_ERROR("[Python] Initialization failed: {0}", status.err_msg ? status.err_msg : "?");
                    return false;
                }
            }
            // Çıktıyı motor günlüğüne yönlendir, r.x biçimi için öznitelikli sözlük
            PyObject* builtins = PyEval_GetBuiltins();
            PyObject* writer = PyCFunction_New(&g_LogDef, nullptr);
            PyDict_SetItemString(builtins, "_cq_write", writer);
            Py_DECREF(writer);
            const char* bootstrap =
                "import sys\n"
                "class _CQStream:\n"
                "    def __init__(self, err): self.err = err\n"
                "    def write(self, s): _cq_write(s, self.err); return len(s)\n"
                "    def flush(self): pass\n"
                "sys.stdout = _CQStream(False)\n"
                "sys.stderr = _CQStream(True)\n"
                "class AttrDict(dict):\n"
                "    __getattr__ = dict.get\n"
                "    def __setattr__(self, k, v): self[k] = v\n"
                "import builtins as _b\n"
                "_b._CQAttrDict = AttrDict\n";
            if (PyRun_SimpleString(bootstrap) != 0)
                CQ_CORE_WARN("[Python] stdout redirection failed");
            PyObject* main = PyImport_AddModule("__main__");
            if (PyObject* ad = PyObject_GetAttrString(main, "AttrDict"))
                g_AttrDictType = ad; // kalıcı referans
            PyErr_Clear();
            // Proje betik klasörleri import yolunda
            auto dir = Project::GetActiveProjectDirectory();
            if (!dir.empty())
            {
                PyObject* sysPath = PySys_GetObject("path");
                for (auto sub : { dir / "Assets" / "Scripts", dir / "Assets" })
                {
                    PyObject* p = PyUnicode_FromString(sub.string().c_str());
                    PyList_Insert(sysPath, 0, p);
                    Py_DECREF(p);
                }
            }
            m_Initialized = true;
            CQ_CORE_INFO("[Python] {0} embedded", Py_GetVersion());
            return true;
        }

        PyObject* ExecModule(const std::string& source, const std::string& path, std::string* error)
        {
            PyObject* code = Py_CompileString(source.c_str(), path.c_str(), Py_file_input);
            if (!code)
            {
                std::string msg = FetchError();
                ReportError(msg);
                if (error) *error = msg;
                return nullptr;
            }
            std::string name = "cqscript_" + std::filesystem::path(path).stem().string() + "_" + std::to_string(++m_ModuleCounter);
            PyObject* module = PyImport_ExecCodeModuleEx(name.c_str(), code, path.c_str());
            Py_DECREF(code);
            if (!module)
            {
                std::string msg = FetchError();
                ReportError(msg);
                if (error) *error = msg;
                return nullptr;
            }
            return module;
        }

        // İnceleme için modülü geçici olarak çalıştır
        PyObject* InspectModule(const std::string& path, std::string* error)
        {
            if (!EnsureInit())
            {
                if (error) *error = "Python could not be initialized";
                return nullptr;
            }
            std::string source;
            if (!ReadFile(path, source))
            {
                if (error) *error = "Cannot open " + path;
                return nullptr;
            }
            return ExecModule(source, path, error);
        }

        // sys.modules kaydını sil (inceleme modülleri birikmesin)
        static void ForgetModule(PyObject* module)
        {
            PyObject* name = PyObject_GetAttrString(module, "__name__");
            PyObject* modules = PyImport_GetModuleDict();
            if (name && modules && PyDict_Contains(modules, name) == 1)
                PyDict_DelItem(modules, name);
            Py_XDECREF(name);
            PyErr_Clear();
        }

        static void CollectClasses(PyObject* module, std::vector<std::string>& out)
        {
            PyObject* dict = PyModule_GetDict(module);
            PyObject* modName = PyObject_GetAttrString(module, "__name__");
            PyObject *key, *val;
            Py_ssize_t pos = 0;
            while (PyDict_Next(dict, &pos, &key, &val))
            {
                if (!PyType_Check(val)) continue;
                PyObject* owner = PyObject_GetAttrString(val, "__module__");
                bool local = owner && modName && PyObject_RichCompareBool(owner, modName, Py_EQ) == 1;
                Py_XDECREF(owner);
                if (local) out.push_back(PyUnicode_AsUTF8(key));
            }
            Py_XDECREF(modName);
            PyErr_Clear();
        }

        void ClearAll()
        {
            for (Instance* inst : m_Instances)
            {
                Py_XDECREF(inst->Object);
                delete inst;
            }
            m_Instances.clear();
            for (auto& [p, m] : m_Modules)
            {
                if (m.Object) ForgetModule(m.Object);
                Py_XDECREF(m.Object);
            }
            m_Modules.clear();
        }
    };

#ifdef CQ_PYTHON_MODULE
}

// Ayrı modül (libCQPython): motor bu işlevi dlopen ile bulur
extern "C" __attribute__((visibility("default"))) Conqueror::Scripting::ScriptRuntime* CQ_CreatePythonRuntimeModule()
{
    return new Conqueror::Scripting::PythonRuntime();
}

namespace Conqueror::Scripting
{
#else
    std::unique_ptr<ScriptRuntime> CreatePythonRuntime()
    {
        return std::make_unique<PythonRuntime>();
    }
#endif
#elif !defined(CQ_PYTHON_MODULE)
    std::unique_ptr<ScriptRuntime> CreatePythonRuntime()
    {
        return nullptr;
    }
#endif
}
