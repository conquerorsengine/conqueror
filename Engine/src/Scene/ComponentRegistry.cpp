#include "UI/UIExtras.h"
#include "Core/Serialization/SceneSerializer.h"
#include "ComponentRegistry.h"
#include "UI/UIToolkit.h"
#include "AISystem/BehaviorTree/BehaviorTreeAsset.h"
#include "Scene/AssetRuntime.h"
#include "Scene/Foliage.h"
#include "Renderer/LightProfile.h"
#include "Renderer/ExtraTextures.h"
#include "Scene/AnimationAssets.h"
#include "Core/Animation/PropertyAnimation.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        std::vector<ComponentInfo>& Storage()
        {
            static std::vector<ComponentInfo> s_Infos;
            return s_Infos;
        }

        bool& Building()
        {
            static bool s_Building = false;
            return s_Building;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            return s;
        }

        int ReadEnum(const ComponentField& f, void* p)
        {
            switch (f.EnumSize)
            {
                case 1: return (int)*(int8_t*)p;
                case 2: return (int)*(int16_t*)p;
                case 8: return (int)*(int64_t*)p;
                default: return *(int32_t*)p;
            }
        }

        void WriteEnum(const ComponentField& f, void* p, int v)
        {
            switch (f.EnumSize)
            {
                case 1: *(int8_t*)p = (int8_t)v; break;
                case 2: *(int16_t*)p = (int16_t)v; break;
                case 8: *(int64_t*)p = (int64_t)v; break;
                default: *(int32_t*)p = (int32_t)v; break;
            }
        }

        template<int N>
        void WriteSeq(YAML::Emitter& out, const float* v)
        {
            out << YAML::Flow << YAML::BeginSeq;
            for (int i = 0; i < N; ++i) out << v[i];
            out << YAML::EndSeq;
        }

        template<int N>
        bool ReadSeq(const YAML::Node& n, float* v)
        {
            if (!n.IsSequence() || (int)n.size() < N) return false;
            for (int i = 0; i < N; ++i) v[i] = n[i].as<float>();
            return true;
        }
    }

    std::vector<ComponentInfo>& ComponentRegistry::All()
    {
        auto& infos = Storage();
        static bool s_Registered = false;
        if (!s_Registered && !Building())
        {
            s_Registered = true;
            Building() = true;
            RegisterBuiltinComponents();
            RegisterPhysicsComponents();
            RegisterGameplayComponents();
            RegisterGridComponents();
            RegisterNetworkComponents();
            RegisterTimelineComponents();
            PropertyClips::RegisterComponent();
            RegisterSplineComponents();
            RegisterCameraRigComponents();
            RegisterPostProcessVolumeComponents();
            RegisterDecalComponents();
            RegisterLighting2DComponents();
            RegisterPixelPerfectComponents();
            RegisterBillboardComponents();
            RegisterFullScreenPassComponents();
            RegisterCameraSkyboxComponents();
            RegisterAreaLightComponents();
            RegisterLightProbeGroupComponents();
            RegisterSpriteAnimation2DComponents();
            RegisterSpriteShapeComponents();
            RegisterOcclusionComponents();
            RegisterArticulationComponents();
            RegisterXRComponents();
            RegisterVisualEffectComponents();
            RegisterWorldActorComponents();
            RegisterActorComponents();
            RegisterWaterComponents();
            RegisterPlanarReflectionComponents();
            RegisterMetaSoundComponents();
            RegisterWorldPartitionComponents();
            RegisterBlendShapeComponents();
            RegisterMetaHumanComponents();
            RegisterTreeComponents();
            RegisterPlayerInputComponents();
            RegisterRigComponents();
            RegisterNavigationComponents();
            RegisterVideoComponents();
            RegisterGameplayTagComponents();
            RegisterAbilitySystemComponents();
            RegisterGameFrameworkComponents();
            RegisterLevelStreamingComponents();
            RegisterAutoLODComponents();
            RegisterAudioOcclusionComponents();
            RegisterVertexPaintComponents();
            RegisterFractureComponents();
            RegisterRagdollComponents();
            RegisterVirtualGeometryComponents();
            RegisterCharacterCustomizerComponents();
            RegisterSignalComponents();
            UIToolkit::RegisterComponents();
            UIExtras::RegisterComponents();
            BehaviorTrees::RegisterComponents();
            AssetRuntime::RegisterComponents();
            Foliage::RegisterComponents();
            LightProfiles::RegisterComponents();
            ExtraTextures::RegisterComponents();
            AnimationAssets::RegisterComponents();
            RegisterCSGComponents();
            RegisterMultiMeshComponents();
            RegisterGodotNodeComponents();
            RegisterRenderTargetComponents();
            RegisterSkeleton2DComponents();
            Building() = false;
        }
        return infos;
    }

    void ComponentRegistry::Register(ComponentInfo info)
    {
        auto& infos = Storage();
        for (auto& existing : infos)
            if (existing.TypeName == info.TypeName)
            {
                existing = std::move(info);
                return;
            }
        infos.push_back(std::move(info));
    }

    ComponentInfo* ComponentRegistry::Find(const std::string& typeOrName)
    {
        std::string key = Lower(typeOrName);
        for (auto& info : All())
            if (Lower(info.TypeName) == key || Lower(info.Name) == key || Lower(info.ScriptName) == key)
                return &info;
        for (auto& info : All())
            for (const auto& alias : info.Aliases)
                if (Lower(alias) == key)
                    return &info;
        // Boşluksuz ad ("CharacterController" == "Character Controller")
        for (auto& info : All())
        {
            std::string n = Lower(info.Name);
            n.erase(std::remove(n.begin(), n.end(), ' '), n.end());
            if (n == key)
                return &info;
        }
        return nullptr;
    }

    const ComponentField* ComponentRegistry::FindField(const ComponentInfo& info, const std::string& name)
    {
        // Önce alan adı, sonra görünen etiket: ör. Particle System'de "Shape" alanı, etiketi "Shape" olan
        // ShapeEnabled'dan önce eşleşmeli
        std::string key = Lower(name);
        for (auto& f : info.Fields)
            if (Lower(f.Name) == key)
                return &f;
        for (auto& f : info.Fields)
            if (Lower(f.Label) == key)
                return &f;
        // Boşluksuz etiket ("Start Lifetime" == "StartLifetime")
        for (auto& f : info.Fields)
        {
            std::string label = Lower(f.Label);
            label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
            if (label == key)
                return &f;
        }
        return nullptr;
    }

    void ComponentRegistry::WriteComponentFields(YAML::Emitter& out, const ComponentInfo& info, void* c)
    {
        for (auto& f : info.Fields)
        {
            if (!f.Serialize)
                continue;
            if (!f.Ptr)
            {
                if (f.GetString)
                    out << YAML::Key << f.Name << YAML::Value << f.GetString(c);
                continue;
            }
            void* p = f.Ptr(c);
            out << YAML::Key << f.Name << YAML::Value;
            switch (f.Kind)
            {
                case FieldKind::Bool: out << *(bool*)p; break;
                case FieldKind::Int: out << *(int*)p; break;
                case FieldKind::Float:
                case FieldKind::Angle: out << *(float*)p; break;
                case FieldKind::Vec2: WriteSeq<2>(out, (float*)p); break;
                case FieldKind::Vec3:
                case FieldKind::Color3: WriteSeq<3>(out, (float*)p); break;
                case FieldKind::Vec4:
                case FieldKind::Color4: WriteSeq<4>(out, (float*)p); break;
                case FieldKind::String: out << *(std::string*)p; break;
                // Dosya yolu proje göreli / CQ:Resources yazılır (mutlak yol proje taşınınca bozulurdu)
                case FieldKind::Asset: out << SceneSerializer::ToAssetPath(*(std::string*)p); break;
                case FieldKind::Enum: out << ReadEnum(f, p); break;
                case FieldKind::EntityRef: out << *(uint64_t*)p; break;
                case FieldKind::LayerMask: out << *(uint32_t*)p; break;
                case FieldKind::Vec2List:
                {
                    out << YAML::Flow << YAML::BeginSeq;
                    for (auto& v : *(std::vector<glm::vec2>*)p)
                        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
                    out << YAML::EndSeq;
                    break;
                }
                case FieldKind::Vec3List:
                {
                    out << YAML::Flow << YAML::BeginSeq;
                    for (auto& v : *(std::vector<glm::vec3>*)p)
                        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
                    out << YAML::EndSeq;
                    break;
                }
            }
        }
        if (info.CustomSerialize)
            info.CustomSerialize(out, c);
    }

    void ComponentRegistry::ReadComponentFields(const YAML::Node& node, const ComponentInfo& info, void* c)
    {
        for (auto& f : info.Fields)
        {
            if (!f.Serialize)
                continue;
            YAML::Node v = node[f.Name];
            if (!v)
                continue;
            if (!f.Ptr)
            {
                if (f.SetString && v.IsScalar())
                    f.SetString(c, v.as<std::string>());
                continue;
            }
            void* p = f.Ptr(c);
            try
            {
                switch (f.Kind)
                {
                    case FieldKind::Bool: *(bool*)p = v.as<bool>(); break;
                    case FieldKind::Int: *(int*)p = v.as<int>(); break;
                    case FieldKind::Float:
                    case FieldKind::Angle: *(float*)p = v.as<float>(); break;
                    case FieldKind::Vec2: ReadSeq<2>(v, (float*)p); break;
                    case FieldKind::Vec3:
                    case FieldKind::Color3: ReadSeq<3>(v, (float*)p); break;
                    case FieldKind::Vec4:
                    case FieldKind::Color4: ReadSeq<4>(v, (float*)p); break;
                    case FieldKind::String:
                    case FieldKind::Asset: *(std::string*)p = v.as<std::string>(); break;
                    case FieldKind::Enum: WriteEnum(f, p, v.as<int>()); break;
                    case FieldKind::EntityRef: *(uint64_t*)p = v.as<uint64_t>(); break;
                    case FieldKind::LayerMask: *(uint32_t*)p = v.as<uint32_t>(); break;
                    case FieldKind::Vec2List:
                    {
                        auto& list = *(std::vector<glm::vec2>*)p;
                        list.clear();
                        if (v.IsSequence())
                            for (auto item : v)
                                if (item.IsSequence() && item.size() >= 2)
                                    list.emplace_back(item[0].as<float>(), item[1].as<float>());
                        break;
                    }
                    case FieldKind::Vec3List:
                    {
                        auto& list = *(std::vector<glm::vec3>*)p;
                        list.clear();
                        if (v.IsSequence())
                            for (auto item : v)
                                if (item.IsSequence() && item.size() >= 3)
                                    list.emplace_back(item[0].as<float>(), item[1].as<float>(), item[2].as<float>());
                        break;
                    }
                }
            }
            catch (const YAML::Exception& e)
            {
                CQ_CORE_WARN("[Scene] {0}.{1}: {2}", info.TypeName, f.Name, e.what());
            }
        }
    }

    void ComponentRegistry::SerializeEntity(YAML::Emitter& out, Entity entity)
    {
        for (auto& info : All())
        {
            if (info.Builtin || !info.Has(entity))
                continue;
            void* c = info.Get(entity);
            out << YAML::Key << info.TypeName << YAML::Value << YAML::BeginMap;
            WriteComponentFields(out, info, c);
            out << YAML::EndMap;
        }
    }

    void ComponentRegistry::DeserializeEntity(const YAML::Node& entityNode, Entity entity)
    {
        for (auto& info : All())
        {
            if (info.Builtin)
                continue;
            YAML::Node node = entityNode[info.TypeName];
            if (!node || !node.IsMap())
                continue;
            if (!info.Has(entity))
                info.Add(entity);
            void* c = info.Get(entity);
            if (!c)
                continue;
            if (info.CustomDeserialize)
            {
                try { info.CustomDeserialize(node, c); }
                catch (const YAML::Exception& e) { CQ_CORE_WARN("[Scene] {0}: {1}", info.TypeName, e.what()); }
            }
            ReadComponentFields(node, info, c);
        }
    }

    void ComponentRegistry::CopyAll(Entity source, Entity destination)
    {
        for (auto& info : All())
            if (!info.Builtin && info.Has(source))
                info.Copy(source, destination);
    }

    void ComponentRegistry::AddWithRequirements(const ComponentInfo& info, Entity entity)
    {
        for (auto& req : info.Requires)
            if (ComponentInfo* r = Find(req))
            {
                if (!r->Has(entity))
                    AddWithRequirements(*r, entity);
            }
        if (!info.Has(entity))
        {
            info.Add(entity);
            if (info.OnAddedInEditor)
                info.OnAddedInEditor(entity);
        }
    }

    std::string ComponentRegistry::FieldToString(const ComponentField& f, void* component)
    {
        if (!f.Ptr)
            return f.GetString ? f.GetString(component) : std::string();
        void* p = f.Ptr(component);
        char buf[160];
        switch (f.Kind)
        {
            case FieldKind::Bool: return *(bool*)p ? "true" : "false";
            case FieldKind::Int: return std::to_string(*(int*)p);
            case FieldKind::Float:
            case FieldKind::Angle: std::snprintf(buf, sizeof(buf), "%g", *(float*)p); return buf;
            case FieldKind::Vec2: { float* v = (float*)p; std::snprintf(buf, sizeof(buf), "%g %g", v[0], v[1]); return buf; }
            case FieldKind::Vec3:
            case FieldKind::Color3: { float* v = (float*)p; std::snprintf(buf, sizeof(buf), "%g %g %g", v[0], v[1], v[2]); return buf; }
            case FieldKind::Vec4:
            case FieldKind::Color4: { float* v = (float*)p; std::snprintf(buf, sizeof(buf), "%g %g %g %g", v[0], v[1], v[2], v[3]); return buf; }
            case FieldKind::String:
            case FieldKind::Asset: return *(std::string*)p;
            case FieldKind::Enum:
            {
                int v = ReadEnum(f, p);
                return v >= 0 && v < (int)f.Options.size() ? f.Options[(size_t)v] : std::to_string(v);
            }
            case FieldKind::EntityRef: return std::to_string(*(uint64_t*)p);
            case FieldKind::LayerMask: return std::to_string(*(uint32_t*)p);
            case FieldKind::Vec2List:
            {
                std::string out;
                for (auto& v : *(std::vector<glm::vec2>*)p)
                {
                    std::snprintf(buf, sizeof(buf), "%s%g %g", out.empty() ? "" : ", ", v.x, v.y);
                    out += buf;
                }
                return out;
            }
            case FieldKind::Vec3List:
            {
                std::string out;
                for (auto& v : *(std::vector<glm::vec3>*)p)
                {
                    std::snprintf(buf, sizeof(buf), "%s%g %g %g", out.empty() ? "" : ", ", v.x, v.y, v.z);
                    out += buf;
                }
                return out;
            }
        }
        return {};
    }

    bool ComponentRegistry::FieldFromString(const ComponentField& f, void* component, const std::string& text)
    {
        if (!f.Ptr)
        {
            if (!f.SetString || f.ReadOnly)
                return false;
            f.SetString(component, text);
            return true;
        }
        void* p = f.Ptr(component);
        std::string t = text;
        std::replace(t.begin(), t.end(), ',', ' ');
        std::istringstream in(t);
        switch (f.Kind)
        {
            case FieldKind::Bool:
            {
                std::string l = Lower(text);
                *(bool*)p = l == "true" || l == "1" || l == "yes" || l == "on";
                return true;
            }
            case FieldKind::Int: return (bool)(in >> *(int*)p);
            case FieldKind::Float:
            case FieldKind::Angle: return (bool)(in >> *(float*)p);
            case FieldKind::Vec2: { float* v = (float*)p; return (bool)(in >> v[0] >> v[1]); }
            case FieldKind::Vec3:
            case FieldKind::Color3: { float* v = (float*)p; return (bool)(in >> v[0] >> v[1] >> v[2]); }
            case FieldKind::Vec4:
            case FieldKind::Color4: { float* v = (float*)p; return (bool)(in >> v[0] >> v[1] >> v[2] >> v[3]); }
            case FieldKind::String:
            case FieldKind::Asset: *(std::string*)p = text; return true;
            case FieldKind::Enum:
            {
                for (size_t i = 0; i < f.Options.size(); ++i)
                    if (Lower(f.Options[i]) == Lower(text)) { WriteEnum(f, p, (int)i); return true; }
                int v = 0;
                if (!(in >> v)) return false;
                WriteEnum(f, p, v);
                return true;
            }
            case FieldKind::EntityRef: return (bool)(in >> *(uint64_t*)p);
            case FieldKind::LayerMask: return (bool)(in >> *(uint32_t*)p);
            case FieldKind::Vec2List:
            {
                // "x y, x y, ..." (virgüller boşluğa çevrildi: çiftler halinde oku)
                std::vector<glm::vec2> list;
                float x, y;
                while (in >> x >> y)
                    list.emplace_back(x, y);
                *(std::vector<glm::vec2>*)p = std::move(list);
                return true;
            }
            case FieldKind::Vec3List:
            {
                std::vector<glm::vec3> list;
                float x, y, z;
                while (in >> x >> y >> z)
                    list.emplace_back(x, y, z);
                *(std::vector<glm::vec3>*)p = std::move(list);
                return true;
            }
        }
        return false;
    }
}
