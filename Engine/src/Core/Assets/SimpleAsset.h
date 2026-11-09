#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Alanları tabloyla tanımlanan veri varlıkları: Create menüsü girişi, Content Browser simgesi, Inspector ve dosya biçimi
// (YAML) bu tablodan gelir; her türün çalışma zamanı kendi kodunda Load ile okur.
namespace Conqueror::SimpleAssets
{
    enum class FieldType { Float, Int, Bool, Text, Multiline, Color, Vec3, Choice, Asset, Table };

    struct Column
    {
        std::string Name;
        FieldType Type = FieldType::Text;
        std::string Default;
        std::vector<std::string> Options;   // Choice: seçenekler; Asset: uzantılar
    };

    struct Field
    {
        std::string Name;
        FieldType Type = FieldType::Float;
        std::string Default;
        std::string Tooltip;
        float Min = 0.0f, Max = 0.0f;       // Min < Max ise kaydırıcı
        std::vector<std::string> Options;   // Choice: seçenekler; Asset: uzantılar
        std::vector<Column> Columns;        // Table
    };

    struct Def
    {
        std::string Kind;                   // dosyadaki "Asset" değeri ve Create türü
        std::string Extension;
        std::string Label;
        std::string Category;
        std::string Submenu;
        const char* Icon = "";
        uint32_t Color = 0xffffffff;        // IM_COL32 düzeni
        std::string Tooltip;
        std::vector<Field> Fields;
    };

    struct Data
    {
        std::map<std::string, std::string> Values;
        std::map<std::string, std::vector<std::vector<std::string>>> Tables;

        CQ_API std::string Str(const std::string& name, const std::string& def = "") const;
        CQ_API float Float(const std::string& name, float def = 0.0f) const;
        CQ_API int Int(const std::string& name, int def = 0) const;
        CQ_API bool Bool(const std::string& name, bool def = false) const;
        CQ_API glm::vec4 Color(const std::string& name, const glm::vec4& def = glm::vec4(1.0f)) const;
        CQ_API glm::vec3 Vec3(const std::string& name, const glm::vec3& def = glm::vec3(0.0f)) const;
        CQ_API const std::vector<std::vector<std::string>>& Table(const std::string& name) const;
    };

    CQ_API const std::vector<Def>& All();
    CQ_API const Def* ForExtension(const std::string& extension);
    CQ_API const Def* ForKind(const std::string& kind);
    CQ_API bool IsKind(const std::string& path, const char* kind);
    CQ_API Data Defaults(const Def& def);
    // Yol proje göreli olabilir; dosya değişmedikçe önbellekten döner. Eksik alanlar varsayılanla doldurulur.
    CQ_API bool Load(const std::string& path, Data& out);
    CQ_API bool Save(const std::string& path, const Def& def, const Data& data);
    // Projedeki bu türden dosyalar (mutlak yol)
    CQ_API std::vector<std::string> FindAll(const char* kind);
    // Tablo hücresi yardımcıları
    CQ_API std::string Cell(const std::vector<std::string>& row, size_t index, const std::string& def = "");
    CQ_API float CellFloat(const std::vector<std::string>& row, size_t index, float def = 0.0f);
    CQ_API bool CellBool(const std::vector<std::string>& row, size_t index, bool def = false);
}
