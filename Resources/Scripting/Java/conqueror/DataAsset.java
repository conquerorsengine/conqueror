package conqueror;

/**
 * Data Asset sınıflarının temel sınıfı (Unity ScriptableObject, Unreal UDataAsset): public alanları veri şablonudur,
 * değerleri .cqdata dosyalarında durur (Content Browser > Create > Data Asset). Oyunda:
 * {@code WeaponData sword = DataAsset.load(WeaponData.class, "Assets/Sword.cqdata"); sword.damage}
 */
public abstract class DataAsset {
    private String assetPath;

    /** Yüklendiği .cqdata dosyası (proje göreli). */
    public String assetPath() { return assetPath; }

    void setAssetPath(String path) { assetPath = path; }

    /** .cqdata dosyasını type olarak yükle: alanlar dosyadaki değerlerle (dosyada yoksa sınıftaki varsayılanla) dolar. Okunamazsa null. */
    public static <T extends DataAsset> T load(Class<T> type, String path) { return Host.loadDataAsset(type, path); }
}
