#include "Core/Serialization/SceneSerializer.h"
#include "Material.h"
#include "MaterialAsset.h"
#include "Renderer/ExtraTextures.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Serialization/SceneSerializer.h"
#include <glad/glad.h>
#include <unordered_map>
#include "Renderer3D.h"

namespace Conqueror
{
    bool Material::LoadShaderFile()
    {
        if (ShaderPath.empty() || (GraphInitialized && !GraphNodes.empty()))
            return false;
        auto shader = Shader::Create(SceneSerializer::ResolveAssetPath(ShaderPath));
        if (!shader || !shader->IsValid())
        {
            CQ_CORE_ERROR("[Material] Shader '{0}' could not be compiled; using the default Lit shader.", ShaderPath);
            MaterialShader = nullptr;
            return false;
        }
        MaterialShader = shader;
        return true;
    }

    Material::Material()
    {
        CreateDefaultTextures();
    }

    void Material::CreateDefaultTextures()
    {
        // Varsayılan 1x1 dokular TÜM materyaller arasında paylaşılır. Eskiden her
        // Material 3 GPU dokusu oluşturuyordu; sahne her karede geçici Material
        // ürettiği için bu, kare başına yüzlerce doku tahsisi demekti.
        // Bilinçli olarak yok edilmez: statik yıkıcılar GL bağlamı kapandıktan
        // sonra çalışır ve glDeleteTextures çağrısı çökmeye yol açardı.
        struct Defaults { std::shared_ptr<Texture2D> White, Normal, Black; };
        static Defaults* s_Defaults = nullptr;
        if (!s_Defaults)
        {
            s_Defaults = new Defaults();
            uint32_t whiteData = 0xFFFFFFFF;
            s_Defaults->White = Texture2D::Create(1, 1);
            s_Defaults->White->SetData(&whiteData, sizeof(uint32_t));

            uint32_t normalData = 0xFFFF8080; // RGBA: 128, 128, 255, 255 (yukarı bakan normal)
            s_Defaults->Normal = Texture2D::Create(1, 1);
            s_Defaults->Normal->SetData(&normalData, sizeof(uint32_t));

            uint32_t blackData = 0xFF000000;
            s_Defaults->Black = Texture2D::Create(1, 1);
            s_Defaults->Black->SetData(&blackData, sizeof(uint32_t));
        }
        m_WhiteTexture = s_Defaults->White;
        m_DefaultNormalMap = s_Defaults->Normal;
        m_BlackTexture = s_Defaults->Black;
    }

