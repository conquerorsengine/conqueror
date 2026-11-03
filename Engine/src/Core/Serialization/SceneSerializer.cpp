#include "SceneSerializer.h"
#include "Scene/LevelStreaming.h"
#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scripting/NativeScriptCompiler.h"
#include <unordered_set>
#include "Core/Math/Math.h"
#include "Scene/ComponentRegistry.h"
#include <algorithm>
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/EditorCamera.h"
#include "Terrain/Terrain.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Animation/AnimationController.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scripting/ScriptEngine.h"
#include "AISystem/Navigation/NavMesh.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <libloaderapi.h>
#else
#include <unistd.h>
#include "Core/Utils/SelfExecutable.h"
#endif

namespace YAML
{
    template<>
    struct convert<glm::vec2>
    {
        static Node encode(const glm::vec2& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs)
        {
            if (!node.IsSequence() || node.size() != 2)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs)
        {
            if (!node.IsSequence() || node.size() != 3)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec4>
    {
        static Node encode(const glm::vec4& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs)
        {
            if (!node.IsSequence() || node.size() != 4)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            rhs.w = node[3].as<float>();
            return true;
        }
    };
}

namespace Conqueror
{
    namespace
    {
        // Varlıklar oluşturulma (ekleme) sırasıyla yazılır; yükleme aynı sırayla oluşturduğundan Hiyerarşi sırası
        // kaydet/yükle döngüsünde korunur (EnTT görünümleri yeniden eskiye doğru gezer)
        std::vector<entt::entity> SerializationOrder(entt::registry& registry)
        {
            std::vector<entt::entity> order;
            const auto& tags = registry.storage<TagComponent>();
            order.reserve(tags.size());
            for (size_t i = 0; i < tags.size(); ++i)
                if (registry.valid(tags.data()[i]))
                    order.push_back(tags.data()[i]);
            // Etiketsiz varlıklar (olmamalı) sona
            if (order.size() < registry.storage<entt::entity>().size())
            {
                std::unordered_set<entt::entity> seen(order.begin(), order.end());
                std::vector<entt::entity> rest;
                registry.view<entt::entity>().each([&](auto e) { if (!seen.count(e)) rest.push_back(e); });
                order.insert(order.end(), rest.rbegin(), rest.rend());
            }
            return order;
        }
    }

    static std::filesystem::path GetEngineDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
#else
        const std::string exe = SelfExecutablePath();
        if (!exe.empty())
            return std::filesystem::path(exe).parent_path();
        return std::filesystem::current_path();
#endif
    }

    static std::string ResolveScriptPath(const std::string& scriptPath)
    {
        if (scriptPath.empty())
            return scriptPath;

        std::filesystem::path p(scriptPath);
        if (p.is_relative())
        {
            auto projectDir = Project::GetActiveProjectDirectory();
            if (!projectDir.empty())
            {
                auto absolute = projectDir / p;
                if (std::filesystem::exists(absolute))
                {
                    CQ_CORE_INFO("SceneSerializer: Resolved relative script path to: {0}", absolute.string());
                    return absolute.string();
                }
            }
        }

        return scriptPath;
    }

    static std::string ToSerializablePath(const std::string& absolutePath)
    {
        if (absolutePath.empty())
            return absolutePath;

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::error_code ec;
            auto relative = std::filesystem::relative(absolutePath, projectDir, ec);
            if (!ec && !relative.empty() && relative.native()[0] != '.')
                return relative.generic_string();
        }

        auto engineDir = GetEngineDirectory();
        std::error_code ec;
        auto relative = std::filesystem::relative(absolutePath, engineDir, ec);
        if (!ec && !relative.empty() && relative.native()[0] != '.')
        {
            std::string relStr = relative.generic_string();
            if (relStr.find("Resources/") == 0 || relStr.find("Resources\\") == 0)
                return "CQ:" + relStr;
        }

        return absolutePath;
    }

    static std::string ResolveSerializablePath(const std::string& rawPath)
    {
        if (rawPath.empty())
            return rawPath;
        // Eski Windows kayıtları '\' içerebilir: her platformda '/' ile çözülür
        std::string path = rawPath;
        std::replace(path.begin(), path.end(), '\\', '/');

        if (path.size() > 3 && path[0] == 'C' && path[1] == 'Q' && path[2] == ':')
        {
            std::string relative = path.substr(3);
            auto engineDir = GetEngineDirectory();
            auto absolute = engineDir / std::filesystem::path(relative);
            if (std::filesystem::exists(absolute))
                return absolute.string();
        }

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::filesystem::path p(path);
            std::error_code ec;
            if (p.is_relative())
            {
                auto absolute = projectDir / p;
                if (std::filesystem::exists(absolute, ec))
                    return absolute.string();
            }
            else if (!std::filesystem::exists(p, ec))
            {
                // Başka makinede / klasörde kaydedilmiş mutlak yol (eski sahneler, taşınmış proje, derlenmiş oyun):
                // ".../Assets/..." kısmı bu projeye göre çözülür
                const size_t assets = path.rfind("/Assets/");
                if (assets != std::string::npos)
                {
                    auto remapped = projectDir / std::filesystem::path(path.substr(assets + 1));
                    if (std::filesystem::exists(remapped, ec))
                        return remapped.string();
                }
            }
        }

