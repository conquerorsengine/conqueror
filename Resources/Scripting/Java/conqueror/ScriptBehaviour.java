package conqueror;

/** Sahnedeki bir varlığa takılan Java betiklerinin temel sınıfı (Unity MonoBehaviour karşılığı). */
public abstract class ScriptBehaviour {
    /** Betiğin bağlı olduğu varlık. */
    public long entity;

    public String name() { return NativeBridge.GetEntityName().asString(); }
}
