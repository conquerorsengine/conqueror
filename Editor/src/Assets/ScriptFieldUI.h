#pragma once

#include "Scripting/ScriptTypes.h"

#include <map>
#include <string>
#include <vector>

// Betik alanları için ortak Inspector düzenleyicisi (betik bileşeni ve Data Asset aynı arayüzü kullanır).
// Nitelikler (Unity gibi): [Range(a, b)], [Tooltip("...")], [Header("...")], [Space], [Asset(".cqdata")]:
// metin alanı bir dosya yuvası olur (yol kutusu + Content Browser'dan sürükle-bırak + ◎ seçici).
namespace Conqueror::Editor::ScriptFieldUI
{
    bool ParseRange(const std::vector<std::string>& attrs, float& mn, float& mx);
    std::string Quoted(const std::vector<std::string>& attrs, const char* prefix);
    std::string Tooltip(const std::vector<std::string>& attrs);
    bool HasSpace(const std::vector<std::string>& attrs);
    // [Asset(".cqdata")] / [Asset(".png;.jpg")]: dosya süzgeci (yoksa boş)
    std::string AssetFilter(const std::vector<std::string>& attrs);
    std::string PrettyName(const std::string& name);

    // Tek alan değeri; değiştiyse true
    bool DrawValue(const char* id, Scripting::ScriptValue& value, const Scripting::ScriptFieldInfo& field);

    // Alan tablosu (başlık / boşluk / ipucu / değiştirilmiş işareti / sağ tık "Reset to Default"); overrides yalnız
    // varsayılandan farklı değerleri tutar. Değiştiyse true
    bool DrawFieldTable(const char* id, const std::vector<Scripting::ScriptFieldInfo>& fields,
                        std::map<std::string, Scripting::ScriptValue>& overrides);
}
