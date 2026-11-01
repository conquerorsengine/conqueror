package conqueror;

public final class Debug {
    private Debug() {}
    public static void log(Object message) { NativeBridge.LogInfo(Value.of(String.valueOf(message))); }
    public static void logWarning(Object message) { NativeBridge.LogWarn(Value.of(String.valueOf(message))); }
    public static void logError(Object message) { NativeBridge.LogError(Value.of(String.valueOf(message))); }
}
