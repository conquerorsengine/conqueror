#pragma once

#include "Core/Base/Base.h"

#include <functional>
#include <string>

// Uzun yüklemelerde ilerleme bildirimi (270): sahne çözümleme, model içe aktarma, doku yükleme ... buraya ne yaptığını söyler; bir
// dinleyici varsa (editörün proje açılış yükleme ekranı) onu çizer. Dinleyici yokken çağrı bedava sayılır (tek bir boş kontrol)
namespace Conqueror::LoadingProgress
{
    // step: aşama ("Loading scene"), detail: o an işlenen şey ("Importing Assets/Models/Sofa.glb"), fraction: 0..1 (< 0: değişmez)
    using Handler = std::function<void(const std::string& step, const std::string& detail, float fraction)>;

    CQ_API void SetHandler(Handler handler);   // boş: kapalı
    CQ_API bool Active();
    CQ_API void Report(const std::string& step, const std::string& detail, float fraction = -1.0f);
    // Bir aşamanın ilerlemesini genel çubuğun [begin, end] aralığına eşler (sahne yükü 0.15 .. 0.95 gibi)
    CQ_API void SetRange(float begin, float end);
    CQ_API void ReportPart(const std::string& step, const std::string& detail, float partFraction);
}
