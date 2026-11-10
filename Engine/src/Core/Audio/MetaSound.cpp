#include "MetaSound.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Logging/Log.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"

#include <miniaudio.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        constexpr uint32_t kBlock = 256;
        constexpr int kMaxParams = 16;

        enum class NodeType : uint8_t { Output, Oscillator, Envelope, Filter, Mixer, Delay, Input };

        struct DspNode
        {
            NodeType Type = NodeType::Output;
            int In[4] = { -1, -1, -1, -1 };   // sıralı listedeki kaynak düğüm
            float P[8] = {};
            int Param = -1;                    // Input: parametre dizini
        };

        struct Graph
        {
            std::vector<DspNode> Nodes;        // kaynaklardan çıkışa sıralı; son = Output
            std::vector<std::string> ParamNames;
            std::vector<float> ParamDefaults;
            float Volume = 1.0f;
            float Duration = 0.0f;             // s, 0 = durdurulana kadar
        };

        int ChoiceIndex(const std::string& v, std::initializer_list<const char*> options)
        {
            int i = 0;
            for (const char* o : options) { if (v == o) return i; ++i; }
            return 0;
        }

        // Grafiği çıkıştan geriye derinlik öncelikli sırala (bağlı olmayan düğümler atlanır, döngüler kesilir)
        std::shared_ptr<Graph> Compile(const NodeAssets::File& f)
        {
            auto g = std::make_shared<Graph>();
            const NodeAssets::Node* out = nullptr;
            for (const auto& n : f.Nodes)
                if (n.Type == "output") { out = &n; break; }
            if (!out)
                return nullptr;
            std::map<int, int> index;          // dosya kimliği -> sıralı dizin
            std::map<int, int> state;          // 1 ziyarette, 2 bitti
            std::function<int(const NodeAssets::Node&)> visit = [&](const NodeAssets::Node& n) -> int {
                if (state[n.Id] == 2) return index[n.Id];
                if (state[n.Id] == 1) return -1;   // döngü
                state[n.Id] = 1;
                DspNode d;
                if (n.Type == "oscillator")
                {
                    d.Type = NodeType::Oscillator;
                    d.P[0] = (float)ChoiceIndex(n.Str("Waveform", "Sine"), { "Sine", "Saw", "Square", "Triangle", "Noise" });
                    d.P[1] = n.Float("Frequency", 440.0f);
                    d.P[2] = n.Float("Amplitude", 0.5f);
                }
                else if (n.Type == "envelope")
                {
                    d.Type = NodeType::Envelope;
                    d.P[0] = std::max(n.Float("Attack", 0.01f), 0.0f);
                    d.P[1] = std::max(n.Float("Decay", 0.1f), 0.0f);
                    d.P[2] = std::clamp(n.Float("Sustain", 0.7f), 0.0f, 1.0f);
                    d.P[3] = std::max(n.Float("Release", 0.3f), 0.0f);
                    d.P[4] = std::max(n.Float("Hold", 0.5f), 0.0f);
                    d.P[5] = std::max(n.Float("Repeat Every", 0.0f), 0.0f);
                }
                else if (n.Type == "filter")
                {
                    d.Type = NodeType::Filter;
                    d.P[0] = (float)ChoiceIndex(n.Str("Type", "Low Pass"), { "Low Pass", "High Pass", "Band Pass" });
                    d.P[1] = std::max(n.Float("Cutoff", 1000.0f), 10.0f);
                    d.P[2] = std::max(n.Float("Resonance", 0.707f), 0.1f);
                }
                else if (n.Type == "mixer")
                {
                    d.Type = NodeType::Mixer;
                    for (int i = 0; i < 4; ++i) d.P[i] = n.Float("Gain " + std::to_string(i + 1), 1.0f);
                }
                else if (n.Type == "delay")
                {
                    d.Type = NodeType::Delay;
                    d.P[0] = std::clamp(n.Float("Delay Time", 0.25f), 0.001f, 2.0f);
                    d.P[1] = std::clamp(n.Float("Feedback", 0.35f), 0.0f, 0.95f);
                    d.P[2] = std::clamp(n.Float("Mix", 0.4f), 0.0f, 1.0f);
                }
                else if (n.Type == "input")
                {
                    d.Type = NodeType::Input;
                    const std::string name = n.Str("Name", "Value");
                    auto it = std::find(g->ParamNames.begin(), g->ParamNames.end(), name);
                    if (it == g->ParamNames.end() && (int)g->ParamNames.size() < kMaxParams)
                    {
                        g->ParamNames.push_back(name);
                        g->ParamDefaults.push_back(n.Float("Default", 0.0f));
                        it = g->ParamNames.end() - 1;
                    }
                    d.Param = it == g->ParamNames.end() ? -1 : (int)(it - g->ParamNames.begin());
                }
                else if (n.Type == "output")
                {
                    d.Type = NodeType::Output;
                    g->Volume = n.Float("Volume", 1.0f);
                    g->Duration = std::max(n.Float("Duration", 0.0f), 0.0f);
                }
                for (const auto* l : f.In(n.Id))
                    if (l->ToPin >= 0 && l->ToPin < 4)
                        if (const auto* src = f.Find(l->From))
                            d.In[l->ToPin] = visit(*src);
                state[n.Id] = 2;
                index[n.Id] = (int)g->Nodes.size();
                g->Nodes.push_back(d);
                return index[n.Id];
            };
            visit(*out);
            return g;
        }

        struct Voice;
        ma_result VoiceRead(ma_data_source* ds, void* out, ma_uint64 frameCount, ma_uint64* framesRead);
        ma_result VoiceSeek(ma_data_source*, ma_uint64) { return MA_NOT_IMPLEMENTED; }
        ma_result VoiceFormat(ma_data_source* ds, ma_format* fmt, ma_uint32* channels, ma_uint32* rate, ma_channel* map, size_t cap);
        ma_result VoiceCursor(ma_data_source* ds, ma_uint64* cursor);
        ma_result VoiceLength(ma_data_source*, ma_uint64* length) { *length = 0; return MA_NOT_IMPLEMENTED; }
        ma_data_source_vtable s_VoiceVTable = { VoiceRead, VoiceSeek, VoiceFormat, VoiceCursor, VoiceLength, nullptr, 0 };

        // Ses iş parçacığında çalışan örnek (veri kaynağı ilk üye olmalı: miniaudio işaretçiyi buna çevirir)
        struct Voice
        {
            ma_data_source_base Base;
            ma_sound Sound;
            bool SoundReady = false;
            std::shared_ptr<Graph> G;
            uint32_t SampleRate = 48000;
            uint64_t Time = 0;
            std::atomic<bool> Ended{ false };
            std::atomic<float> Peak{ 0.0f };
            std::atomic<float> Params[kMaxParams];
            std::vector<float> Buffers;        // düğüm x kBlock
            struct State { double Phase = 0.0; float Z1 = 0.0f, Z2 = 0.0f; std::vector<float> Delay; uint32_t DelayPos = 0; };
            std::vector<State> States;
            std::minstd_rand Rng{ 12345 };

            void Process(float* out, uint32_t frames)
            {
                const auto& nodes = G->Nodes;
                const float sr = (float)SampleRate;
                for (size_t ni = 0; ni < nodes.size(); ++ni)
                {
                    const DspNode& d = nodes[ni];
                    float* dst = &Buffers[ni * kBlock];
                    auto in = [&](int pin) -> const float* { return d.In[pin] >= 0 ? &Buffers[(size_t)d.In[pin] * kBlock] : nullptr; };
                    State& st = States[ni];
                    switch (d.Type)
                    {
                        case NodeType::Oscillator:
                        {
                            const float* fm = in(0);
                            const float* am = in(1);
                            const int wave = (int)d.P[0];
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                const float freq = d.P[1] * (fm ? 1.0f + fm[i] : 1.0f);
                                const float ph = (float)st.Phase;
                                float v;
                                switch (wave)
                                {
                                    case 1: v = 2.0f * ph - 1.0f; break;
                                    case 2: v = ph < 0.5f ? 1.0f : -1.0f; break;
                                    case 3: v = 1.0f - 4.0f * std::abs(ph - 0.5f); break;
                                    case 4: v = (float)(Rng() % 20001) / 10000.0f - 1.0f; break;
                                    default: v = std::sin(ph * 6.2831853f); break;
                                }
                                dst[i] = v * d.P[2] * (am ? am[i] : 1.0f);
                                st.Phase += std::max(freq, 0.0f) / sr;
                                st.Phase -= std::floor(st.Phase);
                            }
                            break;
                        }
                        case NodeType::Envelope:
                        {
                            const float* src = in(0);
                            const float a = d.P[0], dc = d.P[1], s = d.P[2], r = d.P[3], hold = d.P[4], rep = d.P[5];
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                float t = (float)((double)(Time + i) / sr);
                                if (rep > 0.0f) t = std::fmod(t, rep);
                                float e;
                                if (t < a) e = a > 0.0f ? t / a : 1.0f;
                                else if (t < a + dc) e = 1.0f - (1.0f - s) * (dc > 0.0f ? (t - a) / dc : 1.0f);
                                else if (t < a + dc + hold) e = s;
                                else if (t < a + dc + hold + r) e = s * (1.0f - (r > 0.0f ? (t - a - dc - hold) / r : 1.0f));
                                else e = 0.0f;
                                dst[i] = src ? src[i] * e : e;
                            }
                            break;
                        }
                        case NodeType::Filter:
                        {
                            const float* src = in(0);
                            const float* mod = in(1);
                            // RBJ biquad, katsayılar blok başına (kesim modülasyonu bloğun ilk örneğinden)
                            const float cutoff = std::clamp(d.P[1] * (mod ? 1.0f + mod[0] : 1.0f), 10.0f, sr * 0.45f);
                            const float w0 = 6.2831853f * cutoff / sr, cw = std::cos(w0), alpha = std::sin(w0) / (2.0f * d.P[2]);
                            float b0, b1, b2;
                            const int type = (int)d.P[0];
                            if (type == 1) { b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0; }
                            else if (type == 2) { b0 = alpha; b1 = 0.0f; b2 = -alpha; }
                            else { b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = b0; }
                            const float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
                            b0 /= a0; b1 /= a0; b2 /= a0;
                            const float na1 = a1 / a0, na2 = a2 / a0;
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                const float x = src ? src[i] : 0.0f;
                                const float y = b0 * x + st.Z1;      // dönüştürülmüş doğrudan biçim II
                                st.Z1 = b1 * x - na1 * y + st.Z2;
                                st.Z2 = b2 * x - na2 * y;
                                dst[i] = y;
                            }
                            break;
                        }
                        case NodeType::Mixer:
                        {
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                float sum = 0.0f;
                                for (int p = 0; p < 4; ++p)
                                    if (const float* src = in(p)) sum += src[i] * d.P[p];
                                dst[i] = sum;
                            }
                            break;
                        }
                        case NodeType::Delay:
                        {
                            const float* src = in(0);
                            const uint32_t len = std::max<uint32_t>(1, (uint32_t)(d.P[0] * sr));
                            if (st.Delay.size() < len) st.Delay.assign(len, 0.0f);
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                const float x = src ? src[i] : 0.0f;
                                const float delayed = st.Delay[st.DelayPos];
                                st.Delay[st.DelayPos] = x + delayed * d.P[1];
                                st.DelayPos = (st.DelayPos + 1) % len;
                                dst[i] = x * (1.0f - d.P[2]) + delayed * d.P[2];
                            }
                            break;
                        }
                        case NodeType::Input:
                        {
                            const float v = d.Param >= 0 ? Params[d.Param].load(std::memory_order_relaxed) : 0.0f;
                            for (uint32_t i = 0; i < frames; ++i) dst[i] = v;
                            break;
                        }
                        case NodeType::Output:
                        {
                            const float* src = in(0);
                            float peak = 0.0f;
                            for (uint32_t i = 0; i < frames; ++i)
                            {
                                const float v = src ? std::clamp(src[i] * G->Volume, -1.0f, 1.0f) : 0.0f;
                                out[i] = v;
                                peak = std::max(peak, std::abs(v));
                            }
                            Peak.store(peak, std::memory_order_relaxed);
                            break;
                        }
                    }
                }
                Time += frames;
                if (G->Duration > 0.0f && (double)Time / sr >= G->Duration)
                    Ended.store(true);
            }
        };

        ma_result VoiceRead(ma_data_source* ds, void* outPtr, ma_uint64 frameCount, ma_uint64* framesRead)
        {
            Voice* v = (Voice*)ds;
            float* out = (float*)outPtr;
            if (v->Ended.load())
            {
                if (framesRead) *framesRead = 0;
                return MA_AT_END;
            }
            ma_uint64 done = 0;
            while (done < frameCount)
            {
                const uint32_t n = (uint32_t)std::min<ma_uint64>(kBlock, frameCount - done);
                v->Process(out + done, n);
                done += n;
                if (v->Ended.load())
                    break;
            }
            if (framesRead) *framesRead = done;
            return MA_SUCCESS;
        }

        ma_result VoiceFormat(ma_data_source* ds, ma_format* fmt, ma_uint32* channels, ma_uint32* rate, ma_channel* map, size_t cap)
        {
            Voice* v = (Voice*)ds;
            if (fmt) *fmt = ma_format_f32;
            if (channels) *channels = 1;
            if (rate) *rate = v->SampleRate;
            if (map && cap > 0) map[0] = MA_CHANNEL_MONO;
            return MA_SUCCESS;
        }

        ma_result VoiceCursor(ma_data_source* ds, ma_uint64* cursor)
        {
            *cursor = ((Voice*)ds)->Time;
            return MA_SUCCESS;
        }

        void DestroyVoice(Voice* v)
        {
            if (!v) return;
            if (v->SoundReady)
            {
                ma_sound_stop(&v->Sound);
                ma_sound_uninit(&v->Sound);
            }
            ma_data_source_uninit(&v->Base);
            delete v;
        }

        struct Playing
        {
            Voice* V = nullptr;
            std::string Asset, Text;
        };
        std::map<std::pair<const Scene*, uint32_t>, Playing>& Voices()
        {
            static auto& m = *new std::map<std::pair<const Scene*, uint32_t>, Playing>();
            return m;
        }

        Voice* CreateVoice(const std::shared_ptr<Graph>& g, const MetaSoundSourceComponent& c, const glm::vec3& pos)
        {
            if (!AudioEngine::IsInitialized() || !g)
                return nullptr;
            ma_engine* engine = AudioEngine::Get().GetNativeEngine();
            Voice* v = new Voice();
            v->G = g;
            v->SampleRate = ma_engine_get_sample_rate(engine);
            v->Buffers.assign(g->Nodes.size() * kBlock, 0.0f);
            v->States.resize(g->Nodes.size());
            for (int i = 0; i < kMaxParams; ++i)
                v->Params[i].store(i < (int)g->ParamDefaults.size() ? g->ParamDefaults[(size_t)i] : 0.0f);
            ma_data_source_config cfg = ma_data_source_config_init();
            cfg.vtable = &s_VoiceVTable;
            if (ma_data_source_init(&cfg, &v->Base) != MA_SUCCESS)
            {
                delete v;
                return nullptr;
            }
            ma_uint32 flags = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
            if (!c.Spatialize) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
            if (ma_sound_init_from_data_source(engine, v, flags, nullptr, &v->Sound) != MA_SUCCESS)
            {
                ma_data_source_uninit(&v->Base);
                delete v;
                return nullptr;
            }
            v->SoundReady = true;
            ma_sound_set_volume(&v->Sound, std::max(0.0f, c.Volume));
            if (c.Spatialize)
            {
                ma_sound_set_position(&v->Sound, pos.x, pos.y, pos.z);
                ma_sound_set_min_distance(&v->Sound, std::max(0.01f, c.MinDistance));
                ma_sound_set_max_distance(&v->Sound, std::max(c.MinDistance, c.MaxDistance));
                ma_sound_set_attenuation_model(&v->Sound, ma_attenuation_model_inverse);
            }
            ma_node_base* input = AudioMixer::Get().IsInitialized() ? AudioMixer::Get().GetGroupInput(c.Group) : nullptr;
            ma_node_attach_output_bus(&v->Sound, 0, input ? (ma_node*)input : ma_engine_get_endpoint(engine), 0);
            ma_sound_start(&v->Sound);
            return v;
        }
    }

    namespace MetaSounds
    {
        bool Play(Scene& scene, uint32_t entity)
        {
            auto* c = scene.m_Registry.try_get<MetaSoundSourceComponent>((entt::entity)entity);
            if (!c || c->Asset.empty())
                return false;
            NodeAssets::File f;
            if (!NodeAssets::Load(c->Asset, f) || f.Kind != Kind)
                return false;
            auto g = Compile(f);
            if (!g)
                return false;
            Stop(scene, entity);
            const glm::vec3 pos = glm::vec3(scene.ComputeWorldMatrix((entt::entity)entity)[3]);
            Voice* v = CreateVoice(g, *c, pos);
            if (!v)
                return false;
            Voices()[{ &scene, entity }] = { v, c->Asset, f.ToText() };
            c->Playing = true;
            return true;
        }

        void Stop(Scene& scene, uint32_t entity)
        {
            auto it = Voices().find({ &scene, entity });
            if (it == Voices().end())
                return;
            DestroyVoice(it->second.V);
            Voices().erase(it);
            if (auto* c = scene.m_Registry.try_get<MetaSoundSourceComponent>((entt::entity)entity))
                c->Playing = false;
        }

        bool SetFloat(Scene& scene, uint32_t entity, const std::string& name, float value)
        {
            auto it = Voices().find({ &scene, entity });
            if (it == Voices().end() || !it->second.V)
                return false;
            const auto& names = it->second.V->G->ParamNames;
            auto p = std::find(names.begin(), names.end(), name);
            if (p == names.end())
                return false;
            it->second.V->Params[p - names.begin()].store(value);
            return true;
        }

        void StopAll(Scene& scene)
        {
            for (auto it = Voices().begin(); it != Voices().end();)
            {
                if (it->first.first == &scene)
                {
                    DestroyVoice(it->second.V);
                    it = Voices().erase(it);
                }
                else
                    ++it;
            }
        }

        void Update(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            for (auto e : reg.view<TransformComponent, MetaSoundSourceComponent>())
            {
                auto& c = reg.get<MetaSoundSourceComponent>(e);
                auto it = Voices().find({ &scene, (uint32_t)e });
                if (!scene.IsEntityActive(e))
                {
                    if (it != Voices().end()) Stop(scene, (uint32_t)e);
                    continue;
                }
                if (it == Voices().end())
                {
                    // Oynatma başında bir kez (Play On Awake); sonra betikler MetaSoundPlay ile
                    if (c.PlayOnAwake && !c.AutoStarted)
                    {
                        c.AutoStarted = true;
                        Play(scene, (uint32_t)e);
                    }
                    continue;
                }
                Voice* v = it->second.V;
                c.PeakLevel = v->Peak.load(std::memory_order_relaxed);
                if (v->Ended.load())
                {
                    Stop(scene, (uint32_t)e);
                    continue;
                }
                ma_sound_set_volume(&v->Sound, std::max(0.0f, c.Volume));
                if (c.Spatialize)
                {
                    const glm::vec3 p = glm::vec3(scene.ComputeWorldMatrix(e)[3]);
                    ma_sound_set_position(&v->Sound, p.x, p.y, p.z);
                }
            }
            // Silinen nesnelerin sesleri
            for (auto it = Voices().begin(); it != Voices().end();)
            {
                if (it->first.first == &scene && !reg.valid((entt::entity)it->first.second))
                {
                    DestroyVoice(it->second.V);
                    it = Voices().erase(it);
                }
                else
                    ++it;
            }
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            vm->DefineNative("MetaSoundPlay", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                return Value::MakeBool(s && argc > 0 && args[0].IsNumber() && Play(*s, (uint32_t)(int64_t)args[0].ToNumber()));
            }, 1);
            vm->DefineNative("MetaSoundStop", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                if (s && argc > 0 && args[0].IsNumber()) Stop(*s, (uint32_t)(int64_t)args[0].ToNumber());
                return Value::MakeNull();
            }, 1);
            // MetaSoundSetFloat(varlık, "Parametre", değer): grafiğin Input düğümü
            vm->DefineNative("MetaSoundSetFloat", [](int argc, Value* args) -> Value {
                Scene* s = ScriptContext::GetActiveScene();
                return Value::MakeBool(s && argc > 2 && args[0].IsNumber() && args[1].IsString() &&
                                       SetFloat(*s, (uint32_t)(int64_t)args[0].ToNumber(), args[1].AsString()->Value, (float)args[2].ToNumber()));
            }, 3);
        }
    }

    template<> void Scene::OnComponentAdded<MetaSoundSourceComponent>(Entity, MetaSoundSourceComponent&) {}

    void RegisterMetaSoundComponents()
    {
        using C = MetaSoundSourceComponent;
        ComponentDef<C>("MetaSound Source", "MetaSoundSourceComponent", "Audio", ICON_FA_WAVE_SQUARE).Color({ 1.0f, 0.67f, 0.35f, 1.0f })
            .Describe("Plays a MetaSound graph (.cqmetasound): sound generated live from oscillators, envelopes, filters and delays. "
                      "Scripts: MetaSoundPlay / MetaSoundStop(entity), MetaSoundSetFloat(entity, \"Input\", value).")
            .Field("Asset", "MetaSound", &C::Asset, FieldKind::Asset).Filter(".cqmetasound")
            .Field("PlayOnAwake", "Play On Awake", &C::PlayOnAwake)
            .Field("Volume", "Volume", &C::Volume).Range(0.0f, 4.0f, 0.01f)
            .Field("Spatialize", "Spatialize", &C::Spatialize)
            .Field("MinDistance", "Min Distance", &C::MinDistance).Range(0.01f, 1000.0f, 0.05f)
            .Field("MaxDistance", "Max Distance", &C::MaxDistance).Range(0.01f, 10000.0f, 0.1f)
            .Field("Group", "Output Group", &C::Group)
            .Field("Playing", "Playing", &C::Playing).ReadOnly().Header("Runtime")
            .Field("PeakLevel", "Peak Level", &C::PeakLevel).ReadOnly();
    }
}
