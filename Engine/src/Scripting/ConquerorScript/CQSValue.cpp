#include "CQSValue.h"
#include <sstream>

namespace Conqueror::CQS
{
    std::string Value::ToString() const
    {
        switch (Type)
        {
            case ValueType::Null:
                return "null";

            case ValueType::Bool:
                return BoolVal ? "true" : "false";

            case ValueType::Int:
                return std::to_string(IntVal);

            case ValueType::Float:
            {
                std::ostringstream oss;
                oss << FloatVal;
                std::string result = oss.str();
                // Ondalık nokta yoksa ekle
                if (result.find('.') == std::string::npos)
                    result += ".0";
                return result;
            }

            case ValueType::Object:
            {
                if (!ObjectVal) return "null";

                switch (ObjectVal->Type)
                {
                    case ObjectType::String:
                        return static_cast<CQSStringObject*>(ObjectVal)->Value;

                    case ObjectType::Function:
                    {
                        auto* fn = static_cast<CQSFunctionObject*>(ObjectVal);
                        if (fn->Name.empty()) return "<script>";
                        return "<func " + fn->Name + ">";
                    }

                    case ObjectType::NativeFunction:
                    {
                        auto* fn = static_cast<CQSNativeFunctionObject*>(ObjectVal);
                        return "<native " + fn->Name + ">";
                    }

                    case ObjectType::Instance:
                    {
                        auto* inst = static_cast<CQSInstanceObject*>(ObjectVal);
                        return "<" + inst->ClassName + " instance>";
                    }

                    case ObjectType::List:
                    {
                        auto* list = static_cast<CQSListObject*>(ObjectVal);
                        std::string result = "[";
                        for (size_t i = 0; i < list->Elements.size(); i++)
                        {
                            if (i > 0) result += ", ";
                            result += list->Elements[i].ToString();
                        }
                        result += "]";
                        return result;
                    }

                    case ObjectType::Map:
                    {
                        auto* map = static_cast<CQSMapObject*>(ObjectVal);
                        std::string result = "{";
                        bool first = true;
                        for (auto& [key, val] : map->Entries)
                        {
                            if (!first) result += ", ";
                            result += "\"" + key + "\": " + val.ToString();
                            first = false;
                        }
                        result += "}";
                        return result;
                    }

                    default:
                        return "<object>";
                }
            }

            default:
                return "<unknown>";
        }
    }
}
