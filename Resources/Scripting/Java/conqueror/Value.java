package conqueror;

import java.util.*;

/** Motor ile değiş tokuş edilen değer: sayı, metin, liste, harita (vektörler x/y/z alanlı haritadır). */
public final class Value {
    public enum Kind { NULL, BOOL, NUMBER, STRING, LIST, MAP }

    public static final Value NULL = new Value(Kind.NULL, null, 0);
    public final Kind kind;
    private final Object ref;
    private final double num;

    private Value(Kind k, Object r, double n) { kind = k; ref = r; num = n; }

    public static Value of(boolean b) { return new Value(Kind.BOOL, null, b ? 1 : 0); }
    public static Value of(double d) { return new Value(Kind.NUMBER, null, d); }
    public static Value of(long l) { return new Value(Kind.NUMBER, null, l); }
    public static Value of(String s) { return s == null ? NULL : new Value(Kind.STRING, s, 0); }
    public static Value of(List<Value> l) { return new Value(Kind.LIST, l, 0); }
    public static Value of(Map<String, Value> m) { return new Value(Kind.MAP, m, 0); }
    public static Value vector(double x, double y, double z) {
        Map<String, Value> m = new LinkedHashMap<>();
        m.put("x", of(x)); m.put("y", of(y)); m.put("z", of(z));
        return of(m);
    }
    /** Herhangi bir Java nesnesinden (sayı, metin, liste, dizi, harita). */
    public static Value from(Object o) {
        if (o == null) return NULL;
        if (o instanceof Value v) return v;
        if (o instanceof Boolean b) return of(b);
        if (o instanceof Number n) return of(n.doubleValue());
        if (o instanceof CharSequence s) return of(s.toString());
        if (o instanceof Collection<?> c) { List<Value> l = new ArrayList<>(); for (Object e : c) l.add(from(e)); return of(l); }
        if (o.getClass().isArray()) {
            List<Value> l = new ArrayList<>();
            int n = java.lang.reflect.Array.getLength(o);
            for (int i = 0; i < n; ++i) l.add(from(java.lang.reflect.Array.get(o, i)));
            return of(l);
        }
        if (o instanceof Map<?, ?> m) {
            Map<String, Value> r = new LinkedHashMap<>();
            for (Map.Entry<?, ?> e : m.entrySet()) r.put(String.valueOf(e.getKey()), from(e.getValue()));
            return of(r);
        }
        if (o instanceof Enum<?> e) return of(e.ordinal());
        return of(o.toString());
    }

    public boolean isNull() { return kind == Kind.NULL; }
    public double asDouble() { return switch (kind) { case NUMBER, BOOL -> num; case STRING -> parse((String) ref); default -> 0; }; }
    public float asFloat() { return (float) asDouble(); }
    public int asInt() { return (int) asDouble(); }
    public long asLong() { return (long) asDouble(); }
    public boolean asBool() { return switch (kind) { case BOOL, NUMBER -> num != 0; case STRING -> !((String) ref).isEmpty(); case NULL -> false; default -> true; }; }
    public String asString() { return kind == Kind.STRING ? (String) ref : toString(); }
    @SuppressWarnings("unchecked") public List<Value> asList() { return kind == Kind.LIST ? (List<Value>) ref : new ArrayList<>(); }
    @SuppressWarnings("unchecked") public Map<String, Value> asMap() { return kind == Kind.MAP ? (Map<String, Value>) ref : new LinkedHashMap<>(); }
    public Value get(String key) { Value v = asMap().get(key); return v == null ? NULL : v; }
    public Value get(int i) { List<Value> l = asList(); return i >= 0 && i < l.size() ? l.get(i) : NULL; }
    public double x() { return get("x").asDouble(); }
    public double y() { return get("y").asDouble(); }
    public double z() { return get("z").asDouble(); }
    public double w() { return get("w").asDouble(); }

    private static double parse(String s) { try { return Double.parseDouble(s); } catch (NumberFormatException e) { return 0; } }

    @Override public String toString() {
        switch (kind) {
            case NULL: return "null";
            case BOOL: return num != 0 ? "true" : "false";
            case NUMBER: return num == Math.rint(num) && Math.abs(num) < 1e15 ? Long.toString((long) num) : Double.toString(num);
            case STRING: return (String) ref;
            default: return Json.write(this);
        }
    }
}
