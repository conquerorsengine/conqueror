#pragma once

// GL çeviri katmanlarının ortak GLSL dönüşümleri (VkGL, DxGL)

#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace Conqueror::GLEmu
{
    inline size_t MatchingBracket(const std::string& s, size_t open)
    {
        int depth = 0;
        for (size_t i = open; i < s.size(); ++i)
        {
            if (s[i] == '[')
                ++depth;
            else if (s[i] == ']' && --depth == 0)
                return i;
        }
        return std::string::npos;
    }

    // Büyük birleşik örnekleyici dizilerini (sampler2D u_Textures[32]) ayrık doku dizisi + tek örnekleyiciye çevirir:
    //   uniform texture2D u_Textures_cqtex[32]; uniform sampler u_Textures_cqsmp;
    //   u_Textures[i] -> sampler2D(u_Textures_cqtex[i], u_Textures_cqsmp)
    // Direct3D 11 ve Metal aşama başına en çok 16 örnekleyici destekler. Dizideki tüm dokular aynı örnekleyici
    // durumunu kullanır (çizimdeki ilk geçerli dokununki).
    inline std::string SplitLargeSamplerArrays(const std::string& src, uint32_t maxCombined)
    {
        static const std::regex s_Decl(R"((?:layout\s*\([^)]*\)\s*)?uniform\s+(?:(?:lowp|mediump|highp)\s+)?(sampler2D|samplerCube|isampler2D|usampler2D)\s+(\w+)\s*\[\s*(\d+)\s*\]\s*;)");
        struct Found
        {
            std::string Type, Name;
            uint32_t Count;
        };
        std::vector<Found> found;
        for (std::sregex_iterator m(src.begin(), src.end(), s_Decl), end; m != end; ++m)
        {
            const uint32_t count = (uint32_t)std::stoul((*m)[3].str());
            if (count > maxCombined)
                found.push_back({ (*m)[1].str(), (*m)[2].str(), count });
        }
        std::string s = src;
        for (const auto& f : found)
        {
            const std::string texType = f.Type == "samplerCube" ? "textureCube" : f.Type == "isampler2D" ? "itexture2D" : f.Type == "usampler2D" ? "utexture2D" : "texture2D";
            const std::regex decl("(?:layout\\s*\\([^)]*\\)\\s*)?uniform\\s+(?:(?:lowp|mediump|highp)\\s+)?" + f.Type + "\\s+" + f.Name + "\\s*\\[\\s*\\d+\\s*\\]\\s*;");
            s = std::regex_replace(s, decl, "uniform " + texType + " " + f.Name + "_cqtex[" + std::to_string(f.Count) + "]; uniform sampler " + f.Name + "_cqsmp;",
                                   std::regex_constants::format_first_only);
            const std::regex use("\\b" + f.Name + "\\s*\\[");
            std::string out;
            size_t pos = 0;
            std::smatch m;
            std::string rest = s;
            while (std::regex_search(rest, m, use))
            {
                const size_t at = pos + (size_t)m.position(0);
                const size_t open = at + (size_t)m.length(0) - 1;
                const size_t close = MatchingBracket(s, open);
                if (close == std::string::npos)
                    break;
                out += s.substr(pos, at - pos);
                out += f.Type + "(" + f.Name + "_cqtex" + s.substr(open, close - open + 1) + ", " + f.Name + "_cqsmp)";
                pos = close + 1;
                rest = s.substr(pos);
            }
            out += s.substr(pos);
            s = out;
        }
        return s;
    }
}
