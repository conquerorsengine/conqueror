#include "InputActionRuntime.h"

#include "Core/Input/GameInput.h"
#include "Core/Input/Input.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Conqueror::InputActionRuntime
{
    namespace
    {
        constexpr float kDefaultPressPoint = 0.5f;
        constexpr float kReleaseFactor = 0.75f;   // bırakma noktası = basma noktası * 0.75 (Unity)
        constexpr float kStickDeadzoneMin = 0.125f;
        constexpr float kStickDeadzoneMax = 0.925f;

        glm::vec2 StickDeadzone(glm::vec2 v, float mn, float mx)
        {
            const float len = glm::length(v);
            if (len < mn || len <= 0.0f)
                return glm::vec2(0.0f);
            const float scaled = std::min(1.0f, (len - mn) / std::max(mx - mn, 1e-5f));
            return v / len * scaled;
        }

        float AxisDeadzone(float x, float mn, float mx)
        {
            const float a = std::fabs(x);
            if (a < mn)
                return 0.0f;
            return std::copysign(std::min(1.0f, (a - mn) / std::max(mx - mn, 1e-5f)), x);
        }

        bool PadConnected(int pad)
        {
            return pad >= 0 && Input::IsGamepadConnected(pad);
        }

        // Okunacak gamepad'ler
        void Pads(const InputDeviceFilter& d, int* out, int& count)
        {
            count = 0;
            if (d.Gamepad >= 0)
            {
                if (PadConnected(d.Gamepad))
                    out[count++] = d.Gamepad;
            }
            else if (d.Gamepad == -2)
                for (int p = 0; p < 16 && count < 16; ++p)
                    if (Input::IsGamepadConnected(p))
                        out[count++] = p;
        }

        glm::vec2 ReadStick(int stick, int pad)
        {
            glm::vec2 raw = stick == 0 ? Input::GetGamepadLeftStick(pad) : Input::GetGamepadRightStick(pad);
            raw.y = -raw.y;   // GLFW: aşağı artı; Input System: yukarı artı
            return StickDeadzone(raw, kStickDeadzoneMin, kStickDeadzoneMax);
        }

        glm::vec2 ReadPadControl(const InputControlInfo& c, int pad)
        {
            switch (c.Source)
            {
            case InputControlSource::PadButton: return glm::vec2(Input::IsGamepadButtonPressed(c.Code, pad) ? 1.0f : 0.0f, 0.0f);
            case InputControlSource::PadTrigger:
                return glm::vec2(c.Code == 0 ? Input::GetGamepadLeftTrigger(pad) : Input::GetGamepadRightTrigger(pad), 0.0f);
            case InputControlSource::PadStick: return ReadStick(c.Code, pad);
            case InputControlSource::PadStickAxis:
            {
                const glm::vec2 s = ReadStick(c.Code, pad);
                return glm::vec2(c.Extra == 0 ? s.x : s.y, 0.0f);
            }
            case InputControlSource::PadStickDir:
            {
                const glm::vec2 s = ReadStick(c.Code, pad);
                const float v = c.Extra == 0 ? std::max(0.0f, s.y) : c.Extra == 1 ? std::max(0.0f, -s.y) : c.Extra == 2 ? std::max(0.0f, -s.x) : std::max(0.0f, s.x);
                return glm::vec2(v, 0.0f);
            }
            case InputControlSource::PadDpad:
            {
                auto b = [&](int i) { return Input::IsGamepadButtonPressed(i, pad) ? 1.0f : 0.0f; };
                return glm::vec2(b(12) - b(14), b(11) - b(13));
            }
            default: return glm::vec2(0.0f);
            }
        }

        float Magnitude(glm::vec2 v, bool vector)
        {
            return vector ? glm::length(v) : std::fabs(v.x);
        }

        // Composite: kökten sonraki parçalar
        bool EvaluateComposite(const InputBindingDef& root, const std::vector<const InputBindingDef*>& parts, const std::string& scheme,
                               const InputDeviceFilter& devices, glm::vec2& out)
        {
            // Parça başına en büyük değer (bir parçaya birden çok bağlama olabilir)
            auto part = [&](const char* name, bool& any) {
                float best = 0.0f;
                for (const auto* p : parts)
                {
                    if (p->Part != name || !p->InScheme(scheme))
                        continue;
                    const InputControlInfo* c = InputActionAssets::FindControl(p->Path);
                    if (!c || !DeviceAvailable(c->Device, devices))
                        continue;
                    any = true;
                    glm::vec2 v = ApplyProcessors(p->Processors, ReadControl(*c, devices), c->Is2D);
                    best = std::max(best, Magnitude(v, c->Is2D));
                }
                return best;
            };
            bool any = false;
            const std::string& type = root.Composite;
            if (type == "2DVector")
            {
                const float up = part("up", any), down = part("down", any), left = part("left", any), right = part("right", any);
                const std::string mode = InputActionAssets::Param(root.CompositeParams, "mode", "DigitalNormalized");
                auto digital = [](float v) { return v >= kDefaultPressPoint ? 1.0f : 0.0f; };
                if (mode == "Analog")
                    out = glm::vec2(right - left, up - down);
                else
                {
                    out = glm::vec2(digital(right) - digital(left), digital(up) - digital(down));
                    if (mode == "DigitalNormalized" && glm::length(out) > 0.0f)
                        out = glm::normalize(out);
                }
            }
            else if (type == "1DAxis")
            {
                const float neg = part("negative", any), pos = part("positive", any);
                const std::string wins = InputActionAssets::Param(root.CompositeParams, "whichSideWins", "Neither");
                const bool negOn = neg > 0.0f, posOn = pos > 0.0f;
                if (negOn && posOn)
                    out = glm::vec2(wins == "Positive" ? pos : wins == "Negative" ? -neg : 0.0f, 0.0f);
                else
                    out = glm::vec2(pos - neg, 0.0f);
            }
            else if (type == "OneModifier" || type == "TwoModifiers")
            {
                // Bağlama değeri, değiştirici(ler) basılıyken geçer (Ctrl+S gibi)
                bool modsDown = true;
                if (type == "OneModifier")
                    modsDown = part("modifier", any) >= kDefaultPressPoint;
                else
                    modsDown = part("modifier1", any) >= kDefaultPressPoint && part("modifier2", any) >= kDefaultPressPoint;
                glm::vec2 value(0.0f);
                for (const auto* p : parts)
                {
                    if (p->Part != "binding" || !p->InScheme(scheme))
                        continue;
                    const InputControlInfo* c = InputActionAssets::FindControl(p->Path);
                    if (!c || !DeviceAvailable(c->Device, devices))
                        continue;
                    any = true;
                    glm::vec2 v = ApplyProcessors(p->Processors, ReadControl(*c, devices), c->Is2D);
                    if (Magnitude(v, true) > Magnitude(value, true))
                        value = v;
                }
                out = modsDown ? value : glm::vec2(0.0f);
            }
            else
                return false;
            return any;
        }

        // Bir etkileşimin bu karedeki geçişleri
        void Step(const InputActionAssets::Spec& s, InputInteractionState& m, float mag, float t, std::vector<InputActionPhase>& out)
        {
            const float pressPoint = s.Get("pressPoint", kDefaultPressPoint);
            const bool down = mag >= pressPoint;
            const bool up = mag <= pressPoint * kReleaseFactor;
            using P = InputActionPhase;
            if (s.Name == "hold")
            {
                const float dur = s.Get("duration", 0.4f);
                if (m.Phase == P::Waiting && down)
                {
                    m.Phase = P::Started;
                    m.PressTime = t;
                    m.Performed = false;
                    out.push_back(P::Started);
                }
                if (m.Phase == P::Started && !m.Performed && !up && t - m.PressTime >= dur)
                {
                    m.Phase = P::Performed;
                    m.Performed = true;
                    out.push_back(P::Performed);
                }
                if ((m.Phase == P::Started || m.Phase == P::Performed) && up)
                {
                    m.Phase = P::Waiting;
                    out.push_back(P::Canceled);
                }
            }
            else if (s.Name == "tap")
            {
                const float dur = s.Get("duration", 0.2f);
                if (m.Phase == P::Waiting && down)
                {
                    m.Phase = P::Started;
                    m.PressTime = t;
                    out.push_back(P::Started);
                }
                else if (m.Phase == P::Started && up)
                {
                    out.push_back(t - m.PressTime <= dur ? P::Performed : P::Canceled);
                    m.Phase = P::Waiting;
                }
                else if (m.Phase == P::Started && t - m.PressTime > dur)
                {
                    out.push_back(P::Canceled);
                    m.Phase = P::Canceled;   // bırakılana kadar yeniden başlamaz
                }
                else if (m.Phase == P::Canceled && up)
                    m.Phase = P::Waiting;
            }
            else if (s.Name == "slowTap")
            {
                const float dur = s.Get("duration", 0.5f);
                if (m.Phase == P::Waiting && down)
                {
                    m.Phase = P::Started;
                    m.PressTime = t;
                    out.push_back(P::Started);
                }
                else if (m.Phase == P::Started && up)
                {
                    out.push_back(t - m.PressTime >= dur ? P::Performed : P::Canceled);
                    m.Phase = P::Waiting;
                }
            }
            else if (s.Name == "multiTap")
            {
                const float tapTime = s.Get("tapTime", 0.2f), tapDelay = s.Get("tapDelay", 0.75f);
                const int tapCount = std::max(1, (int)std::lround(s.Get("tapCount", 2.0f)));
                if (m.Phase == P::Waiting && down)
                {
                    m.Phase = P::Started;
                    m.Pressed = true;
                    m.PressTime = t;
                    m.Taps = 0;
                    out.push_back(P::Started);
                }
                else if (m.Phase == P::Started)
                {
                    if (m.Pressed)
                    {
                        if (up)
                        {
                            m.Pressed = false;
                            if (t - m.PressTime <= tapTime)
                            {
                                ++m.Taps;
                                m.ReleaseTime = t;
                                if (m.Taps >= tapCount)
                                {
                                    out.push_back(P::Performed);
                                    m.Phase = P::Waiting;
                                }
                            }
                            else
                            {
                                out.push_back(P::Canceled);
                                m.Phase = P::Waiting;
                            }
                        }
                        else if (t - m.PressTime > tapTime)
                        {
                            out.push_back(P::Canceled);
                            m.Phase = P::Canceled;
                        }
                    }
                    else if (down)
                    {
                        m.Pressed = true;
                        m.PressTime = t;
                    }
                    else if (t - m.ReleaseTime > tapDelay)
                    {
                        out.push_back(P::Canceled);
                        m.Phase = P::Waiting;
                    }
                }
                else if (m.Phase == P::Canceled && up)
                {
                    m.Phase = P::Waiting;
                    m.Pressed = false;
                }
            }
            else   // press (bilinmeyen ad da Press Only gibi)
            {
                const int behavior = s.Name == "press" ? (int)std::lround(s.Get("behavior", 0.0f)) : 0;
                if (!m.Pressed && down)
                {
                    m.Pressed = true;
                    out.push_back(P::Started);
                    if (behavior != 1)
                    {
                        out.push_back(P::Performed);
                        m.Phase = P::Performed;
                    }
                    else
                        m.Phase = P::Started;
                }
                else if (m.Pressed && up)
                {
                    m.Pressed = false;
                    out.push_back(behavior == 0 ? P::Canceled : P::Performed);
                    m.Phase = P::Waiting;
                }
            }
        }

        std::chrono::steady_clock::time_point s_Start = std::chrono::steady_clock::now();

        // Project-wide
        std::string s_ProjectWideAsset;
        std::unordered_map<std::string, InputActionPlayerState> s_ProjectWideMaps;
        std::vector<std::string> s_ProjectWideOrder;
    }

    float Now()
    {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
    }

    const char* PhaseName(InputActionPhase phase)
    {
        switch (phase)
        {
        case InputActionPhase::Disabled: return "Disabled";
        case InputActionPhase::Started: return "Started";
        case InputActionPhase::Performed: return "Performed";
        case InputActionPhase::Canceled: return "Canceled";
        default: return "Waiting";
        }
    }

    bool DeviceAvailable(const std::string& device, const InputDeviceFilter& d)
    {
        if (device == "Keyboard" || device == "Mouse")
            return d.KeyboardMouse;
        if (device == "Gamepad")
        {
            int pads[16], n = 0;
            Pads(d, pads, n);
            return n > 0;
        }
        return false;
    }

    glm::vec2 ReadControl(const InputControlInfo& c, const InputDeviceFilter& d)
    {
        switch (c.Source)
        {
        case InputControlSource::Key: return d.KeyboardMouse && GameInput::IsKeyHeld((KeyCode)c.Code) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f);
        case InputControlSource::MouseButton:
            return d.KeyboardMouse && GameInput::IsMouseHeld((MouseCode)c.Code) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f);
        case InputControlSource::MouseDelta: return d.KeyboardMouse ? GameInput::GetMouseDelta() * glm::vec2(1.0f, -1.0f) : glm::vec2(0.0f);
        case InputControlSource::MouseScroll: return d.KeyboardMouse ? glm::vec2(0.0f, GameInput::GetScroll()) : glm::vec2(0.0f);
        case InputControlSource::MouseScrollY: return d.KeyboardMouse ? glm::vec2(GameInput::GetScroll(), 0.0f) : glm::vec2(0.0f);
        case InputControlSource::MousePosition: return d.KeyboardMouse ? GameInput::GetMousePosition() : glm::vec2(0.0f);
        default: break;
        }
        // Gamepad: oyuncunun gamepad'i (ya da bağlı her gamepad: en çok basılanı)
        int pads[16], n = 0;
        Pads(d, pads, n);
        glm::vec2 best(0.0f);
        for (int i = 0; i < n; ++i)
        {
            glm::vec2 v = ReadPadControl(c, pads[i]);
            if (Magnitude(v, c.Is2D) > Magnitude(best, c.Is2D))
                best = v;
        }
        return best;
    }

    glm::vec2 ApplyProcessors(const std::string& processors, glm::vec2 v, bool vector)
    {
        if (processors.empty())
            return v;
        for (const auto& s : InputActionAssets::ParseSpecs(processors))
        {
            if (s.Name == "invert")
                v = -v;
            else if (s.Name == "invertVector2")
            {
                if (s.Get("invertX", 1.0f) != 0.0f) v.x = -v.x;
                if (s.Get("invertY", 1.0f) != 0.0f) v.y = -v.y;
            }
            else if (s.Name == "normalize")
            {
                const float mn = s.Get("min", 0.0f), mx = s.Get("max", 1.0f), zero = s.Get("zero", 0.0f);
                auto norm = [&](float x) {
                    if (zero > mn)
                        return x >= zero ? (x - zero) / std::max(mx - zero, 1e-5f) : (x - zero) / std::max(zero - mn, 1e-5f);
                    return (x - mn) / std::max(mx - mn, 1e-5f);
                };
                v.x = norm(v.x);
                if (vector) v.y = norm(v.y);
            }
            else if (s.Name == "normalizeVector2")
            {
                if (glm::length(v) > 0.0f) v = glm::normalize(v);
            }
            else if (s.Name == "scale")
                v *= s.Get("factor", 1.0f);
            else if (s.Name == "scaleVector2")
            {
                v.x *= s.Get("x", 1.0f);
                v.y *= s.Get("y", 1.0f);
            }
            else if (s.Name == "clamp")
            {
                const float mn = s.Get("min", 0.0f), mx = s.Get("max", 1.0f);
                v.x = std::clamp(v.x, mn, mx);
                if (vector) v.y = std::clamp(v.y, mn, mx);
            }
            else if (s.Name == "axisDeadzone")
            {
                v.x = AxisDeadzone(v.x, s.Get("min", 0.125f), s.Get("max", 0.925f));
                if (vector) v.y = AxisDeadzone(v.y, s.Get("min", 0.125f), s.Get("max", 0.925f));
            }
            else if (s.Name == "stickDeadzone")
                v = StickDeadzone(v, s.Get("min", 0.125f), s.Get("max", 0.925f));
        }
        return v;
    }

    void Evaluate(const InputActionMapDef& map, const std::string& scheme, const InputDeviceFilter& devices, float time,
                  InputActionPlayerState& state, std::vector<InputActionEvent>* events)
    {
        using P = InputActionPhase;
        for (const auto& action : map.Actions)
        {
            InputActionState& st = state.Actions[action.Name];
            st.PressedThisFrame = st.ReleasedThisFrame = false;
            st.StartedThisFrame = st.PerformedThisFrame = st.CanceledThisFrame = false;
            if (st.Phase == P::Canceled)
                st.Phase = P::Waiting;

            // ---- 1) bağlamalar: en çok basılan kazanır
            bool vector = action.Control == InputControlKind::Vector2;
            glm::vec2 best(0.0f);
            float bestMag = -1.0f;
            std::string bestInteractions;
            auto consider = [&](glm::vec2 v, bool is2D, const std::string& interactions) {
                if (action.Control == InputControlKind::Any && is2D)
                    vector = true;
                const float mag = Magnitude(v, is2D);
                if (mag > bestMag)
                {
                    best = is2D ? v : glm::vec2(v.x, 0.0f);
                    bestMag = mag;
                    bestInteractions = mag > 0.0f ? interactions : std::string();
                }
            };
            const auto& bindings = action.Bindings;
            for (size_t i = 0; i < bindings.size();)
            {
                const InputBindingDef& b = bindings[i];
                if (b.IsComposite())
                {
                    std::vector<const InputBindingDef*> parts;
                    size_t j = i + 1;
                    while (j < bindings.size() && bindings[j].IsPart())
                        parts.push_back(&bindings[j++]);
                    glm::vec2 v(0.0f);
                    if (b.InScheme(scheme) && EvaluateComposite(b, parts, scheme, devices, v))
                    {
                        const bool is2D = b.Composite == "2DVector";
                        consider(ApplyProcessors(b.Processors, v, is2D), is2D, b.Interactions);
                    }
                    i = j;
                    continue;
                }
                if (!b.IsPart() && b.InScheme(scheme))
                    if (const InputControlInfo* c = InputActionAssets::FindControl(b.Path))
                        if (DeviceAvailable(c->Device, devices))
                            consider(ApplyProcessors(b.Processors, ReadControl(*c, devices), c->Is2D), c->Is2D, b.Interactions);
                ++i;
            }
            if (bestMag < 0.0f)
                best = glm::vec2(0.0f);

            // ---- 2) eylem işlemcileri, kontrol türü
            glm::vec2 value = ApplyProcessors(action.Processors, best, vector);
            if (!vector)
                value.y = 0.0f;
            float mag = Magnitude(value, vector);
            if (action.Control == InputControlKind::Button)
                value = glm::vec2(mag, 0.0f);

            auto emit = [&](P phase) {
                st.Phase = phase;
                if (phase == P::Started)
                {
                    st.StartedThisFrame = true;
                    st.StartTime = time;
                }
                else if (phase == P::Performed)
                {
                    st.PerformedThisFrame = true;
                    st.Duration = st.StartTime >= 0.0f ? time - st.StartTime : 0.0f;
                }
                else if (phase == P::Canceled)
                {
                    st.CanceledThisFrame = true;
                    st.Duration = st.StartTime >= 0.0f ? time - st.StartTime : 0.0f;
                }
                if (events)
                    events->push_back({ action.Name, phase, phase == P::Canceled && action.Kind == InputActionKind::Value ? glm::vec2(0.0f) : value,
                                        vector, action.Kind });
            };

            // Basıldı / bırakıldı (her eylem türü için, Unity WasPressedThisFrame / WasReleasedThisFrame)
            const bool wasPressed = st.Pressed;
            if (!st.Pressed && mag >= kDefaultPressPoint)
                st.Pressed = st.PressedThisFrame = true;
            else if (st.Pressed && mag <= kDefaultPressPoint * kReleaseFactor)
            {
                st.Pressed = false;
                st.ReleasedThisFrame = true;
            }

            // İlk değerlendirme: Initial State Check yoksa o anki durum olay üretmez (Value eylemleri hep denetler)
            if (!st.Initialized)
            {
                st.Initialized = true;
                if (!action.InitialStateCheck && action.Kind != InputActionKind::Value)
                {
                    st.Value = value;
                    st.Magnitude = mag;
                    st.Vector = vector;
                    st.PressedThisFrame = st.ReleasedThisFrame = false;
                    if (mag > 0.0f)
                        st.Phase = P::Waiting;
                    continue;
                }
            }

            // ---- 3) aşamalar
            const std::string interactions = st.ActiveInteraction >= 0 ? st.InteractionSource
                                             : (!bestInteractions.empty() ? bestInteractions : action.Interactions);
            const bool changed = value != st.Value;
            if (interactions.empty())
            {
                st.ActiveInteraction = -1;
                st.InteractionSource.clear();
                switch (action.Kind)
                {
                case InputActionKind::Value:
                    if (mag > 0.0f)
                    {
                        if (st.Phase == P::Waiting)
                        {
                            emit(P::Started);
                            emit(P::Performed);
                        }
                        else if (changed)
                            emit(P::Performed);
                    }
                    else if (st.Phase == P::Started || st.Phase == P::Performed)
                        emit(P::Canceled);
                    break;
                case InputActionKind::Button:
                    if (st.PressedThisFrame && !wasPressed)
                    {
                        emit(P::Started);
                        emit(P::Performed);
                    }
                    else if (st.ReleasedThisFrame && (st.Phase == P::Started || st.Phase == P::Performed))
                        emit(P::Canceled);
                    break;
                case InputActionKind::PassThrough:
                    if (changed)
                        emit(P::Performed);
                    if (mag <= 0.0f && st.Phase == P::Performed && !changed)
                        st.Phase = P::Waiting;
                    break;
                }
            }
            else
            {
                const auto specs = InputActionAssets::ParseSpecs(interactions);
                if (st.InteractionSource != interactions || st.Interactions.size() != specs.size())
                {
                    st.InteractionSource = interactions;
                    st.Interactions.assign(specs.size(), InputInteractionState{});
                    st.ActiveInteraction = -1;
                }
                std::vector<std::vector<P>> transitions(specs.size());
                for (size_t k = 0; k < specs.size(); ++k)
                    Step(specs[k], st.Interactions[k], mag, time, transitions[k]);
                if (st.ActiveInteraction < 0)
                    for (size_t k = 0; k < specs.size(); ++k)
                        if (!transitions[k].empty() && (transitions[k].front() == P::Started || transitions[k].front() == P::Performed))
                        {
                            st.ActiveInteraction = (int)k;
                            break;
                        }
                if (st.ActiveInteraction >= 0)
                {
                    const int active = st.ActiveInteraction;
                    for (P ph : transitions[(size_t)active])
                    {
                        if (ph == P::Started && (st.Phase == P::Started || st.Phase == P::Performed))
                            continue;
                        emit(ph);
                    }
                    const InputInteractionState& m = st.Interactions[(size_t)active];
                    if (m.Phase == P::Waiting || m.Phase == P::Canceled)
                    {
                        const bool canceled = !transitions[(size_t)active].empty() && transitions[(size_t)active].back() == P::Canceled;
                        st.ActiveInteraction = -1;
                        if (canceled)
                        {
                            // İptal: başlamış bir sonraki etkileşim eylemi sürmeye devam eder
                            for (size_t k = (size_t)active + 1; k < specs.size(); ++k)
                                if (st.Interactions[k].Phase == P::Started || st.Interactions[k].Phase == P::Performed)
                                {
                                    st.ActiveInteraction = (int)k;
                                    st.Phase = st.Interactions[k].Phase;
                                    st.CanceledThisFrame = false;
                                    if (events && !events->empty() && events->back().Action == action.Name && events->back().Phase == P::Canceled)
                                        events->pop_back();
                                    break;
                                }
                        }
                        else if (st.Phase == P::Performed)
                            st.Phase = P::Waiting;   // tek seferlik (Tap, Press) tamamlandı
                    }
                }
            }
            st.Value = value;
            st.Magnitude = mag;
            st.Vector = vector;
        }
    }

    // ---------------------------------------------------------------- Project-wide Actions
    void SetProjectWideAsset(const std::string& path)
    {
        if (s_ProjectWideAsset == path)
            return;
        s_ProjectWideAsset = path;
        s_ProjectWideMaps.clear();
        s_ProjectWideOrder.clear();
    }

    const std::string& GetProjectWideAsset()
    {
        return s_ProjectWideAsset;
    }

    void UpdateProjectWide()
    {
        if (s_ProjectWideAsset.empty())
            return;
        const InputActionAssetData* asset = InputActionAssets::Get(s_ProjectWideAsset);
        if (!asset)
            return;
        const uint64_t stamp = InputActionAssets::Stamp(s_ProjectWideAsset);
        s_ProjectWideOrder.clear();
        const float t = Now();
        for (const auto& map : asset->Maps)
        {
            auto& st = s_ProjectWideMaps[map.Name];
            if (st.Stamp != stamp)
            {
                st.Reset();
                st.Stamp = stamp;
            }
            s_ProjectWideOrder.push_back(map.Name);
            Evaluate(map, std::string(), InputDeviceFilter{ true, -2 }, t, st, nullptr);
        }
    }

    const InputActionState* FindProjectWide(const std::string& action)
    {
        if (s_ProjectWideAsset.empty())
            return nullptr;
        const size_t slash = action.find('/');
        if (slash != std::string::npos)
        {
            auto it = s_ProjectWideMaps.find(action.substr(0, slash));
            if (it == s_ProjectWideMaps.end())
                return nullptr;
            auto a = it->second.Actions.find(action.substr(slash + 1));
            return a != it->second.Actions.end() ? &a->second : nullptr;
        }
        for (const auto& mapName : s_ProjectWideOrder)
        {
            auto it = s_ProjectWideMaps.find(mapName);
            if (it == s_ProjectWideMaps.end())
                continue;
            auto a = it->second.Actions.find(action);
            if (a != it->second.Actions.end())
                return &a->second;
        }
        return nullptr;
    }
}
