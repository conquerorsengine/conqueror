#include "UpscalerSelfTest.h"

#include "AntiAliasing.h"
#include "Core/Application.h"
#include "Core/Base/Version.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Logging/Log.h"
#include "RHI/RendererAPI.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

namespace Conqueror::UpscalerSelfTest
{
    namespace
    {
        // Her yöntem kWarmup kare çalışır (zamansal yöntemlerin birikimi oturur, gölgelendiriciler derlenir); kare süresi son
        // kMeasure karenin ortalaması (sahne dahil bütün kare)
        constexpr int kWarmup = 30;
        constexpr int kMeasure = 10;
        constexpr int kGridW = 64, kGridH = 36;   // görüntü karşılaştırması için blok ortalamaları

        struct Step
        {
            bool Upscaler = false;
            int Mode = 0;          // 0 + !Upscaler: temel ölçüm (kenar yumuşatma yok, yerel çözünürlük)
            std::string Name;
        };

        struct State
        {
            bool Running = false;
            bool Quit = false;
            std::filesystem::path Report;
            std::vector<Step> Steps;
            size_t Index = 0;
            int Frame = 0;
            double LastTime = 0.0, TimeSum = 0.0;
            int TimeCount = 0;
            int GLErrors = 0;
            std::vector<float> Baseline;
            float BaseMean = 0.0f, BaseStd = 0.0f;
            bool HaveBaseline = false;
            int Width = 0, Height = 0;
            std::vector<std::string> Lines;
            int Pass = 0, Fail = 0, Fallback = 0, Unavailable = 0;
            RenderOverrides Saved;
            bool SavedVSync = true;
            std::string Text;
            std::filesystem::path LastPath;
            ImageReader Reader;
        };

        State& S()
        {
            static State s;
            return s;
        }

        // Oyun: arka arabellek (arayüz çizilmeden önce)
        bool ReadDefaultFramebuffer(std::vector<uint8_t>& rgba, int& width, int& height)
        {
            auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            if (!window)
                return false;
            glfwGetFramebufferSize(window, &width, &height);
            if (width <= 0 || height <= 0)
                return false;
            rgba.resize((size_t)width * (size_t)height * 4);
            GLint prevRead = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
            return true;
        }

        bool Capture(std::vector<float>& grid, float& mean, float& stddev)
        {
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            const bool ok = S().Reader ? S().Reader(px, w, h) : ReadDefaultFramebuffer(px, w, h);
            if (!ok || w <= 0 || h <= 0 || px.size() < (size_t)w * (size_t)h * 4)
                return false;
            S().Width = w;
            S().Height = h;
            grid.assign((size_t)kGridW * kGridH * 3, 0.0f);
            std::vector<int> counts((size_t)kGridW * kGridH, 0);
            double sum = 0.0, sum2 = 0.0;
            size_t n = 0;
            for (int y = 0; y < h; y += 2)
                for (int x = 0; x < w; x += 2)
                {
                    const uint8_t* p = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
                    const int cell = (y * kGridH / h) * kGridW + (x * kGridW / w);
                    for (int c = 0; c < 3; ++c)
                        grid[(size_t)cell * 3 + c] += p[c];
                    ++counts[(size_t)cell];
                    const double lum = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                    sum += lum;
                    sum2 += lum * lum;
                    ++n;
                }
            for (size_t i = 0; i < counts.size(); ++i)
                for (int c = 0; c < 3; ++c)
                    grid[i * 3 + c] /= (float)std::max(1, counts[i]);
            mean = n ? (float)(sum / (double)n) : 0.0f;
            stddev = n ? (float)std::sqrt(std::max(0.0, sum2 / (double)n - (sum / (double)n) * (sum / (double)n))) : 0.0f;
            return true;
        }

        void Apply(const Step& step)
        {
            auto& o = ConsoleSystem::Overrides();
            if (step.Upscaler)
            {
                // Yükseltici: TAA ile, Quality kipinin iç çözünürlüğünde (%67)
                o.AntiAliasing = (int)AntiAliasingMode::TAA;
                o.Upscaler = step.Mode;
                o.ScreenPercentage = 67.0f;
            }
            else
            {
                o.AntiAliasing = step.Mode;
                o.Upscaler = (int)UpscalerMode::None;
                o.ScreenPercentage = NAN;
            }
            S().Frame = 0;
            S().TimeSum = 0.0;
            S().TimeCount = 0;
            S().GLErrors = 0;
        }

        void AddLine(const char* status, const Step& step, double ms, float diff, const std::string& detail)
        {
            char buf[256];
            const char* kind = step.Upscaler ? "Upscaler" : (step.Mode == 0 ? "Baseline" : "AA");
            if (ms > 0.0)
                std::snprintf(buf, sizeof(buf), "%-11s %-9s %-10s %7.2f ms  diff %5.1f", status, kind, step.Name.c_str(), ms, diff);
            else
                std::snprintf(buf, sizeof(buf), "%-11s %-9s %-10s", status, kind, step.Name.c_str());
            S().Lines.push_back(std::string(buf) + (detail.empty() ? "" : "  " + detail));
        }

