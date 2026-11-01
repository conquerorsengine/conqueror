#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror::Scripting
{
    // Diller arası basit değer (Inspector alanları, sahne kaydı)
    struct ScriptValue
    {
        enum class Kind : int { Null = 0, Bool, Int, Float, String };
        Kind        Type = Kind::Null;
        bool        Bool = false;
        int64_t     Int = 0;
        double      Float = 0.0;
        std::string String;

        static ScriptValue MakeBool(bool v) { ScriptValue s; s.Type = Kind::Bool; s.Bool = v; return s; }
        static ScriptValue MakeInt(int64_t v) { ScriptValue s; s.Type = Kind::Int; s.Int = v; return s; }
        static ScriptValue MakeFloat(double v) { ScriptValue s; s.Type = Kind::Float; s.Float = v; return s; }
        static ScriptValue MakeString(std::string v) { ScriptValue s; s.Type = Kind::String; s.String = std::move(v); return s; }
    };

    // Bir betik sınıfının Inspector'da gösterilen alanı
    struct ScriptFieldInfo
    {
        std::string              Name;
        std::string              TypeName;
        std::vector<std::string> Attributes;    // "Range(0, 10)", "Tooltip(\"...\")", "SerializeField"
        ScriptValue              Default;
        bool                     HasDefault = false;
    };
}
