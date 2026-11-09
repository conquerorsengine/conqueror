#include "SimpleAsset.h"

#include <IconsFontAwesome6.h>

// Tabloyla tanımlanan varlık türleri (bkz. SimpleAsset.h). Çalışma zamanı kullanımları ilgili sistemlerde.
namespace Conqueror::SimpleAssets
{
    namespace
    {
        constexpr uint32_t Rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000u | (b << 16) | (g << 8) | r; }

        Field F(const char* name, float def, const char* tip = "", float mn = 0.0f, float mx = 0.0f)
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Float;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", def);
            f.Default = buf;
            f.Tooltip = tip;
            f.Min = mn;
            f.Max = mx;
            return f;
        }

        Field I(const char* name, int def, const char* tip = "", int mn = 0, int mx = 0)
        {
            Field f = F(name, (float)def, tip, (float)mn, (float)mx);
            f.Type = FieldType::Int;
            return f;
        }

        Field B(const char* name, bool def, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Bool;
            f.Default = def ? "true" : "false";
            f.Tooltip = tip;
            return f;
        }

        Field T(const char* name, const char* def = "", const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Text;
            f.Default = def;
            f.Tooltip = tip;
            return f;
        }

        Field Long(const char* name, const char* def = "", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Multiline;
            return f;
        }

        Field C(const char* name, const char* def = "1 1 1 1", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Color;
            return f;
        }

        Field V(const char* name, const char* def = "0 0 0", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Vec3;
            return f;
        }

        Field E(const char* name, std::vector<std::string> options, const char* tip = "", int selected = 0)
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Choice;
            f.Default = options[(size_t)selected];
            f.Options = std::move(options);
            f.Tooltip = tip;
            return f;
        }

        Field A(const char* name, std::vector<std::string> exts, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Asset;
            f.Options = std::move(exts);
            f.Tooltip = tip;
            return f;
        }

        Field Tab(const char* name, std::vector<Column> columns, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Table;
            f.Columns = std::move(columns);
            f.Tooltip = tip;
            return f;
        }

        Column Col(const char* name, const char* def = "") { return { name, FieldType::Text, def, {} }; }
        Column ColF(const char* name, const char* def = "0") { return { name, FieldType::Float, def, {} }; }
        Column ColB(const char* name, const char* def = "false") { return { name, FieldType::Bool, def, {} }; }
        Column ColE(const char* name, std::vector<std::string> options) { std::string d = options.front(); return { name, FieldType::Choice, d, std::move(options) }; }
        Column ColA(const char* name, std::vector<std::string> exts) { return { name, FieldType::Asset, "", std::move(exts) }; }

        const std::vector<std::string> kPlatforms = { "Windows", "Linux", "macOS", "Android", "iOS", "Web" };
    }

    std::vector<Def> BuildDefs()
    {
        std::vector<Def> d;
        auto add = [&](const char* kind, const char* ext, const char* label, const char* category, const char* submenu, const char* icon, uint32_t color,
                       const char* tooltip, std::vector<Field> fields) {
            Def def;
            def.Kind = kind;
            def.Extension = ext;
            def.Label = label;
            def.Category = category;
            def.Submenu = submenu;
            def.Icon = icon;
            def.Color = color;
            def.Tooltip = tooltip;
            def.Fields = std::move(fields);
            d.push_back(std::move(def));
        };

        add("NativeScriptModule", ".cqmodule", "Native Script Module", "Scripting", "", ICON_FA_CUBES, Rgb(110, 175, 255),
            "Groups the C and C++ scripts of its folder (Unity Assembly Definition): platforms, defines and references apply to those scripts only.",
            { B("Enabled", true, "A disabled module's scripts are left out of the build."),
              Tab("Defines", { Col("Name"), Col("Value", "1") }, "Preprocessor definitions for this module's scripts only."),
              Tab("References", { Col("Module") }, "Other modules this one needs. If one is disabled or missing, this module is left out too."),
              Tab("Include Directories", { Col("Folder") }, "Extra include folders, relative to the project."),
              Tab("Exclude Platforms", { ColE("Platform", kPlatforms) }, "The module's scripts are not compiled on these platforms."),
              Tab("Define Constraints", { Col("Symbol") }, "Compiled only when every symbol here is defined in Project Settings > Script Execution Order > Scripting Define Symbols.") });

        return d;
    }
}