        const char* OSName()
        {
#if defined(__EMSCRIPTEN__)
            return "Web";
#elif defined(__ANDROID__)
            return "Android";
#elif defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#elif defined(__FreeBSD__)
            return "FreeBSD";
#else
            return "other";
#endif
        }

        void Finish()
        {
            State& s = S();
            s.Running = false;
            ConsoleSystem::Overrides() = s.Saved;
            Application::Get().GetWindow().SetVSync(s.SavedVSync);

            const auto& real = AntiAliasing::GetRealCaps();
            const auto& eff = AntiAliasing::GetCaps();
            std::ostringstream out;
            char date[64] = {};
            const std::time_t now = std::time(nullptr);
            std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", std::localtime(&now));
            const char* glVersion = (const char*)glGetString(GL_VERSION);
            out << "Conqueror Engine " << CQ_ENGINE_VERSION << " - Upscaler Self-Test\n";
            out << "Date: " << date << "\n";
            out << "GPU: " << real.Vendor << " | " << real.Renderer << "\n";
            if (!eff.Simulated.empty())
                out << "Simulated GPU: " << eff.Simulated << " (" << eff.Renderer << "): menus and rules follow it, rendering uses the GPU above\n";
            out << "Graphics API: " << RendererAPI::GetBackendName() << " (" << (glVersion ? glVersion : "?") << ")\n";
            out << "OS: " << OSName() << "\n";
            out << "Image: " << s.Width << " x " << s.Height << "\n";
            out << "Capabilities: RTX " << (eff.NVIDIARTX ? "yes" : "no") << ", NGX runtime " << (eff.NGXRuntime ? "yes" : "no") << ", XeSS runtime "
                << (eff.XeSSRuntime ? "yes" : "no") << ", compute " << (eff.Compute ? "yes" : "no") << ", multisample textures "
                << (eff.MultisampleTextures ? "yes" : "no") << ", max samples " << eff.MaxSamples << "\n";
            out << "Each method runs " << kWarmup << " frames with VSync off; ms = average whole frame time (scene included) of the last " << kMeasure
                << " frames; diff = mean difference from the native image without anti-aliasing (0-255). Upscalers run with TAA at 67 %.\n\n";
            for (const auto& line : s.Lines)
                out << line << "\n";
            out << "\nResult: " << s.Pass << " passed, " << s.Fail << " failed, " << s.Fallback << " fell back to another method, " << s.Unavailable
                << " not available on this system\n";
            s.Text = out.str();
            std::error_code ec;
            if (!s.Report.parent_path().empty())
                std::filesystem::create_directories(s.Report.parent_path(), ec);
            std::ofstream(s.Report) << s.Text;
            s.LastPath = std::filesystem::absolute(s.Report, ec);
            CQ_CORE_INFO("Upscaler self-test finished: {0} passed, {1} failed, {2} fell back, {3} not available; report: {4}", s.Pass, s.Fail, s.Fallback,
                         s.Unavailable, s.LastPath.string());
            if (s.Quit)
                Application::Get().Close();
        }

        // Sıradaki çalıştırılabilir adıma geç: kullanılamayanlar nedeniyle rapora yazılır, çalıştırılmaz
        void Advance()
        {
            State& s = S();
            for (++s.Index; s.Index < s.Steps.size(); ++s.Index)
            {
                const Step& st = s.Steps[s.Index];
                const AntiAliasing::Availability av = st.Upscaler ? AntiAliasing::GetUpscalerAvailability((UpscalerMode)st.Mode)
                                                                  : AntiAliasing::GetAvailability((AntiAliasingMode)st.Mode);
                if (av.Available)
                {
                    Apply(st);
                    return;
                }
                AddLine("UNAVAILABLE", st, 0.0, 0.0f, av.Reason);
                ++s.Unavailable;
            }
            Finish();
        }