    void Material::Bind(std::shared_ptr<Shader> shader) const
    {
        shader->Bind();

        // PBR parametrelerini shader'a gönder
        shader->SetFloat3("u_Material.Albedo", Albedo);
        shader->SetFloat("u_Material.Metallic", Metallic);
        shader->SetFloat("u_Material.Roughness", Roughness);
        shader->SetFloat("u_Material.AO", AO);

        // Emission
        shader->SetFloat3("u_Material.EmissionColor", EmissionColor);
        shader->SetFloat("u_Material.EmissionStrength", EmissionStrength);

        // UV tiling/offset + render modu
        shader->SetFloat2("u_Material.Tiling", Tiling);
        shader->SetFloat2("u_Material.Offset", Offset);
        shader->SetInt("u_WorldAligned", WorldAligned ? (AlignToObject ? 2 : 1) : 0);
        shader->SetInt("u_Material.ShadingModel", (int)Shading);
        shader->SetFloat3("u_Material.SubsurfaceColor", SubsurfaceColor);
        shader->SetFloat("u_Material.SubsurfaceAmount", SubsurfaceAmount);
        shader->SetInt("u_RenderMode", (int)Rendering);
        shader->SetFloat("u_AlphaClip", AlphaClip);
        shader->SetFloat("u_Opacity", Opacity);
        shader->SetInt("u_MetallicChannel", MetallicChannel);
        shader->SetInt("u_RoughnessChannel", RoughnessChannel);
        shader->SetInt("u_MapsScaleFactors", MapsScaleFactors ? 1 : 0);

        // ── Texture'ları bind et ───────────────────────────────────────

        // Albedo Map (slot 0)
        if (AlbedoMap)
        {
            AlbedoMap->Bind(0);
            shader->SetInt("u_AlbedoMap", 0);
            shader->SetInt("u_UseAlbedoMap", 1);
        }
        else
        {
            m_WhiteTexture->Bind(0);
            shader->SetInt("u_AlbedoMap", 0);
            shader->SetInt("u_UseAlbedoMap", 0);
        }

        // Normal Map (slot 1)
        if (NormalMap)
        {
            NormalMap->Bind(1);
            shader->SetInt("u_NormalMap", 1);
            shader->SetInt("u_UseNormalMap", 1);
        }
        else
        {
            m_DefaultNormalMap->Bind(1);
            shader->SetInt("u_NormalMap", 1);
            shader->SetInt("u_UseNormalMap", 0);
        }

        // Metallic Map (slot 2)
        if (MetallicMap)
        {
            MetallicMap->Bind(2);
            shader->SetInt("u_MetallicMap", 2);
            shader->SetInt("u_UseMetallicMap", 1);
        }
        else
        {
            m_WhiteTexture->Bind(2);
            shader->SetInt("u_MetallicMap", 2);
            shader->SetInt("u_UseMetallicMap", 0);
        }

        // Roughness Map (slot 3)
        if (RoughnessMap)
        {
            RoughnessMap->Bind(3);
            shader->SetInt("u_RoughnessMap", 3);
            shader->SetInt("u_UseRoughnessMap", 1);
        }
        else
        {
            m_WhiteTexture->Bind(3);
            shader->SetInt("u_RoughnessMap", 3);
            shader->SetInt("u_UseRoughnessMap", 0);
        }

        // AO Map (slot 4)
        if (AOMap)
        {
            AOMap->Bind(4);
            shader->SetInt("u_AOMap", 4);
            shader->SetInt("u_UseAOMap", 1);
        }
        else
        {
            m_WhiteTexture->Bind(4);
            shader->SetInt("u_AOMap", 4);
            shader->SetInt("u_UseAOMap", 0);
        }

        // Emission Map (slot 5)
        if (EmissionMap)
        {
            EmissionMap->Bind(5);
            shader->SetInt("u_EmissionMap", 5);
            shader->SetInt("u_UseEmissionMap", 1);
        }
        else
        {
            m_BlackTexture->Bind(5);
            shader->SetInt("u_EmissionMap", 5);
            shader->SetInt("u_UseEmissionMap", 0);
        }

        // ── Genel uniform override'ları ────────────────────────────────
        for (const auto& [name, value] : FloatProperties)
            shader->SetFloat(name, value);

        for (const auto& [name, value] : IntProperties)
            shader->SetInt(name, value);

        for (const auto& [name, value] : Vec3Properties)
            shader->SetFloat3(name, value);

        for (const auto& [name, value] : Vec4Properties)
            shader->SetFloat4(name, value);
    }

    void Material::BindSelf() const
    {
        if (MaterialShader)
        {
            MaterialShader->Bind();
            Bind(MaterialShader);
            BindGraphUniforms(MaterialShader);
        }
    }

