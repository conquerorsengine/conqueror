#include "UIControls.h"

#include "Core/Project/Project.h"
#include "Core/Utils/PlayerPrefs.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/Signals.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "UI/FontAtlas.h"
#include "UI/UISystem.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <spawn.h>
    #include <sys/wait.h>
    extern char** environ;
#endif

namespace Conqueror
{
    namespace fs = std::filesystem;

    namespace
    {
        const glm::vec4 kAccent{ 0.26f, 0.59f, 0.98f, 1.0f };
        const glm::vec4 kBorder{ 0.72f, 0.73f, 0.77f, 1.0f };
        const glm::vec4 kText{ 0.12f, 0.12f, 0.14f, 1.0f };
        const glm::vec4 kMuted{ 0.48f, 0.49f, 0.53f, 1.0f };
        const glm::vec4 kHover{ 0.90f, 0.93f, 1.0f, 1.0f };
        const glm::vec4 kSelect{ 0.80f, 0.88f, 1.0f, 1.0f };

        uint32_t Id(Entity e) { return (uint32_t)(entt::entity)e; }
        double Clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        // ---------------------------------------------------------------- ertelenmiş olaylar (UI güncellemesinin sonunda gönderilir)
        struct PendingSignal
        {
            uint32_t Entity = 0;
            std::string Configured, Type;
            float Value = 0.0f;
            std::string Signal, Method;
            std::vector<CQS::Value> Args;
        };
        std::vector<PendingSignal>& Pending()
        {
            static std::vector<PendingSignal> pending;
            return pending;
        }
        void EmitEvent(Entity e, const std::string& configured, const char* type, float value, const char* signal, const char* method,
                       std::vector<CQS::Value> args)
        {
            UISystem::QueueEvent(e, configured, type, value);
            Pending().push_back({ Id(e), configured, type, value, signal ? signal : "", method ? method : "", std::move(args) });
        }

        // ---------------------------------------------------------------- UTF-8
        std::vector<uint32_t> Decode(const std::string& s)
        {
            std::vector<uint32_t> cps;
            FontAtlas::DecodeUTF8(s, cps);
            return cps;
        }
        std::string Encode(const std::vector<uint32_t>& cps, size_t from = 0, size_t to = std::string::npos)
        {
            std::string out;
            to = std::min(to, cps.size());
            for (size_t i = from; i < to; ++i)
            {
                uint32_t c = cps[i];
                if (c < 0x80) out += (char)c;
                else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
                else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
                else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
            }
            return out;
        }

        // ---------------------------------------------------------------- tek satır düzenleyici
        enum class EditResult { None, Changed, Enter, Escape };
        EditResult LineEditInput(UILineEditState& ed, const UIInputState& in, float dt, const std::function<bool(uint32_t)>& accept)
        {
            ed.Blink += dt;
            std::vector<uint32_t> cps = Decode(ed.Text);
            ed.Caret = std::clamp(ed.Caret, 0, (int)cps.size());
            bool changed = false;
            for (uint32_t c : in.Characters)
                if (c >= 32 && accept(c))
                {
                    cps.insert(cps.begin() + ed.Caret, c);
                    ++ed.Caret;
                    changed = true;
                }
            if (in.Backspace && ed.Caret > 0) { cps.erase(cps.begin() + ed.Caret - 1); --ed.Caret; changed = true; }
            if (in.Delete && ed.Caret < (int)cps.size()) { cps.erase(cps.begin() + ed.Caret); changed = true; }
            if (in.Left) ed.Caret = std::max(0, ed.Caret - 1);
            if (in.Right) ed.Caret = std::min((int)cps.size(), ed.Caret + 1);
            if (in.Home) ed.Caret = 0;
            if (in.End) ed.Caret = (int)cps.size();
            if (changed)
            {
                ed.Text = Encode(cps);
                ed.Blink = 0.0f;
            }
            if (in.Enter) return EditResult::Enter;
            if (in.Escape) return EditResult::Escape;
            return changed ? EditResult::Changed : EditResult::None;
        }
        void DrawLineEdit(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const UILineEditState& ed, float fs, const glm::vec4& color)
        {
            UIDraw::Text(m, mn, mx, ed.Text, fs, color, 0, 1);
            if (ed.Active && std::fmod(ed.Blink, 1.0f) < 0.55f)
            {
                const float x = mn.x + UIDraw::CaretX(ed.Text, ed.Caret, fs);
                const float h = UIDraw::LineHeight(fs) * 0.8f, cy = (mn.y + mx.y) * 0.5f;
                UIDraw::Rect(m, { x, cy - h * 0.5f }, { x + 2.0f, cy + h * 0.5f }, color);
            }
        }

        // ---------------------------------------------------------------- renk
        glm::vec3 HsvToRgb(float h, float s, float v)
        {
            h = h - std::floor(h);
            const float i = std::floor(h * 6.0f), f = h * 6.0f - i;
            const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
            switch (((int)i) % 6)
            {
                case 0: return { v, t, p };
                case 1: return { q, v, p };
                case 2: return { p, v, t };
                case 3: return { p, q, v };
                case 4: return { t, p, v };
                default: return { v, p, q };
            }
        }
        glm::vec3 RgbToHsv(glm::vec3 c)
        {
            const float mx = std::max({ c.r, c.g, c.b }), mn = std::min({ c.r, c.g, c.b }), d = mx - mn;
            float h = 0.0f;
            if (d > 1e-6f)
            {
                if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.0f);
                else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
                else h = (c.r - c.g) / d + 4.0f;
                h /= 6.0f;
                if (h < 0.0f) h += 1.0f;
            }
            return { h, mx > 1e-6f ? d / mx : 0.0f, mx };
        }
        std::string Hex(const glm::vec4& c, bool alpha)
        {
            char buf[16];
            auto b = [](float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
            if (alpha) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
            else std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
            return buf;
        }

        // Renk seçici gövdesi (Color Picker ve düğmesinin açılır paneli)
        struct PickerRef
        {
            float& H; float& S; float& V; int& Drag; glm::vec4& Color; bool EditAlpha; bool ShowHex;
        };
        struct PickerLayout { glm::vec2 SvMin, SvMax, HueMin, HueMax, AlphaMin, AlphaMax, RowMin, RowMax; };
        PickerLayout LayoutPicker(glm::vec2 origin, glm::vec2 size, bool alpha)
        {
            PickerLayout l;
            const float pad = 8.0f;
            l.RowMin = origin + glm::vec2(pad, pad);
            l.RowMax = origin + glm::vec2(size.x - pad, pad + 30.0f);
            float y = pad + 38.0f;
            if (alpha)
            {
                l.AlphaMin = origin + glm::vec2(pad, y);
                l.AlphaMax = origin + glm::vec2(size.x - pad, y + 16.0f);
                y += 24.0f;
            }
            l.SvMin = origin + glm::vec2(pad, y);
            l.SvMax = origin + glm::vec2(size.x - pad - 30.0f, size.y - pad);
            l.HueMin = origin + glm::vec2(size.x - pad - 20.0f, y);
            l.HueMax = origin + glm::vec2(size.x - pad, size.y - pad);
            return l;
        }
        void SyncPicker(PickerRef& p)
        {
            // Renk dışarıdan değiştiyse (betik, Inspector) HSV yeniden hesaplanır
            const glm::vec3 now = HsvToRgb(p.H, p.S, p.V);
            if (p.Drag == 0 && glm::length(now - glm::vec3(p.Color)) > 0.004f)
            {
                const glm::vec3 hsv = RgbToHsv(glm::vec3(p.Color));
                if (hsv.y > 0.001f && hsv.z > 0.001f) p.H = hsv.x;
                p.S = hsv.y;
                p.V = hsv.z;
            }
        }
        void DrawPicker(const glm::mat4& m, glm::vec2 origin, glm::vec2 size, PickerRef p)
        {
            SyncPicker(p);
            const PickerLayout l = LayoutPicker(origin, size, p.EditAlpha);
            // Doygunluk (x) / değer (y) karesi: hücrelerle degrade
            const int n = 20;
            const glm::vec2 cell = (l.SvMax - l.SvMin) / (float)n;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const glm::vec3 c = HsvToRgb(p.H, (x + 0.5f) / n, (y + 0.5f) / n);
                    const glm::vec2 a = l.SvMin + cell * glm::vec2((float)x, (float)y);
                    UIDraw::Rect(m, a, a + cell + glm::vec2(0.5f), glm::vec4(c, 1.0f));
                }
            const glm::vec2 mark = l.SvMin + (l.SvMax - l.SvMin) * glm::vec2(p.S, p.V);
            UIDraw::RoundRect(m, mark - glm::vec2(7.0f), mark + glm::vec2(7.0f), 7.0f, glm::vec4(1.0f));
            UIDraw::RoundRect(m, mark - glm::vec2(5.0f), mark + glm::vec2(5.0f), 5.0f, glm::vec4(glm::vec3(p.Color), 1.0f));
            // Renk tonu çubuğu
            const int hs = 24;
            const float hh = (l.HueMax.y - l.HueMin.y) / hs;
            for (int i = 0; i < hs; ++i)
                UIDraw::Rect(m, { l.HueMin.x, l.HueMin.y + hh * i }, { l.HueMax.x, l.HueMin.y + hh * (i + 1) + 0.5f }, glm::vec4(HsvToRgb((i + 0.5f) / hs, 1, 1), 1.0f));
            const float hy = l.HueMin.y + (l.HueMax.y - l.HueMin.y) * p.H;
            UIDraw::Rect(m, { l.HueMin.x - 3.0f, hy - 2.0f }, { l.HueMax.x + 3.0f, hy + 2.0f }, glm::vec4(1.0f));
            // Saydamlık çubuğu (dama tahtası üstünde)
            if (p.EditAlpha)
            {
                const int as = 16;
                const float aw = (l.AlphaMax.x - l.AlphaMin.x) / as;
                for (int i = 0; i < as; ++i)
                {
                    const float t = (i + 0.5f) / as;
                    const glm::vec3 checker = ((i % 2) == 0) ? glm::vec3(0.8f) : glm::vec3(0.6f);
                    UIDraw::Rect(m, { l.AlphaMin.x + aw * i, l.AlphaMin.y }, { l.AlphaMin.x + aw * (i + 1) + 0.5f, l.AlphaMax.y },
                                 glm::vec4(glm::mix(checker, glm::vec3(p.Color), t), 1.0f));
                }
                const float ax = l.AlphaMin.x + (l.AlphaMax.x - l.AlphaMin.x) * p.Color.a;
                UIDraw::Rect(m, { ax - 2.0f, l.AlphaMin.y - 3.0f }, { ax + 2.0f, l.AlphaMax.y + 3.0f }, glm::vec4(1.0f));
            }
            // Önizleme ve onaltılık değer
            UIDraw::RoundRect(m, l.RowMin, { l.RowMin.x + 60.0f, l.RowMax.y }, 4.0f, glm::vec4(glm::vec3(p.Color), 1.0f));
            if (p.ShowHex)
                UIDraw::Text(m, { l.RowMin.x + 72.0f, l.RowMin.y }, l.RowMax, Hex(p.Color, p.EditAlpha), 20.0f, kText, 0, 1);
        }
        // true: renk değişti
        bool PickerInput(glm::vec2 origin, glm::vec2 size, glm::vec2 local, bool press, PickerRef p)
        {
            const PickerLayout l = LayoutPicker(origin, size, p.EditAlpha);
            auto inside = [&](glm::vec2 a, glm::vec2 b) { return local.x >= a.x - 4 && local.x <= b.x + 4 && local.y >= a.y - 4 && local.y <= b.y + 4; };
            if (press)
            {
                p.Drag = inside(l.SvMin, l.SvMax) ? 1 : inside(l.HueMin, l.HueMax) ? 2 : (p.EditAlpha && inside(l.AlphaMin, l.AlphaMax)) ? 3 : 0;
                SyncPicker(p);
            }
            const glm::vec4 before = p.Color;
            if (p.Drag == 1)
            {
                const glm::vec2 t = glm::clamp((local - l.SvMin) / glm::max(l.SvMax - l.SvMin, glm::vec2(1.0f)), glm::vec2(0.0f), glm::vec2(1.0f));
                p.S = t.x;
                p.V = t.y;
            }
            else if (p.Drag == 2)
                p.H = std::clamp((local.y - l.HueMin.y) / std::max(l.HueMax.y - l.HueMin.y, 1.0f), 0.0f, 0.999f);
            else if (p.Drag == 3)
                p.Color.a = std::clamp((local.x - l.AlphaMin.x) / std::max(l.AlphaMax.x - l.AlphaMin.x, 1.0f), 0.0f, 1.0f);
            if (p.Drag == 1 || p.Drag == 2)
                p.Color = glm::vec4(HsvToRgb(p.H, p.S, p.V), p.Color.a);
            return glm::length(p.Color - before) > 1e-5f;
        }