        void Evaluate()
        {
            State& s = S();
            const Step& st = s.Steps[s.Index];
            const double ms = s.TimeCount ? s.TimeSum / s.TimeCount * 1000.0 : 0.0;
            std::string resolved;
            bool fellBack = false;
            if (st.Upscaler)
            {
                const UpscalerMode r = AntiAliasing::ResolveUpscalerForThisSystem((UpscalerMode)st.Mode);
                fellBack = (int)r != st.Mode;
                resolved = AntiAliasing::GetUpscalerInfo(r).Name;
            }
            else if (st.Mode != 0)
            {
                const AntiAliasingMode r = AntiAliasing::ResolveForThisSystem((AntiAliasingMode)st.Mode);
                fellBack = (int)r != st.Mode;
                resolved = AntiAliasing::GetInfo(r).Name;
            }

            std::vector<float> grid;
            float mean = 0.0f, stddev = 0.0f;
            if (!Capture(grid, mean, stddev))
            {
                AddLine("FAIL", st, ms, 0.0f, "the image could not be read back");
                ++s.Fail;
                return;
            }
            if (st.Mode == 0 && !st.Upscaler)
            {
                s.Baseline = grid;
                s.BaseMean = mean;
                s.BaseStd = stddev;
                s.HaveBaseline = true;
                AddLine("BASE", st, ms, 0.0f, "native resolution, no anti-aliasing (reference image)");
                return;
            }
            float diff = 0.0f;
            if (s.HaveBaseline && s.Baseline.size() == grid.size())
            {
                double d = 0.0;
                for (size_t i = 0; i < grid.size(); ++i)
                    d += std::abs(grid[i] - s.Baseline[i]);
                diff = (float)(d / (double)grid.size());
            }
            std::string problem;
            if (s.HaveBaseline && mean < 2.0f && s.BaseMean > 10.0f)
                problem = "black image";
            else if (s.HaveBaseline && stddev < 1.0f && s.BaseStd > 5.0f)
                problem = "flat single-color image";
            else if (diff > 60.0f)
                problem = "image differs strongly from the native image (corrupted or wrong scale)";
            if (s.GLErrors > 0)
                problem += (problem.empty() ? "" : "; ") + std::to_string(s.GLErrors) + " OpenGL errors";
            if (!problem.empty())
            {
                AddLine("FAIL", st, ms, diff, problem + (fellBack ? " (rendered as " + resolved + ")" : ""));
                ++s.Fail;
            }
            else if (fellBack)
            {
                AddLine("FALLBACK", st, ms, diff, "rendered as " + resolved);
                ++s.Fallback;
            }
            else
            {
                AddLine("PASS", st, ms, diff, "");
                ++s.Pass;
            }
        }
    }

    void SetImageReader(ImageReader reader) { S().Reader = std::move(reader); }

    bool Start(const std::filesystem::path& report, bool quitWhenDone)
    {
        State& s = S();
        if (s.Running)
            return false;
        s.Report = report.empty() ? std::filesystem::path("UpscalerSelfTest.txt") : report;
        s.Quit = quitWhenDone;
        s.Steps.clear();
        s.Lines.clear();
        s.Pass = s.Fail = s.Fallback = s.Unavailable = 0;
        s.HaveBaseline = false;
        s.Steps.push_back({ false, 0, "Native" });
        for (int i = 1; i < AntiAliasing::GetModeCount(); ++i)
        {
            const auto& info = AntiAliasing::GetInfo((AntiAliasingMode)i);
            if (!AntiAliasing::IsLegacyUpscaler(info.Mode))
                s.Steps.push_back({ false, i, info.Name });
        }
        for (int i = 1; i < AntiAliasing::GetUpscalerCount(); ++i)
            s.Steps.push_back({ true, i, AntiAliasing::GetUpscalerInfo((UpscalerMode)i).Name });
        s.Saved = ConsoleSystem::Overrides();
        // Kare süreleri yöntemin maliyetini göstersin: test boyunca dikey eşitleme kapalı
        s.SavedVSync = Application::Get().GetWindow().IsVSync();
        Application::Get().GetWindow().SetVSync(false);
        s.Index = 0;
        s.Running = true;
        s.LastTime = glfwGetTime();
        Apply(s.Steps[0]);
        CQ_CORE_INFO("Upscaler self-test started: {0} methods, report: {1}", s.Steps.size() - 1, s.Report.string());
        return true;
    }

    void Cancel()
    {
        State& s = S();
        if (!s.Running)
            return;
        s.Running = false;
        ConsoleSystem::Overrides() = s.Saved;
        Application::Get().GetWindow().SetVSync(s.SavedVSync);
        CQ_CORE_INFO("Upscaler self-test cancelled");
    }

    bool IsRunning() { return S().Running; }

    void OnFrameEnd()
    {
        State& s = S();
        if (!s.Running)
            return;
        const double now = glfwGetTime();
        const double dt = now - s.LastTime;
        s.LastTime = now;
        if (s.Frame >= kWarmup - kMeasure)
        {
            s.TimeSum += dt;
            ++s.TimeCount;
        }
        // Yöntem değişiminin ilk karelerindeki hedef yeniden oluşturma dışında GL hatası sayılır
        for (int guard = 0; guard < 64; ++guard)
        {
            const GLenum e = glGetError();
            if (e == GL_NO_ERROR)
                break;
            if (s.Frame > 2)
                ++s.GLErrors;
        }
        if (++s.Frame < kWarmup)
            return;
        Evaluate();
        Advance();
    }

    std::string GetStatus()
    {
        const State& s = S();
        if (!s.Running || s.Index >= s.Steps.size())
            return {};
        const Step& st = s.Steps[s.Index];
        return std::string(st.Upscaler ? "Upscaler " : "AA ") + std::to_string(s.Index) + " / " + std::to_string(s.Steps.size() - 1) + ": " + st.Name;
    }

    const std::string& GetLastReport() { return S().Text; }
    const std::filesystem::path& GetLastReportPath() { return S().LastPath; }
}
