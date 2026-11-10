#include "MeshRendererMaterial.h"

#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

        // Rengin okunur adı (malzeme dosya adları için)
        std::string ColorName(const glm::vec3& c)
        {
            const float mx = std::max({ c.r, c.g, c.b }), mn = std::min({ c.r, c.g, c.b });
            const float l = (mx + mn) * 0.5f, d = mx - mn;
            if (d < 0.08f)
                return l > 0.85f ? "White" : (l > 0.55f ? "Light Gray" : (l > 0.25f ? "Gray" : (l > 0.08f ? "Dark Gray" : "Black")));
            float h;
            if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.0f);
            else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
            else h = (c.r - c.g) / d + 4.0f;
            h *= 60.0f;
            if (h < 0.0f) h += 360.0f;
            static const struct { float H; const char* N; } hues[] = { { 15, "Red" }, { 40, "Orange" }, { 65, "Yellow" }, { 90, "Lime" },
                { 150, "Green" }, { 195, "Cyan" }, { 250, "Blue" }, { 285, "Purple" }, { 330, "Magenta" }, { 361, "Red" } };
            std::string n = "Red";
            for (const auto& e : hues)
                if (h < e.H) { n = e.N; break; }
            if (n == "Orange" && l < 0.35f) n = "Brown";
            if (l < 0.25f) return "Dark " + n;
            if (l > 0.8f) return "Light " + n;
            return n;
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
        // Varsayılan dama malzemesinin dünya hizalı UV'si ve döşemesi dama dokusuna aittir: başka doku verilince (betikteki mermi
        // izi, Inspector'da Base Map) doku nesnenin kendi UV'siyle bir kez kaplanır. Değiştirilmiş döşeme / hizalama korunur.
        if (auto grid = MaterialAsset::DefaultGrid(); grid && m.AlbedoMap && m.AlbedoMap == grid->AlbedoMap && texture != grid->AlbedoMap &&
                                                     m.WorldAligned == grid->WorldAligned && m.AlignToObject == grid->AlignToObject &&
                                                     m.Tiling == grid->Tiling && m.Offset == grid->Offset)
        {
            m.WorldAligned = false;
            m.AlignToObject = false;
            m.Tiling = glm::vec2(1.0f);
            m.Offset = glm::vec2(0.0f);
        }
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
                // Paketteki ızgara dokusu yalnız "varsayılan görünüm": böyle malzemeler renklerine göre adlandırılır
                if (src.AlbedoMap && (MaterialAsset::IsReadOnly(src.AlbedoMap->GetPath()) ||
                                      SceneSerializer::ToAssetPath(src.AlbedoMap->GetPath()).rfind("CQ:", 0) == 0))
                    tex.clear();
                if (tex.empty() && (legacyName || name.empty() || fs::exists(packaged, ec)))
                {
                    name = ColorName(src.Albedo) + (src.EmissionStrength > 0.0f && glm::length(src.EmissionColor) > 0.0f ? " Glow" : "");
                    if (src.EmissionStrength > 0.0f && glm::length(src.EmissionColor) > 0.0f)
                        name = ColorName(src.EmissionColor) + " Glow";
                }
                else if (!tex.empty() && (legacyName || name.empty() || fs::exists(packaged, ec)))
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
