#include "MeshRendererMaterial.h"

#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"

#include <cctype>
#include <map>

namespace Conqueror::MeshRendererMaterial
{
    namespace
    {
        const Material& Fallback()
        {
            static std::shared_ptr<Material> fallback;
            if (auto grid = MaterialAsset::DefaultGrid())
                return *grid;
            if (!fallback)
                fallback = std::make_shared<Material>();
            return *fallback;
        }

        bool IsInstanceSuffix(const std::string& name)
        {
            static const std::string suffix = " (Instance)";
            return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }

    const Material& Shared(const MeshRendererComponent& renderer)
    {
        return renderer.MaterialInstance ? *renderer.MaterialInstance : Fallback();
    }

    Material& Edit(MeshRendererComponent& renderer)
    {
        // Dosyadaki (paylaşılan) malzeme, paket malzemesi ya da çoğaltılmış nesnelerle ortak örnek: önce kopya
        const bool shared = !renderer.MaterialPath.empty() || !renderer.MaterialInstance || renderer.MaterialInstance.use_count() > 1;
        if (shared)
        {
            auto copy = std::make_shared<Material>(Shared(renderer));
            if (!IsInstanceSuffix(copy->Name))
                copy->Name += " (Instance)";
            copy->ParentPath.clear();
            renderer.MaterialInstance = copy;
            renderer.MaterialPath.clear();
        }
        return *renderer.MaterialInstance;
    }

    void Apply(Material& material, const LegacyFields& f)
    {
        material.Albedo = glm::vec3(f.Color);
        material.Opacity = f.Color.a;
        material.Metallic = f.Metallic;
        material.Roughness = f.Roughness;
        material.AO = f.AO;
        material.EmissionColor = f.EmissionColor;
        material.EmissionStrength = f.EmissionStrength;
        material.EmissionMap = f.EmissionFromTexture ? f.Texture : nullptr;
        material.Tiling = f.Tiling;
        material.Offset = f.Offset;
        material.WorldAligned = f.WorldAlignedUV;
        material.AlignToObject = f.AlignToObject;
        material.AlbedoMap = f.Texture;
        material.NormalMap = f.NormalMap;
        material.AOMap = f.MaskMap;
        material.RoughnessMap = f.MaskMap;
        material.MetallicMap = f.MaskMap;
        material.RoughnessChannel = f.MaskMap ? 1 : 0;
        material.MetallicChannel = f.MaskMap ? 2 : 0;
        material.MapsScaleFactors = f.MaskMap != nullptr;
        material.Shading = f.Unlit ? Material::ShadingModel::Unlit : Material::ShadingModel::DefaultLit;
        material.Rendering = f.AlphaClip ? Material::RenderMode::Cutout
                           : (f.Color.a < 0.999f ? Material::RenderMode::Transparent : Material::RenderMode::Opaque);
        material.AlphaClip = f.AlphaClipThreshold;
    }

    glm::vec4 GetColor(const MeshRendererComponent& renderer)
    {
        const Material& m = Shared(renderer);
        return glm::vec4(m.Albedo, m.Opacity);
    }

    void SetColor(MeshRendererComponent& renderer, const glm::vec4& color)
    {
        if (GetColor(renderer) == color)
            return;   // aynı renk: gereksiz örnek çıkarma
        Material& m = Edit(renderer);
        m.Albedo = glm::vec3(color);
        m.Opacity = color.a;
        // Saydam renk (Unity: Surface Type Transparent gerekir; eski Mesh Renderer davranışı gibi kendiliğinden)
        if (color.a < 0.999f && m.Rendering == Material::RenderMode::Opaque)
            m.Rendering = Material::RenderMode::Transparent;
        else if (color.a >= 0.999f && m.Rendering == Material::RenderMode::Transparent)
            m.Rendering = Material::RenderMode::Opaque;
    }

