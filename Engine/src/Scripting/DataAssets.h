#pragma once

#include "Core/Base/Base.h"
#include "Scripting/ScriptTypes.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// Data Asset (.cqdata): Unity ScriptableObject / Unreal Data Asset karşılığı. Bir betik sınıfı DataAsset'ten türer
// (CQS: "script WeaponData : DataAsset", C#: "class WeaponData : DataAsset", Java: "class WeaponData extends DataAsset",
// Python: "class WeaponData(DataAsset)", C++: "class WeaponData : public cq::DataAsset"); public alanları veri şablonudur.
// Content Browser > Data Asset ile o sınıftan dosyalar oluşturulur, Inspector'da değerleri düzenlenir. Oyunda:
// CQS LoadDataAsset("Assets/Sword.cqdata").damage, C# DataAsset.Load<WeaponData>(...), Java DataAsset.load(WeaponData.class,
// ...), Python WeaponData.load(...), C++ cq::LoadDataAsset<WeaponData>(...); her dil DataAssetGet(yol, alan) ile de okur.
// Betik alanı [Asset(".cqdata")] ile dosya yuvası olur.
namespace Conqueror::DataAssets
{
    inline constexpr const char* Extension = ".cqdata";
    inline constexpr const char* BaseClass = "DataAsset";

    struct DataAssetFile
    {
        std::string Script;   // proje göreli betik yolu
        std::string Class;
        std::map<std::string, Scripting::ScriptValue> Fields;   // varsayılandan farklı değerler
    };

    CQ_API bool Load(const std::string& path, DataAssetFile& out);
    CQ_API bool Save(const std::string& path, const DataAssetFile& data);
    // Bütün alanlar sınıftaki sırayla: varsayılanlar + dosyadaki değerler (betik ya da dosya değişince yeniden)
    CQ_API bool Resolve(const std::string& path, std::vector<std::pair<std::string, Scripting::ScriptValue>>& out, std::string* error = nullptr);
    // Sınıfın alan tanımları (Inspector)
    CQ_API bool ClassFields(const std::string& scriptPath, const std::string& className, std::vector<Scripting::ScriptFieldInfo>& out,
                            std::string* error = nullptr);
    // Betik dosyasındaki sınıf DataAsset'ten mi türüyor (her dilin sınıf bildiriminden)
    CQ_API bool IsDataClass(const std::string& scriptPath, const std::string& className);
    // Metindeki DataAsset sınıfları (dil kimliği: "cpp" C/C++ başlık / kaynak sözdizimi; boş: bütün diller)
    CQ_API std::vector<std::string> DataClassesInText(const std::string& text, const std::string& languageId = std::string());
    // C++ başlık dosyası mı (.h .hpp .hh .hxx): DataAsset sınıfları başlıkta tanımlanabilir (Unreal gibi)
    CQ_API bool IsCppHeader(const std::string& path);
    // Data Asset sınıfı tanımlayabilen dosya mı (betik ya da C++ başlığı)
    CQ_API bool IsDataScriptFile(const std::string& path);
    // Betikteki DataAsset sınıfları (önbellekli)
    CQ_API std::vector<std::string> DataClassesOf(const std::string& scriptPath);
    // Betik yalnız DataAsset sınıfı mı tanımlıyor (nesneye betik olarak eklenemez; Unity'de ScriptableObject betiği gibi).
    // Metinden: C#/Java/C++'ta dosya adıyla aynı sınıf (çalışma zamanı bu sınıfı seçer), CQS/Python'da bütün sınıflar.
    CQ_API bool IsDataOnlyScript(const std::string& scriptPath);
    // Projedeki veri sınıfları: (proje göreli betik yolu, sınıf adı)
    CQ_API std::vector<std::pair<std::string, std::string>> FindDataClasses(const std::string& projectDirectory);
    // DataAsset sınıfı tanımlanabilen diller (taban sınıf ve tipli yükleme): ConquerorScript, C#, Java, Python, C++.
    // Diğer diller .cqdata dosyalarını LoadDataAsset / DataAssetGet ile okur.
    CQ_API bool SupportsDataClasses(const std::string& languageId);
    // "Data Asset Script" (Unity ScriptableObject Script) şablonu; languageId: cqs, cs, java, py, cpp
    CQ_API std::string NewScriptTemplate(const std::string& className, const std::string& languageId = "cqs");
}
