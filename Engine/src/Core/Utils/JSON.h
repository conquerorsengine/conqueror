#pragma once
#include "Core/Base/Base.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Conqueror::JSON {
    enum class ValueType { Null, Bool, Number, String, List, Map };

    struct Value {
        ValueType Type = ValueType::Null;
        bool BoolVal = false;
        double NumberVal = 0.0;
        std::string StringVal = "";
        std::vector<Value> ListVal;
        std::unordered_map<std::string, Value> MapVal;

        static Value MakeNull() { Value v; v.Type = ValueType::Null; return v; }
        static Value MakeBool(bool b) { Value v; v.Type = ValueType::Bool; v.BoolVal = b; return v; }
        static Value MakeNumber(double n) { Value v; v.Type = ValueType::Number; v.NumberVal = n; return v; }
        static Value MakeString(const std::string& s) { Value v; v.Type = ValueType::String; v.StringVal = s; return v; }
        static Value MakeList(const std::vector<Value>& l) { Value v; v.Type = ValueType::List; v.ListVal = l; return v; }
        static Value MakeMap(const std::unordered_map<std::string, Value>& m) { Value v; v.Type = ValueType::Map; v.MapVal = m; return v; }
    };

    CQ_API std::string Stringify(const Value& val);
    CQ_API Value Parse(const std::string& json);
}
