#include "NativeModules.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSettings.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace Conqueror::NativeModules
{
    namespace fs = std::filesystem;

    namespace
    {
        const char* CurrentPlatform()
        {
#if defined(__ANDROID__)
            return "Android";
#elif defined(__EMSCRIPTEN__)
            return "Web";
#elif defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#else
            return "Linux";
#endif
        }

        std::string Quote(const fs::path& p)
        {
            return "\"" + p.string() + "\"";
        }

        bool IsInside(const fs::path& folder, const fs::path& file)
        {
            const std::string a = folder.generic_string() + "/", b = file.generic_string();
            return b.rfind(a, 0) == 0;
        }

        std::string SafeName(std::string s)
        {
            for (char& c : s)
                if (!std::isalnum((unsigned char)c))
                    c = '_';
            return s;
        }
    }

    std::vector<Module> Resolve()
    {
        std::vector<Module> modules;
        for (const auto& file : SimpleAssets::FindAll("NativeScriptModule"))
        {
            Module m;
            m.Name = fs::path(file).stem().string();
            m.Folder = fs::path(file).parent_path();
            std::ifstream in(file, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            m.Text = ss.str();
            if (!SimpleAssets::Load(file, m.Data))
            {
                m.Active = false;
                m.Why = "the file could not be read";
            }
            modules.push_back(std::move(m));
        }
        const auto& symbols = ProjectSettings::ScriptDefines();
        for (auto& m : modules)
        {
            if (!m.Active)
                continue;
            if (!m.Data.Bool("Enabled", true))
            {
                m.Active = false;
                m.Why = "disabled";
                continue;
            }
            for (const auto& row : m.Data.Table("Exclude Platforms"))
                if (SimpleAssets::Cell(row, 0) == CurrentPlatform())
                {
                    m.Active = false;
                    m.Why = std::string("excluded on ") + CurrentPlatform();
                }
            for (const auto& row : m.Data.Table("Define Constraints"))
            {
                const std::string symbol = SimpleAssets::Cell(row, 0);
                if (!symbol.empty() && std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
                {
                    m.Active = false;
                    m.Why = "symbol " + symbol + " is not defined";
                }
            }
        }
        // Başvurular: eksik ya da dışlanmış modüle bağlı olan da dışlanır (zincir)
        for (bool again = true; again;)
        {
            again = false;
            for (auto& m : modules)
            {
                if (!m.Active)
                    continue;
                for (const auto& row : m.Data.Table("References"))
                {
                    const std::string name = SimpleAssets::Cell(row, 0);
                    if (name.empty())
                        continue;
                    auto it = std::find_if(modules.begin(), modules.end(), [&](const Module& o) { return o.Name == name; });
                    if (it == modules.end() || !it->Active)
                    {
                        m.Active = false;
                        m.Why = "reference " + name + (it == modules.end() ? " is missing" : " is excluded");
                        again = true;
                        break;
                    }
                }
            }
        }
        return modules;
    }

    std::string Fingerprint(const std::vector<Module>& modules)
    {
        std::string text;
        for (const auto& m : modules)
            text += m.Folder.string() + "\n" + m.Text + (m.Active ? "1" : "0");
        for (const auto& s : ProjectSettings::ScriptDefines())
            text += s + "\n";
        return text;
    }

    const Module* ForSource(const std::vector<Module>& modules, const fs::path& source)
    {
        const Module* best = nullptr;
        for (const auto& m : modules)
            if (IsInside(m.Folder, source) && (!best || m.Folder.string().size() > best->Folder.string().size()))
                best = &m;
        return best;
    }

    std::string CompilerFlags(const Module& module)
    {
        std::string flags;
        for (const auto& row : module.Data.Table("Defines"))
        {
            const std::string name = SimpleAssets::Cell(row, 0);
            if (!name.empty())
                flags += " -D" + name + "=" + SimpleAssets::Cell(row, 1, "1");
        }
        for (const auto& symbol : ProjectSettings::ScriptDefines())
            flags += " -D" + symbol + "=1";
        for (const auto& row : module.Data.Table("Include Directories"))
            if (const std::string dir = SimpleAssets::Cell(row, 0); !dir.empty())
                flags += " -I" + Quote(Project::GetActiveProjectDirectory() / dir);
        flags += " -I" + Quote(module.Folder);
        return flags;
    }

    std::vector<fs::path> ApplyModules(const std::vector<Module>& modules, const std::vector<fs::path>& sources, const fs::path& outDir,
                                       const std::string& extension, std::vector<fs::path>& includes)
    {
        includes.clear();
        std::vector<fs::path> units;
        std::map<std::string, int> counters;
        std::error_code ec;
        const fs::path dir = outDir / "modules";
        fs::create_directories(dir, ec);
        for (const auto& m : modules)
        {
            if (!m.Active)
                continue;
            includes.push_back(m.Folder);
            for (const auto& row : m.Data.Table("Include Directories"))
                if (const std::string d = SimpleAssets::Cell(row, 0); !d.empty())
                    includes.push_back(Project::GetActiveProjectDirectory() / d);
        }
        for (const auto& source : sources)
        {
            const Module* module = ForSource(modules, source);
            if (!module)
            {
                units.push_back(source);
                continue;
            }
            if (!module->Active)
            {
                CQ_CORE_INFO("[Native Script Module] {0} left out: {1}", source.filename().string(), module->Why);
                continue;
            }
            // Modülün tanımları yalnız kendi kaynaklarını etkiler: sarmalayıcı birim
            std::string text = "// Auto-generated by Native Script Module " + module->Name + "\n";
            std::vector<std::string> names;
            auto define = [&](const std::string& name, const std::string& value) {
                text += "#define " + name + " " + value + "\n";
                names.push_back(name);
            };
            for (const auto& row : module->Data.Table("Defines"))
                if (const std::string name = SimpleAssets::Cell(row, 0); !name.empty())
                    define(name, SimpleAssets::Cell(row, 1, "1"));
            for (const auto& symbol : ProjectSettings::ScriptDefines())
                define(symbol, "1");
            text += "#include \"" + source.generic_string() + "\"\n";
            for (const auto& name : names)
                text += "#undef " + name + "\n";
            const fs::path unit = dir / (SafeName(module->Name) + "_" + std::to_string(counters[module->Name]++) + extension);
            std::string existing;
            {
                std::ifstream in(unit, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                existing = ss.str();
            }
            if (existing != text)
                std::ofstream(unit, std::ios::binary) << text;
            units.push_back(unit);
        }
        return units;
    }
}
