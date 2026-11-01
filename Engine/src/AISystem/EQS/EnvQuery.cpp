#include "EnvQuery.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/GameplayTags.h"
#include "AISystem/Navigation/NavMesh.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Serialization/PrefabSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace Conqueror
{
    namespace
    {
        constexpr uint32_t kNull = 0xFFFFFFFFu;

        bool ValidEntity(Scene& scene, uint32_t id)
        {
            return id != kNull && scene.m_Registry.valid((entt::entity)id);
        }

        glm::vec3 EntityPosition(Scene& scene, uint32_t id)
        {
            return glm::vec3(scene.m_Registry.get<TransformComponent>((entt::entity)id).WorldTransform[3]);
        }

        bool ResolveContext(Scene& scene, uint32_t querier, const EnvContext& ctx, glm::vec3& out, uint32_t* outEntity = nullptr)
        {
            uint32_t id = kNull;
            switch (ctx.Type)
            {
                case EnvContext::Kind::Querier: id = querier; break;
                case EnvContext::Kind::Entity: id = ctx.EntityId; break;
                case EnvContext::Kind::Point: out = ctx.Point; if (outEntity) *outEntity = kNull; return true;
            }
            if (!ValidEntity(scene, id))
                return false;
            out = EntityPosition(scene, id);
            if (outEntity)
                *outEntity = id;
            return true;
        }

        entt::entity RootOf(Scene& scene, entt::entity e)
        {
            Entity cur{ e, &scene };
            for (int guard = 0; guard < 256; ++guard)
            {
                Entity parent = cur.GetParent();
                if (!parent)
                    break;
                cur = parent;
            }
            return (entt::entity)cur;
        }

        // Nokta: gezinme ağına (varsa) ya da aşağı ışınla zemine oturt
        bool Project(Scene& scene, glm::vec3& p, bool toNav)
        {
            if (!toNav)
                return true;
            NavMesh& nav = scene.GetNavMesh();
            if (nav.IsBaked())
            {
                glm::vec3 snapped;
                if (!nav.SamplePosition(p, 2.0f, snapped))
                    return false;
                p = snapped;
                return true;
            }
            if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D())
            {
                Physics3D::RaycastHit hit;
                if (physics->Raycast(p + glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 30.0f, hit) && !hit.IsTrigger)
                    p = hit.Point;
            }
            return true;
        }

        void Generate(Scene& scene, uint32_t querier, const EnvGenerator& g, std::vector<EnvQueryItem>& items)
        {
            if (g.Type == EnvGeneratorType::Entities)
            {
                auto view = scene.m_Registry.view<TransformComponent, TagComponent>();
                for (auto e : view)
                {
                    if ((uint32_t)e == querier || !scene.IsEntityActive(e))
                        continue;
                    const bool match = g.Tag.empty() || view.get<TagComponent>(e).GameTag == g.Tag
                                    || GameplayTags::Has(Entity{ e, &scene }, g.Tag);
                    if (match)
                        items.push_back({ glm::vec3(view.get<TransformComponent>(e).WorldTransform[3]), (uint32_t)e });
                }
                return;
            }
            glm::vec3 center;
            if (!ResolveContext(scene, querier, g.Center, center))
                return;
            std::vector<glm::vec3> points;
            if (g.Type == EnvGeneratorType::Grid)
            {
                const float spacing = std::max(g.Spacing, 0.1f);
                const int n = std::min((int)std::floor(g.Radius / spacing), 64);
                for (int z = -n; z <= n; ++z)
                    for (int x = -n; x <= n; ++x)
                        points.push_back(center + glm::vec3((float)x * spacing, 0.0f, (float)z * spacing));
            }
            else
            {
                const int rings = std::clamp(g.Rings, 1, 32), per = std::clamp(g.PointsPerRing, 3, 128);
                for (int r = 1; r <= rings; ++r)
                {
                    const float radius = g.Radius * (float)r / (float)rings;
                    for (int i = 0; i < per; ++i)
                    {
                        const float a = (float)i / (float)per * 6.2831853f + (float)r * 0.37f;
                        points.push_back(center + glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius));
                    }
                }
            }
            for (glm::vec3 p : points)
                if (Project(scene, p, g.ProjectToNavMesh))
                    items.push_back({ p, kNull });
        }

        // Ham test değeri; NaN = ölçülemedi (öğe süzülür)
        float Measure(Scene& scene, uint32_t querier, const EnvTest& t, const EnvQueryItem& item)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            glm::vec3 ctx;
            uint32_t ctxEntity = kNull;
            if (!ResolveContext(scene, querier, t.Context, ctx, &ctxEntity))
                return nan;
            switch (t.Type)
            {
                case EnvTestType::Distance:
                    return glm::distance(item.Position, ctx);
                case EnvTestType::Height:
                    return item.Position.y - ctx.y;
                case EnvTestType::Dot:
                {
                    if (!ValidEntity(scene, ctxEntity))
                        return nan;
                    const glm::mat4& w = scene.m_Registry.get<TransformComponent>((entt::entity)ctxEntity).WorldTransform;
                    glm::vec3 forward = -glm::vec3(w[2]);
                    forward.y = 0.0f;
                    glm::vec3 to = item.Position - ctx;
                    to.y = 0.0f;
                    if (glm::dot(forward, forward) < 1e-8f || glm::dot(to, to) < 1e-8f)
                        return 0.0f;
                    return glm::dot(glm::normalize(forward), glm::normalize(to));
                }
                case EnvTestType::Visibility:
                {
                    PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
                    if (!physics)
                        return 1.0f;
                    const glm::vec3 from = ctx + glm::vec3(0.0f, t.EyeHeight, 0.0f);
                    const glm::vec3 to = item.Position + glm::vec3(0.0f, t.ItemHeight, 0.0f);
                    const glm::vec3 d = to - from;
                    const float len = glm::length(d);
                    if (len < 1e-3f)
                        return 1.0f;
                    std::vector<Physics3D::RaycastHit> hits;
                    physics->RaycastAll(from, d / len, len, hits);
                    const entt::entity ctxRoot = ValidEntity(scene, ctxEntity) ? RootOf(scene, (entt::entity)ctxEntity) : entt::null;
                    const entt::entity itemRoot = ValidEntity(scene, item.Entity) ? RootOf(scene, (entt::entity)item.Entity) : entt::null;
                    for (const auto& h : hits)
                    {
                        if (h.IsTrigger || !ValidEntity(scene, h.Entity))
                            continue;
                        const entt::entity root = RootOf(scene, (entt::entity)h.Entity);
                        if (root == ctxRoot || root == itemRoot)
                            continue;
                        return 0.0f;
                    }
                    return 1.0f;
                }
                case EnvTestType::PathLength:
                {
                    NavMesh& nav = scene.GetNavMesh();
                    if (!nav.IsBaked())
                        return glm::distance(item.Position, ctx);
                    std::vector<NavPathPoint> path;
                    bool complete = false;
                    if (!nav.FindPath(ctx, item.Position, path, 0xFFFFFFFFu, &complete) || !complete)
                        return nan;
                    float length = 0.0f;
                    glm::vec3 prev = ctx;
                    for (const auto& p : path)
                    {
                        length += glm::distance(prev, p.Position);
                        prev = p.Position;
                    }
                    return length;
                }
            }
            return nan;
        }

        EnvContext ContextFromYaml(const YAML::Node& n)
        {
            EnvContext c;
            if (!n)
                return c;
            // "querier" (sorgulayan) ya da "target" (çalıştırırken verilen hedef varlık)
            const std::string s = n.as<std::string>("querier");
            c.Type = s == "target" ? EnvContext::Kind::Entity : EnvContext::Kind::Querier;
            return c;
        }
    }

    namespace EnvQuery
    {
        std::vector<EnvQueryItem> Run(Scene& scene, uint32_t querier, const EnvQueryDesc& desc, std::vector<EnvQueryItem>* all)
        {
            std::vector<EnvQueryItem> items;
            Generate(scene, querier, desc.Generator, items);
            std::vector<float> raw(items.size());
            for (const EnvTest& t : desc.Tests)
            {
                // Ham değerler + süzme
                float lo = std::numeric_limits<float>::max(), hi = -std::numeric_limits<float>::max();
                for (size_t i = 0; i < items.size(); ++i)
                {
                    if (!items[i].Valid)
                        continue;
                    const float v = Measure(scene, querier, t, items[i]);
                    raw[i] = v;
                    if (std::isnan(v))
                    {
                        items[i].Valid = false;
                        continue;
                    }
                    if (t.Mode != EnvTestMode::Score)
                    {
                        const bool pass = t.Type == EnvTestType::Visibility ? ((v > 0.5f) == t.Visible) : (v >= t.FilterMin && v <= t.FilterMax);
                        if (!pass)
                        {
                            items[i].Valid = false;
                            continue;
                        }
                    }
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                if (t.Mode == EnvTestMode::Filter)
                    continue;
                // Puan: aralıkta 0..1, ağırlık < 0 düşük değeri tercih eder
                const float range = hi - lo;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    if (!items[i].Valid)
                        continue;
                    float n = range > 1e-6f ? (raw[i] - lo) / range : 1.0f;
                    if (t.Type == EnvTestType::Visibility && !t.Visible)
                        n = 1.0f - n;
                    items[i].Score += t.Weight >= 0.0f ? n * t.Weight : (1.0f - n) * -t.Weight;
                }
            }
            std::stable_sort(items.begin(), items.end(), [](const EnvQueryItem& a, const EnvQueryItem& b) {
                if (a.Valid != b.Valid) return a.Valid;
                return a.Score > b.Score;
            });
            if (all)
                *all = items;
            std::vector<EnvQueryItem> best;
            for (const auto& it : items)
            {
                if (!it.Valid || (int)best.size() >= std::max(desc.MaxResults, 1))
                    break;
                best.push_back(it);
            }
            return best;
        }

        void DebugDraw(const std::vector<EnvQueryItem>& items, float seconds)
        {
            float top = 0.0f;
            for (const auto& it : items)
                if (it.Valid) top = std::max(top, it.Score);
            for (size_t i = 0; i < items.size(); ++i)
            {
                const auto& it = items[i];
                glm::vec4 color(0.9f, 0.15f, 0.1f, 0.9f);
                if (it.Valid)
                {
                    const float t = top > 1e-6f ? it.Score / top : 1.0f;
                    color = glm::vec4(1.0f - t, 0.35f + 0.65f * t, 0.15f, 1.0f);
                }
                Conqueror::DebugDraw::Sphere(it.Position + glm::vec3(0.0f, 0.15f, 0.0f), i == 0 && it.Valid ? 0.3f : 0.15f, color, seconds, false);
            }
        }

        bool Load(const std::string& path, EnvQueryDesc& out, std::string* error)
        {
            YAML::Node root;
            try
            {
                root = YAML::LoadFile(PrefabSerializer::Resolve(path).string());
            }
            catch (const std::exception& e)
            {
                if (error) *error = e.what();
                return false;
            }
            auto q = root["EnvQuery"];
            if (!q)
            {
                if (error) *error = "missing EnvQuery root";
                return false;
            }
            EnvQueryDesc d;
            d.MaxResults = q["MaxResults"].as<int>(5);
            if (auto g = q["Generator"])
            {
                const std::string type = g["Type"].as<std::string>("Grid");
                d.Generator.Type = type == "Ring" ? EnvGeneratorType::Ring : type == "Entities" ? EnvGeneratorType::Entities : EnvGeneratorType::Grid;
                d.Generator.Radius = g["Radius"].as<float>(d.Generator.Radius);
                d.Generator.Spacing = g["Spacing"].as<float>(d.Generator.Spacing);
                d.Generator.Rings = g["Rings"].as<int>(d.Generator.Rings);
                d.Generator.PointsPerRing = g["PointsPerRing"].as<int>(d.Generator.PointsPerRing);
                d.Generator.Tag = g["Tag"].as<std::string>("");
                d.Generator.ProjectToNavMesh = g["ProjectToNavMesh"].as<bool>(true);
                d.Generator.Center = ContextFromYaml(g["Center"]);
            }
            if (auto tests = q["Tests"])
                for (auto n : tests)
                {
                    EnvTest t;
                    const std::string type = n["Type"].as<std::string>("Distance");
                    t.Type = type == "Visibility" ? EnvTestType::Visibility : type == "Dot" ? EnvTestType::Dot
                           : type == "PathLength" ? EnvTestType::PathLength : type == "Height" ? EnvTestType::Height : EnvTestType::Distance;
                    const std::string mode = n["Mode"].as<std::string>("Score");
                    t.Mode = mode == "Filter" ? EnvTestMode::Filter : mode == "FilterAndScore" ? EnvTestMode::FilterAndScore : EnvTestMode::Score;
                    // Bağlam: "querier" ya da "target" (çalıştırırken verilen hedef varlık)
                    const std::string ctx = n["Context"].as<std::string>("querier");
                    t.Context.Type = ctx == "target" ? EnvContext::Kind::Entity : EnvContext::Kind::Querier;
                    t.FilterMin = n["Min"].as<float>(t.FilterMin);
                    t.FilterMax = n["Max"].as<float>(t.FilterMax);
                    t.Visible = n["Visible"].as<bool>(true);
                    t.Weight = n["Weight"].as<float>(1.0f);
                    d.Tests.push_back(t);
                }
            out = d;
            return true;
        }

        bool Save(const std::string& path, const EnvQueryDesc& d)
        {
            static const char* gens[] = { "Grid", "Ring", "Entities" };
            static const char* types[] = { "Distance", "Visibility", "Dot", "PathLength", "Height" };
            static const char* modes[] = { "Score", "Filter", "FilterAndScore" };
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "EnvQuery" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "MaxResults" << YAML::Value << d.MaxResults;
            out << YAML::Key << "Generator" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << gens[(int)d.Generator.Type];
            out << YAML::Key << "Radius" << YAML::Value << d.Generator.Radius;
            out << YAML::Key << "Spacing" << YAML::Value << d.Generator.Spacing;
            out << YAML::Key << "Rings" << YAML::Value << d.Generator.Rings;
            out << YAML::Key << "PointsPerRing" << YAML::Value << d.Generator.PointsPerRing;
            out << YAML::Key << "Tag" << YAML::Value << d.Generator.Tag;
            out << YAML::Key << "ProjectToNavMesh" << YAML::Value << d.Generator.ProjectToNavMesh;
            out << YAML::EndMap;
            out << YAML::Key << "Tests" << YAML::Value << YAML::BeginSeq;
            for (const auto& t : d.Tests)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Type" << YAML::Value << types[(int)t.Type];
                out << YAML::Key << "Mode" << YAML::Value << modes[(int)t.Mode];
                out << YAML::Key << "Context" << YAML::Value << (t.Context.Type == EnvContext::Kind::Entity ? "target" : "querier");
                if (t.FilterMin > -1e29f) out << YAML::Key << "Min" << YAML::Value << t.FilterMin;
                if (t.FilterMax < 1e29f) out << YAML::Key << "Max" << YAML::Value << t.FilterMax;
                out << YAML::Key << "Visible" << YAML::Value << t.Visible;
                out << YAML::Key << "Weight" << YAML::Value << t.Weight;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
            std::ofstream f(PrefabSerializer::Resolve(path));
            if (!f)
                return false;
            f << out.c_str();
            return (bool)f;
        }
    }
}
