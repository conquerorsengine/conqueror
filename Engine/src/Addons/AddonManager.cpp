#include "AddonManager.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/DynamicLibrary.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;

        struct State
        {
            std::vector<AddonManifest> Installed;
            std::vector<AddonManifest> Catalog;
            std::set<std::string> Disabled;   // kurulu ama kapalı (varsayılan: açık)
            fs::path LoadedFor;               // devre dışı listesinin okunduğu Packages klasörü
            uint32_t Version = 1;
        };

        State& S()
        {
            static State s;
            return s;
        }

        std::string Str(const YAML::Node& n, const char* key, const std::string& def = "")
        {
            return n[key] && n[key].IsScalar() ? n[key].as<std::string>() : def;
        }

        std::string Slug(const std::string& name)
        {
            std::string out;
            for (unsigned char c : name)
            {
                if (std::isalnum(c)) out += (char)std::tolower(c);
                else if (!out.empty() && out.back() != '-') out += '-';
            }
            while (!out.empty() && out.back() == '-') out.pop_back();
            return out.empty() ? "addon" : out;
        }

        void ScanDirectory(const fs::path& root, bool builtIn, std::vector<AddonManifest>& out)
        {
            std::error_code ec;
            if (!fs::is_directory(root, ec))
                return;
            for (auto& entry : fs::directory_iterator(root, ec))
            {
                if (!entry.is_directory(ec))
                    continue;
                fs::path manifest = entry.path() / "addon.yaml";
                if (!fs::exists(manifest, ec))
                    continue;
                AddonManifest m;
                std::string error;
                if (!AddonManager::LoadManifest(manifest, m, &error))
                {
                    CQ_CORE_WARN("[Addons] {0}: {1}", manifest.string(), error);
                    continue;
                }
                m.BuiltIn = builtIn;
                m.Installed = !builtIn;
                out.push_back(std::move(m));
            }
            std::sort(out.begin(), out.end(), [](const AddonManifest& a, const AddonManifest& b) { return a.Name < b.Name; });
        }

        fs::path StateFile() { return AddonManager::GetPackagesDirectory() / "addons.yaml"; }

        void LoadState()
        {
            auto& s = S();
            s.Disabled.clear();
            s.LoadedFor = AddonManager::GetPackagesDirectory();
            std::error_code ec;
            if (!fs::exists(StateFile(), ec))
                return;
            try
            {
                YAML::Node root = YAML::LoadFile(StateFile().string());
                if (auto d = root["Disabled"]; d && d.IsSequence())
                    for (auto v : d)
                        s.Disabled.insert(v.as<std::string>());
            }
            catch (const std::exception& e)
            {
                CQ_CORE_WARN("[Addons] addons.yaml: {0}", e.what());
            }
        }

        void SaveState()
        {
            std::error_code ec;
            fs::create_directories(AddonManager::GetPackagesDirectory(), ec);
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "Disabled" << YAML::Value << YAML::BeginSeq;
            for (auto& id : S().Disabled)
                out << id;
            out << YAML::EndSeq << YAML::EndMap;
            std::ofstream f(StateFile());
            f << out.c_str() << "\n";
        }
    }

    fs::path AddonManager::GetPackagesDirectory()
    {
        fs::path project = Project::GetActiveProjectDirectory();
        return project.empty() ? fs::current_path() / "Packages" : project / "Packages";
    }

    fs::path AddonManager::GetCatalogDirectory()
    {
        std::error_code ec;
        fs::path exeDir = DynamicLibrary::ExecutableDirectory() / "Resources" / "Addons";
        if (fs::is_directory(exeDir, ec))
            return exeDir;
        return fs::current_path() / "Resources" / "Addons";
    }

    bool AddonManager::LoadManifest(const fs::path& manifestFile, AddonManifest& m, std::string* error)
    {
        try
        {
            YAML::Node n = YAML::LoadFile(manifestFile.string());
            m.Directory = manifestFile.parent_path();
            m.Name = Str(n, "Name", m.Directory.filename().string());
            m.Id = Str(n, "Id", "local." + Slug(m.Name));
            m.Version = Str(n, "Version", "1.0.0");
            m.Author = Str(n, "Author");
            m.Description = Str(n, "Description");
            m.Category = Str(n, "Category", "Tools");
            m.Icon = Str(n, "Icon", "puzzle-piece");
            m.EditorScript = Str(n, "Editor");
            if (auto c = n["Components"]; c && c.IsSequence())
                for (auto v : c) m.Components.push_back(v.as<std::string>());
            if (auto t = n["Tags"]; t && t.IsSequence())
                for (auto v : t) m.Tags.push_back(v.as<std::string>());
            if (auto d = n["Dependencies"]; d && d.IsSequence())
                for (auto v : d) m.Dependencies.push_back(v.as<std::string>());
            if (auto ins = n["Inserts"]; ins && ins.IsSequence())
                for (auto v : ins)
                {
                    AddonInsertItem item;
                    item.Name = Str(v, "Name", "Object");
                    item.Icon = Str(v, "Icon", "cube");
                    item.Prefab = Str(v, "Prefab");
                    item.Tooltip = Str(v, "Tooltip");
                    if (!item.Prefab.empty())
                        m.Inserts.push_back(item);
                }
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    void AddonManager::Refresh()
    {
        auto& s = S();
        s.Installed.clear();
        s.Catalog.clear();
        ScanDirectory(GetPackagesDirectory(), false, s.Installed);
        ScanDirectory(GetCatalogDirectory(), true, s.Catalog);
        LoadState();
        ++s.Version;
    }

    const std::vector<AddonManifest>& AddonManager::GetInstalled()
    {
        if (S().LoadedFor != GetPackagesDirectory())
            Refresh();
        return S().Installed;
    }

    const std::vector<AddonManifest>& AddonManager::GetCatalog()
    {
        if (S().LoadedFor != GetPackagesDirectory())
            Refresh();
        return S().Catalog;
    }

    const AddonManifest* AddonManager::FindInstalled(const std::string& id)
    {
        for (auto& m : GetInstalled())
            if (m.Id == id) return &m;
        return nullptr;
    }

    const AddonManifest* AddonManager::FindInCatalog(const std::string& id)
    {
        for (auto& m : GetCatalog())
            if (m.Id == id) return &m;
        return nullptr;
    }

    bool AddonManager::IsEnabled(const std::string& id)
    {
        GetInstalled();
        return FindInstalled(id) && !S().Disabled.count(id);
    }

    void AddonManager::SetEnabled(const std::string& id, bool enabled)
    {
        auto& s = S();
        bool changed = enabled ? s.Disabled.erase(id) > 0 : s.Disabled.insert(id).second;
        if (!changed)
            return;
        SaveState();
        ++s.Version;
    }

    bool AddonManager::Install(const std::string& catalogId, std::string* error)
    {
        const AddonManifest* m = FindInCatalog(catalogId);
        if (!m)
        {
            if (error) *error = "Addon '" + catalogId + "' is not in the catalog";
            return false;
        }
        // Bağımlılıklar önce (döngüye karşı: kurulmakta olanlar atlanır)
        static std::vector<std::string> s_Installing;
        if (std::find(s_Installing.begin(), s_Installing.end(), catalogId) != s_Installing.end())
            return true;
        const std::vector<std::string> deps = m->Dependencies;
        s_Installing.push_back(catalogId);
        for (const auto& dep : deps)
        {
            if (FindInstalled(dep))
                continue;
            std::string depError;
            if (!Install(dep, &depError))
            {
                s_Installing.pop_back();
                if (error) *error = "Dependency " + dep + ": " + depError;
                return false;
            }
        }
        s_Installing.pop_back();
        m = FindInCatalog(catalogId);   // bağımlılık kurulumu listeleri yeniden taradı
        if (!m)
        {
            if (error) *error = "Addon '" + catalogId + "' is not in the catalog";
            return false;
        }
        std::error_code ec;
        fs::path target = GetPackagesDirectory() / m->Directory.filename();
        fs::create_directories(GetPackagesDirectory(), ec);
        if (fs::exists(target, ec))
            fs::remove_all(target, ec);   // güncelleme: katalog sürümüyle değiştir
        fs::copy(m->Directory, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            if (error) *error = ec.message();
            return false;
        }
        CQ_CORE_INFO("[Addons] Installed {0} {1}", m->Name, m->Version);
        S().Disabled.erase(catalogId);
        SaveState();
        Refresh();
        return true;
    }

    bool AddonManager::Uninstall(const std::string& id, std::string* error)
    {
        const AddonManifest* m = FindInstalled(id);
        if (!m)
        {
            if (error) *error = "Addon '" + id + "' is not installed";
            return false;
        }
        std::error_code ec;
        fs::path dir = m->Directory;
        // Güvenlik: yalnızca Packages altındaki klasörler silinir
        fs::path packages = fs::weakly_canonical(GetPackagesDirectory(), ec);
        fs::path canonical = fs::weakly_canonical(dir, ec);
        auto rel = fs::relative(canonical, packages, ec);
        if (ec || rel.empty() || rel.native()[0] == '.')
        {
            if (error) *error = "Refusing to delete a folder outside Packages: " + dir.string();
            return false;
        }
        fs::remove_all(canonical, ec);
        if (ec)
        {
            if (error) *error = ec.message();
            return false;
        }
        CQ_CORE_INFO("[Addons] Uninstalled {0}", id);
        S().Disabled.erase(id);
        SaveState();
        Refresh();
        return true;
    }

    bool AddonManager::CreateNew(const std::string& name, std::string* outId, std::string* error)
    {
        std::string clean = name.empty() ? std::string("My Addon") : name;
        std::string slug = Slug(clean);
        std::error_code ec;
        fs::path dir = GetPackagesDirectory() / slug;
        for (int i = 2; fs::exists(dir, ec) && i < 100; ++i)
            dir = GetPackagesDirectory() / (slug + "-" + std::to_string(i));
        fs::create_directories(dir / "Editor", ec);
        fs::create_directories(dir / "Scripts", ec);
        if (ec)
        {
            if (error) *error = ec.message();
            return false;
        }
        // Betik sınıf adı: boşluksuz PascalCase
        std::string cls;
        bool up = true;
        for (unsigned char c : clean)
        {
            if (!std::isalnum(c)) { up = true; continue; }
            cls += up ? (char)std::toupper(c) : (char)c;
            up = false;
        }
        if (cls.empty() || std::isdigit((unsigned char)cls[0])) cls = "Addon" + cls;
        std::string id = "local." + dir.filename().string();

        std::ofstream manifest(dir / "addon.yaml");
        manifest << "Id: " << id << "\n"
                 << "Name: " << clean << "\n"
                 << "Version: 1.0.0\n"
                 << "Author: You\n"
                 << "Description: A new Conqueror addon. Edit Editor/" << cls << "Editor.cqs to add menus, tool buttons and panels.\n"
                 << "Category: Tools\n"
                 << "Icon: puzzle-piece\n"
                 << "Editor: Editor/" << cls << "Editor.cqs\n"
                 << "Components: []\n";
        manifest.close();

        std::ofstream script(dir / "Editor" / (cls + "Editor.cqs"));
        script << "using nativebridge;\n\n"
               << "// Editör eklentisi: OnLoad'da menü / araç düğmesi / panel kaydedilir.\n"
               << "// Menü ve düğme çağrıları otomatik olarak geri alınabilir (Ctrl+Z).\n"
               << "script " << cls << "Editor : ConquerorScript\n"
               << "{\n"
               << "    public int count = 3;\n"
               << "    public float radius = 4.0;\n\n"
               << "    func OnLoad()\n"
               << "    {\n"
               << "        EditorAddMenuItem(\"" << clean << "/Create Ring\", \"CreateRing\");\n"
               << "        EditorAddToolButton(\"Ring\", \"circle-nodes\", \"CreateRing\", \"Create a ring of cubes around the camera target\");\n"
               << "        EditorAddPanel(\"" << clean << "\", \"DrawPanel\");\n"
               << "    }\n\n"
               << "    func CreateRing()\n"
               << "    {\n"
               << "        var c = EditorGetCameraTarget();\n"
               << "        for (var i = 0; i < count; i++)\n"
               << "        {\n"
               << "            var a = i * 6.2831853 / count;\n"
               << "            var e = CreateEntity(\"Ring \" + str(i));\n"
               << "            EntityAddComponent(e, \"MeshRenderer\");\n"
               << "            EntitySetPosition(e, c.x + MathCos(a) * radius, c.y + 0.5, c.z + MathSin(a) * radius);\n"
               << "        }\n"
               << "        EditorNotify(\"Created \" + str(count) + \" cubes\");\n"
               << "    }\n\n"
               << "    func DrawPanel()\n"
               << "    {\n"
               << "        GuiText(\"Ring generator\");\n"
               << "        count = GuiSliderInt(\"Count\", count, 1, 64);\n"
               << "        radius = GuiSliderFloat(\"Radius\", radius, 0.5, 50.0);\n"
               << "        if (GuiButton(\"Create Ring\")) { EditorRunUndoable(\"Create Ring\", \"CreateRing\"); }\n"
               << "    }\n"
               << "}\n";
        script.close();

        Refresh();
        if (outId) *outId = id;
        CQ_CORE_INFO("[Addons] Created addon template at {0}", dir.string());
        return true;
    }

    uint32_t AddonManager::GetVersion()
    {
        return S().Version;
    }
}
