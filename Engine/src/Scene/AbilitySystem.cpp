#include "AbilitySystem.h"
#include "Scene/GameplayTags.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        bool SameName(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        float ToFloat(const std::string& s, float fallback)
        {
            try { size_t used = 0; float f = std::stof(s, &used); return used ? f : fallback; }
            catch (...) { return fallback; }
        }

        std::vector<GameplayAttribute> ParseAttributes(const std::string& text)
        {
            std::vector<GameplayAttribute> out;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
            {
                for (char& c : line) if (c == ',' || c == ';' || c == '=') c = ' ';
                std::istringstream ls(line);
                GameplayAttribute a;
                std::string base, mn, mx;
                if (!(ls >> a.Name) || a.Name.empty() || a.Name[0] == '#')
                    continue;
                if (ls >> base) a.Base = ToFloat(base, 0.0f);
                if (ls >> mn) a.Min = ToFloat(mn, a.Min);
                if (ls >> mx) a.Max = ToFloat(mx, a.Max);
                a.Base = std::clamp(a.Base, a.Min, a.Max);
                a.Current = a.Base;
                out.push_back(a);
            }
            return out;
        }

        std::vector<GameplayAbilitySpec> ParseAbilities(const std::string& text)
        {
            std::vector<GameplayAbilitySpec> out;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
            {
                std::istringstream ls(line);
                GameplayAbilitySpec s;
                if (!(ls >> s.Name) || s.Name[0] == '#')
                    continue;
                std::string token;
                while (ls >> token)
                {
                    size_t eq = token.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = Lower(token.substr(0, eq)), value = token.substr(eq + 1);
                    if (key == "cooldown") s.Cooldown = ToFloat(value, 0.0f);
                    else if (key == "cost")
                    {
                        size_t c = value.find(':');
                        s.CostAttribute = c == std::string::npos ? value : value.substr(0, c);
                        s.CostAmount = c == std::string::npos ? 0.0f : ToFloat(value.substr(c + 1), 0.0f);
                    }
                    else if (key == "blocked") s.BlockedTags = AbilitySystem::ParseTagList(value);
                    else if (key == "required") s.RequiredTags = AbilitySystem::ParseTagList(value);
                }
                out.push_back(s);
            }
            return out;
        }

        void Notify(Entity e, const char* method, const std::vector<CQS::Value>& args)
        {
            if (Scene* scene = e.GetScene())
                scene->InvokeScriptMethod((uint32_t)(entt::entity)e, method, args.empty() ? nullptr : args.data(), (int)args.size());
        }

        float ApplyOp(float value, ModifierOp op, float amount, int stacks)
        {
            switch (op)
            {
                case ModifierOp::Add: return value + amount * (float)stacks;
                case ModifierOp::Multiply: return value * std::pow(amount, (float)stacks);
                case ModifierOp::Override: return amount;
            }
            return value;
        }

        bool IsDurationModifier(const ActiveGameplayEffect& e) { return e.Spec.Period <= 0.0f && e.Spec.Duration != 0.0f; }

        // Current = Base + süreli efektlerin değiştiricileri (önce toplama, sonra çarpma, en son ezme), sınırlara kırp
        void Recompute(Entity e, AbilitySystemComponent& asc, GameplayAttribute& attr, bool notify)
        {
            float add = 0.0f, mul = 1.0f;
            bool hasOverride = false;
            float overrideValue = 0.0f;
            for (const auto& fx : asc.Effects)
            {
                if (!IsDurationModifier(fx))
                    continue;
                for (const auto& m : fx.Spec.Modifiers)
                {
                    if (!SameName(m.Attribute, attr.Name))
                        continue;
                    if (m.Op == ModifierOp::Add) add += m.Value * (float)fx.Stacks;
                    else if (m.Op == ModifierOp::Multiply) mul *= std::pow(m.Value, (float)fx.Stacks);
                    else { hasOverride = true; overrideValue = m.Value; }
                }
            }
            const float old = attr.Current;
            attr.Current = std::clamp(hasOverride ? overrideValue : (attr.Base + add) * mul, attr.Min, attr.Max);
            if (notify && old != attr.Current)
            {
                Notify(e, "OnAttributeChanged", { CQS::Value::MakeString(attr.Name), CQS::Value::MakeFloat(old), CQS::Value::MakeFloat(attr.Current) });
                if (attr.Current <= attr.Min && old > attr.Min && std::isfinite(attr.Min))
                    Notify(e, "OnAttributeDepleted", { CQS::Value::MakeString(attr.Name) });
            }
        }

        void RecomputeFor(Entity e, AbilitySystemComponent& asc, const std::vector<AttributeModifier>& mods)
        {
            for (auto& m : mods)
                for (auto& a : asc.AttributeList)
                    if (SameName(a.Name, m.Attribute))
                        Recompute(e, asc, a, true);
        }

        // Anlık / periyodik uygulama: taban değeri değiştir
        void ExecuteOnBase(Entity e, AbilitySystemComponent& asc, const std::vector<AttributeModifier>& mods, int stacks)
        {
            for (const auto& m : mods)
                for (auto& a : asc.AttributeList)
                    if (SameName(a.Name, m.Attribute))
                    {
                        a.Base = std::clamp(ApplyOp(a.Base, m.Op, m.Value, stacks), a.Min, a.Max);
                        Recompute(e, asc, a, true);
                    }
        }

        std::vector<std::string> ExtraTags(Entity e)
        {
            std::vector<std::string> out;
            if (!e.HasComponent<AbilitySystemComponent>())
                return out;
            auto& asc = e.GetComponent<AbilitySystemComponent>();
            for (const auto& fx : asc.Effects)
                for (const auto& t : fx.Spec.GrantedTags)
                    if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
            for (const auto& ab : asc.AbilityList)
                if (ab.CooldownRemaining > 0.0f) out.push_back("Cooldown." + ab.Name);
            return out;
        }

        void BuildState(AbilitySystemComponent& asc)
        {
            if (!asc.ShowDebug)
            {
                asc.State.clear();
                return;
            }
            std::ostringstream o;
            o.setf(std::ios::fixed);
            o.precision(1);
            for (const auto& a : asc.AttributeList)
                o << a.Name << " " << a.Current << (a.Current != a.Base ? " (base " : "") << (a.Current != a.Base ? std::to_string((int)std::lround(a.Base)) + ")" : "") << "\n";
            for (const auto& fx : asc.Effects)
            {
                o << "[" << fx.Spec.Name << (fx.Stacks > 1 ? " x" + std::to_string(fx.Stacks) : "") << "]";
                if (fx.Spec.Duration > 0.0f) o << " " << fx.Remaining << "s";
                o << "\n";
            }
            for (const auto& ab : asc.AbilityList)
                if (ab.CooldownRemaining > 0.0f) o << ab.Name << " cooldown " << ab.CooldownRemaining << "s\n";
            asc.State = o.str();
            if (!asc.State.empty()) asc.State.pop_back();
        }
    }

    namespace AbilitySystem
    {
        std::vector<std::string> ParseTagList(const std::string& text)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : text + ",")
            {
                if (c == ',' || c == '|' || c == ';')
                {
                    std::string t = GameplayTags::Normalize(cur);
                    if (!t.empty()) out.push_back(t);
                    cur.clear();
                }
                else cur += c;
            }
            return out;
        }

        bool ParseModifiers(const std::string& text, std::vector<AttributeModifier>& out)
        {
            out.clear();
            std::string cur;
            bool ok = true;
            for (char c : text + ";")
            {
                if (c != ';' && c != '\n')
                {
                    cur += c;
                    continue;
                }
                std::string item = Trim(cur);
                cur.clear();
                if (item.empty()) continue;
                // "Attribute:op:value" ya da "Attribute op value" ya da "Attribute:value" (add)
                for (char& ch : item) if (ch == ':' || ch == ',') ch = ' ';
                std::istringstream in(item);
                AttributeModifier m;
                std::string a, b, c2;
                in >> a >> b >> c2;
                m.Attribute = a;
                std::string op = Lower(b);
                if (c2.empty()) { m.Op = ModifierOp::Add; m.Value = ToFloat(b, 0.0f); }
                else
                {
                    m.Op = op == "mul" || op == "multiply" || op == "*" ? ModifierOp::Multiply
                         : op == "override" || op == "set" || op == "=" ? ModifierOp::Override : ModifierOp::Add;
                    m.Value = ToFloat(c2, 0.0f);
                }
                if (m.Attribute.empty()) { ok = false; continue; }
                out.push_back(m);
            }
            return ok;
        }

        AbilitySystemComponent* Get(Entity entity, bool create)
        {
            if (!entity)
                return nullptr;
            if (!entity.HasComponent<AbilitySystemComponent>())
            {
                if (!create) return nullptr;
                auto& asc = entity.AddComponent<AbilitySystemComponent>();
                asc.Attributes.clear();
            }
            EnsureInitialized(entity);
            return &entity.GetComponent<AbilitySystemComponent>();
        }

        void EnsureInitialized(Entity entity)
        {
            auto& asc = entity.GetComponent<AbilitySystemComponent>();
            if (asc.Initialized)
                return;
            asc.Initialized = true;
            asc.AttributeList = ParseAttributes(asc.Attributes);
            asc.AbilityList = ParseAbilities(asc.Abilities);
            asc.Effects.clear();
            BuildState(asc);
        }

        GameplayAttribute* FindAttribute(Entity entity, const std::string& name)
        {
            AbilitySystemComponent* asc = Get(entity);
            if (!asc) return nullptr;
            for (auto& a : asc->AttributeList)
                if (SameName(a.Name, name)) return &a;
            return nullptr;
        }

        float GetAttribute(Entity entity, const std::string& name, float fallback)
        {
            GameplayAttribute* a = FindAttribute(entity, name);
            return a ? a->Current : fallback;
        }

        float GetAttributeBase(Entity entity, const std::string& name, float fallback)
        {
            GameplayAttribute* a = FindAttribute(entity, name);
            return a ? a->Base : fallback;
        }

        bool SetAttributeBase(Entity entity, const std::string& name, float value)
        {
            AbilitySystemComponent* asc = Get(entity);
            GameplayAttribute* a = FindAttribute(entity, name);
            if (!asc || !a) return false;
            a->Base = std::clamp(value, a->Min, a->Max);
            Recompute(entity, *asc, *a, true);
            return true;
        }

        bool DefineAttribute(Entity entity, const std::string& name, float base, float minValue, float maxValue)
        {
            AbilitySystemComponent* asc = Get(entity, true);
            if (!asc || name.empty()) return false;
            if (GameplayAttribute* a = FindAttribute(entity, name))
            {
                a->Min = minValue;
                a->Max = maxValue;
                a->Base = std::clamp(base, minValue, maxValue);
                Recompute(entity, *asc, *a, true);
                return true;
            }
            GameplayAttribute a;
            a.Name = name;
            a.Min = minValue;
            a.Max = maxValue;
            a.Base = a.Current = std::clamp(base, minValue, maxValue);
            asc->AttributeList.push_back(a);
            return true;
        }

        uint32_t ApplyEffect(Entity target, const GameplayEffectSpec& spec, uint32_t source)
        {
            AbilitySystemComponent* asc = Get(target);
            if (!asc) return 0;
            if (!spec.BlockedTags.empty() && GameplayTags::HasAny(target, spec.BlockedTags)) return 0;
            if (!spec.RequiredTags.empty() && !GameplayTags::HasAll(target, spec.RequiredTags)) return 0;

            // Anlık efekt: tabana uygula, iz bırakma
            if (spec.Duration == 0.0f && spec.Period <= 0.0f)
            {
                ExecuteOnBase(target, *asc, spec.Modifiers, 1);
                Notify(target, "OnEffectApplied", { CQS::Value::MakeString(spec.Name) });
                return 1;
            }
            // Yığılma
            if (!spec.Name.empty() && spec.Stacking != EffectStacking::None)
                for (auto& fx : asc->Effects)
                    if (SameName(fx.Spec.Name, spec.Name))
                    {
                        fx.Remaining = spec.Duration;
                        if (spec.Stacking == EffectStacking::Stack && fx.Stacks < std::max(1, spec.MaxStacks))
                        {
                            fx.Stacks++;
                            RecomputeFor(target, *asc, fx.Spec.Modifiers);
                        }
                        Notify(target, "OnEffectApplied", { CQS::Value::MakeString(spec.Name) });
                        return fx.Handle;
                    }
            ActiveGameplayEffect fx;
            fx.Handle = ++asc->NextHandle;
            fx.Spec = spec;
            fx.Remaining = spec.Duration;
            fx.PeriodTimer = spec.Period;
            fx.Source = source;
            asc->Effects.push_back(fx);
            const auto mods = spec.Modifiers;
            if (spec.Period > 0.0f && spec.ExecuteOnApply)
                ExecuteOnBase(target, *asc, mods, 1);
            else
                RecomputeFor(target, *asc, mods);
            Notify(target, "OnEffectApplied", { CQS::Value::MakeString(spec.Name) });
            BuildState(*asc);
            return fx.Handle;
        }

        int RemoveEffect(Entity target, uint32_t handle)
        {
            AbilitySystemComponent* asc = Get(target);
            if (!asc) return 0;
            for (size_t i = 0; i < asc->Effects.size(); ++i)
                if (asc->Effects[i].Handle == handle)
                {
                    ActiveGameplayEffect fx = asc->Effects[i];
                    asc->Effects.erase(asc->Effects.begin() + (long)i);
                    RecomputeFor(target, *asc, fx.Spec.Modifiers);
                    Notify(target, "OnEffectRemoved", { CQS::Value::MakeString(fx.Spec.Name) });
                    BuildState(*asc);
                    return 1;
                }
            return 0;
        }

        int RemoveEffectsByName(Entity target, const std::string& name)
        {
            AbilitySystemComponent* asc = Get(target);
            if (!asc) return 0;
            std::vector<uint32_t> handles;
            for (auto& fx : asc->Effects)
                if (SameName(fx.Spec.Name, name)) handles.push_back(fx.Handle);
            int n = 0;
            for (uint32_t h : handles) n += RemoveEffect(target, h);
            return n;
        }

        int GetEffectStacks(Entity target, const std::string& name)
        {
            AbilitySystemComponent* asc = Get(target);
            if (!asc) return 0;
            int n = 0;
            for (auto& fx : asc->Effects)
                if (SameName(fx.Spec.Name, name)) n += fx.Stacks;
            return n;
        }

        bool GrantAbility(Entity entity, const GameplayAbilitySpec& spec)
        {
            AbilitySystemComponent* asc = Get(entity, true);
            if (!asc || spec.Name.empty()) return false;
            for (auto& ab : asc->AbilityList)
                if (SameName(ab.Name, spec.Name)) { ab = spec; return true; }
            asc->AbilityList.push_back(spec);
            return true;
        }

        bool TryActivateAbility(Entity entity, const std::string& name)
        {
            AbilitySystemComponent* asc = Get(entity);
            if (!asc) return false;
            GameplayAbilitySpec* ab = nullptr;
            for (auto& a : asc->AbilityList)
                if (SameName(a.Name, name)) { ab = &a; break; }
            if (!ab) { asc->LastFailure = "unknown ability " + name; return false; }
            if (ab->CooldownRemaining > 0.0f) { asc->LastFailure = "cooldown"; return false; }
            if (!ab->BlockedTags.empty() && GameplayTags::HasAny(entity, ab->BlockedTags)) { asc->LastFailure = "blocked"; return false; }
            if (!ab->RequiredTags.empty() && !GameplayTags::HasAll(entity, ab->RequiredTags)) { asc->LastFailure = "missing required tags"; return false; }
            if (!ab->CostAttribute.empty() && ab->CostAmount != 0.0f)
            {
                GameplayAttribute* cost = FindAttribute(entity, ab->CostAttribute);
                if (!cost || cost->Current < ab->CostAmount) { asc->LastFailure = "cost"; return false; }
                cost->Base = std::clamp(cost->Base - ab->CostAmount, cost->Min, cost->Max);
                Recompute(entity, *asc, *cost, true);
            }
            ab->CooldownRemaining = ab->Cooldown;
            ab->Activations++;
            asc->LastFailure.clear();
            const std::string abilityName = ab->Name;
            Notify(entity, "OnAbilityActivated", { CQS::Value::MakeString(abilityName) });
            BuildState(*asc);
            return true;
        }

        float GetCooldownRemaining(Entity entity, const std::string& name)
        {
            AbilitySystemComponent* asc = Get(entity);
            if (!asc) return 0.0f;
            for (auto& a : asc->AbilityList)
                if (SameName(a.Name, name)) return std::max(0.0f, a.CooldownRemaining);
            return 0.0f;
        }

        void Update(Scene& scene, float dt)
        {
            if (dt <= 0.0f)
                return;
            auto view = scene.m_Registry.view<AbilitySystemComponent>();
            std::vector<entt::entity> entities(view.begin(), view.end());
            for (auto h : entities)
            {
                if (!scene.m_Registry.valid(h) || !scene.IsEntityActive(h))
                    continue;
                Entity e{ h, &scene };
                EnsureInitialized(e);
                auto& asc = e.GetComponent<AbilitySystemComponent>();
                for (auto& ab : asc.AbilityList)
                    if (ab.CooldownRemaining > 0.0f) ab.CooldownRemaining = std::max(0.0f, ab.CooldownRemaining - dt);
                // Periyodik tikler ve süre sonları (betik olayları listeyi değiştirebilir: tutamaçla ilerle)
                std::vector<uint32_t> handles;
                for (auto& fx : asc.Effects) handles.push_back(fx.Handle);
                for (uint32_t handle : handles)
                {
                    if (!scene.m_Registry.valid(h) || !e.HasComponent<AbilitySystemComponent>())
                        break;
                    auto& list = e.GetComponent<AbilitySystemComponent>().Effects;
                    auto it = std::find_if(list.begin(), list.end(), [&](const ActiveGameplayEffect& f) { return f.Handle == handle; });
                    if (it == list.end())
                        continue;
                    if (it->Spec.Period > 0.0f)
                    {
                        it->PeriodTimer -= dt;
                        int guard = 0;
                        while (it->PeriodTimer <= 0.0f && guard++ < 16)
                        {
                            it->PeriodTimer += it->Spec.Period;
                            const auto mods = it->Spec.Modifiers;
                            const int stacks = it->Stacks;
                            ExecuteOnBase(e, e.GetComponent<AbilitySystemComponent>(), mods, stacks);
                            auto& again = e.GetComponent<AbilitySystemComponent>().Effects;
                            it = std::find_if(again.begin(), again.end(), [&](const ActiveGameplayEffect& f) { return f.Handle == handle; });
                            if (it == again.end()) break;
                        }
                        if (it == e.GetComponent<AbilitySystemComponent>().Effects.end())
                            continue;
                    }
                    if (it->Spec.Duration > 0.0f)
                    {
                        it->Remaining -= dt;
                        if (it->Remaining <= 0.0f)
                            RemoveEffect(e, handle);
                    }
                }
                if (scene.m_Registry.valid(h) && e.HasComponent<AbilitySystemComponent>())
                    BuildState(e.GetComponent<AbilitySystemComponent>());
            }
        }
    }

    void RegisterAbilitySystemComponents()
    {
        GameplayTags::SetExtraSource(&ExtraTags);
        using A = AbilitySystemComponent;
        ComponentDef<A>("Ability System", "AbilitySystemComponent", "Gameplay", ICON_FA_WAND_MAGIC_SPARKLES)
            .Describe("Attributes, gameplay effects and abilities (Unreal GAS). Scripts: AttrGet, EffectApply, AbilityTryActivate ... Events: OnAttributeChanged, OnAbilityActivated, OnEffectApplied / OnEffectRemoved.")
            .Field("Attributes", "Attributes", &A::Attributes).Tooltip("One per line: Name Base [Min Max]   e.g. Health 100 0 100")
            .Field("Abilities", "Abilities", &A::Abilities).Tooltip("One per line: Name cooldown=2 cost=Mana:20 blocked=Status.Stunned required=State.Armed")
            .Field("ShowDebug", "Show Debug", &A::ShowDebug)
            .Field("State", "State", &A::State).ReadOnly();
    }
}
