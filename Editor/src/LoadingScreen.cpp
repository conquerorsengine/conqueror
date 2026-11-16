#include "LoadingScreen.h"

#include "Core/Application.h"
#include "Core/Base/Version.h"
#include "Core/ImGui/ImGuiLayer.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/LoadingProgress.h"
#include "Renderer/RHI/RendererAPI.h"
#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <memory>

namespace Conqueror::Editor::LoadingScreen
{
    namespace fs = std::filesystem;

    namespace
    {
        struct State
        {
            bool Active = false;
            std::string Project;
            std::string Step = "Opening project";
            std::string Detail;
            float Fraction = 0.0f;
            std::shared_ptr<Texture2D> Art;      // projenin küçük resmi
            std::shared_ptr<Texture2D> Emblem;   // küçük resim yoksa
            std::shared_ptr<Texture2D> Logo;     // beyaz yazılı logo
            std::chrono::steady_clock::time_point LastDraw{};
            int Draws = 0;
            // Ayrı açılış penceresi (Unity gibi): başlık çubuğu ve düğmeleri yok; editörün ana penceresi yükleme boyunca gizli
            GLFWwindow* Splash = nullptr;
            bool Transparent = false;   // köşeler yuvarlak + gölge; Wayland'de pencere ekranı kaplar, yalnız kart görünür
            bool FullArea = false;      // Wayland: istemci pencereyi konumlandıramaz, kart ekranın ortasında kalsın diye
            bool Decorated = false;
            bool MainVisibleWhileLoading = false;
            int SplashW = 0, SplashH = 0;
        };

        constexpr float kCardW = 760.0f;
        constexpr float kShadow = 24.0f;   // saydam pencerede kartın çevresindeki gölge payı

        float BaseFontSize()
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float fs = style.FontSizeBase > 0.0f ? style.FontSizeBase : 0.0f;
            if (fs <= 0.0f && ImGui::GetIO().FontDefault)
                fs = ImGui::GetIO().FontDefault->LegacySize;
            if (fs <= 0.0f && !ImGui::GetIO().Fonts->Fonts.empty())
                fs = ImGui::GetIO().Fonts->Fonts[0]->LegacySize;
            return (fs > 0.0f ? fs : 16.0f) * (style.FontScaleMain > 0.0f ? style.FontScaleMain : 1.0f);
        }

        float CardArtH(float cardW) { return std::round(cardW * 9.0f / 16.0f * 0.92f); }
        float CardH(float cardW, float fs) { return CardArtH(cardW) + std::round(fs * 6.2f); }

        State& S()
        {
            static auto& s = *new State();
            return s;
        }

