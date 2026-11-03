// Conqueror Engine C# barındırıcısı: yerel motor <-> .NET köprüsü (motor tarafından derlenir; düzenlemeyin)
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace Conqueror.Hosting
{
    // C++ tarafındaki CQNativeValue ile birebir aynı (64 bayt)
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeValue
    {
        public int Type;      // 0 null, 1 bool, 2 int, 3 float, 4 string, 5 vector, 6 json
        public int Dim;
        public long I;
        public double X, Y, Z, W;
        public byte* Str;
        public int StrLen;
        public int Pad;
    }

    sealed class ScriptLoadContext : AssemblyLoadContext
    {
        readonly Assembly m_Host;
        public ScriptLoadContext(Assembly host) : base("ConquerorScripts", isCollectible: true) { m_Host = host; }
        protected override Assembly Load(AssemblyName name)
        {
            // Betik derlemesi motor API'sini (bu barındırıcıyı) paylaşmalı
            if (name.Name == m_Host.GetName().Name) return m_Host;
            return null;
        }
    }

    public static unsafe class Host
    {
        static delegate* unmanaged<int, int, NativeValue*, NativeValue*, int> s_CallNative;
        static delegate* unmanaged<int, byte*, void> s_Log;
        static delegate* unmanaged<byte*, int, void> s_SetResult;
        static string s_RoslynDir = "";

        static ScriptLoadContext s_Context;
        static Assembly s_UserAssembly;
        static readonly Dictionary<int, Slot> s_Instances = new();
        static int s_NextHandle = 1;
        static string s_LastError = "";

        sealed class Slot
        {
            public object Object;
            public Type Type;
            public uint Entity;
            public readonly Dictionary<string, MethodInfo> Methods = new();
        }

        // ── Yardımcılar ──
        static string Utf8(byte* p) => p == null ? "" : Marshal.PtrToStringUTF8((IntPtr)p) ?? "";

        static void SetResult(string s)
        {
            var bytes = Encoding.UTF8.GetBytes(s ?? "");
            fixed (byte* b = bytes) s_SetResult(b, bytes.Length);
        }

        static void Fail(Exception e)
        {
            while (e is TargetInvocationException tie && tie.InnerException != null) e = tie.InnerException;
            s_LastError = e.GetType().Name + ": " + e.Message + "\n" + e.StackTrace;
        }

        internal static void Log(int level, string message)
        {
            var bytes = Encoding.UTF8.GetBytes((message ?? "") + "\0");
            fixed (byte* b = bytes) s_Log(level, b);
        }

        // ── Value <-> NativeValue ──
        static void WriteJson(Utf8JsonWriter w, Value v)
        {
            switch (v.Type)
            {
                case Value.Kind.Null: w.WriteNullValue(); break;
                case Value.Kind.Bool: w.WriteBooleanValue(v.AsBool); break;
                case Value.Kind.Int: w.WriteNumberValue(v.AsLong); break;
                case Value.Kind.Float: w.WriteNumberValue(v.AsDouble); break;
                case Value.Kind.String: w.WriteStringValue(v.AsString); break;
                case Value.Kind.Vector:
                    w.WriteStartObject();
                    w.WriteNumber("x", v.x); w.WriteNumber("y", v.y);
                    if (v.Dimension >= 3) w.WriteNumber("z", v.z);
                    if (v.Dimension >= 4) w.WriteNumber("w", v.w);
                    w.WriteEndObject();
                    break;
                case Value.Kind.List:
                    w.WriteStartArray();
                    foreach (var e in v.AsList) WriteJson(w, e);
                    w.WriteEndArray();
                    break;
                case Value.Kind.Map:
                    w.WriteStartObject();
                    foreach (var kv in v.AsMap) { w.WritePropertyName(kv.Key); WriteJson(w, kv.Value); }
                    w.WriteEndObject();
                    break;
            }
        }

        static Value FromJson(JsonElement e)
        {
            switch (e.ValueKind)
            {
                case JsonValueKind.True: return Value.FromBool(true);
                case JsonValueKind.False: return Value.FromBool(false);
                case JsonValueKind.Number: return e.TryGetInt64(out var l) && !e.GetRawText().Contains('.') ? Value.FromInt(l) : Value.FromFloat(e.GetDouble());
                case JsonValueKind.String: return Value.FromString(e.GetString());
                case JsonValueKind.Array: return Value.FromList(e.EnumerateArray().Select(FromJson).ToList());
                case JsonValueKind.Object:
                {
                    var d = new Dictionary<string, Value>();
                    foreach (var p in e.EnumerateObject()) d[p.Name] = FromJson(p.Value);
                    return Value.FromMap(d);
                }
                default: return Value.Null;
            }
        }

        // Çağrı süresince yaşayan dizeler için ayrılan yerel bellekler
        [ThreadStatic] static List<IntPtr> t_Temp;

        static void ToNative(Value v, NativeValue* n)
        {
            *n = default;
            switch (v.Type)
            {
                case Value.Kind.Bool: n->Type = 1; n->I = v.AsBool ? 1 : 0; break;
                case Value.Kind.Int: n->Type = 2; n->I = v.AsLong; break;
                case Value.Kind.Float: n->Type = 3; n->X = v.AsDouble; break;
                case Value.Kind.String: SetString(n, 4, v.AsString); break;
                case Value.Kind.Vector: n->Type = 5; n->Dim = v.Dimension; n->X = v.x; n->Y = v.y; n->Z = v.z; n->W = v.w; break;
                case Value.Kind.List:
                case Value.Kind.Map:
                {
                    using var ms = new MemoryStream();
                    using (var w = new Utf8JsonWriter(ms)) WriteJson(w, v);
                    SetString(n, 6, Encoding.UTF8.GetString(ms.ToArray()));
                    break;
                }
            }
        }

        static void SetString(NativeValue* n, int type, string s)
        {
            var bytes = Encoding.UTF8.GetBytes(s ?? "");
            IntPtr mem = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, mem, bytes.Length);
            ((byte*)mem)[bytes.Length] = 0;
            (t_Temp ??= new List<IntPtr>()).Add(mem);
            n->Type = type;
            n->Str = (byte*)mem;
            n->StrLen = bytes.Length;
        }

        static void FreeTemp()
        {
            if (t_Temp == null) return;
            foreach (var p in t_Temp) Marshal.FreeHGlobal(p);
            t_Temp.Clear();
        }

        static Value FromNative(NativeValue* n)
        {
            switch (n->Type)
            {
                case 1: return Value.FromBool(n->I != 0);
                case 2: return Value.FromInt(n->I);
                case 3: return Value.FromFloat(n->X);
                case 4: return Value.FromString(n->Str == null ? "" : Encoding.UTF8.GetString(n->Str, n->StrLen));
                case 5: return Value.FromVector(n->X, n->Y, n->Z, n->W, n->Dim);
                case 6:
                {
                    var text = n->Str == null ? "null" : Encoding.UTF8.GetString(n->Str, n->StrLen);
                    using var doc = JsonDocument.Parse(text);
                    return FromJson(doc.RootElement);
                }
                default: return Value.Null;
            }
        }

        // NativeBridge.g.cs tarafından çağrılır
        internal static Value CallNative(int index, Value[] args)
        {
            int argc = args.Length;
            NativeValue* native = stackalloc NativeValue[Math.Max(argc, 1)];
            NativeValue result = default;
            try
            {
                for (int i = 0; i < argc; ++i) ToNative(args[i], native + i);
                if (s_CallNative(index, argc, native, &result) == 0)
                    throw new InvalidOperationException("native call failed: " + index);
                return FromNative(&result);
            }
            finally
            {
                FreeTemp();
            }
        }

        static object ConvertTo(Value v, Type t)
        {
            if (t == typeof(Value)) return v;
            if (t == typeof(float)) return (float)v.AsDouble;
            if (t == typeof(double)) return v.AsDouble;
            if (t == typeof(int)) return (int)v.AsLong;
            if (t == typeof(long)) return v.AsLong;
            if (t == typeof(uint)) return (uint)v.AsLong;
            if (t == typeof(bool)) return v.AsBool;
            if (t == typeof(string)) return v.IsNull ? null : v.AsString;
            if (t == typeof(object)) return v;
            return t.IsValueType ? Activator.CreateInstance(t) : null;
        }

        static Value ConvertFrom(object o)
        {
            switch (o)
            {
                case null: return Value.Null;
                case Value v: return v;
                case bool b: return Value.FromBool(b);
                case int i: return Value.FromInt(i);
                case long l: return Value.FromInt(l);
                case uint u: return Value.FromInt(u);
                case float f: return Value.FromFloat(f);
                case double d: return Value.FromFloat(d);
                case string s: return Value.FromString(s);
                case Enum e: return Value.FromInt(Convert.ToInt64(e));
                case System.Collections.IEnumerable seq:
                    return Value.FromList(seq.Cast<object>().Select(ConvertFrom).ToList());
            }
            return Value.FromString(o.ToString());
        }

        // ── Yansıma ──
        static IEnumerable<FieldInfo> InspectorFields(Type t)
        {
            var chain = new List<Type>();
            for (var c = t; c != null && c != typeof(ScriptBehaviour) && c != typeof(DataAsset) && c != typeof(object); c = c.BaseType) chain.Insert(0, c);
            foreach (var c in chain)
                foreach (var f in c.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                {
                    if (f.IsInitOnly || f.IsLiteral || f.GetCustomAttribute<HideInInspectorAttribute>() != null) continue;
                    if (f.GetCustomAttribute<CompilerGeneratedAttribute>() != null) continue;
                    if (!f.IsPublic && f.GetCustomAttribute<SerializeFieldAttribute>() == null) continue;
                    yield return f;
                }
        }

        static FieldInfo FindField(Type t, string name)
        {
            for (var c = t; c != null; c = c.BaseType)
            {
                var f = c.GetField(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly);
                if (f != null) return f;
            }
            return null;
        }

        static MethodInfo FindMethod(Slot slot, string name)
        {
            if (slot.Methods.TryGetValue(name, out var m)) return m;
            for (var c = slot.Type; c != null && m == null; c = c.BaseType)
                m = c.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                     .FirstOrDefault(x => x.Name == name && !x.IsGenericMethodDefinition);
            slot.Methods[name] = m;
            return m;
        }

        // DataAsset.Load<T>: .cqdata değerleri (motorun LoadDataAsset'i, sınıf varsayılanlarıyla birleşmiş) alanlara yazılır
        internal static object LoadDataAsset(Type type, string path)
        {
            var map = Conqueror.NativeBridge.LoadDataAsset(Value.FromString(path));
            if (map.Type != Value.Kind.Map) return null;
            var asset = (DataAsset)Activator.CreateInstance(type);
            asset.assetPath = path;
            var values = map.AsMap;
            foreach (var f in InspectorFields(type))
                if (values.TryGetValue(f.Name, out var v) && !v.IsNull)
                    f.SetValue(asset, ConvertTo(v, f.FieldType));
            return asset;
        }

        static string FieldTypeName(Type t) =>
            t == typeof(float) ? "float" : t == typeof(double) ? "double" : t == typeof(int) ? "int" : t == typeof(long) ? "long" :
            t == typeof(bool) ? "bool" : t == typeof(string) ? "string" : t.Name;

        // ═════════════ Yerel motordan çağrılan giriş noktaları ═════════════

        [UnmanagedCallersOnly]
        public static int Initialize(IntPtr callNative, IntPtr log, IntPtr setResult, byte* roslynDir)
        {
            s_CallNative = (delegate* unmanaged<int, int, NativeValue*, NativeValue*, int>)callNative;
            s_Log = (delegate* unmanaged<int, byte*, void>)log;
            s_SetResult = (delegate* unmanaged<byte*, int, void>)setResult;
            s_RoslynDir = Utf8(roslynDir);
            // Oyun kodu sistem diline bağlı olmamalı ("1,5" yerine "1.5"; Unity ile aynı)
            System.Globalization.CultureInfo.DefaultThreadCurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
            System.Globalization.CultureInfo.DefaultThreadCurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
            System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
            System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
            return 1;
        }

        [UnmanagedCallersOnly]
        public static int GetLastError()
        {
            SetResult(s_LastError);
            return 1;
        }

        [UnmanagedCallersOnly]
        public static int LoadUserAssembly(byte* path)
        {
            try
            {
                var bytes = File.ReadAllBytes(Utf8(path));
                var context = new ScriptLoadContext(typeof(Host).Assembly);
                var asm = context.LoadFromStream(new MemoryStream(bytes));
                var oldContext = s_Context;
                s_Context = context;
                s_UserAssembly = asm;
                // Sıcak yeniden yükleme: örnekleri yeni türlere taşı (alanlar ada göre)
                foreach (var slot in s_Instances.Values)
                {
                    var newType = asm.GetType(slot.Type.FullName);
                    if (newType == null) continue;
                    object fresh;
                    try { fresh = Activator.CreateInstance(newType); }
                    catch (Exception) { continue; }
                    foreach (var f in newType.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
                    {
                        var old = FindField(slot.Type, f.Name);
                        if (old == null) continue;
                        try
                        {
                            var v = old.GetValue(slot.Object);
                            if (v == null || f.FieldType.IsAssignableFrom(v.GetType())) f.SetValue(fresh, v);
                            else if (old.FieldType.FullName == f.FieldType.FullName && old.FieldType.IsPrimitive) f.SetValue(fresh, v);
                        }
                        catch (Exception) { }
                    }
                    if (fresh is ScriptBehaviour sb) sb.entity = slot.Entity;
                    slot.Object = fresh;
                    slot.Type = newType;
                    slot.Methods.Clear();
                }
                oldContext?.Unload();
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void Reset()
        {
            s_Instances.Clear();
            s_UserAssembly = null;
            s_Context?.Unload();
            s_Context = null;
            GC.Collect();
        }

        [UnmanagedCallersOnly]
        public static int CreateInstance(byte* className, uint entity)
        {
            try
            {
                var name = Utf8(className);
                var type = s_UserAssembly?.GetTypes().FirstOrDefault(t => t.Name == name || t.FullName == name);
                if (type == null)
                {
                    s_LastError = $"C# class '{name}' was not found (is it public and does it derive from ScriptBehaviour?)";
                    return 0;
                }
                object obj = RuntimeHelpers.GetUninitializedObject(type);
                if (obj is ScriptBehaviour sb) sb.entity = entity;
                // Alan başlatıcıları ve yapıcı çalışsın (entity önceden atanmış olur)
                type.GetConstructor(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, Type.EmptyTypes)?.Invoke(obj, null);
                int h = s_NextHandle++;
                s_Instances[h] = new Slot { Object = obj, Type = type, Entity = entity };
                return h;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyInstance(int handle) => s_Instances.Remove(handle);

        [UnmanagedCallersOnly]
        public static int HasMethod(int handle, byte* name)
        {
            return s_Instances.TryGetValue(handle, out var slot) && FindMethod(slot, Utf8(name)) != null ? 1 : 0;
        }

        [UnmanagedCallersOnly]
        public static int Invoke(int handle, byte* name, NativeValue* args, int argc)
        {
            if (!s_Instances.TryGetValue(handle, out var slot)) return 1;
            var m = FindMethod(slot, Utf8(name));
            if (m == null) return 1;
            try
            {
                var ps = m.GetParameters();
                var values = new object[ps.Length];
                for (int i = 0; i < ps.Length; ++i)
                    values[i] = i < argc ? ConvertTo(FromNative(args + i), ps[i].ParameterType)
                                         : (ps[i].HasDefaultValue ? ps[i].DefaultValue : ConvertTo(Value.Null, ps[i].ParameterType));
                m.Invoke(slot.Object, values);
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int SetField(int handle, byte* name, NativeValue* value)
        {
            if (!s_Instances.TryGetValue(handle, out var slot)) return 0;
            var f = FindField(slot.Type, Utf8(name));
            if (f == null) return 0;
            try
            {
                f.SetValue(slot.Object, ConvertTo(FromNative(value), f.FieldType));
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        // Değer SetResult üzerinden JSON olarak döner: {"t":"int","v":5}
        [UnmanagedCallersOnly]
        public static int GetField(int handle, byte* name)
        {
            if (!s_Instances.TryGetValue(handle, out var slot)) return 0;
            var f = FindField(slot.Type, Utf8(name));
            if (f == null) return 0;
            try
            {
                var v = ConvertFrom(f.GetValue(slot.Object));
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartObject();
                    w.WriteString("t", FieldTypeName(f.FieldType));
                    w.WritePropertyName("v");
                    WriteJson(w, v);
                    w.WriteString("s", v.ToString());
                    w.WriteEndObject();
                }
                SetResult(Encoding.UTF8.GetString(ms.ToArray()));
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        // Derlemedeki ScriptBehaviour sınıfları ve Inspector alanları (JSON)
        [UnmanagedCallersOnly]
        public static int Describe(byte* path)
        {
            ScriptLoadContext ctx = null;
            try
            {
                ctx = new ScriptLoadContext(typeof(Host).Assembly);
                var asm = ctx.LoadFromStream(new MemoryStream(File.ReadAllBytes(Utf8(path))));
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartArray();
                    foreach (var t in asm.GetTypes().Where(t => (typeof(ScriptBehaviour).IsAssignableFrom(t) || typeof(DataAsset).IsAssignableFrom(t)) && !t.IsAbstract))
                    {
                        w.WriteStartObject();
                        w.WriteString("name", t.Name);
                        w.WriteStartArray("fields");
                        object probe = null;
                        try { probe = Activator.CreateInstance(t); } catch (Exception) { }
                        foreach (var f in InspectorFields(t))
                        {
                            w.WriteStartObject();
                            w.WriteString("name", f.Name);
                            w.WriteString("type", FieldTypeName(f.FieldType));
                            w.WritePropertyName("default");
                            WriteJson(w, probe != null ? ConvertFrom(f.GetValue(probe)) : Value.Null);
                            w.WriteStartArray("attributes");
                            var range = f.GetCustomAttribute<RangeAttribute>();
                            if (range != null) w.WriteStringValue(FormattableString.Invariant($"Range({range.Min}, {range.Max})"));
                            var tip = f.GetCustomAttribute<TooltipAttribute>();
                            if (tip != null) w.WriteStringValue("Tooltip(\"" + tip.Text + "\")");
                            if (f.GetCustomAttribute<SerializeFieldAttribute>() != null) w.WriteStringValue("SerializeField");
                            w.WriteEndArray();
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }
                SetResult(Encoding.UTF8.GetString(ms.ToArray()));
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
            finally
            {
                ctx?.Unload();
            }
        }

        // Roslyn ile hızlı sözdizimi denetimi (SDK'daki Microsoft.CodeAnalysis.CSharp, yansıma ile)
        static Func<string, string, string> s_SyntaxChecker;

        [UnmanagedCallersOnly]
        public static int CheckSyntax(byte* source, byte* name)
        {
            try
            {
                s_SyntaxChecker ??= CreateSyntaxChecker();
                SetResult(s_SyntaxChecker == null ? "[]" : s_SyntaxChecker(Utf8(source), Utf8(name)));
                return 1;
            }
            catch (Exception e)
            {
                Fail(e);
                return 0;
            }
        }

        // Roslyn derlemeleri kendi bağlamında: SDK'daki bağımlılıklar (System.Collections.Immutable ...) çatıyla çakışmasın
        sealed class RoslynLoadContext : AssemblyLoadContext
        {
            readonly string m_Dir;
            public RoslynLoadContext(string dir) : base("Roslyn") { m_Dir = dir; }
            protected override Assembly Load(AssemblyName name)
            {
                var path = Path.Combine(m_Dir, name.Name + ".dll");
                return File.Exists(path) ? LoadFromAssemblyPath(path) : null;
            }
        }

        static Func<string, string, string> CreateSyntaxChecker()
        {
            if (string.IsNullOrEmpty(s_RoslynDir)) return null;
            var ctx = new RoslynLoadContext(s_RoslynDir);
            var csharp = ctx.LoadFromAssemblyPath(Path.Combine(s_RoslynDir, "Microsoft.CodeAnalysis.CSharp.dll"));
            var treeType = csharp.GetType("Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree");
            var parse = treeType.GetMethods(BindingFlags.Public | BindingFlags.Static)
                                .First(m => m.Name == "ParseText" && m.GetParameters().Length >= 1 && m.GetParameters()[0].ParameterType == typeof(string));
            return (src, file) =>
            {
                var args = parse.GetParameters().Select(p => p.HasDefaultValue ? p.DefaultValue : null).ToArray();
                args[0] = src;
                var tree = parse.Invoke(null, args);
                var diagMethod = tree.GetType().GetMethods().First(m => m.Name == "GetDiagnostics" && m.GetParameters().Length == 1 && m.GetParameters()[0].ParameterType.Name == "CancellationToken");
                var diags = (System.Collections.IEnumerable)diagMethod.Invoke(tree, new object[] { default(System.Threading.CancellationToken) });
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartArray();
                    foreach (var d in diags)
                    {
                        var severity = d.GetType().GetProperty("Severity").GetValue(d).ToString();
                        if (severity != "Error" && severity != "Warning") continue;
                        var location = d.GetType().GetProperty("Location").GetValue(d);
                        var span = location.GetType().GetMethod("GetLineSpan").Invoke(location, null);
                        var start = span.GetType().GetProperty("StartLinePosition").GetValue(span);
                        int line = (int)start.GetType().GetProperty("Line").GetValue(start) + 1;
                        int col = (int)start.GetType().GetProperty("Character").GetValue(start) + 1;
                        w.WriteStartObject();
                        w.WriteString("severity", severity);
                        w.WriteNumber("line", line);
                        w.WriteNumber("col", col);
                        w.WriteString("id", d.GetType().GetProperty("Id").GetValue(d).ToString());
                        w.WriteString("message", d.GetType().GetMethod("GetMessage", new[] { typeof(IFormatProvider) }).Invoke(d, new object[] { null }).ToString());
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                }
                return Encoding.UTF8.GetString(ms.ToArray());
            };
        }
    }
}
