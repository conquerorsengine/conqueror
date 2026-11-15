#pragma once

#include "Core/Base/Base.h"

#include <string>

// Oyun sırasında görünüm modu kısayolları (Unreal'ın oyunu editörde oynarken ve Development derlemesinde çalışan "view mode"
// kısayolları gibi): F1 tel kafes, F2 ışıksız, F3 normal (ışıklı), F4 yalnız ışık. Editörün Game görünümünde oynarken ve
// Development Build ile derlenmiş oyunda çalışır; son (Development Build kapalı) oyunda yoktur. Shift+F1 fareyi bırakmaya devam eder.
namespace Conqueror::GameViewModes
{
    enum class Mode : int { Lit = 0, Wireframe, Unlit, LightingOnly };

    CQ_API Mode Current();
    CQ_API void Set(Mode mode);
    CQ_API const char* Name(Mode mode);
    // Bu karede F1-F4'e basıldıysa modu değiştirir (true: değişti)
    CQ_API bool HandleHotkeys();
    // Oyun sahnesi çizilmeden önce modu renderer'a uygular, çizimden sonra varsayılana döndürür (Scene görünümü etkilenmez)
    CQ_API void BeginScene();
    CQ_API void EndScene();
    // Hata ayıklama görünümlerinde (ışıksız, yalnız ışık) ton eşleme dışındaki efektler yanıltıcıdır: kapalı
    CQ_API bool PostEffectsAllowed();
    // Ekranda kısa süre gösterilecek yazı ("View Mode: Wireframe (F1)"); süresi dolunca boş
    CQ_API std::string Banner();
}