        std::shared_ptr<Texture2D> TryLoad(const fs::path& path)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
                return nullptr;
            auto tex = Texture2D::Create(path.string());
            return tex && tex->GetRendererID() && tex->GetWidth() > 1 ? tex : nullptr;
        }

        // Görüntüyü kutuya oranı bozulmadan doldurur (taşan kenar kırpılır)
        void ImageCover(ImDrawList* dl, const std::shared_ptr<Texture2D>& tex, ImVec2 a, ImVec2 b, float rounding, ImDrawFlags flags)
        {
            const float bw = b.x - a.x, bh = b.y - a.y;
            const float tw = (float)tex->GetWidth(), th = (float)tex->GetHeight();
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            if (tw / th > bw / bh)
            {
                const float keep = (bw / bh) / (tw / th);
                u0 = (1.0f - keep) * 0.5f;
                u1 = 1.0f - u0;
            }
            else
            {
                const float keep = (tw / th) / (bw / bh);
                v0 = (1.0f - keep) * 0.5f;
                v1 = 1.0f - v0;
            }
            // Doku satırları alttan üste (OpenGL): v ters
            dl->AddImageRounded((ImTextureID)(uint64_t)tex->GetRendererID(), a, b, ImVec2(u0, 1.0f - v0), ImVec2(u1, 1.0f - v1), IM_COL32_WHITE,
                                rounding, flags);
        }

        std::string Ellipsize(const std::string& text, float maxWidth, ImFont* font, float size)
        {
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
                return text;
            // Baştan kısalt: dosya yolunun sonu (dosya adı) görünür kalsın
            for (size_t cut = 1; cut < text.size(); ++cut)
            {
                const std::string t = "..." + text.substr(cut);
                if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str()).x <= maxWidth)
                    return t;
            }
            return "...";
        }

        // Açılış penceresi: ana pencereyle paylaşımlı OpenGL bağlamı (dokular, ImGui font atlası ortak). Başlık çubuğu yok
        // (çarpı / küçült / büyüt düğmesi de yok). Wayland'de istemci pencereyi ortalayamadığı için pencere çalışma alanını kaplar,
        // tamamen saydamdır ve tıklamaları alttaki pencerelere geçirir: ekranda yalnız ortadaki kart görünür. Diğer sistemlerde
        // kart boyunda, ekranın ortasında, her zaman üstte
        void CreateSplash()
        {
            State& s = S();
            if (RendererAPI::GetBackend() != RendererAPI::Backend::OpenGL)
                return;
            GLFWwindow* main = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            bool wayland = false;
#if defined(GLFW_PLATFORM_WAYLAND)
            wayland = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#endif
            const float fs = BaseFontSize();
            const int cardW = (int)kCardW, cardH = (int)CardH(kCardW, fs);
            int wx = 0, wy = 0, ww = 0, wh = 0;
            if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
                glfwGetMonitorWorkarea(mon, &wx, &wy, &ww, &wh);
            int w = cardW + 2 * (int)kShadow, h = cardH + 2 * (int)kShadow;
            if (wayland && ww > 0 && wh > 0)
            {
                w = ww;
                h = wh;
            }
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
            glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
            glfwWindowHint(GLFW_MAXIMIZED, wayland ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, wayland ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_FLOATING, wayland ? GLFW_FALSE : GLFW_TRUE);
            s.Splash = glfwCreateWindow(w, h, "Conqueror's Engine", nullptr, main);
            if (!s.Splash)
            {
                glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
                glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
                glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
                s.Splash = glfwCreateWindow(cardW, cardH, "Conqueror's Engine", nullptr, main);
            }
            // İpuçları varsayılana: sonraki pencereler (dışarı sürüklenen paneller) bunları devralmasın
            glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
            glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
            glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
            glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
            if (!s.Splash)
            {
                CQ_CORE_WARN("[Loading] Could not open the splash window; the editor opens without it");
                return;
            }
            s.Transparent = glfwGetWindowAttrib(s.Splash, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
            s.FullArea = wayland && s.Transparent;
            if (!s.Transparent)
            {
                // Saydamlık yok (bileştiricisiz X11): kart boyunda düz pencere; Wayland'de büyütme de kaldırılır
                glfwRestoreWindow(s.Splash);
                glfwSetWindowSize(s.Splash, cardW, cardH);
                w = cardW;
                h = cardH;
            }
            if (!wayland && ww > 0 && wh > 0)
                glfwSetWindowPos(s.Splash, wx + (ww - w) / 2, wy + (wh - h) / 2);
            glfwShowWindow(s.Splash);
            GLFWwindow* previous = glfwGetCurrentContext();
            glfwMakeContextCurrent(s.Splash);
            glfwSwapInterval(0);   // yükleme vsync beklemesin
            glfwMakeContextCurrent(previous);
            s.Decorated = glfwGetWindowAttrib(s.Splash, GLFW_DECORATED) == GLFW_TRUE;
            glfwGetWindowSize(s.Splash, &s.SplashW, &s.SplashH);
            CQ_CORE_INFO("[Loading] Splash window {0}x{1} ({2})", s.SplashW, s.SplashH,
                         s.FullArea ? "transparent, covers the work area" : s.Transparent ? "transparent" : "opaque");
        }

        void Draw()
        {
            State& s = S();
            if (!s.Splash)
                return;
            Application& app = Application::Get();
            glfwPollEvents();
            int ww = 0, wh = 0, fbw = 0, fbh = 0;
            glfwGetWindowSize(s.Splash, &ww, &wh);
            glfwGetFramebufferSize(s.Splash, &fbw, &fbh);
            if (ww <= 0 || wh <= 0 || fbw <= 0 || fbh <= 0)
                return;
            s.SplashW = ww;
            s.SplashH = wh;
            ImGuiLayer::OverrideNextDisplaySize((float)ww, (float)wh, (float)fbw / (float)ww, (float)fbh / (float)wh);
            ImGuiLayer* imgui = app.GetImGuiLayer();
            imgui->Begin();
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
            ImFont* regular = ImGui::GetFont();
            ImFont* bold = ImGuiLayer::GetFont(ImGuiLayer::FontRole::Bold);
            if (!bold)
                bold = regular;
            const float fs = ImGui::GetFontSize();

            // Kart: 16:9 resim + alt şerit (Unity'nin açılış penceresi oranında). Kart boyundaki pencerede pencereyi (gölge payı
            // dışında) doldurur; Wayland'deki ekranı kaplayan saydam pencerede ortadadır
            const float margin = s.FullArea ? 0.0f : (s.Transparent ? kShadow : 0.0f);
            const float cardW = s.FullArea ? std::min(kCardW, vp->Size.x - 80.0f) : vp->Size.x - 2.0f * margin;
            const float artH = CardArtH(cardW);
            const float cardH = s.FullArea ? artH + std::round(fs * 6.2f) : vp->Size.y - 2.0f * margin;
            const float rounding = s.Transparent ? 10.0f : 0.0f;
            const ImVec2 a(std::round(vp->Pos.x + (vp->Size.x - cardW) * 0.5f), std::round(vp->Pos.y + (vp->Size.y - cardH) * 0.5f));
            const ImVec2 b(a.x + cardW, a.y + cardH);
            if (s.Transparent)
                for (int i = 1; i <= 6; ++i)   // yumuşak gölge
                    dl->AddRectFilled(ImVec2(a.x - i * 2.0f, a.y - i * 2.0f + 6.0f), ImVec2(b.x + i * 2.0f, b.y + i * 2.0f + 6.0f),
                                      IM_COL32(0, 0, 0, 22), rounding + i * 2.0f);
            dl->AddRectFilled(a, b, IM_COL32(22, 24, 29, 255), rounding);

            // Resim alanı
            const ImVec2 artB(b.x, a.y + artH);
            if (s.Art)
                ImageCover(dl, s.Art, a, artB, rounding, ImDrawFlags_RoundCornersTop);
            else
            {
            {
                // Dört köşeli degrade; yuvarlak üst köşeler için en üstteki ince şerit yuvarlatılmış ve yatay degradeyle boyanır
                const ImU32 tl = IM_COL32(38, 60, 118, 255), tr = IM_COL32(58, 42, 112, 255);
                dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + rounding), artB, tl, tr, IM_COL32(18, 20, 32, 255), IM_COL32(16, 22, 40, 255));
                if (rounding > 0.0f)
                {
                    const int v0 = dl->VtxBuffer.Size;
                    dl->AddRectFilled(a, ImVec2(b.x, a.y + rounding + 2.0f), IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersTop);
                    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(b.x, a.y), tl, tr);
                }
            }
                if (s.Emblem)
                {
                    const float e = artH * 0.42f;
                    const ImVec2 c((a.x + b.x) * 0.5f, a.y + artH * 0.5f);
                    dl->AddImage((ImTextureID)(uint64_t)s.Emblem->GetRendererID(), ImVec2(c.x - e * 0.5f, c.y - e * 0.5f), ImVec2(c.x + e * 0.5f, c.y + e * 0.5f),
                                 ImVec2(0, 1), ImVec2(1, 0), IM_COL32(255, 255, 255, 230));
                }
            }

            // İlerleme çubuğu (resmin hemen altında, tam genişlik)
            const float barH = 5.0f;
            dl->AddRectFilled(ImVec2(a.x, artB.y), ImVec2(b.x, artB.y + barH), IM_COL32(48, 52, 60, 255));
            const float f = std::clamp(s.Fraction, 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(a.x, artB.y), ImVec2(a.x + cardW * f, artB.y + barH), IM_COL32(64, 156, 255, 255));

            // Aşama + ayrıntı (sağa yaslı, Unity'deki gibi) ve proje adı
            const float pad = fs * 1.2f;
            const std::string status = s.Detail.empty() ? s.Step : s.Step + " - " + s.Detail;
            const std::string shown = Ellipsize(status, cardW * 0.62f, regular, fs * 0.92f);
            const ImVec2 st = regular->CalcTextSizeA(fs * 0.92f, FLT_MAX, 0.0f, shown.c_str());
            float y = artB.y + barH + fs * 0.9f;
            dl->AddText(regular, fs * 0.92f, ImVec2(b.x - pad - st.x, y), IM_COL32(150, 155, 165, 255), shown.c_str());
            y += fs * 1.35f;
            const ImVec2 pt = bold->CalcTextSizeA(fs * 1.15f, FLT_MAX, 0.0f, s.Project.c_str());
            dl->AddText(bold, fs * 1.15f, ImVec2(b.x - pad - pt.x, y), IM_COL32(236, 238, 242, 255), s.Project.c_str());

            // Logo (sol alt) ve sürüm (sağ alt)
            const float logoH = fs * 2.3f;
            if (s.Logo)
            {
                const float logoW = logoH * (float)s.Logo->GetWidth() / (float)s.Logo->GetHeight();
                const ImVec2 la(a.x + pad, b.y - pad - logoH);
                dl->AddImage((ImTextureID)(uint64_t)s.Logo->GetRendererID(), la, ImVec2(la.x + logoW, la.y + logoH), ImVec2(0, 1), ImVec2(1, 0));
            }
            const char* version = CQ_ENGINE_VERSION;
            const ImVec2 vt = bold->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0.0f, version);
            dl->AddText(bold, fs * 1.05f, ImVec2(b.x - pad - vt.x, b.y - pad - vt.y), IM_COL32(236, 238, 242, 255), version);

            // Saydam pencerede kartın dışı tamamen saydam; değilse kartın rengi
            if (s.Transparent)
                imgui->EndInto(s.Splash, 0.0f, 0.0f, 0.0f, 0.0f);
            else
                imgui->EndInto(s.Splash, 22.0f / 255.0f, 24.0f / 255.0f, 29.0f / 255.0f, 1.0f);
            s.LastDraw = std::chrono::steady_clock::now();
            ++s.Draws;
        }
    }

    void Begin(const std::string& projectName, const fs::path& projectDir)
    {
        State& s = S();
        s.Active = true;
        s.Project = projectName;
        s.Step = "Opening project";
        s.Detail.clear();
        s.Fraction = 0.0f;
        s.Draws = 0;
        GLFWwindow* main = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        CreateSplash();
        if (!s.Splash)
            glfwShowWindow(main);   // açılış penceresi yoksa editör penceresi yükleme boyunca görünür kalsın
        s.MainVisibleWhileLoading = glfwGetWindowAttrib(main, GLFW_VISIBLE) == GLFW_TRUE;
        // Projenin Hub'daki küçük resmi; yoksa amblem. Logo motorla gelir
        s.Art = TryLoad(projectDir / "Library" / "thumbnail.png");
        if (!s.Art)
            s.Emblem = TryLoad(fs::current_path() / "Resources" / "Logo" / "AppIcon" / "conqueror_256.png");
        s.Logo = TryLoad(fs::current_path() / "Resources" / "Logo" / "Conqueror_Logo.png");
        LoadingProgress::SetHandler([](const std::string& step, const std::string& detail, float fraction) {
            State& st = S();
            if (!step.empty())
                st.Step = step;
            st.Detail = detail;
            if (fraction >= 0.0f)
                st.Fraction = std::max(st.Fraction, fraction);
            // En çok saniyede ~30 çizim (yüklemeyi yavaşlatmaz); bitiş ve ilk çizim her zaman
            const auto now = std::chrono::steady_clock::now();
            if (st.Draws > 1 && fraction < 1.0f && now - st.LastDraw < std::chrono::milliseconds(33))
                return;
            Draw();
        });
        Draw();
        CQ_CORE_INFO("[Loading] Opening '{0}'", projectName);
    }

    void End()
    {
        State& s = S();
        if (!s.Active)
            return;
        // Son kare: editörün ilk oturmuş karesi gelene kadar ekranda kalır (o karelere kadar hiçbir şey gösterilmez)
        s.Fraction = 1.0f;
        s.Step = "Starting editor";
        s.Detail.clear();
        Draw();
        LoadingProgress::SetHandler(nullptr);
        s.Active = false;
        GLFWwindow* main = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        s.MainVisibleWhileLoading = s.MainVisibleWhileLoading || glfwGetWindowAttrib(main, GLFW_VISIBLE) == GLFW_TRUE;
        CQ_CORE_INFO("[Loading] Done ({0} frames drawn)", s.Draws);
        s.Art.reset();
        s.Emblem.reset();
        s.Logo.reset();
    }

    void Finish()
    {
        State& s = S();
        if (!s.Splash)
            return;
        GLFWwindow* main = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (glfwGetCurrentContext() == s.Splash)
            glfwMakeContextCurrent(main);
        glfwDestroyWindow(s.Splash);
        s.Splash = nullptr;
        CQ_CORE_INFO("[Loading] Splash window closed");
    }

    bool IsActive() { return S().Active; }
    bool HadSplashWindow() { return S().SplashW > 0; }
    bool SplashDecorated() { return S().Decorated; }
    bool SplashOpen() { return S().Splash != nullptr; }
    bool MainVisibleWhileLoading() { return S().MainVisibleWhileLoading; }
    int SplashWidth() { return S().SplashW; }
    int SplashHeight() { return S().SplashH; }
    int DrawCount() { return S().Draws; }
    float LastFraction() { return S().Fraction; }
}
