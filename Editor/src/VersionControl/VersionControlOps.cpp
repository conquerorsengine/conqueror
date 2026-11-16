// Version Control penceresinin git / p4 işlemleri (Unity Version Control, Unreal Source Control karşılığı). Hepsi eşzamanlı ve
// iş parçacığı güvenli: yalnız verilen Context'i kullanır (proje / ayar okumaz, ImGui'ye dokunmaz). Git komutları depo kökünde, depo
// köküne göre yollarla çalışır (porcelain / diff-tree çıktıları da köke göredir); Perforce komutları proje klasöründe, yerel mutlak
// yollarla. Dosya listeleri geçici dosyadan verilir (git --pathspec-from-file, p4 -x): uzun listeler komut satırı sınırına takılmaz.
#include "VersionControl.h"
#include "VersionControlDetail.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace Conqueror::Editor::VersionControl
{
    namespace fs = std::filesystem;
    using Detail::Quote;
    using Detail::RunSplit;
    using Detail::TempFile;
    using Detail::Trim;

    namespace
    {
        // Renksiz, tırnaksız yol, sayfalayıcısız, isteğe bağlı kilitsiz (arka plan durumu ile çakışmasın), yollar desen değil düz ad
        const std::string kGit = "git -c core.quotepath=off -c color.ui=false -c core.pager=cat --no-optional-locks --literal-pathspecs";
        constexpr size_t kMaxFileBytes = 16u * 1024u * 1024u;   // farkta gösterilen en büyük dosya

        std::vector<std::string> SplitNul(const std::string& s)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); ++i)
                if (i == s.size() || s[i] == '\0')
                {
                    if (i > start)
                        parts.push_back(s.substr(start, i - start));
                    start = i + 1;
                }
            return parts;
        }

        std::vector<std::string> SplitLines(const std::string& s)
        {
            std::vector<std::string> lines;
            std::istringstream in(s);
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(line);
            }
            return lines;
        }

        std::string ErrorText(const std::string& out, const std::string& err)
        {
            std::string e = Trim(err);
            std::string o = Trim(out);
            if (e.empty()) return o;
            if (o.empty()) return e;
            return o + "\n" + e;
        }

        bool IsHex(const std::string& s)
        {
            return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; });
        }

        bool IsNumber(const std::string& s)
        {
            return !s.empty() && s.size() <= 12 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        std::string LocalTime(long long epoch)
        {
            std::time_t t = (std::time_t)epoch;
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
            return buf;
        }

        bool ReadFile(const fs::path& path, std::string& data, long long& size)
        {
            std::error_code ec;
            size = (long long)fs::file_size(path, ec);
            if (ec)
            {
                size = -1;
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            data.resize((size_t)std::min<long long>(size, (long long)kMaxFileBytes));
            in.read(data.data(), (std::streamsize)data.size());
            data.resize((size_t)in.gcount());
            return true;
        }

        // git gibi: ilk 8000 baytta NUL varsa ikili
        bool LooksBinary(const std::string& data) { return data.find('\0', 0) < std::min<size_t>(data.size(), 8000); }

        // Dosyanın tamamı eklendi / silindi (yeni, sürümlenmeyen ya da silinen dosya)
        void WholeFile(const std::string& data, bool added, Diff& d)
        {
            if (LooksBinary(data))
            {
                d.Binary = true;
                return;
            }
            const std::vector<std::string> lines = SplitLines(data);
            d.Lines.push_back({ '@', 0, 0, added ? "@@ -0,0 +1," + std::to_string(lines.size()) + " @@ (whole file added)"
                                                 : "@@ -1," + std::to_string(lines.size()) + " +0,0 @@ (whole file removed)" });
            int n = 0;
            for (const std::string& l : lines)
            {
                ++n;
                d.Lines.push_back({ added ? '+' : '-', added ? 0 : n, added ? n : 0, l });
            }
            (added ? d.Added : d.Removed) += (int)lines.size();
        }

        // ---------------------------------------------------------------- p4 -ztag ayrıştırma
        // "... anahtar değer" satırları; çok satırlı değerler (desc) sonraki satırlarda sürer. Kayıtlar boş satırla ayrılır; boş satır
        // açıklamanın içinde de olabilir: yeni kayıt ancak boş satırdan sonra "... " ile başlayan satır gelince ya da anahtar aynı kayıtta
        // yinelenince başlar.
        using Record = std::map<std::string, std::string>;
        std::vector<Record> ParseZtag(const std::string& text)
        {
            std::vector<Record> records;
            Record cur;
            std::string last;
            bool blank = false;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.rfind("... ", 0) == 0)
                {
                    const size_t sp = line.find(' ', 4);
                    const std::string key = line.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
                    if (!cur.empty() && (blank || cur.count(key)))
                    {
                        records.push_back(std::move(cur));
                        cur.clear();
                    }
                    cur[key] = sp == std::string::npos ? std::string() : line.substr(sp + 1);
                    last = key;
                    blank = false;
                }
                else if (line.empty())
                    blank = true;
                else if (!cur.empty() && !last.empty())
                {
                    cur[last] += (blank ? "\n\n" : "\n") + line;
                    blank = false;
                }
            }
            if (!cur.empty())
                records.push_back(std::move(cur));
            for (auto& r : records)
                for (auto& [k, v] : r)
                    v = Trim(v);
            return records;
        }

        std::string Field(const Record& r, const std::string& key)
        {
            auto it = r.find(key);
            return it == r.end() ? std::string() : it->second;
        }

        char P4Kind(const std::string& action)
        {
            if (action == "add" || action == "move/add" || action == "branch" || action == "import")
                return 'A';
            if (action == "delete" || action == "move/delete" || action == "purge" || action == "archive")
                return 'D';
            return 'M';
        }

        std::string P4(const Context& ctx) { return Detail::P4Command(ctx.Settings, ctx.ProjectDir); }

        std::string P4All(const Context& ctx) { return Quote((fs::path(ctx.ProjectDir) / "...").string()); }

        // p4'ün "dosya yok / açık değil" uyarıları hata sayılmaz
        bool P4Harmless(const std::string& err)
        {
            return err.empty() || err.find("not opened") != std::string::npos || err.find("no such file") != std::string::npos ||
                   err.find("file(s) up-to-date") != std::string::npos || err.find("No such file") != std::string::npos;
        }

        // ---------------------------------------------------------------- Git deposu
        struct GitRepo
        {
            bool Ok = false;
            std::string Root;     // depo kökü
            std::string Prefix;   // proje klasörü köke göre ("" = kök)
            std::string Error;
        };

        GitRepo OpenGit(const Context& ctx)
        {
            GitRepo g;
            if (ctx.ProjectDir.empty())
            {
                g.Error = "No project is open.";
                return g;
            }
            std::string out, err;
            if (RunSplit(ctx.ProjectDir, "git rev-parse --show-toplevel", out, err) != 0)
            {
                g.Error = ErrorText("", err.empty() ? out : err);
                if (g.Error.find("not a git repository") != std::string::npos)
                    g.Error = "The project folder is not inside a Git repository.";
                return g;
            }
            g.Root = fs::path(Trim(out)).lexically_normal().string();
            std::error_code ec;
            const fs::path rel = fs::relative(fs::path(ctx.ProjectDir), fs::path(g.Root), ec);
            g.Prefix = (ec || rel.empty() || rel == ".") ? std::string() : rel.generic_string();
            if (g.Prefix.rfind("..", 0) == 0)
                g.Prefix.clear();
            g.Ok = true;
            return g;
        }

        std::string ToProject(const GitRepo& g, const std::string& repoPath)
        {
            if (g.Prefix.empty())
                return repoPath;
            const std::string pre = g.Prefix + "/";
            if (repoPath.rfind(pre, 0) == 0)
                return repoPath.substr(pre.size());
            return "/" + repoPath;   // proje klasörünün dışında (depo köküne göre)
        }

        std::string ProjectSpec(const GitRepo& g) { return Quote(g.Prefix.empty() ? std::string(".") : g.Prefix); }

        // Proje göreli ya da mutlak yol -> depo köküne göre (dışındaysa boş)
        std::string ToRepo(const GitRepo& g, const Context& ctx, const std::string& path)
        {
            fs::path p(path);
            if (p.is_relative())
                p = fs::path(ctx.ProjectDir) / p;
            std::error_code ec;
            const fs::path rel = fs::relative(p.lexically_normal(), fs::path(g.Root), ec);
            if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
                return {};
            return rel == "." ? std::string(".") : rel.generic_string();
        }

        int GitRun(const GitRepo& g, const std::string& args, std::string& out, std::string& err) { return RunSplit(g.Root, kGit + " " + args, out, err); }

        std::string GitLine(const GitRepo& g, const std::string& args)
        {
            std::string out, err;
            if (GitRun(g, args, out, err) != 0)
                return {};
            return Trim(out);
        }

        long long GitBlobSize(const GitRepo& g, const std::string& spec)
        {
            const std::string s = GitLine(g, "cat-file -s " + Quote(spec));
            return s.empty() ? -1 : std::atoll(s.c_str());
        }

        // Süren işlem (rebase / merge / cherry-pick)
        std::string GitOperation(const GitRepo& g)
        {
            std::string out, err;
            if (GitRun(g, "rev-parse --git-path rebase-merge --git-path rebase-apply --git-path MERGE_HEAD --git-path CHERRY_PICK_HEAD", out, err) != 0)
                return {};
            const std::vector<std::string> paths = SplitLines(out);
            static const char* kNames[] = { "rebase", "rebase", "merge", "cherry-pick" };
            for (size_t i = 0; i < paths.size() && i < 4; ++i)
            {
                fs::path p(paths[i]);
                if (p.is_relative())
                    p = fs::path(g.Root) / p;
                std::error_code ec;
                if (fs::exists(p, ec))
                    return kNames[i];
            }
            return {};
        }

        std::string NulList(const std::vector<std::string>& paths)
        {
            std::string s;
            for (const std::string& p : paths)
            {
                s += p;
                s += '\0';
            }
            return s;
        }

        std::string Pathspec(const TempFile& f) { return " --pathspec-from-file=" + Quote(f.Path) + " --pathspec-file-nul"; }

        // ---------------------------------------------------------------- Perforce değişiklik listesi
        // Seçili dosyalar için numaralı liste: changelist "default" ise yeni liste açılır; açıklama yazılır, seçili dosyalar listeye,
        // listede olup seçilmeyenler default'a taşınır. Numara döner (hata: boş, nedeni message'da)
        std::string PrepareChangelist(const Context& ctx, const std::vector<Change>& files, const std::string& description,
                                      const std::string& changelist, std::string& message)
        {
            const bool numbered = IsNumber(changelist);
            std::string out, err;
            if (RunSplit(ctx.ProjectDir, P4(ctx) + " change -o" + (numbered ? " " + changelist : std::string()), out, err) != 0)
            {
                message = ErrorText(out, err);
                return {};
            }
            // Description bölümü yenisiyle değişir; yeni listede Files bölümü atılır (default listedeki bütün dosyalar girmesin)
            std::string spec;
            bool skipping = false;
            for (const std::string& line : SplitLines(out))
            {
                const bool field = !line.empty() && line[0] != '\t' && line[0] != ' ' && line[0] != '#';
                if (field)
                {
                    skipping = false;
                    if (line.rfind("Description:", 0) == 0)
                    {
                        spec += "Description:\n";
                        for (const std::string& d : SplitLines(description))
                            spec += "\t" + d + "\n";
                        spec += "\n";
                        skipping = true;
                        continue;
                    }
                    if (line.rfind("Files:", 0) == 0 && !numbered)
                    {
                        skipping = true;
                        continue;
                    }
                }
                if (!skipping)
                    spec += line + "\n";
            }
            const TempFile specFile(spec);
            if (RunSplit(ctx.ProjectDir, P4(ctx) + " change -i < " + Quote(specFile.Path), out, err) != 0)
            {
                message = ErrorText(out, err);
                return {};
            }
            std::string number = changelist;
            if (!numbered)
            {
                const size_t at = out.find("Change ");
                number.clear();
                for (size_t i = at == std::string::npos ? out.size() : at + 7; i < out.size() && std::isdigit((unsigned char)out[i]); ++i)
                    number += out[i];
                if (number.empty())
                {
                    message = "Could not create a changelist: " + ErrorText(out, err);
                    return {};
                }
            }
            std::string list;
            std::set<std::string> chosen;
            for (const Change& c : files)
            {
                list += c.RepoPath + "\n";
                chosen.insert(c.RepoPath);
            }
            const TempFile listFile(list);
            if (RunSplit(ctx.ProjectDir, P4(ctx) + " -x " + Quote(listFile.Path) + " reopen -c " + number, out, err) != 0)
            {
                message = ErrorText(out, err);
                return {};
            }
            if (numbered)
            {
                std::string rest;
                for (const Change& c : ListChanges(ctx))
                    if (c.Changelist == number && !chosen.count(c.RepoPath))
                        rest += c.RepoPath + "\n";
                if (!rest.empty())
                {
                    const TempFile restFile(rest);
                    RunSplit(ctx.ProjectDir, P4(ctx) + " -x " + Quote(restFile.Path) + " reopen -c default", out, err);
                }
            }
            return number;
        }
    }

    const char* ChangeKindName(char kind)
    {
        switch (kind)
        {
            case 'A': return "Added";
            case 'D': return "Deleted";
            case 'R': return "Renamed";
            case '?': return "Not versioned";
            case 'U': return "Conflicted";
            default: return "Modified";
        }
    }

    // ------------------------------------------------------------------ Changes

    std::vector<Change> ListChanges(const Context& ctx, std::string* error)
    {
        std::vector<Change> list;
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                if (error) *error = g.Error;
                return list;
            }
            if (GitRun(g, "status --porcelain=v1 -uall -z -- " + ProjectSpec(g), out, err) != 0)
            {
                if (error) *error = ErrorText(out, err);
                return list;
            }
            // XY yol\0 (yeniden adlandırma / kopya: XY yeni\0eski\0)
            size_t i = 0;
            while (i + 3 < out.size())
            {
                const char x = out[i], y = out[i + 1];
                size_t end = out.find('\0', i + 3);
                if (end == std::string::npos)
                    end = out.size();
                Change c;
                c.RepoPath = out.substr(i + 3, end - (i + 3));
                i = end + 1;
                if (x == 'R' || x == 'C')
                {
                    size_t old = out.find('\0', i);
                    if (old == std::string::npos)
                        old = out.size();
                    if (x == 'R')
                        c.OldRepoPath = out.substr(i, old - i);
                    i = old + 1;
                }
                const std::string xy{ x, y };
                if (xy == "??")
                    c.Kind = '?';
                else if (xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA" || xy == "UU")
                    c.Kind = 'U';
                else if (x == 'R')
                    c.Kind = 'R';
                else if (x == 'A' || x == 'C')
                    c.Kind = 'A';
                else if (x == 'D' || y == 'D')
                    c.Kind = 'D';
                else
                    c.Kind = 'M';
                c.Path = ToProject(g, c.RepoPath);
                list.push_back(std::move(c));
            }
        }
        else if (ctx.Mode() == 2)
        {
            RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag fstat -Ro " + P4All(ctx), out, err);
            if (out.empty() && !P4Harmless(err))
            {
                if (error) *error = Trim(err);
                return list;
            }
            for (const Record& r : ParseZtag(out))
            {
                const std::string client = Field(r, "clientFile");
                const std::string action = Field(r, "action");
                if (client.empty() || action.empty())
                    continue;
                Change c;
                c.RepoPath = fs::path(client).lexically_normal().string();
                std::error_code ec;
                const fs::path rel = fs::relative(fs::path(c.RepoPath), fs::path(ctx.ProjectDir), ec);
                c.Path = ec || rel.empty() ? c.RepoPath : rel.generic_string();
                c.Kind = P4Kind(action);
                c.Changelist = Field(r, "change").empty() ? std::string("default") : Field(r, "change");
                c.DepotPath = Field(r, "depotFile");
                list.push_back(std::move(c));
            }
        }
        std::sort(list.begin(), list.end(), [](const Change& a, const Change& b) { return a.Path < b.Path; });
        return list;
    }

    Result Commit(const Context& ctx, const std::vector<Change>& files, const std::string& message, const std::string& changelist)
    {
        Result r;
        if (Trim(message).empty())
        {
            r.Output = ctx.Mode() == 2 ? "Enter a changelist description." : "Enter a commit message.";
            return r;
        }
        if (files.empty())
        {
            r.Output = "Select at least one file.";
            return r;
        }
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                r.Output = g.Error;
                return r;
            }
            std::vector<std::string> paths;
            for (const Change& c : files)
            {
                paths.push_back(c.RepoPath);
                if (!c.OldRepoPath.empty())
                    paths.push_back(c.OldRepoPath);
            }
            const TempFile list(NulList(paths));
            const TempFile msg(Trim(message) + "\n");
            if (GitRun(g, "add -A" + Pathspec(list), out, err) != 0)
            {
                r.Output = ErrorText(out, err);
                return r;
            }
            // Birleştirme sırasında git kısmi kayda izin vermez: bütün index kaydedilir
            const std::string op = GitOperation(g);
            if (op == "rebase")
            {
                r.Output = "A rebase is in progress: resolve the conflicts and press Continue in Branches & Sync.";
                return r;
            }
            const bool whole = op == "merge" || op == "cherry-pick";
            if (GitRun(g, "commit -q -F " + Quote(msg.Path) + (whole ? std::string() : Pathspec(list)), out, err) != 0)
            {
                r.Output = ErrorText(out, err);
                if (r.Output.find("Please tell me who you are") != std::string::npos || r.Output.find("user.email") != std::string::npos)
                    r.Output += "\nSet your name and e-mail: git config --global user.name \"Your Name\" and git config --global user.email you@example.com";
                return r;
            }
            const std::string hash = GitLine(g, "rev-parse --short HEAD");
            std::string first = Trim(message);
            first = first.substr(0, first.find('\n'));
            r.Ok = true;
            r.Output = "Committed " + hash + " (" + std::to_string(files.size()) + " file" + (files.size() == 1 ? "" : "s") + "): " + first;
            return r;
        }
        if (ctx.Mode() == 2)
        {
            const std::string number = PrepareChangelist(ctx, files, message, changelist, r.Output);
            if (number.empty())
                return r;
            int code = RunSplit(ctx.ProjectDir, P4(ctx) + " submit -c " + number, out, err);
            r.Output = ErrorText(out, err);
            // Rafa kaldırılmış (shelve) dosyası olan liste gönderilemez: P4V gibi önce raf silinir (dosyalar açık kalır), sonra yeniden
            if (code != 0 && r.Output.find("shelved") != std::string::npos &&
                RunSplit(ctx.ProjectDir, P4(ctx) + " shelve -d -c " + number, out, err) == 0)
            {
                code = RunSplit(ctx.ProjectDir, P4(ctx) + " submit -c " + number, out, err);
                r.Output = ErrorText(out, err);
            }
            r.Ok = code == 0 && r.Output.find("submitted") != std::string::npos;
            if (!r.Ok && r.Output.find("resolve") != std::string::npos)
                r.Output += "\nGet Latest, resolve the files with p4 resolve (or P4V), then submit changelist " + number + " again.";
            return r;
        }
        r.Output = "Version control is off (Project Settings > Version Control).";
        return r;
    }

    Result Shelve(const Context& ctx, const std::vector<Change>& files, const std::string& description, const std::string& changelist)
    {
        Result r;
        if (ctx.Mode() != 2)
        {
            r.Output = "Shelve is a Perforce feature (Git: commit to a separate branch instead).";
            return r;
        }
        if (Trim(description).empty())
        {
            r.Output = "Enter a changelist description.";
            return r;
        }
        if (files.empty())
        {
            r.Output = "Select at least one file.";
            return r;
        }
        const std::string number = PrepareChangelist(ctx, files, description, changelist, r.Output);
        if (number.empty())
            return r;
        std::string out, err;
        const int code = RunSplit(ctx.ProjectDir, P4(ctx) + " shelve -f -c " + number, out, err);
        r.Ok = code == 0;
        r.Output = ErrorText(out, err);
        if (r.Ok)
            r.Output = "Shelved " + std::to_string(files.size()) + " file(s) in changelist " + number + " (the files stay checked out).\n" + r.Output;
        return r;
    }

    Result RevertChanges(const Context& ctx, const std::vector<Change>& files)
    {
        Result r;
        if (files.empty())
        {
            r.Output = "Select the files to revert.";
            return r;
        }
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                r.Output = g.Error;
                return r;
            }
            std::vector<std::string> restore, unstage, renamedNew, renamedOld;
            int skipped = 0;
            for (const Change& c : files)
            {
                if (c.Kind == '?')
                    ++skipped;   // sürümlenmeyen dosya silinmez
                else if (c.Kind == 'A')
                    unstage.push_back(c.RepoPath);
                else if (c.Kind == 'R')
                {
                    renamedNew.push_back(c.RepoPath);
                    renamedOld.push_back(c.OldRepoPath);
                }
                else
                    restore.push_back(c.RepoPath);
            }
            std::string log;
            bool ok = true;
            auto step = [&](const std::string& args, const std::vector<std::string>& paths) {
                if (paths.empty())
                    return;
                const TempFile list(NulList(paths));
                if (GitRun(g, args + Pathspec(list), out, err) != 0)
                {
                    ok = false;
                    log += ErrorText(out, err) + "\n";
                }
            };
            // Yeniden adlandırma: iki yol da index'ten çıkar, eski dosya geri gelir (yeni dosya sürümlenmeyen olarak kalır)
            std::vector<std::string> both = renamedNew;
            both.insert(both.end(), renamedOld.begin(), renamedOld.end());
            step("reset -q", both);
            step("checkout HEAD", renamedOld);
            step("rm -q --cached", unstage);
            step("checkout HEAD", restore);
            const size_t done = files.size() - (size_t)skipped;
            r.Ok = ok;
            r.Output = ok ? "Reverted " + std::to_string(done) + " file(s)." : Trim(log);
            if (skipped)
                r.Output += "\n" + std::to_string(skipped) + " file(s) are not versioned and were left as they are (delete them in the Content Browser).";
            if (!unstage.empty() && ok)
                r.Output += "\nAdded files are no longer added; the files themselves are kept.";
            return r;
        }
        if (ctx.Mode() == 2)
        {
            std::string list;
            for (const Change& c : files)
                list += c.RepoPath + "\n";
            const TempFile listFile(list);
            const int code = RunSplit(ctx.ProjectDir, P4(ctx) + " -x " + Quote(listFile.Path) + " revert", out, err);
            r.Ok = code == 0;
            r.Output = r.Ok ? "Reverted " + std::to_string(files.size()) + " file(s).\n" + Trim(out) : ErrorText(out, err);
            return r;
        }
        r.Output = "Version control is off.";
        return r;
    }

    std::vector<Changelist> PendingChangelists(const Context& ctx)
    {
        std::vector<Changelist> lists;
        if (ctx.Mode() != 2)
            return lists;
        std::string out, err;
        RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag info", out, err);
        std::string client;
        for (const Record& r : ParseZtag(out))
            if (!Field(r, "clientName").empty())
                client = Field(r, "clientName");
        if (client.empty() || client == "*unknown*")
            return lists;
        RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag changes -s pending -l -c " + Quote(client), out, err);
        for (const Record& r : ParseZtag(out))
            if (IsNumber(Field(r, "change")))
                lists.push_back({ Field(r, "change"), Field(r, "desc") });
        return lists;
    }

    // ------------------------------------------------------------------ History

    std::vector<CommitInfo> Log(const Context& ctx, const std::string& path, int maxCount, std::string* error)
    {
        std::vector<CommitInfo> commits;
        maxCount = std::clamp(maxCount, 1, 100000);
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                if (error) *error = g.Error;
                return commits;
            }
            std::string spec = ProjectSpec(g);
            if (!path.empty())
            {
                const std::string repo = ToRepo(g, ctx, path);
                if (repo.empty())
                {
                    if (error) *error = "The file is outside the repository.";
                    return commits;
                }
                fs::path abs = fs::path(g.Root) / repo;
                std::error_code ec;
                // Tek dosya: yeniden adlandırmaları izle (--follow yalnız tek yolda çalışır)
                spec = (fs::is_directory(abs, ec) ? std::string() : std::string("--follow ")) + "-- " + Quote(repo);
            }
            else
                spec = "-- " + spec;
            const int code = GitRun(g, "log -n " + std::to_string(maxCount) + " --date=" + Quote("format-local:%Y-%m-%d %H:%M") + " --format=" +
                                           Quote("%x1e%H%x1f%h%x1f%an%x1f%ae%x1f%ad%x1f%B") + " " + spec, out, err);
            if (code != 0)
            {
                // Henüz kayıt yok: boş geçmiş, hata değil
                if (err.find("does not have any commits") == std::string::npos && error)
                    *error = ErrorText("", err);
                return commits;
            }
            size_t start = 0;
            while ((start = out.find('\x1e', start)) != std::string::npos)
            {
                size_t end = out.find('\x1e', start + 1);
                const std::string rec = out.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
                start = end == std::string::npos ? out.size() : end;
                std::vector<std::string> f;
                size_t p = 0;
                for (int k = 0; k < 5; ++k)
                {
                    const size_t q = rec.find('\x1f', p);
                    if (q == std::string::npos)
                        break;
                    f.push_back(rec.substr(p, q - p));
                    p = q + 1;
                }
                if (f.size() < 5)
                    continue;
                CommitInfo c;
                c.Id = f[0];
                c.ShortId = f[1];
                c.Author = f[2];
                c.Email = f[3];
                c.Date = f[4];
                c.Message = Trim(rec.substr(p));
                c.Summary = c.Message.substr(0, c.Message.find('\n'));
                commits.push_back(std::move(c));
            }
            return commits;
        }
        if (ctx.Mode() == 2)
        {
            if (path.empty())
            {
                RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag changes -s submitted -l -m " + std::to_string(maxCount) + " " + P4All(ctx), out, err);
                for (const Record& r : ParseZtag(out))
                {
                    if (!IsNumber(Field(r, "change")))
                        continue;
                    CommitInfo c;
                    c.Id = c.ShortId = Field(r, "change");
                    c.Author = Field(r, "user");
                    c.Email = Field(r, "client");
                    c.Date = LocalTime(std::atoll(Field(r, "time").c_str()));
                    c.Message = Field(r, "desc");
                    c.Summary = c.Message.substr(0, c.Message.find('\n'));
                    commits.push_back(std::move(c));
                }
            }
            else
            {
                fs::path abs(path);
                if (abs.is_relative())
                    abs = fs::path(ctx.ProjectDir) / abs;
                std::error_code ec;
                const std::string target = fs::is_directory(abs, ec) ? (abs / "...").string() : abs.string();
                if (fs::is_directory(abs, ec))
                    RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag changes -s submitted -l -m " + std::to_string(maxCount) + " " + Quote(target), out, err);
                else
                    RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag filelog -l -m " + std::to_string(maxCount) + " " + Quote(target), out, err);
                for (const Record& r : ParseZtag(out))
                {
                    if (IsNumber(Field(r, "change")))   // klasör: changes çıktısı
                    {
                        CommitInfo c;
                        c.Id = c.ShortId = Field(r, "change");
                        c.Author = Field(r, "user");
                        c.Email = Field(r, "client");
                        c.Date = LocalTime(std::atoll(Field(r, "time").c_str()));
                        c.Message = Field(r, "desc");
                        c.Summary = c.Message.substr(0, c.Message.find('\n'));
                        commits.push_back(std::move(c));
                        continue;
                    }
                    for (int i = 0;; ++i)   // filelog: rev0, change0, ... (yeniden eskiye)
                    {
                        const std::string n = std::to_string(i);
                        if (!r.count("rev" + n))
                            break;
                        CommitInfo c;
                        c.Id = c.ShortId = Field(r, "change" + n);
                        c.Author = Field(r, "user" + n);
                        c.Email = Field(r, "client" + n);
                        c.Date = LocalTime(std::atoll(Field(r, "time" + n).c_str()));
                        c.Message = Field(r, "desc" + n);
                        c.Summary = c.Message.substr(0, c.Message.find('\n'));
                        c.FileRevision = Field(r, "rev" + n);
                        commits.push_back(std::move(c));
                    }
                }
            }
            if (commits.empty() && !P4Harmless(err) && error)
                *error = Trim(err);
            return commits;
        }
        if (error) *error = "Version control is off.";
        return commits;
    }

    std::vector<CommitFile> CommitFiles(const Context& ctx, const std::string& id, std::string* error)
    {
        std::vector<CommitFile> files;
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok || !IsHex(id))
            {
                if (error) *error = g.Ok ? "Invalid commit." : g.Error;
                return files;
            }
            if (GitRun(g, "diff-tree -r --root --no-commit-id --name-status -z -M --diff-merges=first-parent " + id, out, err) != 0)
            {
                if (error) *error = ErrorText(out, err);
                return files;
            }
            const std::vector<std::string> t = SplitNul(out);
            for (size_t i = 0; i < t.size();)
            {
                const std::string status = t[i++];
                if (status.empty())
                    continue;
                CommitFile f;
                f.Kind = status[0] == 'R' ? 'R' : status[0] == 'A' || status[0] == 'C' ? 'A' : status[0] == 'D' ? 'D' : 'M';
                if ((status[0] == 'R' || status[0] == 'C') && i + 1 < t.size())
                {
                    if (status[0] == 'R')
                        f.OldRepoPath = t[i];
                    f.RepoPath = t[i + 1];
                    i += 2;
                }
                else if (i < t.size())
                    f.RepoPath = t[i++];
                f.Path = ToProject(g, f.RepoPath);
                files.push_back(std::move(f));
            }
            return files;
        }
        if (ctx.Mode() == 2)
        {
            if (!IsNumber(id))
            {
                if (error) *error = "Invalid changelist.";
                return files;
            }
            RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag describe -s " + id, out, err);
            for (const Record& r : ParseZtag(out))
                for (int i = 0;; ++i)
                {
                    const std::string n = std::to_string(i);
                    if (!r.count("depotFile" + n))
                        break;
                    CommitFile f;
                    f.DepotPath = f.Path = Field(r, "depotFile" + n);
                    f.Action = Field(r, "action" + n);
                    f.Revision = Field(r, "rev" + n);
                    f.Kind = P4Kind(f.Action);
                    files.push_back(std::move(f));
                }
            if (files.empty() && !P4Harmless(err) && error)
                *error = Trim(err);
            return files;
        }
        return files;
    }

    // ------------------------------------------------------------------ Diff

    void ParseUnifiedDiff(const std::string& text, Diff& d)
    {
        // Parça başlığındaki satır sayıları tükenene dek satırlar içeriktir ("--- " ile başlayan içerik satırı başlık sanılmaz)
        int oldLeft = 0, newLeft = 0, oldNo = 0, newNo = 0;
        auto readRange = [](const std::string& s, size_t& i, int& start, int& count) {
            start = 0;
            count = 1;
            while (i < s.size() && std::isdigit((unsigned char)s[i]))
                start = start * 10 + (s[i++] - '0');
            if (i < s.size() && s[i] == ',')
            {
                ++i;
                count = 0;
                while (i < s.size() && std::isdigit((unsigned char)s[i]))
                    count = count * 10 + (s[i++] - '0');
            }
        };
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (oldLeft > 0 || newLeft > 0)
            {
                const char k = line.empty() ? ' ' : line[0];
                if (k == '\\')
                    continue;   // "\ No newline at end of file"
                DiffLine l;
                l.Text = line.empty() ? std::string() : line.substr(1);
                if (k == '+')
                {
                    l.Kind = '+';
                    l.NewLine = ++newNo;
                    --newLeft;
                    ++d.Added;
                }
                else if (k == '-')
                {
                    l.Kind = '-';
                    l.OldLine = ++oldNo;
                    --oldLeft;
                    ++d.Removed;
                }
                else
                {
                    l.Kind = ' ';
                    l.OldLine = ++oldNo;
                    l.NewLine = ++newNo;
                    --oldLeft;
                    --newLeft;
                }
                d.Lines.push_back(std::move(l));
                continue;
            }
            if (line.rfind("@@ -", 0) == 0)
            {
                size_t i = 4;
                int a, b, c, e;
                readRange(line, i, a, b);
                while (i < line.size() && line[i] != '+')
                    ++i;
                ++i;
                readRange(line, i, c, e);
                oldNo = b > 0 ? a - 1 : a;
                newNo = e > 0 ? c - 1 : c;
                oldLeft = b;
                newLeft = e;
                d.Lines.push_back({ '@', 0, 0, line });
                continue;
            }
            if (line.rfind("Binary files", 0) == 0 || line.rfind("GIT binary patch", 0) == 0 ||
                (line.rfind("====", 0) == 0 && line.find("(binary") != std::string::npos))
                d.Binary = true;
        }
    }

    Diff DiffWorkingCopy(const Context& ctx, const Change& change)
    {
        Diff d;
        d.Title = change.Path;
        std::string out, err, data;
        if (ctx.Mode() == 1)
        {
            d.OldLabel = "HEAD";
            d.NewLabel = "Working copy";
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                d.Error = g.Error;
                return d;
            }
            const fs::path abs = fs::path(g.Root) / change.RepoPath;
            const bool hasHead = !GitLine(g, "rev-parse -q --verify HEAD").empty();
            if (change.Kind == '?' || !hasHead)
            {
                d.OldLabel = "(not versioned)";
                if (ReadFile(abs, data, d.NewSize))
                    WholeFile(data, true, d);
                d.Ok = true;
                return d;
            }
            std::string paths = Quote(change.RepoPath);
            if (!change.OldRepoPath.empty())
                paths += " " + Quote(change.OldRepoPath);
            if (GitRun(g, "diff HEAD --no-ext-diff --no-textconv -M -- " + paths, out, err) != 0)
            {
                d.Error = ErrorText(out, err);
                return d;
            }
            ParseUnifiedDiff(out, d);
            if (d.Binary)
            {
                d.OldSize = GitBlobSize(g, "HEAD:" + (change.OldRepoPath.empty() ? change.RepoPath : change.OldRepoPath));
                std::error_code ec;
                d.NewSize = fs::exists(abs, ec) ? (long long)fs::file_size(abs, ec) : -1;
            }
            d.Identical = !d.Binary && d.Lines.empty();
            d.Ok = true;
            return d;
        }
        if (ctx.Mode() == 2)
        {
            d.OldLabel = "Have revision";
            d.NewLabel = "Working copy";
            const fs::path abs(change.RepoPath);
            if (change.Kind == 'A')
            {
                d.OldLabel = "(new file)";
                if (ReadFile(abs, data, d.NewSize))
                    WholeFile(data, true, d);
                d.Ok = true;
                return d;
            }
            if (change.Kind == 'D')
            {
                d.NewLabel = "(deleted)";
                RunSplit(ctx.ProjectDir, P4(ctx) + " print -q " + Quote(abs.string() + "#have"), out, err);
                d.OldSize = (long long)out.size();
                WholeFile(out, false, d);
                d.Ok = true;
                return d;
            }
            RunSplit(ctx.ProjectDir, P4(ctx) + " diff -du " + Quote(abs.string()), out, err);
            ParseUnifiedDiff(out, d);
            if (!d.Binary && out.find("(binary") != std::string::npos)
                d.Binary = true;
            if (d.Binary)
            {
                std::error_code ec;
                d.NewSize = fs::exists(abs, ec) ? (long long)fs::file_size(abs, ec) : -1;
                std::string sout, serr;
                RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag fstat -Ol " + Quote(abs.string()), sout, serr);
                for (const Record& r : ParseZtag(sout))
                    if (!Field(r, "fileSize").empty())
                        d.OldSize = std::atoll(Field(r, "fileSize").c_str());
            }
            if (d.Lines.empty() && !d.Binary && !P4Harmless(err))
            {
                d.Error = Trim(err);
                return d;
            }
            d.Identical = !d.Binary && d.Lines.empty();
            d.Ok = true;
            return d;
        }
        d.Error = "Version control is off.";
        return d;
    }

    Diff DiffWorkingFile(const Context& ctx, const std::string& path)
    {
        fs::path abs(path);
        if (abs.is_relative())
            abs = fs::path(ctx.ProjectDir) / abs;
        abs = abs.lexically_normal();
        std::error_code ec;
        const fs::path rel = fs::relative(abs, fs::path(ctx.ProjectDir), ec);
        const std::string relText = ec ? abs.string() : rel.generic_string();
        for (const Change& c : ListChanges(ctx))
            if (c.Path == relText)
                return DiffWorkingCopy(ctx, c);
        Diff d;
        d.Title = relText;
        d.Ok = true;
        d.Identical = true;
        d.OldLabel = ctx.Mode() == 2 ? "Have revision" : "HEAD";
        d.NewLabel = "Working copy";
        return d;
    }

    Diff DiffInCommit(const Context& ctx, const std::string& id, const CommitFile& file)
    {
        Diff d;
        d.Title = file.Path;
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok || !IsHex(id))
            {
                d.Error = g.Ok ? "Invalid commit." : g.Error;
                return d;
            }
            const std::string shortId = GitLine(g, "rev-parse --short " + id);
            d.OldLabel = "Parent of " + shortId;
            d.NewLabel = shortId;
            std::string paths = Quote(file.RepoPath);
            if (!file.OldRepoPath.empty())
                paths += " " + Quote(file.OldRepoPath);
            if (GitRun(g, "show --format= --no-ext-diff --no-textconv -M --diff-merges=first-parent " + id + " -- " + paths, out, err) != 0)
            {
                d.Error = ErrorText(out, err);
                return d;
            }
            ParseUnifiedDiff(out, d);
            if (d.Binary)
            {
                d.OldSize = file.Kind == 'A' ? -1 : GitBlobSize(g, id + "^:" + (file.OldRepoPath.empty() ? file.RepoPath : file.OldRepoPath));
                d.NewSize = file.Kind == 'D' ? -1 : GitBlobSize(g, id + ":" + file.RepoPath);
            }
            d.Identical = !d.Binary && d.Lines.empty();
            d.Ok = true;
            return d;
        }
        if (ctx.Mode() == 2)
        {
            const int rev = std::atoi(file.Revision.c_str());
            const std::string depot = file.DepotPath;
            d.OldLabel = depot + "#" + std::to_string(std::max(0, rev - 1));
            d.NewLabel = depot + "#" + std::to_string(rev);
            if (depot.empty() || depot.find('"') != std::string::npos)
            {
                d.Error = "Invalid depot path.";
                return d;
            }
            if (file.Kind == 'A' || rev <= 1)
            {
                d.OldLabel = "(new file)";
                RunSplit(ctx.ProjectDir, P4(ctx) + " print -q " + Quote(depot + "#" + std::to_string(rev)), out, err);
                d.NewSize = (long long)out.size();
                WholeFile(out, true, d);
            }
            else if (file.Kind == 'D')
            {
                d.NewLabel = "(deleted)";
                RunSplit(ctx.ProjectDir, P4(ctx) + " print -q " + Quote(depot + "#" + std::to_string(rev - 1)), out, err);
                d.OldSize = (long long)out.size();
                WholeFile(out, false, d);
            }
            else
            {
                RunSplit(ctx.ProjectDir, P4(ctx) + " diff2 -du " + Quote(depot + "#" + std::to_string(rev - 1)) + " " + Quote(depot + "#" + std::to_string(rev)),
                         out, err);
                ParseUnifiedDiff(out, d);
                if (!d.Binary && out.find("(binary") != std::string::npos)
                    d.Binary = true;
            }
            if (d.Binary && d.OldSize < 0 && d.NewSize < 0)
            {
                // diff2 boyut vermez: iki sürümün içeriği
                std::string a, b, e2;
                RunSplit(ctx.ProjectDir, P4(ctx) + " print -q " + Quote(depot + "#" + std::to_string(rev - 1)), a, e2);
                RunSplit(ctx.ProjectDir, P4(ctx) + " print -q " + Quote(depot + "#" + std::to_string(rev)), b, e2);
                d.OldSize = (long long)a.size();
                d.NewSize = (long long)b.size();
            }
            d.Identical = !d.Binary && d.Lines.empty();
            d.Ok = true;
            return d;
        }
        d.Error = "Version control is off.";
        return d;
    }

    // ------------------------------------------------------------------ Depo bilgisi, dallar, eşitleme

    RepoInfo GetRepoInfo(const Context& ctx)
    {
        RepoInfo info;
        std::string out, err;
        if (ctx.Mode() == 1)
        {
            const GitRepo g = OpenGit(ctx);
            if (!g.Ok)
            {
                info.Error = g.Error;
                return info;
            }
            info.IsRepo = true;
            info.Branch = GitLine(g, "symbolic-ref --short -q HEAD");
            if (info.Branch.empty())
            {
                info.Detached = true;
                info.Branch = GitLine(g, "rev-parse --short HEAD");
            }
            info.Upstream = GitLine(g, "rev-parse --abbrev-ref --symbolic-full-name " + Quote("@{u}"));
            if (!info.Upstream.empty())
            {
                const std::string counts = GitLine(g, "rev-list --left-right --count " + Quote("HEAD...@{u}"));
                std::istringstream in(counts);
                in >> info.Ahead >> info.Behind;
            }
            for (const std::string& r : SplitLines(GitLine(g, "remote")))
                if (!r.empty())
                    info.Remotes.push_back(r);
            info.Operation = GitOperation(g);
            if (GitRun(g, "diff --name-only --diff-filter=U -z", out, err) == 0)
                info.Conflicts = SplitNul(out);
            if (GitRun(g, "status --porcelain=v1 -uno -z", out, err) == 0)
            {
                // XY yol\0 (R/C: ardından eski yol)
                size_t i = 0;
                while (i + 3 < out.size())
                {
                    const char x = out[i];
                    size_t end = out.find('\0', i + 3);
                    i = (end == std::string::npos ? out.size() : end) + 1;
                    if (x == 'R' || x == 'C')
                    {
                        const size_t old = out.find('\0', i);
                        i = (old == std::string::npos ? out.size() : old) + 1;
                    }
                    ++info.UncommittedTracked;
                }
            }
            return info;
        }
        if (ctx.Mode() == 2)
        {
            const int code = RunSplit(ctx.ProjectDir, P4(ctx) + " -ztag info", out, err);
            for (const Record& r : ParseZtag(out))
            {
                if (!Field(r, "userName").empty()) info.User = Field(r, "userName");
                if (!Field(r, "clientName").empty()) info.Workspace = Field(r, "clientName");
                if (!Field(r, "clientStream").empty()) info.Stream = Field(r, "clientStream");
                if (!Field(r, "clientRoot").empty()) info.Root = Field(r, "clientRoot");
                if (!Field(r, "serverAddress").empty()) info.Server = Field(r, "serverAddress");
            }
            info.IsRepo = code == 0 && !info.Workspace.empty() && info.Workspace != "*unknown*";
            if (!info.IsRepo)
                info.Error = ErrorText(out, err).empty() ? std::string("No Perforce workspace (set Workspace in Project Settings > Version Control).")
                                                         : ErrorText(out, err);
            return info;
        }
        info.Error = "Version control is off.";
        return info;
    }

    std::vector<Branch> ListBranches(const Context& ctx, std::string* error)
    {
        std::vector<Branch> list;
        if (ctx.Mode() != 1)
            return list;
        const GitRepo g = OpenGit(ctx);
        if (!g.Ok)
        {
            if (error) *error = g.Error;
            return list;
        }
        std::string out, err;
        if (GitRun(g, "for-each-ref --sort=-committerdate --format=" +
                          Quote("%(HEAD)%1f%(refname)%1f%(refname:short)%1f%(upstream:short)%1f%(upstream:track)%1f%(committerdate:short)%1f%(subject)") +
                          " refs/heads refs/remotes", out, err) != 0)
        {
            if (error) *error = ErrorText(out, err);
            return list;
        }
        for (const std::string& line : SplitLines(out))
        {
            std::vector<std::string> f;
            size_t p = 0;
            for (;;)
            {
                const size_t q = line.find('\x1f', p);
                f.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p));
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
            if (f.size() < 7)
                continue;
            const std::string& ref = f[1];
            if (ref.rfind("refs/remotes/", 0) == 0 && ref.size() >= 5 && ref.compare(ref.size() - 5, 5, "/HEAD") == 0)
                continue;   // origin/HEAD işaretçisi
            Branch b;
            b.Current = f[0] == "*";
            b.Remote = ref.rfind("refs/remotes/", 0) == 0;
            b.Name = f[2];
            b.Upstream = f[3];
            b.Track = f[4];
            b.Date = f[5];
            b.Subject = f[6];
            list.push_back(std::move(b));
        }
        // Yerel dallar önce
        std::stable_sort(list.begin(), list.end(), [](const Branch& a, const Branch& b) { return !a.Remote && b.Remote; });
        return list;
    }

    Result Fetch(const Context& ctx)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Fetch is a Git command (Perforce: Get Latest)." : g.Error;
            return r;
        }
        std::string out, err;
        const int code = GitRun(g, "fetch --all --prune", out, err);
        r.Ok = code == 0;
        r.Output = ErrorText(out, err);
        if (r.Ok)
            r.Output = r.Output.empty() ? "Fetched: already up to date." : "Fetched.\n" + r.Output;
        return r;
    }

    Result Pull(const Context& ctx, bool rebase)
    {
        Result r;
        if (ctx.Mode() == 2)
            return GetLatest(ctx);
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = g.Ok ? "Version control is off." : g.Error;
            return r;
        }
        if (!GitOperation(g).empty())
        {
            r.Output = "A " + GitOperation(g) + " is in progress: resolve the conflicts, then Continue or Abort.";
            return r;
        }
        if (GitLine(g, "rev-parse --abbrev-ref --symbolic-full-name " + Quote("@{u}")).empty())
        {
            r.Output = "The current branch has no upstream branch: Push it first (that sets the upstream).";
            return r;
        }
        std::string out, err;
        // --autostash: kaydedilmemiş değişiklikler saklanır, çekildikten sonra geri konur
        const int code = GitRun(g, rebase ? "-c core.editor=true pull --rebase --autostash" : "-c core.editor=true pull --no-rebase --no-edit --autostash", out, err);
        r.Ok = code == 0;
        r.Output = ErrorText(out, err);
        if (!r.Ok)
        {
            std::string conflicts;
            if (GitRun(g, "diff --name-only --diff-filter=U -z", out, err) == 0)
                for (const std::string& f : SplitNul(out))
                    conflicts += "\n  " + ToProject(g, f);
            if (!conflicts.empty())
                r.Output = "Pull stopped with conflicts in:" + conflicts + "\nResolve each file (Use Mine / Use Theirs / Mark Resolved), then Continue, or Abort.";
        }
        return r;
    }

    Result Push(const Context& ctx)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Push is a Git command (Perforce: Submit)." : g.Error;
            return r;
        }
        std::string out, err;
        std::string cmd = "push";
        if (GitLine(g, "rev-parse --abbrev-ref --symbolic-full-name " + Quote("@{u}")).empty())
        {
            const std::vector<std::string> remotes = SplitLines(GitLine(g, "remote"));
            if (remotes.empty() || remotes[0].empty())
            {
                r.Output = "No remote is configured: add one under Remotes (for example origin and the repository URL).";
                return r;
            }
            const std::string remote = std::find(remotes.begin(), remotes.end(), "origin") != remotes.end() ? std::string("origin") : remotes[0];
            if (GitLine(g, "symbolic-ref --short -q HEAD").empty())
            {
                r.Output = "HEAD is detached: switch to a branch before pushing.";
                return r;
            }
            cmd = "push -u " + Quote(remote) + " HEAD";   // ilk gönderim: izleme dalını kurar
        }
        const int code = GitRun(g, cmd, out, err);
        r.Ok = code == 0;
        r.Output = ErrorText(out, err);
        if (!r.Ok && (r.Output.find("rejected") != std::string::npos || r.Output.find("fetch first") != std::string::npos))
            r.Output += "\nThe remote has commits you do not have yet: Pull first, then Push.";
        else if (r.Ok && r.Output.empty())
            r.Output = "Pushed.";
        return r;
    }

    Result CreateBranch(const Context& ctx, const std::string& name, bool switchTo)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Branches are a Git feature here (Perforce streams are managed in P4V)." : g.Error;
            return r;
        }
        std::string out, err;
        if (Trim(name).empty() || GitRun(g, "check-ref-format --branch " + Quote(Trim(name)), out, err) != 0)
        {
            r.Output = "'" + name + "' is not a valid branch name (no spaces, no .. ~ ^ : ? * [ or trailing /).";
            return r;
        }
        // Yeni dal HEAD'den açılır: kaydedilmemiş değişiklikler çalışma kopyasında kalır (git switch -c bunları taşır, kaybetmez)
        const int code = GitRun(g, (switchTo ? "switch -c " : "branch ") + Quote(Trim(name)), out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? (switchTo ? "Created and switched to branch " : "Created branch ") + Trim(name) + "." : ErrorText(out, err);
        return r;
    }

    Result SwitchBranch(const Context& ctx, const Branch& branch)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Branches are a Git feature here." : g.Error;
            return r;
        }
        if (branch.Current)
        {
            r.Ok = true;
            r.Output = "Already on " + branch.Name + ".";
            return r;
        }
        std::string out, err;
        // Kaydedilmemiş değişiklikle dal değiştirmek onları diğer dala taşır ya da üzerine yazılacağı için git durdurur: önce Commit / Revert
        if (GitRun(g, "status --porcelain=v1 -uno -z", out, err) == 0 && !out.empty())
        {
            int n = 0;
            for (const std::string& s : SplitNul(out))
                if (s.size() > 3 && s[2] == ' ')
                    ++n;
            r.Output = "Cannot switch to " + branch.Name + ": " + std::to_string(std::max(n, 1)) +
                       " file(s) have uncommitted changes. Switching would carry them over to the other branch or overwrite them; "
                       "commit or revert them first (Changes tab).";
            return r;
        }
        if (!GitOperation(g).empty())
        {
            r.Output = "A " + GitOperation(g) + " is in progress: Continue or Abort it first.";
            return r;
        }
        std::string cmd;
        if (branch.Remote)
        {
            const size_t slash = branch.Name.find('/');
            const std::string local = slash == std::string::npos ? branch.Name : branch.Name.substr(slash + 1);
            if (!GitLine(g, "rev-parse -q --verify " + Quote("refs/heads/" + local)).empty())
                cmd = "switch " + Quote(local);
            else
                cmd = "switch --track " + Quote(branch.Name);
        }
        else
            cmd = "switch " + Quote(branch.Name);
        const int code = GitRun(g, cmd, out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? "Switched to " + GitLine(g, "symbolic-ref --short -q HEAD") + "." : ErrorText(out, err);
        return r;
    }

    Result DeleteBranch(const Context& ctx, const std::string& name, bool force)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Branches are a Git feature here." : g.Error;
            return r;
        }
        if (GitLine(g, "symbolic-ref --short -q HEAD") == name)
        {
            r.Output = "Cannot delete the current branch: switch to another branch first.";
            return r;
        }
        std::string out, err;
        const int code = GitRun(g, std::string("branch ") + (force ? "-D " : "-d ") + Quote(name), out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? "Deleted branch " + name + "." : ErrorText(out, err);
        if (!r.Ok && r.Output.find("not fully merged") != std::string::npos)
        {
            r.NeedsForce = true;
            r.Output = "Branch " + name + " has commits that are not merged into the current branch. Force Delete loses them.";
        }
        return r;
    }

    Result AddRemote(const Context& ctx, const std::string& name, const std::string& url)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Remotes are a Git feature." : g.Error;
            return r;
        }
        if (Trim(name).empty() || Trim(url).empty())
        {
            r.Output = "Enter a remote name and URL.";
            return r;
        }
        std::string out, err;
        const int code = GitRun(g, "remote add " + Quote(Trim(name)) + " " + Quote(Trim(url)), out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? "Added remote " + Trim(name) + "." : ErrorText(out, err);
        return r;
    }

    Result ResolveConflict(const Context& ctx, const std::string& repoPath, Resolve how)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = ctx.Mode() != 1 ? "Resolve Perforce files with p4 resolve or P4V." : g.Error;
            return r;
        }
        std::string out, err;
        if (how != Resolve::MarkResolved)
        {
            // Rebase'de taraflar ters: "ours" çekilen (üst akış) dal, "theirs" yeniden uygulanan kendi kaydınız
            const bool rebase = GitOperation(g) == "rebase";
            const bool ours = (how == Resolve::Mine) != rebase;
            if (GitRun(g, std::string("checkout ") + (ours ? "--ours" : "--theirs") + " -- " + Quote(repoPath), out, err) != 0)
            {
                // Seçilen tarafta dosya yok (orada silinmiş): dosya silinir
                if (GitRun(g, "rm -q -- " + Quote(repoPath), out, err) != 0)
                {
                    r.Output = ErrorText(out, err);
                    return r;
                }
                r.Ok = true;
                r.Output = "Resolved " + ToProject(g, repoPath) + " (deleted on that side).";
                return r;
            }
        }
        const int code = GitRun(g, "add -- " + Quote(repoPath), out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? "Resolved " + ToProject(g, repoPath) + (how == Resolve::Mine ? " with your version." : how == Resolve::Theirs ? " with their version." : ".")
                        : ErrorText(out, err);
        return r;
    }

    Result ContinueOperation(const Context& ctx)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = g.Ok ? "Version control is off." : g.Error;
            return r;
        }
        std::string out, err;
        if (GitRun(g, "diff --name-only --diff-filter=U -z", out, err) == 0 && !out.empty())
        {
            r.Output = std::to_string(SplitNul(out).size()) + " file(s) still have conflicts: resolve them first.";
            return r;
        }
        const std::string op = GitOperation(g);
        int code = 0;
        if (op == "rebase")
            code = GitRun(g, "-c core.editor=true rebase --continue", out, err);
        else if (op == "merge")
            code = GitRun(g, "commit --no-edit -q", out, err);
        else if (op == "cherry-pick")
            code = GitRun(g, "-c core.editor=true cherry-pick --continue", out, err);
        else
        {
            r.Ok = true;
            r.Output = "Nothing to continue.";
            return r;
        }
        r.Ok = code == 0;
        r.Output = ErrorText(out, err);
        if (r.Ok)
            r.Output = "Finished the " + op + ".";
        return r;
    }

    Result AbortOperation(const Context& ctx)
    {
        Result r;
        const GitRepo g = OpenGit(ctx);
        if (ctx.Mode() != 1 || !g.Ok)
        {
            r.Output = g.Ok ? "Version control is off." : g.Error;
            return r;
        }
        const std::string op = GitOperation(g);
        if (op.empty())
        {
            r.Ok = true;
            r.Output = "Nothing to abort.";
            return r;
        }
        std::string out, err;
        const int code = GitRun(g, op + " --abort", out, err);
        r.Ok = code == 0;
        r.Output = r.Ok ? "Aborted the " + op + ": the files are back as they were before the pull." : ErrorText(out, err);
        return r;
    }

    Result GetLatest(const Context& ctx)
    {
        Result r;
        if (ctx.Mode() == 1)
            return Pull(ctx, true);
        if (ctx.Mode() != 2)
        {
            r.Output = "Version control is off.";
            return r;
        }
        std::string out, err;
        const int code = RunSplit(ctx.ProjectDir, P4(ctx) + " sync " + P4All(ctx), out, err);
        r.Ok = code == 0 || P4Harmless(err);
        int updated = 0;
        for (const std::string& l : SplitLines(out))
            if (l.find(" - updating ") != std::string::npos || l.find(" - added as ") != std::string::npos ||
                l.find(" - deleted as ") != std::string::npos || l.find(" - refreshing ") != std::string::npos)
                ++updated;
        r.Output = r.Ok ? (updated ? "Got latest: " + std::to_string(updated) + " file(s) updated." : std::string("Got latest: the workspace is up to date."))
                        : ErrorText(out, err);
        if (r.Ok && !Trim(out).empty())
            r.Output += "\n" + Trim(out);
        return r;
    }
}
