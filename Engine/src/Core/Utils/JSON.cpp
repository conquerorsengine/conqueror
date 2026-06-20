#include "JSON.h"
#include <cmath>
#include <cctype>
#include <functional>

namespace Conqueror::JSON {
    std::string Stringify(const Value& val) {
        if (val.Type == ValueType::Null) return "null";
        if (val.Type == ValueType::Bool) return val.BoolVal ? "true" : "false";
        if (val.Type == ValueType::Number) {
            if (val.NumberVal == std::floor(val.NumberVal)) return std::to_string((long long)val.NumberVal);
            std::string s = std::to_string(val.NumberVal);
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.') s.pop_back();
            return s;
        }
        if (val.Type == ValueType::String) {
            std::string res = "\"";
            for (char c : val.StringVal) {
                if (c == '"') res += "\\\"";
                else if (c == '\\') res += "\\\\";
                else if (c == '\n') res += "\\n";
                else if (c == '\r') res += "\\r";
                else if (c == '\t') res += "\\t";
                else res += c;
            }
            res += "\"";
            return res;
        }
        if (val.Type == ValueType::List) {
            std::string res = "[";
            for (size_t i = 0; i < val.ListVal.size(); ++i) {
                res += Stringify(val.ListVal[i]);
                if (i < val.ListVal.size() - 1) res += ", ";
            }
            res += "]";
            return res;
        }
        if (val.Type == ValueType::Map) {
            std::string res = "{";
            size_t count = 0;
            for (const auto& pair : val.MapVal) {
                res += "\"" + pair.first + "\": " + Stringify(pair.second);
                if (count < val.MapVal.size() - 1) res += ", ";
                count++;
            }
            res += "}";
            return res;
        }
        return "null";
    }

    Value Parse(const std::string& json) {
        size_t pos = 0;
        
        auto skipWhitespace = [&]() {
            while (pos < json.length() && std::isspace(json[pos])) pos++;
        };
        
        std::function<Value()> parseValue;
        
        auto parseString = [&]() -> std::string {
            pos++; // skip "
            std::string res = "";
            while (pos < json.length() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.length()) {
                    pos++;
                    if (json[pos] == 'n') res += '\n';
                    else if (json[pos] == 'r') res += '\r';
                    else if (json[pos] == 't') res += '\t';
                    else if (json[pos] == '"') res += '"';
                    else if (json[pos] == '\\') res += '\\';
                    else res += json[pos];
                } else {
                    res += json[pos];
                }
                pos++;
            }
            if (pos < json.length()) pos++; // skip "
            return res;
        };
        
        auto parseNumber = [&]() -> double {
            size_t start = pos;
            while (pos < json.length() && (std::isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-')) pos++;
            try { return std::stod(json.substr(start, pos - start)); } catch (...) { return 0.0; }
        };
        
        auto parseList = [&]() -> Value {
            pos++; // skip [
            std::vector<Value> list;
            skipWhitespace();
            while (pos < json.length() && json[pos] != ']') {
                list.push_back(parseValue());
                skipWhitespace();
                if (pos < json.length() && json[pos] == ',') pos++;
                skipWhitespace();
            }
            if (pos < json.length()) pos++; // skip ]
            return Value::MakeList(list);
        };
        
        auto parseMap = [&]() -> Value {
            pos++; // skip {
            std::unordered_map<std::string, Value> map;
            skipWhitespace();
            while (pos < json.length() && json[pos] != '}') {
                if (json[pos] == '"') {
                    std::string key = parseString();
                    skipWhitespace();
                    if (pos < json.length() && json[pos] == ':') pos++;
                    skipWhitespace();
                    map[key] = parseValue();
                } else pos++;
                skipWhitespace();
                if (pos < json.length() && json[pos] == ',') pos++;
                skipWhitespace();
            }
            if (pos < json.length()) pos++; // skip }
            return Value::MakeMap(map);
        };
        
        parseValue = [&]() -> Value {
            skipWhitespace();
            if (pos >= json.length()) return Value::MakeNull();
            char c = json[pos];
            if (c == '"') return Value::MakeString(parseString());
            if (c == '[') return parseList();
            if (c == '{') return parseMap();
            if (std::isdigit(c) || c == '-') return Value::MakeNumber(parseNumber());
            if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") { pos += 4; return Value::MakeBool(true); }
            if (pos + 5 <= json.length() && json.substr(pos, 5) == "false") { pos += 5; return Value::MakeBool(false); }
            if (pos + 4 <= json.length() && json.substr(pos, 4) == "null") { pos += 4; return Value::MakeNull(); }
            pos++;
            return Value::MakeNull();
        };
        
        return parseValue();
    }
}
