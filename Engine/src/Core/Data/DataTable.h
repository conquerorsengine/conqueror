#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Unreal Data Table karşılığı: oyun verisi tabloları (eşyalar, düşmanlar, seviyeler, efektler ...).
// Biçimler: .csv (ilk satır başlık, ilk sütun satır anahtarı), .json / .yaml (nesne dizisi ya da anahtar -> nesne).
// Tablolar yol başına önbelleklenir ve dosya değişince kendiliğinden yeniden okunur.
namespace Conqueror
{
    struct DataTableCell
    {
        enum class Kind : int { Text = 0, Number, Bool, Empty };
        Kind Type = Kind::Empty;
        std::string Text;
        double Number = 0.0;
        bool Bool = false;
    };

    struct DataTable
    {
        std::vector<std::string> Columns;                  // Columns[0] = anahtar sütunu
        std::vector<std::vector<DataTableCell>> Rows;      // her satır Columns.size() hücre
        std::unordered_map<std::string, size_t> Index;     // anahtar (küçük harf) -> satır
        std::string Error;

        int ColumnIndex(const std::string& name) const;    // büyük/küçük harf duyarsız; yoksa -1
        const std::vector<DataTableCell>* FindRow(const std::string& key) const;
    };

    namespace DataTables
    {
        // Proje göreli ya da mutlak yol; hata durumunda Error dolu boş tablo döner (asla null değil)
        CQ_API const DataTable& Load(const std::string& path);
        CQ_API bool ParseCSV(const std::string& text, DataTable& out);
        CQ_API bool ParseStructured(const std::string& text, DataTable& out);   // JSON / YAML
        CQ_API DataTableCell MakeCell(const std::string& raw);
        CQ_API void ClearCache();
    }
}
