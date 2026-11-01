package conqueror;

import java.util.*;

/** Değerler için küçük JSON yazıcı/okuyucu (yerel köprü bu biçimi kullanır). */
final class Json {
    private Json() {}

    static String write(Value v) { StringBuilder sb = new StringBuilder(); write(sb, v); return sb.toString(); }

    static void write(StringBuilder sb, Value v) {
        switch (v.kind) {
            case NULL -> sb.append("null");
            case BOOL -> sb.append(v.asBool() ? "true" : "false");
            case NUMBER -> {
                double d = v.asDouble();
                if (Double.isNaN(d) || Double.isInfinite(d)) sb.append("0");
                else if (d == Math.rint(d) && Math.abs(d) < 1e15) sb.append((long) d);
                else sb.append(d);
            }
            case STRING -> quote(sb, v.asString());
            case LIST -> {
                sb.append('[');
                boolean first = true;
                for (Value e : v.asList()) { if (!first) sb.append(','); first = false; write(sb, e); }
                sb.append(']');
            }
            case MAP -> {
                sb.append('{');
                boolean first = true;
                for (Map.Entry<String, Value> e : v.asMap().entrySet()) {
                    if (!first) sb.append(',');
                    first = false;
                    quote(sb, e.getKey());
                    sb.append(':');
                    write(sb, e.getValue());
                }
                sb.append('}');
            }
        }
    }

    static void quote(StringBuilder sb, String s) {
        sb.append('"');
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                default -> { if (c < 0x20) sb.append(String.format("\\u%04x", (int) c)); else sb.append(c); }
            }
        }
        sb.append('"');
    }

    static Value read(String s) { return new Reader(s).value(); }

    private static final class Reader {
        final String s; int i;
        Reader(String s) { this.s = s; }
        void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) ++i; }
        Value value() {
            ws();
            if (i >= s.length()) return Value.NULL;
            char c = s.charAt(i);
            if (c == '{') {
                ++i;
                Map<String, Value> m = new LinkedHashMap<>();
                ws();
                if (i < s.length() && s.charAt(i) == '}') { ++i; return Value.of(m); }
                while (i < s.length()) {
                    ws();
                    String k = string();
                    ws(); ++i; // ':'
                    m.put(k, value());
                    ws();
                    if (i < s.length() && s.charAt(i) == ',') { ++i; continue; }
                    ++i; break;
                }
                return Value.of(m);
            }
            if (c == '[') {
                ++i;
                List<Value> l = new ArrayList<>();
                ws();
                if (i < s.length() && s.charAt(i) == ']') { ++i; return Value.of(l); }
                while (i < s.length()) {
                    l.add(value());
                    ws();
                    if (i < s.length() && s.charAt(i) == ',') { ++i; continue; }
                    ++i; break;
                }
                return Value.of(l);
            }
            if (c == '"') return Value.of(string());
            if (s.startsWith("true", i)) { i += 4; return Value.of(true); }
            if (s.startsWith("false", i)) { i += 5; return Value.of(false); }
            if (s.startsWith("null", i)) { i += 4; return Value.NULL; }
            int start = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) ++i;
            try { return Value.of(Double.parseDouble(s.substring(start, i))); } catch (NumberFormatException e) { return Value.NULL; }
        }
        String string() {
            StringBuilder sb = new StringBuilder();
            ++i; // '"'
            while (i < s.length()) {
                char c = s.charAt(i++);
                if (c == '"') break;
                if (c == '\\' && i < s.length()) {
                    char e = s.charAt(i++);
                    switch (e) {
                        case 'n' -> sb.append('\n');
                        case 'r' -> sb.append('\r');
                        case 't' -> sb.append('\t');
                        case 'u' -> { sb.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; }
                        default -> sb.append(e);
                    }
                } else sb.append(c);
            }
            return sb.toString();
        }
    }
}