    void Material::BindGraphUniforms(std::shared_ptr<Shader> shader) const
    {
        bool needsTime = false;
        for (const auto& node : GraphNodes)
        {
            if (node.Type == ShaderGraphNodeType::Time || node.Type == ShaderGraphNodeType::Panner)
            {
                needsTime = true;
                break;
            }
        }
        if (needsTime)
            shader->SetFloat("u_Time", Renderer3D::GetFrameTime());

        int textureSlot = 0;
        constexpr int kFirstGraphTextureSlot = 8;   // 8..11 (12+ renderer'a ayrılmış)
        constexpr int kMaxGraphTextures = 4;
        // Material Function içindeki düğümler de bağlanır (derlemeyle aynı açılmış liste)
        std::vector<ShaderGraphNode> expanded = GraphNodes;
        std::vector<ShaderGraphLink> expandedLinks = GraphLinks;
        ExpandFunctions(expanded, expandedLinks);
        const auto bindExtra = [&](GLenum target, uint32_t handle, const std::string& uniform) {
            if (!handle || textureSlot >= kMaxGraphTextures)
                return;
            const int slot = kFirstGraphTextureSlot + textureSlot++;
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(target, handle);
            shader->SetInt(uniform, slot);
        };
        for (const auto& node : expanded)
        {
            std::string idStr = std::to_string(node.ID);
            switch (node.Type)
            {
                case ShaderGraphNodeType::Color:
                    shader->SetFloat4("u_Node" + idStr + "_Color", node.ColorValue);
                    break;
                case ShaderGraphNodeType::Float:
                    shader->SetFloat("u_Node" + idStr + "_Float", node.FloatValue);
                    break;
                case ShaderGraphNodeType::Vec2:
                    shader->SetFloat2("u_Node" + idStr + "_Vec2", node.Vec2Value);
                    break;
                case ShaderGraphNodeType::Vec3:
                    shader->SetFloat3("u_Node" + idStr + "_Vec3", node.Vec3Value);
                    break;
                case ShaderGraphNodeType::CollectionParameter:
                {
                    glm::vec4 value(0.0f);
                    float scalar = 0.0f;
                    if (MaterialParameterCollections::GetVector(node.TexturePath, node.CustomCode, value))
                        shader->SetFloat4("u_Node" + idStr + "_MPC", value);
                    else if (MaterialParameterCollections::GetScalar(node.TexturePath, node.CustomCode, scalar))
                        shader->SetFloat4("u_Node" + idStr + "_MPC", glm::vec4(scalar));
                    break;
                }
                case ShaderGraphNodeType::TextureArraySample:
                    bindExtra(GL_TEXTURE_2D_ARRAY, ExtraTextures::TextureArray(node.TexturePath), "u_Node" + idStr + "_Tex");
                    break;
                case ShaderGraphNodeType::VolumeTextureSample:
                    bindExtra(GL_TEXTURE_3D, ExtraTextures::Volume(node.TexturePath), "u_Node" + idStr + "_Tex");
                    break;
                case ShaderGraphNodeType::CubeTargetSample:
                    bindExtra(GL_TEXTURE_CUBE_MAP, ExtraTextures::Cube(node.TexturePath), "u_Node" + idStr + "_Tex");
                    break;
                case ShaderGraphNodeType::TextureSample:
                case ShaderGraphNodeType::NormalMap:
                    if (node.TextureRef && textureSlot < kMaxGraphTextures)
                    {
                        int slot = kFirstGraphTextureSlot + textureSlot++;
                        node.TextureRef->Bind(slot);
                        shader->SetInt("u_Node" + idStr + "_Tex", slot);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void Material::ExpandFunctions(std::vector<ShaderGraphNode>& nodes, std::vector<ShaderGraphLink>& links, int depth) const
    {
        if (depth > 8)
            return;
        bool any = false;
        for (const auto& n : nodes)
            any |= n.Type == ShaderGraphNodeType::MaterialFunctionCall;
        if (!any)
            return;
        int nextId = 0;
        for (const auto& n : nodes)
            nextId = std::max(nextId, n.ID);
        std::vector<ShaderGraphNode> callNodes;
        for (const auto& n : nodes)
            if (n.Type == ShaderGraphNodeType::MaterialFunctionCall)
                callNodes.push_back(n);
        for (const auto& call : callNodes)
        {
            std::shared_ptr<Material> func = call.TexturePath.empty() ? nullptr : MaterialAsset::Load(call.TexturePath);
            std::unordered_map<int, int> remap;
            std::unordered_map<int, int> inputPins, outputPins;
            std::vector<ShaderGraphNode> added;
            if (func)
                for (auto n : func->GraphNodes)
                {
                    if (n.Type == ShaderGraphNodeType::MasterOutput)
                        continue;
                    const int original = n.ID;
                    n.ID = ++nextId;
                    remap[original] = n.ID;
                    if (n.Type == ShaderGraphNodeType::FunctionInput)
                        inputPins[original] = n.IntValue;
                    if (n.Type == ShaderGraphNodeType::FunctionOutput)
                        outputPins[original] = n.IntValue;
                    if (!n.TexturePath.empty() && !n.TextureRef)
                        n.TextureRef = Texture2D::Create(n.TexturePath);
                    added.push_back(n);
                }
            std::vector<ShaderGraphLink> result;
            int nextLink = 1 << 20;
            // Dış bağlantılar: çağrının çıkışı -> iç FunctionOutput'a bağlanan kaynak
            for (const auto& l : links)
            {
                if (l.ToNodeID == call.ID)
                    continue;   // girişler aşağıda iç düğümlere bağlanır
                if (l.FromNodeID != call.ID)
                {
                    result.push_back(l);
                    continue;
                }
                bool wired = false;
                if (func)
                    for (const auto& inner : func->GraphLinks)
                        if (outputPins.count(inner.ToNodeID) && outputPins[inner.ToNodeID] == l.FromPinIndex && remap.count(inner.FromNodeID))
                        {
                            result.emplace_back(nextLink++, remap[inner.FromNodeID], inner.FromPinIndex, l.ToNodeID, l.ToPinIndex);
                            wired = true;
                        }
                (void)wired;
            }
            if (func)
                for (const auto& inner : func->GraphLinks)
                {
                    if (outputPins.count(inner.ToNodeID) || !remap.count(inner.ToNodeID))
                        continue;
                    if (inputPins.count(inner.FromNodeID))
                    {
                        // Giriş düğümü: çağrıya bağlı kaynak varsa ona, yoksa düğümün varsayılanına
                        bool found = false;
                        for (const auto& outer : links)
                            if (outer.ToNodeID == call.ID && outer.ToPinIndex == inputPins[inner.FromNodeID])
                            {
                                result.emplace_back(nextLink++, outer.FromNodeID, outer.FromPinIndex, remap[inner.ToNodeID], inner.ToPinIndex);
                                found = true;
                            }
                        if (!found)
                            result.emplace_back(nextLink++, remap[inner.FromNodeID], inner.FromPinIndex, remap[inner.ToNodeID], inner.ToPinIndex);
                    }
                    else if (remap.count(inner.FromNodeID))
                        result.emplace_back(nextLink++, remap[inner.FromNodeID], inner.FromPinIndex, remap[inner.ToNodeID], inner.ToPinIndex);
                }
            links = std::move(result);
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const ShaderGraphNode& n) { return n.ID == call.ID; }), nodes.end());
            for (auto& n : added)
            {
                // Bağlanmamış giriş düğümü: renk sabitine dönüşür; çıkış düğümleri listeden çıkar
                if (n.Type == ShaderGraphNodeType::FunctionInput)
                    n.Type = ShaderGraphNodeType::Color;
                if (n.Type != ShaderGraphNodeType::FunctionOutput)
                    nodes.push_back(n);
            }
        }
        ExpandFunctions(nodes, links, depth + 1);
    }

    void Material::BuildLayerGraph()
    {
        if (Layers.empty())
            return;
        GraphNodes.clear();
        GraphLinks.clear();
        int nextNode = 1, nextLink = 1;
        const auto addNode = [&](ShaderGraphNodeType type) {
            GraphNodes.emplace_back(nextNode, type);
            return nextNode++;
        };
        const auto addLink = [&](int from, int fromPin, int to, int toPin) { GraphLinks.emplace_back(nextLink++, from, fromPin, to, toPin); };
        const int master = addNode(ShaderGraphNodeType::MasterOutput);
        int albedo = 0, roughness = 0, metallic = 0, normal = 0;
        for (size_t i = 0; i < Layers.size(); ++i)
        {
            SimpleAssets::Data layer;
            if (!SimpleAssets::Load(Layers[i].first, layer))
                continue;
            // Katman renkleri: doku varsa doku x renk
            const int color = addNode(ShaderGraphNodeType::Color);
            GraphNodes.back().ColorValue = layer.Color("Tint");
            int albedoNode = color;
            if (const std::string texture = layer.Str("Albedo"); !texture.empty())
            {
                const int tex = addNode(ShaderGraphNodeType::TextureSample);
                GraphNodes.back().TexturePath = texture;
                GraphNodes.back().TextureRef = Texture2D::Create(SceneSerializer::ResolveAssetPath(texture));
                const int mul = addNode(ShaderGraphNodeType::Multiply);
                addLink(color, 0, mul, 0);
                addLink(tex, 0, mul, 1);
                albedoNode = mul;
            }
            const int rough = addNode(ShaderGraphNodeType::Float);
            GraphNodes.back().FloatValue = layer.Float("Roughness", 0.5f);
            const int metal = addNode(ShaderGraphNodeType::Float);
            GraphNodes.back().FloatValue = layer.Float("Metallic", 0.0f);
            if (i == 0 || !albedo)
            {
                albedo = albedoNode;
                roughness = rough;
                metallic = metal;
                continue;
            }
            // Karışım: maske (doku / köşe rengi / eğim) + keskinlik
            SimpleAssets::Data blend;
            const bool hasBlend = SimpleAssets::Load(Layers[i].second, blend);
            const std::string mode = hasBlend ? blend.Str("Mode", "Mask Texture") : "Mask Texture";
            int mask = 0;
            if (mode == "Vertex Color" )
                mask = addNode(ShaderGraphNodeType::VertexColor);
            else if (mode == "Slope")
            {
                const int normalNode = addNode(ShaderGraphNodeType::NormalVector);
                const int up = addNode(ShaderGraphNodeType::Vec3);
                GraphNodes.back().Vec3Value = glm::vec3(0.0f, 1.0f, 0.0f);
                mask = addNode(ShaderGraphNodeType::Dot);
                addLink(normalNode, 0, mask, 0);
                addLink(up, 0, mask, 1);
            }
            else
            {
                mask = addNode(ShaderGraphNodeType::TextureSample);
                if (hasBlend && !blend.Str("Mask").empty())
                {
                    GraphNodes.back().TexturePath = blend.Str("Mask");
                    GraphNodes.back().TextureRef = Texture2D::Create(SceneSerializer::ResolveAssetPath(blend.Str("Mask")));
                }
            }
            const int low = addNode(ShaderGraphNodeType::Float);
            GraphNodes.back().FloatValue = hasBlend ? blend.Float("Threshold", 0.5f) - blend.Float("Softness", 0.1f) : 0.4f;
            const int high = addNode(ShaderGraphNodeType::Float);
            GraphNodes.back().FloatValue = hasBlend ? blend.Float("Threshold", 0.5f) + blend.Float("Softness", 0.1f) : 0.6f;
            const int smooth = addNode(ShaderGraphNodeType::Smoothstep);
            addLink(low, 0, smooth, 0);
            addLink(high, 0, smooth, 1);
            addLink(mask, 0, smooth, 2);
            const auto mix = [&](int base, int top) {
                const int m = addNode(ShaderGraphNodeType::Mix);
                addLink(base, 0, m, 0);
                addLink(top, 0, m, 1);
                addLink(smooth, 0, m, 2);
                return m;
            };
            albedo = mix(albedo, albedoNode);
            roughness = mix(roughness, rough);
            metallic = mix(metallic, metal);
        }
        if (albedo) addLink(albedo, 0, master, 0);
        if (metallic) addLink(metallic, 0, master, 2);
        if (roughness) addLink(roughness, 0, master, 3);
        (void)normal;
        NextGraphNodeID = nextNode;
        GraphInitialized = true;
    }

    void Material::ApplyGraphToProperties()
    {
        // Graph bağlantılarına göre Material'ın PBR parametrelerini güncelle
        // MasterOutput node'unun her input pin'ine ne bağlı olduğunu bul

        // MasterOutput node'unu bul
        ShaderGraphNode* masterNode = nullptr;
        for (auto& node : GraphNodes)
        {
            if (node.Type == ShaderGraphNodeType::MasterOutput)
            {
                masterNode = &node;
                break;
            }
        }
        if (!masterNode) return;

        // Her MasterOutput input pin'i için bağlı olan source node'u bul
        // Pin indeksleri: 0=Albedo, 1=Normal, 2=Metallic, 3=Roughness, 4=AO, 5=Emission
        auto findSourceForPin = [&](int masterPinIndex) -> ShaderGraphNode* {
            for (const auto& link : GraphLinks)
            {
                if (link.ToNodeID == masterNode->ID && link.ToPinIndex == masterPinIndex)
                {
                    for (auto& node : GraphNodes)
                    {
                        if (node.ID == link.FromNodeID)
                            return &node;
                    }
                }
            }
            return nullptr;
        };

        // Albedo (pin 0)
        ShaderGraphNode* albedoSource = findSourceForPin(0);
        if (albedoSource)
        {
            if (albedoSource->Type == ShaderGraphNodeType::Color)
            {
                Albedo = glm::vec3(albedoSource->ColorValue);   // Base Map korunur: renk onu çarpar (Unity Lit)
                CQ_CORE_INFO("Material '{0}': Albedo updated from Graph Color Node to ({1}, {2}, {3})", Name, Albedo.r, Albedo.g, Albedo.b);
            }
            else if (albedoSource->Type == ShaderGraphNodeType::TextureSample)
            {
                if (!albedoSource->TexturePath.empty() && !albedoSource->TextureRef)
                    albedoSource->TextureRef = Texture2D::Create(albedoSource->TexturePath);
                AlbedoMap = albedoSource->TextureRef;
                CQ_CORE_INFO("Material '{0}': Albedo updated from Graph Texture Node: {1}", Name, albedoSource->TexturePath);
            }
            else if (albedoSource->Type == ShaderGraphNodeType::Vec3)
            {
                Albedo = albedoSource->Vec3Value;
            }
        }

        // Normal (pin 1)
        ShaderGraphNode* normalSource = findSourceForPin(1);
        if (normalSource)
        {
            if (normalSource->Type == ShaderGraphNodeType::TextureSample ||
                normalSource->Type == ShaderGraphNodeType::NormalMap)
            {
                if (!normalSource->TexturePath.empty() && !normalSource->TextureRef)
                    normalSource->TextureRef = Texture2D::Create(normalSource->TexturePath);
                NormalMap = normalSource->TextureRef;
            }
        }

        // Metallic (pin 2)
        ShaderGraphNode* metallicSource = findSourceForPin(2);
        if (metallicSource)
        {
            if (metallicSource->Type == ShaderGraphNodeType::Float)
                Metallic = metallicSource->FloatValue;
            else if (metallicSource->Type == ShaderGraphNodeType::TextureSample)
            {
                if (!metallicSource->TexturePath.empty() && !metallicSource->TextureRef)
                    metallicSource->TextureRef = Texture2D::Create(metallicSource->TexturePath);
                MetallicMap = metallicSource->TextureRef;
            }
        }

        // Roughness (pin 3)
        ShaderGraphNode* roughnessSource = findSourceForPin(3);
        if (roughnessSource)
        {
            if (roughnessSource->Type == ShaderGraphNodeType::Float)
                Roughness = roughnessSource->FloatValue;
            else if (roughnessSource->Type == ShaderGraphNodeType::TextureSample)
            {
                if (!roughnessSource->TexturePath.empty() && !roughnessSource->TextureRef)
                    roughnessSource->TextureRef = Texture2D::Create(roughnessSource->TexturePath);
                RoughnessMap = roughnessSource->TextureRef;
            }
        }

        // AO (pin 4)
        ShaderGraphNode* aoSource = findSourceForPin(4);
        if (aoSource)
        {
            if (aoSource->Type == ShaderGraphNodeType::Float)
                AO = aoSource->FloatValue;
            else if (aoSource->Type == ShaderGraphNodeType::TextureSample)
            {
                if (!aoSource->TexturePath.empty() && !aoSource->TextureRef)
                    aoSource->TextureRef = Texture2D::Create(aoSource->TexturePath);
                AOMap = aoSource->TextureRef;
            }
        }

        // Emission (pin 5)
        ShaderGraphNode* emissionSource = findSourceForPin(5);
        if (emissionSource)
        {
            if (emissionSource->Type == ShaderGraphNodeType::Color)
            {
                EmissionColor = glm::vec3(emissionSource->ColorValue);
                EmissionStrength = emissionSource->ColorValue.a;
            }
            else if (emissionSource->Type == ShaderGraphNodeType::Vec3)
            {
                EmissionColor = emissionSource->Vec3Value;
                EmissionStrength = 1.0f;
            }
            else if (emissionSource->Type == ShaderGraphNodeType::TextureSample)
            {
                if (!emissionSource->TexturePath.empty() && !emissionSource->TextureRef)
                    emissionSource->TextureRef = Texture2D::Create(emissionSource->TexturePath);
                EmissionMap = emissionSource->TextureRef;
                EmissionStrength = 1.0f;
            }
        }
    }

    std::shared_ptr<Material> Material::CreateDefault()
    {
        auto material = std::make_shared<Material>();
        material->Name = "Default-Material";
        material->Albedo = glm::vec3(0.8f, 0.8f, 0.8f);
        material->Metallic  = 0.0f;
        material->Roughness = 0.5f;
        material->AO        = 1.0f;
        return material;
    }
}