        // ---------------------------------------------------------------- menü listesi
        struct MenuEntry { std::string Label; bool Separator = false, Check = false, Checked = false, Disabled = false; };
        MenuEntry ParseMenuItem(const std::string& raw)
        {
            MenuEntry e;
            std::string s = raw;
            if (s == "-") { e.Separator = true; return e; }
            if (!s.empty() && s[0] == '!') { e.Disabled = true; s = s.substr(1); }
            if (s.rfind("[x] ", 0) == 0 || s.rfind("[X] ", 0) == 0) { e.Check = e.Checked = true; s = s.substr(4); }
            else if (s.rfind("[ ] ", 0) == 0) { e.Check = true; s = s.substr(4); }
            e.Label = s;
            return e;
        }
        struct MenuGeom { glm::vec2 Min{ 0.0f }, Max{ 0.0f }; float RowH = 30.0f; };
        float EntryHeight(const MenuEntry& e, float rowH) { return e.Separator ? rowH * 0.4f : rowH; }
        // anchor: listenin sol üst köşesi (yerel)
        MenuGeom LayoutMenu(const std::vector<std::string>& items, float fs, glm::vec2 anchor, float minWidth)
        {
            MenuGeom g;
            g.RowH = std::max(30.0f, fs * 1.6f);
            float h = 8.0f, w = minWidth;
            for (const auto& raw : items)
            {
                const MenuEntry e = ParseMenuItem(raw);
                h += EntryHeight(e, g.RowH);
                w = std::max(w, UIDraw::TextWidth(e.Label, fs) + 56.0f);
            }
            g.Min = glm::vec2(anchor.x, anchor.y - h);
            g.Max = glm::vec2(anchor.x + w, anchor.y);
            return g;
        }
        int MenuItemAt(const std::vector<std::string>& items, const MenuGeom& g, glm::vec2 local, bool any = false)
        {
            if (local.x < g.Min.x || local.x > g.Max.x || local.y < g.Min.y || local.y > g.Max.y)
                return -1;
            float top = g.Max.y - 4.0f;
            for (int i = 0; i < (int)items.size(); ++i)
            {
                const MenuEntry e = ParseMenuItem(items[(size_t)i]);
                const float h = EntryHeight(e, g.RowH);
                if (local.y <= top && local.y > top - h)
                    return (any || (!e.Separator && !e.Disabled)) ? i : -1;
                top -= h;
            }
            return -1;
        }
        void DrawMenu(const glm::mat4& m, const std::vector<std::string>& items, const MenuGeom& g, int hovered, float fs)
        {
            UIDraw::RoundRect(m, g.Min - glm::vec2(1.0f, 3.0f), g.Max + glm::vec2(1.0f, 1.0f), 8.0f, glm::vec4(0, 0, 0, 0.18f));
            UIDraw::RoundRect(m, g.Min, g.Max, 8.0f, glm::vec4(1.0f));
            float top = g.Max.y - 4.0f;
            for (int i = 0; i < (int)items.size(); ++i)
            {
                const MenuEntry e = ParseMenuItem(items[(size_t)i]);
                const float h = EntryHeight(e, g.RowH);
                const glm::vec2 a(g.Min.x + 4.0f, top - h), b(g.Max.x - 4.0f, top);
                if (e.Separator)
                    UIDraw::Rect(m, { a.x + 6.0f, (a.y + b.y) * 0.5f - 0.5f }, { b.x - 6.0f, (a.y + b.y) * 0.5f + 0.5f }, kBorder);
                else
                {
                    if (i == hovered && !e.Disabled) UIDraw::RoundRect(m, a, b, 6.0f, kHover);
                    if (e.Check)
                    {
                        UIDraw::RoundRect(m, { a.x + 8.0f, (a.y + b.y) * 0.5f - 7.0f }, { a.x + 22.0f, (a.y + b.y) * 0.5f + 7.0f }, 3.0f, kBorder);
                        if (e.Checked)
                            UIDraw::RoundRect(m, { a.x + 11.0f, (a.y + b.y) * 0.5f - 4.0f }, { a.x + 19.0f, (a.y + b.y) * 0.5f + 4.0f }, 2.0f, kAccent);
                    }
                    UIDraw::Text(m, { a.x + 30.0f, a.y }, b, e.Label, fs, e.Disabled ? kMuted : kText, 0, 1);
                }
                top -= h;
            }
        }
        // Menü öğesine tıklandı: işaretli öğe değişir
        void PressMenuItem(std::vector<std::string>& items, int i)
        {
            std::string& s = items[(size_t)i];
            const bool disabled = !s.empty() && s[0] == '!';
            std::string body = disabled ? s.substr(1) : s;
            if (body.rfind("[x] ", 0) == 0 || body.rfind("[X] ", 0) == 0) body = "[ ] " + body.substr(4);
            else if (body.rfind("[ ] ", 0) == 0) body = "[x] " + body.substr(4);
            s = (disabled ? "!" : "") + body;
        }
        std::string ItemLabel(const std::vector<std::string>& items, int i)
        {
            return i >= 0 && i < (int)items.size() ? ParseMenuItem(items[(size_t)i]).Label : std::string();
        }

        // ---------------------------------------------------------------- sekmeler
        std::vector<float> TabWidths(const std::vector<std::string>& titles, float fs, bool closeButtons)
        {
            std::vector<float> w;
            for (const auto& t : titles)
                w.push_back(UIDraw::TextWidth(t, fs) + 36.0f + (closeButtons ? 22.0f : 0.0f));
            return w;
        }
        int TabAt(const std::vector<float>& widths, float x)
        {
            float cx = 0.0f;
            for (int i = 0; i < (int)widths.size(); ++i)
            {
                if (x >= cx && x < cx + widths[(size_t)i])
                    return i;
                cx += widths[(size_t)i] + 2.0f;
            }
            return -1;
        }
        void DrawTabs(const glm::mat4& m, const std::vector<std::string>& titles, int current, int hovered, float fs, float y0, float y1, bool closeButtons)
        {
            const std::vector<float> widths = TabWidths(titles, fs, closeButtons);
            float x = 0.0f;
            for (int i = 0; i < (int)titles.size(); ++i)
            {
                const float w = widths[(size_t)i];
                const glm::vec4 bg = i == current ? glm::vec4(1.0f) : i == hovered ? glm::vec4(0.93f, 0.94f, 0.97f, 1.0f) : glm::vec4(0.84f, 0.85f, 0.89f, 1.0f);
                UIDraw::RoundRect(m, { x, y0 }, { x + w, y1 }, 6.0f, bg);
                if (i == current)
                    UIDraw::Rect(m, { x + 4.0f, y1 - 3.0f }, { x + w - 4.0f, y1 }, kAccent);
                UIDraw::Text(m, { x + 18.0f, y0 }, { x + w - (closeButtons ? 22.0f : 0.0f), y1 }, titles[(size_t)i], fs, i == current ? kText : kMuted, 0, 1);
                if (closeButtons)
                    UIDraw::Text(m, { x + w - 24.0f, y0 }, { x + w - 4.0f, y1 }, "x", fs * 0.9f, kMuted, 1, 1);
                x += w + 2.0f;
            }
        }
        std::vector<std::string> ChildTitles(Entity e)
        {
            std::vector<std::string> titles;
            Scene* scene = e.GetScene();
            for (auto c : e.GetChildren())
                if (c.HasComponent<RectTransformComponent>() && (!scene || scene->IsEntityActive((entt::entity)c)))
                    titles.push_back(c.HasComponent<TagComponent>() ? c.GetComponent<TagComponent>().Tag : std::string("Tab"));
            return titles;
        }

        // ---------------------------------------------------------------- listeler
        float ListRowHeight(float fs, float custom) { return custom > 0.0f ? custom : std::max(26.0f, fs * 1.7f); }

        void DrawScrollThumb(const glm::mat4& m, glm::vec2 size, float content, float offset)
        {
            if (content <= size.y + 0.5f)
                return;
            const float frac = size.y / content;
            const float barH = std::max(24.0f, size.y * frac);
            const float t = offset / std::max(content - size.y, 1.0f);
            const float top = size.y - (size.y - barH) * t;
            UIDraw::RoundRect(m, { size.x - 8.0f, top - barH }, { size.x - 3.0f, top }, 2.5f, glm::vec4(0, 0, 0, 0.3f));
        }

        // Tree: görünen öğeler (katlanmış atalar gizler)
        std::vector<int> VisibleTreeItems(const UITreeComponent& t)
        {
            std::vector<int> visible;
            int collapsedDepth = -1;
            for (int i = 0; i < (int)t.Items.size(); ++i)
            {
                const auto& it = t.Items[(size_t)i];
                if (collapsedDepth >= 0 && it.Depth > collapsedDepth)
                    continue;
                collapsedDepth = -1;
                if (!(t.HideRoot && i == 0))
                    visible.push_back(i);
                const bool hasChildren = i + 1 < (int)t.Items.size() && t.Items[(size_t)i + 1].Depth > it.Depth;
                if (it.Collapsed && hasChildren)
                    collapsedDepth = it.Depth;
            }
            return visible;
        }
        bool TreeHasChildren(const UITreeComponent& t, int i) { return i + 1 < (int)t.Items.size() && t.Items[(size_t)i + 1].Depth > t.Items[(size_t)i].Depth; }

        // ---------------------------------------------------------------- dosya diyaloğu
        fs::path DialogRoot(const UIFileDialogComponent& d)
        {
            fs::path root;
            switch (d.Access)
            {
                case 1: root = Project::GetActive() ? Project::GetActiveProjectDirectory() : fs::current_path(); break;
                case 2:
                {
                    const char* home = std::getenv("HOME");
#if defined(_WIN32)
                    if (!home) home = std::getenv("USERPROFILE");
#endif
                    root = home ? fs::path(home) : fs::current_path();
                    break;
                }
                default: root = PlayerPrefs::GetPersistentDataPath(); break;
            }
            if (!d.RootSubfolder.empty())
                root /= d.RootSubfolder;
            std::error_code ec;
            fs::create_directories(root, ec);
            return root;
        }
        std::vector<std::string> FilterExtensions(const std::string& filters)
        {
            std::vector<std::string> exts;
            std::string token;
            std::stringstream ss(filters);
            while (std::getline(ss, token, ','))
            {
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                if (token.rfind("*.", 0) == 0) token = token.substr(1);
                if (!token.empty() && token != "*" && token != ".*")
                {
                    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    exts.push_back(token);
                }
            }
            return exts;
        }
        void RefreshFileDialog(UIFileDialogComponent& d)
        {
            d.NeedsRefresh = false;
            d.Entries.clear();
            d.Selected.clear();
            d.ScrollOffset = 0.0f;
            const fs::path dir = DialogRoot(d) / d.CurrentDir;
            const std::vector<std::string> exts = FilterExtensions(d.Filters);
            std::vector<std::string> dirs, files;
            std::error_code ec;
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string name = it->path().filename().string();
                if (name.empty() || name[0] == '.')
                    continue;
                if (it->is_directory(ec))
                    dirs.push_back("/" + name);
                else if (d.FileMode != 2)
                {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    if (exts.empty() || std::find(exts.begin(), exts.end(), ext) != exts.end())
                        files.push_back(name);
                }
            }
            std::sort(dirs.begin(), dirs.end());
            std::sort(files.begin(), files.end());
            d.Entries = dirs;
            d.Entries.insert(d.Entries.end(), files.begin(), files.end());
        }
        struct FileDialogGeom { glm::vec2 PanelMin, PanelMax, ListMin, ListMax, NameMin, NameMax, UpMin, UpMax, OkMin, OkMax, CancelMin, CancelMax, PathMin, PathMax; float RowH; };
        FileDialogGeom LayoutFileDialog(const UIFileDialogComponent& d, glm::vec2 size)
        {
            FileDialogGeom g;
            const glm::vec2 ps = glm::min(d.Size, size - glm::vec2(20.0f));
            g.PanelMin = (size - ps) * 0.5f;
            g.PanelMax = g.PanelMin + ps;
            g.RowH = ListRowHeight(d.FontSize, 0.0f);
            const float pad = 14.0f, title = 44.0f, bar = 40.0f;
            g.UpMin = { g.PanelMin.x + pad, g.PanelMax.y - title - bar };
            g.UpMax = { g.PanelMin.x + pad + 40.0f, g.PanelMax.y - title - 4.0f };
            g.PathMin = { g.UpMax.x + 8.0f, g.UpMin.y };
            g.PathMax = { g.PanelMax.x - pad, g.UpMax.y };
            g.OkMin = { g.PanelMax.x - pad - 220.0f, g.PanelMin.y + pad };
            g.OkMax = { g.PanelMax.x - pad - 112.0f, g.PanelMin.y + pad + 40.0f };
            g.CancelMin = { g.PanelMax.x - pad - 104.0f, g.OkMin.y };
            g.CancelMax = { g.PanelMax.x - pad, g.OkMax.y };
            g.NameMin = { g.PanelMin.x + pad, g.OkMax.y + 10.0f };
            g.NameMax = { g.PanelMax.x - pad, g.OkMax.y + 50.0f };
            g.ListMin = { g.PanelMin.x + pad, g.NameMax.y + 10.0f };
            g.ListMax = { g.PanelMax.x - pad, g.UpMin.y - 8.0f };
            return g;
        }
        bool In(glm::vec2 p, glm::vec2 a, glm::vec2 b) { return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y; }
        void DrawButton(const glm::mat4& m, glm::vec2 a, glm::vec2 b, const std::string& label, float fs, bool hovered, bool primary)
        {
            UIDraw::RoundRect(m, a, b, 8.0f, primary ? (hovered ? glm::vec4(0.36f, 0.66f, 1.0f, 1.0f) : kAccent) : (hovered ? glm::vec4(0.88f, 0.89f, 0.93f, 1.0f) : glm::vec4(0.93f, 0.94f, 0.96f, 1.0f)));
            UIDraw::Text(m, a, b, label, fs, primary ? glm::vec4(1.0f) : kText, 1, 1);
        }
        void AcceptFileDialog(UIFileDialogComponent& d, Entity e)
        {
            const fs::path dir = DialogRoot(d) / d.CurrentDir;
            std::vector<std::string> picked;
            if (d.FileMode == 2)
                picked.push_back(dir.lexically_normal().string());
            else if (d.FileMode == 3)
            {
                if (d.FileName.Text.empty())
                    return;
                picked.push_back((dir / d.FileName.Text).lexically_normal().string());
            }
            else
                for (int i : d.Selected)
                    if (i >= 0 && i < (int)d.Entries.size() && d.Entries[(size_t)i][0] != '/')
                        picked.push_back((dir / d.Entries[(size_t)i]).lexically_normal().string());
            if (picked.empty())
                return;
            d.LastSelected.clear();
            for (size_t i = 0; i < picked.size(); ++i)
                d.LastSelected += (i ? ";" : "") + picked[i];
            d.Visible = false;
            d.FileName.Active = false;
            const char* signal = d.FileMode == 2 ? "dir_selected" : d.FileMode == 1 ? "files_selected" : "file_selected";
            EmitEvent(e, d.OnFileSelected, "FileSelected", (float)picked.size(), signal, "OnFileSelected", { CQS::Value::MakeString(picked.front()) });
        }

        // ---------------------------------------------------------------- metin düzenleyici
        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::string cur;
            for (char c : text)
            {
                if (c == '\n') { lines.push_back(cur); cur.clear(); }
                else if (c != '\r') cur += c;
            }
            lines.push_back(cur);
            return lines;
        }
        std::string JoinLines(const std::vector<std::string>& lines)
        {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
                out += (i ? "\n" : "") + lines[i];
            return out;
        }
        float GutterWidth(const UITextEditComponent& t, int lineCount)
        {
            if (!t.CodeEditor || !t.LineNumbers)
                return 6.0f;
            return UIDraw::TextWidth(std::to_string(std::max(1, lineCount)), t.FontSize * 0.9f) + 20.0f;
        }
        // Sözdizimi renkleri: anahtar sözcük, sayı, metin, yorum
        struct Span { std::string Text; glm::vec4 Color; };
        std::vector<Span> Highlight(const UITextEditComponent& t, const std::string& line)
        {
            const glm::vec4 base{ 0.86f, 0.87f, 0.9f, 1.0f }, kw{ 0.98f, 0.55f, 0.66f, 1.0f }, num{ 0.70f, 0.85f, 0.55f, 1.0f },
                            str{ 1.0f, 0.83f, 0.50f, 1.0f }, com{ 0.50f, 0.54f, 0.60f, 1.0f };
            std::vector<Span> spans;
            if (!t.CodeEditor || !t.HighlightSyntax)
            {
                spans.push_back({ line, t.CodeEditor ? base : kText });
                return spans;
            }
            std::vector<std::string> keywords;
            {
                std::stringstream ss(t.Keywords);
                std::string w;
                while (ss >> w) keywords.push_back(w);
            }
            size_t i = 0;
            while (i < line.size())
            {
                const char c = line[i];
                if ((c == '/' && i + 1 < line.size() && line[i + 1] == '/') || c == '#')
                {
                    spans.push_back({ line.substr(i), com });
                    break;
                }
                if (c == '"' || c == '\'')
                {
                    size_t j = i + 1;
                    while (j < line.size() && line[j] != c) { if (line[j] == '\\') ++j; ++j; }
                    j = std::min(j + 1, line.size());
                    spans.push_back({ line.substr(i, j - i), str });
                    i = j;
                    continue;
                }
                if (std::isdigit((unsigned char)c))
                {
                    size_t j = i;
                    while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '.')) ++j;
                    spans.push_back({ line.substr(i, j - i), num });
                    i = j;
                    continue;
                }
                if (std::isalpha((unsigned char)c) || c == '_')
                {
                    size_t j = i;
                    while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
                    const std::string word = line.substr(i, j - i);
                    spans.push_back({ word, std::find(keywords.begin(), keywords.end(), word) != keywords.end() ? kw : base });
                    i = j;
                    continue;
                }
                size_t j = i;
                while (j < line.size() && !std::isalnum((unsigned char)line[j]) && line[j] != '_' && line[j] != '"' && line[j] != '\'' && line[j] != '#' &&
                       !(line[j] == '/' && j + 1 < line.size() && line[j + 1] == '/'))
                    ++j;
                if (j == i) ++j;
                spans.push_back({ line.substr(i, j - i), base });
                i = j;
            }
            return spans;
        }

        // ---------------------------------------------------------------- Graph Edit geometrisi
        struct NodeView { Entity E; UIGraphNodeComponent* Node; glm::vec2 Size; };
        std::vector<NodeView> GraphNodes(Entity graph)
        {
            std::vector<NodeView> nodes;
            for (auto c : graph.GetChildren())
                if (c.HasComponent<UIGraphNodeComponent>() && c.HasComponent<RectTransformComponent>())
                    nodes.push_back({ c, &c.GetComponent<UIGraphNodeComponent>(), glm::max(c.GetComponent<RectTransformComponent>().SizeDelta, glm::vec2(40.0f)) });
            return nodes;
        }
        constexpr float kNodeTitle = 32.0f, kPortGap = 28.0f, kPortRadius = 7.0f;
        // İçerik koordinatı (sol üst köken, aşağı +) -> Graph Edit yereli
        glm::vec2 ContentToLocal(const UIGraphEditComponent& g, glm::vec2 graphSize, glm::vec2 c)
        {
            return { g.ScrollOffset.x + c.x * g.Zoom, graphSize.y - g.ScrollOffset.y - c.y * g.Zoom };
        }
        glm::vec2 LocalToContent(const UIGraphEditComponent& g, glm::vec2 graphSize, glm::vec2 l)
        {
            return { (l.x - g.ScrollOffset.x) / std::max(g.Zoom, 0.01f), (graphSize.y - g.ScrollOffset.y - l.y) / std::max(g.Zoom, 0.01f) };
        }
        // Ucun içerik koordinatı (giriş solda, çıkış sağda)
        glm::vec2 PortContent(const NodeView& n, bool output, int port)
        {
            return { n.Node->PositionOffset.x + (output ? n.Size.x : 0.0f), n.Node->PositionOffset.y + kNodeTitle + 16.0f + port * kPortGap };
        }
        uint64_t Uuid(Entity e) { return (uint64_t)e.GetComponent<IDComponent>().ID; }
        glm::vec2 GraphSize(Entity graph)
        {
            return graph.HasComponent<RectTransformComponent>() ? glm::vec2(graph.GetComponent<RectTransformComponent>().ComputedRect.z, graph.GetComponent<RectTransformComponent>().ComputedRect.w) : glm::vec2(0.0f);
        }
        // Düğüm yereli (ölçekli) -> içerik
        glm::vec2 NodeLocalToContent(const NodeView& n, glm::vec2 local) { return { n.Node->PositionOffset.x + local.x, n.Node->PositionOffset.y + (n.Size.y - local.y) }; }
        void DrawBezier(const glm::mat4& m, glm::vec2 a, glm::vec2 b, float thickness, const glm::vec4& color)
        {
            const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
            const glm::vec2 c1 = a + glm::vec2(dx, 0.0f), c2 = b - glm::vec2(dx, 0.0f);
            glm::vec2 prev = a;
            for (int i = 1; i <= 24; ++i)
            {
                const float t = i / 24.0f, u = 1.0f - t;
                const glm::vec2 p = u * u * u * a + 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t * b;
                UIDraw::Line(m, prev, p, thickness, color);
                prev = p;
            }
        }

        void OpenUri(const std::string& uri)
        {
            if (uri.empty())
                return;
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    #if defined(__APPLE__)
            const char* tool = "open";
    #else
            const char* tool = "xdg-open";
    #endif
            // Kabuk yok: adres tek argüman (enjeksiyon olmaz)
            pid_t pid = 0;
            char* argv[] = { const_cast<char*>(tool), const_cast<char*>(uri.c_str()), nullptr };
            if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) == 0)
            {
                int status = 0;
                waitpid(pid, &status, WNOHANG);
            }
