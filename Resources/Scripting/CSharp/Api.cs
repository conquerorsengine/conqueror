// Conqueror Engine C# betik API'si (motor tarafından derlenir; düzenlemeyin)
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Conqueror
{
    /// <summary>Sahnedeki bir varlığa takılan C# betiklerinin temel sınıfı (Unity MonoBehaviour karşılığı).</summary>
    public abstract class ScriptBehaviour
    {
        /// <summary>Betiğin bağlı olduğu varlık.</summary>
        public uint entity { get; internal set; }
        public string name => NativeBridge.GetEntityName();
    }

    /// <summary>Data Asset sınıflarının temel sınıfı (Unity ScriptableObject, Unreal UDataAsset): public alanları veri şablonudur,
    /// değerleri .cqdata dosyalarında durur (Content Browser > Create > Data Asset). Oyunda:
    /// <c>var sword = DataAsset.Load&lt;WeaponData&gt;("Assets/Sword.cqdata"); sword.damage</c></summary>
    public abstract class DataAsset
    {
        /// <summary>Yüklendiği .cqdata dosyası (proje göreli).</summary>
        public string assetPath { get; internal set; }

        /// <summary>.cqdata dosyasını T olarak yükle: alanlar dosyadaki değerlerle (dosyada yoksa sınıftaki varsayılanla) dolar.
        /// Dosya okunamazsa null.</summary>
        public static T Load<T>(string path) where T : DataAsset, new() => (T)Conqueror.Hosting.Host.LoadDataAsset(typeof(T), path);
    }

    [AttributeUsage(AttributeTargets.Field)] public sealed class SerializeFieldAttribute : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class HideInInspectorAttribute : Attribute { }
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class RangeAttribute : Attribute
    {
        public readonly float Min, Max;
        public RangeAttribute(float min, float max) { Min = min; Max = max; }
    }
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class TooltipAttribute : Attribute
    {
        public readonly string Text;
        public TooltipAttribute(string text) { Text = text; }
    }

    public static class Debug
    {
        public static void Log(object message) => NativeBridge.LogInfo(message?.ToString() ?? "null");
        public static void LogWarning(object message) => NativeBridge.LogWarn(message?.ToString() ?? "null");
        public static void LogError(object message) => NativeBridge.LogError(message?.ToString() ?? "null");
    }

    /// <summary>Motor ile değiş tokuş edilen değer (sayı, metin, vektör, liste, harita).</summary>
    public readonly struct Value
    {
        public enum Kind { Null, Bool, Int, Float, String, Vector, List, Map }

        public readonly Kind Type;
        readonly long m_Int;
        readonly double m_X, m_Y, m_Z, m_W;
        readonly int m_Dim;
        readonly object m_Ref;   // string / List<Value> / Dictionary<string, Value>

        Value(Kind t, long i = 0, double x = 0, double y = 0, double z = 0, double w = 0, int dim = 0, object r = null)
        { Type = t; m_Int = i; m_X = x; m_Y = y; m_Z = z; m_W = w; m_Dim = dim; m_Ref = r; }

        public static readonly Value Null = default;
        public static Value FromBool(bool b) => new Value(Kind.Bool, b ? 1 : 0);
        public static Value FromInt(long i) => new Value(Kind.Int, i);
        public static Value FromFloat(double d) => new Value(Kind.Float, x: d);
        public static Value FromString(string s) => s == null ? Null : new Value(Kind.String, r: s);
        public static Value FromVector(double x, double y, double z = 0, double w = 0, int dim = 3) => new Value(Kind.Vector, x: x, y: y, z: z, w: w, dim: dim);
        public static Value FromList(List<Value> l) => new Value(Kind.List, r: l);
        public static Value FromMap(Dictionary<string, Value> m) => new Value(Kind.Map, r: m);

        public bool IsNull => Type == Kind.Null;
        public double x => Type == Kind.Vector ? m_X : this["x"].AsDouble;
        public double y => Type == Kind.Vector ? m_Y : this["y"].AsDouble;
        public double z => Type == Kind.Vector ? m_Z : this["z"].AsDouble;
        public double w => Type == Kind.Vector ? m_W : this["w"].AsDouble;
        public int Dimension => m_Dim;

        public double AsDouble => Type switch { Kind.Int => m_Int, Kind.Float => m_X, Kind.Bool => m_Int, Kind.String => double.TryParse((string)m_Ref, NumberStyles.Float, CultureInfo.InvariantCulture, out var d) ? d : 0, _ => 0 };
        public long AsLong => Type switch { Kind.Int => m_Int, Kind.Float => (long)m_X, Kind.Bool => m_Int, _ => (long)AsDouble };
        public bool AsBool => Type switch { Kind.Bool => m_Int != 0, Kind.Int => m_Int != 0, Kind.Float => m_X != 0, Kind.String => !string.IsNullOrEmpty((string)m_Ref), Kind.Null => false, _ => true };
        public string AsString => Type == Kind.String ? (string)m_Ref : ToString();
        public List<Value> AsList => m_Ref as List<Value> ?? new List<Value>();
        public Dictionary<string, Value> AsMap => m_Ref as Dictionary<string, Value> ?? new Dictionary<string, Value>();

        public Value this[string key]
        {
            get
            {
                if (m_Ref is Dictionary<string, Value> m && m.TryGetValue(key, out var v)) return v;
                if (Type == Kind.Vector)
                    return key switch { "x" or "r" => FromFloat(m_X), "y" or "g" => FromFloat(m_Y), "z" or "b" => FromFloat(m_Z), "w" or "a" => FromFloat(m_W), _ => Null };
                return Null;
            }
        }
        public Value this[int index] => m_Ref is List<Value> l && index >= 0 && index < l.Count ? l[index] : Null;
        public int Count => m_Ref is List<Value> l ? l.Count : m_Ref is Dictionary<string, Value> m ? m.Count : 0;

        public static implicit operator Value(bool b) => FromBool(b);
        public static implicit operator Value(int i) => FromInt(i);
        public static implicit operator Value(long i) => FromInt(i);
        public static implicit operator Value(float f) => FromFloat(f);
        public static implicit operator Value(double d) => FromFloat(d);
        public static implicit operator Value(string s) => FromString(s);
        public static implicit operator double(Value v) => v.AsDouble;
        public static implicit operator float(Value v) => (float)v.AsDouble;
        public static implicit operator int(Value v) => (int)v.AsLong;
        public static implicit operator long(Value v) => v.AsLong;
        public static implicit operator bool(Value v) => v.AsBool;
        public static implicit operator string(Value v) => v.AsString;

        public override string ToString()
        {
            switch (Type)
            {
                case Kind.Null: return "null";
                case Kind.Bool: return m_Int != 0 ? "true" : "false";
                case Kind.Int: return m_Int.ToString(CultureInfo.InvariantCulture);
                case Kind.Float: return m_X.ToString("R", CultureInfo.InvariantCulture);
                case Kind.String: return (string)m_Ref;
                case Kind.Vector:
                    return m_Dim == 2 ? $"({m_X}, {m_Y})" : m_Dim == 4 ? $"({m_X}, {m_Y}, {m_Z}, {m_W})" : $"({m_X}, {m_Y}, {m_Z})";
                case Kind.List:
                {
                    var sb = new StringBuilder("[");
                    var l = (List<Value>)m_Ref;
                    for (int i = 0; i < l.Count; ++i) { if (i > 0) sb.Append(", "); sb.Append(l[i].ToString()); }
                    return sb.Append(']').ToString();
                }
                case Kind.Map:
                {
                    var sb = new StringBuilder("{");
                    bool first = true;
                    foreach (var kv in (Dictionary<string, Value>)m_Ref)
                    {
                        if (!first) sb.Append(", ");
                        first = false;
                        sb.Append(kv.Key).Append(": ").Append(kv.Value.ToString());
                    }
                    return sb.Append('}').ToString();
                }
            }
            return "?";
        }
    }
}
