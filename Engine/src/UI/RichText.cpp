#include "RichText.h"
#include "Core/Input/InputGlyphs.h"
#include "UI/FontAtlas.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <deque>

namespace Conqueror::RichText
{
    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c + 32);
            return s;
        }

        std::string Unquote(std::string v)
        {
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
                v = v.substr(1, v.size() - 2);
            return v;
        }

        struct Tag
        {
            bool Closing = false;
            std::string Name;
            std::string Value;
            std::vector<std::pair<std::string, std::string>> Params;

            std::string ParamText(const char* key) const
            {
                for (const auto& [k, v] : Params)
                    if (k == key)
                        return v;
                return "";
            }

            float Param(const char* key, float fallback) const
            {
                for (const auto& [k, v] : Params)
                    if (k == key)
                        return (float)std::atof(v.c_str());
                return fallback;
            }
        };

        bool ParseTag(const std::string& body, Tag& tag)
        {
            size_t b = body.find_first_not_of(' '), e = body.find_last_not_of(' ');
            if (b == std::string::npos)
                return false;
            std::string s = body.substr(b, e - b + 1);
            if (s[0] == '/')
            {
                tag.Closing = true;
                s = s.substr(1);
            }
            size_t i = 0;
            while (i < s.size() && s[i] != '=' && s[i] != ' ')
                ++i;
            tag.Name = Lower(s.substr(0, i));
            if (i < s.size() && s[i] == '=')
            {
                size_t j = i + 1;
                while (j < s.size() && s[j] != ' ')
                    ++j;
                tag.Value = Unquote(s.substr(i + 1, j - i - 1));
                i = j;
            }
            // anahtar=değer parametreleri (wave amp=50 freq=5)
            while (i < s.size())
            {
                while (i < s.size() && s[i] == ' ')
                    ++i;
                size_t k = i;
                while (k < s.size() && s[k] != '=' && s[k] != ' ')
                    ++k;
                if (k >= s.size() || s[k] != '=')
                {
                    i = k;
                    continue;
                }
                size_t v = k + 1;
                while (v < s.size() && s[v] != ' ')
                    ++v;
                tag.Params.push_back({ Lower(s.substr(i, k - i)), Unquote(s.substr(k + 1, v - k - 1)) });
                i = v;
            }
            return !tag.Name.empty();
        }

        bool IsKnown(const std::string& name)
        {
            static const char* kNames[] = { "b", "i", "u", "s", "color", "size", "font_size", "wave", "shake", "rainbow", "lb", "rb", "glyph", "sprite", "gradient" };
            for (const char* n : kNames)
                if (name == n)
                    return true;
            return false;
        }

        // Etiketi stile uygula (açılış)
        bool Apply(const Tag& tag, Style& style)
        {
            const std::string& n = tag.Name;
            if (n == "b") style.Flags |= Bold;
            else if (n == "i") style.Flags |= Italic;
            else if (n == "u") style.Flags |= Underline;
            else if (n == "s") style.Flags |= Strike;
            else if (n == "gradient")
            {
                style.Gradient = FindGradient(Unquote(tag.Value));
                return style.Gradient != nullptr;
            }
            else if (n == "color")
            {
                glm::vec4 c;
                if (!ParseColor(tag.Value, c))
                    return false;
                style.Color = c;
                style.HasColor = true;
            }
            else if (n == "size" || n == "font_size")
            {
                if (tag.Value.empty())
                    return false;
                float v = (float)std::atof(tag.Value.c_str());
                // TextMeshPro: <size=150%> => bileşen boyutuna göre
                if (tag.Value.back() == '%')
                    v = -v / 100.0f;
                if (v == 0.0f)
                    return false;
                style.Size = v;
            }
            else if (n == "wave")
            {
                style.Flags |= Wave;
                style.WaveAmplitude = tag.Param("amp", 20.0f) * 0.4f;
                style.WaveFrequency = tag.Param("freq", 5.0f);
            }
            else if (n == "shake")
            {
                style.Flags |= Shake;
                style.ShakeRate = tag.Param("rate", 20.0f);
                style.ShakeLevel = tag.Param("level", 5.0f) * 0.6f;
            }
            else if (n == "rainbow")
            {
                style.Flags |= Rainbow;
                style.RainbowFrequency = tag.Param("freq", 1.0f) * 0.5f;
            }
            return true;
        }
    }

    bool ParseColor(const std::string& valueIn, glm::vec4& out)
    {
        std::string value = Lower(Unquote(valueIn));
        if (value.empty())
            return false;
        if (value[0] == '#')
        {
            std::string hex = value.substr(1);
            if (hex.size() == 3 || hex.size() == 4)
            {
                std::string full;
                for (char c : hex) { full += c; full += c; }
                hex = full;
            }
            if (hex.size() != 6 && hex.size() != 8)
                return false;
            for (char c : hex)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return false;
            const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
            if (hex.size() == 6)
                out = glm::vec4(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, 1.0f);
            else
                out = glm::vec4(((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
            return true;
        }
        struct Named { const char* Name; glm::vec4 Color; };
        static const Named kNamed[] = {
            { "red", { 1.0f, 0.0f, 0.0f, 1.0f } }, { "green", { 0.0f, 0.75f, 0.0f, 1.0f } }, { "blue", { 0.0f, 0.25f, 1.0f, 1.0f } },
            { "yellow", { 1.0f, 0.92f, 0.0f, 1.0f } }, { "orange", { 1.0f, 0.55f, 0.0f, 1.0f } }, { "purple", { 0.6f, 0.2f, 0.85f, 1.0f } },
            { "white", { 1.0f, 1.0f, 1.0f, 1.0f } }, { "black", { 0.0f, 0.0f, 0.0f, 1.0f } }, { "gray", { 0.5f, 0.5f, 0.5f, 1.0f } },
            { "grey", { 0.5f, 0.5f, 0.5f, 1.0f } }, { "cyan", { 0.0f, 1.0f, 1.0f, 1.0f } }, { "aqua", { 0.0f, 1.0f, 1.0f, 1.0f } },
            { "magenta", { 1.0f, 0.0f, 1.0f, 1.0f } }, { "pink", { 1.0f, 0.6f, 0.75f, 1.0f } }, { "lime", { 0.5f, 1.0f, 0.0f, 1.0f } },
            { "brown", { 0.55f, 0.33f, 0.15f, 1.0f } }, { "gold", { 1.0f, 0.84f, 0.0f, 1.0f } }, { "silver", { 0.75f, 0.75f, 0.75f, 1.0f } },
            { "transparent", { 0.0f, 0.0f, 0.0f, 0.0f } },
        };
        for (const auto& n : kNamed)
            if (value == n.Name)
            {
                out = n.Color;
                return true;
            }
        return false;
    }

    namespace
    {
        constexpr uint32_t kGlyphBase = 0x10F000;
        std::vector<std::string>& GlyphTable()
        {
            static std::vector<std::string> table;
            return table;
        }
    }

    namespace
    {
        namespace fs = std::filesystem;

        // Proje içi varlık taraması (saniyede en fazla bir kez)
        const std::vector<std::string>& AssetsOfKind(const char* kind)
        {
            struct Entry { double Time = -10.0; std::vector<std::string> Files; };
            static std::map<std::string, Entry> cache;
            auto& e = cache[kind];
            using namespace std::chrono;
            const double now = duration<double>(steady_clock::now().time_since_epoch()).count();
            if (now - e.Time > 1.0)
            {
                e.Time = now;
                e.Files = SimpleAssets::FindAll(kind);
            }
            return e.Files;
        }

        std::string ExpandStyles(const std::string& text)
        {
            if (text.find("style") == std::string::npos)
                return text;
            std::string out;
            std::vector<std::string> closing;
            size_t i = 0;
            const auto findStyle = [](const std::string& name, std::string& open, std::string& close) {
                for (const auto& file : AssetsOfKind("TextStyleSheet"))
                {
                    SimpleAssets::Data data;
                    if (!SimpleAssets::Load(file, data))
                        continue;
                    for (const auto& row : data.Table("Styles"))
                        if (SimpleAssets::Cell(row, 0) == name)
                        {
                            open = SimpleAssets::Cell(row, 1);
                            close = SimpleAssets::Cell(row, 2);
                            return true;
                        }
                }
                return false;
            };
            while (i < text.size())
            {
                if (text[i] == '<' || text[i] == '[')
                {
                    const char closeChar = text[i] == '<' ? '>' : ']';
                    const size_t end = text.find(closeChar, i + 1);
                    if (end != std::string::npos && end - i <= 96)
                    {
                        Tag tag;
                        if (ParseTag(text.substr(i + 1, end - i - 1), tag) && tag.Name == "style")
                        {
                            if (tag.Closing)
                            {
                                if (!closing.empty())
                                {
                                    out += closing.back();
                                    closing.pop_back();
                                }
                            }
                            else
                            {
                                std::string open, close;
                                const std::string name = tag.Value.empty() ? tag.ParamText("name") : tag.Value;
                                if (findStyle(Unquote(name), open, close))
                                {
                                    out += open;
                                    closing.push_back(close);
                                }
                            }
                            i = end + 1;
                            continue;
                        }
                    }
                }
                out += text[i++];
            }
            return out;
        }
    }

    const glm::vec4* FindGradient(const std::string& nameOrPath)
    {
        if (nameOrPath.empty())
            return nullptr;
        static std::map<std::string, std::array<glm::vec4, 4>> stored;
        std::string file;
        if (nameOrPath.find('.') != std::string::npos || nameOrPath.find('/') != std::string::npos)
            file = nameOrPath;
        else
            for (const auto& f : AssetsOfKind("TextColorGradient"))
                if (fs::path(f).stem().string() == nameOrPath)
                {
                    file = f;
                    break;
                }
        SimpleAssets::Data data;
        if (file.empty() || !SimpleAssets::Load(file, data))
            return nullptr;
        const std::string mode = data.Str("Mode");
        glm::vec4 tl = data.Color("Top Left"), tr = data.Color("Top Right"), bl = data.Color("Bottom Left"), br = data.Color("Bottom Right");
        if (mode == "Single") tr = bl = br = tl;
        else if (mode == "Horizontal") { bl = tl; br = tr; }
        else if (mode == "Vertical") { tr = tl; br = bl; }
        auto& slot = stored[file];
        slot = { bl, br, tr, tl };
        return slot.data();
    }

    std::shared_ptr<Conqueror::Texture2D> SpriteTexture(const std::string& name, float* scale)
    {
        static auto& textures = *new std::map<std::string, std::shared_ptr<Texture2D>>();
        for (const auto& file : AssetsOfKind("SpriteAsset"))
        {
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(file, data))
                continue;
            for (const auto& row : data.Table("Sprites"))
            {
                if (SimpleAssets::Cell(row, 0) != name)
                    continue;
                if (scale)
                    *scale = SimpleAssets::CellFloat(row, 2, 1.0f);
                const std::string path = SceneSerializer::ResolveAssetPath(SimpleAssets::Cell(row, 1));
                auto& tex = textures[path];
                if (!tex)
                    tex = Texture2D::Create(path);
                return tex;
            }
        }
        return nullptr;
    }

    uint32_t GlyphCodepoint(const std::string& spec)
    {
        auto& table = GlyphTable();
        for (size_t i = 0; i < table.size(); ++i)
            if (table[i] == spec)
                return kGlyphBase + (uint32_t)i;
        if (table.size() >= 0xFFD)
            return kGlyphBase;
        table.push_back(spec);
        return kGlyphBase + (uint32_t)(table.size() - 1);
    }

    bool IsGlyph(uint32_t cp) { return cp >= kGlyphBase && cp < kGlyphBase + (uint32_t)GlyphTable().size(); }

    std::string GlyphSpec(uint32_t cp) { return IsGlyph(cp) ? GlyphTable()[cp - kGlyphBase] : std::string(); }

    void Parse(const std::string& input, std::vector<uint32_t>& codepoints, std::vector<Style>& styles)
    {
        const std::string text = ExpandStyles(input);
        codepoints.clear();
        styles.clear();
        Style current;
        std::vector<std::pair<std::string, Style>> stack;   // (etiket, açılmadan önceki stil)
        std::string literal;
        std::vector<uint32_t> decoded;
        auto flush = [&]() {
            if (literal.empty())
                return;
            FontAtlas::DecodeUTF8(literal, decoded);
            for (uint32_t cp : decoded)
            {
                codepoints.push_back(cp);
                styles.push_back(current);
            }
            literal.clear();
        };
        size_t i = 0;
        while (i < text.size())
        {
            const char open = text[i];
            if (open == '[' || open == '<')
            {
                const char close = open == '[' ? ']' : '>';
                const size_t end = text.find(close, i + 1);
                if (end != std::string::npos && end - i <= 96 && text.find('\n', i) > end)
                {
                    Tag tag;
                    if (ParseTag(text.substr(i + 1, end - i - 1), tag) && IsKnown(tag.Name))
                    {
                        if (tag.Name == "glyph")
                        {
                            if (!tag.Closing && !tag.Value.empty())
                            {
                                flush();
                                codepoints.push_back(GlyphCodepoint(tag.Value));
                                styles.push_back(current);
                            }
                            i = end + 1;
                            continue;
                        }
                        if (tag.Name == "sprite")
                        {
                            if (!tag.Closing)
                            {
                                const std::string name = Unquote(tag.Value.empty() ? tag.ParamText("name") : tag.Value);
                                if (!name.empty())
                                {
                                    flush();
                                    codepoints.push_back(GlyphCodepoint("sprite:" + name));
                                    styles.push_back(current);
                                }
                            }
                            i = end + 1;
                            continue;
                        }
                        if (tag.Name == "lb" || tag.Name == "rb")
                        {
                            literal += tag.Name == "lb" ? '[' : ']';
                            i = end + 1;
                            continue;
                        }
                        if (tag.Closing)
                        {
                            for (size_t k = stack.size(); k-- > 0;)
                                if (stack[k].first == tag.Name || (tag.Name == "font_size" && stack[k].first == "size") ||
                                    (tag.Name == "size" && stack[k].first == "font_size"))
                                {
                                    flush();
                                    current = stack[k].second;
                                    stack.resize(k);
                                    break;
                                }
                            i = end + 1;
                            continue;
                        }
                        Style next = current;
                        if (Apply(tag, next))
                        {
                            flush();
                            stack.push_back({ tag.Name, current });
                            current = next;
                            i = end + 1;
                            continue;
                        }
                    }
                }
            }
            literal += text[i];
            ++i;
        }
        flush();
    }

    std::string Strip(const std::string& text)
    {
        std::vector<uint32_t> cps;
        std::vector<Style> styles;
        Parse(text, cps, styles);
        std::string out;
        out.reserve(cps.size());
        for (uint32_t cp : cps)
        {
            if (IsGlyph(cp))
            {
                if (GlyphSpec(cp).rfind("sprite:", 0) == 0)
                    continue;
                out += "[" + InputGlyphs::Label(GlyphSpec(cp)) + "]";
                continue;
            }
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        }
        return out;
    }
}
