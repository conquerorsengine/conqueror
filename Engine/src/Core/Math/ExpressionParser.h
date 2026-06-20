#pragma once
#include "Core/Base/Base.h"
#include <string>
#include <unordered_map>

namespace Conqueror::Math {
    class CQ_API ExpressionParser {
    public:
        static double Evaluate(const std::string& expression, const std::unordered_map<std::string, double>& variables = {});
    };
}
