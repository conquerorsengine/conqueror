#include "InteractiveMusic.h"

#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioFilter.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <miniaudio.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <unordered_map>

namespace Conqueror::InteractiveMusic
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr double kLookahead = 0.3;   // sıradaki parça / otomatik ilerleme bu kadar önceden planlanır (s)
        constexpr double kLatency = 0.03;    // "hemen" = motor saatinde bu kadar sonra (ses iş parçacığı kaçırmasın)
        constexpr uint64_t kNever = UINT64_MAX;

        ma_engine* Eng() { return AudioEngine::IsInitialized() ? AudioEngine::Get().GetNativeEngine() : nullptr; }
        double Rate()
        {
            ma_engine* e = Eng();
            return e ? (double)ma_engine_get_sample_rate(e) : 48000.0;
        }
        uint64_t Now()
        {
            ma_engine* e = Eng();
            return e ? ma_engine_get_time_in_pcm_frames(e) : 0;
        }
        uint64_t Frames(double seconds) { return (uint64_t)std::llround(std::max(0.0, seconds) * Rate()); }
        double Seconds(uint64_t frames) { return (double)frames / Rate(); }
        float DbToGain(float db) { return db <= -79.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }
        std::mt19937& Rng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }

        // ---------------------------------------------------------------- çalınan parça ağacı
        class Voice
        {
        public:
            virtual ~Voice() = default;
            // at: motor saati (kare); offset: parçanın içinden başlama (s); fadeIn: sessizlikten açılma (kare)
            virtual void Start(uint64_t at, double offset, uint64_t fadeIn) = 0;
            // at'te sönmeye başlar, at + fade'de susar
            virtual void Stop(uint64_t at, uint64_t fade) = 0;
            virtual void Update(uint64_t now) { (void)now; }
            // Müzikal uzunluk (s); 0: sınırsız / bilinmiyor (döngülü dosya, döngülü liste, etkileşimli)
            virtual double Length() const = 0;
            // Döngü noktası (döngülü dosyada dosya uzunluğu), 0: yok
            virtual double LoopLength() const { return 0.0; }
            // Gerçekten çalınan konum (dosyanın çözme imleci; planlama Position ile, motor saatinden)
            virtual double Cursor(uint64_t now) const { return Position(now); }
            virtual bool Done(uint64_t now) const = 0;
            virtual float Bpm() const { return 0.0f; }
            virtual int BarBeats() const { return 4; }
            virtual void SetGain(float gain) = 0;
            virtual double Position(uint64_t now) const
            {
                const double p = StartOffset + (now > StartFrame ? Seconds(now - StartFrame) : 0.0);
                const double loop = LoopLength();
                return loop > 0.0 ? std::fmod(p, loop) : p;
            }
            uint64_t StartFrame = 0;
            double StartOffset = 0.0;
        };

        std::unique_ptr<Voice> MakeVoice(const MusicClip& clip, ma_sound_group* group, int depth);
        std::unique_ptr<Voice> MakeAssetVoice(const MusicAsset& asset, ma_sound_group* group, int depth);

        class FileVoice : public Voice
        {
        public:
            FileVoice(const MusicClip& clip, ma_sound_group* group) : m_Clip(clip)
            {
                ma_engine* e = Eng();
                if (!e || clip.Stream.empty())
                    return;
                const std::string file = SceneSerializer::ResolveAssetPath(clip.Stream);
                m_Ok = ma_sound_init_from_file(e, file.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, group, nullptr, &m_Sound) == MA_SUCCESS;
                if (!m_Ok)
                {
                    CQ_CORE_WARN("[Music] Cannot play {0}", clip.Stream);
                    return;
                }
                float length = 0.0f;
                ma_sound_get_length_in_seconds(&m_Sound, &length);
                m_FileLength = length;
                ma_sound_set_looping(&m_Sound, clip.Loop ? MA_TRUE : MA_FALSE);
            }
            ~FileVoice() override
            {
                if (m_Ok && AudioEngine::IsInitialized())
                    ma_sound_uninit(&m_Sound);
            }
            void Start(uint64_t at, double offset, uint64_t fadeIn) override
            {
                StartFrame = at;
                StartOffset = offset;
                if (!m_Ok)
                    return;
                double o = offset;
                if (m_Clip.Loop && m_FileLength > 0.0)
                    o = std::fmod(o, m_FileLength);
                if (o > 0.0)
                    ma_sound_seek_to_second(&m_Sound, (float)o);
                if (fadeIn > 0)
                {
                    // Çalmadan önce sessiz; açılma tam başlama anında
                    ma_fader_set_fade(&m_Sound.engineNode.fader, 0.0f, 0.0f, 0);
                    ma_sound_set_fade_start_in_pcm_frames(&m_Sound, 0.0f, 1.0f, fadeIn, at);
                }
                ma_sound_set_start_time_in_pcm_frames(&m_Sound, at);
                ma_sound_start(&m_Sound);
                m_Started = true;
            }
            void Stop(uint64_t at, uint64_t fade) override
            {
                m_StopAt = std::min(m_StopAt, at + fade);
                if (!m_Ok)
                    return;
                if (fade > 0)
                    ma_sound_set_stop_time_with_fade_in_pcm_frames(&m_Sound, at + fade, fade);
                else
                    ma_sound_set_stop_time_in_pcm_frames(&m_Sound, at);
            }
            void Update(uint64_t) override
            {
                // Katman sesi yumuşak değişir (anlık değişim tık sesi yapar)
                if (m_Ok && m_Gain != m_TargetGain)
                {
                    m_Gain += (m_TargetGain - m_Gain) * 0.3f;
                    if (std::fabs(m_TargetGain - m_Gain) < 0.002f)
                        m_Gain = m_TargetGain;
                    ma_sound_set_volume(&m_Sound, m_Gain);
                }
            }
            double Length() const override
            {
                if (m_Clip.Bpm > 0.0f && m_Clip.BeatCount > 0)
                    return m_Clip.BeatCount * 60.0 / m_Clip.Bpm;
                return m_Clip.Loop ? 0.0 : m_FileLength;
            }
            double LoopLength() const override { return m_Clip.Loop ? m_FileLength : 0.0; }
            double Cursor(uint64_t) const override
            {
                float cursor = 0.0f;
                if (m_Ok)
                    ma_sound_get_cursor_in_seconds(&m_Sound, &cursor);
                return cursor;
            }
            bool Done(uint64_t now) const override
            {
                if (!m_Ok || now >= m_StopAt)
                    return true;
                return !m_Clip.Loop && m_Started && now > StartFrame && Position(now) >= m_FileLength + 0.05;
            }
            float Bpm() const override { return m_Clip.Bpm; }
            int BarBeats() const override { return std::max(1, m_Clip.BarBeats); }
            void SetGain(float gain) override
            {
                m_TargetGain = gain;
                if (!m_Started && m_Ok)
                {
                    m_Gain = gain;   // başlamadan önce: hemen
                    ma_sound_set_volume(&m_Sound, gain);
                }
            }

        private:
            MusicClip m_Clip;
            ma_sound m_Sound{};
            bool m_Ok = false, m_Started = false;
            double m_FileLength = 0.0;
            uint64_t m_StopAt = kNever;
            float m_Gain = 1.0f, m_TargetGain = 1.0f;
        };

        // Godot AudioStreamPlaylist
        class PlaylistVoice : public Voice
        {
        public:
            PlaylistVoice(const MusicAsset& a, ma_sound_group* g, int depth) : m_A(a), m_Group(g), m_Depth(depth) {}
            void Start(uint64_t at, double offset, uint64_t fadeIn) override
            {
                StartFrame = at;
                StartOffset = offset;
                MakeOrder();
                m_Index = 0;
                if (m_Order.empty())
                {
                    m_Ended = true;
                    return;
                }
                Begin(at, offset, fadeIn);
            }
            void Stop(uint64_t at, uint64_t fade) override
            {
                m_Stopping = true;
                if (m_Cur) m_Cur->Stop(at, fade);
                for (auto& t : m_Tails) t->Stop(at, fade);
            }
            void Update(uint64_t now) override
            {
                if (m_Cur) m_Cur->Update(now);
                for (auto& t : m_Tails) t->Update(now);
                m_Tails.erase(std::remove_if(m_Tails.begin(), m_Tails.end(), [&](const auto& t) { return t->Done(now); }), m_Tails.end());
                if (m_Stopping || m_Ended || !m_Cur || m_CurEnd == kNever || m_CurEnd > now + Frames(kLookahead))
                    return;
                // Sıradaki parça tam bitiş noktasında başlar; biten parçanın kuyruğu Fade Time'da söner
                if (++m_Index >= (int)m_Order.size())
                {
                    if (!m_A.Loop)
                    {
                        m_Ended = true;
                        return;
                    }
                    MakeOrder();
                    m_Index = 0;
                }
                const uint64_t at = std::max(m_CurEnd, now + Frames(kLatency));
                m_Cur->Stop(at, Frames(m_A.FadeTime));
                m_Tails.push_back(std::move(m_Cur));
                Begin(at, 0.0, 0);
            }
            double Length() const override { return 0.0; }
            bool Done(uint64_t now) const override
            {
                if (!(m_Ended || m_Stopping || m_Order.empty()))
                    return false;
                if (m_Cur && !m_Cur->Done(now))
                    return false;
                return std::all_of(m_Tails.begin(), m_Tails.end(), [&](const auto& t) { return t->Done(now); });
            }
            double Position(uint64_t now) const override { return m_Cur ? m_Cur->Position(now) : 0.0; }
            double Cursor(uint64_t now) const override { return m_Cur ? m_Cur->Cursor(now) : 0.0; }
            float Bpm() const override { return m_Cur ? m_Cur->Bpm() : 0.0f; }
            int BarBeats() const override { return m_Cur ? m_Cur->BarBeats() : 4; }
            void SetGain(float gain) override
            {
                m_Gain = gain;
                if (m_Cur) m_Cur->SetGain(gain);
                for (auto& t : m_Tails) t->SetGain(gain);
            }
            int CurrentTrack() const { return m_Order.empty() ? -1 : m_Order[(size_t)std::min(m_Index, (int)m_Order.size() - 1)]; }

        private:
            void MakeOrder()
            {
                m_Order.resize(m_A.Clips.size());
                std::iota(m_Order.begin(), m_Order.end(), 0);
                if (m_A.Shuffle && m_Order.size() > 1)
                {
                    std::shuffle(m_Order.begin(), m_Order.end(), Rng());
                    if (m_Order.front() == m_Last)   // aynı parça art arda gelmesin
                        std::swap(m_Order.front(), m_Order.back());
                }
                if (!m_Order.empty())
                    m_Last = m_Order.back();
            }
            void Begin(uint64_t at, double offset, uint64_t fadeIn)
            {
                m_Cur = MakeVoice(m_A.Clips[(size_t)m_Order[(size_t)m_Index]], m_Group, m_Depth + 1);
                m_Cur->SetGain(m_Gain);
                m_Cur->Start(at, offset, fadeIn);
                const double len = m_Cur->Length();
                m_CurEnd = len > 0.0 ? at + Frames(std::max(0.0, len - offset)) : kNever;
            }

            MusicAsset m_A;
            ma_sound_group* m_Group;
            int m_Depth;
            std::vector<int> m_Order;
            int m_Index = 0, m_Last = -1;
            std::unique_ptr<Voice> m_Cur;
            std::vector<std::unique_ptr<Voice>> m_Tails;
            uint64_t m_CurEnd = kNever;
            bool m_Stopping = false, m_Ended = false;
            float m_Gain = 1.0f;
        };

        // Godot AudioStreamSynchronized
        class SyncVoice : public Voice
        {
        public:
            SyncVoice(const MusicAsset& a, ma_sound_group* g, int depth)
            {
                for (const auto& clip : a.Clips)
                {
                    MusicClip c = clip;
                    c.VolumeDb = 0.0f;   // katman sesi burada (betikten değişir)
                    m_Layers.push_back(MakeVoice(c, g, depth + 1));
                    m_LayerGain.push_back(DbToGain(clip.VolumeDb));
                }
                ApplyGains();
            }
            void Start(uint64_t at, double offset, uint64_t fadeIn) override
            {
                StartFrame = at;
                StartOffset = offset;
                for (auto& l : m_Layers) l->Start(at, offset, fadeIn);
            }
            void Stop(uint64_t at, uint64_t fade) override { for (auto& l : m_Layers) l->Stop(at, fade); }
            void Update(uint64_t now) override { for (auto& l : m_Layers) l->Update(now); }
            double Length() const override
            {
                double len = 0.0;
                for (const auto& l : m_Layers) len = std::max(len, l->Length());
                return len;
            }
            double LoopLength() const override
            {
                double len = 0.0;
                for (const auto& l : m_Layers) len = std::max(len, l->LoopLength());
                return len;
            }
            bool Done(uint64_t now) const override
            {
                return std::all_of(m_Layers.begin(), m_Layers.end(), [&](const auto& l) { return l->Done(now); });
            }
            double Position(uint64_t now) const override { return m_Layers.empty() ? 0.0 : m_Layers.front()->Position(now); }
            double Cursor(uint64_t now) const override { return m_Layers.empty() ? 0.0 : m_Layers.front()->Cursor(now); }
            float Bpm() const override
            {
                for (const auto& l : m_Layers)
                    if (l->Bpm() > 0.0f) return l->Bpm();
                return 0.0f;
            }
            int BarBeats() const override
            {
                for (const auto& l : m_Layers)
                    if (l->Bpm() > 0.0f) return l->BarBeats();
                return 4;
            }
            void SetGain(float gain) override
            {
                m_Gain = gain;
                ApplyGains();
            }
            bool SetLayerVolume(int layer, float db)
            {
                if (layer < 0 || layer >= (int)m_Layers.size())
                    return false;
                m_LayerGain[(size_t)layer] = DbToGain(db);
                ApplyGains();
                return true;
            }

        private:
            void ApplyGains()
            {
                for (size_t i = 0; i < m_Layers.size(); ++i)
                    m_Layers[i]->SetGain(m_Gain * m_LayerGain[i]);
            }
            std::vector<std::unique_ptr<Voice>> m_Layers;
            std::vector<float> m_LayerGain;
            float m_Gain = 1.0f;
        };

        // Godot AudioStreamInteractive
        class InteractiveVoice : public Voice
        {
        public:
            InteractiveVoice(const MusicAsset& a, ma_sound_group* g, int depth) : m_A(a), m_Group(g), m_Depth(depth) {}
            void Start(uint64_t at, double offset, uint64_t fadeIn) override
            {
                StartFrame = at;
                StartOffset = offset;
                if (m_A.InteractiveClips.empty())
                    return;
                StartClip(std::clamp(m_A.InitialClip, 0, (int)m_A.InteractiveClips.size() - 1), at, offset, fadeIn);
            }
            void Stop(uint64_t at, uint64_t fade) override
            {
                m_Stopping = true;
                m_Requested = -1;
                if (m_Cur) m_Cur->Stop(at, fade);
                for (auto& t : m_Tails) t->Stop(at, fade);
            }
            void Update(uint64_t now) override
            {
                if (m_Cur) m_Cur->Update(now);
                for (auto& t : m_Tails) t->Update(now);
                m_Tails.erase(std::remove_if(m_Tails.begin(), m_Tails.end(), [&](const auto& t) { return t->Done(now); }), m_Tails.end());
                if (m_Stopping || !m_Cur)
                    return;
                // Planlanmış geçiş henüz olmadıysa yeni istek bekler (sıradaki geçiş yeni klibin vuruşlarına göre hesaplanır)
                if (now < m_PendingUntil)
                    return;
                if (m_Requested >= 0)
                {
                    const int to = m_Requested;
                    m_Requested = -1;
                    if (to != m_CurClip || m_AfterFiller >= 0)
                        Transition(to, now);
                    return;
                }
                if (m_CurEnd == kNever || m_CurEnd > now + Frames(kLookahead))
                    return;
                // Klip sonu: dolgudan hedefe, otomatik ilerleme ya da tutulan klibe dönüş
                const MusicInteractiveClip& c = m_A.InteractiveClips[(size_t)m_CurClip];
                int dest = -1;
                double offset = 0.0;
                if (m_AfterFiller >= 0)
                {
                    dest = m_AfterFiller;
                    m_AfterFiller = -1;
                }
                else if (c.AutoAdvance == 1 && Valid(c.NextClip))
                    dest = c.NextClip;
                else if (c.AutoAdvance == 2 && m_Hold >= 0)
                {
                    dest = m_Hold;
                    offset = m_HoldOffset;
                    m_Hold = -1;
                }
                if (dest < 0)
                {
                    m_CurEnd = kNever;   // ilerleme yok: klip kendi sonunda biter (döngülüyse sürer)
                    return;
                }
                const uint64_t at = std::max(m_CurEnd, now + Frames(kLatency));
                if (m_Cur->LoopLength() > 0.0)
                    m_Cur->Stop(at, 0);   // döngülü klip: döngü noktasında kesilir (kuyruk yok)
                m_Tails.push_back(std::move(m_Cur));
                StartClip(dest, at, offset, 0);
                m_PendingUntil = at;
            }
            double Length() const override { return 0.0; }
            bool Done(uint64_t now) const override
            {
                if (m_Cur && !m_Cur->Done(now))
                    return false;
                if (!m_Stopping && m_Requested >= 0)
                    return false;
                return std::all_of(m_Tails.begin(), m_Tails.end(), [&](const auto& t) { return t->Done(now); });
            }
            double Position(uint64_t now) const override { return m_Cur ? m_Cur->Position(now) : 0.0; }
            double Cursor(uint64_t now) const override { return m_Cur ? m_Cur->Cursor(now) : 0.0; }
            float Bpm() const override { return m_Cur ? m_Cur->Bpm() : 0.0f; }
            int BarBeats() const override { return m_Cur ? m_Cur->BarBeats() : 4; }
            void SetGain(float gain) override
            {
                m_Gain = gain;
                if (m_Cur) m_Cur->SetGain(gain);
                for (auto& t : m_Tails) t->SetGain(gain);
            }
            bool Request(int clip)
            {
                if (!Valid(clip) || m_Stopping)
                    return false;
                m_Requested = clip;
                return true;
            }
            int FindClip(const std::string& name) const
            {
                for (int i = 0; i < (int)m_A.InteractiveClips.size(); ++i)
                    if (m_A.InteractiveClips[(size_t)i].Name == name)
                        return i;
                if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isdigit(ch); }))
                {
                    const int i = std::atoi(name.c_str());
                    return Valid(i) ? i : -1;
                }
                return -1;
            }
            int CurrentClip() const { return m_CurClip; }
            const MusicAsset& Asset() const { return m_A; }
            Voice* Current() { return m_Cur.get(); }

        private:
            bool Valid(int i) const { return i >= 0 && i < (int)m_A.InteractiveClips.size(); }

            void StartClip(int index, uint64_t at, double offset, uint64_t fadeIn)
            {
                const MusicInteractiveClip& c = m_A.InteractiveClips[(size_t)index];
                m_Cur = MakeVoice(c.Clip, m_Group, m_Depth + 1);
                m_Cur->SetGain(m_Gain);
                const double loop = m_Cur->LoopLength();
                if (loop > 0.0)
                    offset = std::fmod(offset, loop);
                else if (const double len = m_Cur->Length(); len > 0.0 && offset >= len)
                    offset = 0.0;
                m_Cur->Start(at, offset, fadeIn);
                m_CurClip = index;
                const double len = m_Cur->Length();
                if (len > 0.0)
                    m_CurEnd = at + Frames(std::max(0.0, len - offset));
                else if (loop > 0.0 && (c.AutoAdvance != 0 || m_AfterFiller >= 0))
                    m_CurEnd = at + Frames(loop - offset);   // döngülü klipte ilerleme döngü noktasında
                else
                    m_CurEnd = kNever;
            }

            const MusicTransition* FindTransition(int from, int to) const
            {
                const MusicTransition* fromAny = nullptr;
                const MusicTransition* anyTo = nullptr;
                const MusicTransition* anyAny = nullptr;
                for (const auto& t : m_A.Transitions)
                {
                    if (t.From == from && t.To == to) return &t;
                    if (t.From == from && t.To < 0 && !fromAny) fromAny = &t;
                    if (t.From < 0 && t.To == to && !anyTo) anyTo = &t;
                    if (t.From < 0 && t.To < 0 && !anyAny) anyAny = &t;
                }
                return fromAny ? fromAny : anyTo ? anyTo : anyAny;
            }

            void Transition(int to, uint64_t now)
            {
                static const MusicTransition kImmediate{ -1, -1, 0, 1, 0, 0.0f, false, -1, false };   // tanımsız geçiş: hemen, baştan, kesme
                const MusicTransition& tr = [&]() -> const MusicTransition& {
                    const MusicTransition* t = FindTransition(m_CurClip, to);
                    return t ? *t : kImmediate;
                }();
                m_AfterFiller = -1;
                const double bpm = m_Cur->Bpm() > 0.0f ? m_Cur->Bpm() : 120.0;
                const double beat = 60.0 / bpm;
                const double bar = beat * std::max(1, m_Cur->BarBeats());
                const double pos = m_Cur->Position(now);
                const uint64_t soonest = now + Frames(kLatency);
                uint64_t at = soonest;
                auto nextBoundary = [&](double unit) {
                    double next = std::ceil((pos + 1e-4) / unit) * unit;
                    uint64_t t = now + Frames(next - pos);
                    if (t < soonest)
                        t = now + Frames(next + unit - pos);   // sınır çok yakın: bir sonraki
                    return t;
                };
                switch (tr.FromTime)
                {
                    case 1: at = nextBoundary(beat); break;
                    case 2: at = nextBoundary(bar); break;
                    case 3:
                        if (m_CurEnd != kNever) at = std::max(m_CurEnd, soonest);
                        else if (const double loop = m_Cur->LoopLength(); loop > 0.0) at = std::max(soonest, now + Frames(loop - std::fmod(pos, loop)));
                        break;
                    default: break;
                }
                const uint64_t fade = Frames(std::max(0.0f, tr.FadeBeats) * beat);
                const int mode = tr.FadeMode == 4 ? (tr.ToTime == 0 ? 3 : 2) : tr.FadeMode;
                double offset = tr.ToTime == 0 ? pos + Seconds(at - now) : 0.0;
                if (tr.HoldPrevious)
                {
                    m_Hold = m_CurClip;
                    m_HoldOffset = pos + Seconds(at - now);
                }
                int dest = to;
                if (tr.UseFiller && Valid(tr.FillerClip))
                {
                    m_AfterFiller = to;
                    dest = tr.FillerClip;
                    offset = 0.0;
                }
                m_Cur->Stop(at, mode == 2 || mode == 3 ? fade : 0);
                m_Tails.push_back(std::move(m_Cur));
                StartClip(dest, at, offset, mode == 1 || mode == 3 ? fade : 0);
                m_PendingUntil = at;
            }

            MusicAsset m_A;
            ma_sound_group* m_Group;
            int m_Depth;
            std::unique_ptr<Voice> m_Cur;
            std::vector<std::unique_ptr<Voice>> m_Tails;
            int m_CurClip = -1, m_Requested = -1, m_Hold = -1, m_AfterFiller = -1;
            double m_HoldOffset = 0.0;
            uint64_t m_CurEnd = kNever, m_PendingUntil = 0;
            bool m_Stopping = false;
            float m_Gain = 1.0f;
        };

        // Hiçbir şey çalmayan yer tutucu (okunamayan varlık)
        class SilentVoice : public Voice
        {
        public:
            void Start(uint64_t at, double offset, uint64_t) override { StartFrame = at; StartOffset = offset; }
            void Stop(uint64_t, uint64_t) override {}
            double Length() const override { return 0.0; }
            bool Done(uint64_t) const override { return true; }
            void SetGain(float) override {}
        };

        std::unique_ptr<Voice> MakeAssetVoice(const MusicAsset& asset, ma_sound_group* group, int depth)
        {
            switch (asset.Kind)
            {
                case MusicAsset::Type::Synchronized: return std::make_unique<SyncVoice>(asset, group, depth);
                case MusicAsset::Type::Interactive: return std::make_unique<InteractiveVoice>(asset, group, depth);
                default: return std::make_unique<PlaylistVoice>(asset, group, depth);
            }
        }

        std::unique_ptr<Voice> MakeVoice(const MusicClip& clip, ma_sound_group* group, int depth)
        {
            if (IsAsset(clip.Stream))
            {
                MusicAsset nested;
                // İç içe en çok 4 kat (kendine başvuran varlık sonsuz döngü yapmasın)
                if (depth < 4 && Load(clip.Stream, nested))
                    return MakeAssetVoice(nested, group, depth);
                return std::make_unique<SilentVoice>();
            }
            return std::make_unique<FileVoice>(clip, group);
        }

        // Bir varlığın içinden etkileşimli / senkron parçayı bul (kökün kendisi ya da çalan klibi)
        InteractiveVoice* FindInteractive(Voice* v)
        {
            for (int depth = 0; v && depth < 6; ++depth)
            {
                if (auto* iv = dynamic_cast<InteractiveVoice*>(v)) return iv;
                return nullptr;
            }
            return nullptr;
        }
        SyncVoice* FindSync(Voice* v)
        {
            for (int depth = 0; v && depth < 6; ++depth)
            {
                if (auto* sv = dynamic_cast<SyncVoice*>(v)) return sv;
                if (auto* iv = dynamic_cast<InteractiveVoice*>(v)) { v = iv->Current(); continue; }
                return nullptr;
            }
            return nullptr;
        }

        // ---------------------------------------------------------------- Audio Source başına çalar
        class OutputNode : public AudioNode
        {
        public:
            ma_sound_group* Group = nullptr;
            ma_node_base* GetNativeNode() const override { return Group ? reinterpret_cast<ma_node_base*>(Group) : nullptr; }
        };

        struct Player
        {
            ma_sound_group Group{};
            bool GroupOk = false;
            OutputNode Node;
            std::unique_ptr<Voice> Root;
            std::vector<std::unique_ptr<Voice>> Fading;
            std::string Path;
            bool Playing = false;
            uint64_t RootEnd = kNever;
            uint32_t RoutingGeneration = ~0u;
            std::string Output;

            ~Player()
            {
                Root.reset();
                Fading.clear();
                if (GroupOk && AudioEngine::IsInitialized())
                    ma_sound_group_uninit(&Group);
            }
            bool Init()
            {
                ma_engine* e = Eng();
                if (!e)
                    return false;
                GroupOk = ma_sound_group_init(e, MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT, nullptr, &Group) == MA_SUCCESS;
                Node.Group = GroupOk ? &Group : nullptr;
                if (GroupOk)
                    ma_sound_group_set_spatialization_enabled(&Group, MA_FALSE);
                return GroupOk;
            }
            std::unique_ptr<Voice> MakeRoot() const
            {
                MusicAsset a;
                if (!Load(Path, a))
                    return nullptr;
                return MakeAssetVoice(a, const_cast<ma_sound_group*>(&Group), 0);
            }
            bool Play()
            {
                if (!GroupOk)
                    return false;
                if (Root)
                {
                    Root->Stop(Now() + Frames(kLatency), Frames(0.02));
                    Fading.push_back(std::move(Root));
                }
                Root = MakeRoot();
                if (!Root)
                    return false;
                const uint64_t at = Now() + Frames(kLatency);
                Root->Start(at, 0.0, 0);
                const double len = Root->Length();
                RootEnd = len > 0.0 ? at + Frames(len) : kNever;
                Playing = true;
                return true;
            }
            void Stop()
            {
                if (Root)
                {
                    Root->Stop(Now() + Frames(kLatency), Frames(0.02));
                    Fading.push_back(std::move(Root));
                }
                Playing = false;
            }
            void Update(bool loop)
            {
                const uint64_t now = Now();
                if (Root) Root->Update(now);
                for (auto& f : Fading) f->Update(now);
                Fading.erase(std::remove_if(Fading.begin(), Fading.end(), [&](const auto& f) { return f->Done(now); }), Fading.end());
                if (!Playing || !Root)
                    return;
                // Audio Source Loop: uzunluğu belli parça bitiş noktasında kesintisiz yeniden başlar
                if (loop && RootEnd != kNever && RootEnd <= now + Frames(kLookahead))
                {
                    if (auto next = MakeRoot())
                    {
                        const uint64_t at = std::max(RootEnd, now + Frames(kLatency));
                        next->Start(at, 0.0, 0);
                        Fading.push_back(std::move(Root));
                        Root = std::move(next);
                        const double len = Root->Length();
                        RootEnd = len > 0.0 ? at + Frames(len) : kNever;
                    }
                    return;
                }
                if (Root->Done(now))
                {
                    if (loop)
                        Play();
                    else
                        Playing = false;
                }
            }
        };

        using Key = std::pair<const Scene*, uint32_t>;
        std::map<Key, std::unique_ptr<Player>>& Players()
        {
            static std::map<Key, std::unique_ptr<Player>> players;
            return players;
        }
        Player* Find(Scene& scene, uint32_t entity)
        {
            auto it = Players().find({ &scene, entity });
            return it == Players().end() ? nullptr : it->second.get();
        }
        std::unique_ptr<Player>& Preview()
        {
            static std::unique_ptr<Player> preview;
            return preview;
        }

        // ---------------------------------------------------------------- dosya biçimi
        void EmitClip(YAML::Emitter& e, const MusicClip& c)
        {
            e << YAML::Key << "Stream" << YAML::Value << c.Stream;
            e << YAML::Key << "Bpm" << YAML::Value << c.Bpm;
            e << YAML::Key << "BeatCount" << YAML::Value << c.BeatCount;
            e << YAML::Key << "BarBeats" << YAML::Value << c.BarBeats;
            e << YAML::Key << "Loop" << YAML::Value << c.Loop;
            e << YAML::Key << "Volume" << YAML::Value << c.VolumeDb;
        }
        MusicClip ReadClip(const YAML::Node& n)
        {
            MusicClip c;
            if (n["Stream"]) c.Stream = n["Stream"].as<std::string>();
            if (n["Bpm"]) c.Bpm = std::max(0.0f, n["Bpm"].as<float>());
            if (n["BeatCount"]) c.BeatCount = std::max(0, n["BeatCount"].as<int>());
            if (n["BarBeats"]) c.BarBeats = std::max(1, n["BarBeats"].as<int>());
            if (n["Loop"]) c.Loop = n["Loop"].as<bool>();
            if (n["Volume"]) c.VolumeDb = std::clamp(n["Volume"].as<float>(), -80.0f, 24.0f);
            return c;
        }
        MusicAsset::Type KindFromExtension(const std::string& path)
        {
            const std::string e = Lower(fs::path(path).extension().string());
            if (e == SynchronizedExtension) return MusicAsset::Type::Synchronized;
            if (e == InteractiveExtension) return MusicAsset::Type::Interactive;
            return MusicAsset::Type::Playlist;
        }
    }

    const char* TypeName(MusicAsset::Type type)
    {
        switch (type)
        {
            case MusicAsset::Type::Synchronized: return "Audio Stream Synchronized";
            case MusicAsset::Type::Interactive: return "Audio Stream Interactive";
            default: return "Audio Stream Playlist";
        }
    }

    bool IsAsset(const std::string& path)
    {
        const std::string e = Lower(fs::path(path).extension().string());
        return e == PlaylistExtension || e == SynchronizedExtension || e == InteractiveExtension;
    }

    bool Load(const std::string& path, MusicAsset& out)
    {
        struct Cached { fs::file_time_type Time; MusicAsset Asset; };
        static std::unordered_map<std::string, Cached> cache;
        const std::string file = SceneSerializer::ResolveAssetPath(path);
        std::error_code ec;
        const auto time = fs::last_write_time(file, ec);
        if (ec)
            return false;
        auto it = cache.find(file);
        if (it == cache.end() || it->second.Time != time)
        {
            try
            {
                YAML::Node n = YAML::LoadFile(file)["Music"];
                if (!n)
                    return false;
                MusicAsset a;
                a.Kind = KindFromExtension(path);
                if (auto c = n["Clips"]; c && c.IsSequence())
                    for (const auto& v : c) a.Clips.push_back(ReadClip(v));
                if (n["Shuffle"]) a.Shuffle = n["Shuffle"].as<bool>();
                if (n["Loop"]) a.Loop = n["Loop"].as<bool>();
                if (n["FadeTime"]) a.FadeTime = std::max(0.0f, n["FadeTime"].as<float>());
                if (auto c = n["InteractiveClips"]; c && c.IsSequence())
                    for (const auto& v : c)
                    {
                        MusicInteractiveClip ic;
                        if (v["Name"]) ic.Name = v["Name"].as<std::string>();
                        ic.Clip = ReadClip(v);
                        if (v["AutoAdvance"]) ic.AutoAdvance = std::clamp(v["AutoAdvance"].as<int>(), 0, 2);
                        if (v["NextClip"]) ic.NextClip = v["NextClip"].as<int>();
                        a.InteractiveClips.push_back(ic);
                    }
                if (n["InitialClip"]) a.InitialClip = std::max(0, n["InitialClip"].as<int>());
                if (auto t = n["Transitions"]; t && t.IsSequence())
                    for (const auto& v : t)
                    {
                        MusicTransition tr;
                        if (v["From"]) tr.From = v["From"].as<int>();
                        if (v["To"]) tr.To = v["To"].as<int>();
                        if (v["FromTime"]) tr.FromTime = std::clamp(v["FromTime"].as<int>(), 0, 3);
                        if (v["ToTime"]) tr.ToTime = std::clamp(v["ToTime"].as<int>(), 0, 1);
                        if (v["FadeMode"]) tr.FadeMode = std::clamp(v["FadeMode"].as<int>(), 0, 4);
                        if (v["FadeBeats"]) tr.FadeBeats = std::max(0.0f, v["FadeBeats"].as<float>());
                        if (v["UseFiller"]) tr.UseFiller = v["UseFiller"].as<bool>();
                        if (v["FillerClip"]) tr.FillerClip = v["FillerClip"].as<int>();
                        if (v["HoldPrevious"]) tr.HoldPrevious = v["HoldPrevious"].as<bool>();
                        a.Transitions.push_back(tr);
                    }
                it = cache.insert_or_assign(file, Cached{ time, a }).first;
            }
            catch (...) { return false; }
        }
        out = it->second.Asset;
        out.Kind = KindFromExtension(path);
        return true;
    }

    bool Save(const std::string& path, const MusicAsset& a)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "Music" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "Type" << YAML::Value << TypeName(a.Kind);
        e << YAML::Key << "Clips" << YAML::Value << YAML::BeginSeq;
        for (const auto& c : a.Clips)
        {
            e << YAML::BeginMap;
            EmitClip(e, c);
            e << YAML::EndMap;
        }
        e << YAML::EndSeq;
        if (a.Kind == MusicAsset::Type::Playlist)
        {
            e << YAML::Key << "Shuffle" << YAML::Value << a.Shuffle;
            e << YAML::Key << "Loop" << YAML::Value << a.Loop;
            e << YAML::Key << "FadeTime" << YAML::Value << a.FadeTime;
        }
        if (a.Kind == MusicAsset::Type::Interactive)
        {
            e << YAML::Key << "InitialClip" << YAML::Value << a.InitialClip;
            e << YAML::Key << "InteractiveClips" << YAML::Value << YAML::BeginSeq;
            for (const auto& c : a.InteractiveClips)
            {
                e << YAML::BeginMap;
                e << YAML::Key << "Name" << YAML::Value << c.Name;
                EmitClip(e, c.Clip);
                e << YAML::Key << "AutoAdvance" << YAML::Value << c.AutoAdvance;
                e << YAML::Key << "NextClip" << YAML::Value << c.NextClip;
                e << YAML::EndMap;
            }
            e << YAML::EndSeq;
            e << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
            for (const auto& t : a.Transitions)
            {
                e << YAML::BeginMap;
                e << YAML::Key << "From" << YAML::Value << t.From;
                e << YAML::Key << "To" << YAML::Value << t.To;
                e << YAML::Key << "FromTime" << YAML::Value << t.FromTime;
                e << YAML::Key << "ToTime" << YAML::Value << t.ToTime;
                e << YAML::Key << "FadeMode" << YAML::Value << t.FadeMode;
                e << YAML::Key << "FadeBeats" << YAML::Value << t.FadeBeats;
                e << YAML::Key << "UseFiller" << YAML::Value << t.UseFiller;
                e << YAML::Key << "FillerClip" << YAML::Value << t.FillerClip;
                e << YAML::Key << "HoldPrevious" << YAML::Value << t.HoldPrevious;
                e << YAML::EndMap;
            }
            e << YAML::EndSeq;
        }
        e << YAML::EndMap << YAML::EndMap;
        std::ofstream f(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return (bool)f;
    }

    void UpdateSource(Scene& scene, uint32_t entity, AudioSourceComponent& src, const glm::vec3& position, float gain, bool runtime,
                      uint32_t routingGeneration)
    {
        const Key key{ &scene, entity };
        auto it = Players().find(key);
        if (it != Players().end() && (it->second->Path != src.FilePath || !runtime))
        {
            Players().erase(it);
            it = Players().end();
        }
        // Düzenleme modunda çalmaz (önizleme Inspector'dan); oyunda ilk karede Play On Awake
        if (!runtime)
        {
            src.IsPlaying = false;
            return;
        }
        if (it == Players().end())
        {
            auto p = std::make_unique<Player>();
            if (!p->Init())
                return;
            p->Path = src.FilePath;
            if (src.PlayOnAwake)
                p->Play();
            it = Players().emplace(key, std::move(p)).first;
        }
        Player& p = *it->second;
        if (p.RoutingGeneration != routingGeneration || p.Output != src.OutputGroup)
        {
            p.Node.DetachAllOutputs();
            AudioMixer::Get().Connect(&p.Node, src.OutputGroup);
            p.RoutingGeneration = routingGeneration;
            p.Output = src.OutputGroup;
        }
        ma_sound_group_set_volume(&p.Group, src.Mute ? 0.0f : std::max(0.0f, gain));
        ma_sound_group_set_spatialization_enabled(&p.Group, src.Is3D ? MA_TRUE : MA_FALSE);
        if (src.Is3D)
        {
            ma_sound_group_set_position(&p.Group, position.x, position.y, position.z);
            ma_sound_group_set_min_distance(&p.Group, src.MinDistance);
            ma_sound_group_set_max_distance(&p.Group, src.MaxDistance);
            ma_sound_group_set_attenuation_model(&p.Group, (ma_attenuation_model)std::clamp((int)src.Attenuation, 0, 3));
            ma_sound_group_set_rolloff(&p.Group, src.RolloffFactor);
            ma_sound_group_set_doppler_factor(&p.Group, src.DopplerLevel);
        }
        p.Update(src.Loop);
        src.IsPlaying = p.Playing;
    }

    bool Play(Scene& scene, uint32_t entity)
    {
        Entity e{ (entt::entity)entity, &scene };
        if (!e.IsValid() || !e.HasComponent<AudioSourceComponent>())
            return false;
        auto& src = e.GetComponent<AudioSourceComponent>();
        if (!IsAsset(src.FilePath))
            return false;
        Player* p = Find(scene, entity);
        if (!p || p->Path != src.FilePath)
        {
            auto np = std::make_unique<Player>();
            if (!np->Init())
                return false;
            np->Path = src.FilePath;
            p = np.get();
            Players()[{ &scene, entity }] = std::move(np);
        }
        src.IsPlaying = p->Play();
        return src.IsPlaying;
    }

    void Stop(Scene& scene, uint32_t entity)
    {
        if (Player* p = Find(scene, entity))
            p->Stop();
        Entity e{ (entt::entity)entity, &scene };
        if (e.IsValid() && e.HasComponent<AudioSourceComponent>())
            e.GetComponent<AudioSourceComponent>().IsPlaying = false;
    }

    bool IsPlaying(Scene& scene, uint32_t entity)
    {
        Player* p = Find(scene, entity);
        return p && p->Playing;
    }

    namespace
    {
        bool SwitchPlayer(Player* p, const std::string& clip)
        {
            if (!p || !p->Root)
                return false;
            InteractiveVoice* iv = FindInteractive(p->Root.get());
            if (!iv)
                return false;
            return iv->Request(iv->FindClip(clip));
        }
        std::string CurrentClipName(Player* p)
        {
            if (!p || !p->Root)
                return {};
            if (InteractiveVoice* iv = FindInteractive(p->Root.get()))
            {
                const int i = iv->CurrentClip();
                return i >= 0 && i < (int)iv->Asset().InteractiveClips.size() ? iv->Asset().InteractiveClips[(size_t)i].Name : std::string();
            }
            if (auto* pl = dynamic_cast<PlaylistVoice*>(p->Root.get()))
                return std::to_string(pl->CurrentTrack());
            return {};
        }
        bool LayerVolume(Player* p, int layer, float db)
        {
            if (!p || !p->Root)
                return false;
            SyncVoice* sv = FindSync(p->Root.get());
            return sv && sv->SetLayerVolume(layer, db);
        }
    }

    bool SwitchToClip(Scene& scene, uint32_t entity, const std::string& clip) { return SwitchPlayer(Find(scene, entity), clip); }
    std::string GetCurrentClip(Scene& scene, uint32_t entity) { return CurrentClipName(Find(scene, entity)); }
    bool SetLayerVolume(Scene& scene, uint32_t entity, int layer, float db) { return LayerVolume(Find(scene, entity), layer, db); }

    float GetPosition(Scene& scene, uint32_t entity)
    {
        Player* p = Find(scene, entity);
        return p && p->Root ? (float)p->Root->Cursor(Now()) : 0.0f;
    }

    void StopAll(Scene& scene)
    {
        for (auto it = Players().begin(); it != Players().end();)
            it = it->first.first == &scene ? Players().erase(it) : std::next(it);
    }

    void Prune(Scene& scene)
    {
        for (auto it = Players().begin(); it != Players().end();)
        {
            bool keep = it->first.first != &scene;
            if (!keep)
            {
                Entity e{ (entt::entity)it->first.second, &scene };
                keep = e.IsValid() && e.HasComponent<AudioSourceComponent>() && e.GetComponent<AudioSourceComponent>().FilePath == it->second->Path;
            }
            it = keep ? std::next(it) : Players().erase(it);
        }
    }

    bool PreviewPlay(const std::string& path)
    {
        auto& p = Preview();
        if (!p || p->Path != path)
        {
            p = std::make_unique<Player>();
            if (!p->Init())
            {
                p.reset();
                return false;
            }
            p->Path = path;
            p->Node.ConnectToEndpoint();
        }
        return p->Play();
    }
    void PreviewStop()
    {
        if (auto& p = Preview())
            p->Stop();
    }
    bool PreviewIsPlaying() { return Preview() && Preview()->Playing; }
    bool PreviewSwitch(const std::string& clip) { return SwitchPlayer(Preview().get(), clip); }
    std::string PreviewCurrentClip() { return CurrentClipName(Preview().get()); }
    bool PreviewSetLayerVolume(int layer, float db) { return LayerVolume(Preview().get(), layer, db); }
    void UpdatePreview()
    {
        if (auto& p = Preview())
            p->Update(false);
    }

    void Shutdown()
    {
        Players().clear();
        Preview().reset();
    }
}
