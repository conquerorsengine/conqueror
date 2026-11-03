#include "AnimationStateMachine.h"
#include "Core/Animation/AvatarMask.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        constexpr int kMaxDepth = 16;

        AnimParameterRuntimeValue* FindParam(AnimationComponent& ac, const std::string& name)
        {
            for (auto& p : ac.Parameters)
                if (p.Name == name)
                    return &p;
            return nullptr;
        }

        const AnimParameterRuntimeValue* FindParam(const AnimationComponent& ac, const std::string& name)
        {
            for (const auto& p : ac.Parameters)
                if (p.Name == name)
                    return &p;
            return nullptr;
        }

        float AsFloat(const AnimParameterRuntimeValue& p)
        {
            switch (p.Type)
            {
                case AnimParameterType::Float: return p.FloatValue;
                case AnimParameterType::Int: return (float)p.IntValue;
                case AnimParameterType::Bool: return p.BoolValue ? 1.0f : 0.0f;
                case AnimParameterType::Trigger: return p.FloatValue;
            }
            return 0.0f;
        }

        bool EvaluateCondition(const AnimationComponent& ac, const AnimCondition& c)
        {
            const AnimParameterRuntimeValue* p = FindParam(ac, c.ParameterName);
            if (!p)
                return false;
            switch (p->Type)
            {
                case AnimParameterType::Trigger:
                    return c.Mode == AnimConditionMode::IfNot ? p->FloatValue <= 0.5f : p->FloatValue > 0.5f;
                case AnimParameterType::Bool:
                    if (c.Mode == AnimConditionMode::If) return p->BoolValue;
                    if (c.Mode == AnimConditionMode::IfNot) return !p->BoolValue;
                    if (c.Mode == AnimConditionMode::NotEquals) return p->BoolValue != (c.Threshold > 0.5f);
                    return p->BoolValue == (c.Threshold > 0.5f);
                case AnimParameterType::Int:
                {
                    int t = (int)std::lround(c.Threshold);
                    switch (c.Mode)
                    {
                        case AnimConditionMode::Greater: return p->IntValue > t;
                        case AnimConditionMode::Less: return p->IntValue < t;
                        case AnimConditionMode::Equals: return p->IntValue == t;
                        case AnimConditionMode::NotEquals: return p->IntValue != t;
                        case AnimConditionMode::If: return p->IntValue != 0;
                        case AnimConditionMode::IfNot: return p->IntValue == 0;
                    }
                    return false;
                }
                case AnimParameterType::Float:
                    switch (c.Mode)
                    {
                        case AnimConditionMode::Greater: return p->FloatValue > c.Threshold;
                        case AnimConditionMode::Less: return p->FloatValue < c.Threshold;
                        case AnimConditionMode::Equals: return std::abs(p->FloatValue - c.Threshold) < 1e-4f;
                        case AnimConditionMode::NotEquals: return std::abs(p->FloatValue - c.Threshold) >= 1e-4f;
                        case AnimConditionMode::If: return p->FloatValue != 0.0f;
                        case AnimConditionMode::IfNot: return p->FloatValue == 0.0f;
                    }
                    return false;
            }
            return false;
        }

        bool ConditionsMet(const AnimationComponent& ac, const AnimTransition& t)
        {
            for (const auto& c : t.Conditions)
                if (!EvaluateCondition(ac, c))
                    return false;
            return true;
        }

        // Yalnızca bu geçişte kullanılan trigger'ları tüket (Unity davranışı)
        void ConsumeTriggers(AnimationComponent& ac, const AnimTransition& t)
        {
            for (const auto& c : t.Conditions)
                if (auto* p = FindParam(ac, c.ParameterName); p && p->Type == AnimParameterType::Trigger)
                    p->FloatValue = 0.0f;
        }

        const AnimationClip* GetClip(const Model& model, int index)
        {
            if (index < 0 || index >= (int)model.Animations.size())
                return nullptr;
            return model.Animations[(size_t)index].get();
        }

        float ClipSeconds(const Model& model, int index)
        {
            const AnimationClip* clip = GetClip(model, index);
            return clip ? clip->DurationSeconds() : 0.0f;
        }

        float StateSpeed(const AnimState& state, const AnimationComponent& ac)
        {
            float speed = state.Speed * ac.Speed;
            if (!state.SpeedParameter.empty())
                if (const auto* p = FindParam(ac, state.SpeedParameter))
                    speed *= AsFloat(*p);
            return speed;
        }

        // ---- Anim Notify: (prev, norm] aralığında geçilen notify zamanları (döngüde her tur)
        void CrossingsOf(float prev, float norm, float t, bool loops, int kind, std::vector<std::pair<float, int>>& out)
        {
            if (norm <= prev)
                return;
            if (!loops)
            {
                if (prev < t && t <= norm)
                    out.push_back({ t, kind });
                return;
            }
            float k = std::floor(prev - t) + 1.0f;
            for (int i = 0; i < 4 && k + t <= norm; ++i, k += 1.0f)
                out.push_back({ k + t, kind });
        }

        void PushNotifyEvent(AnimationComponent& ac, const std::string& name, AnimNotifyEventKind kind, bool interrupted = false)
        {
            ac.PendingNotifies.push_back({ name, kind, interrupted });
            if (kind == AnimNotifyEventKind::Notify || kind == AnimNotifyEventKind::Begin)
                ++ac.NotifyCount;
        }

        // owner: notify state kaydının sahibi (durum adı ya da montaj)
        void FireNotifies(AnimationComponent& ac, std::vector<std::pair<std::string, std::string>>& active, const std::string& owner,
                          const std::vector<AnimNotify>& notifies, float prev, float norm, bool loops)
        {
            if (notifies.empty() || norm <= prev)
                return;
            for (const auto& nt : notifies)
            {
                if (nt.Duration <= 0.0f)
                {
                    std::vector<std::pair<float, int>> hits;
                    CrossingsOf(prev, norm, nt.Time, loops, 0, hits);
                    for (size_t i = 0; i < hits.size(); ++i)
                        PushNotifyEvent(ac, nt.Name, AnimNotifyEventKind::Notify);
                    continue;
                }
                // Notify State: başlangıç / bitiş geçişleri sırayla (bitiş, aynı noktadaki başlangıçtan önce)
                std::vector<std::pair<float, int>> hits;
                CrossingsOf(prev, norm, nt.Time, loops, 1, hits);
                const float endT = nt.Time + nt.Duration;
                CrossingsOf(prev, norm, loops ? endT : std::min(endT, 1.0f), loops, 0, hits);
                std::sort(hits.begin(), hits.end());
                const auto key = std::make_pair(owner, nt.Name);
                for (auto& [pos, isBegin] : hits)
                {
                    auto it = std::find(active.begin(), active.end(), key);
                    if (isBegin && it == active.end())
                    {
                        active.push_back(key);
                        PushNotifyEvent(ac, nt.Name, AnimNotifyEventKind::Begin);
                    }
                    else if (!isBegin && it != active.end())
                    {
                        active.erase(it);
                        PushNotifyEvent(ac, nt.Name, AnimNotifyEventKind::End);
                    }
                }
            }
        }

        // Sahibi artık oynamayan Notify State'leri kapat (durumdan erken çıkış)
        void CloseNotifyStates(AnimationComponent& ac, std::vector<std::pair<std::string, std::string>>& active,
                               const std::string& keepA, const std::string& keepB)
        {
            for (size_t i = 0; i < active.size();)
            {
                if (active[i].first == keepA || (!keepB.empty() && active[i].first == keepB))
                {
                    ++i;
                    continue;
                }
                PushNotifyEvent(ac, active[i].second, AnimNotifyEventKind::End);
                active.erase(active.begin() + (long)i);
            }
        }

        // Exit time'a bu karede ulaşıldı mı (döngülü durumlarda her turda bir kez)
        bool ExitTimeReached(float prevNorm, float norm, float exitTime, bool loops, bool firstFrame)
        {
            if (!loops || exitTime >= 1.0f)
                return norm >= exitTime;
            if (firstFrame && norm >= exitTime)
                return true;
            return std::floor(norm - exitTime) > std::floor(prevNorm - exitTime);
        }

        struct LayerContext
        {
            AnimationComponent& Component;
            const Model& ModelRef;
            const AnimLayer& Layer;
        };

        const AnimStateMachine* ParentOf(const AnimStateMachine& root, const AnimStateMachine* target)
        {
            for (const auto& sub : root.SubStates)
            {
                if (&sub == target)
                    return &root;
                if (const AnimStateMachine* p = ParentOf(sub, target))
                    return p;
            }
            return nullptr;
        }

        std::string EnterMachine(LayerContext& ctx, const AnimStateMachine& machine, int depth);

        std::string ResolveTarget(LayerContext& ctx, const AnimStateMachine& container, const std::string& name, int depth)
        {
            if (depth > kMaxDepth || name.empty())
                return {};
            for (const auto& s : container.States)
                if (s.Name == name)
                    return name;
            for (const auto& sub : container.SubStates)
                if (sub.Name == name)
                    return EnterMachine(ctx, sub, depth + 1);
            // Katmanın herhangi bir yerinde (iç içe) arama
            if (AnimationController::FindState(ctx.Layer, name))
                return name;
            if (const AnimStateMachine* sub = AnimationController::FindSubMachine(ctx.Layer, name))
                return EnterMachine(ctx, *sub, depth + 1);
            return {};
        }

        std::string EnterMachine(LayerContext& ctx, const AnimStateMachine& machine, int depth)
        {
            if (depth > kMaxDepth)
                return {};
            for (const auto& t : machine.Transitions)
            {
                if (t.Mute || t.FromState != AnimNodeNames::Entry)
                    continue;
                if (ConditionsMet(ctx.Component, t))
                {
                    ConsumeTriggers(ctx.Component, t);
                    std::string r = ResolveTarget(ctx, machine, t.ToState, depth + 1);
                    if (!r.empty())
                        return r;
                }
            }
            if (!machine.DefaultState.empty())
            {
                std::string r = ResolveTarget(ctx, machine, machine.DefaultState, depth + 1);
                if (!r.empty())
                    return r;
            }
            if (!machine.States.empty())
                return machine.States.front().Name;
            for (const auto& sub : machine.SubStates)
            {
                std::string r = EnterMachine(ctx, sub, depth + 1);
                if (!r.empty())
                    return r;
            }
            return {};
        }

        // Bir sub-state machine'den çıkış: üstteki "alt makineden" geçişler, yoksa üstün girişi
        std::string ExitMachine(LayerContext& ctx, const AnimStateMachine& machine, int depth)
        {
            if (depth > kMaxDepth)
                return {};
            const AnimStateMachine* parent = ParentOf(ctx.Layer, &machine);
            if (!parent)
                return EnterMachine(ctx, ctx.Layer, depth + 1);
            for (const auto& t : parent->Transitions)
            {
                if (t.Mute || t.FromState != machine.Name)
                    continue;
                if (ConditionsMet(ctx.Component, t))
                {
                    ConsumeTriggers(ctx.Component, t);
                    if (t.ToState == AnimNodeNames::Exit)
                        return ExitMachine(ctx, *parent, depth + 1);
                    std::string r = ResolveTarget(ctx, *parent, t.ToState, depth + 1);
                    if (!r.empty())
                        return r;
                }
            }
            return EnterMachine(ctx, *parent, depth + 1);
        }

        void CollectAnyStateTransitions(const AnimStateMachine& m, std::vector<std::pair<const AnimTransition*, const AnimStateMachine*>>& out)
        {
            for (const auto& t : m.Transitions)
                if (t.FromState == AnimNodeNames::AnyState)
                    out.push_back({ &t, &m });
            for (const auto& sub : m.SubStates)
                CollectAnyStateTransitions(sub, out);
        }

        // ---- Blend tree ağırlıkları
        float ParamValue(const AnimationComponent* ac, const std::string& name)
        {
            if (!ac || name.empty())
                return 0.0f;
            const auto* p = FindParam(*ac, name);
            return p ? AsFloat(*p) : 0.0f;
        }
    }

    float AnimationStateMachine::GetParameterFloat(const AnimationComponent& ac, const std::string& name, float fallback)
    {
        const auto* p = FindParam(ac, name);
        return p ? AsFloat(*p) : fallback;
    }

    void AnimationStateMachine::ComputeBlendWeights(const AnimState& state, const AnimationComponent* ac, std::vector<float>& w)
    {
        const size_t n = state.BlendChildren.size();
        w.assign(n, 0.0f);
        if (n == 0)
            return;
        if (n == 1)
        {
            w[0] = 1.0f;
            return;
        }

        switch (state.BlendType)
        {
            case AnimBlendTreeType::Simple1D:
            {
                float x = ParamValue(ac, state.BlendParameter);
                std::vector<size_t> order(n);
                for (size_t i = 0; i < n; ++i) order[i] = i;
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return state.BlendChildren[a].Threshold < state.BlendChildren[b].Threshold; });
                const auto& first = state.BlendChildren[order.front()];
                const auto& last = state.BlendChildren[order.back()];
                if (x <= first.Threshold) { w[order.front()] = 1.0f; return; }
                if (x >= last.Threshold) { w[order.back()] = 1.0f; return; }
                for (size_t k = 0; k + 1 < n; ++k)
                {
                    float a = state.BlendChildren[order[k]].Threshold;
                    float b = state.BlendChildren[order[k + 1]].Threshold;
                    if (x >= a && x <= b)
                    {
                        float t = (b - a) > 1e-6f ? (x - a) / (b - a) : 0.0f;
                        w[order[k]] = 1.0f - t;
                        w[order[k + 1]] = t;
                        return;
                    }
                }
                w[order.back()] = 1.0f;
                return;
            }
            case AnimBlendTreeType::Direct:
            {
                for (size_t i = 0; i < n; ++i)
                    w[i] = std::clamp(ParamValue(ac, state.BlendChildren[i].DirectParameter), 0.0f, 1.0f);
                return;
            }
            default:
            {
                // 2D: gradyan bant enterpolasyonu (Johansen) - kartezyen, yönlü türlerde de kararlı
                glm::vec2 p(ParamValue(ac, state.BlendParameter), ParamValue(ac, state.BlendParameterY));
                float total = 0.0f;
                for (size_t i = 0; i < n; ++i)
                {
                    glm::vec2 pi = state.BlendChildren[i].Position;
                    float weight = 1.0f;
                    for (size_t j = 0; j < n; ++j)
                    {
                        if (i == j)
                            continue;
                        glm::vec2 pij = state.BlendChildren[j].Position - pi;
                        float len2 = glm::dot(pij, pij);
                        if (len2 < 1e-8f)
                            continue;
                        float h = 1.0f - glm::dot(p - pi, pij) / len2;
                        weight = std::min(weight, std::clamp(h, 0.0f, 1.0f));
                    }
                    w[i] = weight;
                    total += weight;
                }
                if (total > 1e-6f)
                    for (auto& x : w) x /= total;
                else
                {
                    // Tüm ağırlıklar sıfır: en yakın çocuk
                    size_t best = 0;
                    float bestD = 1e30f;
                    for (size_t i = 0; i < n; ++i)
                    {
                        float d = glm::length(state.BlendChildren[i].Position - p);
                        if (d < bestD) { bestD = d; best = i; }
                    }
                    w[best] = 1.0f;
                }
                return;
            }
        }
    }

    float AnimationStateMachine::GetStateDuration(const AnimState& state, const Model& model, const AnimationComponent* ac)
    {
        if (state.Type != AnimStateType::BlendTree)
            return ClipSeconds(model, state.ClipIndex);
        std::vector<float> weights;
        ComputeBlendWeights(state, ac, weights);
        float duration = 0.0f, total = 0.0f;
        for (size_t i = 0; i < state.BlendChildren.size(); ++i)
        {
            const auto& child = state.BlendChildren[i];
            float d = ClipSeconds(model, child.ClipIndex) / std::max(std::abs(child.TimeScale), 1e-3f);
            duration += d * weights[i];
            total += weights[i];
        }
        return total > 1e-6f ? duration / total : 0.0f;
    }

    void AnimationStateMachine::SyncParameters(AnimationComponent& ac, const AnimationController& controller)
    {
        std::vector<AnimParameterRuntimeValue> synced;
        synced.reserve(controller.Parameters.size());
        for (const auto& def : controller.Parameters)
        {
            AnimParameterRuntimeValue v;
            v.Name = def.Name;
            v.Type = def.Type;
            v.FloatValue = def.Type == AnimParameterType::Float ? def.DefaultValue : 0.0f;
            v.BoolValue = def.DefaultValue > 0.5f;
            v.IntValue = (int)std::lround(def.DefaultValue);
            if (const auto* existing = FindParam(ac, def.Name); existing && existing->Type == def.Type)
                v = *existing; // çalışma zamanı değerini koru
            synced.push_back(v);
        }
        ac.Parameters = std::move(synced);
    }

    void AnimationStateMachine::Reset(AnimationComponent& ac)
    {
        ac.LayerRuntime.clear();
        ac.CurrentStateName.clear();
        ac.nextStateName.clear();
        ac.IsTransitioning = false;
        ac.TransitionProgress = 0.0f;
        ac.CurrentTime = 0.0f;
        ac.RuntimeController = nullptr;
    }

    namespace
    {
        void EvaluateStatePose(const AnimState& state, float stateTime, const AnimationComponent& ac, const Model& model,
                               const Skeleton& skeleton, const Pose& bind, Pose& out)
        {
            float duration = AnimationStateMachine::GetStateDuration(state, model, &ac);
            float norm;
            if (!state.MotionTimeParameter.empty())
                norm = AnimationStateMachine::GetParameterFloat(ac, state.MotionTimeParameter, 0.0f);
            else
                norm = duration > 1e-6f ? stateTime / duration : 0.0f;
            norm += state.CycleOffset;

            if (state.Type != AnimStateType::BlendTree)
            {
                const AnimationClip* clip = GetClip(model, state.ClipIndex);
                if (!clip)
                {
                    out = bind;
                    return;
                }
                Animator::SamplePose(skeleton, *clip, norm * clip->DurationSeconds(), state.Loop, out);
                return;
            }

            std::vector<float> weights;
            AnimationStateMachine::ComputeBlendWeights(state, &ac, weights);
            static thread_local Pose s_Child;
            Pose accumulator;
            Animator::BeginAccumulate(bind.size(), accumulator);
            float total = 0.0f;
            for (size_t i = 0; i < state.BlendChildren.size(); ++i)
            {
                if (weights[i] <= 1e-4f)
                    continue;
                const auto& child = state.BlendChildren[i];
                const AnimationClip* clip = GetClip(model, child.ClipIndex);
                if (!clip)
                    continue;
                Animator::SamplePose(skeleton, *clip, norm * clip->DurationSeconds(), state.Loop, s_Child);
                Animator::AccumulatePose(s_Child, weights[i], accumulator);
                total += weights[i];
            }
            Animator::NormalizeAccumulated(accumulator, total, bind);
            out = std::move(accumulator);
        }

        void StartTransition(LayerContext& ctx, AnimLayerRuntime& rt, const AnimTransition& t, const AnimStateMachine& container,
                             float currentDuration)
        {
            std::string target;
            if (t.ToState == AnimNodeNames::Exit)
                target = (&container == static_cast<const AnimStateMachine*>(&ctx.Layer)) ? EnterMachine(ctx, ctx.Layer, 0) : ExitMachine(ctx, container, 0);
            else
                target = ResolveTarget(ctx, container, t.ToState, 0);
            if (target.empty())
                return;

            ConsumeTriggers(ctx.Component, t);
            const AnimState* targetState = AnimationController::FindState(ctx.Layer, target);
            float targetDuration = targetState ? AnimationStateMachine::GetStateDuration(*targetState, ctx.ModelRef, &ctx.Component) : 0.0f;

            rt.ActiveTransition = t;
            rt.NextState = target;
            rt.NextTime = std::clamp(t.Offset, 0.0f, 1.0f) * targetDuration;
            rt.NextTimeDuration = targetDuration;
            rt.NextDurationState = target;
            rt.TransitionElapsed = 0.0f;
            rt.TransitionDuration = t.FixedDuration ? std::max(t.Duration, 0.0f) : std::max(t.Duration, 0.0f) * currentDuration;
            rt.InTransition = true;
            if (rt.TransitionDuration <= 1e-5f)
            {
                rt.CurrentState = rt.NextState;
                rt.StateTime = rt.NextTime;
                // Hedefin başlangıcındaki notify'lar sonraki karede ateşlensin diye bir tık geriden
                rt.PreviousNormalized = (targetDuration > 0.0f ? rt.StateTime / targetDuration : 0.0f) - 1e-4f;
                rt.InTransition = false;
                rt.NextState.clear();
            }
        }

        // Geçiş adaylarını sırayla değerlendir; biri başlarsa true
        bool TryTransitions(LayerContext& ctx, AnimLayerRuntime& rt, const std::string& fromState, float prevNorm, float norm,
                            bool loops, bool firstFrame, float duration, const AnimTransition* exclude)
        {
            const AnimStateMachine* owner = nullptr;
            const AnimState* fromDef = AnimationController::FindState(ctx.Layer, fromState, &owner);
            if (!fromDef || !owner)
                return false;

            auto evaluateList = [&](const std::vector<std::pair<const AnimTransition*, const AnimStateMachine*>>& list, bool fromAny) -> bool
            {
                bool anySolo = false;
                for (auto& [t, m] : list)
                    if (t->Solo && !t->Mute) anySolo = true;
                for (auto& [t, m] : list)
                {
                    if (t->Mute || (anySolo && !t->Solo) || t == exclude)
                        continue;
                    if (t->Conditions.empty() && !t->HasExitTime)
                        continue; // Unity: koşulsuz ve exit time'sız geçiş geçersizdir
                    if (fromAny && !t->CanTransitionToSelf)
                    {
                        std::string resolved = t->ToState;
                        if (resolved == fromState)
                            continue;
                    }
                    if (t->HasExitTime && !ExitTimeReached(prevNorm, norm, t->ExitTime, loops, firstFrame))
                        continue;
                    if (!ConditionsMet(ctx.Component, *t))
                        continue;
                    StartTransition(ctx, rt, *t, *m, duration);
                    if (rt.InTransition || rt.CurrentState != fromState)
                        return true;
                }
                return false;
            };

            std::vector<std::pair<const AnimTransition*, const AnimStateMachine*>> any;
            CollectAnyStateTransitions(ctx.Layer, any);
            if (evaluateList(any, true))
                return true;

            std::vector<std::pair<const AnimTransition*, const AnimStateMachine*>> own;
            for (const auto& t : owner->Transitions)
                if (t.FromState == fromState)
                    own.push_back({ &t, owner });
            return evaluateList(own, false);
        }

        void UpdateLayer(LayerContext& ctx, AnimLayerRuntime& rt, float dt)
        {
            auto& ac = ctx.Component;
            bool firstFrame = false;
            if (rt.CurrentState.empty() || !AnimationController::FindState(ctx.Layer, rt.CurrentState))
            {
                rt = AnimLayerRuntime{};
                rt.CurrentState = EnterMachine(ctx, ctx.Layer, 0);
                firstFrame = true;
                if (rt.CurrentState.empty())
                    return;
            }

            const AnimState* current = AnimationController::FindState(ctx.Layer, rt.CurrentState);
            if (!current)
                return;

            float duration = AnimationStateMachine::GetStateDuration(*current, ctx.ModelRef, &ac);
            // Faz koruma: blend tree süresi (ağırlıklar) değişince birikmiş saniye aynı normalize zamana ölçeklenir
            if (current->Type == AnimStateType::BlendTree && rt.TimeDurationState == rt.CurrentState && rt.TimeDuration > 1e-6f &&
                duration > 1e-6f)
                rt.StateTime *= duration / rt.TimeDuration;
            rt.TimeDuration = duration;
            rt.TimeDurationState = rt.CurrentState;
            rt.CurrentDuration = duration;
            rt.StateTime += dt * StateSpeed(*current, ac);
            if (!current->Loop && duration > 0.0f)
                rt.StateTime = std::clamp(rt.StateTime, 0.0f, duration);
            float norm = duration > 1e-6f ? rt.StateTime / duration : 1.0f;
            float prevNorm = firstFrame ? norm : rt.PreviousNormalized;
            if (!current->Notifies.empty() && current->MotionTimeParameter.empty())
                FireNotifies(ac, rt.ActiveNotifyStates, rt.CurrentState, current->Notifies,
                             (firstFrame ? -1e-4f : prevNorm) + current->CycleOffset, norm + current->CycleOffset, current->Loop);

            if (rt.InTransition)
            {
                const AnimState* next = AnimationController::FindState(ctx.Layer, rt.NextState);
                if (!next)
                {
                    rt.InTransition = false;
                }
                else
                {
                    float nextDuration = AnimationStateMachine::GetStateDuration(*next, ctx.ModelRef, &ac);
                    if (next->Type == AnimStateType::BlendTree && rt.NextDurationState == rt.NextState && rt.NextTimeDuration > 1e-6f &&
                        nextDuration > 1e-6f)
                        rt.NextTime *= nextDuration / rt.NextTimeDuration;
                    rt.NextTimeDuration = nextDuration;
                    rt.NextDurationState = rt.NextState;
                    const float nextPrevTime = rt.NextTime;
                    const bool nextFresh = rt.TransitionElapsed <= 0.0f;
                    rt.NextTime += dt * StateSpeed(*next, ac);
                    if (!next->Loop && nextDuration > 0.0f)
                        rt.NextTime = std::clamp(rt.NextTime, 0.0f, nextDuration);
                    rt.NextNormalized = nextDuration > 1e-6f ? rt.NextTime / nextDuration : 0.0f;
                    rt.TransitionElapsed += dt;
                    if (!next->Notifies.empty() && next->MotionTimeParameter.empty() && nextDuration > 1e-6f)
                        FireNotifies(ac, rt.ActiveNotifyStates, rt.NextState, next->Notifies,
                                     nextPrevTime / nextDuration - (nextFresh ? 1e-4f : 0.0f) + next->CycleOffset,
                                     rt.NextNormalized + next->CycleOffset, next->Loop);

                    // Kesinti (interruption): kaynak/hedef durumun geçişleri aktif geçişi bölebilir
                    auto mode = rt.ActiveTransition.Interruption;
                    if (mode != AnimInterruptionSource::None)
                    {
                        AnimTransition active = rt.ActiveTransition;
                        auto tryFrom = [&](const std::string& state, float sPrev, float sNorm, bool loops, float sDur) -> bool
                        {
                            AnimLayerRuntime candidate = rt;
                            candidate.InTransition = false;
                            candidate.CurrentState = rt.TransitionElapsed / std::max(rt.TransitionDuration, 1e-5f) > 0.5f ? rt.NextState : rt.CurrentState;
                            if (TryTransitions(ctx, candidate, state, sPrev, sNorm, loops, false, sDur, nullptr)
                                && candidate.NextState != rt.NextState)
                            {
                                rt = candidate;
                                return true;
                            }
                            return false;
                        };
                        bool interrupted = false;
                        bool sourceFirst = mode == AnimInterruptionSource::Source || mode == AnimInterruptionSource::SourceThenDestination;
                        bool allowSource = mode != AnimInterruptionSource::Destination;
                        bool allowDest = mode != AnimInterruptionSource::Source;
                        for (int pass = 0; pass < 2 && !interrupted; ++pass)
                        {
                            bool sourcePass = (pass == 0) == sourceFirst;
                            if (sourcePass && allowSource)
                                interrupted = tryFrom(rt.CurrentState, prevNorm, norm, current->Loop, duration);
                            else if (!sourcePass && allowDest)
                                interrupted = tryFrom(rt.NextState, rt.NextNormalized, rt.NextNormalized, next->Loop, nextDuration);
                        }
                        (void)active;
                        if (interrupted)
                        {
                            rt.PreviousNormalized = norm;
                            rt.CurrentNormalized = norm;
                            return;
                        }
                    }

                    if (rt.TransitionElapsed >= rt.TransitionDuration)
                    {
                        rt.CurrentState = rt.NextState;
                        rt.StateTime = rt.NextTime;
                        rt.TimeDuration = nextDuration;
                        rt.TimeDurationState = rt.CurrentState;
                        rt.InTransition = false;
                        rt.NextState.clear();
                        rt.CurrentDuration = nextDuration;
                        norm = rt.NextNormalized;
                        rt.PreviousNormalized = norm;
                        rt.CurrentNormalized = norm;
                        return;
                    }
                }
            }

            if (!rt.InTransition)
                TryTransitions(ctx, rt, rt.CurrentState, prevNorm, norm, current->Loop, firstFrame, duration, nullptr);

            rt.PreviousNormalized = norm;
            rt.CurrentNormalized = norm;
        }
    }

    namespace
    {
        const AnimationClip* MontageClip(const Model& model, const AnimMontage& m)
        {
            if (!m.ClipName.empty())
                for (const auto& clip : model.Animations)
                    if (clip && clip->Name == m.ClipName)
                        return clip.get();
            return GetClip(model, m.ClipIndex);
        }

        int SectionIndex(const AnimMontage& m, const std::string& name)
        {
            for (size_t i = 0; i < m.Sections.size(); ++i)
                if (m.Sections[i].Name == name)
                    return (int)i;
            return -1;
        }

        float SectionEnd(const AnimMontage& m, int si)
        {
            return (si >= 0 && si + 1 < (int)m.Sections.size()) ? m.Sections[(size_t)si + 1].Start : 1.0f;
        }

        // Bölümün sonrası: SetNextSection geçersiz kılması > bölümün Next alanı
        std::string NextOf(const AnimMontageInstance& mi, const AnimMontage& m, int si)
        {
            if (si < 0)
                return {};
            const std::string& name = m.Sections[(size_t)si].Name;
            for (auto& [from, to] : mi.NextSectionOverrides)
                if (from == name)
                    return to;
            return m.Sections[(size_t)si].Next;
        }

        void FinishMontage(AnimationComponent& ac)
        {
            AnimMontageInstance& mi = ac.Montage;
            const std::string name = mi.Montage ? mi.Montage->Name : std::string();
            for (auto& [owner, notify] : mi.ActiveNotifyStates)
                PushNotifyEvent(ac, notify, AnimNotifyEventKind::End);
            mi.ActiveNotifyStates.clear();
            if (!mi.BlendingOut)
                PushNotifyEvent(ac, name, AnimNotifyEventKind::MontageBlendingOut, mi.Interrupted);
            PushNotifyEvent(ac, name, AnimNotifyEventKind::MontageEnded, mi.Interrupted);
            mi.Active = false;
            mi.BlendingOut = false;
            mi.Weight = 0.0f;
        }

        void BeginBlendOut(AnimationComponent& ac, bool interrupted, float blendOut)
        {
            AnimMontageInstance& mi = ac.Montage;
            if (mi.BlendingOut)
                return;
            mi.BlendingOut = true;
            mi.Interrupted = interrupted;
            mi.BlendOutTime = std::max(blendOut, 0.0f);
            PushNotifyEvent(ac, mi.Montage ? mi.Montage->Name : std::string(), AnimNotifyEventKind::MontageBlendingOut, interrupted);
        }

        // Montaj zamanı, bölüm geçişleri, notify'lar ve karışma ağırlığı; pozu finalPose üstüne karıştırır
        void UpdateMontage(AnimationComponent& ac, const Model& model, const Skeleton& skeleton, float dt, Pose& finalPose)
        {
            AnimMontageInstance& mi = ac.Montage;
            if (!mi.Active)
                return;
            if (!mi.Montage)
            {
                mi.Active = false;
                return;
            }
            const AnimMontage& m = *mi.Montage;
            const AnimationClip* clip = MontageClip(model, m);
            const float len = clip ? clip->DurationSeconds() : 0.0f;
            if (!clip || len <= 1e-5f)
            {
                mi.Interrupted = true;
                FinishMontage(ac);
                return;
            }

            // Zaman ve bölümler (ileri oynatma)
            int si = SectionIndex(m, mi.Section);
            float prev = mi.Time / len - (mi.Fresh ? 1e-4f : 0.0f);
            mi.Fresh = false;
            bool reachedEnd = false;
            if (!mi.BlendingOut || mi.Time < len)
            {
                mi.Time += dt * mi.Rate;
                float norm = mi.Time / len;
                float sectionEnd = SectionEnd(m, si);
                for (int guard = 0; norm >= sectionEnd && guard < 16; ++guard)
                {
                    FireNotifies(ac, mi.ActiveNotifyStates, "montage", m.Notifies, prev, sectionEnd, false);
                    const std::string next = NextOf(mi, m, si);
                    const float overshoot = norm - sectionEnd;
                    const int ni = (next.empty() || next == "Stop") ? -1 : SectionIndex(m, next);
                    if (next.empty() && si >= 0 && si + 1 < (int)m.Sections.size())
                    {
                        // Zaman çizelgesinde sonraki bölüme akış
                        ++si;
                        mi.Section = m.Sections[(size_t)si].Name;
                        prev = sectionEnd;
                        sectionEnd = SectionEnd(m, si);
                        continue;
                    }
                    if (ni < 0)
                    {
                        // Bitti: son karede kal ve (henüz başlamadıysa) karışarak çık
                        reachedEnd = true;
                        mi.Time = sectionEnd * len;
                        norm = sectionEnd;
                        break;
                    }
                    // Bölüme atla (kendine atlama = döngü)
                    si = ni;
                    mi.Section = m.Sections[(size_t)ni].Name;
                    prev = m.Sections[(size_t)ni].Start - 1e-4f;
                    norm = std::min(m.Sections[(size_t)ni].Start + overshoot, SectionEnd(m, ni));
                    mi.Time = norm * len;
                    sectionEnd = SectionEnd(m, si);
                }
                if (!reachedEnd)
                    FireNotifies(ac, mi.ActiveNotifyStates, "montage", m.Notifies, prev, norm, false);

                // Bitişe BlendOut süresi kaldıysa çıkışa erken başla (bitişte ağırlık 0 olsun)
                if (!mi.BlendingOut)
                {
                    const std::string next = NextOf(mi, m, si);
                    const bool endsHere = next == "Stop" || (next.empty() && si + 1 >= (int)m.Sections.size())
                                          || (!next.empty() && SectionIndex(m, next) < 0);
                    const float remaining = (SectionEnd(m, si) - norm) * len / std::max(std::abs(mi.Rate), 1e-4f);
                    if (reachedEnd || (endsHere && remaining <= m.BlendOut))
                        BeginBlendOut(ac, false, reachedEnd ? std::min(m.BlendOut, 0.05f) : std::max(remaining, 0.0f));
                }
            }

            // Ağırlık
            if (mi.BlendingOut)
                mi.Weight -= mi.BlendOutTime > 1e-4f ? dt / mi.BlendOutTime : 1.0f;
            else
                mi.Weight = m.BlendIn > 1e-4f ? std::min(1.0f, mi.Weight + dt / m.BlendIn) : 1.0f;
            if (mi.Weight <= 0.0f && mi.BlendingOut)
            {
                FinishMontage(ac);
                return;
            }
            const float w = std::clamp(mi.Weight, 0.0f, 1.0f);
            const float ws = w * w * (3.0f - 2.0f * w);

            static thread_local Pose s_Montage;
            Animator::SamplePose(skeleton, *clip, std::min(mi.Time, len), false, s_Montage);
            if (m.FromBone.empty())
            {
                Animator::BlendPoses(finalPose, s_Montage, ws, finalPose);
                return;
            }
            // Layered Blend per Bone: yalnızca FromBone ve altındaki düğümler
            const auto& nodes = Animator::Flatten(skeleton);
            static thread_local std::vector<uint8_t> s_Mask;
            s_Mask.assign(nodes.size(), 0);
            const size_t n = std::min({ nodes.size(), finalPose.size(), s_Montage.size() });
            for (size_t i = 0; i < n; ++i)
            {
                const bool self = nodes[i].Node && nodes[i].Node->Name == m.FromBone;
                s_Mask[i] = self || (nodes[i].Parent >= 0 && s_Mask[(size_t)nodes[i].Parent]);
                if (!s_Mask[i])
                    continue;
                finalPose[i].Translation = glm::mix(finalPose[i].Translation, s_Montage[i].Translation, ws);
                finalPose[i].Rotation = glm::normalize(glm::slerp(finalPose[i].Rotation, s_Montage[i].Rotation, ws));
                finalPose[i].Scale = glm::mix(finalPose[i].Scale, s_Montage[i].Scale, ws);
            }
        }
    }

    float AnimationStateMachine::PlayMontage(AnimationComponent& ac, const Model* model, const std::shared_ptr<AnimMontage>& montage,
                                             float rate, const std::string& section)
    {
        if (!montage)
            return 0.0f;
        float len = 0.0f;
        if (model)
            if (const AnimationClip* clip = MontageClip(*model, *montage))
                len = clip->DurationSeconds();
        if (model && len <= 1e-5f)
            return 0.0f;
        float startWeight = 0.0f;
        if (ac.Montage.Active)
        {
            // Oynayan montaj kesilir; yeni montaj onun ağırlığından devralır (pat diye geçiş olmasın)
            startWeight = std::max(ac.Montage.Weight, 0.0f);
            ac.Montage.Interrupted = true;
            FinishMontage(ac);
        }
        AnimMontageInstance& mi = ac.Montage;
        mi = AnimMontageInstance{};
        mi.Montage = montage;
        mi.Active = true;
        mi.Rate = (rate == 0.0f ? 1.0f : rate) * (montage->Rate == 0.0f ? 1.0f : montage->Rate);
        mi.Weight = montage->BlendIn > 1e-4f ? startWeight : 1.0f;
        mi.BlendOutTime = montage->BlendOut;
        if (!montage->Sections.empty())
        {
            const AnimMontageSection* sec = section.empty() ? &montage->Sections.front() : montage->FindSection(section);
            if (!sec)
                sec = &montage->Sections.front();
            mi.Section = sec->Name;
            mi.Time = sec->Start * len;
        }
        if (!ac.Playing)
            ac.Playing = true;
        return len / std::max(std::abs(mi.Rate), 1e-4f);
    }

    void AnimationStateMachine::StopMontage(AnimationComponent& ac, float blendOut)
    {
        if (!ac.Montage.Active)
            return;
        BeginBlendOut(ac, true, blendOut >= 0.0f ? blendOut : (ac.Montage.Montage ? ac.Montage.Montage->BlendOut : 0.2f));
    }

    bool AnimationStateMachine::JumpToSection(AnimationComponent& ac, const Model* model, const std::string& section)
    {
        AnimMontageInstance& mi = ac.Montage;
        if (!mi.Active || !mi.Montage)
            return false;
        const AnimMontageSection* sec = mi.Montage->FindSection(section);
        if (!sec)
            return false;
        float len = 0.0f;
        if (model)
            if (const AnimationClip* clip = MontageClip(*model, *mi.Montage))
                len = clip->DurationSeconds();
        mi.Section = sec->Name;
        mi.Time = sec->Start * len;
        mi.Fresh = true;
        return true;
    }

    bool AnimationStateMachine::SetNextSection(AnimationComponent& ac, const std::string& from, const std::string& to)
    {
        AnimMontageInstance& mi = ac.Montage;
        if (!mi.Montage || !mi.Montage->FindSection(from))
            return false;
        for (auto& [f, t] : mi.NextSectionOverrides)
            if (f == from)
            {
                t = to;
                return true;
            }
        mi.NextSectionOverrides.push_back({ from, to });
        return true;
    }

    void AnimationStateMachine::Update(AnimationComponent& ac, const Model& model, float dt, const std::function<void(Pose&)>* postPose)
    {
        if (!model.SkeletonData)
            return;
        const bool hasController = ac.Controller && !ac.Controller->Layers.empty();
        if (!hasController && !ac.Montage.Active)
            return;
        // Animator Override Controller: değiştirilen kliplerin dizinleri bu modelden ada göre
        if (hasController && ac.Controller->PendingClipResolve)
            ac.Controller->ResolveClips(model);

        const Skeleton& skeleton = *model.SkeletonData;
        static thread_local Pose s_Bind, s_Final, s_LayerPose, s_NextPose;
        Animator::BindPose(skeleton, s_Bind);
        s_Final = s_Bind;

        if (hasController)
        {
            const AnimationController& controller = *ac.Controller;
            if (ac.RuntimeController != ac.Controller.get())
            {
                ac.LayerRuntime.clear();
                ac.RuntimeController = ac.Controller.get();
            }
            if (ac.Parameters.size() != controller.Parameters.size())
                SyncParameters(ac, controller);
            if (ac.LayerRuntime.size() != controller.Layers.size())
                ac.LayerRuntime.resize(controller.Layers.size());

            bool anySolo = false;
            for (const auto& layer : controller.Layers)
                if (layer.Solo) anySolo = true;

            for (size_t li = 0; li < controller.Layers.size(); ++li)
            {
                const AnimLayer& layer = controller.Layers[li];
                AnimLayerRuntime& rt = ac.LayerRuntime[li];
                LayerContext ctx{ ac, model, layer };
                UpdateLayer(ctx, rt, dt);
                if (!rt.ActiveNotifyStates.empty())
                    CloseNotifyStates(ac, rt.ActiveNotifyStates, rt.CurrentState, rt.InTransition ? rt.NextState : std::string());

                bool active = !layer.Mute && (!anySolo || layer.Solo);
                float weight = (li == 0) ? 1.0f : std::clamp(layer.Weight, 0.0f, 1.0f);
                if (li > 0 && !layer.WeightParameter.empty())
                    if (const auto* p = FindParam(ac, layer.WeightParameter))
                        weight *= std::clamp(AsFloat(*p), 0.0f, 1.0f);
                rt.EffectiveWeight = active ? weight : 0.0f;
                if (!active || weight <= 1e-4f || rt.CurrentState.empty())
                    continue;

                const AnimState* current = AnimationController::FindState(layer, rt.CurrentState);
                if (!current)
                    continue;
                EvaluateStatePose(*current, rt.StateTime, ac, model, skeleton, s_Bind, s_LayerPose);
                if (rt.InTransition)
                {
                    if (const AnimState* next = AnimationController::FindState(layer, rt.NextState))
                    {
                        EvaluateStatePose(*next, rt.NextTime, ac, model, skeleton, s_Bind, s_NextPose);
                        float t = rt.TransitionDuration > 1e-5f ? rt.TransitionElapsed / rt.TransitionDuration : 1.0f;
                        // Yumuşak giriş/çıkış (smoothstep) geçişleri daha doğal yapar
                        t = std::clamp(t, 0.0f, 1.0f);
                        t = t * t * (3.0f - 2.0f * t);
                        Animator::BlendPoses(s_LayerPose, s_NextPose, t, s_LayerPose);
                    }
                }

                if (li == 0)
                    s_Final = s_LayerPose;
                else if (layer.Blending == AnimLayerBlending::Additive)
                    Animator::ApplyAdditive(s_Final, s_LayerPose, s_Bind, weight);
                else if (const AvatarMask* mask = layer.AvatarMask.empty() ? nullptr : AvatarMasks::Get(layer.AvatarMask))
                {
                    // Avatar Mask: insan bölümleri ya da seçili kemikler (Unity katman maskesi)
                    const auto& nodes = Animator::Flatten(skeleton);
                    static thread_local std::vector<std::string> s_Names;
                    static thread_local std::vector<int> s_Parents;
                    static thread_local std::vector<uint8_t> s_MaskBits;
                    s_Names.resize(nodes.size());
                    s_Parents.resize(nodes.size());
                    for (size_t i = 0; i < nodes.size(); ++i)
                    {
                        s_Names[i] = nodes[i].Node ? nodes[i].Node->Name : std::string();
                        s_Parents[i] = nodes[i].Parent;
                    }
                    AvatarMasks::BuildNodeMask(*mask, s_Names, s_Parents, s_MaskBits);
                    const size_t n = std::min({ nodes.size(), s_Final.size(), s_LayerPose.size() });
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (!s_MaskBits[i])
                            continue;
                        s_Final[i].Translation = glm::mix(s_Final[i].Translation, s_LayerPose[i].Translation, weight);
                        s_Final[i].Rotation = glm::normalize(glm::slerp(s_Final[i].Rotation, s_LayerPose[i].Rotation, weight));
                        s_Final[i].Scale = glm::mix(s_Final[i].Scale, s_LayerPose[i].Scale, weight);
                    }
                }
                else if (!layer.FromBone.empty())
                {
                    // Kemik maskesi: yalnızca FromBone ve altındaki düğümler
                    const auto& nodes = Animator::Flatten(skeleton);
                    static thread_local std::vector<uint8_t> s_LayerMask;
                    s_LayerMask.assign(nodes.size(), 0);
                    const size_t n = std::min({ nodes.size(), s_Final.size(), s_LayerPose.size() });
                    for (size_t i = 0; i < n; ++i)
                    {
                        const bool self = nodes[i].Node && nodes[i].Node->Name == layer.FromBone;
                        s_LayerMask[i] = self || (nodes[i].Parent >= 0 && s_LayerMask[(size_t)nodes[i].Parent]);
                        if (!s_LayerMask[i])
                            continue;
                        s_Final[i].Translation = glm::mix(s_Final[i].Translation, s_LayerPose[i].Translation, weight);
                        s_Final[i].Rotation = glm::normalize(glm::slerp(s_Final[i].Rotation, s_LayerPose[i].Rotation, weight));
                        s_Final[i].Scale = glm::mix(s_Final[i].Scale, s_LayerPose[i].Scale, weight);
                    }
                }
                else
                    Animator::BlendPoses(s_Final, s_LayerPose, weight, s_Final);
            }
        }

        // Montaj: durum makinesinin üstünde (Unreal "DefaultSlot")
        UpdateMontage(ac, model, skeleton, dt, s_Final);

        if (postPose && *postPose)
            (*postPose)(s_Final);
        Animator::PoseToSkinning(skeleton, s_Final, ac.BoneMatricesGPU);
        ac.FinalPose = s_Final;

        // Eski alanlar (Inspector / scriptler) taban katmandan
        if (hasController && !ac.LayerRuntime.empty())
        {
            const AnimLayerRuntime& base = ac.LayerRuntime[0];
            ac.CurrentStateName = base.CurrentState;
            ac.nextStateName = base.NextState;
            ac.IsTransitioning = base.InTransition;
            ac.TransitionProgress = base.TransitionElapsed;
            ac.TransitionDuration = base.TransitionDuration;
            ac.CurrentTime = base.StateTime;
        }
    }
}
