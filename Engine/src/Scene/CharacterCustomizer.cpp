#include "CharacterCustomizer.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Animation/Skeleton.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Serialization/SceneSerializer.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        enum class BoneKind : uint8_t { Other = 0, Head, Spine, Arm, Leg };

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        BoneKind KindOf(const std::string& name)
        {
            const std::string n = Lower(name);
            auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
            if (has("head") && !has("forehead"))
                return BoneKind::Head;
            if (has("hand") || has("finger") || has("thumb") || has("foot") || has("toe"))
                return BoneKind::Other;
            if (has("arm") || has("shoulder") || has("clavicle") || has("elbow"))
                return BoneKind::Arm;
            if (has("leg") || has("thigh") || has("calf") || has("shin") || has("knee"))
                return BoneKind::Leg;
            if (has("spine") || has("chest") || has("hip") || has("pelvis") || has("torso"))
                return BoneKind::Spine;
            return BoneKind::Other;
        }

        // İskelet başına kemik türleri (önbellek)
        const std::vector<BoneKind>& Kinds(const Skeleton& skeleton)
        {
            static std::unordered_map<const Skeleton*, std::vector<BoneKind>> cache;
            auto& kinds = cache[&skeleton];
            const auto& nodes = Animator::Flatten(skeleton);
            if (kinds.size() != nodes.size())
            {
                kinds.resize(nodes.size());
                for (size_t i = 0; i < nodes.size(); ++i)
                    kinds[i] = nodes[i].Node ? KindOf(nodes[i].Node->Name) : BoneKind::Other;
            }
            return kinds;
        }

        bool Matches(const std::string& name, const std::string& filter)
        {
            if (filter == "*")
                return true;
            const std::string n = Lower(name);
            std::stringstream ss(filter);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                item.erase(std::remove_if(item.begin(), item.end(), [](unsigned char c) { return std::isspace(c); }), item.end());
                if (!item.empty() && n.find(Lower(item)) != std::string::npos)
                    return true;
            }
            return false;
        }

        uint64_t Signature(const CharacterCustomizerComponent& c, const Model* model)
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](const void* p, size_t n) {
                const auto* b = (const unsigned char*)p;
                for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            };
            mix(&c.SkinTone, sizeof(float));
            mix(&c.HairColor, sizeof(glm::vec3));
            mix(&c.EyeColor, sizeof(glm::vec3));
            mix(&c.SkinShading, sizeof(bool));
            mix(c.SkinMaterials.data(), c.SkinMaterials.size());
            mix(c.HairMaterials.data(), c.HairMaterials.size());
            mix(c.EyeMaterials.data(), c.EyeMaterials.size());
            mix(c.OutfitTexture.data(), c.OutfitTexture.size());
            mix(c.OutfitMaterials.data(), c.OutfitMaterials.size());
            mix(&c.OutfitRoughness, sizeof(float));
            mix(&c.OutfitKeepNormalMap, sizeof(bool));
            mix(&model, sizeof(model));
            return h | 1ull;
        }

    }

    namespace CharacterCustomizer
    {
        glm::vec3 SkinColor(float tone)
        {
            static const glm::vec3 kTones[] = {
                { 1.00f, 0.87f, 0.78f }, { 0.95f, 0.75f, 0.62f }, { 0.80f, 0.58f, 0.44f },
                { 0.60f, 0.40f, 0.28f }, { 0.40f, 0.26f, 0.18f }, { 0.26f, 0.17f, 0.12f } };
            const float t = std::clamp(tone, 0.0f, 1.0f) * 5.0f;
            const int i = std::min((int)t, 4);
            return glm::mix(kTones[i], kTones[i + 1], t - (float)i);
        }

        void ApplyToPose(const CharacterCustomizerComponent& c, const Skeleton& skeleton, Pose& pose)
        {
            const auto& nodes = Animator::Flatten(skeleton);
            if (pose.size() < nodes.size() || nodes.empty())
                return;
            const auto& kinds = Kinds(skeleton);
            const float height = std::clamp(c.Height, 0.2f, 3.0f);
            const float head = std::clamp(c.HeadSize, 0.3f, 3.0f);
            const float width = std::clamp(c.BodyWidth, 0.7f, 1.4f);
            const float arm = std::clamp(c.ArmLength, 0.5f, 2.0f);
            const float leg = std::clamp(c.LegLength, 0.5f, 2.0f);
            // Kök düğüm (model orijini, ayak hizası): tüm iskelet boyla ölçeklenir
            pose[0].Scale *= height;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const int parent = nodes[i].Parent;
                const BoneKind parentKind = parent >= 0 ? kinds[(size_t)parent] : BoneKind::Other;
                // Uzunluk: kolun / bacağın çocuklarının yerel ötelemesi uzar (kalınlık değişmez)
                if (parentKind == BoneKind::Arm && arm != 1.0f)
                    pose[i].Translation *= arm;
                if (parentKind == BoneKind::Leg && leg != 1.0f)
                    pose[i].Translation *= leg;
                if (kinds[i] == BoneKind::Head && parentKind != BoneKind::Head)
                    pose[i].Scale *= head;
                if (kinds[i] == BoneKind::Spine && width != 1.0f)
                {
                    pose[i].Scale.x *= width;
                    pose[i].Scale.z *= width;
                }
                // Gövde genişliği çocuklara geçmesin (kollar / baş / bacaklar şişmesin)
                if (parentKind == BoneKind::Spine && kinds[i] != BoneKind::Spine && width != 1.0f)
                {
                    pose[i].Scale.x /= width;
                    pose[i].Scale.z /= width;
                }
            }
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<CharacterCustomizerComponent, ModelComponent>();
            for (auto h : view)
            {
                auto& c = view.get<CharacterCustomizerComponent>(h);
                auto& mc = view.get<ModelComponent>(h);
                if (!mc.ModelData)
                    continue;
                // Model değiştiyse (yeni dosya / sahne yükleme / kopyalanan bileşen) örnek kopyası yeniden kurulur
                if (mc.ModelData != c.InstanceModel)
                {
                    c.InstanceModel = std::make_shared<Model>(*mc.ModelData);
                    for (auto& mat : c.InstanceModel->Materials)
                        if (mat)
                            mat = std::make_shared<Material>(*mat);
                    mc.ModelData = c.InstanceModel;
                    c.AppliedSignature = 0;
                }
                const uint64_t sig = Signature(c, c.InstanceModel.get());
                if (sig == c.AppliedSignature)
                    continue;
                const glm::vec3 skin = SkinColor(c.SkinTone);
                // Kıyafet dokusu (yüklenemezse yok sayılır)
                std::shared_ptr<Texture2D> outfit;
                if (!c.OutfitTexture.empty())
                {
                    outfit = Texture2D::Create(SceneSerializer::ResolveAssetPath(c.OutfitTexture));
                    if (outfit && (outfit->GetRendererID() == 0 || outfit->GetWidth() <= 1))
                        outfit.reset();
                }
                for (auto& mat : c.InstanceModel->Materials)
                {
                    if (!mat)
                        continue;
                    if (outfit && Matches(mat->Name, c.OutfitMaterials))
                    {
                        mat->AlbedoMap = outfit;
                        mat->Albedo = glm::vec3(1.0f);
                        mat->Roughness = c.OutfitRoughness;
                        mat->Metallic = 0.0f;
                        mat->Shading = Material::ShadingModel::DefaultLit;
                        if (!c.OutfitKeepNormalMap)
                            mat->NormalMap = nullptr;
                    }
                    else if (Matches(mat->Name, c.HairMaterials))
                    {
                        mat->Albedo = c.HairColor;
                        mat->Shading = Material::ShadingModel::Hair;
                        mat->SubsurfaceColor = glm::vec3(1.0f, 0.85f, 0.7f);
                    }
                    else if (Matches(mat->Name, c.EyeMaterials))
                        mat->Albedo = c.EyeColor;
                    else if (Matches(mat->Name, c.SkinMaterials))
                    {
                        mat->Albedo = skin;
                        if (c.SkinShading)
                        {
                            mat->Shading = Material::ShadingModel::Subsurface;
                            mat->SubsurfaceColor = glm::vec3(0.85f, 0.32f, 0.22f);
                            mat->SubsurfaceAmount = 0.6f;
                        }
                        else
                            mat->Shading = Material::ShadingModel::DefaultLit;
                    }
                }
                c.AppliedSignature = sig;
            }
        }
    }

    void RegisterCharacterCustomizerComponents()
    {
        using C = CharacterCustomizerComponent;
        ComponentDef<C>("Character Customizer", "CharacterCustomizerComponent", "Animation", ICON_FA_USER_GEAR)
            .Describe("MetaHuman Creator-style customization of a skinned character: height, head size, body width, arm and leg length (bone scaling), "
                      "skin tone, hair and eye colors. Skin materials switch to the Subsurface (skin) shading model, hair to Hair.")
            .Field("Height", "Height", &C::Height).Range(0.5f, 2.0f, 0.005f)
            .Field("HeadSize", "Head Size", &C::HeadSize).Range(0.5f, 2.0f, 0.005f)
            .Field("BodyWidth", "Body Width", &C::BodyWidth).Range(0.7f, 1.4f, 0.005f)
            .Field("ArmLength", "Arm Length", &C::ArmLength).Range(0.6f, 1.6f, 0.005f)
            .Field("LegLength", "Leg Length", &C::LegLength).Range(0.6f, 1.6f, 0.005f)
            .Field("SkinTone", "Skin Tone", &C::SkinTone).Range(0.0f, 1.0f, 0.005f).Tooltip("Light to dark")
            .Field("HairColor", "Hair Color", &C::HairColor, FieldKind::Color3)
            .Field("EyeColor", "Eye Color", &C::EyeColor, FieldKind::Color3)
            .Field("SkinShading", "Skin Shading (Subsurface)", &C::SkinShading)
            .Field("SkinMaterials", "Skin Materials", &C::SkinMaterials).Tooltip("Comma separated parts of material names, * = all")
            .Field("HairMaterials", "Hair Materials", &C::HairMaterials)
            .Field("EyeMaterials", "Eye Materials", &C::EyeMaterials)
            .Field("OutfitTexture", "Outfit Texture", &C::OutfitTexture, FieldKind::Asset)
                .Tooltip("Base color texture painted in the model's UV space (clothes, shoes, hair); replaces the skin tint on the matching materials")
            .Field("OutfitMaterials", "Outfit Materials", &C::OutfitMaterials).Tooltip("Comma separated parts of material names, * = all")
            .Field("OutfitRoughness", "Outfit Roughness", &C::OutfitRoughness).Range(0.0f, 1.0f, 0.01f)
            .Field("OutfitKeepNormalMap", "Keep Model Normal Map", &C::OutfitKeepNormalMap)
                .Tooltip("Off: the outfit replaces the model's own normal map (e.g. a costume's seams and muscles)");
    }
}
