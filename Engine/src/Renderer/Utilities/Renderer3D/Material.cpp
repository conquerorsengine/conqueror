#include "Material.h"
#include "Core/Time/TimeManager.h"

namespace Conqueror
{
    Material::Material()
    {
        CreateDefaultTextures();
    }

    void Material::CreateDefaultTextures()
    {
        // 1x1 beyaz texture (default albedo, metallic, roughness, AO için)
        uint32_t whiteData = 0xFFFFFFFF;
        m_WhiteTexture = Texture2D::Create(1, 1);
        m_WhiteTexture->SetData(&whiteData, sizeof(uint32_t));

        // 1x1 normal map (128, 128, 255 = yukarı bakan normal)
        uint32_t normalData = 0xFFFF8080; // RGBA: 128, 128, 255, 255
        m_DefaultNormalMap = Texture2D::Create(1, 1);
        m_DefaultNormalMap->SetData(&normalData, sizeof(uint32_t));

        // 1x1 siyah texture (emission default)
        uint32_t blackData = 0xFF000000;
        m_BlackTexture = Texture2D::Create(1, 1);
        m_BlackTexture->SetData(&blackData, sizeof(uint32_t));
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
            shader->SetFloat("u_Time", TimeManager::GetTime());

        for (const auto& node : GraphNodes)
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
                case ShaderGraphNodeType::TextureSample:
                case ShaderGraphNodeType::NormalMap:
                    if (node.TextureRef)
                    {
                        node.TextureRef->Bind(8);
                        shader->SetInt("u_Node" + idStr + "_Tex", 8);
                    }
                    break;
                default:
                    break;
            }
        }
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
                Albedo = glm::vec3(albedoSource->ColorValue);
                AlbedoMap = nullptr;
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
                AlbedoMap = nullptr;
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
