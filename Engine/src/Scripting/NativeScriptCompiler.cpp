#include "NativeScriptCompiler.h"

#include "Scripting/ScriptEngine.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Core/Project/Project.h"
#include "Core/Project/PlatformModules.h"
#include "Core/Utils/DynamicLibrary.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
    #define CQ_NS_POPEN _popen
    #define CQ_NS_PCLOSE _pclose
#else
    #define CQ_NS_POPEN popen
    #define CQ_NS_PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace Conqueror::NativeScriptCompiler
{
    namespace
    {
        const char* LibExt()
        {
#if defined(_WIN32)
            return ".dll";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return ".so";
#endif
        }

        std::string LibName(const fs::path& source)
        {
#if defined(_WIN32)
            return source.stem().string() + LibExt();
#else
            return "lib" + source.stem().string() + LibExt();
#endif
        }

        std::string Quote(const fs::path& p) { return "\"" + p.string() + "\""; }

        std::string Run(const std::string& cmd, int& exitCode)
        {
#ifdef _WIN32
            const std::string full = cmd + " 2>&1";
#else
            // Derleyici iletileri İngilizce (GCC yerelleştirir)
            const std::string full = "LC_ALL=C LANG=C " + cmd + " 2>&1";
#endif
            std::string output;
            FILE* pipe = CQ_NS_POPEN(full.c_str(), "r");
            if (!pipe)
            {
                exitCode = -1;
                return "failed to start the compiler";
            }
            char buf[4096];
            while (std::fgets(buf, sizeof(buf), pipe))
                output += buf;
            exitCode = CQ_NS_PCLOSE(pipe);
            return output;
        }
    }

    bool IsSource(const std::string& path)
    {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return ext == ".cpp" || ext == ".cc" || ext == ".cxx";
    }

    fs::path CompiledLibrary(const fs::path& source)
    {
        fs::path project = Project::GetActive() ? Project::GetActiveProjectDirectory() : source.parent_path();
        return project / "Library" / "NativeScripts" / LibName(source);
    }

    fs::path PackagedLibrary(const fs::path& projectDir, const fs::path& source)
    {
        return projectDir / "NativeScripts" / LibName(source);
    }

    std::string FindCompiler()
    {
        if (const char* cxx = std::getenv("CXX"); cxx && *cxx)
            return cxx;
        for (const char* c : { "c++", "clang++", "g++" })
            if (!PlatformModules::FindProgram(c).empty())
                return c;
        return {};
    }

    fs::path EngineSourceRoot()
    {
        std::error_code ec;
        auto ok = [&](const fs::path& p) {
            return fs::exists(p / "Engine" / "src" / "Scripting" / "ScriptableEntity.h", ec) && fs::is_directory(p / "External" / "entt", ec);
        };
        if (const char* env = std::getenv("CQ_ENGINE_SOURCE"); env && *env && ok(env))
            return env;
        for (fs::path probe = DynamicLibrary::ExecutableDirectory(); !probe.empty(); probe = probe.parent_path())
        {
            if (ok(probe))
                return probe;
            if (!probe.has_parent_path() || probe.parent_path() == probe)
                break;
        }
        return {};
    }

    Result Compile(const fs::path& source, bool force)
    {
        Result r;
        std::error_code ec;
        r.Library = CompiledLibrary(source);
        if (!fs::exists(source, ec))
        {
            r.Log = "Native script source not found: " + source.string();
            return r;
        }
        if (!force && fs::exists(r.Library, ec) && fs::last_write_time(r.Library, ec) >= fs::last_write_time(source, ec))
        {
            r.Success = true;
            return r;
        }
#if defined(_WIN32)
        // Windows'ta motor statik bağlanır: ScriptableEntity sınıfları ayrı DLL'de motor sembollerine bağlanamaz
        r.Log = "Native C++ scripts (ScriptableEntity) need the engine as a shared library (Linux / macOS). On Windows use C++ "
                "scripts on the Script component (Create > Script > C++), which compile everywhere.";
        return r;
#else
        const std::string cxx = FindCompiler();
        if (cxx.empty())
        {
            r.Log = "No C++ compiler found: install g++ or clang++, or set CXX.";
            return r;
        }
        const fs::path root = EngineSourceRoot();
        if (root.empty())
        {
            r.Log = "Engine headers not found next to the editor: set CQ_ENGINE_SOURCE to the engine source folder.";
            return r;
        }
        fs::create_directories(r.Library.parent_path(), ec);
        const fs::path tmp = r.Library.string() + ".new";
        std::string cmd = cxx + " -std=c++23 -O2 -g -fPIC -shared -fvisibility=hidden -DGLM_ENABLE_EXPERIMENTAL";
        for (const char* inc : { "Engine/src", "External/glm", "External/entt/single_include", "External/spdlog/include", "External/yaml-cpp/include",
                                 "External/imgui", "External/IconFontCppHeaders" })
            cmd += " -I" + Quote(root / inc);
#if defined(__APPLE__)
        cmd += " -undefined dynamic_lookup";    // motor sembolleri yükleyen süreçten
#endif
        cmd += " " + Quote(source) + " -o " + Quote(tmp);
        CQ_CORE_INFO("Compiling native script {0}", source.filename().string());
        int code = 0;
        r.Log = Run(cmd, code);
        if (code != 0 || !fs::exists(tmp, ec))
        {
            fs::remove(tmp, ec);
            if (r.Log.empty())
                r.Log = "compiler exit code " + std::to_string(code);
            return r;
        }
        // Yeni dosya + yeniden adlandırma: yüklü eski kitaplığın eşlemesi bozulmaz
        fs::rename(tmp, r.Library, ec);
        if (ec)
        {
            r.Log = "Cannot write " + r.Library.string() + ": " + ec.message();
            return r;
        }
        r.Success = true;
        r.Rebuilt = true;
        return r;
#endif
    }

    Result Resolve(const fs::path& source)
    {
        std::error_code ec;
        // Derlenmiş oyun: kitaplık oyunla gelir (derleyici gerekmez)
        if (Project::GetActive())
        {
            const fs::path packaged = PackagedLibrary(Project::GetActiveProjectDirectory(), source);
            if (fs::exists(packaged, ec))
            {
                Result r;
                r.Success = true;
                r.Library = packaged;
                return r;
            }
        }
        return Compile(source, false);
    }

    void PrepareScene(Scene& scene)
    {
        auto view = scene.m_Registry.view<NativeScriptComponent>();
        for (auto handle : view)
        {
            auto& sc = view.get<NativeScriptComponent>(handle);
            for (auto& script : sc.Scripts)
            {
                if (script.ScriptPath.empty() || !IsSource(script.ScriptPath))
                    continue;
                fs::path source = script.ScriptPath;
                if (source.is_relative() && Project::GetActive())
                    source = Project::GetActiveProjectDirectory() / source;
                const std::string moduleName = script.ModuleName.empty() ? source.stem().string() : script.ModuleName;
                const Result r = Resolve(source);
                if (!r.Success)
                {
                    CQ_CORE_ERROR("Native script {0} could not be compiled:\n{1}", source.filename().string(), r.Log);
                    script.InstantiateScript = nullptr;
                    continue;
                }
                // Kaynak değişti: eski modül bırakılır (Play başında örnek yok), yenisi yüklenir
                if (r.Rebuilt && ScriptEngine::IsModuleLoaded(moduleName))
                    ScriptEngine::UnloadModule(moduleName);
                if (!ScriptEngine::IsModuleLoaded(moduleName) && !ScriptEngine::LoadModule(moduleName, r.Library.string()))
                {
                    CQ_CORE_ERROR("Native script module could not be loaded: {0}", r.Library.string());
                    continue;
                }
                std::string className = script.ClassName;
                if (className.empty())
                {
                    const auto classes = ScriptEngine::GetModuleClassNames(moduleName);
                    if (classes.empty())
                    {
                        CQ_CORE_ERROR("{0}: no CQ_NATIVE_SCRIPT(ClassName) found", source.filename().string());
                        continue;
                    }
                    className = classes.front();
                }
                script.ModuleName = moduleName;
                script.ClassName = className;
                script.InstantiateScript = [moduleName, className]() { return ScriptEngine::CreateScriptInstance(moduleName, className); };
                if (r.Rebuilt)
                    CQ_CORE_INFO("Native script {0} compiled and loaded ({1})", className, r.Library.filename().string());
            }
        }
    }
}