        return path;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
        return out;
    }

    // ---- Parçacık sistemi (tüm modül ayarları)
    // ------------------------------------------------------------------ Malzeme (gömülü ve .cqmat aynı biçim)
    static bool s_EmitEmptyMaps = false;   // varyant farkı için: boş harita da yazılır ("" = kaldırıldı)
    static void EmitTexturePath(YAML::Emitter& out, const char* key, const std::shared_ptr<Texture2D>& tex)
    {
        if (tex && !tex->GetPath().empty())
            out << YAML::Key << key << YAML::Value << ToSerializablePath(tex->GetPath());
        else if (s_EmitEmptyMaps)
            out << YAML::Key << key << YAML::Value << "";
    }

    static void EmitMaterial(YAML::Emitter& out, const Material& mat)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << mat.Name;
        out << YAML::Key << "Albedo" << YAML::Value << mat.Albedo;
        out << YAML::Key << "Opacity" << YAML::Value << mat.Opacity;
        out << YAML::Key << "Metallic" << YAML::Value << mat.Metallic;
        out << YAML::Key << "Roughness" << YAML::Value << mat.Roughness;
        out << YAML::Key << "AO" << YAML::Value << mat.AO;
        out << YAML::Key << "EmissionColor" << YAML::Value << mat.EmissionColor;
        out << YAML::Key << "EmissionStrength" << YAML::Value << mat.EmissionStrength;
        out << YAML::Key << "Tiling" << YAML::Value << mat.Tiling;
        out << YAML::Key << "Offset" << YAML::Value << mat.Offset;
        if (mat.WorldAligned) out << YAML::Key << "WorldAligned" << YAML::Value << true;
        if (mat.AlignToObject) out << YAML::Key << "AlignToObject" << YAML::Value << true;
        out << YAML::Key << "RenderMode" << YAML::Value << (int)mat.Rendering;
        out << YAML::Key << "Culling" << YAML::Value << (int)mat.Culling;
        out << YAML::Key << "AlphaClip" << YAML::Value << mat.AlphaClip;
        if (!mat.ShaderPath.empty()) out << YAML::Key << "Shader" << YAML::Value << ToSerializablePath(mat.ShaderPath);
        if (mat.Shading != Material::ShadingModel::DefaultLit)
        {
            out << YAML::Key << "ShadingModel" << YAML::Value << (int)mat.Shading;
            out << YAML::Key << "SubsurfaceColor" << YAML::Value << mat.SubsurfaceColor;
            out << YAML::Key << "SubsurfaceAmount" << YAML::Value << mat.SubsurfaceAmount;
        }
        if (mat.MapsScaleFactors)
        {
            out << YAML::Key << "MapsScaleFactors" << YAML::Value << true;
            out << YAML::Key << "MetallicChannel" << YAML::Value << mat.MetallicChannel;
            out << YAML::Key << "RoughnessChannel" << YAML::Value << mat.RoughnessChannel;
        }
        EmitTexturePath(out, "AlbedoMap", mat.AlbedoMap);
        EmitTexturePath(out, "NormalMap", mat.NormalMap);
        EmitTexturePath(out, "MetallicMap", mat.MetallicMap);
        EmitTexturePath(out, "RoughnessMap", mat.RoughnessMap);
        EmitTexturePath(out, "AOMap", mat.AOMap);
        EmitTexturePath(out, "EmissionMap", mat.EmissionMap);

        // Shader Graph
        out << YAML::Key << "NextGraphNodeID" << YAML::Value << mat.NextGraphNodeID;
        out << YAML::Key << "NextGraphLinkID" << YAML::Value << mat.NextGraphLinkID;
        out << YAML::Key << "GraphInitialized" << YAML::Value << mat.GraphInitialized;
        out << YAML::Key << "GraphNodes" << YAML::Value << YAML::BeginSeq;
        for (const auto& node : mat.GraphNodes)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "ID" << YAML::Value << node.ID;
            out << YAML::Key << "Type" << YAML::Value << (int)node.Type;
            out << YAML::Key << "EditorPosX" << YAML::Value << node.EditorPosition.x;
            out << YAML::Key << "EditorPosY" << YAML::Value << node.EditorPosition.y;
            out << YAML::Key << "ColorValue" << YAML::Value << node.ColorValue;
            out << YAML::Key << "FloatValue" << YAML::Value << node.FloatValue;
            out << YAML::Key << "Vec2Value" << YAML::Value << node.Vec2Value;
            out << YAML::Key << "Vec3Value" << YAML::Value << node.Vec3Value;
            if (node.IntValue) out << YAML::Key << "IntValue" << YAML::Value << node.IntValue;
            out << YAML::Key << "TexturePath" << YAML::Value << ToSerializablePath(node.TexturePath);
            if (!node.CustomCode.empty()) out << YAML::Key << "CustomCode" << YAML::Value << YAML::Literal << node.CustomCode;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "GraphLinks" << YAML::Value << YAML::BeginSeq;
        for (const auto& link : mat.GraphLinks)
        {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "ID" << YAML::Value << link.ID;
            out << YAML::Key << "From" << YAML::Value << link.FromNodeID;
            out << YAML::Key << "FromPin" << YAML::Value << link.FromPinIndex;
            out << YAML::Key << "To" << YAML::Value << link.ToNodeID;
            out << YAML::Key << "ToPin" << YAML::Value << link.ToPinIndex;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    static std::shared_ptr<Texture2D> ReadTexturePath(const YAML::Node& n, const char* key)
    {
        if (auto t = n[key])
        {
            const std::string path = ResolveSerializablePath(t.as<std::string>());
            if (!path.empty())
                return Texture2D::Create(path);
        }
        return nullptr;
    }

    static void ReadMaterial(const YAML::Node& n, Material& mat, bool partial = false)
    {
        if (n["Name"]) mat.Name = n["Name"].as<std::string>();
        if (n["Albedo"]) mat.Albedo = n["Albedo"].as<glm::vec3>();
        if (n["Opacity"]) mat.Opacity = n["Opacity"].as<float>();
        if (n["Metallic"]) mat.Metallic = n["Metallic"].as<float>();
        if (n["Roughness"]) mat.Roughness = n["Roughness"].as<float>();
        if (n["AO"]) mat.AO = n["AO"].as<float>();
        if (n["EmissionColor"]) mat.EmissionColor = n["EmissionColor"].as<glm::vec3>();
        if (n["EmissionStrength"]) mat.EmissionStrength = n["EmissionStrength"].as<float>();
        if (n["Tiling"]) mat.Tiling = n["Tiling"].as<glm::vec2>();
        if (n["Offset"]) mat.Offset = n["Offset"].as<glm::vec2>();
        if (n["WorldAligned"]) mat.WorldAligned = n["WorldAligned"].as<bool>();
        if (n["AlignToObject"]) mat.AlignToObject = n["AlignToObject"].as<bool>();
        if (n["RenderMode"]) mat.Rendering = (Material::RenderMode)std::clamp(n["RenderMode"].as<int>(), 0, 2);
        if (n["Culling"]) mat.Culling = (Material::CullMode)std::clamp(n["Culling"].as<int>(), 0, 2);
        if (n["AlphaClip"]) mat.AlphaClip = n["AlphaClip"].as<float>();
        if (n["ShadingModel"]) mat.Shading = (Material::ShadingModel)std::clamp(n["ShadingModel"].as<int>(), 0, 4);
        if (n["SubsurfaceColor"]) mat.SubsurfaceColor = n["SubsurfaceColor"].as<glm::vec3>();
        if (n["SubsurfaceAmount"]) mat.SubsurfaceAmount = n["SubsurfaceAmount"].as<float>();
        if (n["MapsScaleFactors"]) mat.MapsScaleFactors = n["MapsScaleFactors"].as<bool>();
        if (n["MetallicChannel"]) mat.MetallicChannel = n["MetallicChannel"].as<int>();
        if (n["RoughnessChannel"]) mat.RoughnessChannel = n["RoughnessChannel"].as<int>();
        // Varyant (partial): dosyada olmayan alan üstten gelir. Boş yol ("") = üstteki dokuyu kaldır.
        auto readMap = [&](const char* key, std::shared_ptr<Texture2D>& slot) {
            if (!partial || n[key])
                slot = ReadTexturePath(n, key);
        };
        readMap("AlbedoMap", mat.AlbedoMap);
        readMap("NormalMap", mat.NormalMap);
        readMap("MetallicMap", mat.MetallicMap);
        readMap("RoughnessMap", mat.RoughnessMap);
        readMap("AOMap", mat.AOMap);
        readMap("EmissionMap", mat.EmissionMap);
        if (n["Parent"]) mat.ParentPath = ResolveSerializablePath(n["Parent"].as<std::string>());
        if (n["Shader"]) mat.ShaderPath = ResolveSerializablePath(n["Shader"].as<std::string>());
        else if (!partial) mat.ShaderPath.clear();
        const bool hasGraph = !partial || n["GraphNodes"] || n["GraphLinks"];

        if (n["NextGraphNodeID"]) mat.NextGraphNodeID = n["NextGraphNodeID"].as<int>();
        if (n["NextGraphLinkID"]) mat.NextGraphLinkID = n["NextGraphLinkID"].as<int>();
        if (n["GraphInitialized"]) mat.GraphInitialized = n["GraphInitialized"].as<bool>();
        if (hasGraph)
        {
            mat.GraphNodes.clear();
            mat.GraphLinks.clear();
        }
        if (auto nodes = n["GraphNodes"])
            for (auto node : nodes)
            {
                ShaderGraphNode g;
                g.ID = node["ID"].as<int>();
                g.Type = (ShaderGraphNodeType)node["Type"].as<int>();
                g.EditorPosition = { node["EditorPosX"].as<float>(0.0f), node["EditorPosY"].as<float>(0.0f) };
                if (node["ColorValue"]) g.ColorValue = node["ColorValue"].as<glm::vec4>();
                if (node["FloatValue"]) g.FloatValue = node["FloatValue"].as<float>();
                if (node["Vec2Value"]) g.Vec2Value = node["Vec2Value"].as<glm::vec2>();
                if (node["Vec3Value"]) g.Vec3Value = node["Vec3Value"].as<glm::vec3>();
                if (node["IntValue"]) g.IntValue = node["IntValue"].as<int>();
                if (node["TexturePath"]) g.TexturePath = ResolveSerializablePath(node["TexturePath"].as<std::string>());
                if (node["CustomCode"]) g.CustomCode = node["CustomCode"].as<std::string>();
                mat.GraphNodes.push_back(g);
            }
        if (auto links = n["GraphLinks"])
            for (auto link : links)
                mat.GraphLinks.emplace_back(link["ID"].as<int>(), link["From"].as<int>(), link["FromPin"].as<int>(), link["To"].as<int>(),
                                            link["ToPin"].as<int>());
        // Shader Graph'lı malzeme yüklenince derlenir (eskiden yalnız graph düzenleyicide derleniyordu: kaydedilip açılan
        // sahnede graph sonucu görünmüyordu)
        if (hasGraph && mat.GraphInitialized && !mat.GraphNodes.empty())
        {
            mat.CompileShaderGraph();
            mat.ApplyGraphToProperties();
        }
        else if (!mat.ShaderPath.empty())
            mat.LoadShaderFile();   // kod shader'ı (.cqsh)
    }

    static void EmitBakeSettings(YAML::Emitter& out, const LightBakeSettings& bk);
    static void ReadBakeSettings(const YAML::Node& bake, LightBakeSettings& b);

    bool SceneSerializer::SaveLightingSettings(const std::string& path, const LightBakeSettings& settings)
    {
        LightBakeSettings copy = settings;
        copy.SettingsAsset.clear();
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "LightingSettings" << YAML::Value << YAML::BeginMap;
        EmitBakeSettings(out, copy);
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream f(ResolveAssetPath(path), std::ios::trunc);
        if (!f)
            return false;
        f << out.c_str() << "\n";
        return true;
    }

    std::string SceneSerializer::LightingSettingsToString(const LightBakeSettings& settings)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        EmitBakeSettings(out, settings);
        out << YAML::EndMap;
        return out.c_str();
    }

    bool SceneSerializer::LoadLightingSettings(const std::string& path, LightBakeSettings& settings)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(ResolveAssetPath(path));
            YAML::Node n = root["LightingSettings"];
            if (!n)
                return false;
            LightBakeSettings loaded;
            n.remove("SettingsAsset");
            ReadBakeSettings(n, loaded);
            loaded.SettingsAsset.clear();
            settings = loaded;
            return true;
        }
        catch (...) { return false; }
    }

    std::string MaterialAsset::ToYaml(const Material& material)
    {
        // Varyant: yalnız üstten farklı alanlar (+ Parent)
        if (!material.ParentPath.empty())
            if (auto parent = MaterialAsset::Load(material.ParentPath); parent && parent.get() != &material)
            {
                auto emitNode = [](const Material& m) {
                    s_EmitEmptyMaps = true;
                    YAML::Emitter e;
                    EmitMaterial(e, m);
                    s_EmitEmptyMaps = false;
                    return YAML::Load(e.c_str());
                };
                YAML::Node mine = emitNode(material), base = emitNode(*parent);
                YAML::Emitter out;
                out << YAML::BeginMap << YAML::Key << "Material" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "Parent" << YAML::Value << ToSerializablePath(material.ParentPath);
                bool graphDiffers = false;
                for (const char* g : { "GraphNodes", "GraphLinks", "NextGraphNodeID", "NextGraphLinkID", "GraphInitialized" })
                    if (YAML::Dump(mine[g]) != YAML::Dump(base[g]))
                        graphDiffers = true;
                for (auto kv : mine)
                {
                    const std::string key = kv.first.as<std::string>();
                    const bool graphKey = key.rfind("Graph", 0) == 0 || key.rfind("NextGraph", 0) == 0;
                    if (key == "Name" || (graphKey ? !graphDiffers : YAML::Dump(kv.second) == YAML::Dump(base[key])))
                        continue;
                    out << YAML::Key << key << YAML::Value << kv.second;
                }
                out << YAML::EndMap << YAML::EndMap;
                return out.c_str();
            }
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "Material" << YAML::Value;
        EmitMaterial(out, material);
        out << YAML::EndMap;
        return out.c_str();
    }

    bool MaterialAsset::FromYaml(const std::string& text, Material& material, std::string* error)
    {
        try
        {
            YAML::Node root = YAML::Load(text);
            YAML::Node n = root["Material"];
            if (!n || !n.IsMap())
            {
                if (error) *error = "Not a material file";
                return false;
            }
            static thread_local int s_Depth = 0;   // döngüsel üst zinciri (A -> B -> A) koruması
            if (auto parentNode = n["Parent"]; parentNode && s_Depth < 8)
            {
                struct DepthGuard { DepthGuard() { ++s_Depth; } ~DepthGuard() { --s_Depth; } } guard;
                const std::string parentPath = ResolveSerializablePath(parentNode.as<std::string>());
                auto parent = MaterialAsset::Load(parentPath);
                if (parent && parent.get() != &material)
                {
                    material = *parent;   // üstten başla, dosyadaki farkları uygula
                    ReadMaterial(n, material, true);
                    material.ParentPath = parentPath;
                    return true;
                }
                CQ_CORE_WARN("Material variant: parent '{0}' not found", parentPath);
            }
            ReadMaterial(n, material);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    static void SerializeParticleSystem(YAML::Emitter& out, const ParticleSystemComponent& ps)
    {
        out << YAML::Key << "ParticleSystemComponent" << YAML::BeginMap;
#define CQ_PS_OUT(field) out << YAML::Key << #field << YAML::Value << ps.field
        CQ_PS_OUT(Duration); CQ_PS_OUT(Looping); CQ_PS_OUT(Prewarm); CQ_PS_OUT(PlayOnAwake); CQ_PS_OUT(StopAction); CQ_PS_OUT(StartDelay);
        CQ_PS_OUT(StartLifetime); CQ_PS_OUT(StartSpeed); CQ_PS_OUT(StartSize); CQ_PS_OUT(StartRotation);
        CQ_PS_OUT(StartColorA); CQ_PS_OUT(StartColorB); CQ_PS_OUT(GravityModifier); CQ_PS_OUT(SimulationSpeed); CQ_PS_OUT(MaxParticles);
        out << YAML::Key << "SimulationSpace" << YAML::Value << (int)ps.SimulationSpace;
        CQ_PS_OUT(EmissionEnabled); CQ_PS_OUT(RateOverTime); CQ_PS_OUT(RateOverDistance);
        CQ_PS_OUT(ShapeEnabled); CQ_PS_OUT(Radius); CQ_PS_OUT(Angle); CQ_PS_OUT(BoxSize); CQ_PS_OUT(EmitFromShell); CQ_PS_OUT(RandomizeDirection);
        out << YAML::Key << "Shape" << YAML::Value << (int)ps.Shape;
        CQ_PS_OUT(VelocityEnabled); CQ_PS_OUT(VelocityOverLifetime); CQ_PS_OUT(OrbitalSpeed);
        CQ_PS_OUT(LimitVelocityEnabled); CQ_PS_OUT(Drag); CQ_PS_OUT(SpeedLimit);
        CQ_PS_OUT(NoiseEnabled); CQ_PS_OUT(NoiseStrength); CQ_PS_OUT(NoiseFrequency); CQ_PS_OUT(NoiseScrollSpeed);
        CQ_PS_OUT(ColorOverLifetimeEnabled); CQ_PS_OUT(SizeOverLifetimeEnabled); CQ_PS_OUT(RotationOverLifetimeEnabled); CQ_PS_OUT(AngularVelocity);
        CQ_PS_OUT(CollisionEnabled); CQ_PS_OUT(CollisionPlaneY); CQ_PS_OUT(Bounce); CQ_PS_OUT(LifetimeLoss);
        CQ_PS_OUT(TextureSheetEnabled); CQ_PS_OUT(AnimationCycles); CQ_PS_OUT(RandomStartFrame);
        out << YAML::Key << "Tiles" << YAML::Value << glm::vec2(ps.Tiles);
        out << YAML::Key << "RenderMode" << YAML::Value << (int)ps.RenderMode;
        out << YAML::Key << "Blend" << YAML::Value << (int)ps.Blend;
        out << YAML::Key << "Texture" << YAML::Value << ToSerializablePath(ps.TexturePath);
        CQ_PS_OUT(LengthScale); CQ_PS_OUT(SpeedScale); CQ_PS_OUT(SortByDistance); CQ_PS_OUT(EmissiveIntensity);
#undef CQ_PS_OUT
        out << YAML::Key << "Bursts" << YAML::Value << YAML::BeginSeq;
        for (const auto& b : ps.Bursts)
            out << YAML::Flow << YAML::BeginSeq << b.Time << b.Count << b.Cycles << b.Interval << YAML::EndSeq;
        out << YAML::EndSeq;
        out << YAML::Key << "ColorOverLifetime" << YAML::Value << YAML::BeginSeq;
        for (const auto& k : ps.ColorOverLifetime.Keys)
            out << YAML::Flow << YAML::BeginSeq << k.Time << k.Color.r << k.Color.g << k.Color.b << k.Color.a << YAML::EndSeq;
        out << YAML::EndSeq;
        out << YAML::Key << "SizeOverLifetime" << YAML::Value << YAML::BeginSeq;
        for (const auto& k : ps.SizeOverLifetime.Keys)
            out << YAML::Flow << YAML::BeginSeq << k.Time << k.Value << YAML::EndSeq;
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    static void DeserializeParticleSystem(const YAML::Node& n, ParticleSystemComponent& ps)
    {
#define CQ_PS_IN(field, T) if (n[#field]) ps.field = n[#field].as<T>()
        CQ_PS_IN(Duration, float); CQ_PS_IN(Looping, bool); CQ_PS_IN(Prewarm, bool); CQ_PS_IN(PlayOnAwake, bool); CQ_PS_IN(StopAction, int); CQ_PS_IN(StartDelay, float);
        CQ_PS_IN(StartLifetime, glm::vec2); CQ_PS_IN(StartSpeed, glm::vec2); CQ_PS_IN(StartSize, glm::vec2); CQ_PS_IN(StartRotation, glm::vec2);
        CQ_PS_IN(StartColorA, glm::vec4); CQ_PS_IN(StartColorB, glm::vec4); CQ_PS_IN(GravityModifier, float); CQ_PS_IN(SimulationSpeed, float); CQ_PS_IN(MaxParticles, int);
        if (n["SimulationSpace"]) ps.SimulationSpace = (ParticleSpace)n["SimulationSpace"].as<int>();
        CQ_PS_IN(EmissionEnabled, bool); CQ_PS_IN(RateOverTime, float); CQ_PS_IN(RateOverDistance, float);
        CQ_PS_IN(ShapeEnabled, bool); CQ_PS_IN(Radius, float); CQ_PS_IN(Angle, float); CQ_PS_IN(BoxSize, glm::vec3); CQ_PS_IN(EmitFromShell, bool); CQ_PS_IN(RandomizeDirection, float);
        if (n["Shape"]) ps.Shape = (ParticleShape)n["Shape"].as<int>();
        CQ_PS_IN(VelocityEnabled, bool); CQ_PS_IN(VelocityOverLifetime, glm::vec3); CQ_PS_IN(OrbitalSpeed, float);
        CQ_PS_IN(LimitVelocityEnabled, bool); CQ_PS_IN(Drag, float); CQ_PS_IN(SpeedLimit, float);
        CQ_PS_IN(NoiseEnabled, bool); CQ_PS_IN(NoiseStrength, float); CQ_PS_IN(NoiseFrequency, float); CQ_PS_IN(NoiseScrollSpeed, float);
        CQ_PS_IN(ColorOverLifetimeEnabled, bool); CQ_PS_IN(SizeOverLifetimeEnabled, bool); CQ_PS_IN(RotationOverLifetimeEnabled, bool); CQ_PS_IN(AngularVelocity, float);
        CQ_PS_IN(CollisionEnabled, bool); CQ_PS_IN(CollisionPlaneY, float); CQ_PS_IN(Bounce, float); CQ_PS_IN(LifetimeLoss, float);
        CQ_PS_IN(TextureSheetEnabled, bool); CQ_PS_IN(AnimationCycles, float); CQ_PS_IN(RandomStartFrame, bool);
        if (n["Tiles"]) ps.Tiles = glm::ivec2(n["Tiles"].as<glm::vec2>());
        if (n["RenderMode"]) ps.RenderMode = (ParticleRenderMode)n["RenderMode"].as<int>();
        if (n["Blend"]) ps.Blend = (ParticleBlend)n["Blend"].as<int>();
        if (n["Texture"]) ps.TexturePath = ResolveSerializablePath(n["Texture"].as<std::string>());
        CQ_PS_IN(LengthScale, float); CQ_PS_IN(SpeedScale, float); CQ_PS_IN(SortByDistance, bool); CQ_PS_IN(EmissiveIntensity, float);
#undef CQ_PS_IN
        ps.Bursts.clear();
        for (const auto& b : n["Bursts"])
            if (b.size() >= 4)
                ps.Bursts.push_back({ b[0].as<float>(), b[1].as<int>(), b[2].as<int>(), b[3].as<float>() });
        if (n["ColorOverLifetime"])
        {
            ps.ColorOverLifetime.Keys.clear();
            for (const auto& k : n["ColorOverLifetime"])
                if (k.size() >= 5)
                    ps.ColorOverLifetime.Keys.push_back({ k[0].as<float>(), glm::vec4(k[1].as<float>(), k[2].as<float>(), k[3].as<float>(), k[4].as<float>()) });
            if (ps.ColorOverLifetime.Keys.empty()) ps.ColorOverLifetime = ColorGradient{};
        }
        if (n["SizeOverLifetime"])
        {
            ps.SizeOverLifetime.Keys.clear();
            for (const auto& k : n["SizeOverLifetime"])
                if (k.size() >= 2)
                    ps.SizeOverLifetime.Keys.push_back({ k[0].as<float>(), k[1].as<float>() });
            if (ps.SizeOverLifetime.Keys.empty()) ps.SizeOverLifetime = FloatCurve{};
        }
    }

    // ---- UI bileşenleri
#define CQ_O(obj, f) out << YAML::Key << #f << YAML::Value << obj.f
#define CQ_OE(obj, f) out << YAML::Key << #f << YAML::Value << (int)obj.f
#define CQ_I(node, obj, f, T) if (node[#f]) obj.f = node[#f].as<T>()
#define CQ_IE(node, obj, f, E) if (node[#f]) obj.f = (E)node[#f].as<int>()
    static void SerializeSelectable(YAML::Emitter& out, const UISelectableState& s)
    {
        out << YAML::Key << "Selectable" << YAML::BeginMap;
        CQ_O(s, Interactable); CQ_OE(s, Transition); CQ_O(s, NormalColor); CQ_O(s, HighlightedColor); CQ_O(s, PressedColor);
        CQ_O(s, DisabledColor); CQ_O(s, FadeDuration);
        out << YAML::EndMap;
    }
    static void DeserializeSelectable(const YAML::Node& n, UISelectableState& s)
    {
        if (!n) return;
        CQ_I(n, s, Interactable, bool); CQ_IE(n, s, Transition, UITransition); CQ_I(n, s, NormalColor, glm::vec4); CQ_I(n, s, HighlightedColor, glm::vec4);
        CQ_I(n, s, PressedColor, glm::vec4); CQ_I(n, s, DisabledColor, glm::vec4); CQ_I(n, s, FadeDuration, float);
        s.CurrentTint = s.NormalColor;
    }

    static void SerializeUI(YAML::Emitter& out, Entity entity)
    {
        if (entity.HasComponent<RectTransformComponent>())
        {
            auto& c = entity.GetComponent<RectTransformComponent>();
            out << YAML::Key << "RectTransformComponent" << YAML::BeginMap;
            CQ_O(c, AnchorMin); CQ_O(c, AnchorMax); CQ_O(c, Pivot); CQ_O(c, AnchoredPosition); CQ_O(c, SizeDelta); CQ_O(c, Rotation); CQ_O(c, Scale);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIImageComponent>())
        {
            auto& c = entity.GetComponent<UIImageComponent>();
            out << YAML::Key << "UIImageComponent" << YAML::BeginMap;
            CQ_O(c, Color); out << YAML::Key << "TexturePath" << YAML::Value << ToSerializablePath(c.TexturePath); CQ_O(c, SpriteName); CQ_OE(c, Type);
            CQ_O(c, Border); CQ_O(c, PixelsPerUnitMultiplier); CQ_OE(c, FillMethod); CQ_O(c, FillAmount); CQ_O(c, FillClockwise);
            CQ_O(c, PreserveAspect); CQ_O(c, RaycastTarget); CQ_O(c, CornerRadius);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UITextComponent>())
        {
            auto& c = entity.GetComponent<UITextComponent>();
            out << YAML::Key << "UITextComponent" << YAML::BeginMap;
            CQ_O(c, Text); out << YAML::Key << "FontPath" << YAML::Value << ToSerializablePath(c.FontPath); CQ_O(c, FontSize); CQ_O(c, Color);
            CQ_OE(c, AlignH); CQ_OE(c, AlignV); CQ_O(c, WordWrap); CQ_O(c, BestFit); CQ_O(c, MinSize); CQ_O(c, MaxSize); CQ_O(c, LineSpacing);
            CQ_O(c, Outline); CQ_O(c, OutlineColor); CQ_O(c, Shadow); CQ_O(c, ShadowOffset); CQ_O(c, ShadowColor); CQ_O(c, RaycastTarget); CQ_O(c, RichText);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIButtonComponent>())
        {
            auto& c = entity.GetComponent<UIButtonComponent>();
            out << YAML::Key << "UIButtonComponent" << YAML::BeginMap;
            SerializeSelectable(out, c.Selectable); CQ_O(c, OnClick);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIToggleComponent>())
        {
            auto& c = entity.GetComponent<UIToggleComponent>();
            out << YAML::Key << "UIToggleComponent" << YAML::BeginMap;
            SerializeSelectable(out, c.Selectable); CQ_O(c, IsOn); CQ_O(c, Group); CQ_O(c, OnValueChanged);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UISliderComponent>())
        {
            auto& c = entity.GetComponent<UISliderComponent>();
            out << YAML::Key << "UISliderComponent" << YAML::BeginMap;
            SerializeSelectable(out, c.Selectable); CQ_O(c, MinValue); CQ_O(c, MaxValue); CQ_O(c, Value); CQ_O(c, WholeNumbers); CQ_OE(c, Direction);
            CQ_O(c, BackgroundColor); CQ_O(c, FillColor); CQ_O(c, HandleColor); CQ_O(c, HandleSize); CQ_O(c, OnValueChanged);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIInputFieldComponent>())
        {
            auto& c = entity.GetComponent<UIInputFieldComponent>();
            out << YAML::Key << "UIInputFieldComponent" << YAML::BeginMap;
            SerializeSelectable(out, c.Selectable); CQ_O(c, Text); CQ_O(c, Placeholder); CQ_O(c, CharacterLimit); CQ_OE(c, ContentType); CQ_O(c, FontSize);
            CQ_O(c, TextColor); CQ_O(c, PlaceholderColor); CQ_O(c, BackgroundColor); CQ_O(c, OnSubmit);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIDropdownComponent>())
        {
            auto& c = entity.GetComponent<UIDropdownComponent>();
            out << YAML::Key << "UIDropdownComponent" << YAML::BeginMap;
            SerializeSelectable(out, c.Selectable);
            out << YAML::Key << "Options" << YAML::Value << YAML::Flow << c.Options;
            CQ_O(c, Value); CQ_O(c, FontSize); CQ_O(c, OnValueChanged);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UIScrollRectComponent>())
        {
            auto& c = entity.GetComponent<UIScrollRectComponent>();
            out << YAML::Key << "UIScrollRectComponent" << YAML::BeginMap;
            CQ_O(c, Horizontal); CQ_O(c, Vertical); CQ_O(c, ScrollSensitivity); CQ_O(c, Mask); CQ_O(c, ShowScrollbar);
            out << YAML::EndMap;
        }
        if (entity.HasComponent<UILayoutGroupComponent>())
        {
            auto& c = entity.GetComponent<UILayoutGroupComponent>();
            out << YAML::Key << "UILayoutGroupComponent" << YAML::BeginMap;
            CQ_OE(c, Type); CQ_O(c, Padding); CQ_O(c, Spacing); CQ_O(c, CellSize); CQ_O(c, GridSpacing); CQ_O(c, Constraint); CQ_OE(c, ChildAlignment);
            CQ_O(c, ControlChildWidth); CQ_O(c, ControlChildHeight); CQ_O(c, ChildForceExpandWidth); CQ_O(c, ChildForceExpandHeight); CQ_O(c, FitContent);
            out << YAML::EndMap;
        }
    }

    static void DeserializeUI(const YAML::Node& entity, Entity e)
    {
        if (auto n = entity["RectTransformComponent"])
        {
            auto& c = e.AddComponent<RectTransformComponent>();
            CQ_I(n, c, AnchorMin, glm::vec2); CQ_I(n, c, AnchorMax, glm::vec2); CQ_I(n, c, Pivot, glm::vec2); CQ_I(n, c, AnchoredPosition, glm::vec2);
            CQ_I(n, c, SizeDelta, glm::vec2); CQ_I(n, c, Rotation, float); CQ_I(n, c, Scale, glm::vec2);
        }
        if (auto n = entity["UIImageComponent"])
        {
            auto& c = e.AddComponent<UIImageComponent>();
            CQ_I(n, c, Color, glm::vec4); if (n["TexturePath"]) c.TexturePath = ResolveSerializablePath(n["TexturePath"].as<std::string>());
            CQ_I(n, c, SpriteName, std::string); CQ_IE(n, c, Type, UIImageType); CQ_I(n, c, Border, glm::vec4); CQ_I(n, c, PixelsPerUnitMultiplier, float);
            CQ_IE(n, c, FillMethod, UIFillMethod); CQ_I(n, c, FillAmount, float); CQ_I(n, c, FillClockwise, bool); CQ_I(n, c, PreserveAspect, bool);
            CQ_I(n, c, RaycastTarget, bool); CQ_I(n, c, CornerRadius, float);
        }
        if (auto n = entity["UITextComponent"])
        {
            auto& c = e.AddComponent<UITextComponent>();
            CQ_I(n, c, Text, std::string); if (n["FontPath"]) c.FontPath = ResolveSerializablePath(n["FontPath"].as<std::string>());
            CQ_I(n, c, FontSize, float); CQ_I(n, c, Color, glm::vec4); CQ_IE(n, c, AlignH, UITextAlignH); CQ_IE(n, c, AlignV, UITextAlignV);
            CQ_I(n, c, WordWrap, bool); CQ_I(n, c, BestFit, bool); CQ_I(n, c, MinSize, float); CQ_I(n, c, MaxSize, float); CQ_I(n, c, LineSpacing, float);
            CQ_I(n, c, Outline, bool); CQ_I(n, c, OutlineColor, glm::vec4); CQ_I(n, c, Shadow, bool); CQ_I(n, c, ShadowOffset, glm::vec2);
            CQ_I(n, c, ShadowColor, glm::vec4); CQ_I(n, c, RaycastTarget, bool); CQ_I(n, c, RichText, bool);
        }
        if (auto n = entity["UIButtonComponent"])
        {
            auto& c = e.AddComponent<UIButtonComponent>();
            DeserializeSelectable(n["Selectable"], c.Selectable); CQ_I(n, c, OnClick, std::string);
        }
        if (auto n = entity["UIToggleComponent"])
        {
            auto& c = e.AddComponent<UIToggleComponent>();
            DeserializeSelectable(n["Selectable"], c.Selectable); CQ_I(n, c, IsOn, bool); CQ_I(n, c, Group, int); CQ_I(n, c, OnValueChanged, std::string);
        }
        if (auto n = entity["UISliderComponent"])
        {
            auto& c = e.AddComponent<UISliderComponent>();
            DeserializeSelectable(n["Selectable"], c.Selectable); CQ_I(n, c, MinValue, float); CQ_I(n, c, MaxValue, float); CQ_I(n, c, Value, float);
            CQ_I(n, c, WholeNumbers, bool); CQ_IE(n, c, Direction, UISliderDirection); CQ_I(n, c, BackgroundColor, glm::vec4); CQ_I(n, c, FillColor, glm::vec4);
            CQ_I(n, c, HandleColor, glm::vec4); CQ_I(n, c, HandleSize, float); CQ_I(n, c, OnValueChanged, std::string);
        }
        if (auto n = entity["UIInputFieldComponent"])
        {
            auto& c = e.AddComponent<UIInputFieldComponent>();
            DeserializeSelectable(n["Selectable"], c.Selectable); CQ_I(n, c, Text, std::string); CQ_I(n, c, Placeholder, std::string);
            CQ_I(n, c, CharacterLimit, int); CQ_IE(n, c, ContentType, UIContentType); CQ_I(n, c, FontSize, float); CQ_I(n, c, TextColor, glm::vec4);
            CQ_I(n, c, PlaceholderColor, glm::vec4); CQ_I(n, c, BackgroundColor, glm::vec4); CQ_I(n, c, OnSubmit, std::string);
        }
        if (auto n = entity["UIDropdownComponent"])
        {
            auto& c = e.AddComponent<UIDropdownComponent>();
            DeserializeSelectable(n["Selectable"], c.Selectable);
            if (n["Options"]) c.Options = n["Options"].as<std::vector<std::string>>();
            CQ_I(n, c, Value, int); CQ_I(n, c, FontSize, float); CQ_I(n, c, OnValueChanged, std::string);
        }
        if (auto n = entity["UIScrollRectComponent"])
        {
            auto& c = e.AddComponent<UIScrollRectComponent>();
            CQ_I(n, c, Horizontal, bool); CQ_I(n, c, Vertical, bool); CQ_I(n, c, ScrollSensitivity, float); CQ_I(n, c, Mask, bool); CQ_I(n, c, ShowScrollbar, bool);
        }
        if (auto n = entity["UILayoutGroupComponent"])
        {
            auto& c = e.AddComponent<UILayoutGroupComponent>();
            CQ_IE(n, c, Type, UILayoutType); CQ_I(n, c, Padding, glm::vec4); CQ_I(n, c, Spacing, float); CQ_I(n, c, CellSize, glm::vec2);
            CQ_I(n, c, GridSpacing, glm::vec2); CQ_I(n, c, Constraint, int); CQ_IE(n, c, ChildAlignment, UIChildAlignment);
            CQ_I(n, c, ControlChildWidth, bool); CQ_I(n, c, ControlChildHeight, bool); CQ_I(n, c, ChildForceExpandWidth, bool);
            CQ_I(n, c, ChildForceExpandHeight, bool); CQ_I(n, c, FitContent, bool);
        }
    }
#undef CQ_O
#undef CQ_OE
#undef CQ_I
#undef CQ_IE


    // ---- RenderSettings (gölge / sis / post-process / ortam) ------------------
    // Bake (Lighting Settings) alanları: sahne dosyası ve .cqlighting varlığı aynı biçimi kullanır
    static void EmitBakeSettings(YAML::Emitter& out, const LightBakeSettings& bk)
    {
        if (!bk.SettingsAsset.empty())
            out << YAML::Key << "SettingsAsset" << YAML::Value << ToSerializablePath(bk.SettingsAsset);
        if (!bk.DefaultLightmapParameters.empty())
            out << YAML::Key << "DefaultLightmapParameters" << YAML::Value << ToSerializablePath(bk.DefaultLightmapParameters);
        out << YAML::Key << "RealtimeGI" << YAML::Value << bk.RealtimeGI;
        out << YAML::Key << "BakedGI" << YAML::Value << bk.BakedGI;
        out << YAML::Key << "LightingMode" << YAML::Value << bk.LightingMode;
        out << YAML::Key << "Lightmapper" << YAML::Value << bk.Lightmapper;
        out << YAML::Key << "ImportanceSampling" << YAML::Value << bk.ImportanceSampling;
        out << YAML::Key << "DirectSamples" << YAML::Value << bk.DirectSamples;
        out << YAML::Key << "IndirectSamples" << YAML::Value << bk.IndirectSamples;
        out << YAML::Key << "EnvironmentSamples" << YAML::Value << bk.EnvironmentSamples;
        out << YAML::Key << "MaxBounces" << YAML::Value << bk.MaxBounces;
        out << YAML::Key << "Filtering" << YAML::Value << bk.Filtering;
        out << YAML::Key << "Packing" << YAML::Value << bk.Packing;
        out << YAML::Key << "Resolution" << YAML::Value << bk.Resolution;
        out << YAML::Key << "MaxLightmapSize" << YAML::Value << bk.MaxLightmapSize;
        out << YAML::Key << "UseMipmapLimits" << YAML::Value << bk.UseMipmapLimits;
        out << YAML::Key << "Compression" << YAML::Value << bk.Compression;
        out << YAML::Key << "AmbientOcclusion" << YAML::Value << bk.AmbientOcclusion;
        out << YAML::Key << "DirectionalMode" << YAML::Value << bk.DirectionalMode;
        out << YAML::Key << "AlbedoBoost" << YAML::Value << bk.AlbedoBoost;
        out << YAML::Key << "IndirectIntensity" << YAML::Value << bk.IndirectIntensity;
        out << YAML::Key << "RealtimeResolution" << YAML::Value << bk.RealtimeResolution;
        out << YAML::Key << "RealtimeIndirectSamples" << YAML::Value << bk.RealtimeIndirectSamples;
        out << YAML::Key << "RealtimeMaxBounces" << YAML::Value << bk.RealtimeMaxBounces;
        out << YAML::Key << "AutoGenerate" << YAML::Value << bk.AutoGenerate;
        out << YAML::Key << "BakeOnSceneLoad" << YAML::Value << bk.BakeOnSceneLoad;
        out << YAML::Key << "APVFitToScene" << YAML::Value << bk.APVFitToScene;
        out << YAML::Key << "APVOrigin" << YAML::Value << bk.APVOrigin;
        out << YAML::Key << "APVSize" << YAML::Value << bk.APVSize;
        out << YAML::Key << "APVProbeOffset" << YAML::Value << bk.APVProbeOffset;
        out << YAML::Key << "APVMinSpacing" << YAML::Value << bk.APVMinSpacing;
        out << YAML::Key << "APVMaxSpacing" << YAML::Value << bk.APVMaxSpacing;
        out << YAML::Key << "APVBakingMode" << YAML::Value << bk.APVBakingMode;
        out << YAML::Key << "APVDilation" << YAML::Value << bk.APVDilation;
        out << YAML::Key << "APVVirtualOffset" << YAML::Value << bk.APVVirtualOffset;
        out << YAML::Key << "APVValidityThreshold" << YAML::Value << bk.APVValidityThreshold;
        out << YAML::Key << "APVSearchDistanceMultiplier" << YAML::Value << bk.APVSearchDistanceMultiplier;
        out << YAML::Key << "APVGeometryBias" << YAML::Value << bk.APVGeometryBias;
        out << YAML::Key << "APVRayOriginBias" << YAML::Value << bk.APVRayOriginBias;
    }

    static void SerializeRenderSettings(YAML::Emitter& out, const RenderSettings& rs)
    {
        out << YAML::Key << "RenderSettings" << YAML::Value << YAML::BeginMap;

        const auto& sh = rs.Shadows;
        out << YAML::Key << "Shadows" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Enabled" << YAML::Value << sh.Enabled;
        out << YAML::Key << "Quality" << YAML::Value << (int)sh.Quality;
        out << YAML::Key << "Resolution" << YAML::Value << sh.Resolution;
        out << YAML::Key << "Distance" << YAML::Value << sh.Distance;
        out << YAML::Key << "CascadeCount" << YAML::Value << sh.CascadeCount;
        out << YAML::Key << "CascadeSplitLambda" << YAML::Value << sh.CascadeSplitLambda;
        out << YAML::Key << "Strength" << YAML::Value << sh.Strength;
        out << YAML::Key << "Softness" << YAML::Value << sh.Softness;
        out << YAML::Key << "DepthBias" << YAML::Value << sh.DepthBias;
        out << YAML::Key << "NormalBias" << YAML::Value << sh.NormalBias;
        out << YAML::Key << "Color" << YAML::Value << sh.Color;
        out << YAML::EndMap;

        const auto& fog = rs.Fog;
        out << YAML::Key << "Fog" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Enabled" << YAML::Value << fog.Enabled;
        out << YAML::Key << "Mode" << YAML::Value << (int)fog.Mode;
        out << YAML::Key << "Color" << YAML::Value << fog.Color;
        out << YAML::Key << "Density" << YAML::Value << fog.Density;
        out << YAML::Key << "Start" << YAML::Value << fog.Start;
        out << YAML::Key << "End" << YAML::Value << fog.End;
        out << YAML::Key << "HeightFalloff" << YAML::Value << fog.HeightFalloff;
        out << YAML::Key << "BaseHeight" << YAML::Value << fog.BaseHeight;
        out << YAML::EndMap;

        const auto& pp = rs.Post;
        out << YAML::Key << "Post" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Enabled" << YAML::Value << pp.Enabled;
        out << YAML::Key << "Tonemapper" << YAML::Value << (int)pp.Tonemap;
        out << YAML::Key << "Exposure" << YAML::Value << pp.Exposure;
        out << YAML::Key << "Bloom" << YAML::Value << pp.Bloom;
        out << YAML::Key << "BloomThreshold" << YAML::Value << pp.BloomThreshold;
        out << YAML::Key << "BloomKnee" << YAML::Value << pp.BloomKnee;
        out << YAML::Key << "BloomIntensity" << YAML::Value << pp.BloomIntensity;
        out << YAML::Key << "BloomScatter" << YAML::Value << pp.BloomScatter;
        out << YAML::Key << "BloomTint" << YAML::Value << pp.BloomTint;
        out << YAML::Key << "AmbientOcclusion" << YAML::Value << pp.AmbientOcclusion;
        out << YAML::Key << "AORadius" << YAML::Value << pp.AORadius;
        out << YAML::Key << "AOIntensity" << YAML::Value << pp.AOIntensity;
        out << YAML::Key << "AOPower" << YAML::Value << pp.AOPower;
        out << YAML::Key << "ColorGrading" << YAML::Value << pp.ColorGrading;
        out << YAML::Key << "Contrast" << YAML::Value << pp.Contrast;
        out << YAML::Key << "Saturation" << YAML::Value << pp.Saturation;
        out << YAML::Key << "Temperature" << YAML::Value << pp.Temperature;
        out << YAML::Key << "Tint" << YAML::Value << pp.Tint;
        out << YAML::Key << "Lift" << YAML::Value << pp.Lift;
        out << YAML::Key << "Gamma" << YAML::Value << pp.Gamma;
        out << YAML::Key << "Gain" << YAML::Value << pp.Gain;
        out << YAML::Key << "Vignette" << YAML::Value << pp.Vignette;
        out << YAML::Key << "VignetteIntensity" << YAML::Value << pp.VignetteIntensity;
        out << YAML::Key << "VignetteSmoothness" << YAML::Value << pp.VignetteSmoothness;
        out << YAML::Key << "VignetteColor" << YAML::Value << pp.VignetteColor;
        out << YAML::Key << "ChromaticAberration" << YAML::Value << pp.ChromaticAberration;
        out << YAML::Key << "ChromaticAberrationIntensity" << YAML::Value << pp.ChromaticAberrationIntensity;
        out << YAML::Key << "FilmGrain" << YAML::Value << pp.FilmGrain;
        out << YAML::Key << "FilmGrainIntensity" << YAML::Value << pp.FilmGrainIntensity;
        out << YAML::Key << "Sharpen" << YAML::Value << pp.Sharpen;
        out << YAML::Key << "SharpenAmount" << YAML::Value << pp.SharpenAmount;
        out << YAML::Key << "AntiAliasing" << YAML::Value << (int)pp.AntiAliasing;
        out << YAML::Key << "TAAFeedback" << YAML::Value << pp.TAAFeedback;
        out << YAML::Key << "TAASharpness" << YAML::Value << pp.TAASharpness;
        out << YAML::Key << "ScreenSpaceReflections" << YAML::Value << pp.ScreenSpaceReflections;
        out << YAML::Key << "SSRIntensity" << YAML::Value << pp.SSRIntensity;
        out << YAML::Key << "SSRMaxDistance" << YAML::Value << pp.SSRMaxDistance;
        out << YAML::Key << "SSRThickness" << YAML::Value << pp.SSRThickness;
        out << YAML::Key << "SSRMaxRoughness" << YAML::Value << pp.SSRMaxRoughness;
        out << YAML::Key << "ScreenSpaceGI" << YAML::Value << pp.ScreenSpaceGI;
        out << YAML::Key << "SSGIIntensity" << YAML::Value << pp.SSGIIntensity;
        out << YAML::Key << "SSGIRadius" << YAML::Value << pp.SSGIRadius;
        out << YAML::Key << "SSGIRays" << YAML::Value << pp.SSGIRays;
        out << YAML::Key << "VolumetricFog" << YAML::Value << pp.VolumetricFog;
        out << YAML::Key << "VolumetricFogDensity" << YAML::Value << pp.VolumetricFogDensity;
        out << YAML::Key << "VolumetricFogHeightFalloff" << YAML::Value << pp.VolumetricFogHeightFalloff;
        out << YAML::Key << "VolumetricFogBaseHeight" << YAML::Value << pp.VolumetricFogBaseHeight;
        out << YAML::Key << "VolumetricFogAnisotropy" << YAML::Value << pp.VolumetricFogAnisotropy;
        out << YAML::Key << "VolumetricFogIntensity" << YAML::Value << pp.VolumetricFogIntensity;
        out << YAML::Key << "VolumetricFogDistance" << YAML::Value << pp.VolumetricFogDistance;
        out << YAML::Key << "VolumetricFogAlbedo" << YAML::Value << pp.VolumetricFogAlbedo;
        out << YAML::Key << "VolumetricFogAmbient" << YAML::Value << pp.VolumetricFogAmbient;
        out << YAML::Key << "VolumetricClouds" << YAML::Value << pp.VolumetricClouds;
        out << YAML::Key << "CloudCoverage" << YAML::Value << pp.CloudCoverage;
        out << YAML::Key << "CloudDensity" << YAML::Value << pp.CloudDensity;
        out << YAML::Key << "CloudBottom" << YAML::Value << pp.CloudBottom;
        out << YAML::Key << "CloudThickness" << YAML::Value << pp.CloudThickness;
        out << YAML::Key << "CloudScale" << YAML::Value << pp.CloudScale;
        out << YAML::Key << "CloudWindSpeed" << YAML::Value << pp.CloudWindSpeed;
        out << YAML::Key << "CloudWindDirection" << YAML::Value << pp.CloudWindDirection;
        out << YAML::Key << "CloudColor" << YAML::Value << pp.CloudColor;
        out << YAML::Key << "DepthOfField" << YAML::Value << pp.DepthOfField;
        out << YAML::Key << "FocusDistance" << YAML::Value << pp.FocusDistance;
        out << YAML::Key << "Aperture" << YAML::Value << pp.Aperture;
        out << YAML::Key << "FocalLength" << YAML::Value << pp.FocalLength;
        out << YAML::Key << "DOFMaxBlur" << YAML::Value << pp.DOFMaxBlur;
        out << YAML::Key << "MotionBlur" << YAML::Value << pp.MotionBlur;
        out << YAML::Key << "MotionBlurIntensity" << YAML::Value << pp.MotionBlurIntensity;
        out << YAML::Key << "MotionBlurMax" << YAML::Value << pp.MotionBlurMax;
        out << YAML::Key << "AutoExposure" << YAML::Value << pp.AutoExposure;
        out << YAML::Key << "AutoExposureMin" << YAML::Value << pp.AutoExposureMin;
        out << YAML::Key << "AutoExposureMax" << YAML::Value << pp.AutoExposureMax;
        out << YAML::Key << "AutoExposureSpeedUp" << YAML::Value << pp.AutoExposureSpeedUp;
        out << YAML::Key << "AutoExposureSpeedDown" << YAML::Value << pp.AutoExposureSpeedDown;
        out << YAML::Key << "AutoExposureCompensation" << YAML::Value << pp.AutoExposureCompensation;
        out << YAML::EndMap;

        const auto& env = rs.Environment;
        out << YAML::Key << "Environment" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Sky" << YAML::Value << (int)env.Sky;
        out << YAML::Key << "BackgroundColor" << YAML::Value << env.BackgroundColor;
        out << YAML::Key << "ImageBasedLighting" << YAML::Value << env.ImageBasedLighting;
        out << YAML::Key << "ReflectionIntensity" << YAML::Value << env.ReflectionIntensity;
        out << YAML::Key << "SkyboxIntensity" << YAML::Value << env.SkyboxIntensity;
        out << YAML::Key << "SpecularOcclusion" << YAML::Value << env.SpecularOcclusion;
        out << YAML::Key << "AtmosphereRayleigh" << YAML::Value << env.AtmosphereRayleigh;
        out << YAML::Key << "AtmosphereMie" << YAML::Value << env.AtmosphereMie;
        out << YAML::Key << "AtmosphereMieAnisotropy" << YAML::Value << env.AtmosphereMieAnisotropy;
        out << YAML::Key << "AtmosphereOzone" << YAML::Value << env.AtmosphereOzone;
        out << YAML::Key << "AtmosphereSunDisk" << YAML::Value << env.AtmosphereSunDisk;
        out << YAML::Key << "AtmosphereIntensity" << YAML::Value << env.AtmosphereIntensity;
        out << YAML::Key << "AtmosphereGroundAlbedo" << YAML::Value << env.AtmosphereGroundAlbedo;
        out << YAML::EndMap;

        out << YAML::Key << "Bake" << YAML::Value << YAML::BeginMap;
        EmitBakeSettings(out, rs.Bake);
        out << YAML::EndMap;

        out << YAML::EndMap; // RenderSettings
    }

    template<typename T>
    static void ReadIf(const YAML::Node& node, const char* key, T& value)
    {
        if (node && node[key])
        {
            try { value = node[key].as<T>(); }
            catch (const std::exception&) { /* bozuk değer: varsayılan kalır */ }
        }
    }

    template<typename E>
    static void ReadEnumIf(const YAML::Node& node, const char* key, E& value)
    {
        int v = (int)value;
        ReadIf(node, key, v);
        value = (E)v;
    }

    static void ReadBakeSettings(const YAML::Node& bake, LightBakeSettings& b)
    {
        if (bake["SettingsAsset"]) b.SettingsAsset = ResolveSerializablePath(bake["SettingsAsset"].as<std::string>());
        if (bake["DefaultLightmapParameters"]) b.DefaultLightmapParameters = ResolveSerializablePath(bake["DefaultLightmapParameters"].as<std::string>());
        ReadIf(bake, "RealtimeGI", b.RealtimeGI);
        ReadIf(bake, "BakedGI", b.BakedGI);
        ReadIf(bake, "LightingMode", b.LightingMode);
        ReadIf(bake, "Lightmapper", b.Lightmapper);
        ReadIf(bake, "ImportanceSampling", b.ImportanceSampling);
        ReadIf(bake, "DirectSamples", b.DirectSamples);
        ReadIf(bake, "IndirectSamples", b.IndirectSamples);
        ReadIf(bake, "EnvironmentSamples", b.EnvironmentSamples);
        ReadIf(bake, "MaxBounces", b.MaxBounces);
        ReadIf(bake, "Filtering", b.Filtering);
        ReadIf(bake, "Packing", b.Packing);
        ReadIf(bake, "Resolution", b.Resolution);
        ReadIf(bake, "MaxLightmapSize", b.MaxLightmapSize);
        ReadIf(bake, "UseMipmapLimits", b.UseMipmapLimits);
        ReadIf(bake, "Compression", b.Compression);
        ReadIf(bake, "AmbientOcclusion", b.AmbientOcclusion);
        ReadIf(bake, "DirectionalMode", b.DirectionalMode);
        ReadIf(bake, "AlbedoBoost", b.AlbedoBoost);
        ReadIf(bake, "IndirectIntensity", b.IndirectIntensity);
        ReadIf(bake, "RealtimeResolution", b.RealtimeResolution);
        ReadIf(bake, "RealtimeIndirectSamples", b.RealtimeIndirectSamples);
        ReadIf(bake, "RealtimeMaxBounces", b.RealtimeMaxBounces);
        ReadIf(bake, "AutoGenerate", b.AutoGenerate);
        ReadIf(bake, "BakeOnSceneLoad", b.BakeOnSceneLoad);
        ReadIf(bake, "APVFitToScene", b.APVFitToScene);
        ReadIf(bake, "APVOrigin", b.APVOrigin);
        ReadIf(bake, "APVSize", b.APVSize);
        ReadIf(bake, "APVProbeOffset", b.APVProbeOffset);
        ReadIf(bake, "APVMinSpacing", b.APVMinSpacing);
        ReadIf(bake, "APVMaxSpacing", b.APVMaxSpacing);
        ReadIf(bake, "APVBakingMode", b.APVBakingMode);
        ReadIf(bake, "APVDilation", b.APVDilation);
        ReadIf(bake, "APVVirtualOffset", b.APVVirtualOffset);
        ReadIf(bake, "APVValidityThreshold", b.APVValidityThreshold);
        ReadIf(bake, "APVSearchDistanceMultiplier", b.APVSearchDistanceMultiplier);
        ReadIf(bake, "APVGeometryBias", b.APVGeometryBias);
        ReadIf(bake, "APVRayOriginBias", b.APVRayOriginBias);
        // Lighting Settings varlığı: ayarlar dosyadan (varlık yolu korunur)
        if (!b.SettingsAsset.empty())
        {
            const std::string asset = b.SettingsAsset;
            if (SceneSerializer::LoadLightingSettings(asset, b))
                b.SettingsAsset = asset;
        }
    }

    static void DeserializeRenderSettings(const YAML::Node& node, RenderSettings& rs)
    {
        if (!node)
            return;
        if (auto sh = node["Shadows"])
        {
            auto& s = rs.Shadows;
            ReadIf(sh, "Enabled", s.Enabled);
            ReadEnumIf(sh, "Quality", s.Quality);
            ReadIf(sh, "Resolution", s.Resolution);
            ReadIf(sh, "Distance", s.Distance);
            ReadIf(sh, "CascadeCount", s.CascadeCount);
            ReadIf(sh, "CascadeSplitLambda", s.CascadeSplitLambda);
            ReadIf(sh, "Strength", s.Strength);
            ReadIf(sh, "Softness", s.Softness);
            ReadIf(sh, "DepthBias", s.DepthBias);
            ReadIf(sh, "NormalBias", s.NormalBias);
            ReadIf(sh, "Color", s.Color);
        }
        if (auto fog = node["Fog"])
        {
            auto& f = rs.Fog;
            ReadIf(fog, "Enabled", f.Enabled);
            ReadEnumIf(fog, "Mode", f.Mode);
            ReadIf(fog, "Color", f.Color);
            ReadIf(fog, "Density", f.Density);
            ReadIf(fog, "Start", f.Start);
            ReadIf(fog, "End", f.End);
            ReadIf(fog, "HeightFalloff", f.HeightFalloff);
            ReadIf(fog, "BaseHeight", f.BaseHeight);
        }
        if (auto pp = node["Post"])
        {
            auto& p = rs.Post;
            ReadIf(pp, "Enabled", p.Enabled);
            ReadEnumIf(pp, "Tonemapper", p.Tonemap);
            ReadIf(pp, "Exposure", p.Exposure);
            ReadIf(pp, "Bloom", p.Bloom);
            ReadIf(pp, "BloomThreshold", p.BloomThreshold);
            ReadIf(pp, "BloomKnee", p.BloomKnee);
            ReadIf(pp, "BloomIntensity", p.BloomIntensity);
            ReadIf(pp, "BloomScatter", p.BloomScatter);
            ReadIf(pp, "BloomTint", p.BloomTint);
            ReadIf(pp, "AmbientOcclusion", p.AmbientOcclusion);
            ReadIf(pp, "AORadius", p.AORadius);
            ReadIf(pp, "AOIntensity", p.AOIntensity);
            ReadIf(pp, "AOPower", p.AOPower);
            ReadIf(pp, "ColorGrading", p.ColorGrading);
            ReadIf(pp, "Contrast", p.Contrast);
            ReadIf(pp, "Saturation", p.Saturation);
            ReadIf(pp, "Temperature", p.Temperature);
            ReadIf(pp, "Tint", p.Tint);
            ReadIf(pp, "Lift", p.Lift);
            ReadIf(pp, "Gamma", p.Gamma);
            ReadIf(pp, "Gain", p.Gain);
            ReadIf(pp, "Vignette", p.Vignette);
            ReadIf(pp, "VignetteIntensity", p.VignetteIntensity);
            ReadIf(pp, "VignetteSmoothness", p.VignetteSmoothness);
            ReadIf(pp, "VignetteColor", p.VignetteColor);
            ReadIf(pp, "ChromaticAberration", p.ChromaticAberration);
            ReadIf(pp, "ChromaticAberrationIntensity", p.ChromaticAberrationIntensity);
            ReadIf(pp, "FilmGrain", p.FilmGrain);
            ReadIf(pp, "FilmGrainIntensity", p.FilmGrainIntensity);
            ReadIf(pp, "Sharpen", p.Sharpen);
            ReadIf(pp, "SharpenAmount", p.SharpenAmount);
            ReadEnumIf(pp, "AntiAliasing", p.AntiAliasing);
            ReadIf(pp, "TAAFeedback", p.TAAFeedback);
            ReadIf(pp, "TAASharpness", p.TAASharpness);
            ReadIf(pp, "ScreenSpaceReflections", p.ScreenSpaceReflections);
            ReadIf(pp, "SSRIntensity", p.SSRIntensity);
            ReadIf(pp, "SSRMaxDistance", p.SSRMaxDistance);
            ReadIf(pp, "SSRThickness", p.SSRThickness);
            ReadIf(pp, "SSRMaxRoughness", p.SSRMaxRoughness);
            ReadIf(pp, "ScreenSpaceGI", p.ScreenSpaceGI);
            ReadIf(pp, "SSGIIntensity", p.SSGIIntensity);
            ReadIf(pp, "SSGIRadius", p.SSGIRadius);
            ReadIf(pp, "SSGIRays", p.SSGIRays);
            ReadIf(pp, "VolumetricFog", p.VolumetricFog);
            ReadIf(pp, "VolumetricFogDensity", p.VolumetricFogDensity);
            ReadIf(pp, "VolumetricFogHeightFalloff", p.VolumetricFogHeightFalloff);
            ReadIf(pp, "VolumetricFogBaseHeight", p.VolumetricFogBaseHeight);
            ReadIf(pp, "VolumetricFogAnisotropy", p.VolumetricFogAnisotropy);
            ReadIf(pp, "VolumetricFogIntensity", p.VolumetricFogIntensity);
            ReadIf(pp, "VolumetricFogDistance", p.VolumetricFogDistance);
            ReadIf(pp, "VolumetricFogAlbedo", p.VolumetricFogAlbedo);
            ReadIf(pp, "VolumetricFogAmbient", p.VolumetricFogAmbient);
            ReadIf(pp, "VolumetricClouds", p.VolumetricClouds);
            ReadIf(pp, "CloudCoverage", p.CloudCoverage);
            ReadIf(pp, "CloudDensity", p.CloudDensity);
            ReadIf(pp, "CloudBottom", p.CloudBottom);
            ReadIf(pp, "CloudThickness", p.CloudThickness);
            ReadIf(pp, "CloudScale", p.CloudScale);
            ReadIf(pp, "CloudWindSpeed", p.CloudWindSpeed);
            ReadIf(pp, "CloudWindDirection", p.CloudWindDirection);
            ReadIf(pp, "CloudColor", p.CloudColor);
            ReadIf(pp, "DepthOfField", p.DepthOfField);
            ReadIf(pp, "FocusDistance", p.FocusDistance);
            ReadIf(pp, "Aperture", p.Aperture);
            ReadIf(pp, "FocalLength", p.FocalLength);
            ReadIf(pp, "DOFMaxBlur", p.DOFMaxBlur);
            ReadIf(pp, "MotionBlur", p.MotionBlur);
            ReadIf(pp, "MotionBlurIntensity", p.MotionBlurIntensity);
            ReadIf(pp, "MotionBlurMax", p.MotionBlurMax);
            ReadIf(pp, "AutoExposure", p.AutoExposure);
            ReadIf(pp, "AutoExposureMin", p.AutoExposureMin);
            ReadIf(pp, "AutoExposureMax", p.AutoExposureMax);
            ReadIf(pp, "AutoExposureSpeedUp", p.AutoExposureSpeedUp);
            ReadIf(pp, "AutoExposureSpeedDown", p.AutoExposureSpeedDown);
            ReadIf(pp, "AutoExposureCompensation", p.AutoExposureCompensation);
        }
        if (auto env = node["Environment"])
        {
            auto& e = rs.Environment;
            ReadEnumIf(env, "Sky", e.Sky);
            ReadIf(env, "BackgroundColor", e.BackgroundColor);
            ReadIf(env, "ImageBasedLighting", e.ImageBasedLighting);
            ReadIf(env, "ReflectionIntensity", e.ReflectionIntensity);
            ReadIf(env, "SkyboxIntensity", e.SkyboxIntensity);
            ReadIf(env, "SpecularOcclusion", e.SpecularOcclusion);
            ReadIf(env, "AtmosphereRayleigh", e.AtmosphereRayleigh);
            ReadIf(env, "AtmosphereMie", e.AtmosphereMie);
            ReadIf(env, "AtmosphereMieAnisotropy", e.AtmosphereMieAnisotropy);
            ReadIf(env, "AtmosphereOzone", e.AtmosphereOzone);
            ReadIf(env, "AtmosphereSunDisk", e.AtmosphereSunDisk);
            ReadIf(env, "AtmosphereIntensity", e.AtmosphereIntensity);
            ReadIf(env, "AtmosphereGroundAlbedo", e.AtmosphereGroundAlbedo);
        }
        if (auto bake = node["Bake"])
            ReadBakeSettings(bake, rs.Bake);

        // Akıl sağlığı sınırları (elle düzenlenmiş dosyalar için)
        rs.Shadows.CascadeCount = std::clamp(rs.Shadows.CascadeCount, 1, 4);
        rs.Shadows.Resolution = std::clamp(rs.Shadows.Resolution, 512, 8192);
        rs.Shadows.Distance = std::max(rs.Shadows.Distance, 1.0f);
    }

    std::string SceneSerializer::ToAssetPath(const std::string& absolutePath)
    {
        return ToSerializablePath(absolutePath);
    }

    std::string SceneSerializer::ResolveAssetPath(const std::string& path)
    {
        return ResolveSerializablePath(path);
    }

    SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
        : m_Scene(scene)
    {
    }

    static void SerializeEntity(YAML::Emitter& out, Entity entity)
    {
        out << YAML::BeginMap; // Entity
        out << YAML::Key << "Entity" << YAML::Value << entity.GetComponent<IDComponent>().ID;

        if (entity.HasComponent<TagComponent>())
        {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;

            auto& tag = entity.GetComponent<TagComponent>();
            out << YAML::Key << "Tag" << YAML::Value << tag.Tag;
            out << YAML::Key << "GameTag" << YAML::Value << tag.GameTag;
            out << YAML::Key << "IsStatic" << YAML::Value << tag.IsStatic;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<ActiveComponent>())
        {
            out << YAML::Key << "ActiveComponent";
            out << YAML::BeginMap;

            auto& active = entity.GetComponent<ActiveComponent>();
            out << YAML::Key << "Active" << YAML::Value << active.Active;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<TransformComponent>())
        {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;

            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Position" << YAML::Value << tc.Position;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CameraComponent>())
        {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap;

            auto& cameraComponent = entity.GetComponent<CameraComponent>();
            auto& camera = cameraComponent.Camera;

            out << YAML::Key << "Camera" << YAML::Value;
            out << YAML::BeginMap;
            out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
            out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
            out << YAML::EndMap;

            out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;
            out << YAML::Key << "Background" << YAML::Value << (int)cameraComponent.Background;
            out << YAML::Key << "BackgroundColor" << YAML::Value << cameraComponent.BackgroundColor;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpriteRendererComponent>())
        {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap;

            auto& spriteRenderer = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << spriteRenderer.Color;
            out << YAML::Key << "TilingFactor" << YAML::Value << spriteRenderer.TilingFactor;
            
            // Texture path kaydet
            if (spriteRenderer.Texture)
            {
                out << YAML::Key << "TexturePath" << YAML::Value << ToSerializablePath(spriteRenderer.Texture->GetPath());
            }
            if (!spriteRenderer.SpriteName.empty())
                out << YAML::Key << "SpriteName" << YAML::Value << spriteRenderer.SpriteName;
            out << YAML::Key << "Rect" << YAML::Value << spriteRenderer.Rect;
            out << YAML::Key << "Pivot" << YAML::Value << spriteRenderer.Pivot;
            out << YAML::Key << "Border" << YAML::Value << spriteRenderer.Border;
            out << YAML::Key << "PixelsPerUnit" << YAML::Value << spriteRenderer.PixelsPerUnit;
            out << YAML::Key << "FlipX" << YAML::Value << spriteRenderer.FlipX;
            out << YAML::Key << "FlipY" << YAML::Value << spriteRenderer.FlipY;
            out << YAML::Key << "DrawMode" << YAML::Value << (int)spriteRenderer.DrawMode;
            out << YAML::Key << "Size" << YAML::Value << spriteRenderer.Size;
            if (spriteRenderer.MaskInteraction != SpriteMaskInteraction::None)
                out << YAML::Key << "MaskInteraction" << YAML::Value << (int)spriteRenderer.MaskInteraction;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<LayerComponent>())
        {
            out << YAML::Key << "LayerComponent";
            out << YAML::BeginMap;

            auto& layer = entity.GetComponent<LayerComponent>();
            out << YAML::Key << "Layer" << YAML::Value << layer.Layer;
            out << YAML::Key << "OrderInLayer" << YAML::Value << layer.OrderInLayer;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<TextRendererComponent>())
        {
            out << YAML::Key << "TextRendererComponent";
            out << YAML::BeginMap;

            auto& textRenderer = entity.GetComponent<TextRendererComponent>();
            out << YAML::Key << "Text" << YAML::Value << textRenderer.Text;
            out << YAML::Key << "Color" << YAML::Value << textRenderer.Color;
            out << YAML::Key << "FontSize" << YAML::Value << textRenderer.FontSize;
            if (textRenderer.Alignment != TextAlignment::Left)
                out << YAML::Key << "Alignment" << YAML::Value << (int)textRenderer.Alignment;

            out << YAML::EndMap;
        }

        // NavMesh Agent / Obstacle / Surface: bileşen kayıt sistemi (ComponentRegistry) serileştirir

        if (entity.HasComponent<ReflectionProbeComponent>())
        {
            auto& comp = entity.GetComponent<ReflectionProbeComponent>();
            out << YAML::Key << "ReflectionProbeComponent" << YAML::BeginMap;
            out << YAML::Key << "BoxOffset" << YAML::Value << comp.BoxOffset;
            out << YAML::Key << "BoxSize" << YAML::Value << comp.BoxSize;
            out << YAML::Key << "Resolution" << YAML::Value << comp.Resolution;
            out << YAML::Key << "Intensity" << YAML::Value << comp.Intensity;
            out << YAML::Key << "Bounces" << YAML::Value << comp.Bounces;
            out << YAML::Key << "RealtimeUpdate" << YAML::Value << comp.RealtimeUpdate;
            out << YAML::Key << "BlendDistance" << YAML::Value << comp.BlendDistance;
            out << YAML::Key << "UpdateInterval" << YAML::Value << comp.UpdateInterval;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AdaptiveProbeVolumeComponent>())
        {
            auto& comp = entity.GetComponent<AdaptiveProbeVolumeComponent>();
            out << YAML::Key << "AdaptiveProbeVolumeComponent" << YAML::BeginMap;
            out << YAML::Key << "ProbeOffset" << YAML::Value << comp.ProbeOffset;
            out << YAML::Key << "MinSpacing" << YAML::Value << comp.MinSpacing;
            out << YAML::Key << "MaxSpacing" << YAML::Value << comp.MaxSpacing;
            out << YAML::Key << "BakingMode" << YAML::Value << comp.BakingMode;
            out << YAML::Key << "Dilation" << YAML::Value << comp.Dilation;
            out << YAML::Key << "VirtualOffset" << YAML::Value << comp.VirtualOffset;
            out << YAML::Key << "ValidityThreshold" << YAML::Value << comp.ValidityThreshold;
            out << YAML::Key << "SearchDistanceMultiplier" << YAML::Value << comp.SearchDistanceMultiplier;
            out << YAML::Key << "GeometryBias" << YAML::Value << comp.GeometryBias;
            out << YAML::Key << "RayOriginBias" << YAML::Value << comp.RayOriginBias;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<ParticleSystemComponent>())
            SerializeParticleSystem(out, entity.GetComponent<ParticleSystemComponent>());
        // Kayıtlı (yansımalı) bileşenler
        ComponentRegistry::SerializeEntity(out, entity);
        SerializeUI(out, entity);

        if (entity.HasComponent<TerrainComponent>())
        {
            auto& t = entity.GetComponent<TerrainComponent>();
            TerrainSystem::EnsureData(t);
            out << YAML::Key << "TerrainComponent" << YAML::BeginMap;
            out << YAML::Key << "Width" << YAML::Value << t.Width;
            out << YAML::Key << "Length" << YAML::Value << t.Length;
            out << YAML::Key << "Height" << YAML::Value << t.Height;
            out << YAML::Key << "Resolution" << YAML::Value << t.Resolution;
            out << YAML::Key << "SplatResolution" << YAML::Value << t.SplatResolution;
            out << YAML::Key << "LayerCount" << YAML::Value << t.LayerCount;
            out << YAML::Key << "CastShadows" << YAML::Value << t.CastShadows;
            out << YAML::Key << "ReceiveShadows" << YAML::Value << t.ReceiveShadows;
            out << YAML::Key << "EnableCollider" << YAML::Value << t.EnableCollider;
            out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
            for (int i = 0; i < TerrainComponent::MaxLayers; ++i)
            {
                const auto& l = t.Layers[(size_t)i];
                out << YAML::BeginMap;
                out << YAML::Key << "Texture" << YAML::Value << ToSerializablePath(l.TexturePath);
                out << YAML::Key << "Tint" << YAML::Value << l.Tint;
                out << YAML::Key << "Tiling" << YAML::Value << l.Tiling;
                out << YAML::Key << "Roughness" << YAML::Value << l.Roughness;
                out << YAML::Key << "Metallic" << YAML::Value << l.Metallic;
                if (!l.Asset.empty()) out << YAML::Key << "Asset" << YAML::Value << ToSerializablePath(l.Asset);
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::Key << "Heights" << YAML::Value << TerrainSystem::EncodeHeights(t);
            out << YAML::Key << "Splat" << YAML::Value << TerrainSystem::EncodeSplat(t);
            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshRendererComponent>())
        {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap;

            auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();
            out << YAML::Key << "MeshType" << YAML::Value << static_cast<int>(meshRenderer.Type);
            // Mesh alanında model dosyası (eski Model bileşeni)
            if (!meshRenderer.ModelPath.empty())
                out << YAML::Key << "Model" << YAML::Value << ToSerializablePath(meshRenderer.ModelPath);
            if (meshRenderer.Tint != glm::vec3(1.0f))
                out << YAML::Key << "Tint" << YAML::Value << meshRenderer.Tint;
            if (!meshRenderer.CastShadows)
                out << YAML::Key << "CastShadows" << YAML::Value << false;
            if (!meshRenderer.ReceiveShadows)
                out << YAML::Key << "ReceiveShadows" << YAML::Value << false;
            if (meshRenderer.RealtimeGI)
                out << YAML::Key << "RealtimeGI" << YAML::Value << true;
            if (!meshRenderer.LightmapParameters.empty())
                out << YAML::Key << "LightmapParameters" << YAML::Value << ToSerializablePath(meshRenderer.LightmapParameters);

            // Malzeme: dosyaya başvuru (Unity / Unreal), yoksa sahneye gömülü
            // Dosya yeniden adlandırıldı / taşındıysa güncel yol önbellekten
            const std::string materialPath = meshRenderer.MaterialPath.empty() ? std::string()
                : [&] { std::string now = MaterialAsset::PathOf(meshRenderer.MaterialInstance.get()); return now.empty() ? meshRenderer.MaterialPath : now; }();
            if (!meshRenderer.ModelPath.empty())
                ;   // model kendi malzemeleriyle çizilir (içe aktarma ayarları / .meta eşlemesi)
            else if (!materialPath.empty())
                out << YAML::Key << "MaterialAsset" << YAML::Value << ToSerializablePath(materialPath);
            else if (meshRenderer.MaterialInstance)
            {
                out << YAML::Key << "Material";
                EmitMaterial(out, *meshRenderer.MaterialInstance);
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<DirectionalLightComponent>())
        {
            out << YAML::Key << "DirectionalLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<DirectionalLightComponent>();
            out << YAML::Key << "Direction" << YAML::Value << light.Direction;
            // Yön artık nesnenin dönüşünden gelir (eski sahneler yüklenirken Direction -> dönüş)
            out << YAML::Key << "FromRotation" << YAML::Value << true;
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;
            out << YAML::Key << "Mode" << YAML::Value << (int)light.Mode;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<PointLightComponent>())
        {
            out << YAML::Key << "PointLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "Range" << YAML::Value << light.Range;
            out << YAML::Key << "Constant" << YAML::Value << light.Constant;
            out << YAML::Key << "Linear" << YAML::Value << light.Linear;
            out << YAML::Key << "Quadratic" << YAML::Value << light.Quadratic;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;
            out << YAML::Key << "Mode" << YAML::Value << (int)light.Mode;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpotLightComponent>())
        {
            out << YAML::Key << "SpotLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<SpotLightComponent>();
            out << YAML::Key << "Direction" << YAML::Value << light.Direction;
            out << YAML::Key << "FromRotation" << YAML::Value << true;
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "Range" << YAML::Value << light.Range;
            out << YAML::Key << "InnerConeAngle" << YAML::Value << light.InnerConeAngle;
            out << YAML::Key << "OuterConeAngle" << YAML::Value << light.OuterConeAngle;
            out << YAML::Key << "Constant" << YAML::Value << light.Constant;
            out << YAML::Key << "Linear" << YAML::Value << light.Linear;
            out << YAML::Key << "Quadratic" << YAML::Value << light.Quadratic;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;
            out << YAML::Key << "Mode" << YAML::Value << (int)light.Mode;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<NativeScriptComponent>())
        {
            auto& sc = entity.GetComponent<NativeScriptComponent>();
            for (const auto& script : sc.Scripts)
            {
                std::string keyName = script.ClassName.empty() ? "NativeScriptComponent" : script.ClassName + "_NativeScriptComponent";
                out << YAML::Key << keyName;
                out << YAML::BeginMap;
                out << YAML::Key << "ModuleName" << YAML::Value << script.ModuleName;
                out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
                out << YAML::Key << "ScriptPath" << YAML::Value << ToSerializablePath(script.ScriptPath);
                out << YAML::EndMap;
            }
        }

        if (entity.HasComponent<ConquerorScriptComponent>())
        {
            auto& sc = entity.GetComponent<ConquerorScriptComponent>();
            for (const auto& script : sc.Scripts)
            {
                std::string keyName = script.ClassName.empty() ? "ConquerorScriptComponent" : script.ClassName + "_ConquerorScriptComponent";
                out << YAML::Key << keyName;
                out << YAML::BeginMap;
                out << YAML::Key << "ScriptPath" << YAML::Value << ToSerializablePath(script.ScriptPath);
                out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
                if (!script.FieldOverrides.empty())
                {
                    out << YAML::Key << "Fields" << YAML::Value << YAML::BeginMap;
                    for (const auto& [name, v] : script.FieldOverrides)
                    {
                        out << YAML::Key << name << YAML::Value << YAML::BeginMap;
                        switch (v.Type)
                        {
                            case ScriptFieldOverride::Kind::Bool: out << YAML::Key << "Bool" << YAML::Value << v.Bool; break;
                            case ScriptFieldOverride::Kind::Int: out << YAML::Key << "Int" << YAML::Value << v.Int; break;
                            case ScriptFieldOverride::Kind::Float: out << YAML::Key << "Float" << YAML::Value << v.Float; break;
                            case ScriptFieldOverride::Kind::String: out << YAML::Key << "String" << YAML::Value << v.String; break;
                            default: out << YAML::Key << "Null" << YAML::Value << true; break;
                        }
                        out << YAML::EndMap;
                    }
                    out << YAML::EndMap;
                }
                out << YAML::EndMap;
            }
        }

        if (entity.HasComponent<AnimatorComponent>())
        {
            out << YAML::Key << "AnimatorComponent";
            out << YAML::BeginMap;
            auto& anim = entity.GetComponent<AnimatorComponent>();
            out << YAML::Key << "Playing" << YAML::Value << anim.Playing;
            out << YAML::Key << "Speed" << YAML::Value << anim.Speed;
            out << YAML::Key << "ActiveClipIndex" << YAML::Value << anim.ActiveClipIndex;
            out << YAML::Key << "CurrentTime" << YAML::Value << anim.CurrentTime;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AnimationComponent>())
        {
            out << YAML::Key << "AnimationComponent";
            out << YAML::BeginMap;
            auto& ac = entity.GetComponent<AnimationComponent>();
            out << YAML::Key << "ControllerPath" << YAML::Value << ToSerializablePath(ac.ControllerFilePath);
            out << YAML::Key << "Speed" << YAML::Value << ac.Speed;
            out << YAML::Key << "PlayOnAwake" << YAML::Value << ac.PlayOnAwake;

            out << YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
            for (const auto& param : ac.Parameters)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << param.Name;
                out << YAML::Key << "Type" << YAML::Value << static_cast<int>(param.Type);
                out << YAML::Key << "FloatValue" << YAML::Value << param.FloatValue;
                out << YAML::Key << "BoolValue" << YAML::Value << param.BoolValue;
                out << YAML::Key << "IntValue" << YAML::Value << param.IntValue;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<ButtonComponent>())
        {
            out << YAML::Key << "ButtonComponent";
            out << YAML::BeginMap;

            auto& button = entity.GetComponent<ButtonComponent>();
            out << YAML::Key << "Interactable" << YAML::Value << button.Interactable;
            out << YAML::Key << "Navigation" << YAML::Value << (int)button.Navigation;
            out << YAML::Key << "Use9Slice" << YAML::Value << button.Use9Slice;
            out << YAML::Key << "BorderSize" << YAML::Value << button.BorderSize;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << button.ColorMultiplier;
            out << YAML::Key << "TransitionSpeed" << YAML::Value << button.TransitionSpeed;
            
            // Renkler
            out << YAML::Key << "BackgroundNormalColor" << YAML::Value << button.BackgroundNormalColor;
            out << YAML::Key << "BackgroundHoverColor" << YAML::Value << button.BackgroundHoverColor;
            out << YAML::Key << "BackgroundPressedColor" << YAML::Value << button.BackgroundPressedColor;
            out << YAML::Key << "BackgroundDisabledColor" << YAML::Value << button.BackgroundDisabledColor;
            out << YAML::Key << "BackgroundHighlightedColor" << YAML::Value << button.BackgroundHighlightedColor;
            
            out << YAML::Key << "TextNormalColor" << YAML::Value << button.TextNormalColor;
            out << YAML::Key << "TextHoverColor" << YAML::Value << button.TextHoverColor;
            out << YAML::Key << "TextPressedColor" << YAML::Value << button.TextPressedColor;
            out << YAML::Key << "TextDisabledColor" << YAML::Value << button.TextDisabledColor;
            out << YAML::Key << "TextHighlightedColor" << YAML::Value << button.TextHighlightedColor;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CanvasComponent>())
        {
            out << YAML::Key << "CanvasComponent";
            out << YAML::BeginMap;

            auto& canvas = entity.GetComponent<CanvasComponent>();
            out << YAML::Key << "RenderMode" << YAML::Value << (int)canvas.Mode;
            out << YAML::Key << "PixelPerfect" << YAML::Value << canvas.PixelPerfect;
            out << YAML::Key << "SortOrder" << YAML::Value << canvas.SortOrder;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CanvasScalerComponent>())
        {
            out << YAML::Key << "CanvasScalerComponent";
            out << YAML::BeginMap;

            auto& scaler = entity.GetComponent<CanvasScalerComponent>();
            out << YAML::Key << "ScaleMode" << YAML::Value << (int)scaler.ScaleMode;
            out << YAML::Key << "ScaleFactor" << YAML::Value << scaler.ScaleFactor;
            out << YAML::Key << "ReferencePixelsPerUnit" << YAML::Value << scaler.ReferencePixelsPerUnit;
            out << YAML::Key << "ReferenceResolution" << YAML::Value << scaler.ReferenceResolution;
            out << YAML::Key << "MatchWidthOrHeight" << YAML::Value << scaler.MatchWidthOrHeight;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<GraphicRaycasterComponent>())
        {
            out << YAML::Key << "GraphicRaycasterComponent";
            out << YAML::BeginMap;

            auto& raycaster = entity.GetComponent<GraphicRaycasterComponent>();
            out << YAML::Key << "IgnoreReversedGraphics" << YAML::Value << raycaster.IgnoreReversedGraphics;
            out << YAML::Key << "BlockingObjects" << YAML::Value << (int)raycaster.Blocking;
            out << YAML::Key << "BlockingMask" << YAML::Value << raycaster.BlockingMask;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBody2DComponent>())
        {
            out << YAML::Key << "RigidBody2DComponent";
            out << YAML::BeginMap;

            auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb2d.Type;
            out << YAML::Key << "FixedRotation" << YAML::Value << rb2d.FixedRotation;
            out << YAML::Key << "UseAutoMass" << YAML::Value << rb2d.UseAutoMass;
            out << YAML::Key << "Mass" << YAML::Value << rb2d.Mass;
            out << YAML::Key << "LinearDrag" << YAML::Value << rb2d.LinearDrag;
            out << YAML::Key << "AngularDrag" << YAML::Value << rb2d.AngularDrag;
            out << YAML::Key << "GravityScale" << YAML::Value << rb2d.GravityScale;
            out << YAML::Key << "Interpolate" << YAML::Value << rb2d.Interpolate;
            out << YAML::Key << "ContinuousCollision" << YAML::Value << rb2d.ContinuousCollision;
            out << YAML::Key << "AllowSleep" << YAML::Value << rb2d.AllowSleep;
            if (!rb2d.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(rb2d.PhysicsMaterial);

            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxCollider2DComponent>())
        {
            out << YAML::Key << "BoxCollider2DComponent";
            out << YAML::BeginMap;

            auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc2d.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc2d.Size;
            out << YAML::Key << "Density" << YAML::Value << bc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << bc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << bc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2d.RestitutionThreshold;
            out << YAML::Key << "IsTrigger" << YAML::Value << bc2d.IsTrigger;
            out << YAML::Key << "EdgeRadius" << YAML::Value << bc2d.EdgeRadius;
            if (!bc2d.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(bc2d.PhysicsMaterial);
            out << YAML::Key << "Version" << YAML::Value << 2;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CircleCollider2DComponent>())
        {
            out << YAML::Key << "CircleCollider2DComponent";
            out << YAML::BeginMap;

            auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc2d.Offset;
            out << YAML::Key << "Radius" << YAML::Value << cc2d.Radius;
            out << YAML::Key << "Density" << YAML::Value << cc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << cc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << cc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << cc2d.RestitutionThreshold;
            out << YAML::Key << "IsTrigger" << YAML::Value << cc2d.IsTrigger;
            if (!cc2d.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(cc2d.PhysicsMaterial);
            out << YAML::Key << "Version" << YAML::Value << 2;

            out << YAML::EndMap;
        }

        // 3D Physics Components
        if (entity.HasComponent<RigidbodyComponent>())
        {
            out << YAML::Key << "RigidbodyComponent";
            out << YAML::BeginMap;

            auto& rb = entity.GetComponent<RigidbodyComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb.Type;
            out << YAML::Key << "Mass" << YAML::Value << rb.Mass;
            out << YAML::Key << "LinearDrag" << YAML::Value << rb.LinearDrag;
            out << YAML::Key << "AngularDrag" << YAML::Value << rb.AngularDrag;
            out << YAML::Key << "GravityScale" << YAML::Value << rb.GravityScale;
            out << YAML::Key << "FreezeRotation" << YAML::Value << rb.FreezeRotation;
            out << YAML::Key << "Friction" << YAML::Value << rb.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << rb.Restitution;
            out << YAML::Key << "UseGravity" << YAML::Value << rb.UseGravity;
            out << YAML::Key << "FreezePosition" << YAML::Value << YAML::Flow << YAML::BeginSeq << rb.FreezePositionX << rb.FreezePositionY << rb.FreezePositionZ << YAML::EndSeq;
            out << YAML::Key << "FreezeRotationAxes" << YAML::Value << YAML::Flow << YAML::BeginSeq << rb.FreezeRotationX << rb.FreezeRotationY << rb.FreezeRotationZ << YAML::EndSeq;
            out << YAML::Key << "Interpolate" << YAML::Value << rb.Interpolate;
            out << YAML::Key << "CollisionDetection" << YAML::Value << (int)rb.CollisionDetection;
            out << YAML::Key << "MaxAngularVelocity" << YAML::Value << rb.MaxAngularVelocity;
            out << YAML::Key << "AllowSleep" << YAML::Value << rb.AllowSleep;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxColliderComponent>())
        {
            out << YAML::Key << "BoxColliderComponent";
            out << YAML::BeginMap;

            auto& bc = entity.GetComponent<BoxColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc.Size;
            out << YAML::Key << "IsTrigger" << YAML::Value << bc.IsTrigger;
            if (!bc.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(bc.PhysicsMaterial);
            out << YAML::Key << "Version" << YAML::Value << 2;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SphereColliderComponent>())
        {
            out << YAML::Key << "SphereColliderComponent";
            out << YAML::BeginMap;

            auto& sc = entity.GetComponent<SphereColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << sc.Offset;
            out << YAML::Key << "Radius" << YAML::Value << sc.Radius;
            out << YAML::Key << "IsTrigger" << YAML::Value << sc.IsTrigger;
            if (!sc.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(sc.PhysicsMaterial);
            out << YAML::Key << "Version" << YAML::Value << 2;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CapsuleColliderComponent>())
        {
            out << YAML::Key << "CapsuleColliderComponent";
            out << YAML::BeginMap;

            auto& cc = entity.GetComponent<CapsuleColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc.Offset;
            out << YAML::Key << "Radius" << YAML::Value << cc.Radius;
            out << YAML::Key << "Height" << YAML::Value << cc.Height;
            out << YAML::Key << "Direction" << YAML::Value << cc.Direction;
            out << YAML::Key << "IsTrigger" << YAML::Value << cc.IsTrigger;
            if (!cc.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(cc.PhysicsMaterial);
            out << YAML::Key << "Version" << YAML::Value << 2;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshColliderComponent>())
        {
            out << YAML::Key << "MeshColliderComponent";
            out << YAML::BeginMap;

            auto& mc = entity.GetComponent<MeshColliderComponent>();
            out << YAML::Key << "IsConvex" << YAML::Value << mc.IsConvex;
            out << YAML::Key << "IsTrigger" << YAML::Value << mc.IsTrigger;
            if (!mc.PhysicsMaterial.empty()) out << YAML::Key << "PhysicsMaterial" << YAML::Value << ToSerializablePath(mc.PhysicsMaterial);

            out << YAML::EndMap;
        }

        // Audio Components
        if (entity.HasComponent<AudioSourceComponent>())
        {
            out << YAML::Key << "AudioSourceComponent";
            out << YAML::BeginMap;

            auto& audio = entity.GetComponent<AudioSourceComponent>();
            out << YAML::Key << "FilePath" << YAML::Value << ToSerializablePath(audio.FilePath);
            out << YAML::Key << "PlayOnAwake" << YAML::Value << audio.PlayOnAwake;
            out << YAML::Key << "Loop" << YAML::Value << audio.Loop;
            out << YAML::Key << "Mute" << YAML::Value << audio.Mute;
            out << YAML::Key << "Volume" << YAML::Value << audio.Volume;
            out << YAML::Key << "Pitch" << YAML::Value << audio.Pitch;
            out << YAML::Key << "Pan" << YAML::Value << audio.Pan;
            out << YAML::Key << "Is3D" << YAML::Value << audio.Is3D;
            out << YAML::Key << "MinDistance" << YAML::Value << audio.MinDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << audio.MaxDistance;
            out << YAML::Key << "DopplerLevel" << YAML::Value << audio.DopplerLevel;
            out << YAML::Key << "Attenuation" << YAML::Value << (int)audio.Attenuation;
            out << YAML::Key << "RolloffFactor" << YAML::Value << audio.RolloffFactor;
            out << YAML::Key << "OutputGroup" << YAML::Value << audio.OutputGroup;

            out << YAML::Key << "NextGraphNodeID" << YAML::Value << audio.NextGraphNodeID;
            out << YAML::Key << "NextGraphLinkID" << YAML::Value << audio.NextGraphLinkID;
            out << YAML::Key << "GraphInitialized" << YAML::Value << audio.GraphInitialized;

            out << YAML::Key << "GraphNodes" << YAML::Value << YAML::BeginSeq;
            for (const auto& node : audio.GraphNodes)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "ID" << YAML::Value << node.ID;
                out << YAML::Key << "Type" << YAML::Value << (int)node.Type;
                out << YAML::Key << "EditorPosX" << YAML::Value << node.EditorPosition.x;
                out << YAML::Key << "EditorPosY" << YAML::Value << node.EditorPosition.y;
                out << YAML::Key << "Bypassed" << YAML::Value << node.Bypassed;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::Key << "GraphLinks" << YAML::Value << YAML::BeginSeq;
            for (const auto& link : audio.GraphLinks)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "ID" << YAML::Value << link.ID;
                out << YAML::Key << "From" << YAML::Value << link.FromNodeID;
                out << YAML::Key << "To" << YAML::Value << link.ToNodeID;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioListenerComponent>())
        {
            out << YAML::Key << "AudioListenerComponent";
            out << YAML::BeginMap;

            auto& listener = entity.GetComponent<AudioListenerComponent>();
            out << YAML::Key << "IsActive" << YAML::Value << listener.IsActive;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioEchoFilterComponent>())
        {
            out << YAML::Key << "AudioEchoFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioEchoFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Delay" << YAML::Value << comp.Delay;
            out << YAML::Key << "Decay" << YAML::Value << comp.Decay;
            out << YAML::Key << "WetMix" << YAML::Value << comp.WetMix;
            out << YAML::Key << "DryMix" << YAML::Value << comp.DryMix;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioReverbFilterComponent>())
        {
            out << YAML::Key << "AudioReverbFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioReverbFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "DecayTime" << YAML::Value << comp.DecayTime;
            out << YAML::Key << "Room" << YAML::Value << comp.Room;
            out << YAML::Key << "ReverbLevel" << YAML::Value << comp.ReverbLevel;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioLowPassFilterComponent>())
        {
            out << YAML::Key << "AudioLowPassFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioLowPassFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "CutoffFrequency" << YAML::Value << comp.CutoffFrequency;
            out << YAML::Key << "Resonance" << YAML::Value << comp.Resonance;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioHighPassFilterComponent>())
        {
            out << YAML::Key << "AudioHighPassFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioHighPassFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "CutoffFrequency" << YAML::Value << comp.CutoffFrequency;
            out << YAML::Key << "Resonance" << YAML::Value << comp.Resonance;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioChorusFilterComponent>())
        {
            out << YAML::Key << "AudioChorusFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioChorusFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Delay" << YAML::Value << comp.Delay;
            out << YAML::Key << "Depth" << YAML::Value << comp.Depth;
            out << YAML::Key << "Rate" << YAML::Value << comp.Rate;
            out << YAML::Key << "WetMix" << YAML::Value << comp.WetMix;
            out << YAML::Key << "DryMix" << YAML::Value << comp.DryMix;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioDistortionFilterComponent>())
        {
            out << YAML::Key << "AudioDistortionFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioDistortionFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Level" << YAML::Value << comp.Level;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioGainComponent>())
        {
            out << YAML::Key << "AudioGainComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioGainComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Gain" << YAML::Value << comp.Gain;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioPanComponent>())
        {
            out << YAML::Key << "AudioPanComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioPanComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Pan" << YAML::Value << comp.Pan;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioReverbZoneComponent>())
        {
            out << YAML::Key << "AudioReverbZoneComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioReverbZoneComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "MinDistance" << YAML::Value << comp.MinDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << comp.MaxDistance;
            out << YAML::Key << "Preset" << YAML::Value << (int)comp.Preset;
            out << YAML::EndMap;
        }

        out << YAML::EndMap; // Entity
    }

    std::string SceneSerializer::SerializeEntityToString(Entity entity)
    {
        if (!entity)
            return {};
        YAML::Emitter out;
        SerializeEntity(out, entity);
        return out.c_str();
    }

    void SceneSerializer::Serialize(const std::filesystem::path& filepath)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        
        // Entity'leri kaydet
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        // Ek açılmış / akışla yüklenmiş seviyeler (StreamedLevelComponent kökü ve altı) bu sahneye yazılmaz: kendi
        // .cqscene dosyalarına aittir (Unity'de ek açılan sahne ayrı kaydedilir). Geri al anlık görüntüleri (akış) bunları tutar.
        auto inStreamedLevel = [&](Entity e) {
            for (Entity p = e; p; p = p.GetParent())
                if (p.HasComponent<StreamedLevelComponent>())
                    return true;
            return false;
        };
        for (entt::entity entityID : SerializationOrder(m_Scene->m_Registry))
        {
            Entity entity = { entityID, m_Scene.get() };
            if (!entity || inStreamedLevel(entity))
                continue;

            SerializeEntity(out, entity);
        }

        out << YAML::EndSeq;
        
        // Scene Environment ayarları
        out << YAML::Key << "Environment" << YAML::Value << YAML::BeginMap;
        
        // Skybox
        if (m_Scene->GetSkybox())
        {
            out << YAML::Key << "SkyboxPath" << YAML::Value << ToSerializablePath(m_Scene->GetSkybox()->GetPath());
        }
        out << YAML::Key << "SkyboxExposure" << YAML::Value << m_Scene->GetSkyboxExposure();
        out << YAML::Key << "SkyboxRotation" << YAML::Value << m_Scene->GetSkyboxRotation();
        out << YAML::Key << "SkyboxTint" << YAML::Value << m_Scene->GetSkyboxTint();
        
        // Ambient Light
        out << YAML::Key << "EnvironmentLightingSource" << YAML::Value << (int)m_Scene->GetEnvironmentLightingSource();
        out << YAML::Key << "AmbientColor" << YAML::Value << m_Scene->GetAmbientColor();
        out << YAML::Key << "AmbientSkyColor" << YAML::Value << m_Scene->GetAmbientSkyColor();
        out << YAML::Key << "AmbientEquatorColor" << YAML::Value << m_Scene->GetAmbientEquatorColor();
        out << YAML::Key << "AmbientGroundColor" << YAML::Value << m_Scene->GetAmbientGroundColor();
        out << YAML::Key << "AmbientIntensity" << YAML::Value << m_Scene->GetAmbientIntensity();
        
        // Fog
        out << YAML::Key << "FogEnabled" << YAML::Value << m_Scene->IsFogEnabled();
        out << YAML::Key << "FogColor" << YAML::Value << m_Scene->GetFogColor();
        out << YAML::Key << "FogDensity" << YAML::Value << m_Scene->GetFogDensity();
        out << YAML::Key << "FogStart" << YAML::Value << m_Scene->GetFogStart();
        out << YAML::Key << "FogEnd" << YAML::Value << m_Scene->GetFogEnd();
        
        // Sun Source
        out << YAML::Key << "SunSource" << YAML::Value << m_Scene->GetSunSource();
        
        // Halo/Flare
        out << YAML::Key << "HaloEnabled" << YAML::Value << m_Scene->IsHaloEnabled();
        out << YAML::Key << "HaloStrength" << YAML::Value << m_Scene->GetHaloStrength();
        out << YAML::Key << "FlareEnabled" << YAML::Value << m_Scene->IsFlareEnabled();
        out << YAML::Key << "FlareFadeSpeed" << YAML::Value << m_Scene->GetFlareFadeSpeed();
        out << YAML::Key << "FlareStrength" << YAML::Value << m_Scene->GetFlareStrength();
        
        // Flare Elements
        out << YAML::Key << "FlareElements" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < m_Scene->GetFlareElementCount(); i++)
        {
            auto element = m_Scene->GetFlareElement(i);
            out << YAML::BeginMap;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << element.ColorMultiplier;
            out << YAML::Key << "Size" << YAML::Value << element.Size;
            out << YAML::Key << "Offset" << YAML::Value << element.Offset;
            out << YAML::Key << "Intensity" << YAML::Value << element.Intensity;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        
        // Shadow settings
        out << YAML::Key << "ShadowEnabled" << YAML::Value << m_Scene->IsShadowEnabled();
        out << YAML::Key << "ShadowIntensity" << YAML::Value << m_Scene->GetShadowIntensity();
        
        // Environment Reflections
        out << YAML::Key << "ReflectionSource" << YAML::Value << (int)m_Scene->GetReflectionSource();
        out << YAML::Key << "ReflectionResolution" << YAML::Value << m_Scene->GetReflectionResolution();
        out << YAML::Key << "ReflectionCompression" << YAML::Value << m_Scene->GetReflectionCompression();
        out << YAML::Key << "ReflectionIntensityMultiplier" << YAML::Value << m_Scene->GetReflectionIntensityMultiplier();
        out << YAML::Key << "ReflectionBounces" << YAML::Value << m_Scene->GetReflectionBounces();
        
        // Baked Lightmap
        out << YAML::Key << "BakedLightmapPath" << YAML::Value << m_Scene->GetBakedLightmapPath();
        out << YAML::Key << "LightmapResolution" << YAML::Value << m_Scene->GetLightmapResolution();
        out << YAML::Key << "MaxLightmapSize" << YAML::Value << m_Scene->GetMaxLightmapSize();
        out << YAML::Key << "MaxBounces" << YAML::Value << m_Scene->GetMaxBounces();
        out << YAML::Key << "DirectSamples" << YAML::Value << m_Scene->GetDirectSamples();
        out << YAML::Key << "IndirectSamples" << YAML::Value << m_Scene->GetIndirectSamples();
        out << YAML::Key << "AmbientOcclusion" << YAML::Value << m_Scene->IsAmbientOcclusion();
        out << YAML::Key << "LightmapFiltering" << YAML::Value << m_Scene->GetFiltering();
        
        SerializeRenderSettings(out, m_Scene->GetRenderSettings());

        out << YAML::EndMap; // Environment
        
        // Editor Camera kaydet
        if (m_EditorCamera)
        {
            out << YAML::Key << "EditorCamera" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "CameraMode" << YAML::Value << (int)m_EditorCamera->GetCameraMode();
            out << YAML::Key << "Position" << YAML::Value << m_EditorCamera->GetPosition();
            out << YAML::Key << "Distance" << YAML::Value << m_EditorCamera->GetDistance();
            out << YAML::Key << "Pitch" << YAML::Value << m_EditorCamera->GetPitch();
            out << YAML::Key << "Yaw" << YAML::Value << m_EditorCamera->GetYaw();
            out << YAML::Key << "OrthographicSize" << YAML::Value << m_EditorCamera->GetOrthographicSize();
            out << YAML::EndMap;
        }
        
        // Hierarchy (parent-child ilişkileri) kaydet
        out << YAML::Key << "Hierarchy" << YAML::Value << YAML::BeginSeq;
        
        // Çocuk sırası korunur (ebeveyn başına vektör sırası); ebeveynler varlık listesi sırasıyla yazılır, böylece aynı
        // sahne her kayıtta aynı metni verir (eskiden karma tablo sırasıydı: kaydedilmemiş değişiklik yanlış algılanıyordu)
        for (entt::entity parent : SerializationOrder(m_Scene->m_Registry))
        {
        auto childrenIt = m_Scene->m_EntityChildren.find(parent);
        if (childrenIt == m_Scene->m_EntityChildren.end())
            continue;
        for (auto child : childrenIt->second)
        {
            Entity childEntity = { child, m_Scene.get() };
            Entity parentEntity = { parent, m_Scene.get() };
            
            if (childEntity && parentEntity && m_Scene->m_Registry.valid(child) && m_Scene->m_Registry.valid(parent) &&
                !inStreamedLevel(childEntity))
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Child" << YAML::Value << childEntity.GetComponent<IDComponent>().ID;
                out << YAML::Key << "Parent" << YAML::Value << parentEntity.GetComponent<IDComponent>().ID;
                out << YAML::EndMap;
            }
        }
        }
        
        out << YAML::EndSeq;

        // NavMesh sahneyle kaydedilmez: NavMesh Surface "Bake On Play" ile oyun başında (önbellekli) pişirilir

        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        fout.close();

        CQ_CORE_INFO("Scene serialized: {0}", filepath.string());
    }

    void SceneSerializer::SerializeRuntime(const std::filesystem::path& filepath)
    {
        // Runtime serialization için aynı format
        Serialize(filepath);
    }

    void SceneSerializer::SerializeToStream(std::ostream& stream)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        
        // Entity'leri kaydet
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        for (entt::entity entityID : SerializationOrder(m_Scene->m_Registry))
        {
            Entity entity = { entityID, m_Scene.get() };
            if (!entity)
                continue;

            SerializeEntity(out, entity);
        }

        out << YAML::EndSeq;
        
        // Scene Environment ayarları
        out << YAML::Key << "Environment" << YAML::Value << YAML::BeginMap;
        
        // Skybox
        if (m_Scene->GetSkybox())
        {
            out << YAML::Key << "SkyboxPath" << YAML::Value << ToSerializablePath(m_Scene->GetSkybox()->GetPath());
        }
        out << YAML::Key << "SkyboxExposure" << YAML::Value << m_Scene->GetSkyboxExposure();
        out << YAML::Key << "SkyboxRotation" << YAML::Value << m_Scene->GetSkyboxRotation();
        out << YAML::Key << "SkyboxTint" << YAML::Value << m_Scene->GetSkyboxTint();
        
        // Ambient Light
        out << YAML::Key << "EnvironmentLightingSource" << YAML::Value << (int)m_Scene->GetEnvironmentLightingSource();
        out << YAML::Key << "AmbientColor" << YAML::Value << m_Scene->GetAmbientColor();
        out << YAML::Key << "AmbientSkyColor" << YAML::Value << m_Scene->GetAmbientSkyColor();
        out << YAML::Key << "AmbientEquatorColor" << YAML::Value << m_Scene->GetAmbientEquatorColor();
        out << YAML::Key << "AmbientGroundColor" << YAML::Value << m_Scene->GetAmbientGroundColor();
        out << YAML::Key << "AmbientIntensity" << YAML::Value << m_Scene->GetAmbientIntensity();
        
        // Fog
        out << YAML::Key << "FogEnabled" << YAML::Value << m_Scene->IsFogEnabled();
        out << YAML::Key << "FogColor" << YAML::Value << m_Scene->GetFogColor();
        out << YAML::Key << "FogDensity" << YAML::Value << m_Scene->GetFogDensity();
        out << YAML::Key << "FogStart" << YAML::Value << m_Scene->GetFogStart();
        out << YAML::Key << "FogEnd" << YAML::Value << m_Scene->GetFogEnd();
        
        // Sun Source
        out << YAML::Key << "SunSource" << YAML::Value << m_Scene->GetSunSource();
        
        // Halo/Flare
        out << YAML::Key << "HaloEnabled" << YAML::Value << m_Scene->IsHaloEnabled();
        out << YAML::Key << "HaloStrength" << YAML::Value << m_Scene->GetHaloStrength();
        out << YAML::Key << "FlareEnabled" << YAML::Value << m_Scene->IsFlareEnabled();
        out << YAML::Key << "FlareFadeSpeed" << YAML::Value << m_Scene->GetFlareFadeSpeed();
        out << YAML::Key << "FlareStrength" << YAML::Value << m_Scene->GetFlareStrength();
        
        // Flare Elements
        out << YAML::Key << "FlareElements" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < m_Scene->GetFlareElementCount(); i++)
        {
            auto element = m_Scene->GetFlareElement(i);
            out << YAML::BeginMap;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << element.ColorMultiplier;
            out << YAML::Key << "Size" << YAML::Value << element.Size;
            out << YAML::Key << "Offset" << YAML::Value << element.Offset;
            out << YAML::Key << "Intensity" << YAML::Value << element.Intensity;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        
        // Shadow settings
        out << YAML::Key << "ShadowEnabled" << YAML::Value << m_Scene->IsShadowEnabled();
        out << YAML::Key << "ShadowIntensity" << YAML::Value << m_Scene->GetShadowIntensity();
        
        // Environment Reflections
        out << YAML::Key << "ReflectionSource" << YAML::Value << (int)m_Scene->GetReflectionSource();
        out << YAML::Key << "ReflectionResolution" << YAML::Value << m_Scene->GetReflectionResolution();
        out << YAML::Key << "ReflectionCompression" << YAML::Value << m_Scene->GetReflectionCompression();
        out << YAML::Key << "ReflectionIntensityMultiplier" << YAML::Value << m_Scene->GetReflectionIntensityMultiplier();
        out << YAML::Key << "ReflectionBounces" << YAML::Value << m_Scene->GetReflectionBounces();
        
        SerializeRenderSettings(out, m_Scene->GetRenderSettings());

        out << YAML::EndMap; // Environment
        
        // Hierarchy (parent-child ilişkileri) kaydet
        out << YAML::Key << "Hierarchy" << YAML::Value << YAML::BeginSeq;
        
        // Çocuk sırası korunur (ebeveyn başına vektör sırası); ebeveynler varlık listesi sırasıyla yazılır, böylece aynı
        // sahne her kayıtta aynı metni verir (eskiden karma tablo sırasıydı: kaydedilmemiş değişiklik yanlış algılanıyordu)
        for (entt::entity parent : SerializationOrder(m_Scene->m_Registry))
        {
        auto childrenIt = m_Scene->m_EntityChildren.find(parent);
        if (childrenIt == m_Scene->m_EntityChildren.end())
            continue;
        for (auto child : childrenIt->second)
        {
            Entity childEntity = { child, m_Scene.get() };
            Entity parentEntity = { parent, m_Scene.get() };
            
            if (childEntity && parentEntity && m_Scene->m_Registry.valid(child) && m_Scene->m_Registry.valid(parent))
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Child" << YAML::Value << childEntity.GetComponent<IDComponent>().ID;
                out << YAML::Key << "Parent" << YAML::Value << parentEntity.GetComponent<IDComponent>().ID;
                out << YAML::EndMap;
            }
        }
        }
        
        out << YAML::EndSeq;
        out << YAML::EndMap;

        stream << out.c_str();
    }

    bool SceneSerializer::DeserializeFromStream(std::istream& stream)
    {
        try
        {
            return DeserializeFromStreamImpl(stream);
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load scene: {0}", e.what());
            return false;
        }
    }

    bool SceneSerializer::DeserializeFromStreamImpl(std::istream& stream)
    {
        std::stringstream buffer;
        buffer << stream.rdbuf();
        std::string yamlStr = buffer.str();
        
        YAML::Node data;
        try
        {
            data = YAML::Load(yamlStr);
        }
        catch (const YAML::ParserException& e)
        {
            CQ_CORE_ERROR("Failed to load scene from stream: {0}", e.what());
            return false;
        }

        if (!data["Scene"])
            return false;

        std::string sceneName = data["Scene"].as<std::string>();
        CQ_CORE_INFO("Deserializing scene '{0}'", sceneName);

        auto entities = data["Entities"];
        if (entities)
        {
            for (auto entity : entities)
            {
                // Bozuk/eksik bir alan tüm sahneyi düşürmesin: entity bazında koruma
                try
                {
                uint64_t uuid = entity["Entity"].as<uint64_t>();

                std::string name;
                auto tagComponent = entity["TagComponent"];
                if (tagComponent)
                    name = tagComponent["Tag"].as<std::string>();

                CQ_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

                Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

                if (tagComponent)
                {
                    auto& tag = deserializedEntity.GetComponent<TagComponent>();
                    tag.GameTag = tagComponent["GameTag"].as<std::string>();
                    if (tagComponent["IsStatic"])
                        tag.IsStatic = tagComponent["IsStatic"].as<bool>();
                }

                auto activeComponent = entity["ActiveComponent"];
                if (activeComponent && activeComponent["Active"])
                {
                    auto& ac = deserializedEntity.AddComponent<ActiveComponent>();
                    ac.Active = activeComponent["Active"].as<bool>();
                }

                auto transformComponent = entity["TransformComponent"];
                if (transformComponent)
                {
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    tc.Position = transformComponent["Position"].as<glm::vec3>();
                    tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                    tc.Scale = transformComponent["Scale"].as<glm::vec3>();
                }

                auto cameraComponent = entity["CameraComponent"];
                if (cameraComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CameraComponent>();

                    const YAML::Node& cameraProps = cameraComponent["Camera"];
                    cc.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());

                    cc.Camera.SetPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>());
                    cc.Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());
                    cc.Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());

                    cc.Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());
                    cc.Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());
                    cc.Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());

                    cc.Primary = cameraComponent["Primary"].as<bool>();
                    cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
                    if (auto n = cameraComponent["Background"])
                        cc.Background = (CameraComponent::BackgroundMode)std::clamp(n.as<int>(), 0, 2);
                    if (auto n = cameraComponent["BackgroundColor"])
                        cc.BackgroundColor = n.as<glm::vec4>();
                }

                auto spriteRendererComponent = entity["SpriteRendererComponent"];
                if (spriteRendererComponent)
                {
                    auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                    src.Color = spriteRendererComponent["Color"].as<glm::vec4>();
                    src.TilingFactor = spriteRendererComponent["TilingFactor"].as<float>();
                    
                    if (spriteRendererComponent["TexturePath"])
                    {
                        std::string texturePath = ResolveSerializablePath(spriteRendererComponent["TexturePath"].as<std::string>());
                        if (!texturePath.empty())
                        {
                            src.Texture = Texture2D::Create(texturePath);
                        }
                    }
                    if (auto n = spriteRendererComponent["SpriteName"]) src.SpriteName = n.as<std::string>();
                    if (auto n = spriteRendererComponent["Rect"]) src.Rect = n.as<glm::vec4>();
                    if (auto n = spriteRendererComponent["Pivot"]) src.Pivot = n.as<glm::vec2>();
                    if (auto n = spriteRendererComponent["Border"]) src.Border = n.as<glm::vec4>();
                    if (auto n = spriteRendererComponent["PixelsPerUnit"]) src.PixelsPerUnit = n.as<float>();
                    if (auto n = spriteRendererComponent["FlipX"]) src.FlipX = n.as<bool>();
                    if (auto n = spriteRendererComponent["FlipY"]) src.FlipY = n.as<bool>();
                    if (auto n = spriteRendererComponent["DrawMode"]) src.DrawMode = (SpriteDrawMode)n.as<int>();
                    if (auto n = spriteRendererComponent["Size"]) src.Size = n.as<glm::vec2>();
                    if (auto n = spriteRendererComponent["MaskInteraction"]) src.MaskInteraction = (SpriteMaskInteraction)n.as<int>();
                }

                auto layerComponent = entity["LayerComponent"];
                if (layerComponent)
                {
                    auto& lc = deserializedEntity.GetComponent<LayerComponent>();
                    lc.Layer = layerComponent["Layer"].as<int32_t>();
                    lc.OrderInLayer = layerComponent["OrderInLayer"].as<int32_t>();
                }

                auto textRendererComponent = entity["TextRendererComponent"];
                if (textRendererComponent)
                {
                    auto& trc = deserializedEntity.AddComponent<TextRendererComponent>();
                    trc.Text = textRendererComponent["Text"].as<std::string>();
                    trc.Color = textRendererComponent["Color"].as<glm::vec4>();
                    trc.FontSize = textRendererComponent["FontSize"].as<float>();
                    if (auto al = textRendererComponent["Alignment"])
                        trc.Alignment = (TextAlignment)std::clamp(al.as<int>(), 0, 2);
                }

                if (auto n = entity["ReflectionProbeComponent"])
                {
                    auto& comp = deserializedEntity.AddComponent<ReflectionProbeComponent>();
                    if (n["BoxOffset"]) comp.BoxOffset = n["BoxOffset"].as<glm::vec3>();
                    if (n["BoxSize"]) comp.BoxSize = n["BoxSize"].as<glm::vec3>();
                    comp.Resolution = n["Resolution"].as<uint32_t>(128);
                    comp.Intensity = n["Intensity"].as<float>(1.0f);
                    comp.Bounces = n["Bounces"].as<int>(1);
                    comp.RealtimeUpdate = n["RealtimeUpdate"].as<bool>(false);
                    comp.BlendDistance = n["BlendDistance"].as<float>(1.0f);
                    comp.UpdateInterval = n["UpdateInterval"].as<float>(0.5f);
                }

                if (auto n = entity["AdaptiveProbeVolumeComponent"])
                {
                    auto& comp = deserializedEntity.AddComponent<AdaptiveProbeVolumeComponent>();
                    if (n["ProbeOffset"]) comp.ProbeOffset = n["ProbeOffset"].as<glm::vec3>();
                    comp.MinSpacing = n["MinSpacing"].as<float>(1.0f);
                    comp.MaxSpacing = n["MaxSpacing"].as<float>(9.0f);
                    comp.BakingMode = n["BakingMode"].as<int>(0);
                    comp.Dilation = n["Dilation"].as<bool>(false);
                    comp.VirtualOffset = n["VirtualOffset"].as<bool>(true);
                    comp.ValidityThreshold = n["ValidityThreshold"].as<float>(0.75f);
                    comp.SearchDistanceMultiplier = n["SearchDistanceMultiplier"].as<float>(0.2f);
                    comp.GeometryBias = n["GeometryBias"].as<float>(0.01f);
                    comp.RayOriginBias = n["RayOriginBias"].as<float>(-0.001f);
                }

                if (auto n = entity["ParticleSystemComponent"])
                    DeserializeParticleSystem(n, deserializedEntity.AddComponent<ParticleSystemComponent>());
                ComponentRegistry::DeserializeEntity(entity, deserializedEntity);
                DeserializeUI(entity, deserializedEntity);

                if (auto n = entity["TerrainComponent"])
                {
                    TerrainComponent t;
                    t.Width = n["Width"].as<float>(100.0f);
                    t.Length = n["Length"].as<float>(100.0f);
                    t.Height = n["Height"].as<float>(30.0f);
                    t.Resolution = n["Resolution"].as<int>(129);
                    t.SplatResolution = n["SplatResolution"].as<int>(256);
                    t.LayerCount = n["LayerCount"].as<int>(1);
                    t.CastShadows = n["CastShadows"].as<bool>(true);
                    t.ReceiveShadows = n["ReceiveShadows"].as<bool>(true);
                    t.EnableCollider = n["EnableCollider"].as<bool>(true);
                    int li = 0;
                    for (const auto& ln : n["Layers"])
                    {
                        if (li >= TerrainComponent::MaxLayers) break;
                        auto& l = t.Layers[(size_t)li++];
                        l.TexturePath = ResolveSerializablePath(ln["Texture"].as<std::string>(""));
                        if (ln["Tint"]) l.Tint = ln["Tint"].as<glm::vec3>();
                        l.Tiling = ln["Tiling"].as<float>(12.0f);
                        l.Roughness = ln["Roughness"].as<float>(0.85f);
                        l.Metallic = ln["Metallic"].as<float>(0.0f);
                        if (ln["Asset"]) l.Asset = ResolveSerializablePath(ln["Asset"].as<std::string>());
                    }
                    if (!n["Heights"] || !TerrainSystem::DecodeHeights(t, n["Heights"].as<std::string>("")))
                        TerrainSystem::Initialize(t, t.Resolution);
                    if (!n["Splat"] || !TerrainSystem::DecodeSplat(t, n["Splat"].as<std::string>("")))
                    {
                        t.Splat.clear();
                        TerrainSystem::EnsureData(t);
                    }
                    deserializedEntity.AddComponent<TerrainComponent>(t);
                }

                auto meshRendererComponent = entity["MeshRendererComponent"];
                if (meshRendererComponent)
                {
                    auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                    const YAML::Node& n = meshRendererComponent;
                    if (n["MeshType"])
                        mrc.Type = static_cast<MeshType>(n["MeshType"].as<int>());
                    if (auto v = n["CastShadows"]) mrc.CastShadows = v.as<bool>();
                    if (auto v = n["ReceiveShadows"]) mrc.ReceiveShadows = v.as<bool>();
                    if (auto v = n["RealtimeGI"]) mrc.RealtimeGI = v.as<bool>();
                    if (auto v = n["LightmapParameters"]) mrc.LightmapParameters = ResolveSerializablePath(v.as<std::string>());
                    if (auto v = n["Tint"]) mrc.Tint = v.as<glm::vec3>();
                    if (auto v = n["Model"])
                    {
                        mrc.ModelPath = ResolveSerializablePath(v.as<std::string>());
                        if (!mrc.ModelPath.empty())
                        {
                            mrc.ModelData = ModelLoader::Load(mrc.ModelPath);
                            if (!mrc.ModelData)
                                CQ_CORE_WARN("Deserialize: Failed to load model: {0}", mrc.ModelPath);
                        }
                    }

                    // Eski biçim: renk / doku / ışıma ... Mesh Renderer'daydı -> malzemeye taşınır (görünüm aynı kalır)
                    MeshRendererMaterial::LegacyFields legacy;
                    const bool hasLegacy = n["Color"] || n["TexturePath"];
                    if (hasLegacy)
                    {
                        if (auto v = n["Color"]) legacy.Color = v.as<glm::vec4>();
                        if (auto v = n["Metallic"]) legacy.Metallic = v.as<float>();
                        if (auto v = n["Roughness"]) legacy.Roughness = v.as<float>();
                        if (auto v = n["AO"]) legacy.AO = v.as<float>();
                        if (auto v = n["EmissionColor"]) legacy.EmissionColor = v.as<glm::vec3>();
                        if (auto v = n["EmissionStrength"]) legacy.EmissionStrength = v.as<float>();
                        if (auto v = n["EmissionFromTexture"]) legacy.EmissionFromTexture = v.as<bool>();
                        if (auto v = n["Tiling"]) legacy.Tiling = v.as<glm::vec2>();
                        if (auto v = n["Offset"]) legacy.Offset = v.as<glm::vec2>();
                        if (auto v = n["WorldAlignedUV"]) legacy.WorldAlignedUV = v.as<bool>();
                        if (auto v = n["AlignToObject"]) legacy.AlignToObject = v.as<bool>();
                        if (auto v = n["Unlit"]) legacy.Unlit = v.as<bool>();
                        if (auto v = n["AlphaClip"]) legacy.AlphaClip = v.as<bool>();
                        if (auto v = n["AlphaClipThreshold"]) legacy.AlphaClipThreshold = v.as<float>();
                        auto loadTex = [](const YAML::Node& node) -> std::shared_ptr<Texture2D> {
                            if (!node) return nullptr;
                            const std::string path = ResolveSerializablePath(node.as<std::string>());
                            return path.empty() ? nullptr : Texture2D::Create(path);
                        };
                        legacy.Texture = loadTex(n["TexturePath"]);
                        legacy.NormalMap = loadTex(n["NormalMapPath"]);
                        legacy.MaskMap = loadTex(n["MaskMapPath"]);
                    }

                    mrc.MaterialInstance = nullptr;
                    mrc.MaterialPath.clear();
                    if (auto assetNode = n["MaterialAsset"])
                    {
                        mrc.MaterialPath = ResolveSerializablePath(assetNode.as<std::string>());
                        mrc.MaterialInstance = MaterialAsset::Load(mrc.MaterialPath);
                        if (!mrc.MaterialInstance)
                        {
                            CQ_CORE_WARN("Material asset not found: {0}", mrc.MaterialPath);
                            mrc.MaterialPath.clear();
                        }
                    }
                    else if (auto materialNode = n["Material"])
                    {
                        mrc.MaterialInstance = Material::CreateDefault();
                        ReadMaterial(materialNode, *mrc.MaterialInstance);
                        // Daha eski biçim: dokular hâlâ Mesh Renderer'daydı
                        if (!materialNode["RenderMode"] && hasLegacy)
                        {
                            auto& m = *mrc.MaterialInstance;
                            if (!m.AlbedoMap) m.AlbedoMap = legacy.Texture;
                            if (!m.NormalMap) m.NormalMap = legacy.NormalMap;
                            if (legacy.MaskMap && !m.AOMap)
                            {
                                m.AOMap = m.RoughnessMap = m.MetallicMap = legacy.MaskMap;
                                m.RoughnessChannel = 1;
                                m.MetallicChannel = 2;
                                m.MapsScaleFactors = true;
                            }
                        }
                    }
                    else if (hasLegacy)
                    {
                        // Eski varsayılan (beyaz + dama, başka ayar yok) = paketteki Default Grid malzemesi
                        const std::string texPath = legacy.Texture ? ToSerializablePath(legacy.Texture->GetPath()) : std::string();
                        const bool defaultGrid = legacy.Color == glm::vec4(1.0f) && legacy.Metallic == 0.0f && legacy.Roughness == 0.5f &&
                                                 legacy.AO == 1.0f && legacy.EmissionStrength == 0.0f && legacy.Tiling == glm::vec2(1.0f) &&
                                                 legacy.Offset == glm::vec2(0.0f) && !legacy.WorldAlignedUV && !legacy.NormalMap && !legacy.MaskMap &&
                                                 !legacy.Unlit && !legacy.AlphaClip &&
                                                 (texPath == "CQ:Resources/Textures/texture_grid.png" || texPath == "CQ:Resources/Packages/Core/Textures/Grid.png");
                        if (defaultGrid && MaterialAsset::DefaultGrid())
                        {
                            mrc.MaterialPath = MaterialAsset::DefaultGridPath();
                            mrc.MaterialInstance = MaterialAsset::DefaultGrid();
                        }
                        else
                        {
                            mrc.MaterialInstance = Material::CreateDefault();
                            mrc.MaterialInstance->Name = deserializedEntity.GetComponent<TagComponent>().Tag + " (Instance)";
                            MeshRendererMaterial::Apply(*mrc.MaterialInstance, legacy);
                        }
                    }
                    else
                    {
                        // Ne malzeme ne eski alan: varsayılan malzeme (eklemede atanan)
                        mrc.MaterialPath = MaterialAsset::DefaultGridPath();
                        mrc.MaterialInstance = MaterialAsset::DefaultGrid();
                        if (!mrc.MaterialInstance)
                            mrc.MaterialPath.clear();
                    }
                }

                auto directionalLightComponent = entity["DirectionalLightComponent"];
                if (directionalLightComponent)
                {
                    auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
                    dlc.Direction = directionalLightComponent["Direction"].as<glm::vec3>();
                    // Eski sahne: yön ayrı alandaydı -> nesne dönüşüne çevir (ışık görünümü korunur)
                    if (!directionalLightComponent["FromRotation"] && directionalLightComponent["Direction"])
                        deserializedEntity.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(dlc.Direction);
                    dlc.Color = directionalLightComponent["Color"].as<glm::vec3>();
                    dlc.Intensity = directionalLightComponent["Intensity"].as<float>();
                    dlc.CastShadows = directionalLightComponent["CastShadows"].as<bool>();
                    if (directionalLightComponent["Mode"])
                        dlc.Mode = (LightMode)directionalLightComponent["Mode"].as<int>();
                }

                auto pointLightComponent = entity["PointLightComponent"];
                if (pointLightComponent)
                {
                    auto& plc = deserializedEntity.AddComponent<PointLightComponent>();
                    plc.Color = pointLightComponent["Color"].as<glm::vec3>();
                    plc.Intensity = pointLightComponent["Intensity"].as<float>();
                    plc.Range = pointLightComponent["Range"].as<float>();
                    plc.Constant = pointLightComponent["Constant"].as<float>();
                    plc.Linear = pointLightComponent["Linear"].as<float>();
                    plc.Quadratic = pointLightComponent["Quadratic"].as<float>();
                    plc.CastShadows = pointLightComponent["CastShadows"].as<bool>();
                    if (pointLightComponent["Mode"])
                        plc.Mode = (LightMode)pointLightComponent["Mode"].as<int>();
                }

                auto spotLightComponent = entity["SpotLightComponent"];
                if (spotLightComponent)
                {
                    auto& slc = deserializedEntity.AddComponent<SpotLightComponent>();
                    slc.Direction = spotLightComponent["Direction"].as<glm::vec3>();
                    if (!spotLightComponent["FromRotation"])
                        deserializedEntity.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(slc.Direction);
                    slc.Color = spotLightComponent["Color"].as<glm::vec3>();
                    slc.Intensity = spotLightComponent["Intensity"].as<float>();
                    slc.Range = spotLightComponent["Range"].as<float>();
                    slc.InnerConeAngle = spotLightComponent["InnerConeAngle"].as<float>();
                    slc.OuterConeAngle = spotLightComponent["OuterConeAngle"].as<float>();
                    slc.Constant = spotLightComponent["Constant"].as<float>();
                    slc.Linear = spotLightComponent["Linear"].as<float>();
                    slc.Quadratic = spotLightComponent["Quadratic"].as<float>();
                    slc.CastShadows = spotLightComponent["CastShadows"].as<bool>();
                    if (spotLightComponent["Mode"])
                        slc.Mode = (LightMode)spotLightComponent["Mode"].as<int>();
                }

                // Eski "Model" bileşeni Mesh Renderer'a katıldı: model dosyası Mesh alanına yüklenir
                auto modelComponent = entity["ModelComponent"];
                if (modelComponent)
                {
                    auto& mc = deserializedEntity.HasComponent<MeshRendererComponent>() ? deserializedEntity.GetComponent<MeshRendererComponent>()
                                                                                         : deserializedEntity.AddComponent<MeshRendererComponent>();
                    if (modelComponent["FilePath"])
                        mc.ModelPath = ResolveSerializablePath(modelComponent["FilePath"].as<std::string>());
                    if (auto t = modelComponent["Tint"]) mc.Tint = t.as<glm::vec3>();
                    if (auto cs = modelComponent["CastShadows"]) mc.CastShadows = cs.as<bool>();
                    if (auto gi = modelComponent["RealtimeGI"]) mc.RealtimeGI = gi.as<bool>();
                    mc.MaterialInstance = nullptr;
                    mc.MaterialPath.clear();
                    if (!mc.ModelPath.empty())
                    {
                        mc.ModelData = ModelLoader::Load(mc.ModelPath);
                        if (!mc.ModelData)
                            CQ_CORE_WARN("Deserialize: Failed to load model (Play/runtime): {0}", mc.ModelPath);
                    }
                }

                auto animatorComponent = entity["AnimatorComponent"];
                if (animatorComponent)
                {
                    auto& ac = deserializedEntity.AddComponent<AnimatorComponent>();
                    if (animatorComponent["Playing"])
                        ac.Playing = animatorComponent["Playing"].as<bool>();
                    if (animatorComponent["Speed"])
                        ac.Speed = animatorComponent["Speed"].as<float>();
                    if (animatorComponent["ActiveClipIndex"])
                        ac.ActiveClipIndex = animatorComponent["ActiveClipIndex"].as<int>();
                    if (animatorComponent["CurrentTime"])
                        ac.CurrentTime = animatorComponent["CurrentTime"].as<float>();
                }

                auto animationComponent = entity["AnimationComponent"];
                if (animationComponent)
                {
                    auto& ac = deserializedEntity.AddComponent<AnimationComponent>();
                    if (animationComponent["ControllerPath"])
                        ac.ControllerFilePath = ResolveSerializablePath(animationComponent["ControllerPath"].as<std::string>());
                    if (animationComponent["Speed"])
                        ac.Speed = animationComponent["Speed"].as<float>();
                    if (animationComponent["PlayOnAwake"])
                        ac.PlayOnAwake = animationComponent["PlayOnAwake"].as<bool>();

                    if (!ac.ControllerFilePath.empty())
                    {
                        ac.Controller = AnimationController::Deserialize(ac.ControllerFilePath);
                        if (ac.Controller && !ac.Controller->Layers.empty())
                        {
                            auto& layer = ac.Controller->Layers[0];
                            ac.CurrentStateName = layer.DefaultState;

                            for (const auto& param : ac.Controller->Parameters)
                            {
                                AnimParameterRuntimeValue rtParam;
                                rtParam.Name = param.Name;
                                rtParam.Type = param.Type;
                                rtParam.FloatValue = param.DefaultValue;
                                rtParam.BoolValue = param.DefaultValue > 0.5f;
                                rtParam.IntValue = static_cast<int>(param.DefaultValue);
                                ac.Parameters.push_back(rtParam);
                            }
                        }
                    }

                    auto paramsNode = animationComponent["Parameters"];
                    if (paramsNode)
                    {
                        for (auto paramNode : paramsNode)
                        {
                            std::string name = paramNode["Name"].as<std::string>();
                            for (auto& rtParam : ac.Parameters)
                            {
                                if (rtParam.Name == name)
                                {
                                    if (paramNode["FloatValue"])
                                        rtParam.FloatValue = paramNode["FloatValue"].as<float>();
                                    if (paramNode["BoolValue"])
                                        rtParam.BoolValue = paramNode["BoolValue"].as<bool>();
                                    if (paramNode["IntValue"])
                                        rtParam.IntValue = paramNode["IntValue"].as<int>();
                                    break;
                                }
                            }
                        }
                    }
                }

                for (auto component : entity)
                {
                    std::string compName = component.first.as<std::string>();
                    
                    if (compName.find("_NativeScriptComponent") != std::string::npos || compName == "NativeScriptComponent")
                    {
                        auto nativeScriptComponent = component.second;
                        auto& sc = deserializedEntity.HasComponent<NativeScriptComponent>() ? 
                                   deserializedEntity.GetComponent<NativeScriptComponent>() : 
                                   deserializedEntity.AddComponent<NativeScriptComponent>();
                        
                        NativeScriptData data;
                        data.ModuleName = nativeScriptComponent["ModuleName"].as<std::string>();
                        data.ClassName = nativeScriptComponent["ClassName"].as<std::string>();
                        
                        if (nativeScriptComponent["ScriptPath"])
                            data.ScriptPath = ResolveSerializablePath(nativeScriptComponent["ScriptPath"].as<std::string>());
                            
                        // .cpp kaynağı: Play başında NativeScriptCompiler derler ve bağlar (burada DLL yüklenmez)
                        if (NativeScriptCompiler::IsSource(data.ScriptPath))
                        {
                            if (data.ModuleName.empty())
                                data.ModuleName = std::filesystem::path(data.ScriptPath).stem().string();
                            sc.Scripts.push_back(data);
                            continue;
                        }
                        if (data.ClassName.empty() && !data.ModuleName.empty())
                        {
                            if (!data.ScriptPath.empty() && !ScriptEngine::IsModuleLoaded(data.ModuleName))
                                ScriptEngine::LoadModule(data.ModuleName, data.ScriptPath);

                            auto classNames = ScriptEngine::GetModuleClassNames(data.ModuleName);
                            data.ClassName = classNames.empty() ? data.ModuleName : classNames.front();
                        }
                        
                        if (!data.ScriptPath.empty() && !ScriptEngine::IsModuleLoaded(data.ModuleName))
                        {
                            ScriptEngine::LoadModule(data.ModuleName, data.ScriptPath);
                        }
                        
                        if (!data.ModuleName.empty() && !data.ClassName.empty() && ScriptEngine::IsModuleLoaded(data.ModuleName))
                        {
                            data.InstantiateScript = [moduleName = data.ModuleName, className = data.ClassName]() {
                                return ScriptEngine::CreateScriptInstance(moduleName, className);
                            };
                        }
                        
                        sc.Scripts.push_back(data);
                    }
                    else if (compName.find("_ConquerorScriptComponent") != std::string::npos || compName == "ConquerorScriptComponent")
                    {
                        auto conquerorScriptComponent = component.second;
                        auto& sc = deserializedEntity.HasComponent<ConquerorScriptComponent>() ? 
                                   deserializedEntity.GetComponent<ConquerorScriptComponent>() : 
                                   deserializedEntity.AddComponent<ConquerorScriptComponent>();
                        
                        ConquerorScriptData data;
                        data.ScriptPath = ResolveSerializablePath(conquerorScriptComponent["ScriptPath"].as<std::string>());
                        data.ClassName = conquerorScriptComponent["ClassName"].as<std::string>();
                        if (auto fields = conquerorScriptComponent["Fields"]; fields && fields.IsMap())
                        {
                            for (auto it = fields.begin(); it != fields.end(); ++it)
                            {
                                ScriptFieldOverride v;
                                const YAML::Node& n = it->second;
                                if (n["Bool"]) { v.Type = ScriptFieldOverride::Kind::Bool; v.Bool = n["Bool"].as<bool>(); }
                                else if (n["Int"]) { v.Type = ScriptFieldOverride::Kind::Int; v.Int = n["Int"].as<int64_t>(); }
                                else if (n["Float"]) { v.Type = ScriptFieldOverride::Kind::Float; v.Float = n["Float"].as<double>(); }
                                else if (n["String"]) { v.Type = ScriptFieldOverride::Kind::String; v.String = n["String"].as<std::string>(); }
                                data.FieldOverrides[it->first.as<std::string>()] = v;
                            }
                        }
                        
                        sc.Scripts.push_back(data);
                    }
                }

                // Eski Image bileşeni kaldırıldı (Sprite Renderer ile aynı işi yapıyordu): eski sahnelerde Sprite Renderer olarak
                // yüklenir (renk, doku, döşeme); varlıkta zaten Sprite Renderer varsa ona dokunulmaz
                auto imageComponent = entity["ImageComponent"];
                if (imageComponent && !deserializedEntity.HasComponent<SpriteRendererComponent>())
                {
                    auto& sr = deserializedEntity.AddComponent<SpriteRendererComponent>();
                    if (imageComponent["Color"])
                        sr.Color = imageComponent["Color"].as<glm::vec4>();
                    if (imageComponent["TilingFactor"])
                        sr.TilingFactor = imageComponent["TilingFactor"].as<float>();
                    if (imageComponent["TexturePath"])
                    {
                        std::string texturePath = ResolveSerializablePath(imageComponent["TexturePath"].as<std::string>());
                        if (!texturePath.empty())
                            sr.Texture = Texture2D::Create(texturePath);
                    }
                }

                auto buttonComponent = entity["ButtonComponent"];
                if (buttonComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<ButtonComponent>();
                    bc.Interactable = buttonComponent["Interactable"].as<bool>();
                    bc.Navigation = (ButtonComponent::NavigationMode)buttonComponent["Navigation"].as<int>();
                    bc.Use9Slice = buttonComponent["Use9Slice"].as<bool>();
                    bc.BorderSize = buttonComponent["BorderSize"].as<float>();
                    bc.ColorMultiplier = buttonComponent["ColorMultiplier"].as<float>();
                    bc.TransitionSpeed = buttonComponent["TransitionSpeed"].as<float>();
                    
                    bc.BackgroundNormalColor = buttonComponent["BackgroundNormalColor"].as<glm::vec4>();
                    bc.BackgroundHoverColor = buttonComponent["BackgroundHoverColor"].as<glm::vec4>();
                    bc.BackgroundPressedColor = buttonComponent["BackgroundPressedColor"].as<glm::vec4>();
                    bc.BackgroundDisabledColor = buttonComponent["BackgroundDisabledColor"].as<glm::vec4>();
                    bc.BackgroundHighlightedColor = buttonComponent["BackgroundHighlightedColor"].as<glm::vec4>();
                    
                    bc.TextNormalColor = buttonComponent["TextNormalColor"].as<glm::vec4>();
                    bc.TextHoverColor = buttonComponent["TextHoverColor"].as<glm::vec4>();
                    bc.TextPressedColor = buttonComponent["TextPressedColor"].as<glm::vec4>();
                    bc.TextDisabledColor = buttonComponent["TextDisabledColor"].as<glm::vec4>();
                    bc.TextHighlightedColor = buttonComponent["TextHighlightedColor"].as<glm::vec4>();
                }

                auto canvasComponent = entity["CanvasComponent"];
                if (canvasComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CanvasComponent>();
                    cc.Mode = (CanvasComponent::RenderMode)canvasComponent["RenderMode"].as<int>();
                    cc.PixelPerfect = canvasComponent["PixelPerfect"].as<bool>();
                    cc.SortOrder = canvasComponent["SortOrder"].as<int32_t>();
                }

                auto canvasScalerComponent = entity["CanvasScalerComponent"];
                if (canvasScalerComponent)
                {
                    auto& csc = deserializedEntity.AddComponent<CanvasScalerComponent>();
                    csc.ScaleMode = (CanvasScalerComponent::UIScaleMode)canvasScalerComponent["ScaleMode"].as<int>();
                    csc.ScaleFactor = canvasScalerComponent["ScaleFactor"].as<float>();
                    csc.ReferencePixelsPerUnit = canvasScalerComponent["ReferencePixelsPerUnit"].as<float>();
                    if (auto n = canvasScalerComponent["ReferenceResolution"]) csc.ReferenceResolution = n.as<glm::vec2>();
                    if (auto n = canvasScalerComponent["MatchWidthOrHeight"]) csc.MatchWidthOrHeight = n.as<float>();
                }

                auto graphicRaycasterComponent = entity["GraphicRaycasterComponent"];
                if (graphicRaycasterComponent)
                {
                    auto& grc = deserializedEntity.AddComponent<GraphicRaycasterComponent>();
                    grc.IgnoreReversedGraphics = graphicRaycasterComponent["IgnoreReversedGraphics"].as<bool>();
                    grc.Blocking = (GraphicRaycasterComponent::BlockingObjects)graphicRaycasterComponent["BlockingObjects"].as<int>();
                    grc.BlockingMask = graphicRaycasterComponent["BlockingMask"].as<uint32_t>();
                }

                auto rigidBody2DComponent = entity["RigidBody2DComponent"];
                if (rigidBody2DComponent)
                {
                    auto& rb2d = deserializedEntity.AddComponent<RigidBody2DComponent>();
                    rb2d.Type = (RigidBody2DComponent::BodyType)rigidBody2DComponent["BodyType"].as<int>();
                    rb2d.FixedRotation = rigidBody2DComponent["FixedRotation"].as<bool>();
#define CQ_RB2D_IN(key, T) if (rigidBody2DComponent[#key]) rb2d.key = rigidBody2DComponent[#key].as<T>()
                    CQ_RB2D_IN(UseAutoMass, bool); CQ_RB2D_IN(Mass, float); CQ_RB2D_IN(LinearDrag, float); CQ_RB2D_IN(AngularDrag, float);
                    CQ_RB2D_IN(GravityScale, float); CQ_RB2D_IN(Interpolate, bool); CQ_RB2D_IN(ContinuousCollision, bool); CQ_RB2D_IN(AllowSleep, bool);
                    if (auto pm = rigidBody2DComponent["PhysicsMaterial"]) rb2d.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
#undef CQ_RB2D_IN
                    // Eski sahneler: kütle yoğunluktan geliyordu
                    if (!rigidBody2DComponent["Mass"])
                        rb2d.UseAutoMass = true;
                }

                // Eski sahneler (Version < 2): kutu boyutu yarım genişlikti, doku varsa boyut dokudan (100 px/birim) geliyordu
                glm::vec2 legacyTexSize(0.0f);
                if (deserializedEntity.HasComponent<SpriteRendererComponent>() && deserializedEntity.GetComponent<SpriteRendererComponent>().Texture)
                {
                    auto& tex = deserializedEntity.GetComponent<SpriteRendererComponent>().Texture;
                    legacyTexSize = glm::vec2((float)tex->GetWidth(), (float)tex->GetHeight()) / 100.0f;
                }

                auto boxCollider2DComponent = entity["BoxCollider2DComponent"];
                if (boxCollider2DComponent)
                {
                    auto& bc2d = deserializedEntity.AddComponent<BoxCollider2DComponent>();
                    bc2d.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
                    bc2d.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
                    bc2d.Density = boxCollider2DComponent["Density"].as<float>();
                    bc2d.Friction = boxCollider2DComponent["Friction"].as<float>();
                    bc2d.Restitution = boxCollider2DComponent["Restitution"].as<float>();
                    bc2d.RestitutionThreshold = boxCollider2DComponent["RestitutionThreshold"].as<float>();
                    if (boxCollider2DComponent["IsTrigger"]) bc2d.IsTrigger = boxCollider2DComponent["IsTrigger"].as<bool>();
                    if (boxCollider2DComponent["EdgeRadius"]) bc2d.EdgeRadius = boxCollider2DComponent["EdgeRadius"].as<float>();
                    if (auto pm = boxCollider2DComponent["PhysicsMaterial"]) bc2d.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                    if (!boxCollider2DComponent["Version"])
                    {
                        bc2d.Size = legacyTexSize.x > 0.0f ? legacyTexSize : bc2d.Size * 2.0f;
                        bc2d.Offset = glm::vec2(0.0f);
                    }
                }

                auto circleCollider2DComponent = entity["CircleCollider2DComponent"];
                if (circleCollider2DComponent)
                {
                    auto& cc2d = deserializedEntity.AddComponent<CircleCollider2DComponent>();
                    cc2d.Offset = circleCollider2DComponent["Offset"].as<glm::vec2>();
                    cc2d.Radius = circleCollider2DComponent["Radius"].as<float>();
                    cc2d.Density = circleCollider2DComponent["Density"].as<float>();
                    cc2d.Friction = circleCollider2DComponent["Friction"].as<float>();
                    cc2d.Restitution = circleCollider2DComponent["Restitution"].as<float>();
                    cc2d.RestitutionThreshold = circleCollider2DComponent["RestitutionThreshold"].as<float>();
                    if (circleCollider2DComponent["IsTrigger"]) cc2d.IsTrigger = circleCollider2DComponent["IsTrigger"].as<bool>();
                    if (auto pm = circleCollider2DComponent["PhysicsMaterial"]) cc2d.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                    if (!circleCollider2DComponent["Version"] && legacyTexSize.x > 0.0f)
                        cc2d.Radius = std::max(legacyTexSize.x, legacyTexSize.y) * 0.5f;
                }

                auto rigidbodyComponent = entity["RigidbodyComponent"];
                if (rigidbodyComponent)
                {
                    auto& rb = deserializedEntity.AddComponent<RigidbodyComponent>();
                    rb.Type = (RigidbodyComponent::BodyType)rigidbodyComponent["BodyType"].as<int>();
                    rb.Mass = rigidbodyComponent["Mass"].as<float>();
                    rb.LinearDrag = rigidbodyComponent["LinearDrag"].as<float>();
                    rb.AngularDrag = rigidbodyComponent["AngularDrag"].as<float>();
                    rb.GravityScale = rigidbodyComponent["GravityScale"].as<float>();
                    rb.FreezeRotation = rigidbodyComponent["FreezeRotation"].as<bool>();
                    rb.Friction = rigidbodyComponent["Friction"].as<float>();
                    rb.Restitution = rigidbodyComponent["Restitution"].as<float>();
                    if (rigidbodyComponent["UseGravity"]) rb.UseGravity = rigidbodyComponent["UseGravity"].as<bool>();
                    if (auto fp = rigidbodyComponent["FreezePosition"]; fp && fp.IsSequence() && fp.size() == 3)
                    {
                        rb.FreezePositionX = fp[0].as<bool>(); rb.FreezePositionY = fp[1].as<bool>(); rb.FreezePositionZ = fp[2].as<bool>();
                    }
                    if (auto fr = rigidbodyComponent["FreezeRotationAxes"]; fr && fr.IsSequence() && fr.size() == 3)
                    {
                        rb.FreezeRotationX = fr[0].as<bool>(); rb.FreezeRotationY = fr[1].as<bool>(); rb.FreezeRotationZ = fr[2].as<bool>();
                    }
                    if (rigidbodyComponent["Interpolate"]) rb.Interpolate = rigidbodyComponent["Interpolate"].as<bool>();
                    if (rigidbodyComponent["CollisionDetection"])
                        rb.CollisionDetection = (RigidbodyComponent::CollisionDetectionMode)rigidbodyComponent["CollisionDetection"].as<int>();
                    if (rigidbodyComponent["MaxAngularVelocity"]) rb.MaxAngularVelocity = rigidbodyComponent["MaxAngularVelocity"].as<float>();
                    if (rigidbodyComponent["AllowSleep"]) rb.AllowSleep = rigidbodyComponent["AllowSleep"].as<bool>();
                }

                // Eski sahneler (Version < 2): çalışma zamanı çarpıştırıcıyı ilkel ağa göre zorla boyutlandırıyordu
                // (küp 2x2x2, küre/kapsül yarıçap 1). Aynı davranışı korumak için değerler taşınır.
                const bool legacyMeshFit = deserializedEntity.HasComponent<MeshRendererComponent>();

                auto boxColliderComponent = entity["BoxColliderComponent"];
                if (boxColliderComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<BoxColliderComponent>();
                    bc.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
                    bc.Size = boxColliderComponent["Size"].as<glm::vec3>();
                    bc.IsTrigger = boxColliderComponent["IsTrigger"].as<bool>();
                    if (auto pm = boxColliderComponent["PhysicsMaterial"]) bc.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                    if (!boxColliderComponent["Version"] && legacyMeshFit)
                    {
                        bc.Size = glm::vec3(2.0f);
                        bc.Offset = glm::vec3(0.0f);
                    }
                }

                auto sphereColliderComponent = entity["SphereColliderComponent"];
                if (sphereColliderComponent)
                {
                    auto& sc = deserializedEntity.AddComponent<SphereColliderComponent>();
                    sc.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
                    sc.Radius = sphereColliderComponent["Radius"].as<float>();
                    sc.IsTrigger = sphereColliderComponent["IsTrigger"].as<bool>();
                    if (auto pm = sphereColliderComponent["PhysicsMaterial"]) sc.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                    if (!sphereColliderComponent["Version"] && legacyMeshFit)
                    {
                        sc.Radius = 1.0f;
                        sc.Offset = glm::vec3(0.0f);
                    }
                }

                auto capsuleColliderComponent = entity["CapsuleColliderComponent"];
                if (capsuleColliderComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CapsuleColliderComponent>();
                    cc.Offset = capsuleColliderComponent["Offset"].as<glm::vec3>();
                    cc.Radius = capsuleColliderComponent["Radius"].as<float>();
                    cc.Height = capsuleColliderComponent["Height"].as<float>();
                    cc.IsTrigger = capsuleColliderComponent["IsTrigger"].as<bool>();
                    if (auto pm = capsuleColliderComponent["PhysicsMaterial"]) cc.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                    if (capsuleColliderComponent["Direction"]) cc.Direction = capsuleColliderComponent["Direction"].as<int>();
                    if (!capsuleColliderComponent["Version"] && legacyMeshFit)
                    {
                        cc.Radius = 1.0f;
                        cc.Height = 2.0f;
                        cc.Offset = glm::vec3(0.0f);
                    }
                }

                auto meshColliderComponent = entity["MeshColliderComponent"];
                if (meshColliderComponent)
                {
                    auto& mc = deserializedEntity.AddComponent<MeshColliderComponent>();
                    mc.IsConvex = meshColliderComponent["IsConvex"].as<bool>();
                    mc.IsTrigger = meshColliderComponent["IsTrigger"].as<bool>();
                    if (auto pm = meshColliderComponent["PhysicsMaterial"]) mc.PhysicsMaterial = ResolveSerializablePath(pm.as<std::string>());
                }

                auto audioSourceComponent = entity["AudioSourceComponent"];
                if (audioSourceComponent)
                {
                    auto& audio = deserializedEntity.AddComponent<AudioSourceComponent>();
                    audio.FilePath = ResolveSerializablePath(audioSourceComponent["FilePath"].as<std::string>());
                    audio.PlayOnAwake = audioSourceComponent["PlayOnAwake"].as<bool>();
                    audio.Loop = audioSourceComponent["Loop"].as<bool>();
                    audio.Mute = audioSourceComponent["Mute"].as<bool>();
                    audio.Volume = audioSourceComponent["Volume"].as<float>();
                    audio.Pitch = audioSourceComponent["Pitch"].as<float>();
                    audio.Pan = audioSourceComponent["Pan"].as<float>();
                    audio.Is3D = audioSourceComponent["Is3D"].as<bool>();
                    audio.MinDistance = audioSourceComponent["MinDistance"].as<float>();
                    audio.MaxDistance = audioSourceComponent["MaxDistance"].as<float>();
                    audio.DopplerLevel = audioSourceComponent["DopplerLevel"].as<float>();
                    audio.Attenuation = (AudioSourceComponent::AttenuationModel)audioSourceComponent["Attenuation"].as<int>();
                    audio.RolloffFactor = audioSourceComponent["RolloffFactor"].as<float>();
                    if (audioSourceComponent["OutputGroup"])
                        audio.OutputGroup = audioSourceComponent["OutputGroup"].as<std::string>();

                    if (audioSourceComponent["NextGraphNodeID"])
                        audio.NextGraphNodeID = audioSourceComponent["NextGraphNodeID"].as<int>();
                    if (audioSourceComponent["NextGraphLinkID"])
                        audio.NextGraphLinkID = audioSourceComponent["NextGraphLinkID"].as<int>();
                    if (audioSourceComponent["GraphInitialized"])
                        audio.GraphInitialized = audioSourceComponent["GraphInitialized"].as<bool>();

                    auto graphNodes = audioSourceComponent["GraphNodes"];
                    if (graphNodes)
                    {
                        for (auto node : graphNodes)
                        {
                            AudioGraphNode graphNode(
                                node["ID"].as<int>(),
                                (AudioGraphNodeType)node["Type"].as<int>(),
                                { node["EditorPosX"].as<float>(), node["EditorPosY"].as<float>() }
                            );
                            graphNode.Bypassed = node["Bypassed"].as<bool>();
                            audio.GraphNodes.push_back(graphNode);
                        }
                    }

                    auto graphLinks = audioSourceComponent["GraphLinks"];
                    if (graphLinks)
                    {
                        for (auto link : graphLinks)
                        {
                            audio.GraphLinks.emplace_back(
                                link["ID"].as<int>(),
                                link["From"].as<int>(),
                                link["To"].as<int>()
                            );
                        }
                    }
                    
                    audio.NeedsReconnection = true;
                }

                auto audioListenerComponent = entity["AudioListenerComponent"];
                if (audioListenerComponent)
                {
                    auto& listener = deserializedEntity.AddComponent<AudioListenerComponent>();
                    listener.IsActive = audioListenerComponent["IsActive"].as<bool>();
                }

                auto audioEchoFilterComponent = entity["AudioEchoFilterComponent"];
                if (audioEchoFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioEchoFilterComponent>();
                    comp.Enabled = audioEchoFilterComponent["Enabled"].as<bool>();
                    comp.Delay = audioEchoFilterComponent["Delay"].as<float>();
                    comp.Decay = audioEchoFilterComponent["Decay"].as<float>();
                    comp.WetMix = audioEchoFilterComponent["WetMix"].as<float>();
                    comp.DryMix = audioEchoFilterComponent["DryMix"].as<float>();
                }

                auto audioReverbFilterComponent = entity["AudioReverbFilterComponent"];
                if (audioReverbFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioReverbFilterComponent>();
                    comp.Enabled = audioReverbFilterComponent["Enabled"].as<bool>();
                    comp.DecayTime = audioReverbFilterComponent["DecayTime"].as<float>();
                    comp.Room = audioReverbFilterComponent["Room"].as<float>();
                    comp.ReverbLevel = audioReverbFilterComponent["ReverbLevel"].as<float>();
                }

                auto audioLowPassFilterComponent = entity["AudioLowPassFilterComponent"];
                if (audioLowPassFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioLowPassFilterComponent>();
                    comp.Enabled = audioLowPassFilterComponent["Enabled"].as<bool>();
                    comp.CutoffFrequency = audioLowPassFilterComponent["CutoffFrequency"].as<float>();
                    comp.Resonance = audioLowPassFilterComponent["Resonance"].as<float>();
                }

                auto audioHighPassFilterComponent = entity["AudioHighPassFilterComponent"];
                if (audioHighPassFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioHighPassFilterComponent>();
                    comp.Enabled = audioHighPassFilterComponent["Enabled"].as<bool>();
                    comp.CutoffFrequency = audioHighPassFilterComponent["CutoffFrequency"].as<float>();
                    comp.Resonance = audioHighPassFilterComponent["Resonance"].as<float>();
                }

                auto audioChorusFilterComponent = entity["AudioChorusFilterComponent"];
                if (audioChorusFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioChorusFilterComponent>();
                    comp.Enabled = audioChorusFilterComponent["Enabled"].as<bool>();
                    comp.Delay = audioChorusFilterComponent["Delay"].as<float>();
                    comp.Depth = audioChorusFilterComponent["Depth"].as<float>();
                    comp.Rate = audioChorusFilterComponent["Rate"].as<float>();
                    comp.WetMix = audioChorusFilterComponent["WetMix"].as<float>();
                    comp.DryMix = audioChorusFilterComponent["DryMix"].as<float>();
                }

                auto audioDistortionFilterComponent = entity["AudioDistortionFilterComponent"];
                if (audioDistortionFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioDistortionFilterComponent>();
                    comp.Enabled = audioDistortionFilterComponent["Enabled"].as<bool>();
                    comp.Level = audioDistortionFilterComponent["Level"].as<float>();
                }

                auto audioGainComponent = entity["AudioGainComponent"];
                if (audioGainComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioGainComponent>();
                    comp.Enabled = audioGainComponent["Enabled"].as<bool>();
                    comp.Gain = audioGainComponent["Gain"].as<float>();
                }

                auto audioPanComponent = entity["AudioPanComponent"];
                if (audioPanComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioPanComponent>();
                    comp.Enabled = audioPanComponent["Enabled"].as<bool>();
                    comp.Pan = audioPanComponent["Pan"].as<float>();
                }

                auto audioReverbZoneComponent = entity["AudioReverbZoneComponent"];
                if (audioReverbZoneComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioReverbZoneComponent>();
                    comp.Enabled = audioReverbZoneComponent["Enabled"].as<bool>();
                    comp.MinDistance = audioReverbZoneComponent["MinDistance"].as<float>();
                    comp.MaxDistance = audioReverbZoneComponent["MaxDistance"].as<float>();
                    comp.Preset = (AudioReverbZoneComponent::ReverbPreset)audioReverbZoneComponent["Preset"].as<int>();
                }
                            }
                catch (const std::exception& e)
                {
                    CQ_CORE_ERROR("Scene load: skipped invalid data in entity: {0}", e.what());
                }
            }
        }
        
        auto environment = data["Environment"];
        if (environment)
        {
            if (environment["SkyboxPath"])
            {
                std::string skyboxPath = ResolveSerializablePath(environment["SkyboxPath"].as<std::string>());
                if (!skyboxPath.empty() && std::filesystem::exists(skyboxPath))
                {
                    auto skybox = Cubemap::CreateFromEquirectangular(skyboxPath, 512);
                    m_Scene->SetSkybox(skybox);
                }
                else if (!skyboxPath.empty())
                {
                    CQ_CORE_WARN("SceneSerializer: Skybox file not found, skipping: {0}", skyboxPath);
                }
            }
            if (environment["SkyboxExposure"])
                m_Scene->SetSkyboxExposure(environment["SkyboxExposure"].as<float>());
            if (environment["SkyboxRotation"])
                m_Scene->SetSkyboxRotation(environment["SkyboxRotation"].as<float>());
            if (environment["SkyboxTint"])
                m_Scene->SetSkyboxTint(environment["SkyboxTint"].as<glm::vec3>());
            
            if (environment["EnvironmentLightingSource"])
                m_Scene->SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)environment["EnvironmentLightingSource"].as<int>());
            if (environment["AmbientColor"])
                m_Scene->SetAmbientColor(environment["AmbientColor"].as<glm::vec3>());
            if (environment["AmbientSkyColor"])
                m_Scene->SetAmbientSkyColor(environment["AmbientSkyColor"].as<glm::vec3>());
            if (environment["AmbientEquatorColor"])
                m_Scene->SetAmbientEquatorColor(environment["AmbientEquatorColor"].as<glm::vec3>());
            if (environment["AmbientGroundColor"])
                m_Scene->SetAmbientGroundColor(environment["AmbientGroundColor"].as<glm::vec3>());
            if (environment["AmbientIntensity"])
                m_Scene->SetAmbientIntensity(environment["AmbientIntensity"].as<float>());
            
            if (environment["FogEnabled"])
                m_Scene->SetFogEnabled(environment["FogEnabled"].as<bool>());
            if (environment["FogColor"])
                m_Scene->SetFogColor(environment["FogColor"].as<glm::vec3>());
            if (environment["FogDensity"])
                m_Scene->SetFogDensity(environment["FogDensity"].as<float>());
            if (environment["FogStart"])
                m_Scene->SetFogStart(environment["FogStart"].as<float>());
            if (environment["FogEnd"])
                m_Scene->SetFogEnd(environment["FogEnd"].as<float>());
            
            if (environment["SunSource"])
                m_Scene->SetSunSource(environment["SunSource"].as<uint64_t>());
            
            if (environment["HaloEnabled"])
                m_Scene->SetHaloEnabled(environment["HaloEnabled"].as<bool>());
            if (environment["HaloStrength"])
                m_Scene->SetHaloStrength(environment["HaloStrength"].as<float>());
            if (environment["FlareEnabled"])
                m_Scene->SetFlareEnabled(environment["FlareEnabled"].as<bool>());
            if (environment["FlareFadeSpeed"])
                m_Scene->SetFlareFadeSpeed(environment["FlareFadeSpeed"].as<float>());
            if (environment["FlareStrength"])
                m_Scene->SetFlareStrength(environment["FlareStrength"].as<float>());
            
            auto flareElements = environment["FlareElements"];
            if (flareElements)
            {
                m_Scene->SetFlareElementCount(flareElements.size());
                int index = 0;
                for (auto elementNode : flareElements)
                {
                    Scene::FlareElement element;
                    element.ColorMultiplier = elementNode["ColorMultiplier"].as<glm::vec3>();
                    element.Size = elementNode["Size"].as<float>();
                    element.Offset = elementNode["Offset"].as<float>();
                    element.Intensity = elementNode["Intensity"].as<float>();
                    m_Scene->SetFlareElement(index++, element);
                }
            }
            
            // Shadow settings
            if (environment["ShadowEnabled"])
                m_Scene->SetShadowEnabled(environment["ShadowEnabled"].as<bool>());
            if (environment["ShadowIntensity"])
                m_Scene->SetShadowIntensity(environment["ShadowIntensity"].as<float>());
            
            // Environment Reflections
            if (environment["ReflectionSource"])
                m_Scene->SetReflectionSource((Scene::ReflectionSource)environment["ReflectionSource"].as<int>());
            if (environment["ReflectionResolution"])
                m_Scene->SetReflectionResolution(environment["ReflectionResolution"].as<int>());
            if (environment["ReflectionCompression"])
                m_Scene->SetReflectionCompression(environment["ReflectionCompression"].as<int>());
            if (environment["ReflectionIntensityMultiplier"])
                m_Scene->SetReflectionIntensityMultiplier(environment["ReflectionIntensityMultiplier"].as<float>());
            if (environment["ReflectionBounces"])
                m_Scene->SetReflectionBounces(environment["ReflectionBounces"].as<int>());
            
            // Baked Lightmap
            if (environment["BakedLightmapPath"])
                m_Scene->SetBakedLightmapPath(environment["BakedLightmapPath"].as<std::string>());
            if (environment["LightmapResolution"])
                m_Scene->SetLightmapResolution(environment["LightmapResolution"].as<int>());
            if (environment["MaxLightmapSize"])
                m_Scene->SetMaxLightmapSize(environment["MaxLightmapSize"].as<int>());
            if (environment["MaxBounces"])
                m_Scene->SetMaxBounces(environment["MaxBounces"].as<int>());
            if (environment["DirectSamples"])
                m_Scene->SetDirectSamples(environment["DirectSamples"].as<int>());
            if (environment["IndirectSamples"])
                m_Scene->SetIndirectSamples(environment["IndirectSamples"].as<int>());
            if (environment["AmbientOcclusion"])
                m_Scene->SetAmbientOcclusion(environment["AmbientOcclusion"].as<bool>());
            if (environment["LightmapFiltering"])
                m_Scene->SetFiltering(environment["LightmapFiltering"].as<int>());
            
            DeserializeRenderSettings(environment["RenderSettings"], m_Scene->GetRenderSettings());

            // Load lightmap texture from disk
            std::string lmPath = m_Scene->GetBakedLightmapPath();
            if (!lmPath.empty())
            {
                std::string resolvedPath = ResolveSerializablePath(lmPath);
                if (!resolvedPath.empty())
                {
                    auto lmTex = Texture2D::Create(resolvedPath);
                    if (lmTex)
                    {
                        m_Scene->SetBakedLightmapTexture(lmTex);
                    }
                }
            }
        }

        // Editör kamerası (sahneyle birlikte kaydedilen bakış açısı / 2D-3D modu)
        if (auto cam = data["EditorCamera"]; cam && m_EditorCamera)
        {
            try
            {
                auto mode = (EditorCameraMode)cam["CameraMode"].as<int>(0);
                m_EditorCamera->SetCameraMode(mode);
                float distance = std::max(0.05f, cam["Distance"].as<float>(10.0f));
                glm::vec3 position = cam["Position"] ? cam["Position"].as<glm::vec3>() : glm::vec3(0.0f, 0.0f, 10.0f);
                m_EditorCamera->SetOrthographicSize(std::max(0.01f, cam["OrthographicSize"].as<float>(10.0f)));
                if (mode == EditorCameraMode::Orthographic2D)
                {
                    m_EditorCamera->SetDistance(distance);
                    m_EditorCamera->SetFocalPoint(glm::vec3(position.x, position.y, 0.0f));
                }
                else
                {
                    m_EditorCamera->SetPitch(cam["Pitch"].as<float>(0.5235987f));
                    m_EditorCamera->SetYaw(cam["Yaw"].as<float>(0.7853981f));
                    m_EditorCamera->SetDistance(distance);
                    m_EditorCamera->SetFocalPoint(position + m_EditorCamera->GetForwardDirection() * distance);
                }
            }
            catch (const std::exception& e)
            {
                CQ_CORE_WARN("SceneSerializer: invalid EditorCamera block ({0})", e.what());
            }
        }

        auto hierarchy = data["Hierarchy"];
        if (hierarchy)
        {
            std::unordered_map<uint64_t, entt::entity> uuidToEntity;
            m_Scene->m_Registry.view<IDComponent>().each([&](auto entity, auto& idComponent)
            {
                uuidToEntity[idComponent.ID] = entity;
            });
            
            for (auto relation : hierarchy)
            {
                uint64_t childUUID = relation["Child"].as<uint64_t>();
                uint64_t parentUUID = relation["Parent"].as<uint64_t>();
                
                if (uuidToEntity.find(childUUID) != uuidToEntity.end() && 
                    uuidToEntity.find(parentUUID) != uuidToEntity.end())
                {
                    Entity childEntity = { uuidToEntity[childUUID], m_Scene.get() };
                    Entity parentEntity = { uuidToEntity[parentUUID], m_Scene.get() };
                    m_Scene->SetEntityParent(childEntity, parentEntity, false);
                }
            }
        }

        auto navMeshNode = data["NavMesh"];
        // Eski sürümlerin kaydettiği NavMesh üçgenleri yok sayılır (ağ yeniden pişirilir)
        (void)navMeshNode;

        return true;
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        std::ifstream stream(filepath);
        if (!stream.is_open())
        {
            CQ_CORE_ERROR("Failed to open scene file: {0}", filepath.string());
            return false;
        }
        return DeserializeFromStream(stream);
    }

    bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
    {
        // Runtime deserialization için aynı format
        return Deserialize(filepath);
    }
}
