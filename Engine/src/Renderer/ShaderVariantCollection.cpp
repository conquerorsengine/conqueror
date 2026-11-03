#include "ShaderVariantCollection.h"

#include "Core/Logging/Log.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/ComputeShaders.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace Conqueror::ShaderVariantCollections
{
    namespace fs = std::filesystem;

    namespace
    {
        bool s_PreloadedDone = false;
        // Derlenmiş kod shader'ları canlı tutulur (önbellek: ilk kullanımda yeniden derlenmez)
        std::unordered_map<std::string, std::shared_ptr<Shader>>& Warm()
        {
            static auto* m = new std::unordered_map<std::string, std::shared_ptr<Shader>>();
            return *m;
        }
    }

    bool Load(const std::string& path, std::vector<std::string>& entries)
    {
        try
        {
            YAML::Node n = YAML::LoadFile(SceneSerializer::ResolveAssetPath(path))["ShaderVariantCollection"];
            if (!n)
                return false;
            entries.clear();
            if (auto e = n["Shaders"]; e && e.IsSequence())
                for (const auto& x : e)
                    entries.push_back(x.as<std::string>());
            return true;
        }
        catch (...) { return false; }
    }

    bool Save(const std::string& path, const std::vector<std::string>& entries)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "ShaderVariantCollection" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Shaders" << YAML::Value << YAML::BeginSeq;
        for (const auto& e : entries) out << e;
        out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
        std::ofstream f(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
        if (!f)
            return false;
        f << out.c_str() << "\n";
        return true;
    }

    int WarmUp(const std::string& path, std::vector<std::string>* errors)
    {
        std::vector<std::string> entries;
        if (!Load(path, entries))
            return 0;
        const auto start = std::chrono::steady_clock::now();
        int count = 0;
        for (const auto& e : entries)
        {
            const std::string file = SceneSerializer::ResolveAssetPath(e);
            std::string ext = fs::path(file).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool ok = false;
            if (ext == ".cqmat")
            {
                // Shader Graph / kod shader'lı malzeme yüklenirken derlenir ve önbellekte kalır
                auto mat = MaterialAsset::Load(file);
                ok = mat && (!mat->MaterialShader || mat->MaterialShader->IsValid());
            }
            else if (ext == ".cqsh")
            {
                auto shader = Shader::Create(file);
                ok = shader && shader->IsValid();
                if (ok)
                    Warm()[file] = shader;
            }
            else if (ext == ".comp")
                ok = ComputeShaders::Inspect(file).Compiled;
            if (ok)
                ++count;
            else if (errors)
                errors->push_back(e);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        CQ_CORE_INFO("[ShaderVariantCollection] {0}: warmed up {1}/{2} shaders in {3:.1f} ms", fs::path(path).filename().string(), count,
                     entries.size(), ms);
        return count;
    }

    int WarmUpPreloaded()
    {
        if (s_PreloadedDone)
            return 0;
        s_PreloadedDone = true;
        int total = 0;
        for (const auto& c : ProjectSettings::Graphics().PreloadedShaders)
            total += WarmUp(c);
        return total;
    }

    void ResetPreloaded()
    {
        s_PreloadedDone = false;
    }
}