    void SetTexture(MeshRendererComponent& renderer, const std::shared_ptr<Texture2D>& texture)
    {
        if (Shared(renderer).AlbedoMap == texture)
            return;
        Material& m = Edit(renderer);
        const bool emissionFollows = m.EmissionMap && m.EmissionMap == m.AlbedoMap;
        m.AlbedoMap = texture;
        if (emissionFollows)
            m.EmissionMap = texture;
    }

    int BakeInstances(Scene& scene, const std::filesystem::path& dir, int* created, const std::filesystem::path& relativeTo)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir, ec);
        auto keyOf = [](const Material& m) { Material c = m; c.Name.clear(); return MaterialAsset::ToYaml(c); };
        std::map<std::string, std::string> byContent;
        for (auto& f : fs::directory_iterator(dir, ec))
            if (f.path().extension() == MaterialAsset::Extension)
                if (auto m = MaterialAsset::Load(f.path().string()))
                    byContent.emplace(keyOf(*m), f.path().string());
        int baked = 0, made = 0;
        auto view = scene.m_Registry.view<MeshRendererComponent, TagComponent>();
        for (auto id : view)
        {
            auto& mr = view.get<MeshRendererComponent>(id);
            if (!mr.MaterialPath.empty() || !mr.MaterialInstance)
                continue;
            const std::string key = keyOf(*mr.MaterialInstance);
            auto it = byContent.find(key);
            if (it == byContent.end())
            {
                // Ad: dokulu yüzey -> dokunun adı ("plywood.jpg" -> "Plywood"); yoksa malzemenin adı (" (Instance)" atılır);
                // paket malzemesinin (Default Grid ...) kopyası ya da adsız ise ilk nesnenin adı
                const Material& src = *mr.MaterialInstance;
                const std::string tag = view.get<TagComponent>(id).Tag;
                std::string name = src.Name;
                if (auto p = name.rfind(" (Instance)"); p != std::string::npos)
                    name.erase(p);
                const bool legacyName = name == tag;   // eski sahneden gelen "<nesne> (Instance)"
                const fs::path packaged = fs::path(MaterialAsset::DefaultGridPath()).parent_path() / (name + MaterialAsset::Extension);
                std::string tex = src.AlbedoMap ? fs::path(src.AlbedoMap->GetPath()).stem().string() : std::string();
                if (!tex.empty() && (legacyName || name.empty() || fs::exists(packaged, ec)))
                {
                    for (char& ch : tex)
                        if (ch == '_' || ch == '-') ch = ' ';
                    tex[0] = (char)std::toupper((unsigned char)tex[0]);
                    name = tex;
                }
                else if (name.empty() || fs::exists(packaged, ec))
                    name = tag;
                for (char& ch : name)
                    if (std::string("/\\:*?\"<>|").find(ch) != std::string::npos)
                        ch = '_';
                if (name.empty())
                    name = "Material";
                fs::path path = dir / (name + MaterialAsset::Extension);
                for (int n = 1; fs::exists(path, ec); ++n)
                    path = dir / (name + " " + std::to_string(n) + MaterialAsset::Extension);
                Material copy = *mr.MaterialInstance;
                copy.Name = path.stem().string();
                if (!MaterialAsset::Create(path.string(), &copy))
                    continue;
                it = byContent.emplace(key, path.string()).first;
                ++made;
            }
            if (relativeTo.empty())
            {
                mr.MaterialInstance = MaterialAsset::Load(it->second);
                mr.MaterialPath = it->second;
            }
            else
            {
                // Önbellekte olmayan kopya: serileştirici göreli yolu olduğu gibi yazar
                auto loaded = MaterialAsset::Load(it->second);
                mr.MaterialInstance = loaded ? std::make_shared<Material>(*loaded) : mr.MaterialInstance;
                mr.MaterialPath = fs::relative(it->second, relativeTo, ec).generic_string();
            }
            ++baked;
        }
        if (created)
            *created = made;
        return baked;
    }
}
