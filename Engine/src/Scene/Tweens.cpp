#include "Tweens.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        // Özellik erişimi: değeri vec4 olarak oku / yaz (boyut 1..4)
        struct Accessor
        {
            enum class Kind { None, Position, Rotation, Scale, Field } Type = Kind::None;
            const ComponentInfo* Info = nullptr;
            const ComponentField* Field = nullptr;
            int Size = 1;
            int Channel = -1;   // "alpha" gibi tek bileşen (yoksa -1)
        };

        struct Tween
        {
            uint32_t Id = 0;
            TweenDesc Desc;
            Accessor Access;
            glm::vec4 From{ 0.0f }, To{ 0.0f };
            float Time = 0.0f;
            int LoopsDone = 0;
            bool Forward = true;
            bool Paused = false;
            bool Started = false;
        };

        struct State
        {
            std::vector<Tween> Tweens;
            uint32_t NextId = 1;
        };

        std::unordered_map<Scene*, State>& States()
        {
            static std::unordered_map<Scene*, State> states;
            return states;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        int SizeOf(FieldKind k)
        {
            switch (k)
            {
                case FieldKind::Float: case FieldKind::Angle: case FieldKind::Int: return 1;
                case FieldKind::Vec2: return 2;
                case FieldKind::Vec3: case FieldKind::Color3: return 3;
                case FieldKind::Vec4: case FieldKind::Color4: return 4;
                default: return 0;
            }
        }

        bool Resolve(Entity e, const std::string& property, Accessor& out)
        {
            const std::string p = Lower(property);
            if (p == "position") { out.Type = Accessor::Kind::Position; out.Size = 3; return true; }
            if (p == "rotation") { out.Type = Accessor::Kind::Rotation; out.Size = 3; return true; }
            if (p == "scale") { out.Type = Accessor::Kind::Scale; out.Size = 3; return true; }
            auto tryField = [&](const std::string& comp, const std::string& field, int channel) {
                const ComponentInfo* info = ComponentRegistry::Find(comp);
                if (!info || !info->Has || !info->Has(e))
                    return false;
                const ComponentField* f = ComponentRegistry::FindField(*info, field);
                if (!f || !f->Ptr || SizeOf(f->Kind) == 0)
                    return false;
                out.Type = Accessor::Kind::Field;
                out.Info = info;
                out.Field = f;
                out.Size = channel >= 0 ? 1 : SizeOf(f->Kind);
                out.Channel = channel;
                return true;
            };
            if (p == "color" || p == "alpha")
            {
                const int channel = p == "alpha" ? 3 : -1;
                for (const char* comp : { "MeshRenderer", "SpriteRenderer", "UIImage", "UIText", "TextRenderer" })
                    if (tryField(comp, "Color", channel))
                        return true;
                return false;
            }
            const size_t dot = property.find('.');
            if (dot == std::string::npos)
                return false;
            return tryField(property.substr(0, dot), property.substr(dot + 1), -1);
        }

        glm::vec4 Read(Entity e, const Accessor& a)
        {
            auto& tc = e.GetComponent<TransformComponent>();
            switch (a.Type)
            {
                case Accessor::Kind::Position: return glm::vec4(tc.Position, 0.0f);
                case Accessor::Kind::Rotation: return glm::vec4(glm::degrees(tc.Rotation), 0.0f);
                case Accessor::Kind::Scale: return glm::vec4(tc.Scale, 0.0f);
                case Accessor::Kind::Field:
                {
                    void* comp = a.Info->Get(e);
                    if (!comp)
                        return glm::vec4(0.0f);
                    void* f = a.Field->Ptr(comp);
                    glm::vec4 v(0.0f);
                    const int n = SizeOf(a.Field->Kind);
                    if (a.Field->Kind == FieldKind::Int)
                        v.x = (float)*(int*)f;
                    else
                        for (int i = 0; i < n; ++i)
                            v[i] = ((float*)f)[i];
                    if (a.Channel >= 0)
                        return glm::vec4(v[a.Channel], 0.0f, 0.0f, 0.0f);
                    return v;
                }
                default: return glm::vec4(0.0f);
            }
        }

        void Write(Entity e, const Accessor& a, const glm::vec4& v)
        {
            auto& tc = e.GetComponent<TransformComponent>();
            switch (a.Type)
            {
                case Accessor::Kind::Position: tc.Position = glm::vec3(v); break;
                case Accessor::Kind::Rotation: tc.Rotation = glm::radians(glm::vec3(v)); break;
                case Accessor::Kind::Scale: tc.Scale = glm::vec3(v); break;
                case Accessor::Kind::Field:
                {
                    void* comp = a.Info->Get(e);
                    if (!comp)
                        return;
                    void* f = a.Field->Ptr(comp);
                    if (a.Field->Kind == FieldKind::Int)
                        *(int*)f = (int)std::lround(v.x);
                    else if (a.Channel >= 0)
                        ((float*)f)[a.Channel] = v.x;
                    else
                        for (int i = 0; i < SizeOf(a.Field->Kind); ++i)
                            ((float*)f)[i] = v[i];
                    break;
                }
                default: break;
            }
        }
    }

    namespace Tweens
    {
        float Evaluate(TweenEase ease, float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            const float pi = glm::pi<float>();
            switch (ease)
            {
                case TweenEase::Linear: return t;
                case TweenEase::SineIn: return 1.0f - std::cos(t * pi * 0.5f);
                case TweenEase::SineOut: return std::sin(t * pi * 0.5f);
                case TweenEase::SineInOut: return -(std::cos(pi * t) - 1.0f) * 0.5f;
                case TweenEase::QuadIn: return t * t;
                case TweenEase::QuadOut: return 1.0f - (1.0f - t) * (1.0f - t);
                case TweenEase::QuadInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
                case TweenEase::CubicIn: return t * t * t;
                case TweenEase::CubicOut: return 1.0f - std::pow(1.0f - t, 3.0f);
                case TweenEase::CubicInOut: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
                case TweenEase::ExpoIn: return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
                case TweenEase::ExpoOut: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
                case TweenEase::ExpoInOut:
                    if (t <= 0.0f || t >= 1.0f) return t;
                    return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
                case TweenEase::BackIn: { const float c1 = 1.70158f, c3 = c1 + 1.0f; return c3 * t * t * t - c1 * t * t; }
                case TweenEase::BackOut: { const float c1 = 1.70158f, c3 = c1 + 1.0f; return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f); }
                case TweenEase::BackInOut:
                {
                    const float c2 = 1.70158f * 1.525f;
                    return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                                    : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
                }
                case TweenEase::ElasticOut:
                {
                    if (t <= 0.0f || t >= 1.0f) return t;
                    const float c4 = (2.0f * pi) / 3.0f;
                    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
                }
                case TweenEase::BounceOut:
                {
                    const float n1 = 7.5625f, d1 = 2.75f;
                    if (t < 1.0f / d1) return n1 * t * t;
                    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
                    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
                    t -= 2.625f / d1;
                    return n1 * t * t + 0.984375f;
                }
            }
            return t;
        }

        TweenEase ParseEase(const std::string& name)
        {
            static const std::pair<const char*, TweenEase> kNames[] = {
                { "linear", TweenEase::Linear }, { "sine_in", TweenEase::SineIn }, { "sine_out", TweenEase::SineOut }, { "sine_inout", TweenEase::SineInOut },
                { "quad_in", TweenEase::QuadIn }, { "quad_out", TweenEase::QuadOut }, { "quad_inout", TweenEase::QuadInOut },
                { "cubic_in", TweenEase::CubicIn }, { "cubic_out", TweenEase::CubicOut }, { "cubic_inout", TweenEase::CubicInOut },
                { "expo_in", TweenEase::ExpoIn }, { "expo_out", TweenEase::ExpoOut }, { "expo_inout", TweenEase::ExpoInOut },
                { "back_in", TweenEase::BackIn }, { "back_out", TweenEase::BackOut }, { "back_inout", TweenEase::BackInOut },
                { "elastic_out", TweenEase::ElasticOut }, { "bounce_out", TweenEase::BounceOut } };
            // "sine_inout", "SineInOut", "easeOutBounce", "back-out" ... hepsi aynı: küçük harf, ayraçsız, "ease" öneki yok
            auto normalize = [](std::string s) {
                s = Lower(s);
                s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '_' || c == '-' || c == ' '; }), s.end());
                if (s.rfind("ease", 0) == 0)
                    s = s.substr(4);
                return s;
            };
            const std::string n = normalize(name);
            for (auto& [k, v] : kNames)
                if (n == normalize(k))
                    return v;
            // Ters sıra (DOTween / easings.net): "outbounce" -> "bounceout"
            for (const char* dir : { "inout", "out", "in" })
            {
                const std::string d = dir;
                if (n.rfind(d, 0) == 0)
                {
                    const std::string swapped = n.substr(d.size()) + d;
                    for (auto& [k, v] : kNames)
                        if (normalize(k) == swapped)
                            return v;
                }
            }
            return TweenEase::SineInOut;
        }

        uint32_t Start(Scene& scene, const TweenDesc& desc)
        {
            const entt::entity h = (entt::entity)desc.Entity;
            if (!scene.m_Registry.valid(h))
                return 0;
            Entity e{ h, &scene };
            Tween tw;
            if (!Resolve(e, desc.Property, tw.Access))
                return 0;
            auto& state = States()[&scene];
            tw.Id = state.NextId++;
            tw.Desc = desc;
            state.Tweens.push_back(std::move(tw));
            return state.Tweens.back().Id;
        }

        bool Kill(Scene& scene, uint32_t id, bool complete)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return false;
            auto& list = it->second.Tweens;
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].Id == id)
                {
                    if (complete && list[i].Started && scene.m_Registry.valid((entt::entity)list[i].Desc.Entity))
                        Write(Entity{ (entt::entity)list[i].Desc.Entity, &scene }, list[i].Access, list[i].To);
                    list.erase(list.begin() + (long)i);
                    return true;
                }
            return false;
        }

        int KillAll(Scene& scene, uint32_t entity)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return 0;
            auto& list = it->second.Tweens;
            const size_t before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Tween& t) { return t.Desc.Entity == entity; }), list.end());
            return (int)(before - list.size());
        }

        bool IsActive(Scene& scene, uint32_t id)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return false;
            for (const auto& t : it->second.Tweens)
                if (t.Id == id)
                    return true;
            return false;
        }

        void SetPaused(Scene& scene, uint32_t id, bool paused)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return;
            for (auto& t : it->second.Tweens)
                if (t.Id == id)
                    t.Paused = paused;
        }

        int ActiveCount(Scene& scene)
        {
            auto it = States().find(&scene);
            return it == States().end() ? 0 : (int)it->second.Tweens.size();
        }

        void Update(Scene& scene, float dt)
        {
            auto it = States().find(&scene);
            if (it == States().end() || it->second.Tweens.empty())
                return;
            auto& list = it->second.Tweens;
            std::vector<std::pair<uint32_t, std::string>> completed;
            for (size_t i = 0; i < list.size();)
            {
                Tween& t = list[i];
                const entt::entity h = (entt::entity)t.Desc.Entity;
                if (!scene.m_Registry.valid(h))
                {
                    list.erase(list.begin() + (long)i);
                    continue;
                }
                Entity e{ h, &scene };
                if (t.Paused)
                {
                    ++i;
                    continue;
                }
                if (!t.Started)
                {
                    t.Desc.Delay -= dt;
                    if (t.Desc.Delay > 0.0f)
                    {
                        ++i;
                        continue;
                    }
                    // Başlangıç değeri gecikme bitince okunur (zincirleme tween'ler doğru başlar)
                    t.From = Read(e, t.Access);
                    t.To = t.Desc.Target;
                    if (t.Access.Size < 4)
                        for (int c = t.Access.Size; c < 4; ++c)
                            t.To[c] = t.From[c];
                    if (t.Desc.Relative)
                        t.To = t.From + glm::vec4(glm::vec3(t.Desc.Target), t.Access.Size == 4 ? t.Desc.Target.w : 0.0f);
                    t.Started = true;
                    t.Time = 0.0f;
                }
                t.Time += dt;
                const float duration = std::max(t.Desc.Duration, 1e-4f);
                float u = std::min(t.Time / duration, 1.0f);
                const float k = Evaluate(t.Desc.Ease, t.Forward ? u : 1.0f - u);
                Write(e, t.Access, t.From + (t.To - t.From) * k);
                if (u < 1.0f)
                {
                    ++i;
                    continue;
                }
                // Döngü / bitiş
                const bool infinite = t.Desc.Loops < 0;
                if (infinite || t.LoopsDone < t.Desc.Loops)
                {
                    ++t.LoopsDone;
                    t.Time = 0.0f;
                    if (t.Desc.Yoyo)
                        t.Forward = !t.Forward;
                    ++i;
                    continue;
                }
                if (!t.Desc.OnComplete.empty())
                    completed.push_back({ t.Desc.Entity, t.Desc.OnComplete });
                list.erase(list.begin() + (long)i);
            }
            for (auto& [entity, method] : completed)
                if (scene.m_Registry.valid((entt::entity)entity))
                    scene.InvokeScriptMethod(entity, method.c_str(), nullptr, 0);
        }

        void ForgetScene(Scene* scene)
        {
            States().erase(scene);
        }
    }
}
