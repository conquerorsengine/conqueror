package conqueror;

import java.io.File;
import java.lang.reflect.*;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import javax.tools.*;

/** Yerel motor <-> JVM köprüsü (motor tarafından derlenir; düzenlemeyin). Tüm çağrılar ana iş parçacığındadır. */
public final class Host {
    private Host() {}

    // Motor tarafından RegisterNatives ile bağlanır
    static native String callNative(int index, String argsJson);
    static native void log(int level, String message);

    private static URLClassLoader loader;
    private static final Map<Integer, Slot> instances = new HashMap<>();
    private static int nextHandle = 1;
    private static String lastError = "";

    private static final class Slot {
        Object object;
        Class<?> type;
        long entity;
        final Map<String, Method> methods = new HashMap<>();
    }

    // ── Köprü ──
    static Value call(int index, Value... args) {
        String json = Json.write(Value.of(Arrays.asList(args)));
        String result = callNative(index, json);
        return result == null ? Value.NULL : Json.read(result);
    }

    private static void fail(Throwable t) {
        while ((t instanceof InvocationTargetException || t instanceof ExceptionInInitializerError) && t.getCause() != null) t = t.getCause();
        StringBuilder sb = new StringBuilder(t.toString());
        for (StackTraceElement e : t.getStackTrace()) {
            if (e.getClassName().startsWith("conqueror.Host") || e.getClassName().startsWith("jdk.internal")) break;
            sb.append("\n    at ").append(e);
        }
        lastError = sb.toString();
    }

    public static String getLastError() { return lastError; }

    // ── Derleme (JDK derleyici API'si, süreç içinde) ──
    public static String compile(String[] sources, String outDir, String classpath) {
        JavaCompiler javac = ToolProvider.getSystemJavaCompiler();
        if (javac == null) return "{\"ok\":false,\"diagnostics\":[{\"line\":0,\"col\":0,\"kind\":\"ERROR\",\"message\":\"No Java compiler (a JDK, not a JRE, is required)\",\"file\":\"\"}]}";
        DiagnosticCollector<JavaFileObject> diags = new DiagnosticCollector<>();
        try (StandardJavaFileManager fm = javac.getStandardFileManager(diags, Locale.ROOT, StandardCharsets.UTF_8)) {
            Files.createDirectories(Paths.get(outDir));
            List<String> options = new ArrayList<>(List.of("-d", outDir, "-cp", classpath, "-encoding", "UTF-8", "-proc:none", "-g", "-Xlint:none", "-nowarn"));
            Iterable<? extends JavaFileObject> units = fm.getJavaFileObjects(sources);
            boolean ok = javac.getTask(null, fm, diags, options, null, units).call();
            return diagnosticsJson(ok, diags);
        } catch (Exception e) {
            fail(e);
            return "{\"ok\":false,\"diagnostics\":[]}";
        }
    }

    /** Tek dosya sözdizimi denetimi (yalnızca ayrıştırma; derleme yok) */
    public static String checkSyntax(String source, String name) {
        JavaCompiler javac = ToolProvider.getSystemJavaCompiler();
        if (javac == null) return "{\"ok\":true,\"diagnostics\":[]}";
        DiagnosticCollector<JavaFileObject> diags = new DiagnosticCollector<>();
        JavaFileObject unit = new SimpleJavaFileObject(java.net.URI.create("string:///" + name.replace('\\', '/')), JavaFileObject.Kind.SOURCE) {
            @Override public CharSequence getCharContent(boolean ignore) { return source; }
        };
        try {
            com.sun.source.util.JavacTask task = (com.sun.source.util.JavacTask) javac.getTask(null, null, diags, List.of("-proc:none"), null, List.of(unit));
            task.parse();
        } catch (Exception e) {
            fail(e);
        }
        return diagnosticsJson(true, diags);
    }

    private static String diagnosticsJson(boolean ok, DiagnosticCollector<JavaFileObject> diags) {
        StringBuilder sb = new StringBuilder("{\"ok\":").append(ok).append(",\"diagnostics\":[");
        boolean first = true;
        for (Diagnostic<? extends JavaFileObject> d : diags.getDiagnostics()) {
            if (d.getKind() != Diagnostic.Kind.ERROR && d.getKind() != Diagnostic.Kind.WARNING && d.getKind() != Diagnostic.Kind.MANDATORY_WARNING) continue;
            if (!first) sb.append(',');
            first = false;
            sb.append("{\"line\":").append(d.getLineNumber()).append(",\"col\":").append(d.getColumnNumber())
              .append(",\"kind\":\"").append(d.getKind() == Diagnostic.Kind.ERROR ? "ERROR" : "WARNING").append("\",\"message\":");
            Json.quote(sb, d.getMessage(Locale.ROOT));
            sb.append(",\"file\":");
            Json.quote(sb, d.getSource() == null ? "" : new File(d.getSource().toUri().getPath()).getName());
            sb.append('}');
        }
        return sb.append("]}").toString();
    }

