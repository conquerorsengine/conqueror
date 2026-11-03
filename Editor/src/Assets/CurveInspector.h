#pragma once

#include <string>

namespace Conqueror
{
    struct CurveAsset;
}

namespace Conqueror::Editor::CurveInspector
{
    // .cqcurve Inspector'ı (Unreal Curve düzenleyicisi / Unity eğri penceresi): tür, ön / son sonsuzluk, hazır eğriler,
    // eğri düzenleyici (anahtar ekle / sürükle / sil, Auto / Linear / Constant / Free teğetler, teğet tutamaçları,
    // yakınlaştırma, Frame All), seçili anahtarın alanları; Color eğrisinde renk geçişi önizlemesi.
    void Draw(const std::string& path);

    // Eğri düzenleyici (başka pencereler de kullanabilir); değişince true
    bool CurveEditorWidget(const char* id, CurveAsset& curve, float height = 230.0f);
}
