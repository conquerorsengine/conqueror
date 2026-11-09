#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "VideoPlayer.h"
#include "Core/Assets/SimpleAsset.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "UI/UIComponents.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Project/Project.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/Shader.h"

#include <glad/glad.h>
#include <miniaudio.h>
#include <stb_image.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;

        void FlipRows(std::vector<uint8_t>& rgba, int w, int h)
        {
            const size_t row = (size_t)w * 4;
            std::vector<uint8_t> tmp(row);
            for (int y = 0; y < h / 2; ++y)
            {
                uint8_t* a = rgba.data() + (size_t)y * row;
                uint8_t* b = rgba.data() + (size_t)(h - 1 - y) * row;
                std::memcpy(tmp.data(), a, row);
                std::memcpy(a, b, row);
                std::memcpy(b, tmp.data(), row);
            }
        }

        // ---------------------------------------------------------------- Kaynaklar
        class VideoSource
        {
        public:
            virtual ~VideoSource() = default;
            virtual int FrameCount() const = 0;
            virtual double FrameTime(int i) const = 0;
            virtual double Duration() const = 0;
            // false: bu kare boş (önceki kare kalır)
            virtual bool Decode(int frame, std::vector<uint8_t>& rgba, int& w, int& h) = 0;
            bool HasAudio() const { return !Audio.empty() && AudioChannels > 0 && AudioRate > 0; }
            std::vector<int16_t> Audio;
            int AudioChannels = 0, AudioRate = 0;
            int Width = 0, Height = 0;
            float Fps = 0.0f;
        };

        // Motion-JPEG AVI (RIFF; OpenDML "AVIX" ekleri dahil)
        class AviSource : public VideoSource
        {
        public:
            bool Open(const std::string& path, std::string& error)
            {
                m_File.open(path, std::ios::binary);
                if (!m_File)
                {
                    error = "Cannot open " + path;
                    return false;
                }
                m_File.seekg(0, std::ios::end);
                const uint64_t size = (uint64_t)m_File.tellg();
                uint64_t pos = 0;
                while (pos + 12 <= size)
                {
                    char id[4], type[4];
                    uint32_t len = 0;
                    if (!ReadAt(pos, id, 4) || !ReadAt(pos + 4, &len, 4) || !ReadAt(pos + 8, type, 4))
                        break;
                    if (std::memcmp(id, "RIFF", 4) != 0)
                        break;
                    ParseList(pos + 12, pos + 8 + len, 0);
                    pos += 8 + (uint64_t)len + (len & 1);
                }
                if (m_VideoStream < 0 || m_Frames.empty())
                {
                    error = "No video stream (only Motion-JPEG AVI is supported)";
                    return false;
                }
                if (std::memcmp(m_Codec, "MJPG", 4) != 0 && std::memcmp(m_Codec, "mjpg", 4) != 0 && std::memcmp(m_Codec, "AVRn", 4) != 0)
                {
                    error = std::string("Unsupported AVI video codec '") + std::string(m_Codec, 4) + "' (use Motion-JPEG)";
                    return false;
                }
                if (Fps <= 0.0f)
                    Fps = 25.0f;
                return true;
            }

            int FrameCount() const override { return (int)m_Frames.size(); }
            double FrameTime(int i) const override { return (double)i / (double)Fps; }
            double Duration() const override { return (double)m_Frames.size() / (double)Fps; }

            bool Decode(int frame, std::vector<uint8_t>& rgba, int& w, int& h) override
            {
                if (frame < 0 || frame >= (int)m_Frames.size() || m_Frames[(size_t)frame].second == 0)
                    return false;
                std::vector<uint8_t> data(m_Frames[(size_t)frame].second);
                if (!ReadAt(m_Frames[(size_t)frame].first, data.data(), data.size()))
                    return false;
                int comp = 0;
                stbi_uc* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &comp, 4);
                if (!pixels)
                    return false;
                rgba.assign(pixels, pixels + (size_t)w * (size_t)h * 4);
                stbi_image_free(pixels);
                FlipRows(rgba, w, h);
                return true;
            }

        private:
            bool ReadAt(uint64_t pos, void* dst, size_t n)
            {
                m_File.clear();
                m_File.seekg((std::streamoff)pos);
                m_File.read((char*)dst, (std::streamsize)n);
                return (size_t)m_File.gcount() == n;
            }

            void ParseList(uint64_t begin, uint64_t end, int depth)
            {
                uint64_t pos = begin;
                while (pos + 8 <= end && depth < 8)
                {
                    char id[4];
                    uint32_t len = 0;
                    if (!ReadAt(pos, id, 4) || !ReadAt(pos + 4, &len, 4))
                        return;
                    const uint64_t data = pos + 8;
                    if (std::memcmp(id, "LIST", 4) == 0)
                    {
                        char type[4];
                        if (!ReadAt(data, type, 4))
                            return;
                        if (std::memcmp(type, "strl", 4) == 0)
                            ++m_StreamIndex;
                        ParseList(data + 4, data + len, depth + 1);
                    }
                    else if (std::memcmp(id, "avih", 4) == 0 && len >= 40)
                    {
                        uint32_t h[10];
                        ReadAt(data, h, 40);
                        if (h[0] > 0) Fps = 1.0e6f / (float)h[0];
                        Width = (int)h[8];
                        Height = (int)h[9];
                    }
                    else if (std::memcmp(id, "strh", 4) == 0 && len >= 32)
                    {
                        char fcc[4];
                        uint32_t v[8];
                        ReadAt(data, fcc, 4);
                        ReadAt(data, v, 32);
                        const uint32_t scale = v[5], rate = v[6];
                        if (std::memcmp(fcc, "vids", 4) == 0 && m_VideoStream < 0)
                        {
                            m_VideoStream = m_StreamIndex - 1;
                            if (scale > 0 && rate > 0) Fps = (float)rate / (float)scale;
                        }
                        else if (std::memcmp(fcc, "auds", 4) == 0 && m_AudioStream < 0)
                            m_AudioStream = m_StreamIndex - 1;
                        m_LastStreamType = std::string(fcc, 4);
                    }
                    else if (std::memcmp(id, "strf", 4) == 0)
                    {
                        if (m_LastStreamType == "vids" && len >= 20)
                        {
                            uint8_t bih[20];
                            ReadAt(data, bih, 20);
                            std::memcpy(m_Codec, bih + 16, 4);
                            int32_t w, hgt;
                            std::memcpy(&w, bih + 4, 4);
                            std::memcpy(&hgt, bih + 8, 4);
                            Width = w;
                            Height = std::abs(hgt);
                        }
                        else if (m_LastStreamType == "auds" && len >= 16)
                        {
                            uint8_t wf[16];
                            ReadAt(data, wf, 16);
                            uint16_t tag, channels, bits;
                            uint32_t rate;
                            std::memcpy(&tag, wf, 2);
                            std::memcpy(&channels, wf + 2, 2);
                            std::memcpy(&rate, wf + 4, 4);
                            std::memcpy(&bits, wf + 14, 2);
                            m_AudioPcm16 = tag == 1 && bits == 16;
                            AudioChannels = channels;
                            AudioRate = (int)rate;
                        }
                    }
                    else if (id[0] >= '0' && id[0] <= '9' && id[1] >= '0' && id[1] <= '9')
                    {
                        const int stream = (id[0] - '0') * 10 + (id[1] - '0');
                        if (stream == m_VideoStream && (id[2] == 'd'))
                            m_Frames.emplace_back(data, len);
                        else if (stream == m_AudioStream && id[2] == 'w' && id[3] == 'b' && m_AudioPcm16 && len > 0 && Audio.size() < 200u * 1024 * 1024)
                        {
                            const size_t old = Audio.size();
                            Audio.resize(old + len / 2);
                            ReadAt(data, Audio.data() + old, (len / 2) * 2);
                        }
                    }
                    pos = data + len + (len & 1);
                }
            }

            std::ifstream m_File;
            std::vector<std::pair<uint64_t, uint32_t>> m_Frames;
            int m_StreamIndex = 0, m_VideoStream = -1, m_AudioStream = -1;
            bool m_AudioPcm16 = false;
            std::string m_LastStreamType;
            char m_Codec[4] = { 0, 0, 0, 0 };
        };

        // Animasyonlu GIF (tüm kareler belleğe çözülür)
        class GifSource : public VideoSource
        {
        public:
            bool Open(const std::string& path, std::string& error)
            {
                std::ifstream f(path, std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                int* delays = nullptr;
                int w = 0, h = 0, frames = 0, comp = 0;
                stbi_uc* data = stbi_load_gif_from_memory(bytes.data(), (int)bytes.size(), &delays, &w, &h, &frames, &comp, 4);
                if (!data || frames <= 0)
                {
                    error = "Cannot decode GIF " + path;
                    return false;
                }
                Width = w;
                Height = h;
                const size_t frameBytes = (size_t)w * (size_t)h * 4;
                double t = 0.0;
                for (int i = 0; i < frames; ++i)
                {
                    std::vector<uint8_t> px(data + frameBytes * (size_t)i, data + frameBytes * (size_t)(i + 1));
                    FlipRows(px, w, h);
                    m_Frames.push_back(std::move(px));
                    m_Times.push_back(t);
                    t += std::max(delays ? delays[i] : 100, 20) / 1000.0;
                }
                m_Duration = t;
                Fps = (float)((double)frames / std::max(t, 1e-3));
                stbi_image_free(data);
                if (delays) stbi_image_free(delays);
                return true;
            }
            int FrameCount() const override { return (int)m_Frames.size(); }
            double FrameTime(int i) const override { return m_Times[(size_t)i]; }
            double Duration() const override { return m_Duration; }
            bool Decode(int frame, std::vector<uint8_t>& rgba, int& w, int& h) override
            {
                if (frame < 0 || frame >= (int)m_Frames.size())
                    return false;
                rgba = m_Frames[(size_t)frame];
                w = Width;
                h = Height;
                return true;
            }

        private:
            std::vector<std::vector<uint8_t>> m_Frames;
            std::vector<double> m_Times;
            double m_Duration = 0.0;
        };

        // Klasördeki PNG / JPG / TGA dizisi (ada göre sıralı)
        class SequenceSource : public VideoSource
        {
        public:
            bool Open(const std::string& dir, float fps, std::string& error)
            {
                std::error_code ec;
                for (const auto& e : fs::directory_iterator(dir, ec))
                {
                    std::string ext = e.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
                        m_Files.push_back(e.path().string());
                }
                std::sort(m_Files.begin(), m_Files.end());
                if (m_Files.empty())
                {
                    error = "No images in " + dir;
                    return false;
                }
                Fps = std::max(fps, 0.1f);
                int w = 0, h = 0, comp = 0;
                if (stbi_info(m_Files[0].c_str(), &w, &h, &comp))
                {
                    Width = w;
                    Height = h;
                }
                return true;
            }
            int FrameCount() const override { return (int)m_Files.size(); }
            double FrameTime(int i) const override { return (double)i / (double)Fps; }
            double Duration() const override { return (double)m_Files.size() / (double)Fps; }
            bool Decode(int frame, std::vector<uint8_t>& rgba, int& w, int& h) override
            {
                if (frame < 0 || frame >= (int)m_Files.size())
                    return false;
                int comp = 0;
                stbi_uc* px = stbi_load(m_Files[(size_t)frame].c_str(), &w, &h, &comp, 4);
                if (!px)
                    return false;
                rgba.assign(px, px + (size_t)w * (size_t)h * 4);
                stbi_image_free(px);
                FlipRows(rgba, w, h);
                return true;
            }

        private:
            std::vector<std::string> m_Files;
        };

        // ---------------------------------------------------------------- ffmpeg ile dönüştürme
        std::string ShellQuote(const std::string& s)
        {
            std::string out = "\"";
            for (char c : s)
            {
                if (c == '"' || c == '\\' || c == '$' || c == '`')
                    out += '\\';
                out += c;
            }
            return out + "\"";
        }

        bool HasFfmpeg()
        {
            static int cached = -1;
            if (cached < 0)
            {
#ifdef _WIN32
                cached = std::system("where ffmpeg >NUL 2>NUL") == 0 ? 1 : 0;
#else
                cached = std::system("command -v ffmpeg >/dev/null 2>&1") == 0 ? 1 : 0;
#endif
            }
            return cached == 1;
        }

        std::string TranscodeToMjpeg(const std::string& source, std::string& error)
        {
            std::error_code ec;
            fs::path project = Project::GetActiveProjectDirectory();
            fs::path cacheDir = (project.empty() ? fs::temp_directory_path(ec) : project / "Library") / "VideoCache";
            fs::create_directories(cacheDir, ec);
            // libc++'ta file_time_type sayacı __int128: to_string için 64 bite indir
            const long long stamp = (long long)fs::last_write_time(source, ec).time_since_epoch().count();
            const size_t key = std::hash<std::string>()(fs::absolute(source, ec).string() + "|" + std::to_string(stamp));
            fs::path out = cacheDir / (fs::path(source).stem().string() + "_" + std::to_string(key) + ".avi");
            if (fs::exists(out, ec) && fs::file_size(out, ec) > 0)
                return out.string();
            if (!HasFfmpeg())
            {
                error = "Only Motion-JPEG AVI, GIF and image folders play without ffmpeg; install ffmpeg to play " + fs::path(source).extension().string();
                return {};
            }
            const std::string cmd = "ffmpeg -y -loglevel error -i " + ShellQuote(source) + " -c:v mjpeg -q:v 3 -pix_fmt yuvj420p -c:a pcm_s16le -ar 44100 -ac 2 " + ShellQuote(out.string())
#ifdef _WIN32
                + " >NUL 2>NUL";
#else
                + " >/dev/null 2>&1";
#endif
            CQ_CORE_INFO("[Video] Converting {} for playback (ffmpeg, cached)", source);
            if (std::system(cmd.c_str()) != 0 || !fs::exists(out, ec))
            {
                error = "ffmpeg could not convert " + source;
                return {};
            }
            return out.string();
        }

        // ---------------------------------------------------------------- Oynatıcı örnekleri
        struct Instance
        {
            std::unique_ptr<VideoSource> Source;
            std::string LoadedSource;
            std::vector<std::string> Playlist;       // Media Playlist / Player
            int PlaylistIndex = 0;
            bool MediaLoop = false, MediaShuffle = false;
            float MediaRate = 1.0f;
            std::string Error;
            std::shared_ptr<Texture2D> Texture;
            std::vector<uint8_t> Pixels;
            int UploadedFrame = -1;
            bool Playing = false;
            bool Started = false;
            double Time = 0.0;
            ma_audio_buffer* AudioBuffer = nullptr;
            ma_sound* Sound = nullptr;

            void ReleaseAudio()
            {
                if (Sound) { ma_sound_uninit(Sound); delete Sound; Sound = nullptr; }
                if (AudioBuffer) { ma_audio_buffer_uninit(AudioBuffer); delete AudioBuffer; AudioBuffer = nullptr; }
            }
            ~Instance() { ReleaseAudio(); }
        };
        std::map<std::pair<Scene*, uint32_t>, std::unique_ptr<Instance>>* s_Instances = nullptr;
        std::map<std::pair<Scene*, uint32_t>, std::unique_ptr<Instance>>& Instances()
        {
            if (!s_Instances) s_Instances = new std::map<std::pair<Scene*, uint32_t>, std::unique_ptr<Instance>>();
            return *s_Instances;
        }

        std::shared_ptr<Shader> s_PlaneShader;
        GLuint s_PlaneVao = 0;

        // Media Source / Playlist / Player -> oynatılacak dosya
        std::string MediaFileOf(const std::string& item)
        {
            if (!SimpleAssets::IsKind(item, "MediaSource"))
                return item;
            SimpleAssets::Data data;
            return SimpleAssets::Load(item, data) ? data.Str("File") : std::string();
        }

        std::string ResolveMediaSource(Instance& inst, const VideoPlayerComponent& vp)
        {
            std::string playlist;
            if (SimpleAssets::IsKind(vp.Source, "MediaPlayer"))
            {
                SimpleAssets::Data data;
                if (SimpleAssets::Load(vp.Source, data))
                {
                    playlist = data.Str("Playlist");
                    inst.MediaLoop = data.Bool("Loop");
                    inst.MediaShuffle = data.Bool("Shuffle");
                    inst.MediaRate = data.Float("Rate", 1.0f);
                }
            }
            else if (SimpleAssets::IsKind(vp.Source, "MediaPlaylist"))
                playlist = vp.Source;
            if (playlist.empty())
                return MediaFileOf(vp.Source);
            SimpleAssets::Data list;
            if (!SimpleAssets::Load(playlist, list))
                return {};
            inst.Playlist.clear();
            for (const auto& row : list.Table("Items"))
                if (const std::string file = MediaFileOf(SimpleAssets::Cell(row, 0)); !file.empty())
                    inst.Playlist.push_back(file);
            if (inst.Playlist.empty())
                return {};
            inst.PlaylistIndex = std::clamp(inst.PlaylistIndex, 0, (int)inst.Playlist.size() - 1);
            return inst.Playlist[(size_t)inst.PlaylistIndex];
        }

        bool OpenSource(Instance& inst, const VideoPlayerComponent& vp)
        {
            inst.ReleaseAudio();
            inst.Source.reset();
            inst.Texture.reset();
            inst.UploadedFrame = -1;
            inst.Error.clear();
            inst.LoadedSource = vp.Source;
            inst.Time = 0.0;
            if (vp.Source.empty())
                return false;
            std::string path = SceneSerializer::ResolveAssetPath(ResolveMediaSource(inst, vp));
            std::error_code ec;
            if (fs::is_directory(path, ec))
            {
                auto s = std::make_unique<SequenceSource>();
                if (!s->Open(path, vp.ImageSequenceFrameRate, inst.Error))
                    return false;
                inst.Source = std::move(s);
            }
            else
            {
                std::string ext = fs::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".gif")
                {
                    auto s = std::make_unique<GifSource>();
                    if (!s->Open(path, inst.Error))
                        return false;
                    inst.Source = std::move(s);
                }
                else
                {
                    auto s = std::make_unique<AviSource>();
                    std::string err;
                    if (ext != ".avi" || !s->Open(path, err))
                    {
                        // MJPEG olmayan AVI ya da başka kap: ffmpeg ile bir kez dönüştür
                        std::string converted = TranscodeToMjpeg(path, inst.Error);
                        if (converted.empty())
                        {
                            if (inst.Error.empty()) inst.Error = err;
                            return false;
                        }
                        s = std::make_unique<AviSource>();
                        if (!s->Open(converted, inst.Error))
                            return false;
                    }
                    inst.Source = std::move(s);
                }
            }
            // Ses (PCM 16 bit): miniaudio veri kaynağı
            if (inst.Source->HasAudio() && vp.AudioOutput == VideoAudioOutput::Direct && AudioEngine::IsInitialized())
            {
                const auto& pcm = inst.Source->Audio;
                const ma_uint64 frames = pcm.size() / (size_t)inst.Source->AudioChannels;
                ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_s16, (ma_uint32)inst.Source->AudioChannels, frames, pcm.data(), nullptr);
                cfg.sampleRate = (ma_uint32)inst.Source->AudioRate;
                inst.AudioBuffer = new ma_audio_buffer();
                if (ma_audio_buffer_init(&cfg, inst.AudioBuffer) == MA_SUCCESS)
                {
                    inst.Sound = new ma_sound();
                    if (ma_sound_init_from_data_source(AudioEngine::Get().GetNativeEngine(), inst.AudioBuffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, inst.Sound) != MA_SUCCESS)
                    {
                        delete inst.Sound;
                        inst.Sound = nullptr;
                    }
                }
                else
                {
                    delete inst.AudioBuffer;
                    inst.AudioBuffer = nullptr;
                }
            }
            return true;
        }

        int FrameAt(const VideoSource& s, double t)
        {
            int lo = 0, hi = s.FrameCount() - 1, best = 0;
            while (lo <= hi)
            {
                int mid = (lo + hi) / 2;
                if (s.FrameTime(mid) <= t) { best = mid; lo = mid + 1; }
                else hi = mid - 1;
            }
            return best;
        }

        void ApplyToTargets(Scene& scene, entt::entity e, const VideoPlayerComponent& vp, const std::shared_ptr<Texture2D>& tex)
        {
            auto& reg = scene.m_Registry;
            entt::entity target = e;
            if (vp.Target)
            {
                Entity t = scene.FindEntityByUUID(vp.Target);
                if (t) target = (entt::entity)t;
            }
            switch (vp.RenderMode)
            {
                case VideoRenderMode::MaterialOverride:
                    if (auto* mr = reg.try_get<MeshRendererComponent>(target))
                    {
                        // Malzemenin taban dokusu video karesi (Unity VideoPlayer Material Override); nesneye özel örnek
                        MeshRendererMaterial::SetTexture(*mr, tex);
                        if (vp.Emissive)
                        {
                            const Material& cur = MeshRendererMaterial::Shared(*mr);
                            if (cur.EmissionMap != tex || cur.EmissionColor != glm::vec3(1.0f) || cur.EmissionStrength < 1.0f)
                            {
                                Material& m = MeshRendererMaterial::Edit(*mr);
                                m.EmissionMap = tex;
                                m.EmissionColor = glm::vec3(1.0f);
                                m.EmissionStrength = std::max(m.EmissionStrength, 1.0f);
                            }
                        }
                    }
                    break;
                case VideoRenderMode::RawImage:
                    if (auto* raw = reg.try_get<UIRawImageComponent>(target))
                        raw->ExternalTexture = tex;
                    break;
                case VideoRenderMode::SpriteRenderer:
                    if (auto* sr = reg.try_get<SpriteRendererComponent>(target))
                        sr->Texture = tex;
                    break;
                default:
                    break;
            }
        }
    }

    namespace VideoPlayers
    {
        void Update(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            auto& all = Instances();
            for (auto e : reg.view<VideoPlayerComponent>())
            {
                auto& vp = reg.get<VideoPlayerComponent>(e);
                auto& slot = all[{ &scene, (uint32_t)e }];
                if (!slot)
                    slot = std::make_unique<Instance>();
                Instance& inst = *slot;
                if (inst.LoadedSource != vp.Source)
                {
                    const bool ok = OpenSource(inst, vp);
                    vp.Status = ok ? "Ready" : inst.Error;
                    if (ok && inst.Playlist.size() > 1)
                        vp.Status += " (item " + std::to_string(inst.PlaylistIndex + 1) + " of " + std::to_string(inst.Playlist.size()) + ")";
                    if (!ok && !inst.Error.empty())
                        CQ_CORE_WARN("[Video] {}", inst.Error);
                    inst.Started = false;
                }
                if (!inst.Source)
                {
                    vp.IsPlaying = false;
                    continue;
                }
                VideoSource& src = *inst.Source;
                vp.FrameCount = src.FrameCount();
                vp.Length = (float)src.Duration();
                vp.Width = src.Width;
                vp.Height = src.Height;
                vp.FrameRate = src.Fps;
                if (!inst.Started)
                {
                    inst.Started = true;
                    if (vp.PlayOnAwake && scene.IsEntityActive(e))
                        inst.Playing = true;
                }

                // Ses
                if (inst.Sound)
                {
                    ma_sound_set_volume(inst.Sound, std::clamp(vp.Volume, 0.0f, 4.0f));
                    ma_sound_set_pitch(inst.Sound, std::max(vp.PlaybackSpeed, 0.01f));
                    ma_sound_set_looping(inst.Sound, vp.Loop ? MA_TRUE : MA_FALSE);
                    const bool soundPlaying = ma_sound_is_playing(inst.Sound);
                    if (inst.Playing && !soundPlaying && !ma_sound_at_end(inst.Sound))
                        ma_sound_start(inst.Sound);
                    else if (!inst.Playing && soundPlaying)
                        ma_sound_stop(inst.Sound);
                }

                // Zaman: ses varsa ses imlecini izle (dudak senkronu), yoksa kare süresi
                const double duration = std::max(src.Duration(), 1e-3);
                if (inst.Playing)
                {
                    float cursor = 0.0f;
                    if (inst.Sound && ma_sound_is_playing(inst.Sound) && ma_sound_get_cursor_in_seconds(inst.Sound, &cursor) == MA_SUCCESS)
                        inst.Time = cursor;
                    else
                        inst.Time += (double)dt * std::max(vp.PlaybackSpeed, 0.0f) * inst.MediaRate;
                    if (inst.Time >= duration)
                    {
                        if (vp.Loop)
                        {
                            inst.Time = std::fmod(inst.Time, duration);
                            if (inst.Sound) ma_sound_seek_to_pcm_frame(inst.Sound, (ma_uint64)(inst.Time * src.AudioRate));
                        }
                        else if (inst.Playlist.size() > 0 && (inst.PlaylistIndex + 1 < (int)inst.Playlist.size() || inst.MediaLoop))
                        {
                            // Sıradaki öğe (Shuffle: rastgele, aynısı tekrar seçilmez)
                            if (inst.MediaShuffle && inst.Playlist.size() > 1)
                            {
                                int next = inst.PlaylistIndex;
                                while (next == inst.PlaylistIndex)
                                    next = std::rand() % (int)inst.Playlist.size();
                                inst.PlaylistIndex = next;
                            }
                            else
                                inst.PlaylistIndex = (inst.PlaylistIndex + 1) % (int)inst.Playlist.size();
                            inst.LoadedSource.clear();
                            continue;
                        }
                        else
                        {
                            inst.Time = duration;
                            inst.Playing = false;
                        }
                    }
                }

                // Kare çöz + yükle (yalnızca değişince)
                const int frame = std::min(FrameAt(src, inst.Time), src.FrameCount() - 1);
                if (frame != inst.UploadedFrame)
                {
                    int w = 0, h = 0;
                    if (src.Decode(frame, inst.Pixels, w, h) && w > 0 && h > 0)
                    {
                        if (!inst.Texture || (int)inst.Texture->GetWidth() != w || (int)inst.Texture->GetHeight() != h)
                            inst.Texture = Texture2D::Create((uint32_t)w, (uint32_t)h);
                        inst.Texture->SetData(inst.Pixels.data(), (uint32_t)inst.Pixels.size());
                    }
                    inst.UploadedFrame = frame;
                }
                vp.Frame = frame;
                vp.Time = (float)inst.Time;
                vp.IsPlaying = inst.Playing;
                if (inst.Texture)
                    ApplyToTargets(scene, e, vp, inst.Texture);
            }
            // Silinmiş varlıkların oynatıcıları
            for (auto it = all.begin(); it != all.end();)
            {
                if (it->first.first == &scene && (!reg.valid((entt::entity)it->first.second) || !reg.all_of<VideoPlayerComponent>((entt::entity)it->first.second)))
                    it = all.erase(it);
                else
                    ++it;
            }
        }

        void RenderCameraPlanes(Scene& scene, bool farPlane)
        {
            auto& reg = scene.m_Registry;
            auto& all = Instances();
            for (auto e : reg.view<VideoPlayerComponent>())
            {
                const auto& vp = reg.get<VideoPlayerComponent>(e);
                if ((farPlane && vp.RenderMode != VideoRenderMode::CameraFarPlane) || (!farPlane && vp.RenderMode != VideoRenderMode::CameraNearPlane))
                    continue;
                auto it = all.find({ &scene, (uint32_t)e });
                if (it == all.end() || !it->second->Texture || !scene.IsEntityActive(e))
                    continue;
                if (!s_PlaneShader)
                    s_PlaneShader = Shader::Create("Engine/src/Shaders/2D/VideoPlane.cqsh");
                if (!s_PlaneShader || !s_PlaneShader->GetRendererID())
                    return;
                if (!s_PlaneVao)
                    glCreateVertexArrays(1, &s_PlaneVao);
                GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
                GLboolean depthMask;
                glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                for (GLuint i = 1; i < 4; ++i)
                    glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                s_PlaneShader->Bind();
                it->second->Texture->Bind(0);
                s_PlaneShader->SetInt("u_Texture", 0);
                s_PlaneShader->SetFloat("u_Alpha", std::clamp(vp.CameraAlpha, 0.0f, 1.0f));
                glBindVertexArray(s_PlaneVao);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                for (GLuint i = 1; i < 4; ++i)
                    glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(depthMask);
                if (depthTest) glEnable(GL_DEPTH_TEST);
                if (!blend) glDisable(GL_BLEND);
            }
        }

        static Instance* Find(Scene& scene, uint32_t entity)
        {
            auto& all = Instances();
            auto it = all.find({ &scene, entity });
            return it != all.end() ? it->second.get() : nullptr;
        }

        void Play(Scene& scene, uint32_t entity)
        {
            if (Instance* inst = Find(scene, entity))
            {
                if (!inst->Playing && inst->Source && inst->Time >= inst->Source->Duration())
                {
                    inst->Time = 0.0;
                    if (inst->Sound) ma_sound_seek_to_pcm_frame(inst->Sound, 0);
                }
                inst->Playing = true;
                inst->Started = true;
            }
            else if (scene.m_Registry.valid((entt::entity)entity) && scene.m_Registry.all_of<VideoPlayerComponent>((entt::entity)entity))
                scene.m_Registry.get<VideoPlayerComponent>((entt::entity)entity).PlayOnAwake = true;
        }

        void Pause(Scene& scene, uint32_t entity)
        {
            if (Instance* inst = Find(scene, entity))
                inst->Playing = false;
        }

        void Stop(Scene& scene, uint32_t entity)
        {
            if (Instance* inst = Find(scene, entity))
            {
                inst->Playing = false;
                inst->Time = 0.0;
                if (inst->Sound)
                {
                    ma_sound_stop(inst->Sound);
                    ma_sound_seek_to_pcm_frame(inst->Sound, 0);
                }
            }
        }

        void Seek(Scene& scene, uint32_t entity, float seconds)
        {
            if (Instance* inst = Find(scene, entity); inst && inst->Source)
            {
                inst->Time = std::clamp((double)seconds, 0.0, inst->Source->Duration());
                if (inst->Sound)
                    ma_sound_seek_to_pcm_frame(inst->Sound, (ma_uint64)(inst->Time * inst->Source->AudioRate));
            }
        }

        std::shared_ptr<Texture2D> GetTexture(Scene& scene, uint32_t entity)
        {
            Instance* inst = Find(scene, entity);
            return inst ? inst->Texture : nullptr;
        }

        void Release(Scene* scene)
        {
            auto& all = Instances();
            for (auto it = all.begin(); it != all.end();)
            {
                if (it->first.first == scene)
                    it = all.erase(it);
                else
                    ++it;
            }
        }

        void Shutdown()
        {
            delete s_Instances;
            s_Instances = nullptr;
            s_PlaneShader.reset();
            if (s_PlaneVao)
            {
                glDeleteVertexArrays(1, &s_PlaneVao);
                s_PlaneVao = 0;
            }
        }
    }

    void RegisterVideoComponents()
    {
        using V = VideoPlayerComponent;
        ComponentDef<V>("Video Player", "VideoPlayerComponent", "Video", ICON_FA_FILM)
            .Describe("Plays Motion-JPEG AVI, GIF or an image folder (other formats are converted once with ffmpeg if it is installed) onto a mesh, a Raw Image, a Sprite or the camera background.")
            .Field("Source", "Source", &V::Source, FieldKind::Asset).Filter(".avi;.gif;.mp4;.webm;.mov;.mkv;.cqmediasource;.cqmediaplaylist;.cqmediaplayer")
            .Field("PlayOnAwake", "Play On Awake", &V::PlayOnAwake)
            .Field("Loop", "Loop", &V::Loop)
            .Field("PlaybackSpeed", "Playback Speed", &V::PlaybackSpeed).Range(0.0f, 10.0f, 0.01f)
            .Field("RenderMode", "Render Mode", &V::RenderMode).Options({ "Material Override", "Camera Far Plane", "Camera Near Plane", "Raw Image", "Sprite Renderer", "API Only" })
            .Field("Target", "Target", &V::Target).Tooltip("Object that shows the video (empty = this object).")
            .Field("Emissive", "Emissive", &V::Emissive).VisibleIf([](const V& v) { return v.RenderMode == VideoRenderMode::MaterialOverride; })
            .Field("CameraAlpha", "Alpha", &V::CameraAlpha).Range(0.0f, 1.0f, 0.01f)
                .VisibleIf([](const V& v) { return v.RenderMode == VideoRenderMode::CameraFarPlane || v.RenderMode == VideoRenderMode::CameraNearPlane; })
            .Field("AudioOutput", "Audio Output", &V::AudioOutput).Options({ "None", "Direct" }).Header("Audio")
            .Field("Volume", "Volume", &V::Volume).Range(0.0f, 4.0f, 0.01f)
            .Field("ImageSequenceFrameRate", "Image Sequence FPS", &V::ImageSequenceFrameRate).Range(0.1f, 240.0f, 0.1f)
            .Field("IsPlaying", "Playing", &V::IsPlaying).ReadOnly()
            .Field("Time", "Time", &V::Time).ReadOnly()
            .Field("Length", "Length", &V::Length).ReadOnly()
            .Field("Frame", "Frame", &V::Frame).ReadOnly()
            .Field("Status", "Status", &V::Status).ReadOnly();
    }
}
