#include "ExpressionParser.h"
#include <cmath>
#include <cctype>
#include <functional>

namespace Conqueror::Math {
    double ExpressionParser::Evaluate(const std::string& expression, const std::unordered_map<std::string, double>& variables) {
        int pos = 0;
        auto peek = [&]() -> char { 
            while(pos < expression.length() && std::isspace(expression[pos])) pos++; 
            return pos < expression.length() ? expression[pos] : 0; 
        };
        auto get = [&]() -> char { char c = peek(); pos++; return c; };
        
        std::function<double()> parseExpr;
        std::function<double()> parseTerm;
        std::function<double()> parseFactor;

        parseFactor = [&]() -> double {
            char c = peek();
            if (c == '(') {
                get(); double v = parseExpr(); get(); return v;
            } else if (c == '-') {
                get(); return -parseFactor();
            } else if (std::isalpha(c)) {
                std::string ident = "";
                while (std::isalpha(peek())) ident += get();
                if (peek() == '(') {
                    get(); double v = parseExpr(); get();
                    if (ident == "sin") return std::sin(v);
                    if (ident == "cos") return std::cos(v);
                    if (ident == "tan") return std::tan(v);
                    if (ident == "sqrt") return std::sqrt(v);
                    if (ident == "abs") return std::abs(v);
                } else {
                    if (variables.find(ident) != variables.end()) {
                        return variables.at(ident);
                    }
                }
                return 0.0;
            } else if (std::isdigit(c) || c == '.') {
                std::string num = "";
                while (std::isdigit(peek()) || peek() == '.') num += get();
                return std::stod(num);
            }
            return 0.0;
        };

        parseTerm = [&]() -> double {
            double v = parseFactor();
            while (true) {
                char c = peek();
                if (c == '*') { get(); v *= parseFactor(); }
                else if (c == '/') { get(); v /= parseFactor(); }
                else if (c == '^') { get(); v = std::pow(v, parseFactor()); }
                else break;
            }
            return v;
        };

        parseExpr = [&]() -> double {
            double v = parseTerm();
            while (true) {
                char c = peek();
                if (c == '+') { get(); v += parseTerm(); }
                else if (c == '-') { get(); v -= parseTerm(); }
                else break;
            }
            return v;
        };

        try {
            return parseExpr();
        } catch (...) { return 0.0; }
    }
}