    // ── Sınıf yükleme ──
    public static boolean loadClasses(String dir) {
        try {
            URLClassLoader fresh = new URLClassLoader(new URL[] { new File(dir).toURI().toURL() }, Host.class.getClassLoader());
            URLClassLoader old = loader;
            loader = fresh;
            // Sıcak yeniden yükleme: örnekleri yeni sınıflara taşı (alanlar ada göre)
            for (Slot slot : instances.values()) {
                Class<?> type;
                try { type = Class.forName(slot.type.getName(), true, fresh); } catch (ClassNotFoundException e) { continue; }
                Object obj;
                try {
                    Constructor<?> c = type.getDeclaredConstructor();
                    c.setAccessible(true);
                    obj = c.newInstance();
                } catch (Exception e) { continue; }
                for (Field f : allFields(type)) {
                    Field oldField = findField(slot.type, f.getName());
                    if (oldField == null) continue;
                    try {
                        oldField.setAccessible(true);
                        f.setAccessible(true);
                        Object v = oldField.get(slot.object);
                        if (v == null || f.getType().isInstance(v) || (f.getType().isPrimitive() && f.getType() == oldField.getType())) f.set(obj, v);
                    } catch (Exception ignored) {}
                }
                if (obj instanceof ScriptBehaviour sb) sb.entity = slot.entity;
                slot.object = obj;
                slot.type = type;
                slot.methods.clear();
            }
            if (old != null) old.close();
            return true;
        } catch (Exception e) {
            fail(e);
            return false;
        }
    }

    public static void reset() {
        instances.clear();
        try { if (loader != null) loader.close(); } catch (Exception ignored) {}
        loader = null;
    }

    // ── Örnekler ──
    public static int createInstance(String className, long entity) {
        try {
            if (loader == null) { lastError = "Java scripts are not loaded"; return 0; }
            Class<?> type = Class.forName(className, true, loader);
            Constructor<?> c = type.getDeclaredConstructor();
            c.setAccessible(true);
            Object obj = c.newInstance();
            if (obj instanceof ScriptBehaviour sb) sb.entity = entity;
            Slot slot = new Slot();
            slot.object = obj;
            slot.type = type;
            slot.entity = entity;
            int h = nextHandle++;
            instances.put(h, slot);
            return h;
        } catch (ClassNotFoundException e) {
            lastError = "Java class '" + className + "' not found (public class in the default package, extending ScriptBehaviour)";
            return 0;
        } catch (Throwable t) {
            fail(t);
            return 0;
        }
    }

    public static void destroyInstance(int handle) { instances.remove(handle); }

    private static String lowerFirst(String s) { return s.isEmpty() ? s : Character.toLowerCase(s.charAt(0)) + s.substring(1); }

    private static Method findMethod(Slot slot, String name) {
        if (slot.methods.containsKey(name)) return slot.methods.get(name);
        Method found = null;
        for (String candidate : new String[] { name, lowerFirst(name) }) {
            for (Class<?> c = slot.type; c != null && found == null; c = c.getSuperclass())
                for (Method m : c.getDeclaredMethods())
                    if (m.getName().equals(candidate) && !Modifier.isStatic(m.getModifiers())) { found = m; break; }
            if (found != null) break;
        }
        if (found != null) found.setAccessible(true);
        slot.methods.put(name, found);
        return found;
    }

    public static boolean hasMethod(int handle, String name) {
        Slot slot = instances.get(handle);
        return slot != null && findMethod(slot, name) != null;
    }

    private static Object convert(Value v, Class<?> t) {
        if (t == Value.class || t == Object.class) return v;
        if (t == float.class || t == Float.class) return v.asFloat();
        if (t == double.class || t == Double.class) return v.asDouble();
        if (t == int.class || t == Integer.class) return v.asInt();
        if (t == long.class || t == Long.class) return v.asLong();
        if (t == boolean.class || t == Boolean.class) return v.asBool();
        if (t == String.class) return v.isNull() ? null : v.asString();
        if (t.isPrimitive()) return t == char.class ? (char) 0 : 0;
        return null;
    }

    public static boolean invoke(int handle, String name, String argsJson) {
        Slot slot = instances.get(handle);
        if (slot == null) return true;
        Method m = findMethod(slot, name);
        if (m == null) return true;
        try {
            List<Value> args = Json.read(argsJson).asList();
            Class<?>[] ps = m.getParameterTypes();
            Object[] values = new Object[ps.length];
            for (int i = 0; i < ps.length; ++i) values[i] = convert(i < args.size() ? args.get(i) : Value.NULL, ps[i]);
            m.invoke(slot.object, values);
            return true;
        } catch (Throwable t) {
            fail(t);
            return false;
        }
    }

