#include "CQShaderLoader.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/FileSystem.h"

#include <filesystem>
#include <set>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        constexpr int kMaxIncludeDepth = 16;

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return {};
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        std::string ResolveInclude(const std::string& includeName, const std::filesystem::path& currentDir)
        {
            std::error_code ec;
            if (!currentDir.empty())
            {
                auto candidate = currentDir / includeName;
                if (std::filesystem::exists(candidate, ec))
                    return candidate.lexically_normal().string();
            }
            std::string viaRoot = FileSystem::ResolveResourcePath("Engine/src/Shaders/" + includeName);
            if (std::filesystem::exists(viaRoot, ec))
                return viaRoot;
            std::string direct = FileSystem::ResolveResourcePath(includeName);
            if (std::filesystem::exists(direct, ec))
                return direct;
            return {};
        }

        bool ExpandIncludes(const std::string& source, const std::filesystem::path& currentDir,
                            std::set<std::string>& included, std::vector<std::string>& deps,
                            std::string& out, int depth, const std::string& originName)
        {
            if (depth > kMaxIncludeDepth)
            {
                CQ_CORE_ERROR("CQShaderLoader: include depth exceeded in '{0}'", originName);
                return false;
            }

            std::istringstream stream(source);
            std::string line;
            int lineNo = 0;
            bool ok = true;
            while (std::getline(stream, line))
            {
                ++lineNo;
                std::string t = Trim(line);
                // Her aşama (#type) ayrı derleme birimidir: tek-sefer korumasını sıfırla
                if (depth == 0 && t.rfind("#type", 0) == 0)
                    included.clear();
                if (t.rfind("#include", 0) == 0)
                {
                    size_t q1 = t.find_first_of("\"<");
                    size_t q2 = (q1 == std::string::npos) ? std::string::npos : t.find_first_of("\">", q1 + 1);
                    if (q1 == std::string::npos || q2 == std::string::npos)
                    {
                        CQ_CORE_ERROR("CQShaderLoader: malformed #include at {0}:{1}", originName, lineNo);
                        ok = false;
                        continue;
                    }
                    std::string name = t.substr(q1 + 1, q2 - q1 - 1);
                    std::string resolved = ResolveInclude(name, currentDir);
                    if (resolved.empty())
                    {
                        CQ_CORE_ERROR("CQShaderLoader: include '{0}' not found ({1}:{2})", name, originName, lineNo);
                        ok = false;
                        continue;
                    }
                    std::string key = std::filesystem::weakly_canonical(resolved).string();
                    if (included.count(key))
                        continue; // tek seferlik dahil etme
                    included.insert(key);
                    deps.push_back(resolved);

                    std::string content;
                    if (!FileSystem::ReadFile(resolved, content))
                    {
                        CQ_CORE_ERROR("CQShaderLoader: could not read include '{0}'", resolved);
                        ok = false;
                        continue;
                    }
                    out += "// ---- begin include: " + name + "\n";
                    ok &= ExpandIncludes(content, std::filesystem::path(resolved).parent_path(), included, deps, out, depth + 1, resolved);
                    out += "// ---- end include: " + name + "\n";
                    continue;
                }
                out += line;
                out += "\n";
            }
            return ok;
        }
    }

    CQShaderSource CQShaderLoader::Load(const std::string& filepath)
    {
        std::string rawContent;
        std::string resolved = FileSystem::ResolveResourcePath(filepath);
        if (!FileSystem::ReadFile(resolved, rawContent))
        {
            CQ_CORE_ERROR("CQShaderLoader: Could not read file '{0}'", filepath);
            return {};
        }

        CQShaderSource header = ParseHeader(rawContent);
        CQShaderSource result;
        result.Language = header.Language;
        result.Dependencies.push_back(resolved);

        std::set<std::string> included;
        std::error_code ec;
        included.insert(std::filesystem::weakly_canonical(resolved, ec).string());
        ExpandIncludes(header.Source, std::filesystem::path(resolved).parent_path(), included, result.Dependencies, result.Source, 0, filepath);
        return result;
    }

    CQShaderSource CQShaderLoader::LoadFromString(const std::string& source, const std::string& virtualPath)
    {
        CQShaderSource header = ParseHeader(source);
        CQShaderSource result;
        result.Language = header.Language;
        std::set<std::string> included;
        std::filesystem::path dir = virtualPath.empty() ? std::filesystem::path() : std::filesystem::path(virtualPath).parent_path();
        ExpandIncludes(header.Source, dir, included, result.Dependencies, result.Source, 0, virtualPath.empty() ? "<memory>" : virtualPath);
        return result;
    }

    CQShaderSource CQShaderLoader::ParseHeader(const std::string& rawSource)
    {
        CQShaderSource result;
        result.Language = ShaderLanguage::GLSL;

        std::istringstream stream(rawSource);
        std::string line;

        bool headerParsed = false;
        std::string cleanSource;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!headerParsed)
            {
                std::string trimmed = Trim(line);
                if (trimmed.empty())
                {
                    cleanSource += "\n";
                    continue;
                }
                headerParsed = true;
                if (trimmed.rfind("#language", 0) == 0)
                {
                    size_t spacePos = trimmed.find(' ');
                    if (spacePos != std::string::npos)
                        result.Language = ShaderLanguageFromString(Trim(trimmed.substr(spacePos + 1)));
                    cleanSource += "\n"; // satır numaraları hata mesajlarında doğru kalsın
                    continue;
                }
            }

            cleanSource += line + "\n";
        }

        result.Source = cleanSource;
        return result;
    }
}
