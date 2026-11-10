#include "BlendShapes.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Animation/Animator.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/SkinnedMesh.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"

#include <glad/glad.h>
#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr int kRowTexels = 1024;   // Skinning.glsl ile aynı

        struct MeshDeltas
        {
            GLuint Texture = 0;
            int Rows = 0;
        };

        struct Entry
        {
            const Model* ModelPtr = nullptr;
            std::vector<float> LastWeights;
            std::vector<MeshDeltas> Meshes;
            std::vector<uint32_t> Bound;     // çizimde kullanılan (0: bu ağda etkin ağırlık yok)
            uint64_t Seen = 0;
            bool Valid = false;
        };

        std::unordered_map<const Scene*, std::unordered_map<uint32_t, Entry>>& Entries()
        {
            static auto& m = *new std::unordered_map<const Scene*, std::unordered_map<uint32_t, Entry>>();
            return m;
        }
        uint64_t s_UpdateCounter = 0;

        void Release(Entry& e)
        {
            for (auto& m : e.Meshes)
                if (m.Texture) glDeleteTextures(1, &m.Texture);
            e.Meshes.clear();
            e.Bound.clear();
        }

        const Model* MorphModel(Scene& scene, entt::entity e)
        {
            const auto* mr = scene.m_Registry.try_get<MeshRendererComponent>(e);
            if (!mr || !mr->ModelData || !mr->ModelData->IsSkinned || !mr->ModelData->SkeletonData || mr->ModelData->SkeletonData->MorphNames.empty())
                return nullptr;
            return mr->ModelData.get();
        }

        // Etkin ağırlıklar (MorphNames sırası): animasyon pozu + bileşen
        void EffectiveWeights(Scene& scene, entt::entity e, const Model& model, std::vector<float>& out)
        {
            const Skeleton& skel = *model.SkeletonData;
            out.assign(skel.MorphNames.size(), 0.0f);
            auto& reg = scene.m_Registry;
            const Pose* pose = nullptr;
            if (const auto* ac = reg.try_get<AnimationComponent>(e); ac && !ac->FinalPose.empty())
                pose = &ac->FinalPose;
            else if (const auto* an = reg.try_get<AnimatorComponent>(e); an && !an->FinalPose.empty())
                pose = &an->FinalPose;
            if (pose)
            {
                static thread_local std::vector<float> s_Anim;
                Animator::PoseMorphWeights(skel, *pose, s_Anim);
                for (size_t i = 0; i < out.size() && i < s_Anim.size(); ++i)
                    out[i] = s_Anim[i];
            }
            if (auto* bs = reg.try_get<BlendShapesComponent>(e))
            {
                for (const auto& w : bs->Weights)
                {
                    auto it = std::find(skel.MorphNames.begin(), skel.MorphNames.end(), w.Name);
                    if (it != skel.MorphNames.end())
                        out[(size_t)(it - skel.MorphNames.begin())] += w.Weight;
                }
            }
        }
    }

    namespace BlendShapes
    {
        void Update(Scene& scene)
        {
            ++s_UpdateCounter;
            auto& entries = Entries()[&scene];
            auto& reg = scene.m_Registry;
            static thread_local std::vector<float> s_Weights;
            static thread_local std::vector<glm::vec4> s_Staging;
            for (auto e : reg.view<MeshRendererComponent>())
            {
                if (!scene.IsEntityActive(e))
                    continue;
                const Model* model = MorphModel(scene, e);
                if (!model)
                    continue;
                Entry& entry = entries[(uint32_t)e];
                entry.Seen = s_UpdateCounter;
                if (entry.ModelPtr != model)
                {
                    Release(entry);
                    entry.ModelPtr = model;
                    entry.LastWeights.clear();
                    entry.Meshes.resize(model->SkinnedMeshes.size());
                    entry.Bound.assign(model->SkinnedMeshes.size(), 0u);
                    entry.Valid = false;
                }
                EffectiveWeights(scene, e, *model, s_Weights);
                int active = 0;
                for (float w : s_Weights)
                    active += std::abs(w) > 1e-5f ? 1 : 0;
                if (auto* bs = reg.try_get<BlendShapesComponent>(e))
                {
                    bs->Count = (int)model->SkeletonData->MorphNames.size();
                    bs->Active = active;
                }
                if (entry.Valid && s_Weights == entry.LastWeights)
                    continue;
                entry.LastWeights = s_Weights;
                entry.Valid = true;
                for (size_t m = 0; m < model->SkinnedMeshes.size(); ++m)
                {
                    const auto& mesh = model->SkinnedMeshes[m];
                    entry.Bound[m] = 0;
                    if (!mesh || mesh->GetMorphTargets().empty())
                        continue;
                    bool any = false;
                    for (const auto& t : mesh->GetMorphTargets())
                        if (t.Channel >= 0 && (size_t)t.Channel < s_Weights.size() && std::abs(s_Weights[(size_t)t.Channel]) > 1e-5f)
                            any = true;
                    if (!any)
                        continue;
                    // Yoğun fark dizisi: köşe başına konum + normal
                    const int texels = (int)mesh->GetVertexCount() * 2;
                    const int rows = std::max(1, (texels + kRowTexels - 1) / kRowTexels);
                    s_Staging.assign((size_t)rows * kRowTexels, glm::vec4(0.0f));
                    for (const auto& t : mesh->GetMorphTargets())
                    {
                        if (t.Channel < 0 || (size_t)t.Channel >= s_Weights.size())
                            continue;
                        const float w = s_Weights[(size_t)t.Channel];
                        if (std::abs(w) <= 1e-5f)
                            continue;
                        for (size_t i = 0; i < t.Vertices.size(); ++i)
                        {
                            const size_t base = (size_t)t.Vertices[i] * 2;
                            s_Staging[base] += glm::vec4(t.DeltaPositions[i] * w, 0.0f);
                            s_Staging[base + 1] += glm::vec4(t.DeltaNormals[i] * w, 0.0f);
                        }
                    }
                    MeshDeltas& d = entry.Meshes[m];
                    if (!d.Texture || d.Rows < rows)
                    {
                        if (d.Texture) glDeleteTextures(1, &d.Texture);
                        glCreateTextures(GL_TEXTURE_2D, 1, &d.Texture);
                        glTextureStorage2D(d.Texture, 1, GL_RGBA32F, kRowTexels, rows);
                        glTextureParameteri(d.Texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                        glTextureParameteri(d.Texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                        d.Rows = rows;
                    }
                    glTextureSubImage2D(d.Texture, 0, 0, 0, kRowTexels, rows, GL_RGBA, GL_FLOAT, s_Staging.data());
                    entry.Bound[m] = d.Texture;
                }
            }
            // Bu güncellemede görülmeyen (silinen / model değişen) varlıklar
            for (auto it = entries.begin(); it != entries.end();)
            {
                if (it->second.Seen + 2 < s_UpdateCounter)
                {
                    Release(it->second);
                    it = entries.erase(it);
                }
                else
                    ++it;
            }
        }

        const std::vector<uint32_t>* GetTextures(const Scene& scene, uint32_t entity)
        {
            auto s = Entries().find(&scene);
            if (s == Entries().end())
                return nullptr;
            auto e = s->second.find(entity);
            return e == s->second.end() ? nullptr : &e->second.Bound;
        }

        void Forget(const Scene& scene)
        {
            auto s = Entries().find(&scene);
            if (s == Entries().end())
                return;
            for (auto& [id, e] : s->second)
                Release(e);
            Entries().erase(s);
        }

        const std::vector<std::string>* Names(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e))
                return nullptr;
            const Model* model = MorphModel(scene, e);
            return model ? &model->SkeletonData->MorphNames : nullptr;
        }

        bool SetWeight(Scene& scene, uint32_t entity, const std::string& name, float weight)
        {
            const entt::entity e = (entt::entity)entity;
            const auto* names = Names(scene, entity);
            if (!names || std::find(names->begin(), names->end(), name) == names->end())
                return false;
            auto& bs = scene.m_Registry.all_of<BlendShapesComponent>(e) ? scene.m_Registry.get<BlendShapesComponent>(e)
                                                                         : scene.m_Registry.emplace<BlendShapesComponent>(e);
            for (auto& w : bs.Weights)
                if (w.Name == name)
                {
                    w.Weight = weight;
                    return true;
                }
            bs.Weights.push_back({ name, weight });
            return true;
        }

        float GetWeight(Scene& scene, uint32_t entity, const std::string& name)
        {
            const entt::entity e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e))
                return 0.0f;
            const Model* model = MorphModel(scene, e);
            if (!model)
                return 0.0f;
            std::vector<float> weights;
            EffectiveWeights(scene, e, *model, weights);
            const auto& names = model->SkeletonData->MorphNames;
            auto it = std::find(names.begin(), names.end(), name);
            return it == names.end() ? 0.0f : weights[(size_t)(it - names.begin())];
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            // SetBlendShapeWeight(varlık, "Ad", ağırlık): bileşendeki ağırlık (animasyona eklenir)
            vm->DefineNative("SetBlendShapeWeight", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                return Value::MakeBool(s && argc > 2 && args[0].IsNumber() && args[1].IsString() &&
                                       SetWeight(*s, (uint32_t)(int64_t)args[0].ToNumber(), args[1].AsString()->Value, (float)args[2].ToNumber()));
            }, 3);
            // GetBlendShapeWeight(varlık, "Ad") -> etkin ağırlık (animasyon + bileşen)
            vm->DefineNative("GetBlendShapeWeight", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                if (!s || argc < 2 || !args[0].IsNumber() || !args[1].IsString()) return Value::MakeFloat(0.0);
                return Value::MakeFloat(GetWeight(*s, (uint32_t)(int64_t)args[0].ToNumber(), args[1].AsString()->Value));
            }, 2);
            vm->DefineNative("GetBlendShapeCount", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                const auto* names = s && argc > 0 && args[0].IsNumber() ? Names(*s, (uint32_t)(int64_t)args[0].ToNumber()) : nullptr;
                return Value::MakeInt(names ? (int64_t)names->size() : 0);
            }, 1);
            vm->DefineNative("GetBlendShapeName", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                const auto* names = s && argc > 1 && args[0].IsNumber() ? Names(*s, (uint32_t)(int64_t)args[0].ToNumber()) : nullptr;
                const int64_t i = argc > 1 && args[1].IsNumber() ? (int64_t)args[1].ToNumber() : -1;
                return names && i >= 0 && i < (int64_t)names->size() ? Value::MakeString((*names)[(size_t)i]) : Value::MakeNull();
            }, 2);
        }
    }

    template<> void Scene::OnComponentAdded<BlendShapesComponent>(Entity, BlendShapesComponent&) {}

    void RegisterBlendShapeComponents()
    {
        using C = BlendShapesComponent;
        ComponentDef<C>("Blend Shapes", "BlendShapesComponent", "Animation", ICON_FA_FACE_SMILE).Color({ 0.95f, 0.6f, 0.75f, 1.0f })
            .Describe("Morph target weights of the model (Unity Blend Shapes, Unreal Morph Targets): sliders per shape; animation clips with "
                      "morph curves add to these. Scripts: SetBlendShapeWeight(entity, \"Name\", w), GetBlendShapeWeight, GetBlendShapeCount / Name.")
            .Field("Count", "Blend Shapes", &C::Count).ReadOnly().Header("Runtime")
            .Field("Active", "Active", &C::Active).ReadOnly()
            .CustomData(
                [](YAML::Emitter& out, C& c) {
                    if (c.Weights.empty())
                        return;
                    out << YAML::Key << "Weights" << YAML::Value << YAML::BeginMap;
                    for (const auto& w : c.Weights)
                        out << YAML::Key << w.Name << YAML::Value << w.Weight;
                    out << YAML::EndMap;
                },
                [](const YAML::Node& n, C& c) {
                    c.Weights.clear();
                    if (auto w = n["Weights"]; w && w.IsMap())
                        for (auto it = w.begin(); it != w.end(); ++it)
                            c.Weights.push_back({ it->first.as<std::string>(), it->second.as<float>(0.0f) });
                });
    }
}
