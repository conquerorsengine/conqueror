#include "MaterialAsset.h"
#include "Core/Utils/ImportActivity.h"

#include <chrono>
#include "Material.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Project/Project.h"

#include "Core/Logging/Log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        struct Entry
        {
            std::shared_ptr<Material> Mat;
            std::filesystem::file_time_type Time{};
        };

        std::unordered_map<std::string, Entry>& Cache()
        {
            static auto* cache = new std::unordered_map<std::string, Entry>();   // GL kaynakları: bilinçli sızdırılır
            return *cache;
        }

        std::string Key(const std::string& path)
        {
            std::error_code ec;
            auto p = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
            return (ec ? std::filesystem::path(path).lexically_normal() : p).generic_string();
        }

        std::filesystem::file_time_type MTime(const std::string& path)
        {
            std::error_code ec;
            auto t = std::filesystem::last_write_time(path, ec);
            return ec ? std::filesystem::file_time_type{} : t;
        }

        bool ReadText(const std::string& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }
    }

    namespace MaterialAsset
    {
        // Dosyayı mevcut nesnenin üstüne yeniden oku (tüm kullanıcılar yeni hali görür)
        static bool ReloadInPlace(const std::string& key, Entry& entry)
        {
            std::string text;
            if (!ReadText(key, text))
                return false;
            Material fresh;
            std::string error;
            if (!FromYaml(text, fresh, &error))
            {
                CQ_CORE_WARN("Material '{0}' reload failed: {1}", key, error);
                return false;
            }
            fresh.Name = std::filesystem::path(key).stem().string();
            if ((fresh.GraphNodes.empty() || !fresh.GraphInitialized) && !fresh.LoadShaderFile())
                fresh.MaterialShader = nullptr;   // graph / kod shader'ı yok: varsayılan PBR
            *entry.Mat = fresh;
            return true;
        }

        // Üstü değişen varyantları yenile (zincir: varyantın varyantı)
        static void RefreshVariantsOf(std::vector<std::string> changedKeys)
        {
            for (size_t i = 0; i < changedKeys.size() && i < 64; ++i)
                for (auto& [key, entry] : Cache())
                    if (!entry.Mat->ParentPath.empty() && Key(entry.Mat->ParentPath) == changedKeys[i] && ReloadInPlace(key, entry))
                        changedKeys.push_back(key);
        }

        bool IsMaterialFile(const std::string& path)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext == Extension;
        }

        std::shared_ptr<Material> Load(const std::string& path)
        {
            if (path.empty())
                return nullptr;
            const std::string key = Key(path);
            auto& cache = Cache();
            if (auto it = cache.find(key); it != cache.end())
                return it->second.Mat;

            const auto t0 = std::chrono::steady_clock::now();
            auto elapsed = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
            std::string text;
            if (!ReadText(path, text))
                return nullptr;
            auto mat = Material::CreateDefault();
            mat->Name = std::filesystem::path(path).stem().string();
            std::string error;
            if (!FromYaml(text, *mat, &error))
            {
                CQ_CORE_ERROR("Material '{0}' could not be read: {1}", path, error);
                ImportActivity::Add(path, "Material", elapsed(), false, error);
                return nullptr;
            }
            mat->Name = std::filesystem::path(path).stem().string();
            cache[key] = { mat, MTime(path) };
            ImportActivity::Add(path, "Material", elapsed(), true);
            return mat;
        }

        bool Save(const std::shared_ptr<Material>& material, const std::string& path)
        {
            if (!material || path.empty())
                return false;
            std::error_code ec;
            auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, ec);
            material->Name = std::filesystem::path(path).stem().string();
            {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    CQ_CORE_ERROR("Material '{0}' could not be written", path);
                    return false;
                }
                out << ToYaml(*material) << "\n";
            }
            Cache()[Key(path)] = { material, MTime(path) };
            RefreshVariantsOf({ Key(path) });
            return true;
        }

        std::shared_ptr<Material> Create(const std::string& path, const Material* from)
        {
            auto mat = Material::CreateDefault();
            if (from)
            {
                *mat = *from;
                mat->MaterialShader = from->MaterialShader;
            }
            if (!Save(mat, path))
                return nullptr;
            if (mat->GraphInitialized && !mat->GraphNodes.empty() && !from)
                mat->CompileShaderGraph();
            return mat;
        }

        std::string PathOf(const Material* material)
        {
            if (!material)
                return {};
            for (const auto& [key, entry] : Cache())
                if (entry.Mat.get() == material)
                    return key;
            return {};
        }

        void OnFileMoved(const std::string& from, const std::string& to)
        {
            auto& cache = Cache();
            const std::string a = Key(from), b = Key(to);
            std::vector<std::pair<std::string, Entry>> moved;
            for (auto it = cache.begin(); it != cache.end();)
            {
                // Klasör taşındıysa içindeki malzemeler de
                if (it->first == a || it->first.rfind(a + "/", 0) == 0)
                {
                    moved.push_back({ b + it->first.substr(a.size()), it->second });
                    it = cache.erase(it);
                }
                else
                    ++it;
            }
            for (auto& [k, e] : moved)
                cache[k] = e;
            for (auto& [k, e] : cache)
                if (!e.Mat->ParentPath.empty())
                {
                    const std::string parent = Key(e.Mat->ParentPath);
                    if (parent == a || parent.rfind(a + "/", 0) == 0)
                    {
                        e.Mat->ParentPath = b + parent.substr(a.size());
                        Save(e.Mat, k);
                    }
                }
            for (auto& [k, e] : cache)
                e.Mat->Name = std::filesystem::path(k).stem().string();
        }

        const std::string& DefaultGridPath()
        {
            static const std::string path =
                std::filesystem::path(SceneSerializer::ResolveAssetPath("CQ:Resources/Packages/Core/Materials/Default Grid.cqmat")).lexically_normal().string();
            return path;
        }

        std::shared_ptr<Material> DefaultGrid()
        {
            return Load(DefaultGridPath());
        }

        bool IsReadOnly(const std::string& path)
        {
            if (path.empty())
                return false;
            static const std::string root =
                Key(std::filesystem::path(SceneSerializer::ResolveAssetPath("CQ:Resources/Packages")).string());
            const std::string k = Key(path);
            if (k.rfind(root + "/", 0) == 0)
                return true;
            // Projenin Packages klasörü de salt okunur (Content Browser ile aynı kural)
            const std::filesystem::path project = Project::GetActiveProjectDirectory();
            if (project.empty())
                return false;
            const std::string pk = Key((project / "Packages").string());
            return k.rfind(pk + "/", 0) == 0;
        }

        int CheckFileChanges()
        {
            std::vector<std::string> changed;
            for (auto& [key, entry] : Cache())
            {
                std::error_code ec;
                if (!std::filesystem::exists(key, ec))
                    continue;   // silindi: son yüklenen hali kalır
                auto t = MTime(key);
                if (t == entry.Time)
                    continue;
                entry.Time = t;
                if (ReloadInPlace(key, entry))
                {
                    changed.push_back(key);
                    CQ_CORE_INFO("Material reloaded: {0}", key);
                }
            }
            // Kod shader'ı (.cqsh) dosyası değiştiyse yeniden derle (Unity: shader kaydedilince malzemeler güncellenir)
            for (auto& [key, entry] : Cache())
                if (entry.Mat && !entry.Mat->ShaderPath.empty() && entry.Mat->MaterialShader && entry.Mat->MaterialShader->IsSourceModified())
                    entry.Mat->LoadShaderFile();
            const int count = (int)changed.size();
            if (count)
                RefreshVariantsOf(std::move(changed));
            return count;
        }
    }
}
