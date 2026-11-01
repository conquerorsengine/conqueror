#include "DataTable.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        struct CacheEntry
        {
            DataTable Table;
            std::filesystem::file_time_type Stamp{};
        };

        std::mutex s_Mutex;
        std::unordered_map<std::string, CacheEntry> s_Cache;

        std::filesystem::path Resolve(const std::string& path)
        {
            std::filesystem::path p(path);
            std::error_code ec;
            if (p.is_absolute() || std::filesystem::exists(p, ec))
                return p;
            auto dir = Project::GetActiveProjectDirectory();
            if (!dir.empty() && std::filesystem::exists(dir / p, ec))
                return dir / p;
            return p;
        }

        void BuildIndex(DataTable& t)
        {
            t.Index.clear();
            for (size_t r = 0; r < t.Rows.size(); ++r)
                if (!t.Rows[r].empty())
                    t.Index.emplace(Lower(t.Rows[r][0].Text), r);
        }

        // CSV satırını ayır: tırnaklı alanlar ("a, b"), "" kaçış
        std::vector<std::string> SplitCSV(const std::string& line, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            bool quoted = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                char c = line[i];
                if (quoted)
                {
                    if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                    else if (c == '"') quoted = false;
                    else cur += c;
                }
                else if (c == '"') quoted = true;
                else if (c == sep) { out.push_back(cur); cur.clear(); }
                else cur += c;
            }
            out.push_back(cur);
            return out;
        }

        std::string ScalarText(const YAML::Node& n)
        {
            if (!n || n.IsNull()) return "";
            if (n.IsScalar()) return n.Scalar();
            YAML::Emitter e;
            e << YAML::Flow << n;
            return e.c_str();
        }
    }

    int DataTable::ColumnIndex(const std::string& name) const
    {
        const std::string l = Lower(name);
        for (size_t i = 0; i < Columns.size(); ++i)
            if (Lower(Columns[i]) == l) return (int)i;
        return -1;
    }

    const std::vector<DataTableCell>* DataTable::FindRow(const std::string& key) const
    {
        auto it = Index.find(Lower(key));
        return it != Index.end() ? &Rows[it->second] : nullptr;
    }

    namespace DataTables
    {
        DataTableCell MakeCell(const std::string& raw)
        {
            DataTableCell c;
            c.Text = Trim(raw);
            if (c.Text.empty()) { c.Type = DataTableCell::Kind::Empty; return c; }
            const std::string l = Lower(c.Text);
            if (l == "true" || l == "false") { c.Type = DataTableCell::Kind::Bool; c.Bool = l == "true"; return c; }
            try
            {
                size_t used = 0;
                double d = std::stod(c.Text, &used);
                if (used == c.Text.size()) { c.Type = DataTableCell::Kind::Number; c.Number = d; return c; }
            }
            catch (...) {}
            c.Type = DataTableCell::Kind::Text;
            return c;
        }

        bool ParseCSV(const std::string& text, DataTable& out)
        {
            out = DataTable{};
            std::istringstream in(text);
            std::string line;
            std::vector<std::string> lines;
            // Tırnak içinde satır sonu olabilir: tırnaklar kapanana kadar birleştir
            std::string pending;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                pending += (pending.empty() ? "" : "\n") + line;
                if (std::count(pending.begin(), pending.end(), '"') % 2 == 0)
                {
                    if (!Trim(pending).empty() && Trim(pending)[0] != '#') lines.push_back(pending);
                    pending.clear();
                }
            }
            if (lines.empty()) { out.Error = "empty table"; return false; }
            // Ayırıcı: başlıkta noktalı virgül virgülden çoksa ';'
            const std::string& header = lines[0];
            char sep = std::count(header.begin(), header.end(), ';') > std::count(header.begin(), header.end(), ',') ? ';' : ',';
            for (auto& h : SplitCSV(header, sep))
                out.Columns.push_back(Trim(h));
            for (size_t i = 1; i < lines.size(); ++i)
            {
                auto fields = SplitCSV(lines[i], sep);
                std::vector<DataTableCell> row;
                for (size_t c = 0; c < out.Columns.size(); ++c)
                    row.push_back(MakeCell(c < fields.size() ? fields[c] : std::string()));
                out.Rows.push_back(std::move(row));
            }
            BuildIndex(out);
            return true;
        }

        bool ParseStructured(const std::string& text, DataTable& out)
        {
            out = DataTable{};
            YAML::Node root;
            try { root = YAML::Load(text); }
            catch (const YAML::Exception& e) { out.Error = e.what(); return false; }
            // Satırlar: [ {..}, {..} ] ya da { anahtar: {..} }
            std::vector<std::pair<std::string, YAML::Node>> rows;
            if (root.IsSequence())
            {
                for (const auto& n : root)
                {
                    if (!n.IsMap()) continue;
                    std::string key;
                    for (const char* k : { "Name", "name", "Id", "id", "ID", "Key", "key" })
                        if (n[k]) { key = ScalarText(n[k]); break; }
                    if (key.empty() && n.size() > 0) key = ScalarText(n.begin()->second);
                    rows.emplace_back(key, n);
                }
            }
            else if (root.IsMap())
            {
                for (const auto& kv : root)
                    if (kv.second.IsMap()) rows.emplace_back(ScalarText(kv.first), kv.second);
            }
            else { out.Error = "expected a list of rows or a map of rows"; return false; }

            out.Columns.push_back("Name");
            for (auto& [key, n] : rows)
                for (const auto& kv : n)
                {
                    std::string col = ScalarText(kv.first);
                    if (Lower(col) == "name" || Lower(col) == "id" || Lower(col) == "key") continue;
                    if (out.ColumnIndex(col) < 0) out.Columns.push_back(col);
                }
            for (auto& [key, n] : rows)
            {
                std::vector<DataTableCell> row;
                row.push_back(MakeCell(key));
                for (size_t c = 1; c < out.Columns.size(); ++c)
                {
                    YAML::Node v;
                    for (const auto& kv : n)
                        if (Lower(ScalarText(kv.first)) == Lower(out.Columns[c])) { v = kv.second; break; }
                    row.push_back(MakeCell(ScalarText(v)));
                }
                out.Rows.push_back(std::move(row));
            }
            BuildIndex(out);
            return true;
        }

        const DataTable& Load(const std::string& path)
        {
            std::lock_guard lock(s_Mutex);
            std::filesystem::path file = Resolve(path);
            std::error_code ec;
            auto stamp = std::filesystem::last_write_time(file, ec);
            auto& entry = s_Cache[path];
            if (!ec && entry.Stamp == stamp && entry.Table.Error.empty() && !entry.Table.Columns.empty())
                return entry.Table;
            entry.Stamp = ec ? std::filesystem::file_time_type{} : stamp;
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                entry.Table = DataTable{};
                entry.Table.Error = "cannot open " + path;
                return entry.Table;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string ext = Lower(file.extension().string());
            bool ok = ext == ".json" || ext == ".yaml" || ext == ".yml" ? ParseStructured(ss.str(), entry.Table) : ParseCSV(ss.str(), entry.Table);
            if (!ok)
                CQ_CORE_WARN("[DataTable] {0}: {1}", path, entry.Table.Error);
            return entry.Table;
        }

        void ClearCache()
        {
            std::lock_guard lock(s_Mutex);
            s_Cache.clear();
        }
    }
}
