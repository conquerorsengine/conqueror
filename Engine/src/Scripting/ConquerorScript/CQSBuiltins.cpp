#include "CQSVM.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace Conqueror::CQS
{
    namespace
    {
        std::mt19937_64& Rng()
        {
            static std::mt19937_64 s_Rng{ std::random_device{}() };
            return s_Rng;
        }

        CQSMapObject* MakeVector(std::initializer_list<std::pair<const char*, double>> comps)
        {
            auto* m = new CQSMapObject();
            for (auto& [k, v] : comps)
                m->Entries[k] = Value::MakeFloat(v);
            return m;
        }

        std::string Trim(const std::string& s, bool left, bool right)
        {
            size_t a = 0, b = s.size();
            if (left) while (a < b && std::isspace((unsigned char)s[a])) ++a;
            if (right) while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        std::string FormatWithDecimals(double v, int decimals, bool grouping)
        {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 20), v);
            std::string s = buf;
            if (!grouping)
                return s;
            size_t dot = s.find('.');
            size_t intEnd = dot == std::string::npos ? s.size() : dot;
            size_t start = (s[0] == '-') ? 1 : 0;
            std::string out = s.substr(0, start);
            std::string digits = s.substr(start, intEnd - start);
            for (size_t i = 0; i < digits.size(); ++i)
            {
                out += digits[i];
                size_t rem = digits.size() - i - 1;
                if (rem > 0 && rem % 3 == 0) out += ',';
            }
            out += s.substr(intEnd);
            return out;
        }
    }

    // ════════════════════════════════════════
    //  ToString / format
    // ════════════════════════════════════════
    bool CQSVM::ValueToString(const Value& v, std::string& out)
    {
        if (v.IsInstance() && v.AsInstance()->Class)
        {
            const Value* m = v.AsInstance()->Class->FindMethod("ToString");
            if (m && m->IsObjType(ObjectType::Closure))
            {
                Value* base = m_StackTop;
                if (base + 8 >= m_StackEnd)
                {
                    RuntimeError("StackOverflow: Stack overflow in ToString().");
                    return false;
                }
                base[0] = v;
                m_StackTop = base + 1;
                if (!CallInstanceMethodWithEntity(v.AsInstance(), m->AsClosure(), base, 0))
                {
                    m_StackTop = base;
                    return false;
                }
                Value r = base[0];
                m_StackTop = base;
                out = r.IsString() ? r.AsString()->Value : r.ToString();
                return true;
            }
        }
        out = v.ToString();
        return true;
    }

    bool CQSVM::FormatValue(const Value& v, const std::string& format, std::string& out)
    {
        if (format.empty() || !v.IsNumber())
            return ValueToString(v, out);
        char kind = (char)std::toupper((unsigned char)format[0]);
        bool hasDigits = format.size() > 1 && std::isdigit((unsigned char)format[1]);
        int digits = hasDigits ? std::atoi(format.c_str() + 1) : -1;
        double d = v.ToNumber();
        switch (kind)
        {
            case 'F': out = FormatWithDecimals(d, digits < 0 ? 2 : digits, false); return true;
            case 'N': out = FormatWithDecimals(d, digits < 0 ? 2 : digits, true); return true;
            case 'P': out = FormatWithDecimals(d * 100.0, digits < 0 ? 2 : digits, false) + "%"; return true;
            case 'E':
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf), format[0] == 'e' ? "%.*e" : "%.*E", digits < 0 ? 6 : digits, d);
                out = buf;
                return true;
            }
            case 'D':
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%0*lld", digits < 0 ? 1 : digits, (long long)v.ToInt());
                out = buf;
                return true;
            }
            case 'X':
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf), format[0] == 'x' ? "%0*llx" : "%0*llX", digits < 0 ? 1 : digits, (unsigned long long)v.ToInt());
                out = buf;
                return true;
            }
            case 'G':
                out = v.ToString();
                return true;
            default:
                break;
        }
        // Özel desen: "0.00", "#.##", "000"
        size_t dot = format.find('.');
        if (format.find_first_not_of("0#.,") == std::string::npos)
        {
            int decimals = dot == std::string::npos ? 0 : (int)(format.size() - dot - 1);
            int minInt = 0;
            for (size_t i = 0; i < (dot == std::string::npos ? format.size() : dot); ++i)
                if (format[i] == '0') ++minInt;
            bool grouping = format.find(',') != std::string::npos;
            std::string s = FormatWithDecimals(d, decimals, grouping);
            // '#' ondalıklar: sondaki sıfırları at
            if (dot != std::string::npos && format.find('#', dot) != std::string::npos && s.find('.') != std::string::npos)
            {
                while (!s.empty() && s.back() == '0') s.pop_back();
                if (!s.empty() && s.back() == '.') s.pop_back();
            }
            // En az tam sayı basamağı
            size_t neg = (!s.empty() && s[0] == '-') ? 1 : 0;
            size_t intLen = (s.find('.') == std::string::npos ? s.size() : s.find('.')) - neg;
            if ((int)intLen < minInt)
                s.insert(neg, (size_t)(minInt - (int)intLen), '0');
            out = s;
            return true;
        }
        RuntimeError("FormatException: Unknown format specifier '%s'.", format.c_str());
        return false;
    }

    // ════════════════════════════════════════
    //  Builtin global functions
    // ════════════════════════════════════════
    void CQSVM::DefineBuiltins()
    {
        CQSVM* vm = this;

        DefineNative("List", [](int argc, Value* args) -> Value {
            auto* list = new CQSListObject();
            if (argc == 1 && args[0].IsList())
                list->Elements = args[0].AsList()->Elements;
            else if (argc == 1 && args[0].IsRange())
            {
                auto* r = args[0].AsRange();
                for (int64_t i = 0; i < r->Count(); ++i) list->Elements.push_back(Value::MakeInt(r->At(i)));
            }
            else if (argc == 1 && args[0].IsNumber())
                list->Elements.reserve((size_t)std::clamp<int64_t>(args[0].ToInt(), 0, 1 << 20)); // kapasite (C#)
            else
                for (int i = 0; i < argc; ++i) list->Elements.push_back(args[i]);
            return Value::MakeObject(list);
        }, -1);
        auto mapCtor = [](int argc, Value* args) -> Value {
            auto* map = new CQSMapObject();
            if (argc == 1 && args[0].IsMap())
                map->Entries = args[0].AsMap()->Entries;
            return Value::MakeObject(map);
        };
        DefineNative("Map", mapCtor, -1);
        DefineNative("Dictionary", mapCtor, -1);
        DefineNative("HashMap", mapCtor, -1);
        DefineNative("Vector2", [](int argc, Value* args) -> Value {
            return Value::MakeObject(MakeVector({ { "x", NativeArg(argc, args, 0).ToNumber() }, { "y", NativeArg(argc, args, 1).ToNumber() } }));
        }, 2);
        DefineNative("Vector3", [](int argc, Value* args) -> Value {
            return Value::MakeObject(MakeVector({ { "x", NativeArg(argc, args, 0).ToNumber() }, { "y", NativeArg(argc, args, 1).ToNumber() }, { "z", NativeArg(argc, args, 2).ToNumber() } }));
        }, 3);
        DefineNative("Vector4", [](int argc, Value* args) -> Value {
            return Value::MakeObject(MakeVector({ { "x", NativeArg(argc, args, 0).ToNumber() }, { "y", NativeArg(argc, args, 1).ToNumber() },
                                                  { "z", NativeArg(argc, args, 2).ToNumber() }, { "w", NativeArg(argc, args, 3).ToNumber() } }));
        }, 4);
        DefineNative("Color", [](int argc, Value* args) -> Value {
            auto* m = MakeVector({ { "r", NativeArg(argc, args, 0).ToNumber() }, { "g", NativeArg(argc, args, 1).ToNumber() },
                                   { "b", NativeArg(argc, args, 2).ToNumber() }, { "a", argc > 3 ? args[3].ToNumber() : 1.0 } });
            return Value::MakeObject(m);
        }, 4);

        DefineNative("len", [vm](int, Value* args) -> Value {
            const Value& v = args[0];
            if (v.IsString()) return Value::MakeInt((int64_t)v.AsString()->Value.size());
            if (v.IsList()) return Value::MakeInt((int64_t)v.AsList()->Elements.size());
            if (v.IsMap()) return Value::MakeInt((int64_t)v.AsMap()->Entries.size());
            if (v.IsRange()) return Value::MakeInt(v.AsRange()->Count());
            vm->ThrowError("TypeError: len() argument has no length.");
            return Value();
        }, 1);
        DefineNative("str", [vm](int, Value* args) -> Value {
            std::string s;
            if (!vm->ValueToString(args[0], s)) return Value();
            return Value::MakeString(s);
        }, 1);
        DefineNative("int", [vm](int, Value* args) -> Value {
            Value v = args[0];
            if (!vm->CastToType(&v, "int")) return Value();
            return v;
        }, 1);
        DefineNative("float", [vm](int, Value* args) -> Value {
            Value v = args[0];
            if (!vm->CastToType(&v, "float")) return Value();
            return v;
        }, 1);
        DefineNative("bool", [](int, Value* args) -> Value { return Value::MakeBool(args[0].IsTruthy()); }, 1);
        DefineNative("typeof", [](int, Value* args) -> Value { return Value::MakeString(args[0].TypeName()); }, 1);
        DefineNative("type", [](int, Value* args) -> Value { return Value::MakeString(args[0].TypeName()); }, 1);
        DefineNative("range", [vm](int argc, Value* args) -> Value {
            if (argc == 1) return Value::MakeObject(new CQSRangeObject(0, args[0].ToInt()));
            if (argc == 2) return Value::MakeObject(new CQSRangeObject(args[0].ToInt(), args[1].ToInt()));
            vm->ThrowError("ArgumentError: range() takes 1 or 2 arguments.");
            return Value();
        }, 2);
        DefineNative("assert", [vm](int argc, Value* args) -> Value {
            if (!args[0].IsTruthy())
            {
                std::string msg = argc > 1 ? args[1].ToString() : "Assertion failed.";
                vm->ThrowError("AssertionError: " + msg);
            }
            return Value();
        }, 2);
        DefineNative("error", [vm](int, Value* args) -> Value {
            vm->ThrowError(args[0].ToString());
            return Value();
        }, 1);
        DefineNative("random", [](int argc, Value* args) -> Value {
            if (argc == 0)
                return Value::MakeFloat(std::uniform_real_distribution<double>(0.0, 1.0)(Rng()));
            if (args[0].IsInt() && args[1].IsInt())
            {
                int64_t lo = args[0].AsInt(), hi = args[1].AsInt();
                if (hi <= lo) return Value::MakeInt(lo);
                return Value::MakeInt(std::uniform_int_distribution<int64_t>(lo, hi - 1)(Rng()));   // [lo, hi)
            }
            double lo = args[0].ToNumber(), hi = args[1].ToNumber();
            return Value::MakeFloat(std::uniform_real_distribution<double>(lo, hi > lo ? hi : lo + 1e-9)(Rng()));
        }, 2);
        DefineNative("collectgarbage", [vm](int, Value*) -> Value {
            size_t before = CQSHeap::Get().GetObjectCount();
            vm->CollectGarbage();
            return Value::MakeInt((int64_t)(before - CQSHeap::Get().GetObjectCount()));
        }, 0);
        DefineNative("print", [vm](int argc, Value* args) -> Value {
            std::string line;
            for (int i = 0; i < argc; ++i)
            {
                std::string s;
                if (!vm->ValueToString(args[i], s)) return Value();
                line += (i ? " " : "") + s;
            }
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
            return Value();
        }, -1);
    }

    // ════════════════════════════════════════
    //  Builtin methods (List / Map / String / Range / sayılar)
    // ════════════════════════════════════════
    bool CQSVM::InvokeBuiltinMethod(Value* base, int argc, const std::string& name, bool& handled)
    {
        handled = true;
        Value self = base[0];
        Value* args = base + 1;
        m_StackTop = base + 1 + argc;
        auto arg = [&](int i) -> Value { return i < argc ? args[i] : Value(); };
        auto ret = [&](Value v) { base[0] = v; m_StackTop = base + 1; return true; };
        auto need = [&](int n) {
            if (argc < n)
            {
                RuntimeError("ArgumentError: %s() requires %d argument(s).", name.c_str(), n);
                return false;
            }
            return true;
        };
        // Geri çağırma (comparator / predicate): m_StackTop üstünü kullanır
        auto callFn = [&](const Value& f, std::initializer_list<Value> a, Value& out) -> bool {
            Value* cb = m_StackTop;
            if (cb + a.size() + 8 >= m_StackEnd)
            {
                RuntimeError("StackOverflow: Stack overflow.");
                return false;
            }
            cb[0] = f;
            int i = 1;
            for (const Value& v : a) cb[i++] = v;
            m_StackTop = cb + i;
            // Geri çağırma sırasında C++ tarafında tutulan değerler (sıralama anahtarları vb.) GC'den korunmalı
            CQSNoGCScope noGC;
            bool lenient = m_HostLenient;
            m_HostLenient = true; // (x) => ... yerine (x, i) => ... ile çağrılabilsin diye fazlalık kırpılır
            bool ok = CallValue(cb, (int)a.size());
            m_HostLenient = lenient;
            out = cb[0];
            m_StackTop = cb;
            return ok;
        };

        // ── Her değer: ToString / GetType / Equals ──
        if (name == "ToString" && argc == 0)
        {
            std::string s;
            if (!ValueToString(self, s)) return false;
            return ret(Value::MakeString(s));
        }
        if (name == "ToString" && argc == 1 && self.IsNumber())
        {
            std::string s;
            if (!FormatValue(self, arg(0).ToString(), s)) return false;
            return ret(Value::MakeString(s));
        }
        if (name == "GetType") return ret(Value::MakeString(self.TypeName()));
        if (name == "Equals" && argc == 1) return ret(Value::MakeBool(self.Equals(args[0])));
        if (name == "GetHashCode")
            return ret(Value::MakeInt((int64_t)std::hash<std::string>{}(self.ToString())));

        // ── List ──
        if (self.IsList())
        {
            auto* list = self.AsList();
            auto& e = list->Elements;
            auto index = [&](const Value& v, bool allowEnd, int64_t& out) {
                if (!v.IsNumber())
                {
                    RuntimeError("TypeError: Index must be a number.");
                    return false;
                }
                out = v.ToInt();
                if (out < 0) out += (int64_t)e.size();
                if (out < 0 || out > (int64_t)e.size() || (!allowEnd && out == (int64_t)e.size()))
                {
                    RuntimeError("IndexOutOfRangeException: Index %lld is out of range (list has %zu elements).", (long long)v.ToInt(), e.size());
                    return false;
                }
                return true;
            };
            if (name == "Add" || name == "Push" || name == "Append")
            {
                for (int i = 0; i < argc; ++i) e.push_back(args[i]);
                return ret(Value());
            }
            if (name == "AddRange" || name == "Extend")
            {
                if (!need(1)) return false;
                if (args[0].IsList())
                {
                    auto copy = args[0].AsList()->Elements;
                    e.insert(e.end(), copy.begin(), copy.end());
                }
                else if (args[0].IsRange())
                {
                    auto* r = args[0].AsRange();
                    for (int64_t i = 0; i < r->Count(); ++i) e.push_back(Value::MakeInt(r->At(i)));
                }
                else
                {
                    RuntimeError("TypeError: AddRange() expects a List.");
                    return false;
                }
                return ret(Value());
            }
            if (name == "Insert")
            {
                if (!need(2)) return false;
                int64_t i;
                if (!index(args[0], true, i)) return false;
                e.insert(e.begin() + i, args[1]);
                return ret(Value());
            }
            if (name == "RemoveAt")
            {
                if (!need(1)) return false;
                int64_t i;
                if (!index(args[0], false, i)) return false;
                Value removed = e[(size_t)i];
                e.erase(e.begin() + i);
                return ret(removed);
            }
            if (name == "Remove")
            {
                if (!need(1)) return false;
                for (size_t i = 0; i < e.size(); ++i)
                    if (e[i].Equals(args[0]))
                    {
                        e.erase(e.begin() + (ptrdiff_t)i);
                        return ret(Value::MakeBool(true));
                    }
                return ret(Value::MakeBool(false));
            }
            if (name == "RemoveAll")
            {
                if (!need(1)) return false;
                int64_t removed = 0;
                std::vector<Value> kept;
                for (const Value& v : std::vector<Value>(e))
                {
                    bool match;
                    if (args[0].IsCallable())
                    {
                        Value r;
                        if (!callFn(args[0], { v }, r)) return false;
                        match = r.IsTruthy();
                    }
                    else
                        match = v.Equals(args[0]);
                    if (match) ++removed;
                    else kept.push_back(v);
                }
                e = std::move(kept);
                return ret(Value::MakeInt(removed));
            }
            if (name == "Clear") { e.clear(); return ret(Value()); }
            if (name == "Contains" || name == "Includes")
            {
                if (!need(1)) return false;
                for (const Value& v : e) if (v.Equals(args[0])) return ret(Value::MakeBool(true));
                return ret(Value::MakeBool(false));
            }
            if (name == "IndexOf")
            {
                if (!need(1)) return false;
                for (size_t i = 0; i < e.size(); ++i) if (e[i].Equals(args[0])) return ret(Value::MakeInt((int64_t)i));
                return ret(Value::MakeInt(-1));
            }
            if (name == "LastIndexOf")
            {
                if (!need(1)) return false;
                for (size_t i = e.size(); i > 0; --i) if (e[i - 1].Equals(args[0])) return ret(Value::MakeInt((int64_t)i - 1));
                return ret(Value::MakeInt(-1));
            }
            if (name == "Count" && argc == 0) return ret(Value::MakeInt((int64_t)e.size()));
            if (name == "Count" && argc == 1)
            {
                int64_t n = 0;
                for (const Value& v : std::vector<Value>(e))
                {
                    Value r;
                    if (!callFn(args[0], { v }, r)) return false;
                    if (r.IsTruthy()) ++n;
                }
                return ret(Value::MakeInt(n));
            }
            if (name == "Pop")
            {
                if (e.empty()) { RuntimeError("InvalidOperationException: Pop() on an empty list."); return false; }
                Value v = e.back();
                e.pop_back();
                return ret(v);
            }
            if (name == "Shift" || name == "Dequeue")
            {
                if (e.empty()) { RuntimeError("InvalidOperationException: %s() on an empty list.", name.c_str()); return false; }
                Value v = e.front();
                e.erase(e.begin());
                return ret(v);
            }
            if (name == "Peek")
            {
                if (e.empty()) { RuntimeError("InvalidOperationException: Peek() on an empty list."); return false; }
                return ret(e.back());
            }
            if (name == "Reverse") { std::reverse(e.begin(), e.end()); return ret(self); }
            if (name == "Sort" || name == "SortBy")
            {
                // Karşılaştırıcı yoksa sayı/metin sıralaması; hata güvenli (exception yayılır)
                bool failed = false;
                std::vector<Value> copy = e;
                if (argc == 0)
                {
                    for (const Value& v : copy)
                        if (!(v.IsNumber() || v.IsString()) || (v.IsString() != copy[0].IsString()))
                        {
                            RuntimeError("TypeError: Sort() without a comparer requires all numbers or all strings.");
                            return false;
                        }
                    std::stable_sort(copy.begin(), copy.end(), [](const Value& a, const Value& b) {
                        if (a.IsString()) return a.AsString()->Value < b.AsString()->Value;
                        return a.ToNumber() < b.ToNumber();
                    });
                }
                else if (name == "SortBy")
                {
                    std::vector<std::pair<Value, Value>> keyed;
                    for (const Value& v : copy)
                    {
                        Value key;
                        if (!callFn(args[0], { v }, key)) return false;
                        keyed.emplace_back(key, v);
                    }
                    std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) {
                        if (a.first.IsString() && b.first.IsString()) return a.first.AsString()->Value < b.first.AsString()->Value;
                        return a.first.ToNumber() < b.first.ToNumber();
                    });
                    for (size_t i = 0; i < keyed.size(); ++i) copy[i] = keyed[i].second;
                }
                else
                {
                    Value fnv = args[0];
                    std::stable_sort(copy.begin(), copy.end(), [&](const Value& a, const Value& b) {
                        if (failed) return false;
                        Value r;
                        if (!callFn(fnv, { a, b }, r)) { failed = true; return false; }
                        return r.IsBool() ? r.AsBool() : r.ToNumber() < 0;
                    });
                }
                if (failed) return false;
                e = std::move(copy);
                return ret(self);   // zincirleme: list.Sort().Join(...)
            }
            if (name == "Join")
            {
                std::string sep = argc > 0 ? args[0].ToString() : ",";
                std::string out;
                for (size_t i = 0; i < e.size(); ++i)
                {
                    std::string s;
                    if (!ValueToString(e[i], s)) return false;
                    out += (i ? sep : "") + s;
                }
                return ret(Value::MakeString(out));
            }
            if (name == "Slice" || name == "GetRange")
            {
                int64_t a = argc > 0 ? args[0].ToInt() : 0;
                int64_t b;
                if (name == "GetRange") b = a + (argc > 1 ? args[1].ToInt() : (int64_t)e.size() - a);
                else b = argc > 1 ? args[1].ToInt() : (int64_t)e.size();
                int64_t n = (int64_t)e.size();
                if (a < 0) a += n;
                if (b < 0) b += n;
                a = std::clamp<int64_t>(a, 0, n);
                b = std::clamp<int64_t>(b, a, n);
                auto* out = new CQSListObject();
                out->Elements.assign(e.begin() + a, e.begin() + b);
                return ret(Value::MakeObject(out));
            }
            if (name == "Copy" || name == "Clone" || name == "ToList")
            {
                auto* out = new CQSListObject();
                out->Elements = e;
                return ret(Value::MakeObject(out));
            }
            if (name == "Map" || name == "Select" || name == "Filter" || name == "Where" || name == "ForEach")
            {
                if (!need(1)) return false;
                auto* out = new CQSListObject();
                Value outVal = Value::MakeObject(out);
                base[0] = outVal; // GC kökü
                std::vector<Value> snapshot = e;
                for (size_t i = 0; i < snapshot.size(); ++i)
                {
                    Value r;
                    if (!callFn(args[0], { snapshot[i], Value::MakeInt((int64_t)i) }, r)) return false;
                    if (name == "Map" || name == "Select") out->Elements.push_back(r);
                    else if (name != "ForEach" && r.IsTruthy()) out->Elements.push_back(snapshot[i]);
                }
                return ret(name == "ForEach" ? Value() : outVal);
            }
            if (name == "Find" || name == "FindIndex" || name == "Any" || name == "All" || name == "First" || name == "FirstOrDefault" || name == "Last")
            {
                if (argc == 0 && (name == "First" || name == "Last" || name == "Any"))
                {
                    if (name == "Any") return ret(Value::MakeBool(!e.empty()));
                    if (e.empty()) { RuntimeError("InvalidOperationException: List is empty."); return false; }
                    return ret(name == "First" ? e.front() : e.back());
                }
                if (argc == 0 && name == "FirstOrDefault")
                    return ret(e.empty() ? Value() : e.front());
                if (!need(1)) return false;
                std::vector<Value> snapshot = e;
                if (name == "Last") std::reverse(snapshot.begin(), snapshot.end());
                for (size_t i = 0; i < snapshot.size(); ++i)
                {
                    Value r;
                    if (!callFn(args[0], { snapshot[i] }, r)) return false;
                    bool t = r.IsTruthy();
                    if (name == "All") { if (!t) return ret(Value::MakeBool(false)); continue; }
                    if (t)
                    {
                        if (name == "Any") return ret(Value::MakeBool(true));
                        if (name == "FindIndex") return ret(Value::MakeInt((int64_t)i));
                        return ret(snapshot[i]);
                    }
                }
                if (name == "All") return ret(Value::MakeBool(true));
                if (name == "Any") return ret(Value::MakeBool(false));
                if (name == "FindIndex") return ret(Value::MakeInt(-1));
                return ret(Value());
            }
            if (name == "Reduce" || name == "Aggregate")
            {
                if (!need(1)) return false;
                std::vector<Value> snapshot = e;
                size_t start = 0;
                Value acc;
                if (argc > 1) acc = args[1];
                else
                {
                    if (snapshot.empty()) { RuntimeError("InvalidOperationException: Reduce() of empty list with no initial value."); return false; }
                    acc = snapshot[0];
                    start = 1;
                }
                for (size_t i = start; i < snapshot.size(); ++i)
                {
                    base[0] = acc; // GC kökü
                    if (!callFn(args[0], { acc, snapshot[i] }, acc)) return false;
                }
                return ret(acc);
            }
            if (name == "Sum" || name == "Min" || name == "Max" || name == "Average")
            {
                if (e.empty())
                {
                    if (name == "Sum") return ret(Value::MakeInt(0));
                    RuntimeError("InvalidOperationException: %s() of an empty list.", name.c_str());
                    return false;
                }
                bool allInt = true;
                for (const Value& v : e)
                {
                    if (!v.IsNumber()) { RuntimeError("TypeError: %s() requires a list of numbers.", name.c_str()); return false; }
                    if (!v.IsInt()) allInt = false;
                }
                if (name == "Sum" || name == "Average")
                {
                    int64_t is = 0;
                    double fs = 0.0;
                    for (const Value& v : e) { is += v.ToInt(); fs += v.ToNumber(); }
                    if (name == "Average") return ret(Value::MakeFloat(fs / (double)e.size()));
                    return ret(allInt ? Value::MakeInt(is) : Value::MakeFloat(fs));
                }
                Value best = e[0];
                for (const Value& v : e)
                    if (name == "Min" ? v.ToNumber() < best.ToNumber() : v.ToNumber() > best.ToNumber()) best = v;
                return ret(best);
            }
            if (name == "Shuffle")
            {
                std::shuffle(e.begin(), e.end(), Rng());
                return ret(self);
            }
            if (name == "Distinct")
            {
                auto* out = new CQSListObject();
                for (const Value& v : e)
                {
                    bool dup = false;
                    for (const Value& o : out->Elements) if (o.Equals(v)) { dup = true; break; }
                    if (!dup) out->Elements.push_back(v);
                }
                return ret(Value::MakeObject(out));
            }
            if (name == "Fill")
            {
                for (Value& v : e) v = arg(0);
                return ret(Value());
            }
            if (name == "Resize")
            {
                if (!need(1)) return false;
                int64_t n = args[0].ToInt();
                if (n < 0 || n > 100000000) { RuntimeError("ArgumentOutOfRangeException: Invalid size."); return false; }
                e.resize((size_t)n, arg(1));
                return ret(Value());
            }
            if (name == "IsEmpty") return ret(Value::MakeBool(e.empty()));
            if (name == "Swap")
            {
                if (!need(2)) return false;
                int64_t a, b;
                if (!index(args[0], false, a) || !index(args[1], false, b)) return false;
                std::swap(e[(size_t)a], e[(size_t)b]);
                return ret(Value());
            }
            if (name == "Random" || name == "Choice")
            {
                if (e.empty()) return ret(Value());
                return ret(e[(size_t)std::uniform_int_distribution<size_t>(0, e.size() - 1)(Rng())]);
            }
        }

        // ── Map ──
        if (self.IsMap())
        {
            auto& m = self.AsMap()->Entries;
            auto key = [&](const Value& v) { return v.IsString() ? v.AsString()->Value : v.ToString(); };
            auto sortedKeys = [&]() {
                std::vector<std::string> keys;
                keys.reserve(m.size());
                for (auto& [k, v] : m) keys.push_back(k);
                std::sort(keys.begin(), keys.end());
                return keys;
            };
            if (name == "Keys")
            {
                auto* out = new CQSListObject();
                for (auto& k : sortedKeys()) out->Elements.push_back(Value::MakeString(k));
                return ret(Value::MakeObject(out));
            }
            if (name == "Values")
            {
                auto* out = new CQSListObject();
                for (auto& k : sortedKeys()) out->Elements.push_back(m.at(k));
                return ret(Value::MakeObject(out));
            }
            if (name == "Entries" || name == "Items")
            {
                auto* out = new CQSListObject();
                base[0] = Value::MakeObject(out);
                for (auto& k : sortedKeys())
                {
                    auto* pair = new CQSListObject();
                    pair->Elements.push_back(Value::MakeString(k));
                    pair->Elements.push_back(m.at(k));
                    out->Elements.push_back(Value::MakeObject(pair));
                }
                return ret(base[0]);
            }
            if (name == "ContainsKey" || name == "Has" || name == "HasKey")
            {
                if (!need(1)) return false;
                return ret(Value::MakeBool(m.count(key(args[0])) != 0));
            }
            if (name == "ContainsValue")
            {
                if (!need(1)) return false;
                for (auto& [k, v] : m) if (v.Equals(args[0])) return ret(Value::MakeBool(true));
                return ret(Value::MakeBool(false));
            }
            if (name == "Get" || name == "GetValueOrDefault")
            {
                if (!need(1)) return false;
                auto it = m.find(key(args[0]));
                return ret(it != m.end() ? it->second : arg(1));
            }
            if (name == "Set" || name == "Add" || name == "TryAdd")
            {
                if (!need(2)) return false;
                std::string k = key(args[0]);
                if (name == "Add" && m.count(k))
                {
                    RuntimeError("ArgumentException: An item with the key \"%s\" has already been added.", k.c_str());
                    return false;
                }
                if (name == "TryAdd" && m.count(k)) return ret(Value::MakeBool(false));
                m[k] = args[1];
                return ret(name == "TryAdd" ? Value::MakeBool(true) : Value());
            }
            if (name == "Remove")
            {
                if (!need(1)) return false;
                return ret(Value::MakeBool(m.erase(key(args[0])) != 0));
            }
            if (name == "Clear") { m.clear(); return ret(Value()); }
            if (name == "Count" && argc == 0) return ret(Value::MakeInt((int64_t)m.size()));
            if (name == "Copy" || name == "Clone")
            {
                auto* out = new CQSMapObject();
                out->Entries = m;
                return ret(Value::MakeObject(out));
            }
            if (name == "Merge")
            {
                if (!need(1) || !args[0].IsMap()) { if (argc) RuntimeError("TypeError: Merge() expects a Map."); return false; }
                for (auto& [k, v] : args[0].AsMap()->Entries) m[k] = v;
                return ret(Value());
            }
            if (name == "IsEmpty") return ret(Value::MakeBool(m.empty()));
        }

        // ── String ──
        if (self.IsString())
        {
            const std::string s = self.AsString()->Value;
            auto str = [&](int i) { return i < argc ? (args[i].IsString() ? args[i].AsString()->Value : args[i].ToString()) : std::string(); };
            if (name == "ToUpper") { std::string r = s; for (char& c : r) c = (char)std::toupper((unsigned char)c); return ret(Value::MakeString(r)); }
            if (name == "ToLower") { std::string r = s; for (char& c : r) c = (char)std::tolower((unsigned char)c); return ret(Value::MakeString(r)); }
            if (name == "Trim") return ret(Value::MakeString(Trim(s, true, true)));
            if (name == "TrimStart") return ret(Value::MakeString(Trim(s, true, false)));
            if (name == "TrimEnd") return ret(Value::MakeString(Trim(s, false, true)));
            if (name == "Contains") { if (!need(1)) return false; return ret(Value::MakeBool(s.find(str(0)) != std::string::npos)); }
            if (name == "StartsWith") { if (!need(1)) return false; return ret(Value::MakeBool(s.rfind(str(0), 0) == 0)); }
            if (name == "EndsWith")
            {
                if (!need(1)) return false;
                std::string t = str(0);
                return ret(Value::MakeBool(t.size() <= s.size() && s.compare(s.size() - t.size(), t.size(), t) == 0));
            }
            if (name == "IndexOf")
            {
                if (!need(1)) return false;
                size_t p = s.find(str(0), argc > 1 ? (size_t)std::max<int64_t>(0, args[1].ToInt()) : 0);
                return ret(Value::MakeInt(p == std::string::npos ? -1 : (int64_t)p));
            }
            if (name == "LastIndexOf")
            {
                if (!need(1)) return false;
                size_t p = s.rfind(str(0));
                return ret(Value::MakeInt(p == std::string::npos ? -1 : (int64_t)p));
            }
            if (name == "Substring")
            {
                if (!need(1)) return false;
                int64_t a = args[0].ToInt();
                int64_t len = argc > 1 ? args[1].ToInt() : (int64_t)s.size() - a;
                if (a < 0 || a > (int64_t)s.size() || len < 0 || a + len > (int64_t)s.size())
                {
                    RuntimeError("ArgumentOutOfRangeException: Substring(%lld, %lld) is out of range for a string of length %zu.", (long long)a, (long long)len, s.size());
                    return false;
                }
                return ret(Value::MakeString(s.substr((size_t)a, (size_t)len)));
            }
            if (name == "Slice")
            {
                int64_t n = (int64_t)s.size();
                int64_t a = argc > 0 ? args[0].ToInt() : 0;
                int64_t b = argc > 1 ? args[1].ToInt() : n;
                if (a < 0) a += n;
                if (b < 0) b += n;
                a = std::clamp<int64_t>(a, 0, n);
                b = std::clamp<int64_t>(b, a, n);
                return ret(Value::MakeString(s.substr((size_t)a, (size_t)(b - a))));
            }
            if (name == "Replace")
            {
                if (!need(2)) return false;
                std::string from = str(0), to = str(1), r;
                if (from.empty()) return ret(self);
                size_t pos = 0, f;
                while ((f = s.find(from, pos)) != std::string::npos) { r += s.substr(pos, f - pos) + to; pos = f + from.size(); }
                r += s.substr(pos);
                return ret(Value::MakeString(r));
            }
            if (name == "Split")
            {
                std::string sep = argc > 0 ? str(0) : " ";
                auto* out = new CQSListObject();
                base[0] = Value::MakeObject(out);
                if (sep.empty())
                    for (char c : s) out->Elements.push_back(Value::MakeString(std::string(1, c)));
                else
                {
                    size_t pos = 0, f;
                    while ((f = s.find(sep, pos)) != std::string::npos) { out->Elements.push_back(Value::MakeString(s.substr(pos, f - pos))); pos = f + sep.size(); }
                    out->Elements.push_back(Value::MakeString(s.substr(pos)));
                }
                return ret(base[0]);
            }
            if (name == "Repeat")
            {
                int64_t n = std::clamp<int64_t>(arg(0).ToInt(), 0, 10000000 / std::max<size_t>(1, s.size()));
                std::string r;
                r.reserve(s.size() * (size_t)n);
                for (int64_t i = 0; i < n; ++i) r += s;
                return ret(Value::MakeString(r));
            }
            if (name == "PadLeft" || name == "PadRight")
            {
                int64_t width = arg(0).ToInt();
                std::string pad = argc > 1 ? str(1) : " ";
                char pc = pad.empty() ? ' ' : pad[0];
                if ((int64_t)s.size() >= width) return ret(self);
                std::string fill((size_t)(width - (int64_t)s.size()), pc);
                return ret(Value::MakeString(name == "PadLeft" ? fill + s : s + fill));
            }
            if (name == "CharAt")
            {
                int64_t i = arg(0).ToInt();
                if (i < 0 || i >= (int64_t)s.size()) return ret(Value::MakeString(""));
                return ret(Value::MakeString(std::string(1, s[(size_t)i])));
            }
            if (name == "CharCodeAt")
            {
                int64_t i = arg(0).ToInt();
                if (i < 0 || i >= (int64_t)s.size()) return ret(Value::MakeInt(-1));
                return ret(Value::MakeInt((unsigned char)s[(size_t)i]));
            }
            if (name == "IsEmpty") return ret(Value::MakeBool(s.empty()));
            if (name == "ToInt" || name == "ToFloat")
            {
                Value v = self;
                if (!CastToType(&v, name == "ToInt" ? "int" : "float")) return false;
                return ret(v);
            }
            if (name == "Lines")
            {
                auto* out = new CQSListObject();
                base[0] = Value::MakeObject(out);
                size_t pos = 0, f;
                while ((f = s.find('\n', pos)) != std::string::npos)
                {
                    std::string line = s.substr(pos, f - pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    out->Elements.push_back(Value::MakeString(line));
                    pos = f + 1;
                }
                out->Elements.push_back(Value::MakeString(s.substr(pos)));
                return ret(base[0]);
            }
            if (name == "Format")
            {
                // "{0} ve {1}".Format(a, b)
                std::string r;
                for (size_t i = 0; i < s.size(); ++i)
                {
                    if (s[i] == '{' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))
                    {
                        size_t close = s.find('}', i);
                        if (close != std::string::npos)
                        {
                            std::string spec = s.substr(i + 1, close - i - 1);
                            std::string fmt;
                            size_t colon = spec.find(':');
                            if (colon != std::string::npos) { fmt = spec.substr(colon + 1); spec = spec.substr(0, colon); }
                            int idx = std::atoi(spec.c_str());
                            std::string part;
                            if (!FormatValue(arg(idx), fmt, part)) return false;
                            r += part;
                            i = close;
                            continue;
                        }
                    }
                    r += s[i];
                }
                return ret(Value::MakeString(r));
            }
        }

        // ── Range ──
        if (self.IsRange())
        {
            auto* r = self.AsRange();
            if (name == "Contains")
            {
                int64_t v = arg(0).ToInt();
                bool in = r->End >= r->Start ? (v >= r->Start && v < r->End) : (v <= r->Start && v > r->End);
                return ret(Value::MakeBool(in));
            }
            if (name == "ToList")
            {
                auto* out = new CQSListObject();
                for (int64_t i = 0; i < r->Count(); ++i) out->Elements.push_back(Value::MakeInt(r->At(i)));
                return ret(Value::MakeObject(out));
            }
            if (name == "Count") return ret(Value::MakeInt(r->Count()));
        }

        // ── Sayılar ──
        if (self.IsNumber())
        {
            double d = self.ToNumber();
            if (name == "Abs") return ret(self.IsInt() ? Value::MakeInt(self.AsInt() < 0 ? -self.AsInt() : self.AsInt()) : Value::MakeFloat(std::fabs(d)));
            if (name == "Floor") return ret(Value::MakeInt((int64_t)std::floor(d)));
            if (name == "Ceil") return ret(Value::MakeInt((int64_t)std::ceil(d)));
            if (name == "Round") return ret(argc > 0 ? Value::MakeFloat(std::round(d * std::pow(10.0, args[0].ToNumber())) / std::pow(10.0, args[0].ToNumber())) : Value::MakeInt((int64_t)std::llround(d)));
            if (name == "Clamp")
            {
                if (!need(2)) return false;
                double lo = args[0].ToNumber(), hi = args[1].ToNumber();
                if (self.IsInt() && args[0].IsInt() && args[1].IsInt())
                    return ret(Value::MakeInt(std::clamp(self.AsInt(), args[0].AsInt(), std::max(args[0].AsInt(), args[1].AsInt()))));
                return ret(Value::MakeFloat(std::clamp(d, lo, std::max(lo, hi))));
            }
        }

        handled = false;
        return false;
    }
}
