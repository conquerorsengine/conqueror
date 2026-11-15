#include "Recorder.h"
#include "Core/Utils/Threads.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/ProcessRunner.h"
#include "Core/Time/TimeManager.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Utils/Platform.h"

#include <glad/glad.h>
#include "stb_image_write.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct Frame
        {
            std::vector<uint8_t> Pixels;
            uint32_t Index = 0;
        };

        struct RecorderState
        {
            RecorderSettings Settings;
            std::atomic<bool> Recording{ false };
            uint32_t Width = 0, Height = 0;       // kodlayıcı boyutu (çift)
            uint32_t SourceWidth = 0, SourceHeight = 0;
            float Elapsed = 0.0f;
            float Accumulator = 0.0f;
            uint32_t Frames = 0;
            std::string Output;
            std::string LastOutput;
            std::string LastError;
            std::unique_ptr<ProcessRunner> FFmpeg;

            std::thread Worker;
            std::mutex Mutex;
            std::condition_variable Cv;
            std::deque<Frame> Queue;
            bool StopWorker = false;
            std::vector<uint8_t> Scratch;
            int FFmpegAvailable = -1;
            std::string H264Encoder;              // "libx264" / "libopenh264" / boş (MPEG-4 Part 2'ye düşer)
            bool Fixed = false;                   // bu kayıt sabit adımlı (çevrimdışı)
            std::string FFmpegExe;                // bulunan ffmpeg (tırnaksız)
            // Ses (249): WAV (16 bit PCM) kayıt sırasında yazılır, bitince videoyla birleştirilir
            bool Audio = false;
            uint32_t AudioChannels = 0, AudioRate = 0;
            uint64_t AudioFrames = 0;
            std::FILE* AudioFile = nullptr;
            std::string AudioPath;
            std::vector<float> AudioBuffer;
        };

        RecorderState& S()
        {
            static RecorderState s;
            return s;
        }

        std::string Timestamp()
        {
            std::time_t t = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
            return buf;
        }

        std::string Quote(const std::string& s);

        // 16 bit PCM WAV: başlık şimdi, boyutlar bitince
        void WriteWavHeader(std::FILE* f, uint32_t channels, uint32_t rate, uint32_t dataBytes)
        {
            auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
            auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
            std::fseek(f, 0, SEEK_SET);
            std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
            std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16((uint16_t)channels); u32(rate); u32(rate * channels * 2); u16((uint16_t)(channels * 2)); u16(16);
            std::fwrite("data", 1, 4, f); u32(dataBytes);
        }

        void DrainAudio()
        {
            auto& s = S();
            if (!s.Audio || !s.AudioFile || !AudioEngine::IsInitialized())
                return;
            const size_t chunk = 4096;
            s.AudioBuffer.resize(chunk * s.AudioChannels);
            std::vector<int16_t> pcm(chunk * s.AudioChannels);
            for (;;)
            {
                const size_t n = AudioEngine::Get().ReadCapture(s.AudioBuffer.data(), chunk);
                if (n == 0)
                    break;
                for (size_t i = 0; i < n * s.AudioChannels; ++i)
                    pcm[i] = (int16_t)std::lround(std::clamp(s.AudioBuffer[i], -1.0f, 1.0f) * 32767.0f);
                std::fwrite(pcm.data(), sizeof(int16_t), n * s.AudioChannels, s.AudioFile);
                s.AudioFrames += n;
            }
        }

        std::string Quote(const std::string& s)
        {
#ifdef _WIN32
            return "\"" + s + "\"";
#else
            std::string out = "'";
            for (char c : s)
            {
                if (c == '\'') out += "'\\''";
                else out += c;
            }
            return out + "'";
#endif
        }

        void WorkerLoop()
        {
            auto& s = S();
            for (;;)
            {
                Frame frame;
                {
                    std::unique_lock<std::mutex> lock(s.Mutex);
                    s.Cv.wait(lock, [&] { return s.StopWorker || !s.Queue.empty(); });
                    if (s.Queue.empty() && s.StopWorker)
                        break;
                    frame = std::move(s.Queue.front());
                    s.Queue.pop_front();
                }
                s.Cv.notify_all();

                if (s.Settings.Format == RecordingFormat::PNGSequence)
                {
                    char name[64];
                    snprintf(name, sizeof(name), "frame_%06u.png", frame.Index);
                    std::string path = (std::filesystem::path(s.Output) / name).string();
                    // GL satırları alttan üste: ters çevirerek yaz
                    stbi_flip_vertically_on_write(1);
                    stbi_write_png(path.c_str(), (int)s.Width, (int)s.Height, 4, frame.Pixels.data(), (int)s.Width * 4);
                    stbi_flip_vertically_on_write(0);
                }
                else if (s.FFmpeg)
                {
                    std::string data(reinterpret_cast<const char*>(frame.Pixels.data()), frame.Pixels.size());
                    if (!s.FFmpeg->WriteInput(data))
                    {
                        s.LastError = "ffmpeg stopped accepting frames";
                    }
                    std::string out = s.FFmpeg->ReadAvailable();
                    if (!out.empty())
                        CQ_CORE_WARN("[Recorder] ffmpeg: {0}", out);
                }
            }
        }
    }

    RecorderSettings& Recorder::Settings() { return S().Settings; }
    bool Recorder::IsRecording() { return S().Recording.load(); }
    float Recorder::GetElapsedSeconds() { return S().Elapsed; }
    uint32_t Recorder::GetFrameCount() { return S().Frames; }
    const std::string& Recorder::GetCurrentOutput() { return S().Output; }
    const std::string& Recorder::GetLastOutput() { return S().LastOutput; }
    const std::string& Recorder::GetLastError() { return S().LastError; }
    float Recorder::GetAudioSeconds() { return S().AudioRate ? (float)((double)S().AudioFrames / S().AudioRate) : 0.0f; }

    std::string Recorder::FFmpegPath()
    {
        namespace fs = std::filesystem;
#ifdef _WIN32
        const char* exe = "ffmpeg.exe";
#else
        const char* exe = "ffmpeg";
#endif
        std::error_code ec;
        if (const char* env = std::getenv("CQ_FFMPEG"); env && env[0] && fs::exists(env, ec))
            return env;
        // Motorla gelen kopya (Tools/FFmpeg/fetch-ffmpeg betiği ya da sürüm paketi buraya koyar) ve kullanıcı klasörüne indirilen
        const fs::path candidates[] = {
            fs::current_path() / "Tools" / "ffmpeg" / exe,
            fs::current_path() / exe,
            fs::path(Platform::GetUserAppDataDirectory()) / "ConquerorEngine" / "ffmpeg" / exe,
        };
        for (const auto& c : candidates)
            if (!c.empty() && fs::exists(c, ec))
                return c.string();
        return "ffmpeg";   // PATH
    }

    bool Recorder::IsFFmpegAvailable()
    {
        auto& s = S();
        if (s.FFmpegAvailable < 0)
        {
            ProcessRunner probe;
            bool ok = false;
            s.FFmpegExe = FFmpegPath();
            if (probe.Start(Quote(s.FFmpegExe) + " -hide_banner -version", std::filesystem::current_path()))
            {
                std::string out;
                for (int i = 0; i < 300 && probe.IsRunning(); ++i)
                {
                    out += probe.ReadAvailable();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                out += probe.ReadAvailable();
                ok = probe.GetExitCode() == 0 && out.find("ffmpeg") != std::string::npos;
            }
            s.FFmpegAvailable = ok ? 1 : 0;
            // En iyi H.264 kodlayıcısı (Fedora ffmpeg-free'de libx264 yok, OpenH264 var)
            if (ok)
            {
                ProcessRunner enc;
                if (enc.Start(Quote(s.FFmpegExe) + " -hide_banner -encoders", std::filesystem::current_path()))
                {
                    std::string list;
                    for (int i = 0; i < 400 && enc.IsRunning(); ++i)
                    {
                        list += enc.ReadAvailable();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    list += enc.ReadAvailable();
                    if (list.find(" libx264 ") != std::string::npos)
                        s.H264Encoder = "libx264";
                    else if (list.find(" libopenh264 ") != std::string::npos)
                        s.H264Encoder = "libopenh264";
                }
            }
        }
        return s.FFmpegAvailable == 1;
    }

    bool Recorder::Start(uint32_t width, uint32_t height)
    {
        if constexpr (!Threads::Available)
        {
            CQ_CORE_WARN("[Recorder] Video recording needs background threads and is not available in this build");
            return false;
        }
        auto& s = S();
        if (s.Recording || width < 2 || height < 2)
            return false;
        s.LastError.clear();

        auto& cfg = s.Settings;
        cfg.FrameRate = std::clamp(cfg.FrameRate, 1, 240);
        if (cfg.Format != RecordingFormat::PNGSequence && !IsFFmpegAvailable())
        {
            s.LastError = "ffmpeg not found: falling back to PNG sequence";
            CQ_CORE_WARN("[Recorder] {0}", s.LastError);
            cfg.Format = RecordingFormat::PNGSequence;
        }

        std::filesystem::path dir = cfg.OutputDirectory;
        if (dir.empty())
        {
            auto project = Project::GetActiveProjectDirectory();
            dir = project.empty() ? std::filesystem::current_path() / "Recordings" : project / "Recordings";
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        s.SourceWidth = width;
        s.SourceHeight = height;
        s.Width = width & ~1u;   // kodlayıcılar çift boyut ister
        s.Height = height & ~1u;
        std::string base = "Recording_" + Timestamp();

        if (cfg.Format == RecordingFormat::PNGSequence)
        {
            s.Output = (dir / base).string();
            std::filesystem::create_directories(s.Output, ec);
        }
        else
        {
            const char* ext = cfg.Format == RecordingFormat::WebM ? ".webm" : cfg.Format == RecordingFormat::GIF ? ".gif" : ".mp4";
            s.Output = (dir / (base + ext)).string();
            int outW = std::max(2, (int)((float)s.Width * std::clamp(cfg.ResolutionScale, 0.1f, 1.0f))) & ~1;
            int outH = std::max(2, (int)((float)s.Height * std::clamp(cfg.ResolutionScale, 0.1f, 1.0f))) & ~1;
            std::string filters = "vflip";
            if (cfg.OutputWidth > 0 && cfg.OutputHeight > 0)
            {
                // Tam çıkış çözünürlüğü: oran korunarak sığdırılır, kalan kenarlar siyah
                outW = std::clamp(cfg.OutputWidth, 2, 8192) & ~1;
                outH = std::clamp(cfg.OutputHeight, 2, 8192) & ~1;
                const std::string w = std::to_string(outW), h = std::to_string(outH);
                filters += ",scale=" + w + ":" + h + ":flags=lanczos:force_original_aspect_ratio=decrease,pad=" + w + ":" + h + ":(ow-iw)/2:(oh-ih)/2:color=black";
            }
            else if (outW != (int)s.Width || outH != (int)s.Height)
                filters += ",scale=" + std::to_string(outW) + ":" + std::to_string(outH) + ":flags=lanczos";

            std::string codec;
            float q = std::clamp(cfg.Quality, 0.0f, 1.0f);
            if (cfg.Format == RecordingFormat::MP4)
            {
                if (s.H264Encoder == "libx264")
                {
                    const int crf = 14 + (int)((1.0f - q) * 20.0f);   // 14 (neredeyse kayıpsız) .. 34
                    codec = "-c:v libx264 -preset medium -crf " + std::to_string(crf) + " -pix_fmt yuv420p -movflags +faststart";
                }
                else if (s.H264Encoder == "libopenh264")
                {
                    // Bit hızı: piksel x kare x kalite (1080p30, kalite 0.85: ~14 Mbit/s)
                    const double bpp = 0.03 + 0.2 * q;
                    const long long rate = (long long)((double)outW * outH * cfg.FrameRate * bpp);
                    codec = "-c:v libopenh264 -b:v " + std::to_string(std::max(rate, 500000LL)) + " -allow_skip_frames 0 -pix_fmt yuv420p -movflags +faststart";
                }
                else
                {
                    int qscale = 2 + (int)((1.0f - q) * 12.0f); // 2 (en iyi) .. 14
                    codec = "-c:v mpeg4 -q:v " + std::to_string(qscale) + " -pix_fmt yuv420p";
                }
            }
            else if (cfg.Format == RecordingFormat::WebM)
            {
                int crf = 15 + (int)((1.0f - q) * 35.0f);
                codec = "-c:v libvpx-vp9 -b:v 0 -crf " + std::to_string(crf) + " -deadline realtime -cpu-used 8 -row-mt 1 -pix_fmt yuv420p";
            }
            else
            {
                filters += ",split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer";
                codec = "";
            }

            std::string cmd = Quote(s.FFmpegExe) + " -y -hide_banner -loglevel error -f rawvideo -pix_fmt rgba -s "
                + std::to_string(s.Width) + "x" + std::to_string(s.Height) + " -r " + std::to_string(cfg.FrameRate)
                + " -i - -vf " + Quote(filters) + " " + codec + " " + Quote(s.Output);
            s.FFmpeg = std::make_unique<ProcessRunner>();
            if (!s.FFmpeg->Start(cmd, dir))
            {
                s.LastError = "failed to start ffmpeg";
                s.FFmpeg.reset();
                return false;
            }
        }

        s.Elapsed = 0.0f;
        s.Accumulator = 1.0f / (float)cfg.FrameRate; // ilk kare hemen
        s.Frames = 0;
        // Çevrimdışı kayıt: oyun her karede tam bir video karesi kadar ilerler
        s.Fixed = cfg.FixedTimeStep;
        if (s.Fixed)
            TimeManager::SetCaptureDeltaTime(1.0f / (float)cfg.FrameRate);
        s.StopWorker = false;
        s.Queue.clear();

        // Ses: GIF'te yok; sabit adımlı kayıtta ses gerçek zamanda aktığından görüntüyle örtüşmez
        s.Audio = false;
        s.AudioFrames = 0;
        s.AudioRate = 0;
        if (cfg.RecordAudio && cfg.Format != RecordingFormat::GIF && !s.Fixed && AudioEngine::IsInitialized()
            && AudioEngine::Get().BeginCapture(s.AudioChannels, s.AudioRate))
        {
            s.AudioPath = cfg.Format == RecordingFormat::PNGSequence ? (std::filesystem::path(s.Output) / "audio.wav").string() : s.Output + ".audio.wav";
            s.AudioFile = std::fopen(s.AudioPath.c_str(), "wb");
            if (s.AudioFile)
            {
                WriteWavHeader(s.AudioFile, s.AudioChannels, s.AudioRate, 0);
                s.Audio = true;
            }
            else
                AudioEngine::Get().EndCapture();
        }

        s.Worker = std::thread(WorkerLoop);
        s.Recording = true;
        CQ_CORE_INFO("[Recorder] Recording to {0} ({1}x{2} @ {3} fps)", s.Output, s.Width, s.Height, cfg.FrameRate);
        return true;
    }

    void Recorder::Stop()
    {
        auto& s = S();
        if (!s.Recording)
            return;
        s.Recording = false;
        if (s.Fixed)
        {
            TimeManager::SetCaptureDeltaTime(0.0f);
            s.Fixed = false;
        }
        {
            std::lock_guard<std::mutex> lock(s.Mutex);
            s.StopWorker = true;
        }
        s.Cv.notify_all();
        if (s.Worker.joinable())
            s.Worker.join();

        // Sesi kapat (WAV boyutları), görüntü dosyası bitince birleştirilecek
        bool muxAudio = false;
        if (s.Audio)
        {
            DrainAudio();
            if (AudioEngine::IsInitialized())
                AudioEngine::Get().EndCapture();
            const uint32_t bytes = (uint32_t)std::min<uint64_t>(s.AudioFrames * s.AudioChannels * 2, 0xFFFFFFF0u);
            WriteWavHeader(s.AudioFile, s.AudioChannels, s.AudioRate, bytes);
            std::fclose(s.AudioFile);
            s.AudioFile = nullptr;
            s.Audio = false;
            muxAudio = s.Settings.Format != RecordingFormat::PNGSequence && s.AudioFrames > 0;
            if (s.AudioFrames == 0 && s.Settings.Format != RecordingFormat::PNGSequence)
            {
                std::error_code ec;
                std::filesystem::remove(s.AudioPath, ec);
            }
        }

        if (s.FFmpeg)
        {
            // stdin kapanınca ffmpeg dosyayı bitirir; kısa süre bekle
            s.FFmpeg->CloseInput();
            for (int i = 0; i < 1000 && s.FFmpeg->IsRunning(); ++i)
            {
                std::string out = s.FFmpeg->ReadAvailable();
                if (!out.empty())
                    CQ_CORE_WARN("[Recorder] ffmpeg: {0}", out);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (s.FFmpeg->IsRunning())
                s.FFmpeg->Terminate();
            else if (s.FFmpeg->GetExitCode() != 0)
                s.LastError = "ffmpeg exited with code " + std::to_string(s.FFmpeg->GetExitCode());
            s.FFmpeg.reset();
        }
        // Ses + görüntü tek dosyada (görüntü yeniden kodlanmaz)
        if (muxAudio && s.LastError.empty())
        {
            namespace fs = std::filesystem;
            const fs::path video(s.Output);
            const fs::path tmp = video.parent_path() / (video.stem().string() + ".mux" + video.extension().string());
            const std::string audioCodec = s.Settings.Format == RecordingFormat::WebM ? "-c:a libopus -b:a 160k" : "-c:a aac -b:a 192k";
            const std::string cmd = Quote(s.FFmpegExe) + " -y -hide_banner -loglevel error -i " + Quote(s.Output) + " -i " + Quote(s.AudioPath)
                + " -map 0:v:0 -map 1:a:0 -c:v copy " + audioCodec + " -shortest " + Quote(tmp.string());
            ProcessRunner mux;
            bool ok = false;
            if (mux.Start(cmd, video.parent_path()))
            {
                for (int i = 0; i < 6000 && mux.IsRunning(); ++i)
                {
                    std::string out = mux.ReadAvailable();
                    if (!out.empty())
                        CQ_CORE_WARN("[Recorder] ffmpeg (audio): {0}", out);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                ok = !mux.IsRunning() && mux.GetExitCode() == 0;
            }
            std::error_code ec;
            if (ok && fs::exists(tmp, ec) && fs::file_size(tmp, ec) > 0)
            {
                fs::rename(tmp, video, ec);
                fs::remove(s.AudioPath, ec);
            }
            else
            {
                fs::remove(tmp, ec);
                s.LastError = "could not add the sound to the video (kept next to it: " + fs::path(s.AudioPath).filename().string() + ")";
            }
        }
        s.LastOutput = s.Output;
        CQ_CORE_INFO("[Recorder] Saved {0} frames ({1:.1f}s) to {2}", s.Frames, s.Elapsed, s.Output);
    }

    void Recorder::SubmitFrame(uint32_t textureID, uint32_t width, uint32_t height, float deltaSeconds)
    {
        auto& s = S();
        if (!s.Recording || textureID == 0)
            return;
        if (width != s.SourceWidth || height != s.SourceHeight)
        {
            // Görünüm boyutu değişti: kaydı güvenle bitir
            CQ_CORE_WARN("[Recorder] view resized during recording, stopping");
            Stop();
            return;
        }

        const float frameTime = 1.0f / (float)s.Settings.FrameRate;
        // Sabit adımda her kare tam bir video karesidir (kopya / atlama yok)
        if (s.Fixed)
            deltaSeconds = frameTime;
        s.Elapsed += deltaSeconds;
        s.Accumulator += deltaSeconds;
        if (s.Fixed)
            s.Accumulator = frameTime;
        if (s.Accumulator < frameTime)
            return;

        // Dokuyu bir kez oku; kare hızını korumak için gerekirse kopyala
        size_t bytes = (size_t)s.SourceWidth * s.SourceHeight * 4;
        s.Scratch.resize(bytes);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTextureImage(textureID, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)bytes, s.Scratch.data());

        // Kırp (çift boyut) + alfa'yı opak yap
        std::vector<uint8_t> pixels((size_t)s.Width * s.Height * 4);
        for (uint32_t y = 0; y < s.Height; ++y)
            memcpy(&pixels[(size_t)y * s.Width * 4], &s.Scratch[(size_t)y * s.SourceWidth * 4], (size_t)s.Width * 4);
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;

        int copies = 0;
        while (s.Accumulator >= frameTime && copies < 4)
        {
            s.Accumulator -= frameTime;
            ++copies;
        }
        if (s.Accumulator > frameTime) // çok geride: biriken süreyi bırak
            s.Accumulator = 0.0f;

        for (int c = 0; c < copies; ++c)
        {
            std::unique_lock<std::mutex> lock(s.Mutex);
            // Kuyruk doluysa kodlayıcıyı bekle (bellek patlamasın)
            s.Cv.wait(lock, [&] { return s.Queue.size() < 12; });
            Frame f;
            f.Index = s.Frames++;
            f.Pixels = (c + 1 == copies) ? std::move(pixels) : pixels;
            s.Queue.push_back(std::move(f));
            lock.unlock();
            s.Cv.notify_all();
        }

        DrainAudio();
        if (s.Settings.MaxDuration > 0.0f && s.Elapsed >= s.Settings.MaxDuration)
            Stop();
    }
}
