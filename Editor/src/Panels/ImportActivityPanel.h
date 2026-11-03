#pragma once

#include <string>

namespace Conqueror::Editor
{
    // Import Activity penceresi (Unity Window > Analysis > Import Activity): diskten yüklenen / yeniden yüklenen
    // varlıklar, içe aktarıcı, süre ve sonuç; arama, yalnız hatalar, en yavaşa göre sıralama.
    class ImportActivityPanel
    {
    public:
        void OnImGuiRender(bool* open);
        void SetFilter(const std::string& filter);

    private:
        char m_Filter[256] = {};
        bool m_OnlyFailed = false;
        bool m_SortBySlowest = false;
    };
}
