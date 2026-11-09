#include "AnimationAssets.h"

#include "Core/Animation/AnimationClip.h"
#include "Core/Animation/Skeleton.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/RigConstraints.h"
#include "Scene/Scene.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<PoseAssetComponent>(Entity, PoseAssetComponent&) {}
    template<> void Scene::OnComponentAdded<AimOffsetComponent>(Entity, AimOffsetComponent&) {}
    template<> void Scene::OnComponentAdded<IKRigComponent>(Entity, IKRigComponent&) {}
    template<> void Scene::OnComponentAdded<RetargetPlayerComponent>(Entity, RetargetPlayerComponent&) {}

    namespace AnimationAssets
    {
        namespace
        {
            int FindNode(const std::vector<FlatAnimNode>& nodes, const std::string& name)
            {
                for (size_t i = 0; i < nodes.size(); ++i)
                    if (nodes[i].Node && nodes[i].Node->Name == name)
                        return (int)i;
                return -1;
            }

            std::shared_ptr<Model> LoadModel(const std::string& path)
            {
                return path.empty() ? nullptr : ModelLoader::Load(SceneSerializer::ResolveAssetPath(path));
            }

            std::string ModelOf(const std::string& skeletonOrModel)
            {
                if (SimpleAssets::IsKind(skeletonOrModel, "Skeleton"))
                {
                    SimpleAssets::Data data;
                    return SimpleAssets::Load(skeletonOrModel, data) ? data.Str("Model") : std::string();
                }
                return skeletonOrModel;
            }

            const AnimationClip* FindClip(const Model& model, const std::string& name)
            {
                for (const auto& clip : model.Animations)
                    if (clip && clip->Name == name)
                        return clip.get();
                return nullptr;
            }

            void ApplyPoseAsset(const PoseAssetComponent& c, const std::vector<FlatAnimNode>& nodes, Pose& pose)
            {
                SimpleAssets::Data data;
                if (c.Asset.empty() || !SimpleAssets::Load(c.Asset, data))
                    return;
                const float w = std::clamp(c.Weight, 0.0f, 1.0f);
                for (const auto& row : data.Table("Bones"))
                {
                    if (SimpleAssets::Cell(row, 0) != c.PoseName)
                        continue;
                    const int i = FindNode(nodes, SimpleAssets::Cell(row, 1));
                    if (i < 0 || i >= (int)pose.size())
                        continue;
                    const glm::vec3 t(SimpleAssets::CellFloat(row, 2), SimpleAssets::CellFloat(row, 3), SimpleAssets::CellFloat(row, 4));
                    const glm::quat q(SimpleAssets::CellFloat(row, 8, 1.0f), SimpleAssets::CellFloat(row, 5), SimpleAssets::CellFloat(row, 6), SimpleAssets::CellFloat(row, 7));
                    pose[(size_t)i].Translation = glm::mix(pose[(size_t)i].Translation, t, w);
                    pose[(size_t)i].Rotation = glm::slerp(pose[(size_t)i].Rotation, q, w);
                }
            }

            void ApplyAimOffset(const AimOffsetComponent& c, const Skeleton& skeleton, Pose& pose)
            {
                SimpleAssets::Data data;
                if (c.Asset.empty() || !SimpleAssets::Load(c.Asset, data))
                    return;
                auto model = LoadModel(data.Str("Model"));
                if (!model || !model->SkeletonData || model->SkeletonData.get() != &skeleton)
                    return;
                const AnimationClip* baseClip = FindClip(*model, data.Str("Base Clip"));
                if (!baseClip)
                    return;
                static thread_local Pose s_Base, s_Sample;
                Animator::SamplePose(skeleton, *baseClip, 0.0f, false, s_Base);
                float total = 0.0f;
                struct Hit { const AnimationClip* Clip; float Weight; };
                std::vector<Hit> hits;
                for (const auto& row : data.Table("Samples"))
                {
                    const AnimationClip* clip = FindClip(*model, SimpleAssets::Cell(row, 0));
                    if (!clip)
                        continue;
                    const float dx = (SimpleAssets::CellFloat(row, 1) - c.Yaw) / 90.0f, dy = (SimpleAssets::CellFloat(row, 2) - c.Pitch) / 90.0f;
                    const float d2 = dx * dx + dy * dy;
                    const float w = 1.0f / std::max(d2 * d2, 1e-5f);
                    hits.push_back({ clip, w });
                    total += w;
                }
                if (total <= 0.0f)
                    return;
                for (const auto& h : hits)
                {
                    Animator::SamplePose(skeleton, *h.Clip, 0.0f, false, s_Sample);
                    Animator::ApplyAdditive(pose, s_Sample, s_Base, h.Weight / total * std::clamp(c.Weight, 0.0f, 1.0f));
                }
            }

            void ApplyRetarget(RetargetPlayerComponent& c, const Skeleton& skeleton, Pose& pose, float dt)
            {
                SimpleAssets::Data data;
                if (c.Retargeter.empty() || !SimpleAssets::Load(c.Retargeter, data))
                    return;
                auto source = LoadModel(ModelOf(data.Str("Source Skeleton")));
                if (!source || !source->SkeletonData)
                    return;
                const AnimationClip* clip = FindClip(*source, data.Str("Clip"));
                if (!clip)
                    return;
                if (c.Playing)
                    c.Time += dt * c.Speed;
                const Skeleton& srcSkeleton = *source->SkeletonData;
                const auto& srcNodes = Animator::Flatten(srcSkeleton);
                const auto& dstNodes = Animator::Flatten(skeleton);
                static thread_local Pose s_SrcPose, s_SrcBind, s_DstBind;
                Animator::SamplePose(srcSkeleton, *clip, c.Time, true, s_SrcPose);
                Animator::BindPose(srcSkeleton, s_SrcBind);
                Animator::BindPose(skeleton, s_DstBind);
                std::vector<std::pair<int, int>> map;
                const auto& rows = data.Table("Bone Map");
                if (rows.empty())
                {
                    for (size_t i = 0; i < srcNodes.size(); ++i)
                        if (srcNodes[i].Node)
                            if (int d = FindNode(dstNodes, srcNodes[i].Node->Name); d >= 0)
                                map.emplace_back((int)i, d);
                }
                else
                    for (const auto& row : rows)
                    {
                        const int s = FindNode(srcNodes, SimpleAssets::Cell(row, 0)), d = FindNode(dstNodes, SimpleAssets::Cell(row, 1));
                        if (s >= 0 && d >= 0)
                            map.emplace_back(s, d);
                    }
                const float w = std::clamp(c.Weight, 0.0f, 1.0f);
                const float scale = data.Float("Translation Scale", 1.0f);
                for (const auto& [s, d] : map)
                {
                    // Bağlama pozuna göre fark: kaynak kemiğin bağ poza göre dönüşü hedefin bağ pozuna uygulanır
                    const glm::quat delta = glm::inverse(s_SrcBind[(size_t)s].Rotation) * s_SrcPose[(size_t)s].Rotation;
                    const glm::quat target = s_DstBind[(size_t)d].Rotation * delta;
                    pose[(size_t)d].Rotation = glm::slerp(pose[(size_t)d].Rotation, target, w);
                }
                // Kök hareketi: kaynağın bağ pozundan farklı oynayan kemikler (kalça / kök) hedefe aktarılır
                if (data.Bool("Copy Root Translation", false))
                    for (const auto& [s, d] : map)
                    {
                        const glm::vec3 delta = s_SrcPose[(size_t)s].Translation - s_SrcBind[(size_t)s].Translation;
                        if (glm::length(delta) > 1e-4f)
                            pose[(size_t)d].Translation = glm::mix(pose[(size_t)d].Translation, s_DstBind[(size_t)d].Translation + delta * scale, w);
                    }
            }
        }

        void Apply(Scene& scene, uint32_t entity, const Skeleton& skeleton, Pose& pose, float dt)
        {
            auto& reg = scene.m_Registry;
            const auto e = (entt::entity)entity;
            auto* poseAsset = reg.try_get<PoseAssetComponent>(e);
            auto* aim = reg.try_get<AimOffsetComponent>(e);
            auto* retarget = reg.try_get<RetargetPlayerComponent>(e);
            if (!poseAsset && !aim && !retarget)
                return;
            const auto& nodes = Animator::Flatten(skeleton);
            if (nodes.empty() || pose.size() < nodes.size())
                return;
            if (retarget)
                ApplyRetarget(*retarget, skeleton, pose, dt);
            if (poseAsset)
                ApplyPoseAsset(*poseAsset, nodes, pose);
            if (aim)
                ApplyAimOffset(*aim, skeleton, pose);
        }

        void Update(Scene& scene)
        {
            // IK Rig: zincirler oyun başında Chain IK kısıtlarına dönüşür (hedef nesne adıyla bulunur)
            std::vector<entt::entity> pending;
            for (auto e : scene.m_Registry.view<IKRigComponent>())
                if (!scene.m_Registry.get<IKRigComponent>(e).Built)
                    pending.push_back(e);
            for (auto e : pending)
            {
                auto& rig = scene.m_Registry.get<IKRigComponent>(e);
                rig.Built = true;
                SimpleAssets::Data data;
                if (rig.Rig.empty() || !SimpleAssets::Load(rig.Rig, data))
                    continue;
                Entity owner{ e, &scene };
                if (!owner.HasComponent<RigComponent>())
                    owner.AddComponent<RigComponent>().Weight = rig.Weight;
                for (const auto& row : data.Table("Chains"))
                {
                    Entity target{};
                    const std::string targetName = SimpleAssets::Cell(row, 3);
                    for (auto t : scene.m_Registry.view<TagComponent>())
                        if (scene.m_Registry.get<TagComponent>(t).Tag == targetName)
                        {
                            target = Entity{ t, &scene };
                            break;
                        }
                    if (!target)
                    {
                        CQ_CORE_WARN("[IK Rig] Target object '{0}' not found", SimpleAssets::Cell(row, 3));
                        continue;
                    }
                    Entity child = scene.CreateEntity("IK Chain: " + SimpleAssets::Cell(row, 0));
                    child.SetParent(owner);
                    auto& chain = child.AddComponent<ChainIKConstraintComponent>();
                    chain.Root = SimpleAssets::Cell(row, 1);
                    chain.Tip = SimpleAssets::Cell(row, 2);
                    chain.Target = target.GetComponent<IDComponent>().ID;
                    chain.Weight = SimpleAssets::CellFloat(row, 4, 1.0f);
                }
            }
        }

        std::vector<std::string> BoneNames(const std::string& modelPath)
        {
            std::vector<std::string> out;
            auto model = LoadModel(ModelOf(modelPath));
            if (!model || !model->SkeletonData)
                return out;
            for (const auto& n : Animator::Flatten(*model->SkeletonData))
                if (n.BoneIndex >= 0 && n.Node)
                    out.push_back(n.Node->Name);
            return out;
        }

        std::vector<std::string> ClipNames(const std::string& modelPath)
        {
            std::vector<std::string> out;
            auto model = LoadModel(ModelOf(modelPath));
            if (model)
                for (const auto& clip : model->Animations)
                    if (clip)
                        out.push_back(clip->Name);
            return out;
        }

        bool CapturePose(const std::string& assetPath, const std::string& modelPath, const std::string& clipName, float time, const std::string& poseName)
        {
            auto model = LoadModel(ModelOf(modelPath));
            const auto* def = SimpleAssets::ForKind("PoseAsset");
            SimpleAssets::Data data;
            if (!model || !model->SkeletonData || !def || !SimpleAssets::Load(assetPath, data))
                return false;
            const AnimationClip* clip = FindClip(*model, clipName);
            if (!clip)
                return false;
            Pose pose;
            Animator::SamplePose(*model->SkeletonData, *clip, time, false, pose);
            const auto& nodes = Animator::Flatten(*model->SkeletonData);
            auto& rows = data.Tables["Bones"];
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const std::vector<std::string>& r) { return SimpleAssets::Cell(r, 0) == poseName; }), rows.end());
            char v[9][32];
            for (size_t i = 0; i < nodes.size() && i < pose.size(); ++i)
            {
                if (nodes[i].BoneIndex < 0 || !nodes[i].Node)
                    continue;
                const auto& p = pose[i];
                const float values[7] = { p.Translation.x, p.Translation.y, p.Translation.z, p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w };
                std::vector<std::string> row = { poseName, nodes[i].Node->Name };
                for (int k = 0; k < 7; ++k)
                {
                    std::snprintf(v[k], sizeof(v[k]), "%.5f", values[k]);
                    row.push_back(v[k]);
                }
                rows.push_back(std::move(row));
            }
            return SimpleAssets::Save(assetPath, *def, data);
        }

        void RegisterComponents()
        {
            {
                using C = PoseAssetComponent;
                ComponentDef<C>("Pose Asset Player", "PoseAssetComponent", "Animation", ICON_FA_PERSON)
                    .Describe("Blends a named pose from a Pose Asset (.cqpose) over the animation, for facial or hand poses.")
                    .Field("Asset", "Asset", &C::Asset, FieldKind::Asset).Filter(".cqpose")
                    .Field("PoseName", "Pose", &C::PoseName)
                    .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f);
            }
            {
                using C = AimOffsetComponent;
                ComponentDef<C>("Aim Offset Player", "AimOffsetComponent", "Animation", ICON_FA_CROSSHAIRS)
                    .Describe("Layers an Aim Offset (.cqaimoffset) over the animation: Yaw and Pitch pick additive poses.")
                    .Field("Asset", "Asset", &C::Asset, FieldKind::Asset).Filter(".cqaimoffset")
                    .Field("Yaw", "Yaw", &C::Yaw).Range(-180.0f, 180.0f, 0.5f)
                    .Field("Pitch", "Pitch", &C::Pitch).Range(-90.0f, 90.0f, 0.5f)
                    .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f);
            }
            {
                using C = IKRigComponent;
                ComponentDef<C>("IK Rig", "IKRigComponent", "Animation", ICON_FA_DIAGRAM_NEXT)
                    .Describe("Solves the chains of an IK Rig (.cqikrig) to their target objects while the game plays.")
                    .Field("Rig", "Rig", &C::Rig, FieldKind::Asset).Filter(".cqikrig")
                    .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f);
            }
            {
                using C = RetargetPlayerComponent;
                ComponentDef<C>("Retarget Player", "RetargetPlayerComponent", "Animation", ICON_FA_PERSON_RUNNING)
                    .Describe("Plays a clip from another skeleton on this model through an IK Retargeter (.cqretarget).")
                    .Field("Retargeter", "Retargeter", &C::Retargeter, FieldKind::Asset).Filter(".cqretarget")
                    .Field("Playing", "Playing", &C::Playing)
                    .Field("Speed", "Speed", &C::Speed)
                    .Field("Weight", "Weight", &C::Weight).Range(0.0f, 1.0f, 0.01f);
            }
        }
    }
}