    // ── Alanlar ──
    private static List<Field> allFields(Class<?> type) {
        List<Class<?>> chain = new ArrayList<>();
        for (Class<?> c = type; c != null && c != ScriptBehaviour.class && c != Object.class; c = c.getSuperclass()) chain.add(0, c);
        List<Field> fields = new ArrayList<>();
        for (Class<?> c : chain)
            for (Field f : c.getDeclaredFields())
                if (!Modifier.isStatic(f.getModifiers()) && !f.isSynthetic()) fields.add(f);
        return fields;
    }

    private static Field findField(Class<?> type, String name) {
        for (Class<?> c = type; c != null; c = c.getSuperclass()) {
            try { return c.getDeclaredField(name); } catch (NoSuchFieldException ignored) {}
        }
        return null;
    }

    private static String typeName(Class<?> t) {
        if (t == float.class || t == Float.class) return "float";
        if (t == double.class || t == Double.class) return "double";
        if (t == int.class || t == Integer.class) return "int";
        if (t == long.class || t == Long.class) return "long";
        if (t == boolean.class || t == Boolean.class) return "bool";
        if (t == String.class) return "string";
        return t.getSimpleName();
    }

    public static boolean setField(int handle, String name, String valueJson) {
        Slot slot = instances.get(handle);
        if (slot == null) return false;
        Field f = findField(slot.type, name);
        if (f == null) return false;
        try {
            f.setAccessible(true);
            f.set(slot.object, convert(Json.read(valueJson), f.getType()));
            return true;
        } catch (Throwable t) {
            fail(t);
            return false;
        }
    }

    public static String getField(int handle, String name) {
        Slot slot = instances.get(handle);
        if (slot == null) return null;
        Field f = findField(slot.type, name);
        if (f == null) return null;
        try {
            f.setAccessible(true);
            Value v = Value.from(f.get(slot.object));
            StringBuilder sb = new StringBuilder("{\"t\":\"").append(typeName(f.getType())).append("\",\"v\":");
            Json.write(sb, v);
            sb.append(",\"s\":");
            Json.quote(sb, v.toString());
            return sb.append('}').toString();
        } catch (Throwable t) {
            fail(t);
            return null;
        }
    }

    // ── İnceleme ──
    public static String describe(String dir) {
        StringBuilder sb = new StringBuilder("[");
        try (URLClassLoader probe = new URLClassLoader(new URL[] { new File(dir).toURI().toURL() }, Host.class.getClassLoader())) {
            List<String> names = new ArrayList<>();
            try (var walk = Files.walk(Paths.get(dir))) {
                walk.filter(p -> p.toString().endsWith(".class") && !p.getFileName().toString().contains("$"))
                    .forEach(p -> names.add(Paths.get(dir).relativize(p).toString().replace(File.separatorChar, '.').replaceAll("\\.class$", "")));
            }
            Collections.sort(names);
            boolean firstClass = true;
            for (String n : names) {
                Class<?> type;
                try { type = Class.forName(n, false, probe); } catch (Throwable t) { continue; }
                if (!ScriptBehaviour.class.isAssignableFrom(type) || Modifier.isAbstract(type.getModifiers())) continue;
                Object instance = null;
                try {
                    Constructor<?> c = type.getDeclaredConstructor();
                    c.setAccessible(true);
                    instance = c.newInstance();
                } catch (Throwable ignored) {}
                if (!firstClass) sb.append(',');
                firstClass = false;
                sb.append("{\"name\":");
                Json.quote(sb, type.getSimpleName());
                sb.append(",\"fields\":[");
                boolean firstField = true;
                for (Field f : allFields(type)) {
                    boolean visible = Modifier.isPublic(f.getModifiers()) || f.isAnnotationPresent(SerializeField.class);
                    if (!visible || f.isAnnotationPresent(HideInInspector.class) || Modifier.isFinal(f.getModifiers())) continue;
                    if (!firstField) sb.append(',');
                    firstField = false;
                    sb.append("{\"name\":");
                    Json.quote(sb, f.getName());
                    sb.append(",\"type\":\"").append(typeName(f.getType())).append("\",\"default\":");
                    Value def = Value.NULL;
                    if (instance != null) {
                        try { f.setAccessible(true); def = Value.from(f.get(instance)); } catch (Throwable ignored) {}
                    }
                    Json.write(sb, def);
                    sb.append(",\"attributes\":[");
                    List<String> attrs = new ArrayList<>();
                    Range r = f.getAnnotation(Range.class);
                    if (r != null) attrs.add("Range(" + r.min() + ", " + r.max() + ")");
                    Tooltip tip = f.getAnnotation(Tooltip.class);
                    if (tip != null) attrs.add("Tooltip(\"" + tip.value() + "\")");
                    if (f.isAnnotationPresent(SerializeField.class)) attrs.add("SerializeField");
                    for (int i = 0; i < attrs.size(); ++i) { if (i > 0) sb.append(','); Json.quote(sb, attrs.get(i)); }
                    sb.append("]}");
                }
                sb.append("]}");
            }
        } catch (Throwable t) {
            fail(t);
            return null;
        }
        return sb.append(']').toString();
    }
}
