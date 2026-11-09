#include "UIToolkit.h"

#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "UI/FontAtlas.h"
#include "UI/RichText.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<UIDocumentComponent>(Entity, UIDocumentComponent&) {}
    template<> void Scene::OnComponentAdded<UISkinComponent>(Entity, UISkinComponent&) {}

    namespace UIToolkit
    {
        namespace
        {
            namespace fs = std::filesystem;

            struct SelectorPart
            {
                std::string Type, Id, Pseudo;
                std::vector<std::string> Classes;
            };
            using Selector = std::vector<SelectorPart>;     // soy zinciri: son öğe hedef

            struct Rule
            {
                std::vector<Selector> Selectors;
                std::map<std::string, std::string> Props;
            };

            struct Sheet
            {
                std::vector<Rule> Rules;
                std::map<std::string, std::string> Variables;
            };

            struct Element
            {
                std::string Type, Name;
                std::vector<std::string> Classes;
                std::map<std::string, std::string> Attrs, Style, Hover, Active;
                std::vector<std::unique_ptr<Element>> Children;
                Element* Parent = nullptr;
                float X = 0.0f, Y = 0.0f, W = 0.0f, H = 0.0f;   // üst öğeye göre
            };

            std::string Trim(const std::string& s)
            {
                const size_t a = s.find_first_not_of(" \t\r\n");
                if (a == std::string::npos)
                    return "";
                return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
            }

            std::string ReadFile(const std::string& path)
            {
                std::ifstream in(SceneSerializer::ResolveAssetPath(path), std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }

            std::string Unescape(std::string s)
            {
                static const std::pair<const char*, const char*> kEntities[] = { { "&quot;", "\"" }, { "&apos;", "'" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&amp;", "&" } };
                for (const auto& [from, to] : kEntities)
                    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos;)
                    {
                        s.replace(p, std::strlen(from), to);
                        p += std::strlen(to);
                    }
                return s;
            }

            std::vector<std::string> Words(const std::string& s)
            {
                std::vector<std::string> out;
                std::stringstream ss(s);
                std::string w;
                while (ss >> w)
                    out.push_back(w);
                return out;
            }

            std::map<std::string, std::string> ParseDeclarations(const std::string& body)
            {
                std::map<std::string, std::string> props;
                std::stringstream ss(body);
                std::string decl;
                while (std::getline(ss, decl, ';'))
                {
                    const size_t colon = decl.find(':');
                    if (colon == std::string::npos)
                        continue;
                    const std::string key = Trim(decl.substr(0, colon)), value = Trim(decl.substr(colon + 1));
                    if (!key.empty() && !value.empty())
                        props[key] = value;
                }
                return props;
            }

            SelectorPart ParseSelectorPart(const std::string& text)
            {
                SelectorPart part;
                size_t i = 0;
                const auto token = [&]() {
                    const size_t begin = i;
                    while (i < text.size() && text[i] != '.' && text[i] != '#' && text[i] != ':')
                        ++i;
                    return text.substr(begin, i - begin);
                };
                if (i < text.size() && text[i] != '.' && text[i] != '#' && text[i] != ':')
                    part.Type = token();
                while (i < text.size())
                {
                    const char kind = text[i++];
                    const std::string word = token();
                    if (kind == '.') part.Classes.push_back(word);
                    else if (kind == '#') part.Id = word;
                    else part.Pseudo = word;
                }
                if (part.Type == "*")
                    part.Type.clear();
                return part;
            }

            void ParseSheet(const std::string& source, Sheet& sheet)
            {
                std::string text;
                for (size_t i = 0; i < source.size();)
                {
                    if (source.compare(i, 2, "/*") == 0)
                    {
                        const size_t end = source.find("*/", i + 2);
                        i = end == std::string::npos ? source.size() : end + 2;
                        continue;
                    }
                    text += source[i++];
                }
                for (size_t i = 0; i < text.size();)
                {
                    const size_t open = text.find('{', i);
                    if (open == std::string::npos)
                        break;
                    const size_t close = text.find('}', open);
                    if (close == std::string::npos)
                        break;
                    Rule rule;
                    std::stringstream selectors(text.substr(i, open - i));
                    std::string one;
                    while (std::getline(selectors, one, ','))
                    {
                        Selector selector;
                        for (const auto& word : Words(one))
                            selector.push_back(ParseSelectorPart(word));
                        if (!selector.empty())
                            rule.Selectors.push_back(std::move(selector));
                    }
                    rule.Props = ParseDeclarations(text.substr(open + 1, close - open - 1));
                    for (auto it = rule.Props.begin(); it != rule.Props.end();)
                    {
                        if (it->first.rfind("--", 0) == 0)
                        {
                            sheet.Variables[it->first] = it->second;
                            it = rule.Props.erase(it);
                        }
                        else
                            ++it;
                    }
                    if (!rule.Selectors.empty())
                        sheet.Rules.push_back(std::move(rule));
                    i = close + 1;
                }
            }

            bool ParseDocument(const std::string& text, Element& root, std::vector<std::string>& styles)
            {
                std::vector<Element*> stack{ &root };
                size_t i = 0;
                while (i < text.size())
                {
                    const size_t open = text.find('<', i);
                    if (open == std::string::npos)
                        break;
                    if (text.compare(open, 4, "<!--") == 0)
                    {
                        const size_t end = text.find("-->", open);
                        if (end == std::string::npos)
                            return false;
                        i = end + 3;
                        continue;
                    }
                    if (text.compare(open, 2, "<?") == 0)
                    {
                        const size_t end = text.find("?>", open);
                        if (end == std::string::npos)
                            return false;
                        i = end + 2;
                        continue;
                    }
                    const size_t close = text.find('>', open);
                    if (close == std::string::npos)
                        return false;
                    std::string body = text.substr(open + 1, close - open - 1);
                    i = close + 1;
                    if (!body.empty() && body[0] == '/')
                    {
                        if (stack.size() > 1)
                            stack.pop_back();
                        continue;
                    }
                    const bool selfClosing = !body.empty() && body.back() == '/';
                    if (selfClosing)
                        body.pop_back();
                    size_t p = 0;
                    while (p < body.size() && !std::isspace((unsigned char)body[p]))
                        ++p;
                    std::string type = body.substr(0, p);
                    if (const size_t colon = type.find(':'); colon != std::string::npos)
                        type = type.substr(colon + 1);
                    std::map<std::string, std::string> attrs;
                    while (p < body.size())
                    {
                        while (p < body.size() && std::isspace((unsigned char)body[p]))
                            ++p;
                        const size_t keyStart = p;
                        while (p < body.size() && body[p] != '=' && !std::isspace((unsigned char)body[p]))
                            ++p;
                        const std::string key = body.substr(keyStart, p - keyStart);
                        while (p < body.size() && std::isspace((unsigned char)body[p]))
                            ++p;
                        if (p >= body.size() || body[p] != '=')
                            continue;
                        ++p;
                        while (p < body.size() && std::isspace((unsigned char)body[p]))
                            ++p;
                        if (p >= body.size() || (body[p] != '"' && body[p] != '\''))
                            continue;
                        const char quote = body[p++];
                        const size_t valueEnd = body.find(quote, p);
                        if (valueEnd == std::string::npos)
                            return false;
                        attrs[key] = Unescape(body.substr(p, valueEnd - p));
                        p = valueEnd + 1;
                    }
                    if (type == "UXML")
                    {
                        if (selfClosing)
                            break;
                        continue;
                    }
                    if (type == "Style")
                    {
                        if (attrs.count("src"))
                            styles.push_back(attrs["src"]);
                        continue;
                    }
                    auto element = std::make_unique<Element>();
                    element->Type = type;
                    element->Name = attrs.count("name") ? attrs["name"] : "";
                    if (attrs.count("class"))
                        element->Classes = Words(attrs["class"]);
                    element->Attrs = attrs;
                    element->Parent = stack.back();
                    Element* raw = element.get();
                    stack.back()->Children.push_back(std::move(element));
                    if (!selfClosing)
                        stack.push_back(raw);
                }
                return true;
            }

            bool MatchPart(const SelectorPart& part, const Element& e)
            {
                if (!part.Type.empty() && part.Type != e.Type)
                    return false;
                if (!part.Id.empty() && part.Id != e.Name)
                    return false;
                for (const auto& c : part.Classes)
                    if (std::find(e.Classes.begin(), e.Classes.end(), c) == e.Classes.end())
                        return false;
                return true;
            }

            bool Matches(const Selector& selector, const Element& e)
            {
                if (selector.empty() || !MatchPart(selector.back(), e))
                    return false;
                const Element* cursor = e.Parent;
                for (size_t i = selector.size() - 1; i-- > 0;)
                {
                    while (cursor && !MatchPart(selector[i], *cursor))
                        cursor = cursor->Parent;
                    if (!cursor)
                        return false;
                    cursor = cursor->Parent;
                }
                return true;
            }

            int Specificity(const Selector& selector)
            {
                int s = 0;
                for (const auto& p : selector)
                    s += (p.Id.empty() ? 0 : 100) + (int)p.Classes.size() * 10 + (p.Type.empty() ? 0 : 1) + (p.Pseudo.empty() ? 0 : 10);
                return s;
            }

            void ApplySheets(Element& e, const std::vector<Sheet>& sheets)
            {
                struct Hit { int Rank; int Order; const Rule* Source; std::string Pseudo; };
                std::vector<Hit> hits;
                int order = 0;
                for (const auto& sheet : sheets)
                    for (const auto& rule : sheet.Rules)
                    {
                        ++order;
                        for (const auto& selector : rule.Selectors)
                            if (Matches(selector, e))
                                hits.push_back({ Specificity(selector), order, &rule, selector.back().Pseudo });
                    }
                std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                    return a.Rank != b.Rank ? a.Rank < b.Rank : a.Order < b.Order;
                });
                for (const auto& h : hits)
                {
                    auto& target = h.Pseudo == "hover" ? e.Hover : h.Pseudo == "active" ? e.Active : e.Style;
                    for (const auto& [k, v] : h.Source->Props)
                        target[k] = v;
                }
                if (auto it = e.Attrs.find("style"); it != e.Attrs.end())
                    for (const auto& [k, v] : ParseDeclarations(it->second))
                        e.Style[k] = v;
                // Devralınan özellikler
                if (e.Parent)
                    for (const char* key : { "color", "font-size", "-unity-font", "-unity-text-align" })
                        if (!e.Style.count(key) && e.Parent->Style.count(key))
                            e.Style[key] = e.Parent->Style[key];
                for (auto& c : e.Children)
                    ApplySheets(*c, sheets);
            }

            std::string ResolveVars(std::string value, const std::map<std::string, std::string>& vars)
            {
                for (size_t p; (p = value.find("var(")) != std::string::npos;)
                {
                    const size_t end = value.find(')', p);
                    if (end == std::string::npos)
                        break;
                    const std::string name = Trim(value.substr(p + 4, end - p - 4));
                    auto it = vars.find(name);
                    value.replace(p, end - p + 1, it == vars.end() ? "" : it->second);
                }
                return value;
            }

            void ExpandVariables(Element& e, const std::map<std::string, std::string>& vars)
            {
                for (auto* map : { &e.Style, &e.Hover, &e.Active })
                    for (auto& [k, v] : *map)
                        v = ResolveVars(v, vars);
                for (auto& c : e.Children)
                    ExpandVariables(*c, vars);
            }

            std::string Get(const Element& e, const char* key, const std::string& def = "")
            {
                auto it = e.Style.find(key);
                return it == e.Style.end() ? def : it->second;
            }

            // "10px", "10", "50%": yoksa fallback; yüzde parent'a göre
            float Length(const std::string& value, float parent, float fallback)
            {
                if (value.empty() || value == "auto")
                    return fallback;
                if (value.back() == '%')
                    return (float)std::atof(value.c_str()) * 0.01f * parent;
                return (float)std::atof(value.c_str());
            }

            bool IsAuto(const std::string& value)
            {
                return value.empty() || value == "auto";
            }

            struct Sides { float L = 0.0f, R = 0.0f, T = 0.0f, B = 0.0f; };

            Sides ReadSides(const Element& e, const char* base, float w, float h)
            {
                Sides s;
                const std::string name(base);
                if (const std::string all = Get(e, base); !all.empty())
                {
                    const auto parts = Words(all);
                    const auto at = [&](size_t i, float parent) { return Length(parts[i], parent, 0.0f); };
                    if (parts.size() == 1) s.L = s.R = at(0, w), s.T = s.B = at(0, h);
                    else if (parts.size() == 2) s.T = s.B = at(0, h), s.L = s.R = at(1, w);
                    else if (parts.size() == 3) s.T = at(0, h), s.L = s.R = at(1, w), s.B = at(2, h);
                    else if (parts.size() >= 4) s.T = at(0, h), s.R = at(1, w), s.B = at(2, h), s.L = at(3, w);
                }
                s.L = Length(Get(e, (name + "-left").c_str()), w, s.L);
                s.R = Length(Get(e, (name + "-right").c_str()), w, s.R);
                s.T = Length(Get(e, (name + "-top").c_str()), h, s.T);
                s.B = Length(Get(e, (name + "-bottom").c_str()), h, s.B);
                return s;
            }

            std::string TextOf(const Element& e)
            {
                auto it = e.Attrs.find("text");
                if (it != e.Attrs.end())
                    return it->second;
                it = e.Attrs.find("label");
                if (it != e.Attrs.end())
                    return it->second;
                return "";
            }

            float FontSize(const Element& e)
            {
                return Length(Get(e, "font-size"), 0.0f, 24.0f);
            }

            std::string FontOf(const Element& e)
            {
                const std::string raw = Get(e, "-unity-font");
                if (raw.empty())
                    return "";
                const size_t a = raw.find('"'), b = raw.rfind('"');
                if (a != std::string::npos && b > a)
                    return raw.substr(a + 1, b - a - 1);
                return raw;
            }

            glm::vec2 MeasureText(const Element& e)
            {
                const std::string text = TextOf(e);
                if (text.empty())
                    return glm::vec2(0.0f);
                auto font = FontAtlas::Get(FontOf(e));
                std::vector<uint32_t> cps;
                std::vector<RichText::Style> styles;
                RichText::Parse(text, cps, styles);
                const float size = FontSize(e);
                float width = 2.0f;
                for (size_t i = 0; i < cps.size(); ++i)
                {
                    const float px = styles[i].Size > 0.0f ? styles[i].Size : styles[i].Size < 0.0f ? size * -styles[i].Size : size;
                    if (!RichText::IsGlyph(cps[i]))
                    {
                        width += font->GetGlyph(cps[i]).Advance * px / FontAtlas::BaseSize;
                        continue;
                    }
                    const std::string spec = RichText::GlyphSpec(cps[i]);
                    float scale = 1.0f;
                    float aspect = 1.0f;
                    if (spec.rfind("sprite:", 0) == 0)
                        if (auto tex = RichText::SpriteTexture(spec.substr(7), &scale); tex && tex->GetHeight() > 0)
                            aspect = (float)tex->GetWidth() / (float)tex->GetHeight();
                    width += px * (1.25f * scale * aspect + 0.12f);
                }
                return { width, font->GetLineHeight() * size / FontAtlas::BaseSize };
            }

            bool IsRow(const Element& e)
            {
                const std::string d = Get(e, "flex-direction", "column");
                return d == "row" || d == "row-reverse";
            }

            bool IsAbsolute(const Element& e)
            {
                return Get(e, "position") == "absolute";
            }

            bool IsHidden(const Element& e)
            {
                return Get(e, "display") == "none";
            }

            glm::vec2 Measure(const Element& e, const glm::vec2& parent)
            {
                glm::vec2 size(0.0f);
                const Sides pad = ReadSides(e, "padding", parent.x, parent.y);
                if (e.Type == "Label")
                    size = MeasureText(e);
                else if (e.Type == "Button")
                    size = MeasureText(e) + glm::vec2(24.0f, 12.0f);
                else if (e.Type == "Toggle")
                    size = MeasureText(e) + glm::vec2(54.0f, 0.0f), size.y = std::max(size.y, 44.0f);
                else if (e.Type == "Slider")
                    size = { 300.0f, 40.0f };
                else if (e.Type == "TextField")
                    size = { 340.0f, 56.0f };
                else if (e.Type == "DropdownField")
                    size = { 280.0f, 52.0f };
                else if (e.Type == "Image")
                    size = { 100.0f, 100.0f };
                else
                {
                    const bool row = IsRow(e);
                    for (const auto& c : e.Children)
                    {
                        if (IsAbsolute(*c) || IsHidden(*c))
                            continue;
                        glm::vec2 s = Measure(*c, parent);
                        s.x = Length(Get(*c, "width"), parent.x, s.x);
                        s.y = Length(Get(*c, "height"), parent.y, s.y);
                        const Sides m = ReadSides(*c, "margin", parent.x, parent.y);
                        s += glm::vec2(m.L + m.R, m.T + m.B);
                        if (row) { size.x += s.x; size.y = std::max(size.y, s.y); }
                        else { size.y += s.y; size.x = std::max(size.x, s.x); }
                    }
                }
                return size + glm::vec2(pad.L + pad.R, pad.T + pad.B);
            }

            void Layout(Element& e)
            {
                const Sides pad = ReadSides(e, "padding", e.W, e.H);
                const bool row = IsRow(e);
                const glm::vec2 content(std::max(0.0f, e.W - pad.L - pad.R), std::max(0.0f, e.H - pad.T - pad.B));
                const float contentMain = row ? content.x : content.y;
                const float contentCross = row ? content.y : content.x;
                struct Slot { Element* E; float Main, Cross, Grow, Shrink; Sides Margin; };
                std::vector<Slot> slots;
                float used = 0.0f, grow = 0.0f, shrinkTotal = 0.0f;
                for (auto& child : e.Children)
                {
                    Element& c = *child;
                    if (IsHidden(c))
                    {
                        c.W = c.H = 0.0f;
                        continue;
                    }
                    if (IsAbsolute(c))
                    {
                        glm::vec2 s = Measure(c, content);
                        const std::string left = Get(c, "left"), right = Get(c, "right"), top = Get(c, "top"), bottom = Get(c, "bottom");
                        c.W = !IsAuto(Get(c, "width")) ? Length(Get(c, "width"), e.W, s.x) : (!IsAuto(left) && !IsAuto(right) ? e.W - Length(left, e.W, 0) - Length(right, e.W, 0) : s.x);
                        c.H = !IsAuto(Get(c, "height")) ? Length(Get(c, "height"), e.H, s.y) : (!IsAuto(top) && !IsAuto(bottom) ? e.H - Length(top, e.H, 0) - Length(bottom, e.H, 0) : s.y);
                        c.X = !IsAuto(left) ? Length(left, e.W, 0.0f) : !IsAuto(right) ? e.W - c.W - Length(right, e.W, 0.0f) : 0.0f;
                        c.Y = !IsAuto(top) ? Length(top, e.H, 0.0f) : !IsAuto(bottom) ? e.H - c.H - Length(bottom, e.H, 0.0f) : 0.0f;
                        Layout(c);
                        continue;
                    }
                    Slot slot;
                    slot.E = &c;
                    slot.Margin = ReadSides(c, "margin", content.x, content.y);
                    const glm::vec2 intrinsic = Measure(c, content);
                    const std::string width = Get(c, "width"), height = Get(c, "height");
                    float w = Length(width, content.x, intrinsic.x), h = Length(height, content.y, intrinsic.y);
                    slot.Main = row ? w : h;
                    slot.Cross = row ? h : w;
                    slot.Grow = (float)std::atof(Get(c, "flex-grow", "0").c_str());
                    slot.Shrink = (float)std::atof(Get(c, "flex-shrink", "1").c_str());
                    const float marginMain = row ? slot.Margin.L + slot.Margin.R : slot.Margin.T + slot.Margin.B;
                    used += slot.Main + marginMain;
                    grow += slot.Grow;
                    shrinkTotal += slot.Shrink * slot.Main;
                    slots.push_back(slot);
                }
                float leftover = contentMain - used;
                if (leftover > 0.0f && grow > 0.0f)
                {
                    for (auto& s : slots)
                        s.Main += leftover * s.Grow / grow;
                    leftover = 0.0f;
                }
                else if (leftover < 0.0f && shrinkTotal > 0.0f)
                {
                    for (auto& s : slots)
                        s.Main = std::max(0.0f, s.Main + leftover * s.Shrink * s.Main / shrinkTotal);
                    leftover = 0.0f;
                }
                const std::string justify = Get(e, "justify-content", "flex-start");
                float cursor = 0.0f, gap = 0.0f;
                if (justify == "center") cursor = leftover * 0.5f;
                else if (justify == "flex-end") cursor = leftover;
                else if (justify == "space-between" && slots.size() > 1) gap = std::max(0.0f, leftover) / (float)(slots.size() - 1);
                else if (justify == "space-around" && !slots.empty()) { gap = std::max(0.0f, leftover) / (float)slots.size(); cursor = gap * 0.5f; }
                const std::string alignItems = Get(e, "align-items", "stretch");
                for (auto& s : slots)
                {
                    Element& c = *s.E;
                    const std::string align = Get(c, "align-self", alignItems);
                    const float marginBefore = row ? s.Margin.L : s.Margin.T, marginAfter = row ? s.Margin.R : s.Margin.B;
                    const float crossBefore = row ? s.Margin.T : s.Margin.L, crossAfter = row ? s.Margin.B : s.Margin.R;
                    const bool autoCross = IsAuto(Get(c, row ? "height" : "width"));
                    float cross = (align == "stretch" && autoCross) ? std::max(0.0f, contentCross - crossBefore - crossAfter) : s.Cross;
                    float crossPos = crossBefore;
                    if (align == "center") crossPos = (contentCross - cross) * 0.5f;
                    else if (align == "flex-end") crossPos = contentCross - cross - crossAfter;
                    const float mainPos = cursor + marginBefore;
                    c.X = pad.L + (row ? mainPos : crossPos);
                    c.Y = pad.T + (row ? crossPos : mainPos);
                    c.W = row ? s.Main : cross;
                    c.H = row ? cross : s.Main;
                    cursor += marginBefore + s.Main + marginAfter + gap;
                    Layout(c);
                }
                if (Get(e, "flex-direction") == "row-reverse" || Get(e, "flex-direction") == "column-reverse")
                    for (auto& s : slots)
                    {
                        if (row) s.E->X = e.W - s.E->X - s.E->W;
                        else s.E->Y = e.H - s.E->Y - s.E->H;
                    }
            }

            bool ParseCssColor(const std::string& value, glm::vec4& out)
            {
                if (value.empty())
                    return false;
                if (value.rfind("rgb", 0) == 0)
                {
                    const size_t a = value.find('('), b = value.find(')');
                    if (a == std::string::npos || b == std::string::npos)
                        return false;
                    std::stringstream ss(value.substr(a + 1, b - a - 1));
                    std::string part;
                    float v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    for (int i = 0; i < 4 && std::getline(ss, part, ','); ++i)
                        v[i] = (float)std::atof(part.c_str()) / (i < 3 ? 255.0f : 1.0f);
                    out = glm::vec4(v[0], v[1], v[2], v[3]);
                    return true;
                }
                return RichText::ParseColor(value, out);
            }

            std::string UrlOf(const std::string& value)
            {
                const size_t a = value.find("url("), b = value.rfind(')');
                if (a == std::string::npos || b == std::string::npos)
                    return value;
                std::string inner = Trim(value.substr(a + 4, b - a - 4));
                if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\''))
                    inner = inner.substr(1, inner.size() - 2);
                return inner;
            }

            void ApplyText(const Element& e, UITextComponent& t)
            {
                t.Text = TextOf(e);
                t.FontSize = FontSize(e);
                t.FontPath = FontOf(e);
                t.RichText = true;
                t.WordWrap = Get(e, "white-space", "normal") != "nowrap";
                glm::vec4 color;
                t.Color = ParseCssColor(Get(e, "color"), color) ? color : glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);
                const std::string align = Get(e, "-unity-text-align", e.Type == "Button" ? "middle-center" : "middle-left");
                t.AlignV = align.rfind("upper", 0) == 0 ? UITextAlignV::Top : align.rfind("lower", 0) == 0 ? UITextAlignV::Bottom : UITextAlignV::Middle;
                t.AlignH = align.find("left") != std::string::npos ? UITextAlignH::Left : align.find("right") != std::string::npos ? UITextAlignH::Right : UITextAlignH::Center;
            }

            void ApplyBackground(const Element& e, Entity entity)
            {
                glm::vec4 color(1.0f);
                const bool hasColor = ParseCssColor(Get(e, "background-color"), color);
                const std::string image = Get(e, "background-image");
                if (!hasColor && image.empty() && !entity.HasComponent<UIImageComponent>())
                    return;
                auto& img = entity.HasComponent<UIImageComponent>() ? entity.GetComponent<UIImageComponent>() : entity.AddComponent<UIImageComponent>();
                if (hasColor)
                    img.Color = color;
                else if (image.empty())
                    img.Color = glm::vec4(0.0f);
                if (!image.empty())
                    img.TexturePath = UrlOf(image);
                img.CornerRadius = Length(Get(e, "border-radius"), 0.0f, 0.0f);
            }

            void SetRect(Entity entity, const Element& e, bool top)
            {
                auto& rt = entity.GetComponent<RectTransformComponent>();
                rt.AnchorMin = rt.AnchorMax = glm::vec2(0.0f, 1.0f);
                rt.Pivot = glm::vec2(0.0f, 1.0f);
                rt.AnchoredPosition = top ? glm::vec2(0.0f) : glm::vec2(e.X, -e.Y);
                rt.SizeDelta = glm::vec2(e.W, e.H);
            }

            void ColorsFromPseudo(const Element& e, UISelectableState& s)
            {
                glm::vec4 base(1.0f), c(1.0f);
                ParseCssColor(Get(e, "background-color"), base);
                const auto tint = [&](const std::map<std::string, std::string>& map, glm::vec4& out) {
                    auto it = map.find("background-color");
                    if (it != map.end() && ParseCssColor(it->second, c))
                        out = glm::vec4(c.r / std::max(base.r, 0.001f), c.g / std::max(base.g, 0.001f), c.b / std::max(base.b, 0.001f), 1.0f);
                };
                tint(e.Hover, s.HighlightedColor);
                tint(e.Active, s.PressedColor);
            }

            void Build(Scene& scene, const Element& e, Entity parent, UIDocumentComponent& doc)
            {
                Entity entity;
                if (e.Type == "Toggle")
                {
                    entity = UISystem::CreateElement(scene, "Toggle", parent);
                    if (auto* label = entity.GetChildren().empty() ? nullptr : &entity)
                        (void)label;
                    entity.GetComponent<UIToggleComponent>().IsOn = e.Attrs.count("value") && e.Attrs.at("value") == "true";
                    if (auto it = e.Attrs.find("on-value-changed"); it != e.Attrs.end())
                        entity.GetComponent<UIToggleComponent>().OnValueChanged = it->second;
                    for (Entity child : entity.GetChildren())
                        if (child.HasComponent<UITextComponent>())
                            ApplyText(e, child.GetComponent<UITextComponent>());
                }
                else if (e.Type == "Slider")
                {
                    entity = UISystem::CreateElement(scene, "Slider", parent);
                    auto& s = entity.GetComponent<UISliderComponent>();
                    if (auto it = e.Attrs.find("low-value"); it != e.Attrs.end()) s.MinValue = (float)std::atof(it->second.c_str());
                    if (auto it = e.Attrs.find("high-value"); it != e.Attrs.end()) s.MaxValue = (float)std::atof(it->second.c_str());
                    if (auto it = e.Attrs.find("value"); it != e.Attrs.end()) s.Value = (float)std::atof(it->second.c_str());
                    if (auto it = e.Attrs.find("on-value-changed"); it != e.Attrs.end()) s.OnValueChanged = it->second;
                }
                else if (e.Type == "TextField")
                {
                    entity = UISystem::CreateElement(scene, "InputField", parent);
                    auto& f = entity.GetComponent<UIInputFieldComponent>();
                    if (auto it = e.Attrs.find("value"); it != e.Attrs.end()) f.Text = it->second;
                    if (auto it = e.Attrs.find("placeholder-text"); it != e.Attrs.end()) f.Placeholder = it->second;
                    if (auto it = e.Attrs.find("on-submit"); it != e.Attrs.end()) f.OnSubmit = it->second;
                    f.FontSize = FontSize(e);
                }
                else if (e.Type == "DropdownField")
                {
                    entity = UISystem::CreateElement(scene, "Dropdown", parent);
                    auto& d = entity.GetComponent<UIDropdownComponent>();
                    if (auto it = e.Attrs.find("choices"); it != e.Attrs.end())
                    {
                        d.Options.clear();
                        std::stringstream ss(it->second);
                        std::string option;
                        while (std::getline(ss, option, ','))
                            d.Options.push_back(Trim(option));
                    }
                    if (auto it = e.Attrs.find("on-value-changed"); it != e.Attrs.end())
                        d.OnValueChanged = it->second;
                }
                else
                {
                    entity = scene.CreateEntity(e.Name.empty() ? e.Type : e.Name);
                    entity.AddComponent<RectTransformComponent>();
                    if (entity.HasComponent<LayerComponent>())
                        entity.GetComponent<LayerComponent>().Layer = 5;
                    entity.SetParent(parent);
                    if (e.Type == "Label")
                        ApplyText(e, entity.AddComponent<UITextComponent>());
                    else if (e.Type == "Button")
                    {
                        auto& button = entity.AddComponent<UIButtonComponent>();
                        if (auto it = e.Attrs.find("on-click"); it != e.Attrs.end())
                            button.OnClick = it->second;
                        ColorsFromPseudo(e, button.Selectable);
                        Entity label = scene.CreateEntity("Text");
                        auto& rt = label.AddComponent<RectTransformComponent>();
                        rt.AnchorMin = glm::vec2(0.0f);
                        rt.AnchorMax = glm::vec2(1.0f);
                        rt.SizeDelta = glm::vec2(0.0f);
                        if (label.HasComponent<LayerComponent>())
                            label.GetComponent<LayerComponent>().Layer = 5;
                        label.SetParent(entity);
                        ApplyText(e, label.AddComponent<UITextComponent>());
                    }
                    else if (e.Type == "Image")
                    {
                        auto& img = entity.AddComponent<UIImageComponent>();
                        if (auto it = e.Attrs.find("src"); it != e.Attrs.end())
                            img.TexturePath = SceneSerializer::ResolveAssetPath(it->second);
                        img.PreserveAspect = true;
                    }
                    if (e.Type == "ScrollView")
                        entity.AddComponent<UIRectMask2DComponent>();
                }
                SetRect(entity, e, false);
                ApplyBackground(e, entity);
                if (auto it = e.Style.find("opacity"); it != e.Style.end())
                    entity.AddComponent<UICanvasGroupComponent>().Alpha = (float)std::atof(it->second.c_str());
                UIDocumentComponent::Item item;
                item.Entity = (uint32_t)(entt::entity)entity;
                item.Type = e.Type;
                item.Name = e.Name;
                item.Classes = e.Classes;
                doc.Items.push_back(std::move(item));
                for (const auto& c : e.Children)
                    if (!IsHidden(*c))
                        Build(scene, *c, entity, doc);
            }

            void LoadThemeSheets(const std::string& theme, std::vector<Sheet>& sheets, std::map<std::string, std::string>& variables, int depth = 0)
            {
                SimpleAssets::Data data;
                if (depth > 8 || theme.empty() || !SimpleAssets::Load(theme, data))
                    return;
                if (const std::string parent = data.Str("Inherits"); !parent.empty())
                    LoadThemeSheets(parent, sheets, variables, depth + 1);
                for (const auto& row : data.Table("Style Sheets"))
                {
                    Sheet sheet;
                    ParseSheet(ReadFile(SimpleAssets::Cell(row, 0)), sheet);
                    for (const auto& [k, v] : sheet.Variables)
                        variables[k] = v;
                    sheets.push_back(std::move(sheet));
                }
                for (const auto& row : data.Table("Variables"))
                    if (const std::string name = SimpleAssets::Cell(row, 0); !name.empty())
                        variables[name.rfind("--", 0) == 0 ? name : "--" + name] = SimpleAssets::Cell(row, 1);
            }

            void BuildDocument(Scene& scene, Entity host, UIDocumentComponent& doc)
            {
                doc.Built = true;
                if (doc.Source.empty())
                    return;
                glm::vec2 resolution(1920.0f, 1080.0f);
                float match = 0.5f;
                CanvasScalerComponent::UIScaleMode scaleMode = CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize;
                int sortOrder = doc.SortOrder;
                std::vector<Sheet> sheets;
                std::map<std::string, std::string> variables;
                SimpleAssets::Data settings;
                if (!doc.PanelSettings.empty() && SimpleAssets::Load(doc.PanelSettings, settings))
                {
                    if (settings.Str("Scale Mode") == "Constant Pixel Size")
                        scaleMode = CanvasScalerComponent::UIScaleMode::ConstantPixelSize;
                    const glm::vec3 reference = settings.Vec3("Reference Resolution", glm::vec3(1920.0f, 1080.0f, 0.0f));
                    resolution = glm::vec2(reference.x, reference.y);
                    match = settings.Float("Match", 0.5f);
                    sortOrder += settings.Int("Sort Order", 0);
                    LoadThemeSheets(settings.Str("Theme"), sheets, variables);
                }
                Element root;
                root.Type = "Root";
                std::vector<std::string> styles;
                if (!ParseDocument(ReadFile(doc.Source), root, styles))
                {
                    CQ_CORE_ERROR("[UI Document] Malformed document: {0}", doc.Source);
                    return;
                }
                const fs::path base = fs::path(doc.Source).parent_path();
                for (const auto& style : styles)
                {
                    Sheet sheet;
                    std::string path = style;
                    if (!fs::exists(SceneSerializer::ResolveAssetPath(path)))
                        path = (base / style).generic_string();
                    ParseSheet(ReadFile(path), sheet);
                    for (const auto& [k, v] : sheet.Variables)
                        variables[k] = v;
                    sheets.push_back(std::move(sheet));
                }
                ApplySheets(root, sheets);
                ExpandVariables(root, variables);
                root.X = root.Y = 0.0f;
                root.W = resolution.x;
                root.H = resolution.y;
                Layout(root);
                Entity canvas = UISystem::CreateCanvas(scene, "UI Document");
                auto& scaler = canvas.GetComponent<CanvasScalerComponent>();
                scaler.ScaleMode = scaleMode;
                scaler.ReferenceResolution = resolution;
                scaler.MatchWidthOrHeight = match;
                canvas.GetComponent<CanvasComponent>().SortOrder = sortOrder;
                canvas.SetParent(host);
                doc.Items.clear();
                for (const auto& c : root.Children)
                    if (!IsHidden(*c))
                        Build(scene, *c, canvas, doc);
            }

            void ApplySkinTo(Entity e, const SimpleAssets::Data& skin)
            {
                const auto selectable = [&](UISelectableState& s) {
                    s.NormalColor = skin.Color("Normal");
                    s.HighlightedColor = skin.Color("Highlighted");
                    s.PressedColor = skin.Color("Pressed");
                    s.DisabledColor = skin.Color("Disabled");
                };
                if (e.HasComponent<UITextComponent>())
                {
                    auto& t = e.GetComponent<UITextComponent>();
                    t.Color = skin.Color("Text Color");
                    if (const std::string font = skin.Str("Font"); !font.empty())
                        t.FontPath = font;
                }
                if (e.HasComponent<UIButtonComponent>())
                {
                    selectable(e.GetComponent<UIButtonComponent>().Selectable);
                    if (e.HasComponent<UIImageComponent>())
                    {
                        auto& img = e.GetComponent<UIImageComponent>();
                        img.Color = skin.Color("Button Color");
                        if (const std::string tex = skin.Str("Button Background"); !tex.empty())
                            img.TexturePath = tex;
                    }
                }
                else if (e.HasComponent<UIImageComponent>())
                {
                    auto& img = e.GetComponent<UIImageComponent>();
                    img.Color = skin.Color("Panel Color");
                    if (const std::string tex = skin.Str("Panel Background"); !tex.empty())
                        img.TexturePath = tex;
                }
                if (e.HasComponent<UIToggleComponent>())
                    selectable(e.GetComponent<UIToggleComponent>().Selectable);
                if (e.HasComponent<UISliderComponent>())
                {
                    auto& s = e.GetComponent<UISliderComponent>();
                    selectable(s.Selectable);
                    s.BackgroundColor = skin.Color("Slider Background");
                    s.FillColor = skin.Color("Slider Fill");
                    s.HandleColor = skin.Color("Slider Handle");
                }
                if (e.HasComponent<UIInputFieldComponent>())
                {
                    auto& f = e.GetComponent<UIInputFieldComponent>();
                    selectable(f.Selectable);
                    f.BackgroundColor = skin.Color("Input Background");
                    f.TextColor = skin.Color("Text Color");
                }
                for (Entity child : e.GetChildren())
                    ApplySkinTo(child, skin);
            }
        }

        void Update(Scene& scene)
        {
            std::vector<entt::entity> pending;
            for (auto e : scene.m_Registry.view<UIDocumentComponent>())
                if (!scene.m_Registry.get<UIDocumentComponent>(e).Built)
                    pending.push_back(e);
            for (auto e : pending)
                BuildDocument(scene, Entity{ e, &scene }, scene.m_Registry.get<UIDocumentComponent>(e));
            for (auto e : scene.m_Registry.view<UISkinComponent>())
            {
                auto& skin = scene.m_Registry.get<UISkinComponent>(e);
                if (skin.Applied)
                    continue;
                skin.Applied = true;
                SimpleAssets::Data data;
                if (!skin.Skin.empty() && SimpleAssets::Load(skin.Skin, data))
                    ApplySkinTo(Entity{ e, &scene }, data);
            }
        }

        Entity Query(Scene& scene, Entity document, const std::string& selector)
        {
            if (!document || !document.HasComponent<UIDocumentComponent>() || selector.empty())
                return {};
            const auto& doc = document.GetComponent<UIDocumentComponent>();
            const SelectorPart part = ParseSelectorPart(selector);
            for (const auto& item : doc.Items)
            {
                if ((!part.Type.empty() && part.Type != item.Type) || (!part.Id.empty() && part.Id != item.Name))
                    continue;
                bool classes = true;
                for (const auto& c : part.Classes)
                    classes = classes && std::find(item.Classes.begin(), item.Classes.end(), c) != item.Classes.end();
                if (classes)
                    return Entity{ (entt::entity)item.Entity, &scene };
            }
            return {};
        }

        std::string NewDocumentTemplate()
        {
            return "<UXML>\n"
                   "    <Style src=\"Assets/NewStyleSheet.cquss\" />\n"
                   "    <VisualElement class=\"root\">\n"
                   "        <Label name=\"title\" text=\"Hello\" />\n"
                   "        <Button name=\"start\" text=\"Start\" on-click=\"OnStart\" />\n"
                   "    </VisualElement>\n"
                   "</UXML>\n";
        }

        std::string NewStyleTemplate()
        {
            return ".root {\n    flex-grow: 1;\n    align-items: center;\n    justify-content: center;\n}\n\n"
                   "Label {\n    font-size: 48px;\n    color: rgb(255, 255, 255);\n}\n\n"
                   "Button {\n    margin-top: 16px;\n    padding: 12px 40px;\n    background-color: rgb(46, 110, 214);\n    color: rgb(255, 255, 255);\n    border-radius: 8px;\n}\n\n"
                   "Button:hover {\n    background-color: rgb(70, 140, 240);\n}\n";
        }

        void RegisterComponents()
        {
            using D = UIDocumentComponent;
            ComponentDef<D>("UI Document", "UIDocumentComponent", "UI", ICON_FA_WINDOW_MAXIMIZE)
                .Describe("Shows a UI Document (.cquxml) when the game plays: its elements become Canvas UI laid out with flexbox and styled by its style sheets. "
                          "Scripts find elements with UIQuery(self, \"#name\").")
                .Field("Source", "Source", &D::Source, FieldKind::Asset).Filter(".cquxml")
                .Field("PanelSettings", "Panel Settings", &D::PanelSettings, FieldKind::Asset).Filter(".cqpanelsettings")
                .Field("SortOrder", "Sort Order", &D::SortOrder);
            using S = UISkinComponent;
            ComponentDef<S>("UI Skin", "UISkinComponent", "UI", ICON_FA_PALETTE)
                .Describe("Applies a GUI Skin (.cqguiskin) to this Canvas and everything under it when the game plays.")
                .Field("Skin", "Skin", &S::Skin, FieldKind::Asset).Filter(".cqguiskin");
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            // UIQuery(documentEntity, "#name" | ".class" | "Button") -> öğe varlığı (yoksa -1)
            vm->DefineNative("UIQuery", [](int argc, Value* args) -> Value {
                Scene* scene = ScriptContext::GetActiveScene();
                if (!scene || argc < 2 || !args[1].IsString())
                    return Value::MakeInt(-1);
                Entity doc{ (entt::entity)(uint32_t)args[0].ToNumber(), scene };
                if (!scene->m_Registry.valid((entt::entity)doc))
                    return Value::MakeInt(-1);
                Entity found = Query(*scene, doc, args[1].AsString()->Value);
                return Value::MakeInt(found ? (int64_t)(uint32_t)(entt::entity)found : -1);
            }, 2);
        }
    }
}
