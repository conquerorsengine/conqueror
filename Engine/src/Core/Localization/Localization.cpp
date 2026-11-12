#include "Localization.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Renderer/RHI/Texture.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;
        using Table = std::unordered_map<std::string, std::string>;
        using PluralTable = std::unordered_map<std::string, std::vector<std::string>>;

        // gettext: bağlamlı anahtar = bağlam + EOT + msgid
        std::string ContextKey(const std::string& context, const std::string& key)
        {
            return context.empty() ? key : context + '\x04' + key;
        }

        struct Language
        {
            Table Strings;
            PluralTable Plurals;
            std::string PluralRule;   // PO "plural=" ifadesi (boş: n != 1)
            int PluralCount = 2;
        };

        struct State
        {
            LocalizationSettings Settings;
            std::unordered_map<std::string, Language> Tables;   // dil -> çeviriler
            std::string Locale;                                   // etkin
            const Language* Active = nullptr;
            const Language* Fallback = nullptr;
            uint64_t Version = 1;
            bool LocaleOverridden = false;                        // betik SetLocale çağırdı (Reload'da korunur)
            std::unordered_map<std::string, std::string> PseudoCache;
            // Remaps (etkin dilde): asıl dosya (proje göreli ve mutlak yazımlarıyla) -> karşılığı (mutlak)
            bool RemapsActive = false;
            std::unordered_map<std::string, std::string> RemapLookup;
            std::unordered_map<Texture2D*, std::string> RemappedTextures;   // şu an karşılığından yüklü dokular
        };
        State& S() { static auto& s = *new State(); return s; }

        // "tr-TR" / "TR_tr" -> "tr_TR"
        std::string Normalize(const std::string& in)
        {
            std::string s = in;
            const size_t dot = s.find_first_of(".@");
            if (dot != std::string::npos)
                s = s.substr(0, dot);
            std::replace(s.begin(), s.end(), '-', '_');
            const size_t us = s.find('_');
            for (size_t i = 0; i < s.size(); ++i)
                s[i] = (char)(us != std::string::npos && i > us ? std::toupper((unsigned char)s[i]) : std::tolower((unsigned char)s[i]));
            return s;
        }

        std::string LanguageOf(const std::string& locale)
        {
            const size_t us = locale.find('_');
            return us == std::string::npos ? locale : locale.substr(0, us);
        }

        fs::path ProjectDir()
        {
            return Project::GetActiveProjectDirectory();
        }

        fs::path ResolvePath(const std::string& p)
        {
            fs::path path(p);
            if (path.is_relative())
            {
                const auto dir = ProjectDir();
                if (!dir.empty())
                    path = dir / path;
            }
            return path.lexically_normal();
        }

        std::string ReadText(const fs::path& path, bool& ok)
        {
            std::ifstream in(path, std::ios::binary);
            ok = (bool)in;
            if (!in)
                return {};
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        // ------------------------------------------------------------------ Çoğul kuralı (PO Plural-Forms "plural=" ifadesi)
        // C ifadesi: n, tamsayılar, ( ), ! - * / % + < > <= >= == != && || ?:
        class PluralExpr
        {
        public:
            PluralExpr(const std::string& text, int64_t n) : m_Text(text), m_N(n) {}
            bool Evaluate(int64_t& out)
            {
                m_Pos = 0;
                m_Ok = true;
                out = Ternary();
                Skip();
                return m_Ok && m_Pos >= m_Text.size();
            }

        private:
            void Skip() { while (m_Pos < m_Text.size() && std::isspace((unsigned char)m_Text[m_Pos])) ++m_Pos; }
            bool Eat(const char* tok)
            {
                Skip();
                const size_t len = std::char_traits<char>::length(tok);
                if (m_Text.compare(m_Pos, len, tok) == 0)
                {
                    // "<" ile "<=" / "!" ile "!=" karışmasın
                    if (len == 1 && m_Pos + 1 < m_Text.size() && (tok[0] == '<' || tok[0] == '>' || tok[0] == '!' || tok[0] == '=') && m_Text[m_Pos + 1] == '=')
                        return false;
                    if (len == 1 && (tok[0] == '&' || tok[0] == '|'))
                        return false;
                    m_Pos += len;
                    return true;
                }
                return false;
            }
            int64_t Ternary()
            {
                int64_t c = Or();
                if (Eat("?"))
                {
                    int64_t a = Ternary();
                    if (!Eat(":")) { m_Ok = false; return 0; }
                    int64_t b = Ternary();
                    return c ? a : b;
                }
                return c;
            }
            int64_t Or() { int64_t v = And(); while (Eat("||")) { int64_t r = And(); v = (v || r) ? 1 : 0; } return v; }
            int64_t And() { int64_t v = Eq(); while (Eat("&&")) { int64_t r = Eq(); v = (v && r) ? 1 : 0; } return v; }
            int64_t Eq()
            {
                int64_t v = Rel();
                for (;;)
                {
                    if (Eat("==")) v = v == Rel() ? 1 : 0;
                    else if (Eat("!=")) v = v != Rel() ? 1 : 0;
                    else return v;
                }
            }
            int64_t Rel()
            {
                int64_t v = Add();
                for (;;)
                {
                    if (Eat("<=")) v = v <= Add() ? 1 : 0;
                    else if (Eat(">=")) v = v >= Add() ? 1 : 0;
                    else if (Eat("<")) v = v < Add() ? 1 : 0;
                    else if (Eat(">")) v = v > Add() ? 1 : 0;
                    else return v;
                }
            }
            int64_t Add()
            {
                int64_t v = Mul();
                for (;;)
                {
                    if (Eat("+")) v += Mul();
                    else if (Eat("-")) v -= Mul();
                    else return v;
                }
            }
            int64_t Mul()
            {
                int64_t v = Unary();
                for (;;)
                {
                    if (Eat("*")) v *= Unary();
                    else if (Eat("/")) { int64_t d = Unary(); v = d ? v / d : 0; }
                    else if (Eat("%")) { int64_t d = Unary(); v = d ? v % d : 0; }
                    else return v;
                }
            }
            int64_t Unary()
            {
                if (Eat("!")) return Unary() ? 0 : 1;
                if (Eat("-")) return -Unary();
                return Primary();
            }
            int64_t Primary()
            {
                Skip();
                if (Eat("("))
                {
                    int64_t v = Ternary();
                    if (!Eat(")")) m_Ok = false;
                    return v;
                }
                if (m_Pos < m_Text.size() && m_Text[m_Pos] == 'n') { ++m_Pos; return m_N; }
                if (m_Pos < m_Text.size() && std::isdigit((unsigned char)m_Text[m_Pos]))
                {
                    int64_t v = 0;
                    while (m_Pos < m_Text.size() && std::isdigit((unsigned char)m_Text[m_Pos]))
                        v = v * 10 + (m_Text[m_Pos++] - '0');
                    return v;
                }
                m_Ok = false;
                return 0;
            }

            const std::string& m_Text;
            int64_t m_N;
            size_t m_Pos = 0;
            bool m_Ok = true;
        };

        int PluralIndex(const Language& lang, int64_t n)
        {
            int64_t index = n != 1 ? 1 : 0;
            if (!lang.PluralRule.empty())
            {
                int64_t v = 0;
                if (PluralExpr(lang.PluralRule, n).Evaluate(v))
                    index = v;
            }
            return (int)std::clamp<int64_t>(index, 0, std::max(lang.PluralCount, 1) - 1);
        }

        // ------------------------------------------------------------------ CSV
        // RFC 4180 CSV: tırnaklı alanlarda virgül, satır sonu ve "" kaçışı
        std::vector<std::vector<std::string>> ParseCSV(const std::string& text)
        {
            std::vector<std::vector<std::string>> rows;
            std::vector<std::string> row;
            std::string field;
            bool quoted = false;
            size_t i = 0;
            if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
                i = 3;   // UTF-8 BOM
            for (; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quoted)
                {
                    if (c == '"')
                    {
                        if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
                        else quoted = false;
                    }
                    else
                        field += c;
                    continue;
                }
                if (c == '"') quoted = true;
                else if (c == ',') { row.push_back(field); field.clear(); }
                else if (c == '\n' || c == '\r')
                {
                    if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                    row.push_back(field);
                    field.clear();
                    if (!(row.size() == 1 && row[0].empty()))
                        rows.push_back(row);
                    row.clear();
                }
                else field += c;
            }
            if (!field.empty() || !row.empty())
            {
                row.push_back(field);
                rows.push_back(row);
            }
            return rows;
        }

        // Başlık: "keys,en,tr" (Godot); "?context" sütunu varsa satırın bağlamı. '_' ile başlayan sütunlar not
        void LoadCSV(State& s, const std::string& text, const std::string& name)
        {
            const auto rows = ParseCSV(text);
            if (rows.size() < 1 || rows[0].size() < 2)
            {
                CQ_CORE_WARN("[Localization] {0}: expected a header like 'keys,en,tr'", name);
                return;
            }
            const auto& header = rows[0];
            int contextColumn = -1;
            for (size_t c = 1; c < header.size(); ++c)
                if (header[c] == "?context")
                    contextColumn = (int)c;
            for (size_t r = 1; r < rows.size(); ++r)
            {
                const auto& row = rows[r];
                if (row.empty() || row[0].empty())
                    continue;
                const std::string context = contextColumn > 0 && contextColumn < (int)row.size() ? row[(size_t)contextColumn] : std::string();
                const std::string key = ContextKey(context, row[0]);
                for (size_t c = 1; c < header.size() && c < row.size(); ++c)
                {
                    if ((int)c == contextColumn || header[c].empty() || header[c][0] == '_' || header[c][0] == '?' || row[c].empty())
                        continue;
                    s.Tables[Normalize(header[c])].Strings[key] = row[c];
                }
            }
        }

        // ------------------------------------------------------------------ PO
        // "..." parçasını çöz (\n, \t, \", \\ kaçışları)
        std::string Unquote(const std::string& line)
        {
            const size_t a = line.find('"'), b = line.rfind('"');
            if (a == std::string::npos || b <= a)
                return {};
            std::string out;
            for (size_t i = a + 1; i < b; ++i)
            {
                if (line[i] == '\\' && i + 1 < b)
                {
                    const char n = line[++i];
                    out += n == 'n' ? '\n' : n == 't' ? '\t' : n == 'r' ? '\r' : n;
                }
                else
                    out += line[i];
            }
            return out;
        }

        std::string HeaderField(const std::string& header, const std::string& name)
        {
            const size_t p = header.find(name + ":");
            if (p == std::string::npos)
                return {};
            size_t e = header.find('\n', p);
            std::string v = header.substr(p + name.size() + 1, e == std::string::npos ? std::string::npos : e - p - name.size() - 1);
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t\r") + 1);
            return v;
        }

        void LoadPO(State& s, const std::string& text, const fs::path& path)
        {
            std::istringstream in(text);
            std::string line, msgid, msgidPlural, ctxt, header;
            std::map<int, std::string> msgstr;
            int mode = 0;   // 1 msgid, 2 msgid_plural, 3 msgstr (index: current)
            int current = 0;
            struct Entry { std::string Key; std::map<int, std::string> Forms; bool Plural; };
            std::vector<Entry> entries;
            auto flush = [&]() {
                if (mode == 3)
                {
                    if (msgid.empty() && ctxt.empty())
                        header = msgstr.count(0) ? msgstr[0] : std::string();
                    else
                    {
                        bool any = false;
                        for (auto& [i, v] : msgstr)
                            any = any || !v.empty();
                        if (any)
                            entries.push_back({ ContextKey(ctxt, msgid), msgstr, !msgidPlural.empty() });
                    }
                }
                msgid.clear();
                msgidPlural.clear();
                msgstr.clear();
                ctxt.clear();
                mode = 0;
            };
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos) { flush(); continue; }
                line = line.substr(first);
                if (line[0] == '#') continue;
                if (line.rfind("msgctxt", 0) == 0) { if (mode == 3) flush(); ctxt = Unquote(line); mode = 0; continue; }
                if (line.rfind("msgid_plural", 0) == 0) { msgidPlural = Unquote(line); mode = 2; continue; }
                if (line.rfind("msgid", 0) == 0) { if (mode == 3) flush(); msgid = Unquote(line); mode = 1; continue; }
                if (line.rfind("msgstr", 0) == 0)
                {
                    current = 0;
                    if (line.size() > 6 && line[6] == '[')
                        current = std::atoi(line.c_str() + 7);
                    msgstr[current] = Unquote(line);
                    mode = 3;
                    continue;
                }
                if (line[0] == '"')
                {
                    if (mode == 1) msgid += Unquote(line);
                    else if (mode == 2) msgidPlural += Unquote(line);
                    else if (mode == 3) msgstr[current] += Unquote(line);
                }
            }
            flush();
            std::string locale = HeaderField(header, "Language");
            if (locale.empty())
                locale = path.stem().string();   // "tr.po"
            Language& lang = s.Tables[Normalize(locale)];
            // "Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : ...);"
            const std::string forms = HeaderField(header, "Plural-Forms");
            if (!forms.empty())
            {
                const size_t np = forms.find("nplurals=");
                if (np != std::string::npos)
                    lang.PluralCount = std::max(1, std::atoi(forms.c_str() + np + 9));
                const size_t pl = forms.find("plural=");
                if (pl != std::string::npos)
                {
                    std::string rule = forms.substr(pl + 7);
                    const size_t semi = rule.rfind(';');
                    if (semi != std::string::npos)
                        rule = rule.substr(0, semi);
                    lang.PluralRule = rule;
                }
            }
            for (auto& e : entries)
            {
                if (e.Plural)
                {
                    std::vector<std::string> v;
                    for (auto& [i, f] : e.Forms)
                    {
                        if (i < 0 || i > 16) continue;
                        if ((int)v.size() <= i) v.resize((size_t)i + 1);
                        v[(size_t)i] = f;
                    }
                    lang.Plurals[e.Key] = v;
                    if (!v.empty() && !v[0].empty())
                        lang.Strings[e.Key] = v[0];
                }
                else if (e.Forms.count(0))
                    lang.Strings[e.Key] = e.Forms[0];
            }
        }

        bool LocaleAllowed(const State& s, const std::string& locale)
        {
            if (s.Settings.LocaleFilter.empty())
                return true;
            for (const auto& f : s.Settings.LocaleFilter)
            {
                const std::string n = Normalize(f);
                if (n == locale || n == LanguageOf(locale))
                    return true;
            }
            return false;
        }

        void BuildRemapLookup(State& s)
        {
            s.RemapLookup.clear();
            if (s.Settings.Remaps.empty())
                return;
            const std::string locale = s.Locale, lang = LanguageOf(s.Locale);
            for (const auto& r : s.Settings.Remaps)
            {
                if (r.Resource.empty())
                    continue;
                // Tam dil, yoksa yalnız dil kodu ("pt_BR" -> "pt")
                const std::string* target = nullptr;
                for (const auto& [loc, file] : r.Locales)
                    if (Normalize(loc) == locale && !file.empty()) { target = &file; break; }
                if (!target)
                    for (const auto& [loc, file] : r.Locales)
                        if (Normalize(loc) == lang && !file.empty()) { target = &file; break; }
                if (!target)
                    continue;
                const std::string abs = ResolvePath(*target).string();
                const fs::path resAbs = ResolvePath(r.Resource);
                s.RemapLookup[fs::path(r.Resource).generic_string()] = abs;
                s.RemapLookup[resAbs.string()] = abs;
                s.RemapLookup[resAbs.generic_string()] = abs;
                std::error_code ec;
                const fs::path canon = fs::weakly_canonical(resAbs, ec);
                if (!ec)
                    s.RemapLookup[canon.string()] = abs;
            }
        }

        void Choose(State& s)
        {
            s.Active = s.Fallback = nullptr;
            std::string want = !s.Settings.TestLocale.empty() && !s.LocaleOverridden ? s.Settings.TestLocale
                             : !s.Locale.empty() && s.LocaleOverridden ? s.Locale
                             : !s.Settings.Locale.empty() ? s.Settings.Locale : Localization::SystemLocale();
            want = Normalize(want);
            std::string chosen;
            auto usable = [&](const std::string& loc) { return s.Tables.count(loc) && LocaleAllowed(s, loc); };
            if (usable(want))
                chosen = want;
            else
            {
                const std::string lang = LanguageOf(want);
                if (usable(lang))
                    chosen = lang;
                else
                    for (const auto& [loc, table] : s.Tables)
                        if (LanguageOf(loc) == lang && LocaleAllowed(s, loc)) { chosen = loc; break; }
            }
            const std::string fallback = Normalize(s.Settings.Fallback);
            if (chosen.empty() && usable(fallback))
                chosen = fallback;
            if (chosen.empty())
                for (const auto& [loc, table] : s.Tables)
                    if (LocaleAllowed(s, loc)) { chosen = loc; break; }
            s.Locale = chosen.empty() ? want : chosen;
            if (auto it = s.Tables.find(chosen); it != s.Tables.end())
                s.Active = &it->second;
            if (auto it = s.Tables.find(fallback); it != s.Tables.end() && &it->second != s.Active)
                s.Fallback = &it->second;
            s.PseudoCache.clear();
            BuildRemapLookup(s);
            ++s.Version;
        }

        const std::string* Lookup(const State& s, const std::string& key)
        {
            if (s.Active)
                if (auto it = s.Active->Strings.find(key); it != s.Active->Strings.end())
                    return &it->second;
            if (s.Fallback)
                if (auto it = s.Fallback->Strings.find(key); it != s.Fallback->Strings.end())
                    return &it->second;
            return nullptr;
        }

        const std::string& Finish(const std::string& text)
        {
            State& s = S();
            if (!s.Settings.Pseudo.Enabled || text.empty())
                return text;
            auto it = s.PseudoCache.find(text);
            if (it == s.PseudoCache.end())
                it = s.PseudoCache.emplace(text, Localization::Pseudolocalize(text)).first;
            return it->second;
        }

        // UTF-8 kod noktalarına böl
        std::vector<std::string> SplitUTF8(const std::string& s)
        {
            std::vector<std::string> out;
            for (size_t i = 0; i < s.size();)
            {
                const unsigned char c = (unsigned char)s[i];
                const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
                out.push_back(s.substr(i, len));
                i += len;
            }
            return out;
        }

        const char* Accented(char c)
        {
            switch (c)
            {
                case 'a': return "\xC3\xA5"; case 'b': return "\xC6\x80"; case 'c': return "\xC3\xA7"; case 'd': return "\xC3\xB0";
                case 'e': return "\xC3\xA9"; case 'f': return "\xC6\x92"; case 'g': return "\xC4\x9D"; case 'h': return "\xC4\xA5";
                case 'i': return "\xC3\xAE"; case 'j': return "\xC4\xB5"; case 'k': return "\xC4\xB7"; case 'l': return "\xC4\xBC";
                case 'n': return "\xC3\xB1"; case 'o': return "\xC3\xB6"; case 'r': return "\xC5\x95"; case 's': return "\xC5\xA1";
                case 't': return "\xC5\xA3"; case 'u': return "\xC3\xBC"; case 'w': return "\xC5\xB5"; case 'y': return "\xC3\xBD";
                case 'z': return "\xC5\xBE";
                case 'A': return "\xC3\x85"; case 'C': return "\xC3\x87"; case 'D': return "\xC3\x90"; case 'E': return "\xC3\x89";
                case 'G': return "\xC4\x9C"; case 'H': return "\xC4\xA4"; case 'I': return "\xC3\x8E"; case 'J': return "\xC4\xB4";
                case 'K': return "\xC4\xB6"; case 'L': return "\xC4\xBB"; case 'N': return "\xC3\x91"; case 'O': return "\xC3\x96";
                case 'R': return "\xC5\x94"; case 'S': return "\xC5\xA0"; case 'T': return "\xC5\xA2"; case 'U': return "\xC3\x9C";
                case 'W': return "\xC5\xB4"; case 'Y': return "\xC3\x9D"; case 'Z': return "\xC5\xBD";
                default: return nullptr;
            }
        }

        // Yer tutucu uzunluğu (%s, %d, %5.2f, %%, {0}, {name}); değilse 0
        size_t PlaceholderAt(const std::string& s, size_t i)
        {
            if (s[i] == '%' && i + 1 < s.size())
            {
                if (s[i + 1] == '%') return 2;
                size_t j = i + 1;
                while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == '-' || s[j] == '+')) ++j;
                if (j < s.size() && std::isalpha((unsigned char)s[j])) return j - i + 1;
                return 0;
            }
            if (s[i] == '{')
            {
                const size_t e = s.find('}', i);
                if (e != std::string::npos && e - i <= 32) return e - i + 1;
            }
            return 0;
        }

        std::string EscapePO(const std::string& s)
        {
            std::string out;
            for (char c : s)
            {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c == '\t') out += "\\t";
                else if (c != '\r') out += c;
            }
            return out;
        }

        // Betikteki "..." dizgisinin kaçışlarını çöz
        std::string UnescapeLiteral(const std::string& s)
        {
            std::string out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    const char n = s[++i];
                    out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
                }
                else
                    out += s[i];
            }
            return out;
        }
    }

    namespace Localization
    {
        LocalizationSettings& Settings() { return S().Settings; }
        uint64_t GetVersion() { return S().Version; }
        const std::string& GetLocale() { return S().Locale; }

        std::string SystemLocale()
        {
#ifdef _WIN32
            wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
            if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
            {
                std::string out;
                for (const wchar_t* p = name; *p; ++p) out += (char)*p;
                return Normalize(out);
            }
#endif
            for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" })
                if (const char* v = std::getenv(var); v && *v && std::string(v) != "C" && std::string(v) != "POSIX")
                    return Normalize(v);
            return "en";
        }

        void Reload()
        {
            State& s = S();
            s.Tables.clear();
            for (const std::string& file : s.Settings.Translations)
            {
                const fs::path path = ResolvePath(file);
                bool ok = false;
                const std::string text = ReadText(path, ok);
                if (!ok)
                {
                    CQ_CORE_WARN("[Localization] translation file not found: {0}", file);
                    continue;
                }
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (ext == ".po" || ext == ".pot")
                    LoadPO(s, text, path);
                else
                    LoadCSV(s, text, file);
            }
            Choose(s);
            ApplyTextureRemaps();
        }

        const std::string& Translate(const std::string& key)
        {
            const State& s = S();
            if (key.empty())
                return key;
            const std::string* t = (s.Active || s.Fallback) ? Lookup(s, key) : nullptr;
            return Finish(t ? *t : key);
        }

        const std::string& Translate(const std::string& key, const std::string& context)
        {
            if (context.empty())
                return Translate(key);
            const State& s = S();
            const std::string* t = (s.Active || s.Fallback) ? Lookup(s, ContextKey(context, key)) : nullptr;
            return Finish(t ? *t : key);
        }

        const std::string& TranslatePlural(const std::string& key, const std::string& pluralKey, int64_t n, const std::string& context)
        {
            const State& s = S();
            const std::string full = ContextKey(context, key);
            for (const Language* lang : { s.Active, s.Fallback })
            {
                if (!lang)
                    continue;
                auto it = lang->Plurals.find(full);
                if (it == lang->Plurals.end() || it->second.empty())
                    continue;
                const int index = PluralIndex(*lang, n);
                if (index < (int)it->second.size() && !it->second[(size_t)index].empty())
                    return Finish(it->second[(size_t)index]);
                return Finish(it->second[0]);
            }
            return Finish(n == 1 ? key : pluralKey);
        }

        void SetLocale(const std::string& locale)
        {
            State& s = S();
            s.Locale = locale;
            s.LocaleOverridden = !locale.empty();
            Choose(s);
            ApplyTextureRemaps();
        }

        std::vector<std::string> GetLoadedLocales()
        {
            std::vector<std::string> out;
            const State& s = S();
            for (const auto& [loc, table] : s.Tables)
                if (LocaleAllowed(s, loc))
                    out.push_back(loc);
            std::sort(out.begin(), out.end());
            return out;
        }

        int GetKeyCount()
        {
            const State& s = S();
            return s.Active ? (int)(s.Active->Strings.size()) : 0;
        }

        void PseudolocalizationChanged()
        {
            S().PseudoCache.clear();
            ++S().Version;
        }

        std::string Pseudolocalize(const std::string& text)
        {
            const PseudolocalizationSettings& p = S().Settings.Pseudo;
            std::string body;
            size_t letters = 0;
            for (size_t i = 0; i < text.size();)
            {
                if (p.SkipPlaceholders)
                    if (const size_t ph = PlaceholderAt(text, i))
                    {
                        body += text.substr(i, ph);
                        i += ph;
                        continue;
                    }
                const unsigned char c = (unsigned char)text[i];
                if (c >= 0x80)
                {
                    // ASCII dışı karakter olduğu gibi (çok baytlı)
                    const size_t len = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
                    body += p.Override && c >= 0xC0 ? std::string("*") : text.substr(i, len);
                    i += len;
                    continue;
                }
                const char ch = (char)c;
                ++i;
                if (p.Override && std::isalnum((unsigned char)ch)) { body += '*'; ++letters; continue; }
                const bool vowel = std::strchr("aeiouAEIOU", ch) != nullptr;
                const char* acc = p.Accents ? Accented(ch) : nullptr;
                const std::string glyph = acc ? std::string(acc) : std::string(1, ch);
                body += glyph;
                if (p.DoubleVowels && vowel)
                    body += glyph;
                if (std::isalpha((unsigned char)ch))
                    ++letters;
            }
            if (p.ExpansionRatio > 0.0f)
            {
                const size_t extra = (size_t)std::ceil((double)std::max<size_t>(letters, 1) * std::min(p.ExpansionRatio, 10.0f));
                body += std::string(extra, '_');
            }
            if (p.FakeBiDi)
            {
                // Sağdan sola dilleri taklit: karakterler ters sırada yazılır (arayüzün sağdan sola metne dayanıklılığını görmek için)
                auto cps = SplitUTF8(body);
                std::reverse(cps.begin(), cps.end());
                body.clear();
                for (auto& cp : cps) body += cp;
            }
            return p.Prefix + body + p.Suffix;
        }

        void SetRemapsActive(bool active)
        {
            State& s = S();
            if (s.RemapsActive == active)
                return;
            s.RemapsActive = active;
            ApplyTextureRemaps();
        }

        bool RemapsActive() { return S().RemapsActive; }

        std::string Remap(const std::string& path)
        {
            const State& s = S();
            if (!s.RemapsActive || s.RemapLookup.empty() || path.empty())
                return path;
            if (auto it = s.RemapLookup.find(path); it != s.RemapLookup.end())
                return it->second;
            const std::string generic = fs::path(path).lexically_normal().generic_string();
            if (auto it = s.RemapLookup.find(generic); it != s.RemapLookup.end())
                return it->second;
            return path;
        }

        void ApplyTextureRemaps()
        {
            State& s = S();
            // Yüklü dokular: karşılığı olan karşılığından, olmayan (ya da Remaps kapalıyken) asıl dosyasından
            std::vector<std::shared_ptr<Texture2D>> live;
            for (auto& [key, weak] : Texture2D::GetCache())
                if (auto tex = weak.lock())
                    live.push_back(tex);
            std::unordered_map<Texture2D*, std::string> still;
            for (auto& tex : live)
            {
                const std::string& path = tex->GetPath();
                const std::string target = s.RemapsActive ? Remap(path) : path;
                auto it = s.RemappedTextures.find(tex.get());
                const bool remapped = it != s.RemappedTextures.end();
                if (target != path)
                {
                    if (!remapped || it->second != target)
                    {
                        if (tex->ReloadFrom(target))
                            CQ_CORE_INFO("[Localization] {0} -> {1} ({2})", fs::path(path).filename().string(), fs::path(target).filename().string(), s.Locale);
                    }
                    still[tex.get()] = target;
                }
                else if (remapped)
                    tex->Reload();
            }
            s.RemappedTextures.swap(still);
        }

        void OnTextureLoaded(Texture2D& texture)
        {
            State& s = S();
            if (!s.RemapsActive || s.RemapLookup.empty())
                return;
            const std::string& path = texture.GetPath();
            const std::string target = Remap(path);
            if (target == path)
                return;
            if (texture.ReloadFrom(target))
                s.RemappedTextures[&texture] = target;
        }

        int GeneratePOT(const std::string& outPath, const std::vector<std::string>& files)
        {
            // msgctxt + msgid -> (çoğul, kaynak dosyalar)
            struct Item { std::string Context, Id, Plural; std::set<std::string> Sources; };
            std::map<std::pair<std::string, std::string>, Item> items;
            auto add = [&](const std::string& ctx, const std::string& id, const std::string& plural, const std::string& source) {
                if (id.empty())
                    return;
                auto& it = items[{ ctx, id }];
                it.Context = ctx;
                it.Id = id;
                if (!plural.empty())
                    it.Plural = plural;
                it.Sources.insert(source);
            };

            const fs::path dir = ProjectDir();
            std::vector<fs::path> list;
            if (!files.empty())
            {
                for (const auto& f : files)
                    list.push_back(ResolvePath(f));
            }
            else if (!dir.empty())
            {
                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(dir / "Assets", fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file(ec))
                        list.push_back(it->path());
            }

            static const std::regex trCall(R"re((?:\bTr|\btr)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*(?:,\s*"((?:[^"\\]|\\.)*)"\s*)?\))re");
            static const std::regex trnCall(R"re((?:\bTrN|\btr_n)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,[^,)]*(?:,\s*"((?:[^"\\]|\\.)*)"\s*)?\))re");
            for (const fs::path& path : list)
            {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                std::error_code ec;
                fs::path rel = dir.empty() ? path : fs::relative(path, dir, ec);
                if (ec || rel.empty())
                    rel = path;
                const std::string source = rel.generic_string();
                bool ok = false;
                if (ext == ".cqscene" || ext == ".cqprefab")
                {
                    const std::string text = ReadText(path, ok);
                    if (!ok)
                        continue;
                    try
                    {
                        YAML::Node root = YAML::Load(text);
                        const YAML::Node entities = root["Entities"];
                        if (!entities || !entities.IsSequence())
                            continue;
                        for (const auto& e : entities)
                        {
                            if (auto t = e["UITextComponent"])
                                if (t["Text"] && !(t["AutoTranslate"] && !t["AutoTranslate"].as<bool>()))
                                    add({}, t["Text"].as<std::string>(), {}, source);
                            if (auto t = e["TextRendererComponent"])
                                if (t["Text"] && !(t["AutoTranslate"] && !t["AutoTranslate"].as<bool>()))
                                    add({}, t["Text"].as<std::string>(), {}, source);
                            if (auto d = e["UIDropdownComponent"])
                                if (d["Options"] && d["Options"].IsSequence() && !(d["AutoTranslate"] && !d["AutoTranslate"].as<bool>()))
                                    for (const auto& o : d["Options"])
                                        add({}, o.as<std::string>(), {}, source);
                        }
                    }
                    catch (const YAML::Exception& ex)
                    {
                        CQ_CORE_WARN("[Localization] POT: could not read {0}: {1}", source, ex.what());
                    }
                }
                else if (ext == ".cqs" || ext == ".py" || ext == ".lua" || ext == ".cs" || ext == ".js" || ext == ".luaq" || ext == ".gd")
                {
                    const std::string text = ReadText(path, ok);
                    if (!ok)
                        continue;
                    for (auto it = std::sregex_iterator(text.begin(), text.end(), trnCall); it != std::sregex_iterator(); ++it)
                        add((*it)[3].matched ? UnescapeLiteral((*it)[3].str()) : std::string(), UnescapeLiteral((*it)[1].str()),
                            UnescapeLiteral((*it)[2].str()), source);
                    for (auto it = std::sregex_iterator(text.begin(), text.end(), trCall); it != std::sregex_iterator(); ++it)
                        add((*it)[2].matched ? UnescapeLiteral((*it)[2].str()) : std::string(), UnescapeLiteral((*it)[1].str()), {}, source);
                }
            }

            const fs::path out = ResolvePath(outPath);
            std::error_code ec;
            if (out.has_parent_path())
                fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            if (!f)
            {
                CQ_CORE_ERROR("[Localization] cannot write POT file {0}", out.string());
                return -1;
            }
            f << "# Translation template generated by Conqueror Engine.\n";
            f << "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Content-Transfer-Encoding: 8-bit\\n\"\n\n";
            for (const auto& [k, it] : items)
            {
                f << "#:";
                for (const auto& src : it.Sources)
                    f << ' ' << src;
                f << '\n';
                if (!it.Context.empty())
                    f << "msgctxt \"" << EscapePO(it.Context) << "\"\n";
                f << "msgid \"" << EscapePO(it.Id) << "\"\n";
                if (!it.Plural.empty())
                    f << "msgid_plural \"" << EscapePO(it.Plural) << "\"\nmsgstr[0] \"\"\nmsgstr[1] \"\"\n\n";
                else
                    f << "msgstr \"\"\n\n";
            }
            CQ_CORE_INFO("[Localization] POT written: {0} ({1} strings)", out.string(), items.size());
            return (int)items.size();
        }

        void Serialize(YAML::Emitter& out)
        {
            const auto& st = S().Settings;
            out << YAML::Key << "Localization" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Translations" << YAML::Value << YAML::Flow << st.Translations;
            out << YAML::Key << "Locale" << YAML::Value << st.Locale;
            out << YAML::Key << "Fallback" << YAML::Value << st.Fallback;
            out << YAML::Key << "TestLocale" << YAML::Value << st.TestLocale;
            if (!st.LocaleFilter.empty())
                out << YAML::Key << "LocaleFilter" << YAML::Value << YAML::Flow << st.LocaleFilter;
            if (!st.Remaps.empty())
            {
                out << YAML::Key << "Remaps" << YAML::Value << YAML::BeginSeq;
                for (const auto& r : st.Remaps)
                {
                    out << YAML::BeginMap << YAML::Key << "Resource" << YAML::Value << r.Resource;
                    out << YAML::Key << "Locales" << YAML::Value << YAML::BeginMap;
                    for (const auto& [loc, file] : r.Locales)
                        out << YAML::Key << loc << YAML::Value << file;
                    out << YAML::EndMap << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            const auto& p = st.Pseudo;
            out << YAML::Key << "Pseudolocalization" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Enabled" << YAML::Value << p.Enabled;
            out << YAML::Key << "Accents" << YAML::Value << p.Accents;
            out << YAML::Key << "DoubleVowels" << YAML::Value << p.DoubleVowels;
            out << YAML::Key << "FakeBiDi" << YAML::Value << p.FakeBiDi;
            out << YAML::Key << "Override" << YAML::Value << p.Override;
            out << YAML::Key << "ExpansionRatio" << YAML::Value << p.ExpansionRatio;
            out << YAML::Key << "Prefix" << YAML::Value << p.Prefix;
            out << YAML::Key << "Suffix" << YAML::Value << p.Suffix;
            out << YAML::Key << "SkipPlaceholders" << YAML::Value << p.SkipPlaceholders;
            out << YAML::EndMap;
            if (!st.POTFiles.empty())
                out << YAML::Key << "POTFiles" << YAML::Value << YAML::Flow << st.POTFiles;
            out << YAML::EndMap;
        }

        void Deserialize(const YAML::Node& node)
        {
            auto& st = S().Settings;
            st = LocalizationSettings{};
            auto strings = [](const YAML::Node& n, std::vector<std::string>& out) {
                if (n && n.IsSequence())
                    for (const auto& v : n)
                    {
                        try { out.push_back(v.as<std::string>()); }
                        catch (const YAML::Exception&) {}
                    }
            };
            if (auto l = node["Localization"])
            {
                strings(l["Translations"], st.Translations);
                strings(l["LocaleFilter"], st.LocaleFilter);
                strings(l["POTFiles"], st.POTFiles);
                try
                {
                    if (l["Locale"]) st.Locale = l["Locale"].as<std::string>();
                    if (l["Fallback"]) st.Fallback = l["Fallback"].as<std::string>();
                    if (l["TestLocale"]) st.TestLocale = l["TestLocale"].as<std::string>();
                    if (auto r = l["Remaps"]; r && r.IsSequence())
                        for (const auto& e : r)
                        {
                            LocalizationRemap remap;
                            if (e["Resource"]) remap.Resource = e["Resource"].as<std::string>();
                            if (auto locs = e["Locales"]; locs && locs.IsMap())
                                for (const auto& kv : locs)
                                    remap.Locales.push_back({ kv.first.as<std::string>(), kv.second.as<std::string>() });
                            st.Remaps.push_back(remap);
                        }
                    if (auto p = l["Pseudolocalization"])
                    {
                        auto& ps = st.Pseudo;
                        if (p["Enabled"]) ps.Enabled = p["Enabled"].as<bool>();
                        if (p["Accents"]) ps.Accents = p["Accents"].as<bool>();
                        if (p["DoubleVowels"]) ps.DoubleVowels = p["DoubleVowels"].as<bool>();
                        if (p["FakeBiDi"]) ps.FakeBiDi = p["FakeBiDi"].as<bool>();
                        if (p["Override"]) ps.Override = p["Override"].as<bool>();
                        if (p["ExpansionRatio"]) ps.ExpansionRatio = std::clamp(p["ExpansionRatio"].as<float>(), 0.0f, 10.0f);
                        if (p["Prefix"]) ps.Prefix = p["Prefix"].as<std::string>();
                        if (p["Suffix"]) ps.Suffix = p["Suffix"].as<std::string>();
                        if (p["SkipPlaceholders"]) ps.SkipPlaceholders = p["SkipPlaceholders"].as<bool>();
                    }
                }
                catch (const YAML::Exception&) {}
            }
            S().LocaleOverridden = false;
            // Dosyalar proje etkinleşince yüklenir (Project::SetActive: proje klasörü o zaman belli)
            S().Tables.clear();
            Choose(S());
        }

        void ResetToDefaults()
        {
            S().Settings = LocalizationSettings{};
            S().LocaleOverridden = false;
            S().Tables.clear();
            Choose(S());
        }
    }
}