#endif
        }

        void SetSpinValue(Entity e, UISpinBoxComponent& sb, float v)
        {
            if (sb.Step > 0.0f)
                v = sb.MinValue + std::round((v - sb.MinValue) / sb.Step) * sb.Step;
            if (!sb.AllowLesser) v = std::max(v, sb.MinValue);
            if (!sb.AllowGreater) v = std::min(v, sb.MaxValue);
            if (std::fabs(v - sb.Value) > 1e-6f)
            {
                sb.Value = v;
                sb.ChangedThisFrame = true;
                EmitEvent(e, sb.OnValueChanged, "ValueChanged", v, "value_changed", "OnValueChanged", { CQS::Value::MakeFloat(v) });
            }
        }
        std::string SpinText(const UISpinBoxComponent& sb)
        {
            char buf[64];
            int decimals = 0;
            for (float s = sb.Step; decimals < 4 && s > 0.0f && std::fabs(s - std::round(s)) > 1e-4f; s *= 10.0f) ++decimals;
            std::snprintf(buf, sizeof(buf), "%.*f", decimals, sb.Value);
            return buf;
        }
        void CommitSpinEdit(Entity e, UISpinBoxComponent& sb)
        {
            if (!sb.Edit.Active)
                return;
            sb.Edit.Active = false;
            std::string t = sb.Edit.Text;
            std::replace(t.begin(), t.end(), ',', '.');
            char* end = nullptr;
            const float v = std::strtof(t.c_str(), &end);
            if (end != t.c_str())
                SetSpinValue(e, sb, v);
        }

        void SelectListItem(Entity e, UIItemListComponent& l, int i, bool toggle)
        {
            if (i < 0 || i >= (int)l.Items.size() || l.Items[(size_t)i].Disabled)
                return;
            const bool was = std::find(l.Selected.begin(), l.Selected.end(), i) != l.Selected.end();
            if (l.SelectMode == 1 && toggle)
            {
                if (was) l.Selected.erase(std::remove(l.Selected.begin(), l.Selected.end(), i), l.Selected.end());
                else l.Selected.push_back(i);
                l.ChangedThisFrame = true;
                EmitEvent(e, l.OnItemSelected, "ItemSelected", (float)i, "multi_selected", "OnMultiSelected", { CQS::Value::MakeInt(i), CQS::Value::MakeBool(!was) });
                return;
            }
            if (was && l.Selected.size() == 1 && !l.AllowReselect)
                return;
            l.Selected = { i };
            l.ChangedThisFrame = true;
            EmitEvent(e, l.OnItemSelected, "ItemSelected", (float)i, "item_selected", "OnItemSelected", { CQS::Value::MakeInt(i) });
        }
    }

    namespace UIControls
    {
        Entity FindControl(Entity e)
        {
            for (; e; e = e.GetParent())
            {
                if (e.HasComponent<UISpinBoxComponent>() || e.HasComponent<UIColorPickerComponent>() || e.HasComponent<UIColorPickerButtonComponent>() ||
                    e.HasComponent<UIMenuButtonComponent>() || e.HasComponent<UIMenuBarComponent>() || e.HasComponent<UITabContainerComponent>() ||
                    e.HasComponent<UITabBarComponent>() || e.HasComponent<UISplitContainerComponent>() || e.HasComponent<UIItemListComponent>() ||
                    e.HasComponent<UITreeComponent>() || e.HasComponent<UILinkButtonComponent>() || e.HasComponent<UITextEditComponent>() ||
                    e.HasComponent<UIGraphEditComponent>() || e.HasComponent<UIGraphNodeComponent>())
                    return e;
            }
            return {};
        }

        UISelectableState* GetSelectable(Entity e)
        {
            if (e.HasComponent<UISpinBoxComponent>()) return &e.GetComponent<UISpinBoxComponent>().Selectable;
            if (e.HasComponent<UIColorPickerComponent>()) return &e.GetComponent<UIColorPickerComponent>().Selectable;
            if (e.HasComponent<UIColorPickerButtonComponent>()) return &e.GetComponent<UIColorPickerButtonComponent>().Selectable;
            if (e.HasComponent<UIMenuButtonComponent>()) return &e.GetComponent<UIMenuButtonComponent>().Selectable;
            if (e.HasComponent<UILinkButtonComponent>()) return &e.GetComponent<UILinkButtonComponent>().Selectable;
            if (e.HasComponent<UITextEditComponent>()) return &e.GetComponent<UITextEditComponent>().Selectable;
            return nullptr;
        }

        bool IsRaycastTarget(Entity e)
        {
            return GetSelectable(e) || e.HasComponent<UIMenuBarComponent>() || e.HasComponent<UITabContainerComponent>() || e.HasComponent<UITabBarComponent>() ||
                   e.HasComponent<UISplitContainerComponent>() || e.HasComponent<UIItemListComponent>() || e.HasComponent<UITreeComponent>() ||
                   e.HasComponent<UIGraphEditComponent>() || e.HasComponent<UIGraphNodeComponent>() || e.HasComponent<UIProgressBarComponent>();
        }

        bool HasOverlay(Entity e)
        {
            return (e.HasComponent<UIMenuButtonComponent>() && e.GetComponent<UIMenuButtonComponent>().Open) ||
                   (e.HasComponent<UIPopupMenuComponent>() && e.GetComponent<UIPopupMenuComponent>().Visible) ||
                   (e.HasComponent<UIMenuBarComponent>() && e.GetComponent<UIMenuBarComponent>().OpenMenu >= 0) ||
                   (e.HasComponent<UIColorPickerButtonComponent>() && e.GetComponent<UIColorPickerButtonComponent>().Open) ||
                   (e.HasComponent<UIDialogComponent>() && e.GetComponent<UIDialogComponent>().Visible) ||
                   (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible);
        }

        // ================================================================ çizim
        void Draw(Entity e, const glm::mat4& m, glm::vec2 size, const glm::vec4& tint)
        {
            // Progress Bar
            if (e.HasComponent<UIProgressBarComponent>())
            {
                auto& p = e.GetComponent<UIProgressBarComponent>();
                const float radius = std::min(8.0f, std::min(size.x, size.y) * 0.5f);
                UIDraw::RoundRect(m, { 0, 0 }, size, radius, p.BackgroundColor);
                const float t = p.MaxValue > p.MinValue ? std::clamp((p.Value - p.MinValue) / (p.MaxValue - p.MinValue), 0.0f, 1.0f) : 0.0f;
                glm::vec2 a(0.0f), b = size;
                if (p.Indeterminate)
                {
                    const float w = size.x * 0.3f, x = (size.x + w) * (0.5f + 0.5f * std::sin(p.Phase * 2.2f)) - w;
                    a.x = std::max(0.0f, x);
                    b.x = std::min(size.x, x + w);
                }
                else switch (p.FillMode)
                {
                    case 1: a.x = size.x * (1.0f - t); break;
                    case 2: a.y = size.y * (1.0f - t); break;
                    case 3: b.y = size.y * t; break;
                    default: b.x = size.x * t; break;
                }
                if (b.x - a.x > 0.5f && b.y - a.y > 0.5f)
                    UIDraw::RoundRect(m, a, b, radius, p.FillColor);
                if (p.ShowPercentage && !p.Indeterminate)
                {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(t * 100.0f));
                    UIDraw::Text(m, { 0, 0 }, size, buf, p.FontSize, kText, 1, 1);
                }
            }
            // Link Button
            if (e.HasComponent<UILinkButtonComponent>())
            {
                auto& l = e.GetComponent<UILinkButtonComponent>();
                const glm::vec4 color = l.Selectable.Hovered ? glm::mix(l.Color, glm::vec4(1.0f), 0.25f) : l.Color;
                UIDraw::Text(m, { 0, 0 }, size, l.Text, l.FontSize, color * glm::vec4(1, 1, 1, l.Selectable.Interactable ? 1.0f : 0.5f), 0, 1);
                if (l.Underline == 0 || (l.Underline == 1 && l.Selectable.Hovered))
                {
                    const float w = std::min(size.x, UIDraw::TextWidth(l.Text, l.FontSize));
                    const float y = size.y * 0.5f - l.FontSize * 0.5f;
                    UIDraw::Rect(m, { 0.0f, y }, { w, y + 1.5f }, color);
                }
            }
            // Spin Box
            if (e.HasComponent<UISpinBoxComponent>())
            {
                auto& sb = e.GetComponent<UISpinBoxComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, (sb.Edit.Active ? kAccent : kBorder) * tint);
                UIDraw::RoundRect(m, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, glm::vec4(1.0f) * tint);
                const float aw = std::min(30.0f, size.x * 0.3f);
                if (sb.Edit.Active)
                    DrawLineEdit(m, { 10.0f, 0.0f }, { size.x - aw - 4.0f, size.y }, sb.Edit, sb.FontSize, kText);
                else
                    UIDraw::Text(m, { 10.0f, 0.0f }, { size.x - aw - 4.0f, size.y }, sb.Prefix + (sb.Prefix.empty() ? "" : " ") + SpinText(sb) +
                                 (sb.Suffix.empty() ? "" : " ") + sb.Suffix, sb.FontSize, kText, 0, 1);
                const float ax = size.x - aw * 0.5f, a = std::min(6.0f, aw * 0.25f);
                UIDraw::Rect(m, { size.x - aw, size.y * 0.5f - 0.5f }, { size.x - 2.0f, size.y * 0.5f + 0.5f }, kBorder);
                UIDraw::Triangle(m, { ax - a, size.y * 0.75f - a * 0.5f }, { ax + a, size.y * 0.75f - a * 0.5f }, { ax, size.y * 0.75f + a * 0.6f },
                                 sb.HeldArrow == 1 ? kAccent : kMuted);
                UIDraw::Triangle(m, { ax - a, size.y * 0.25f + a * 0.5f }, { ax, size.y * 0.25f - a * 0.6f }, { ax + a, size.y * 0.25f + a * 0.5f },
                                 sb.HeldArrow == -1 ? kAccent : kMuted);
            }
            // Color Picker
            if (e.HasComponent<UIColorPickerComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, glm::vec4(0.97f, 0.97f, 0.98f, 1.0f));
                DrawPicker(m, { 0, 0 }, size, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, c.ShowHex });
            }
            // Color Picker Button
            if (e.HasComponent<UIColorPickerButtonComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerButtonComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, kBorder * tint);
                UIDraw::RoundRect(m, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, glm::vec4(1.0f) * tint);
                const glm::vec2 a(6.0f), b = size - glm::vec2(6.0f);
                UIDraw::Rect(m, a, { (a.x + b.x) * 0.5f, b.y }, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
                UIDraw::Rect(m, { (a.x + b.x) * 0.5f, a.y }, b, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));
                UIDraw::RoundRect(m, a, b, 4.0f, c.Color);
            }
            // Menu Button
            if (e.HasComponent<UIMenuButtonComponent>())
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                if (!mb.Flat || mb.Open || mb.Selectable.Hovered)
                    UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, (mb.Open ? kSelect : mb.Selectable.Hovered ? kHover : glm::vec4(0.93f, 0.94f, 0.96f, 1.0f)) * tint);
                UIDraw::Text(m, { 0, 0 }, size, mb.Text, mb.FontSize, kText, 1, 1);
            }
            // Menu Bar
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                UIDraw::Rect(m, { 0, 0 }, size, glm::vec4(0.95f, 0.95f, 0.97f, 1.0f));
                float x = 4.0f;
                for (int i = 0; i < (int)bar.Menus.size(); ++i)
                {
                    const float w = UIDraw::TextWidth(bar.Menus[(size_t)i].Title, bar.FontSize) + 24.0f;
                    if (i == bar.OpenMenu || i == bar.Hovered)
                        UIDraw::RoundRect(m, { x, 3.0f }, { x + w, size.y - 3.0f }, 6.0f, i == bar.OpenMenu ? kSelect : kHover);
                    UIDraw::Text(m, { x, 0.0f }, { x + w, size.y }, bar.Menus[(size_t)i].Title, bar.FontSize, kText, 1, 1);
                    x += w + 2.0f;
                }
            }
            // Tab Container
            if (e.HasComponent<UITabContainerComponent>())
            {
                auto& tc = e.GetComponent<UITabContainerComponent>();
                const float strip = tc.TabsVisible ? tc.TabHeight : 0.0f;
                UIDraw::RoundRect(m, { 0, 0 }, { size.x, size.y - strip }, 8.0f, glm::vec4(1.0f));
                if (tc.TabsVisible)
                    DrawTabs(m, ChildTitles(e), tc.CurrentTab, tc.HoveredTab, tc.FontSize, size.y - strip, size.y, false);
            }
            // Tab Bar
            if (e.HasComponent<UITabBarComponent>())
            {
                auto& tb = e.GetComponent<UITabBarComponent>();
                DrawTabs(m, tb.Tabs, tb.CurrentTab, tb.HoveredTab, tb.FontSize, 0.0f, size.y, tb.CloseButtons);
            }
            // Split Container: tutamaç
            if (e.HasComponent<UISplitContainerComponent>())
            {
                auto& sp = e.GetComponent<UISplitContainerComponent>();
                if (sp.DraggerVisible && !sp.Collapsed)
                {
                    const float total = (sp.Vertical ? size.y : size.x) - sp.DraggerThickness;
                    const float a = std::clamp(sp.SplitRatio * total, std::min(sp.MinPaneSize, total * 0.5f), std::max(total - sp.MinPaneSize, total * 0.5f));
                    const glm::vec4 col = sp.Dragging ? kAccent : glm::vec4(0.6f, 0.62f, 0.67f, 1.0f);
                    if (sp.Vertical)
                    {
                        const float y = size.y - a - sp.DraggerThickness * 0.5f;
                        for (int k = -1; k <= 1; ++k)
                            UIDraw::RoundRect(m, { size.x * 0.5f + k * 9.0f - 2.0f, y - 2.0f }, { size.x * 0.5f + k * 9.0f + 2.0f, y + 2.0f }, 2.0f, col);
                    }
                    else
                    {
                        const float x = a + sp.DraggerThickness * 0.5f;
                        for (int k = -1; k <= 1; ++k)
                            UIDraw::RoundRect(m, { x - 2.0f, size.y * 0.5f + k * 9.0f - 2.0f }, { x + 2.0f, size.y * 0.5f + k * 9.0f + 2.0f }, 2.0f, col);
                    }
                }
            }
            // Item List
            if (e.HasComponent<UIItemListComponent>())
            {
                auto& l = e.GetComponent<UIItemListComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, kBorder);
                UIDraw::RoundRect(m, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, glm::vec4(1.0f));
                const float rowH = ListRowHeight(l.FontSize, l.RowHeight);
                const int cols = std::max(1, l.MaxColumns);
                const float colW = (size.x - 8.0f) / cols;
                const int rows = ((int)l.Items.size() + cols - 1) / cols;
                UIDraw::PushClip(m, { 2, 2 }, size - glm::vec2(2.0f));
                for (int i = 0; i < (int)l.Items.size(); ++i)
                {
                    const int r = i / cols, c = i % cols;
                    const float top = size.y - 4.0f - r * rowH + l.ScrollOffset;
                    if (top < 0.0f || top - rowH > size.y)
                        continue;
                    const glm::vec2 a(4.0f + c * colW, top - rowH), b(4.0f + (c + 1) * colW - 2.0f, top);
                    const bool sel = std::find(l.Selected.begin(), l.Selected.end(), i) != l.Selected.end();
                    if (sel) UIDraw::RoundRect(m, a, b, 5.0f, kSelect);
                    else if (i == l.Hovered) UIDraw::RoundRect(m, a, b, 5.0f, kHover);
                    float tx = a.x + 8.0f;
                    if (!l.Items[(size_t)i].Icon.empty())
                    {
                        UIDraw::Image(m, { a.x + 4.0f, a.y + 4.0f }, { a.x + rowH - 4.0f, b.y - 4.0f }, l.Items[(size_t)i].Icon, glm::vec4(1.0f));
                        tx = a.x + rowH + 2.0f;
                    }
                    UIDraw::Text(m, { tx, a.y }, b, l.Items[(size_t)i].Text, l.FontSize, l.Items[(size_t)i].Disabled ? kMuted : kText, 0, 1);
                }
                UIDraw::PopClip();
                DrawScrollThumb(m, size, rows * rowH + 8.0f, l.ScrollOffset);
            }
            // Tree
            if (e.HasComponent<UITreeComponent>())
            {
                auto& t = e.GetComponent<UITreeComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, kBorder);
                UIDraw::RoundRect(m, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, glm::vec4(1.0f));
                const float rowH = ListRowHeight(t.FontSize, 0.0f);
                const std::vector<int> visible = VisibleTreeItems(t);
                const int depthBase = t.HideRoot ? 1 : 0;
                UIDraw::PushClip(m, { 2, 2 }, size - glm::vec2(2.0f));
                for (int r = 0; r < (int)visible.size(); ++r)
                {
                    const int i = visible[(size_t)r];
                    const auto& it = t.Items[(size_t)i];
                    const float top = size.y - 4.0f - r * rowH + t.ScrollOffset;
                    if (top < 0.0f || top - rowH > size.y)
                        continue;
                    const glm::vec2 a(4.0f, top - rowH), b(size.x - 4.0f, top);
                    if (i == t.Selected) UIDraw::RoundRect(m, a, b, 5.0f, kSelect);
                    else if (i == t.Hovered) UIDraw::RoundRect(m, a, b, 5.0f, kHover);
                    const float x = a.x + 6.0f + std::max(0, it.Depth - depthBase) * t.Indent;
                    const float cy = (a.y + b.y) * 0.5f;
                    if (TreeHasChildren(t, i))
                    {
                        if (it.Collapsed) UIDraw::Triangle(m, { x + 3.0f, cy - 5.0f }, { x + 3.0f, cy + 5.0f }, { x + 11.0f, cy }, kMuted);
                        else UIDraw::Triangle(m, { x + 1.0f, cy + 4.0f }, { x + 11.0f, cy + 4.0f }, { x + 6.0f, cy - 4.0f }, kMuted);
                    }
                    UIDraw::Text(m, { x + 18.0f, a.y }, b, it.Text, t.FontSize, kText, 0, 1);
                }
                UIDraw::PopClip();
                DrawScrollThumb(m, size, visible.size() * rowH + 8.0f, t.ScrollOffset);
            }
            // Text Edit / Code Edit
            if (e.HasComponent<UITextEditComponent>())
            {
                auto& t = e.GetComponent<UITextEditComponent>();
                const glm::vec4 bg = t.CodeEditor ? glm::vec4(0.13f, 0.14f, 0.17f, 1.0f) : glm::vec4(1.0f);
                UIDraw::RoundRect(m, { 0, 0 }, size, 8.0f, t.Focused ? kAccent : (t.CodeEditor ? glm::vec4(0.25f, 0.27f, 0.32f, 1.0f) : kBorder));
                UIDraw::RoundRect(m, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, bg);
                const std::vector<std::string> lines = SplitLines(t.Text);
                const float lineH = UIDraw::LineHeight(t.FontSize);
                const float gutter = GutterWidth(t, (int)lines.size());
                UIDraw::PushClip(m, { 2, 2 }, size - glm::vec2(2.0f));
                if (t.Text.empty() && !t.Focused && !t.Placeholder.empty())
                    UIDraw::Text(m, { gutter + 4.0f, size.y - 6.0f - lineH }, { size.x, size.y - 6.0f }, t.Placeholder, t.FontSize, kMuted, 0, 1);
                for (int i = 0; i < (int)lines.size(); ++i)
                {
                    const float top = size.y - 6.0f - i * lineH + t.ScrollY;
                    if (top < 0.0f || top - lineH > size.y)
                        continue;
                    if (t.CodeEditor && t.Focused && i == t.CaretLine)
                        UIDraw::Rect(m, { gutter, top - lineH }, { size.x - 2.0f, top }, glm::vec4(1.0f, 1.0f, 1.0f, 0.05f));
                    if (t.CodeEditor && t.LineNumbers)
                        UIDraw::Text(m, { 2.0f, top - lineH }, { gutter - 8.0f, top }, std::to_string(i + 1), t.FontSize * 0.9f,
                                     i == t.CaretLine ? glm::vec4(0.8f, 0.82f, 0.86f, 1.0f) : glm::vec4(0.45f, 0.48f, 0.54f, 1.0f), 2, 1);
                    float x = gutter + 4.0f - t.ScrollX;
                    for (const Span& s : Highlight(t, lines[(size_t)i]))
                    {
                        const float w = UIDraw::TextWidth(s.Text, t.FontSize);
                        if (x + w > gutter && x < size.x)
                            UIDraw::Text(m, { x, top - lineH }, { x + w + 4.0f, top }, s.Text, t.FontSize, s.Color, 0, 1);
                        x += w;
                    }
                    if (t.Focused && i == t.CaretLine && std::fmod(t.Blink, 1.0f) < 0.55f)
                    {
                        const float cx = gutter + 4.0f - t.ScrollX + UIDraw::CaretX(lines[(size_t)i], t.CaretColumn, t.FontSize);
                        UIDraw::Rect(m, { cx, top - lineH + 2.0f }, { cx + 2.0f, top - 2.0f }, t.CodeEditor ? glm::vec4(0.9f) : kText);
                    }
                }
                if (t.CodeEditor && t.LineNumbers)
                    UIDraw::Rect(m, { gutter - 4.0f, 2.0f }, { gutter - 3.0f, size.y - 2.0f }, glm::vec4(0.3f, 0.32f, 0.38f, 1.0f));
                UIDraw::PopClip();
                DrawScrollThumb(m, size, lines.size() * lineH + 12.0f, t.ScrollY);
            }
            // Graph Edit
            if (e.HasComponent<UIGraphEditComponent>())
            {
                auto& g = e.GetComponent<UIGraphEditComponent>();
                UIDraw::RoundRect(m, { 0, 0 }, size, 6.0f, glm::vec4(0.16f, 0.17f, 0.20f, 1.0f));
                UIDraw::PushClip(m, { 0, 0 }, size);
                if (g.ShowGrid)
                {
                    const float step = std::max(8.0f, 32.0f * g.Zoom);
                    const glm::vec4 line(1.0f, 1.0f, 1.0f, 0.05f);
                    float x0 = std::fmod(g.ScrollOffset.x, step);
                    if (x0 < 0.0f) x0 += step;
                    for (float x = x0; x < size.x; x += step)
                        UIDraw::Rect(m, { x, 0.0f }, { x + 1.0f, size.y }, line);
                    float y0 = std::fmod(size.y - g.ScrollOffset.y, step);
                    if (y0 < 0.0f) y0 += step;
                    for (float y = y0; y < size.y; y += step)
                        UIDraw::Rect(m, { 0.0f, y }, { size.x, y + 1.0f }, line);
                }
                // Bağlantılar
                const std::vector<NodeView> nodes = GraphNodes(e);
                auto findNode = [&](uint64_t id) -> const NodeView* {
                    for (const auto& n : nodes) if (Uuid(n.E) == id) return &n;
                    return nullptr;
                };
                for (const auto& c : g.Connections)
                {
                    const NodeView* a = findNode(c.From);
                    const NodeView* b = findNode(c.To);
                    if (!a || !b)
                        continue;
                    DrawBezier(m, ContentToLocal(g, size, PortContent(*a, true, c.FromPort)), ContentToLocal(g, size, PortContent(*b, false, c.ToPort)),
                               3.0f * g.Zoom, glm::vec4(0.85f, 0.87f, 0.92f, 0.9f));
                }
                if (g.DragFrom)
                    if (const NodeView* a = findNode(g.DragFrom))
                        DrawBezier(m, ContentToLocal(g, size, PortContent(*a, true, g.DragFromPort)), g.DragPoint, 3.0f * g.Zoom, kAccent);
                UIDraw::PopClip();
            }
            // Graph Node
            if (e.HasComponent<UIGraphNodeComponent>())
            {
                auto& n = e.GetComponent<UIGraphNodeComponent>();
                if (n.Selected)
                    UIDraw::RoundRect(m, { -3, -3 }, size + glm::vec2(3.0f), 9.0f, kAccent);
                UIDraw::RoundRect(m, { 0, 0 }, size, 7.0f, glm::vec4(0.24f, 0.25f, 0.29f, 1.0f));
                UIDraw::RoundRect(m, { 0, size.y - kNodeTitle }, size, 7.0f, n.TitleColor);
                UIDraw::Text(m, { 10.0f, size.y - kNodeTitle }, { size.x - 6.0f, size.y }, n.Title, 20.0f, glm::vec4(1.0f), 0, 1);
                for (int i = 0; i < n.InputPorts; ++i)
                {
                    const float y = size.y - kNodeTitle - 16.0f - i * kPortGap;
                    UIDraw::RoundRect(m, { -kPortRadius, y - kPortRadius }, { kPortRadius, y + kPortRadius }, kPortRadius, glm::vec4(0.45f, 0.85f, 0.55f, 1.0f));
                }
                for (int i = 0; i < n.OutputPorts; ++i)
                {
                    const float y = size.y - kNodeTitle - 16.0f - i * kPortGap;
                    UIDraw::RoundRect(m, { size.x - kPortRadius, y - kPortRadius }, { size.x + kPortRadius, y + kPortRadius }, kPortRadius, glm::vec4(0.98f, 0.70f, 0.35f, 1.0f));
                }
            }
        }

        // ================================================================ açılır katmanlar
        namespace
        {
            MenuGeom MenuBarGeom(const UIMenuBarComponent& bar, int menu, glm::vec2 size)
            {
                float x = 4.0f;
                for (int i = 0; i < menu && i < (int)bar.Menus.size(); ++i)
                    x += UIDraw::TextWidth(bar.Menus[(size_t)i].Title, bar.FontSize) + 26.0f;
                return LayoutMenu(bar.Menus[(size_t)menu].Items, bar.FontSize, { x, -2.0f }, 160.0f);
                (void)size;
            }
            constexpr float kPickerW = 280.0f, kPickerH = 320.0f;
            glm::vec2 DialogPanelMin(const UIDialogComponent& d, glm::vec2 size) { return (size - glm::min(d.Size, size - glm::vec2(20.0f))) * 0.5f; }
            glm::vec2 DialogPanelMax(const UIDialogComponent& d, glm::vec2 size) { return DialogPanelMin(d, size) + glm::min(d.Size, size - glm::vec2(20.0f)); }
            struct DialogButtons { glm::vec2 OkMin, OkMax, CancelMin, CancelMax, CloseMin, CloseMax; };
            DialogButtons LayoutDialog(const UIDialogComponent& d, glm::vec2 size)
            {
                const glm::vec2 a = DialogPanelMin(d, size), b = DialogPanelMax(d, size);
                DialogButtons k;
                const float bw = 120.0f, bh = 42.0f, pad = 16.0f;
                if (d.Kind == 1)
                {
                    k.OkMin = { (a.x + b.x) * 0.5f - bw - 6.0f, a.y + pad };
                    k.CancelMin = { (a.x + b.x) * 0.5f + 6.0f, a.y + pad };
                }
                else
                    k.OkMin = { (a.x + b.x - bw) * 0.5f, a.y + pad };
                k.OkMax = k.OkMin + glm::vec2(bw, bh);
                k.CancelMax = k.CancelMin + glm::vec2(bw, bh);
                k.CloseMin = { b.x - 40.0f, b.y - 40.0f };
                k.CloseMax = { b.x - 8.0f, b.y - 8.0f };
                return k;
            }
        }

        void DrawOverlay(Entity e, const glm::mat4& m, glm::vec2 size)
        {
            if (e.HasComponent<UIMenuButtonComponent>() && e.GetComponent<UIMenuButtonComponent>().Open)
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                DrawMenu(m, mb.Items, LayoutMenu(mb.Items, mb.FontSize, { 0.0f, -2.0f }, size.x), mb.Hovered, mb.FontSize);
            }
            if (e.HasComponent<UIPopupMenuComponent>() && e.GetComponent<UIPopupMenuComponent>().Visible)
            {
                auto& pm = e.GetComponent<UIPopupMenuComponent>();
                DrawMenu(m, pm.Items, LayoutMenu(pm.Items, pm.FontSize, pm.Position, 160.0f), pm.Hovered, pm.FontSize);
            }
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                if (bar.OpenMenu >= 0 && bar.OpenMenu < (int)bar.Menus.size())
                    DrawMenu(m, bar.Menus[(size_t)bar.OpenMenu].Items, MenuBarGeom(bar, bar.OpenMenu, size), bar.Hovered >= 1000 ? bar.Hovered - 1000 : -1, bar.FontSize);
            }
            if (e.HasComponent<UIColorPickerButtonComponent>() && e.GetComponent<UIColorPickerButtonComponent>().Open)
            {
                auto& c = e.GetComponent<UIColorPickerButtonComponent>();
                const glm::vec2 origin(0.0f, -kPickerH - 4.0f);
                UIDraw::RoundRect(m, origin - glm::vec2(1.0f, 3.0f), origin + glm::vec2(kPickerW + 1.0f, kPickerH + 1.0f), 8.0f, glm::vec4(0, 0, 0, 0.18f));
                UIDraw::RoundRect(m, origin, origin + glm::vec2(kPickerW, kPickerH), 8.0f, glm::vec4(0.98f, 0.98f, 0.99f, 1.0f));
                DrawPicker(m, origin, { kPickerW, kPickerH }, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, true });
            }
            if (e.HasComponent<UIDialogComponent>() && e.GetComponent<UIDialogComponent>().Visible)
            {
                auto& d = e.GetComponent<UIDialogComponent>();
                UIDraw::Rect(m, { 0, 0 }, size, glm::vec4(0, 0, 0, 0.35f));
                const glm::vec2 a = DialogPanelMin(d, size), b = DialogPanelMax(d, size);
                UIDraw::RoundRect(m, a - glm::vec2(2.0f, 5.0f), b + glm::vec2(2.0f), 12.0f, glm::vec4(0, 0, 0, 0.25f));
                UIDraw::RoundRect(m, a, b, 12.0f, glm::vec4(1.0f));
                UIDraw::Text(m, { a.x + 18.0f, b.y - 48.0f }, { b.x - 48.0f, b.y - 4.0f }, d.Title, d.FontSize * 1.05f, kText, 0, 1);
                const DialogButtons k = LayoutDialog(d, size);
                UIDraw::Text(m, k.CloseMin, k.CloseMax, "x", d.FontSize, d.Hovered == 2 ? kText : kMuted, 1, 1);
                UIDraw::Text(m, { a.x + 18.0f, k.OkMax.y + 8.0f }, { b.x - 18.0f, b.y - 52.0f }, d.Text, d.FontSize, kText, 0, 1, true);
                DrawButton(m, k.OkMin, k.OkMax, d.OkText, d.FontSize, d.Hovered == 0, true);
                if (d.Kind == 1)
                    DrawButton(m, k.CancelMin, k.CancelMax, d.CancelText, d.FontSize, d.Hovered == 1, false);
            }
            if (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible)
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                if (d.NeedsRefresh)
                    RefreshFileDialog(d);
                const FileDialogGeom g = LayoutFileDialog(d, size);
                UIDraw::Rect(m, { 0, 0 }, size, glm::vec4(0, 0, 0, 0.35f));
                UIDraw::RoundRect(m, g.PanelMin - glm::vec2(2.0f, 5.0f), g.PanelMax + glm::vec2(2.0f), 12.0f, glm::vec4(0, 0, 0, 0.25f));
                UIDraw::RoundRect(m, g.PanelMin, g.PanelMax, 12.0f, glm::vec4(1.0f));
                UIDraw::Text(m, { g.PanelMin.x + 18.0f, g.PanelMax.y - 44.0f }, { g.PanelMax.x - 18.0f, g.PanelMax.y - 4.0f }, d.Title, d.FontSize * 1.05f, kText, 0, 1);
                DrawButton(m, g.UpMin, g.UpMax, "..", d.FontSize, d.HoveredButton == 2, false);
                UIDraw::RoundRect(m, g.PathMin, g.PathMax, 6.0f, glm::vec4(0.95f, 0.95f, 0.97f, 1.0f));
                UIDraw::Text(m, { g.PathMin.x + 8.0f, g.PathMin.y }, g.PathMax, "/" + d.CurrentDir, d.FontSize * 0.9f, kMuted, 0, 1);
                UIDraw::RoundRect(m, g.ListMin, g.ListMax, 6.0f, kBorder);
                UIDraw::RoundRect(m, g.ListMin + glm::vec2(1.0f), g.ListMax - glm::vec2(1.0f), 5.0f, glm::vec4(1.0f));
                UIDraw::PushClip(m, g.ListMin + glm::vec2(1.0f), g.ListMax - glm::vec2(1.0f));
                for (int i = 0; i < (int)d.Entries.size(); ++i)
                {
                    const float top = g.ListMax.y - 4.0f - i * g.RowH + d.ScrollOffset;
                    if (top < g.ListMin.y || top - g.RowH > g.ListMax.y)
                        continue;
                    const glm::vec2 a(g.ListMin.x + 4.0f, top - g.RowH), b(g.ListMax.x - 4.0f, top);
                    const bool sel = std::find(d.Selected.begin(), d.Selected.end(), i) != d.Selected.end();
                    if (sel) UIDraw::RoundRect(m, a, b, 5.0f, kSelect);
                    else if (i == d.Hovered) UIDraw::RoundRect(m, a, b, 5.0f, kHover);
                    const bool dir = d.Entries[(size_t)i][0] == '/';
                    UIDraw::Text(m, { a.x + 10.0f, a.y }, b, dir ? d.Entries[(size_t)i].substr(1) + "/" : d.Entries[(size_t)i], d.FontSize, dir ? kAccent : kText, 0, 1);
                }
                UIDraw::PopClip();
                UIDraw::RoundRect(m, g.NameMin, g.NameMax, 6.0f, d.FileName.Active ? kAccent : kBorder);
                UIDraw::RoundRect(m, g.NameMin + glm::vec2(1.0f), g.NameMax - glm::vec2(1.0f), 5.0f, glm::vec4(1.0f));
                DrawLineEdit(m, { g.NameMin.x + 10.0f, g.NameMin.y }, g.NameMax, d.FileName, d.FontSize, kText);
                const char* ok = d.FileMode == 3 ? "Save" : d.FileMode == 2 ? "Select" : "Open";
                DrawButton(m, g.OkMin, g.OkMax, ok, d.FontSize, d.HoveredButton == 0, true);
                DrawButton(m, g.CancelMin, g.CancelMax, "Cancel", d.FontSize, d.HoveredButton == 1, false);
            }
        }

        bool OverlayHit(Entity e, const glm::mat4& m, glm::vec2 size, glm::vec2 canvasPoint, glm::vec2& local)
        {
            local = glm::vec2(glm::inverse(m) * glm::vec4(canvasPoint, 0.0f, 1.0f));
            // Modal diyaloglar tüm alanı tutar
            if ((e.HasComponent<UIDialogComponent>() && e.GetComponent<UIDialogComponent>().Visible) ||
                (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible))
                return true;
            if (e.HasComponent<UIMenuButtonComponent>() && e.GetComponent<UIMenuButtonComponent>().Open)
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                const MenuGeom g = LayoutMenu(mb.Items, mb.FontSize, { 0.0f, -2.0f }, size.x);
                if (In(local, g.Min, g.Max)) return true;
            }
            if (e.HasComponent<UIPopupMenuComponent>() && e.GetComponent<UIPopupMenuComponent>().Visible)
            {
                auto& pm = e.GetComponent<UIPopupMenuComponent>();
                const MenuGeom g = LayoutMenu(pm.Items, pm.FontSize, pm.Position, 160.0f);
                if (In(local, g.Min, g.Max)) return true;
            }
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                if (bar.OpenMenu >= 0 && bar.OpenMenu < (int)bar.Menus.size())
                {
                    const MenuGeom g = MenuBarGeom(bar, bar.OpenMenu, size);
                    if (In(local, g.Min, g.Max)) return true;
                }
            }
            if (e.HasComponent<UIColorPickerButtonComponent>() && e.GetComponent<UIColorPickerButtonComponent>().Open)
            {
                const glm::vec2 origin(0.0f, -kPickerH - 4.0f);
                if (In(local, origin, origin + glm::vec2(kPickerW, kPickerH))) return true;
            }
            return false;
        }

        void OverlayHover(Entity e, glm::vec2 local, glm::vec2 size, bool inside)
        {
            if (e.HasComponent<UIMenuButtonComponent>())
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                mb.Hovered = inside && mb.Open ? MenuItemAt(mb.Items, LayoutMenu(mb.Items, mb.FontSize, { 0.0f, -2.0f }, size.x), local) : -1;
            }
            if (e.HasComponent<UIPopupMenuComponent>())
            {
                auto& pm = e.GetComponent<UIPopupMenuComponent>();
                pm.Hovered = inside && pm.Visible ? MenuItemAt(pm.Items, LayoutMenu(pm.Items, pm.FontSize, pm.Position, 160.0f), local) : -1;
            }
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                if (bar.OpenMenu >= 0 && inside)
                {
                    const int i = MenuItemAt(bar.Menus[(size_t)bar.OpenMenu].Items, MenuBarGeom(bar, bar.OpenMenu, size), local);
                    bar.Hovered = i >= 0 ? 1000 + i : -1;
                }
            }
            if (e.HasComponent<UIDialogComponent>())
            {
                auto& d = e.GetComponent<UIDialogComponent>();
                const DialogButtons k = LayoutDialog(d, size);
                d.Hovered = !inside ? -1 : In(local, k.OkMin, k.OkMax) ? 0 : (d.Kind == 1 && In(local, k.CancelMin, k.CancelMax)) ? 1 : In(local, k.CloseMin, k.CloseMax) ? 2 : -1;
            }
            if (e.HasComponent<UIFileDialogComponent>())
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                const FileDialogGeom g = LayoutFileDialog(d, size);
                d.HoveredButton = !inside ? -1 : In(local, g.OkMin, g.OkMax) ? 0 : In(local, g.CancelMin, g.CancelMax) ? 1 : In(local, g.UpMin, g.UpMax) ? 2 : -1;
                d.Hovered = -1;
                if (inside && In(local, g.ListMin, g.ListMax))
                {
                    const int i = (int)((g.ListMax.y - 4.0f + d.ScrollOffset - local.y) / g.RowH);
                    if (i >= 0 && i < (int)d.Entries.size()) d.Hovered = i;
                }
            }
        }

        void OverlayPress(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool& wantsFocus)
        {
            (void)scene;
            wantsFocus = false;
            if (e.HasComponent<UIColorPickerButtonComponent>() && e.GetComponent<UIColorPickerButtonComponent>().Open)
            {
                auto& c = e.GetComponent<UIColorPickerButtonComponent>();
                if (PickerInput({ 0.0f, -kPickerH - 4.0f }, { kPickerW, kPickerH }, local, true, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, true }))
                {
                    c.ChangedThisFrame = true;
                    EmitEvent(e, c.OnColorChanged, "ValueChanged", c.Color.r, "color_changed", "OnColorChanged", {});
                }
            }
            if (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible)
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                const FileDialogGeom g = LayoutFileDialog(d, size);
                d.FileName.Active = false;
                if (In(local, g.NameMin, g.NameMax))
                {
                    d.FileName.Active = true;
                    d.FileName.Caret = std::clamp(UIDraw::CodepointAt(d.FileName.Text, local.x - g.NameMin.x - 10.0f, d.FontSize), 0, (int)Decode(d.FileName.Text).size());
                    d.FileName.Blink = 0.0f;
                    wantsFocus = true;
                }
                else if (In(local, g.ListMin, g.ListMax))
                {
                    const int i = (int)((g.ListMax.y - 4.0f + d.ScrollOffset - local.y) / g.RowH);
                    if (i >= 0 && i < (int)d.Entries.size())
                    {
                        const double now = Clock();
                        const bool doubleClick = now - d.LastClickTime < 0.4 && std::find(d.Selected.begin(), d.Selected.end(), i) != d.Selected.end();
                        d.LastClickTime = now;
                        const std::string& entry = d.Entries[(size_t)i];
                        if (entry[0] == '/')
                        {
                            if (doubleClick)
                            {
                                d.CurrentDir = (fs::path(d.CurrentDir) / entry.substr(1)).generic_string();
                                d.NeedsRefresh = true;
                                return;
                            }
                            d.Selected = { i };
                        }
                        else
                        {
                            if (d.FileMode == 1)
                            {
                                auto it = std::find(d.Selected.begin(), d.Selected.end(), i);
                                if (it != d.Selected.end() && !doubleClick) d.Selected.erase(it); else if (it == d.Selected.end()) d.Selected.push_back(i);
                            }
                            else
                                d.Selected = { i };
                            d.FileName.Text = entry;
                            d.FileName.Caret = (int)Decode(entry).size();
                            if (doubleClick)
                                AcceptFileDialog(d, e);
                        }
                    }
                }
            }
        }

        void OverlayDrag(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size)
        {
            (void)scene; (void)size;
            if (e.HasComponent<UIColorPickerButtonComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerButtonComponent>();
                if (c.DragPart && PickerInput({ 0.0f, -kPickerH - 4.0f }, { kPickerW, kPickerH }, local, false, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, true }))
                {
                    c.ChangedThisFrame = true;
                    EmitEvent(e, c.OnColorChanged, "ValueChanged", c.Color.r, "color_changed", "OnColorChanged", {});
                }
            }
        }

        void OverlayRelease(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size)
        {
            if (e.HasComponent<UIColorPickerButtonComponent>())
                e.GetComponent<UIColorPickerButtonComponent>().DragPart = 0;
            auto pressMenu = [&](std::vector<std::string>& items, int i, const std::string& configured, std::vector<CQS::Value> args, const char* method) {
                PressMenuItem(items, i);
                EmitEvent(e, configured, "ItemPressed", (float)i, "id_pressed", method, std::move(args));
            };
            if (e.HasComponent<UIMenuButtonComponent>() && e.GetComponent<UIMenuButtonComponent>().Open)
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                const int i = MenuItemAt(mb.Items, LayoutMenu(mb.Items, mb.FontSize, { 0.0f, -2.0f }, size.x), local);
                if (i >= 0)
                {
                    mb.LastPressed = i;
                    mb.Open = false;
                    pressMenu(mb.Items, i, mb.OnItemPressed, { CQS::Value::MakeInt(i), CQS::Value::MakeString(ItemLabel(mb.Items, i)) }, "OnMenuItemPressed");
                }
            }
            if (e.HasComponent<UIPopupMenuComponent>() && e.GetComponent<UIPopupMenuComponent>().Visible)
            {
                auto& pm = e.GetComponent<UIPopupMenuComponent>();
                const int i = MenuItemAt(pm.Items, LayoutMenu(pm.Items, pm.FontSize, pm.Position, 160.0f), local);
                if (i >= 0)
                {
                    pm.LastPressed = i;
                    if (pm.HideOnItemSelection) pm.Visible = false;
                    pressMenu(pm.Items, i, pm.OnItemPressed, { CQS::Value::MakeInt(i), CQS::Value::MakeString(ItemLabel(pm.Items, i)) }, "OnMenuItemPressed");
                }
            }
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                if (bar.OpenMenu >= 0 && bar.OpenMenu < (int)bar.Menus.size())
                {
                    auto& items = bar.Menus[(size_t)bar.OpenMenu].Items;
                    const int i = MenuItemAt(items, MenuBarGeom(bar, bar.OpenMenu, size), local);
                    if (i >= 0)
                    {
                        bar.LastMenu = bar.OpenMenu;
                        bar.LastItem = i;
                        const int menu = bar.OpenMenu;
                        bar.OpenMenu = -1;
                        PressMenuItem(items, i);
                        EmitEvent(e, bar.OnItemPressed, "ItemPressed", (float)i, "menu_item_pressed", "OnMenuBarItemPressed",
                                  { CQS::Value::MakeInt(menu), CQS::Value::MakeInt(i), CQS::Value::MakeString(ItemLabel(items, i)) });
                    }
                }
            }
            if (e.HasComponent<UIDialogComponent>() && e.GetComponent<UIDialogComponent>().Visible)
            {
                auto& d = e.GetComponent<UIDialogComponent>();
                const DialogButtons k = LayoutDialog(d, size);
                if (In(local, k.OkMin, k.OkMax))
                {
                    ++d.Confirmed;
                    if (d.HideOnOk) d.Visible = false;
                    EmitEvent(e, d.OnConfirmed, "Confirmed", 1.0f, "confirmed", "OnConfirmed", {});
                }
                else if ((d.Kind == 1 && In(local, k.CancelMin, k.CancelMax)) || In(local, k.CloseMin, k.CloseMax))
                {
                    ++d.Canceled;
                    d.Visible = false;
                    EmitEvent(e, d.OnConfirmed, "Canceled", 0.0f, "canceled", "OnCanceled", {});
                }
            }
            if (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible)
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                const FileDialogGeom g = LayoutFileDialog(d, size);
                if (In(local, g.OkMin, g.OkMax))
                    AcceptFileDialog(d, e);
                else if (In(local, g.CancelMin, g.CancelMax))
                {
                    d.Visible = false;
                    d.FileName.Active = false;
                    EmitEvent(e, d.OnFileSelected, "Canceled", 0.0f, "canceled", "OnCanceled", {});
                }
                else if (In(local, g.UpMin, g.UpMax) && !d.CurrentDir.empty())
                {
                    d.CurrentDir = fs::path(d.CurrentDir).parent_path().generic_string();
                    d.NeedsRefresh = true;
                }
            }
            (void)scene;
        }

        void CloseOverlays(Scene& scene, Entity except)
        {
            for (auto e : scene.m_Registry.view<UIMenuButtonComponent>())
                if (Entity(e, &scene) != except) scene.m_Registry.get<UIMenuButtonComponent>(e).Open = false;
            for (auto e : scene.m_Registry.view<UIPopupMenuComponent>())
                if (Entity(e, &scene) != except)
                {
                    auto& pm = scene.m_Registry.get<UIPopupMenuComponent>(e);
                    if (pm.Visible)
                    {
                        pm.Visible = false;
                        EmitEvent(Entity(e, &scene), pm.OnItemPressed, "PopupHide", 0.0f, "popup_hide", "OnPopupHide", {});
                    }
                }
            for (auto e : scene.m_Registry.view<UIMenuBarComponent>())
                if (Entity(e, &scene) != except) scene.m_Registry.get<UIMenuBarComponent>(e).OpenMenu = -1;
            for (auto e : scene.m_Registry.view<UIColorPickerButtonComponent>())
                if (Entity(e, &scene) != except)
                {
                    auto& c = scene.m_Registry.get<UIColorPickerButtonComponent>(e);
                    if (c.Open)
                    {
                        c.Open = false;
                        EmitEvent(Entity(e, &scene), c.OnColorChanged, "PopupClosed", 0.0f, "popup_closed", "OnPopupClosed", {});
                    }
                }
        }

        // ================================================================ normal girdi
        bool OnPress(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool& wantsFocus)
        {
            wantsFocus = false;
            if (e.HasComponent<UISpinBoxComponent>())
            {
                auto& sb = e.GetComponent<UISpinBoxComponent>();
                if (!sb.Selectable.Interactable)
                    return true;
                const float aw = std::min(30.0f, size.x * 0.3f);
                if (local.x >= size.x - aw)
                {
                    CommitSpinEdit(e, sb);
                    sb.HeldArrow = local.y >= size.y * 0.5f ? 1 : -1;
                    sb.RepeatTimer = -0.4f;
                    SetSpinValue(e, sb, sb.Value + sb.HeldArrow * sb.Step);
                }
                else if (sb.Editable)
                {
                    if (!sb.Edit.Active)
                    {
                        sb.Edit.Active = true;
                        sb.Edit.Text = SpinText(sb);
                    }
                    sb.Edit.Caret = (int)Decode(sb.Edit.Text).size();
                    sb.Edit.Blink = 0.0f;
                    wantsFocus = true;
                }
                return true;
            }
            if (e.HasComponent<UIColorPickerComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerComponent>();
                if (c.Selectable.Interactable && PickerInput({ 0, 0 }, size, local, true, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, c.ShowHex }))
                {
                    c.ChangedThisFrame = true;
                    EmitEvent(e, c.OnColorChanged, "ValueChanged", c.Color.r, "color_changed", "OnColorChanged", {});
                }
                return true;
            }
            if (e.HasComponent<UIColorPickerButtonComponent>() || e.HasComponent<UIMenuButtonComponent>() || e.HasComponent<UILinkButtonComponent>())
                return true;   // bırakınca
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                float x = 4.0f;
                for (int i = 0; i < (int)bar.Menus.size(); ++i)
                {
                    const float w = UIDraw::TextWidth(bar.Menus[(size_t)i].Title, bar.FontSize) + 24.0f;
                    if (local.x >= x && local.x < x + w)
                    {
                        bar.OpenMenu = bar.OpenMenu == i ? -1 : i;
                        return true;
                    }
                    x += w + 2.0f;
                }
                bar.OpenMenu = -1;
                return true;
            }
            if (e.HasComponent<UITabContainerComponent>())
            {
                auto& tc = e.GetComponent<UITabContainerComponent>();
                if (!tc.TabsVisible || local.y < size.y - tc.TabHeight)
                    return false;
                const int i = TabAt(TabWidths(ChildTitles(e), tc.FontSize, false), local.x);
                if (i >= 0 && i != tc.CurrentTab)
                {
                    tc.CurrentTab = i;
                    tc.ChangedThisFrame = true;
                    EmitEvent(e, tc.OnTabChanged, "TabChanged", (float)i, "tab_changed", "OnTabChanged", { CQS::Value::MakeInt(i) });
                }
                return true;
            }
            if (e.HasComponent<UITabBarComponent>())
            {
                auto& tb = e.GetComponent<UITabBarComponent>();
                const std::vector<float> widths = TabWidths(tb.Tabs, tb.FontSize, tb.CloseButtons);
                const int i = TabAt(widths, local.x);
                if (i < 0)
                    return true;
                float x0 = 0.0f;
                for (int k = 0; k < i; ++k) x0 += widths[(size_t)k] + 2.0f;
                if (tb.CloseButtons && local.x > x0 + widths[(size_t)i] - 24.0f)
                {
                    EmitEvent(e, tb.OnTabChanged, "TabClosePressed", (float)i, "tab_close_pressed", "OnTabClosePressed", { CQS::Value::MakeInt(i) });
                    return true;
                }
                if (i != tb.CurrentTab)
                {
                    tb.CurrentTab = i;
                    tb.ChangedThisFrame = true;
                    EmitEvent(e, tb.OnTabChanged, "TabChanged", (float)i, "tab_changed", "OnTabChanged", { CQS::Value::MakeInt(i) });
                }
                return true;
            }
            if (e.HasComponent<UISplitContainerComponent>())
            {
                auto& sp = e.GetComponent<UISplitContainerComponent>();
                if (sp.Collapsed)
                    return false;
                sp.Dragging = true;
                return true;
            }
            if (e.HasComponent<UIItemListComponent>())
            {
                auto& l = e.GetComponent<UIItemListComponent>();
                const float rowH = ListRowHeight(l.FontSize, l.RowHeight);
                const int cols = std::max(1, l.MaxColumns);
                const int r = (int)std::floor((size.y - 4.0f + l.ScrollOffset - local.y) / rowH);
                const int c = std::clamp((int)((local.x - 4.0f) / ((size.x - 8.0f) / cols)), 0, cols - 1);
                const int i = r * cols + c;
                if (r >= 0 && i < (int)l.Items.size())
                {
                    const double now = Clock();
                    if (now - l.LastClickTime < 0.4 && l.LastClickItem == i && !l.Items[(size_t)i].Disabled)
                    {
                        l.LastActivated = i;
                        EmitEvent(e, l.OnItemSelected, "ItemActivated", (float)i, "item_activated", "OnItemActivated", { CQS::Value::MakeInt(i) });
                    }
                    else
                        SelectListItem(e, l, i, true);
                    l.LastClickTime = now;
                    l.LastClickItem = i;
                }
                return true;
            }
            if (e.HasComponent<UITreeComponent>())
            {
                auto& t = e.GetComponent<UITreeComponent>();
                const float rowH = ListRowHeight(t.FontSize, 0.0f);
                const std::vector<int> visible = VisibleTreeItems(t);
                const int r = (int)std::floor((size.y - 4.0f + t.ScrollOffset - local.y) / rowH);
                if (r < 0 || r >= (int)visible.size())
                    return true;
                const int i = visible[(size_t)r];
                auto& it = t.Items[(size_t)i];
                const float x = 10.0f + std::max(0, it.Depth - (t.HideRoot ? 1 : 0)) * t.Indent;
                if (TreeHasChildren(t, i) && local.x >= x - 4.0f && local.x <= x + 16.0f)
                {
                    it.Collapsed = !it.Collapsed;
                    EmitEvent(e, t.OnItemSelected, "ItemCollapsed", (float)i, "item_collapsed", "OnItemCollapsed", { CQS::Value::MakeInt(i), CQS::Value::MakeBool(it.Collapsed) });
                    return true;
                }
                const double now = Clock();
                if (now - t.LastClickTime < 0.4 && t.Selected == i)
                {
                    t.LastActivated = i;
                    EmitEvent(e, t.OnItemSelected, "ItemActivated", (float)i, "item_activated", "OnItemActivated", { CQS::Value::MakeInt(i), CQS::Value::MakeString(it.Text) });
                }
                else if (t.Selected != i)
                {
                    t.Selected = i;
                    t.ChangedThisFrame = true;
                    EmitEvent(e, t.OnItemSelected, "ItemSelected", (float)i, "item_selected", "OnItemSelected", { CQS::Value::MakeInt(i), CQS::Value::MakeString(it.Text) });
                }
                t.LastClickTime = now;
                return true;
            }
            if (e.HasComponent<UITextEditComponent>())
            {
                auto& t = e.GetComponent<UITextEditComponent>();
                if (!t.Selectable.Interactable)
                    return true;
                const std::vector<std::string> lines = SplitLines(t.Text);
                const float lineH = UIDraw::LineHeight(t.FontSize);
                t.CaretLine = std::clamp((int)std::floor((size.y - 6.0f + t.ScrollY - local.y) / lineH), 0, (int)lines.size() - 1);
                const float gutter = GutterWidth(t, (int)lines.size());
                t.CaretColumn = std::clamp(UIDraw::CodepointAt(lines[(size_t)t.CaretLine], local.x - gutter - 4.0f + t.ScrollX, t.FontSize), 0,
                                           (int)Decode(lines[(size_t)t.CaretLine]).size());
                t.Focused = true;
                t.Blink = 0.0f;
                wantsFocus = true;
                return true;
            }
            if (e.HasComponent<UIGraphNodeComponent>())
            {
                auto& n = e.GetComponent<UIGraphNodeComponent>();
                Entity graph = e.GetParent();
                if (!graph || !graph.HasComponent<UIGraphEditComponent>())
                    return true;
                auto& g = graph.GetComponent<UIGraphEditComponent>();
                for (auto c : graph.GetChildren())
                    if (c.HasComponent<UIGraphNodeComponent>()) c.GetComponent<UIGraphNodeComponent>().Selected = c == e;
                // Çıkış ucu: bağlantı sürüklemesi
                for (int i = 0; i < n.OutputPorts; ++i)
                {
                    const float y = size.y - kNodeTitle - 16.0f - i * kPortGap;
                    if (glm::length(local - glm::vec2(size.x, y)) <= kPortRadius + 5.0f)
                    {
                        g.DragFrom = Uuid(e);
                        g.DragFromPort = i;
                        const glm::vec2 gs = GraphSize(graph);
                        const NodeView nv{ e, &n, size };
                        g.DragPoint = ContentToLocal(g, gs, NodeLocalToContent(nv, local));
                        return true;
                    }
                }
                // Giriş ucu: bağlıysa kopar ve kaynağından yeniden sürükle (Godot disconnection_request)
                for (int i = 0; i < n.InputPorts; ++i)
                {
                    const float y = size.y - kNodeTitle - 16.0f - i * kPortGap;
                    if (glm::length(local - glm::vec2(0.0f, y)) <= kPortRadius + 5.0f)
                    {
                        const uint64_t self = Uuid(e);
                        for (auto it = g.Connections.begin(); it != g.Connections.end(); ++it)
                            if (it->To == self && it->ToPort == i)
                            {
                                const auto conn = *it;
                                if (g.AutoConnect)
                                    g.Connections.erase(it);
                                Entity from = scene.FindEntityByUUID(conn.From);
                                EmitEvent(graph, g.OnConnectionRequest, "DisconnectionRequest", 0.0f, "disconnection_request", "OnDisconnectionRequest",
                                          { CQS::Value::MakeString(from ? from.GetComponent<TagComponent>().Tag : std::string()), CQS::Value::MakeInt(conn.FromPort),
                                            CQS::Value::MakeString(e.GetComponent<TagComponent>().Tag), CQS::Value::MakeInt(i) });
                                g.DragFrom = conn.From;
                                g.DragFromPort = conn.FromPort;
                                const glm::vec2 gs = GraphSize(graph);
                                g.DragPoint = ContentToLocal(g, gs, NodeLocalToContent({ e, &n, size }, local));
                                break;
                            }
                        return true;
                    }
                }
                if (n.Draggable && local.y >= size.y - kNodeTitle)
                {
                    g.MovingNode = Uuid(e);
                    g.MoveGrab = local;
                }
                return true;
            }
            if (e.HasComponent<UIGraphEditComponent>())
            {
                auto& g = e.GetComponent<UIGraphEditComponent>();
                g.Panning = true;
                g.PanStart = local;
                g.PanOrigin = g.ScrollOffset;
                for (auto c : e.GetChildren())
                    if (c.HasComponent<UIGraphNodeComponent>()) c.GetComponent<UIGraphNodeComponent>().Selected = false;
                return true;
            }
            return false;
        }

        void OnDrag(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size)
        {
            (void)scene;
            if (e.HasComponent<UIColorPickerComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerComponent>();
                if (c.DragPart && PickerInput({ 0, 0 }, size, local, false, { c.Hue, c.Saturation, c.ValueHSV, c.DragPart, c.Color, c.EditAlpha, c.ShowHex }))
                {
                    c.ChangedThisFrame = true;
                    EmitEvent(e, c.OnColorChanged, "ValueChanged", c.Color.r, "color_changed", "OnColorChanged", {});
                }
            }
            if (e.HasComponent<UISplitContainerComponent>())
            {
                auto& sp = e.GetComponent<UISplitContainerComponent>();
                if (sp.Dragging)
                {
                    const float total = std::max(1.0f, (sp.Vertical ? size.y : size.x) - sp.DraggerThickness);
                    const float pos = sp.Vertical ? (size.y - local.y - sp.DraggerThickness * 0.5f) : (local.x - sp.DraggerThickness * 0.5f);
                    const float lo = std::min(sp.MinPaneSize, total * 0.5f), hi = std::max(total - sp.MinPaneSize, total * 0.5f);
                    const float ratio = std::clamp(pos, lo, hi) / total;
                    if (std::fabs(ratio - sp.SplitRatio) > 1e-5f)
                    {
                        sp.SplitRatio = ratio;
                        EmitEvent(e, sp.OnDragged, "Dragged", ratio, "dragged", "OnDragged", { CQS::Value::MakeFloat(ratio) });
                    }
                }
            }
            if (e.HasComponent<UIGraphNodeComponent>())
            {
                auto& n = e.GetComponent<UIGraphNodeComponent>();
                Entity graph = e.GetParent();
                if (graph && graph.HasComponent<UIGraphEditComponent>())
                {
                    auto& g = graph.GetComponent<UIGraphEditComponent>();
                    const NodeView nv{ e, &n, size };
                    if (g.DragFrom)
                        g.DragPoint = ContentToLocal(g, GraphSize(graph), NodeLocalToContent(nv, local));
                    else if (g.MovingNode == Uuid(e))
                    {
                        n.PositionOffset.x += local.x - g.MoveGrab.x;
                        n.PositionOffset.y -= local.y - g.MoveGrab.y;
                    }
                }
            }
            if (e.HasComponent<UIGraphEditComponent>())
            {
                auto& g = e.GetComponent<UIGraphEditComponent>();
                if (g.Panning)
                    g.ScrollOffset = g.PanOrigin + glm::vec2(local.x - g.PanStart.x, -(local.y - g.PanStart.y));
            }
        }

        void OnRelease(Scene& scene, Entity e, glm::vec2 local, glm::vec2 size, bool inside)
        {
            if (e.HasComponent<UISpinBoxComponent>())
                e.GetComponent<UISpinBoxComponent>().HeldArrow = 0;
            if (e.HasComponent<UIColorPickerComponent>())
                e.GetComponent<UIColorPickerComponent>().DragPart = 0;
            if (e.HasComponent<UISplitContainerComponent>())
                e.GetComponent<UISplitContainerComponent>().Dragging = false;
            if (inside && e.HasComponent<UIColorPickerButtonComponent>())
            {
                auto& c = e.GetComponent<UIColorPickerButtonComponent>();
                if (c.Selectable.Interactable)
                {
                    c.Open = !c.Open;
                    if (c.Open)
                        EmitEvent(e, c.OnColorChanged, "PickerCreated", 0.0f, "picker_created", "OnPickerCreated", {});
                }
            }
            if (inside && e.HasComponent<UIMenuButtonComponent>())
            {
                auto& mb = e.GetComponent<UIMenuButtonComponent>();
                if (mb.Selectable.Interactable)
                {
                    mb.Open = !mb.Open;
                    if (mb.Open)
                        EmitEvent(e, mb.OnItemPressed, "AboutToPopup", 0.0f, "about_to_popup", "OnAboutToPopup", {});
                }
            }
            if (inside && e.HasComponent<UILinkButtonComponent>())
            {
                auto& l = e.GetComponent<UILinkButtonComponent>();
                if (l.Selectable.Interactable)
                {
                    ++l.Clicks;
                    OpenUri(l.Uri);
                    EmitEvent(e, l.OnClick, "Click", 1.0f, "pressed", "OnPressed", {});
                }
            }
            if (e.HasComponent<UIGraphEditComponent>())
                e.GetComponent<UIGraphEditComponent>().Panning = false;
            if (e.HasComponent<UIGraphNodeComponent>())
            {
                auto& n = e.GetComponent<UIGraphNodeComponent>();
                Entity graph = e.GetParent();
                if (graph && graph.HasComponent<UIGraphEditComponent>())
                {
                    auto& g = graph.GetComponent<UIGraphEditComponent>();
                    if (g.DragFrom)
                    {
                        // Bırakılan noktadaki giriş ucu (içerik koordinatında)
                        const glm::vec2 point = NodeLocalToContent({ e, &n, size }, local);
                        Entity from = scene.FindEntityByUUID(g.DragFrom);
                        for (auto target : GraphNodes(graph))
                        {
                            if (Uuid(target.E) == g.DragFrom)
                                continue;
                            for (int p = 0; p < target.Node->InputPorts; ++p)
                                if (glm::length(point - PortContent(target, false, p)) <= kPortRadius + 8.0f)
                                {
                                    const UIGraphEditComponent::Connection c{ g.DragFrom, g.DragFromPort, Uuid(target.E), p };
                                    const bool exists = std::any_of(g.Connections.begin(), g.Connections.end(), [&](const auto& x) {
                                        return x.From == c.From && x.FromPort == c.FromPort && x.To == c.To && x.ToPort == c.ToPort;
                                    });
                                    if (g.AutoConnect && !exists)
                                        g.Connections.push_back(c);
                                    EmitEvent(graph, g.OnConnectionRequest, "ConnectionRequest", 1.0f, "connection_request", "OnConnectionRequest",
                                              { CQS::Value::MakeString(from ? from.GetComponent<TagComponent>().Tag : std::string()), CQS::Value::MakeInt(c.FromPort),
                                                CQS::Value::MakeString(target.E.GetComponent<TagComponent>().Tag), CQS::Value::MakeInt(p) });
                                }
                        }
                        g.DragFrom = 0;
                        g.DragFromPort = -1;
                    }
                    g.MovingNode = 0;
                }
            }
        }

        void OnHover(Entity e, glm::vec2 local, glm::vec2 size, bool inside)
        {
            if (e.HasComponent<UIMenuBarComponent>())
            {
                auto& bar = e.GetComponent<UIMenuBarComponent>();
                float x = 4.0f;
                int hovered = -1;
                for (int i = 0; i < (int)bar.Menus.size() && inside; ++i)
                {
                    const float w = UIDraw::TextWidth(bar.Menus[(size_t)i].Title, bar.FontSize) + 24.0f;
                    if (local.x >= x && local.x < x + w) hovered = i;
                    x += w + 2.0f;
                }
                if (bar.Hovered < 1000) bar.Hovered = hovered;
                if (hovered >= 0 && bar.OpenMenu >= 0 && bar.OpenMenu != hovered)
                    bar.OpenMenu = hovered;   // açıkken başlıklar arasında gezinme
            }
            if (e.HasComponent<UITabContainerComponent>())
            {
                auto& tc = e.GetComponent<UITabContainerComponent>();
                tc.HoveredTab = inside && tc.TabsVisible && local.y >= size.y - tc.TabHeight ? TabAt(TabWidths(ChildTitles(e), tc.FontSize, false), local.x) : -1;
            }
            if (e.HasComponent<UITabBarComponent>())
            {
                auto& tb = e.GetComponent<UITabBarComponent>();
                tb.HoveredTab = inside ? TabAt(TabWidths(tb.Tabs, tb.FontSize, tb.CloseButtons), local.x) : -1;
            }
            if (e.HasComponent<UIItemListComponent>())
            {
                auto& l = e.GetComponent<UIItemListComponent>();
                const float rowH = ListRowHeight(l.FontSize, l.RowHeight);
                const int cols = std::max(1, l.MaxColumns);
                const int r = (int)std::floor((size.y - 4.0f + l.ScrollOffset - local.y) / rowH);
                const int c = std::clamp((int)((local.x - 4.0f) / ((size.x - 8.0f) / cols)), 0, cols - 1);
                l.Hovered = inside && r >= 0 && r * cols + c < (int)l.Items.size() ? r * cols + c : -1;
            }
            if (e.HasComponent<UITreeComponent>())
            {
                auto& t = e.GetComponent<UITreeComponent>();
                const std::vector<int> visible = VisibleTreeItems(t);
                const int r = (int)std::floor((size.y - 4.0f + t.ScrollOffset - local.y) / ListRowHeight(t.FontSize, 0.0f));
                t.Hovered = inside && r >= 0 && r < (int)visible.size() ? visible[(size_t)r] : -1;
            }
        }

        bool OnScroll(Scene& scene, Entity e, float delta)
        {
            (void)scene;
            if (e.HasComponent<UISpinBoxComponent>())
            {
                auto& sb = e.GetComponent<UISpinBoxComponent>();
                if (sb.Selectable.Interactable)
                    SetSpinValue(e, sb, sb.Value + (delta > 0.0f ? sb.Step : -sb.Step));
                return true;
            }
            auto scrollList = [&](float& offset, float content, float view, float rowH) {
                offset = std::clamp(offset - delta * rowH * 1.5f, 0.0f, std::max(0.0f, content - view));
            };
            if (e.HasComponent<UIItemListComponent>() && e.HasComponent<RectTransformComponent>())
            {
                auto& l = e.GetComponent<UIItemListComponent>();
                const float rowH = ListRowHeight(l.FontSize, l.RowHeight);
                const int rows = ((int)l.Items.size() + std::max(1, l.MaxColumns) - 1) / std::max(1, l.MaxColumns);
                scrollList(l.ScrollOffset, rows * rowH + 8.0f, e.GetComponent<RectTransformComponent>().ComputedRect.w, rowH);
                return true;
            }
            if (e.HasComponent<UITreeComponent>() && e.HasComponent<RectTransformComponent>())
            {
                auto& t = e.GetComponent<UITreeComponent>();
                const float rowH = ListRowHeight(t.FontSize, 0.0f);
                scrollList(t.ScrollOffset, VisibleTreeItems(t).size() * rowH + 8.0f, e.GetComponent<RectTransformComponent>().ComputedRect.w, rowH);
                return true;
            }
            if (e.HasComponent<UITextEditComponent>() && e.HasComponent<RectTransformComponent>())
            {
                auto& t = e.GetComponent<UITextEditComponent>();
                const float lineH = UIDraw::LineHeight(t.FontSize);
                scrollList(t.ScrollY, SplitLines(t.Text).size() * lineH + 12.0f, e.GetComponent<RectTransformComponent>().ComputedRect.w, lineH);
                return true;
            }
            if (e.HasComponent<UIGraphEditComponent>())
            {
                auto& g = e.GetComponent<UIGraphEditComponent>();
                g.Zoom = std::clamp(g.Zoom * (delta > 0.0f ? 1.1f : 1.0f / 1.1f), 0.3f, 3.0f);
                return true;
            }
            // Dosya diyaloğu listesi
            if (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().Visible && e.HasComponent<RectTransformComponent>())
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                const auto& r = e.GetComponent<RectTransformComponent>().ComputedRect;
                const FileDialogGeom g = LayoutFileDialog(d, { r.z, r.w });
                scrollList(d.ScrollOffset, d.Entries.size() * g.RowH + 8.0f, g.ListMax.y - g.ListMin.y, g.RowH);
                return true;
            }
            return false;
        }

        bool WantsKeyboard(Entity e)
        {
            return (e.HasComponent<UISpinBoxComponent>() && e.GetComponent<UISpinBoxComponent>().Edit.Active) ||
                   (e.HasComponent<UITextEditComponent>() && e.GetComponent<UITextEditComponent>().Focused) ||
                   (e.HasComponent<UIFileDialogComponent>() && e.GetComponent<UIFileDialogComponent>().FileName.Active);
        }

        void Unfocus(Entity e)
        {
            if (e.HasComponent<UISpinBoxComponent>())
                CommitSpinEdit(e, e.GetComponent<UISpinBoxComponent>());
            if (e.HasComponent<UITextEditComponent>())
                e.GetComponent<UITextEditComponent>().Focused = false;
            if (e.HasComponent<UIFileDialogComponent>())
                e.GetComponent<UIFileDialogComponent>().FileName.Active = false;
        }

        void OnKeyboard(Scene& scene, Entity e, const UIInputState& in, float dt)
        {
            (void)scene;
            if (e.HasComponent<UISpinBoxComponent>())
            {
                auto& sb = e.GetComponent<UISpinBoxComponent>();
                if (!sb.Edit.Active)
                    return;
                const EditResult r = LineEditInput(sb.Edit, in, dt, [](uint32_t c) { return (c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-' || c == '+'; });
                if (r == EditResult::Enter) CommitSpinEdit(e, sb);
                else if (r == EditResult::Escape) sb.Edit.Active = false;
                else if (in.Up || in.Down)
                {
                    CommitSpinEdit(e, sb);
                    SetSpinValue(e, sb, sb.Value + (in.Up ? sb.Step : -sb.Step));
                    sb.Edit.Active = true;
                    sb.Edit.Text = SpinText(sb);
                    sb.Edit.Caret = (int)Decode(sb.Edit.Text).size();
                }
                return;
            }
            if (e.HasComponent<UIFileDialogComponent>())
            {
                auto& d = e.GetComponent<UIFileDialogComponent>();
                if (!d.FileName.Active)
                    return;
                const EditResult r = LineEditInput(d.FileName, in, dt, [](uint32_t c) { return c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|'; });
                if (r == EditResult::Enter) AcceptFileDialog(d, e);
                else if (r == EditResult::Escape) d.FileName.Active = false;
                return;
            }
            if (e.HasComponent<UITextEditComponent>())
            {
                auto& t = e.GetComponent<UITextEditComponent>();
                if (!t.Focused)
                    return;
                t.Blink += dt;
                std::vector<std::string> lines = SplitLines(t.Text);
                t.CaretLine = std::clamp(t.CaretLine, 0, (int)lines.size() - 1);
                std::vector<uint32_t> cur = Decode(lines[(size_t)t.CaretLine]);
                t.CaretColumn = std::clamp(t.CaretColumn, 0, (int)cur.size());
                bool changed = false;
                auto commitLine = [&]() { lines[(size_t)t.CaretLine] = Encode(cur); };
                if (t.Editable)
                {
                    for (uint32_t c : in.Characters)
                    {
                        if (c < 32 && c != '\t')
                            continue;
                        cur.insert(cur.begin() + t.CaretColumn, c);
                        ++t.CaretColumn;
                        // Parantez / tırnak kapatma (Code Edit)
                        if (t.CodeEditor && t.AutoBraceCompletion)
                        {
                            const uint32_t close = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : c == '"' ? '"' : 0;
                            if (close)
                                cur.insert(cur.begin() + t.CaretColumn, close);
                        }
                        changed = true;
                    }
                    if (in.Tab)
                    {
                        for (int k = 0; k < 4; ++k) cur.insert(cur.begin() + t.CaretColumn, ' ');
                        t.CaretColumn += 4;
                        changed = true;
                    }
                    if (in.Backspace)
                    {
                        if (t.CaretColumn > 0)
                        {
                            cur.erase(cur.begin() + t.CaretColumn - 1);
                            --t.CaretColumn;
                            changed = true;
                        }
                        else if (t.CaretLine > 0)
                        {
                            std::vector<uint32_t> prev = Decode(lines[(size_t)t.CaretLine - 1]);
                            t.CaretColumn = (int)prev.size();
                            prev.insert(prev.end(), cur.begin(), cur.end());
                            lines.erase(lines.begin() + t.CaretLine);
                            --t.CaretLine;
                            cur = prev;
                            changed = true;
                        }
                    }
                    if (in.Delete)
                    {
                        if (t.CaretColumn < (int)cur.size())
                        {
                            cur.erase(cur.begin() + t.CaretColumn);
                            changed = true;
                        }
                        else if (t.CaretLine + 1 < (int)lines.size())
                        {
                            std::vector<uint32_t> next = Decode(lines[(size_t)t.CaretLine + 1]);
                            cur.insert(cur.end(), next.begin(), next.end());
                            lines.erase(lines.begin() + t.CaretLine + 1);
                            changed = true;
                        }
                    }
                    if (in.Enter)
                    {
                        std::vector<uint32_t> rest(cur.begin() + t.CaretColumn, cur.end());
                        cur.erase(cur.begin() + t.CaretColumn, cur.end());
                        // Otomatik girinti: önceki satırın boşlukları (+ "{" sonrası 4)
                        std::vector<uint32_t> indent;
                        if (t.AutoIndent)
                        {
                            for (uint32_t c : cur) { if (c == ' ' || c == '\t') indent.push_back(c); else break; }
                            if (t.CodeEditor && !cur.empty() && cur.back() == '{')
                                for (int k = 0; k < 4; ++k) indent.push_back(' ');
                        }
                        commitLine();
                        std::vector<uint32_t> next = indent;
                        next.insert(next.end(), rest.begin(), rest.end());
                        lines.insert(lines.begin() + t.CaretLine + 1, Encode(next));
                        ++t.CaretLine;
                        cur = next;
                        t.CaretColumn = (int)indent.size();
                        changed = true;
                    }
                }
                if (in.Left)
                {
                    if (t.CaretColumn > 0) --t.CaretColumn;
                    else if (t.CaretLine > 0) { commitLine(); --t.CaretLine; cur = Decode(lines[(size_t)t.CaretLine]); t.CaretColumn = (int)cur.size(); }
                }
                if (in.Right)
                {
                    if (t.CaretColumn < (int)cur.size()) ++t.CaretColumn;
                    else if (t.CaretLine + 1 < (int)lines.size()) { commitLine(); ++t.CaretLine; cur = Decode(lines[(size_t)t.CaretLine]); t.CaretColumn = 0; }
                }
                if (in.Up && t.CaretLine > 0) { commitLine(); --t.CaretLine; cur = Decode(lines[(size_t)t.CaretLine]); t.CaretColumn = std::min(t.CaretColumn, (int)cur.size()); }
                if (in.Down && t.CaretLine + 1 < (int)lines.size()) { commitLine(); ++t.CaretLine; cur = Decode(lines[(size_t)t.CaretLine]); t.CaretColumn = std::min(t.CaretColumn, (int)cur.size()); }
                if (in.Home) t.CaretColumn = 0;
                if (in.End) t.CaretColumn = (int)cur.size();
                if (in.Escape) t.Focused = false;
                commitLine();
                if (changed)
                {
                    t.Text = JoinLines(lines);
                    t.ChangedThisFrame = true;
                    t.Blink = 0.0f;
                    EmitEvent(e, t.OnTextChanged, "TextChanged", (float)t.Text.size(), "text_changed", "OnTextChanged", {});
                }
                // İmleç görünür kalsın (dikey / yatay kaydırma)
                if (e.HasComponent<RectTransformComponent>())
                {
                    const auto& r = e.GetComponent<RectTransformComponent>().ComputedRect;
                    const float lineH = UIDraw::LineHeight(t.FontSize);
                    const float caretTop = t.CaretLine * lineH;
                    if (caretTop < t.ScrollY) t.ScrollY = caretTop;
                    if (caretTop + lineH + 12.0f > t.ScrollY + r.w) t.ScrollY = caretTop + lineH + 12.0f - r.w;
                    const float gutter = GutterWidth(t, (int)lines.size());
                    const float cx = UIDraw::CaretX(lines[(size_t)t.CaretLine], t.CaretColumn, t.FontSize);
                    const float view = r.z - gutter - 16.0f;
                    if (cx < t.ScrollX) t.ScrollX = std::max(0.0f, cx - 20.0f);
                    if (cx > t.ScrollX + view) t.ScrollX = cx - view + 20.0f;
                }
            }
        }

        void Update(Scene& scene, float dt)
        {
            for (auto e : scene.m_Registry.view<UISpinBoxComponent>())
            {
                auto& sb = scene.m_Registry.get<UISpinBoxComponent>(e);
                sb.ChangedThisFrame = false;
                if (sb.HeldArrow != 0)
                {
                    sb.RepeatTimer += dt;
                    while (sb.RepeatTimer > 0.06f)
                    {
                        sb.RepeatTimer -= 0.06f;
                        SetSpinValue(Entity(e, &scene), sb, sb.Value + sb.HeldArrow * sb.Step);
                    }
                }
                if (sb.Edit.Active) sb.Edit.Blink += 0.0f;
            }
            for (auto e : scene.m_Registry.view<UIProgressBarComponent>())
                scene.m_Registry.get<UIProgressBarComponent>(e).Phase += dt;
            for (auto e : scene.m_Registry.view<UIColorPickerComponent>()) scene.m_Registry.get<UIColorPickerComponent>(e).ChangedThisFrame = false;
            for (auto e : scene.m_Registry.view<UIColorPickerButtonComponent>()) scene.m_Registry.get<UIColorPickerButtonComponent>(e).ChangedThisFrame = false;
            for (auto e : scene.m_Registry.view<UITabContainerComponent>()) { auto& c = scene.m_Registry.get<UITabContainerComponent>(e); c.ChangedThisFrame = false; c.HoveredTab = -1; }
            for (auto e : scene.m_Registry.view<UITabBarComponent>()) { auto& c = scene.m_Registry.get<UITabBarComponent>(e); c.ChangedThisFrame = false; c.HoveredTab = -1; }
            for (auto e : scene.m_Registry.view<UIItemListComponent>()) { auto& c = scene.m_Registry.get<UIItemListComponent>(e); c.ChangedThisFrame = false; c.Hovered = -1; }
            for (auto e : scene.m_Registry.view<UITreeComponent>()) { auto& c = scene.m_Registry.get<UITreeComponent>(e); c.ChangedThisFrame = false; c.Hovered = -1; }
            for (auto e : scene.m_Registry.view<UITextEditComponent>()) scene.m_Registry.get<UITextEditComponent>(e).ChangedThisFrame = false;
            for (auto e : scene.m_Registry.view<UIMenuBarComponent>()) { auto& c = scene.m_Registry.get<UIMenuBarComponent>(e); if (c.Hovered < 1000) c.Hovered = -1; }
            for (auto e : scene.m_Registry.view<UIFileDialogComponent>()) { auto& d = scene.m_Registry.get<UIFileDialogComponent>(e); if (d.FileName.Active) d.FileName.Blink += dt; }
        }

        void FlushEvents(Scene& scene)
        {
            std::vector<PendingSignal> events;
            events.swap(Pending());
            for (auto& ev : events)
            {
                if (!scene.m_Registry.valid((entt::entity)ev.Entity))
                    continue;
                if (!ev.Signal.empty())
                    Signals::Emit(scene, ev.Entity, ev.Signal, ev.Args.data(), (int)ev.Args.size());
                if (!ev.Method.empty() && scene.m_Registry.valid((entt::entity)ev.Entity))
                    scene.InvokeScriptMethod(ev.Entity, ev.Method.c_str(), ev.Args.data(), (int)ev.Args.size());
            }
        }

        // ================================================================ yerleşim
        void LayoutChildren(Entity parent, glm::vec2 parentSize, const std::vector<Entity>& constChildren, std::vector<glm::vec4>& forced, std::vector<bool>& hidden)
        {
            std::vector<Entity> children = constChildren;   // Entity erişimcileri const değil
            if (parent.HasComponent<UITabContainerComponent>())
            {
                auto& tc = parent.GetComponent<UITabContainerComponent>();
                tc.TabCount = (int)children.size();
                tc.CurrentTab = children.empty() ? 0 : std::clamp(tc.CurrentTab, 0, (int)children.size() - 1);
                const float strip = tc.TabsVisible ? tc.TabHeight : 0.0f;
                for (size_t i = 0; i < children.size(); ++i)
                {
                    forced[i] = glm::vec4(0.0f, 0.0f, parentSize.x, std::max(0.0f, parentSize.y - strip));
                    hidden[i] = (int)i != tc.CurrentTab;
                }
            }
            if (parent.HasComponent<UISplitContainerComponent>() && !children.empty())
            {
                auto& sp = parent.GetComponent<UISplitContainerComponent>();
                if (sp.Collapsed || children.size() < 2)
                {
                    forced[0] = glm::vec4(0.0f, 0.0f, parentSize.x, parentSize.y);
                    for (size_t i = 1; i < children.size(); ++i) hidden[i] = sp.Collapsed && i == 1;
                    return;
                }
                const float total = std::max(0.0f, (sp.Vertical ? parentSize.y : parentSize.x) - sp.DraggerThickness);
                const float a = std::clamp(sp.SplitRatio * total, std::min(sp.MinPaneSize, total * 0.5f), std::max(total - sp.MinPaneSize, total * 0.5f));
                if (sp.Vertical)
                {
                    forced[0] = glm::vec4(0.0f, parentSize.y - a, parentSize.x, a);
                    forced[1] = glm::vec4(0.0f, 0.0f, parentSize.x, total - a);
                }
                else
                {
                    forced[0] = glm::vec4(0.0f, 0.0f, a, parentSize.y);
                    forced[1] = glm::vec4(a + sp.DraggerThickness, 0.0f, total - a, parentSize.y);
                }
            }
            if (parent.HasComponent<UIGraphEditComponent>())
                for (size_t i = 0; i < children.size(); ++i)
                    if (children[i].HasComponent<UIGraphNodeComponent>())
                    {
                        const auto& n = children[i].GetComponent<UIGraphNodeComponent>();
                        const glm::vec2 s = glm::max(children[i].GetComponent<RectTransformComponent>().SizeDelta, glm::vec2(40.0f));
                        forced[i] = glm::vec4(n.PositionOffset.x, -(n.PositionOffset.y + s.y), s.x, s.y);
                    }
        }

        bool ChildTransform(Entity parent, glm::vec2 parentSize, glm::mat4& childBase)
        {
            if (!parent.HasComponent<UIGraphEditComponent>())
                return false;
            const auto& g = parent.GetComponent<UIGraphEditComponent>();
            childBase = childBase * glm::translate(glm::mat4(1.0f), glm::vec3(g.ScrollOffset.x, parentSize.y - g.ScrollOffset.y, 0.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(g.Zoom, g.Zoom, 1.0f));
            return true;
        }
    }
}
