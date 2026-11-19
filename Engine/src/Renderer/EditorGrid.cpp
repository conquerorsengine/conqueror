#include "EditorGrid.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Renderer.h"
#include "Renderer/PostProcess.h"

#include <glad/glad.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        std::shared_ptr<Shader> s_GridShader;
        uint32_t s_EmptyVAO = 0;
        bool s_LoadFailed = false;

        // Son görüntünün üstüne çizilmek üzere ertelenen 3D ızgara (DeferToComposite -> RenderDeferred, aynı görünümün aynı karesinde)
        struct DeferredGrid
        {
            bool Pending = false;
            RenderCamera Camera;            // titreşimsiz
            glm::vec2 JitterNDC{ 0.0f };    // bu karenin alt piksel titreşimi (sahne çözünürlüğünde, NDC)
        };
        DeferredGrid s_Deferred;
        uint32_t s_BelowTexture = 0;        // son görüntünün kopyası (ızgara doğrusal uzayda onun üstüne karıştırılır)
        uint32_t s_BelowWidth = 0, s_BelowHeight = 0;

        // Görünüm başına (son görüntünün dokusuyla anahtarlı) birikmiş örtme payı: iki doku, karede bir yer değiştirir
        struct CoverageHistory
        {
            uint32_t Texture[2] = { 0, 0 };
            uint32_t Framebuffer[2] = { 0, 0 };   // 0. ek: son görüntü, 1. ek: Texture[i]
            uint32_t Width = 0, Height = 0;
            int Write = 0;
            bool Valid = false;
            glm::mat4 PrevViewProj{ 1.0f };
            uint64_t LastFrame = 0;
        };
        std::unordered_map<uint32_t, CoverageHistory> s_Coverage;
        uint64_t s_CompositeFrame = 0;

        void DestroyHistory(CoverageHistory& h)
        {
            glDeleteFramebuffers(2, h.Framebuffer);
            glDeleteTextures(2, h.Texture);
            h = CoverageHistory{};
        }

        bool EnsureShader()
        {
            if (s_LoadFailed)
                return false;
            if (!s_GridShader)
            {
                s_GridShader = Shader::Create("Engine/src/Shaders/Editor/Grid.cqsh");
                if (!s_GridShader || !s_GridShader->IsValid())
                {
                    s_LoadFailed = true;
                    return false;
                }
                glGenVertexArrays(1, &s_EmptyVAO);
            }
            return true;
        }

        // Ortak ayarlar (ızgaranın kendisi, LOD, renkler, derinlik payı). viewProj: ızgaranın çizildiği görüş-izdüşüm
        void SetCommonUniforms(const RenderCamera& camera, const glm::mat4& viewProj, bool plane2D, bool linearOutput, bool background)
        {
            const auto& settings = EditorGrid::Settings();
            s_GridShader->SetMat4("u_InvViewProjection", glm::inverse(viewProj));
            s_GridShader->SetMat4("u_ViewProjection", viewProj);
            s_GridShader->SetFloat3("u_CameraPos", camera.Position);
            s_GridShader->SetInt("u_Plane", plane2D ? 1 : 0);
            s_GridShader->SetFloat("u_BaseSize", glm::max(settings.BaseSize, 0.001f));
            s_GridShader->SetFloat("u_Opacity", settings.Opacity);
            s_GridShader->SetFloat("u_FadeDistance", settings.FadeDistance);
            s_GridShader->SetInt("u_LinearOutput", linearOutput ? 1 : 0);
            s_GridShader->SetInt("u_Orthographic", camera.Orthographic ? 1 : 0);
            s_GridShader->SetFloat3("u_MinorColor", settings.MinorColor);
            s_GridShader->SetFloat3("u_MajorColor", settings.MajorColor);
            s_GridShader->SetInt("u_Background", background ? 1 : 0);
            // Derinlik kuantalamasının mesafe hatası: perspektifte ~d² / (near · 2^22), ortografikte (far - near) / 2^22
            // (24 bit / float derinlik + enterpolasyon payı); öne alma payı bundan büyük tutulur
            const float steps = 4194304.0f;
            const float nearClip = glm::max(camera.NearClip, 1e-4f);
            s_GridShader->SetFloat("u_DepthErrorQuad", camera.Orthographic ? 0.0f : 1.0f / (nearClip * steps));
            s_GridShader->SetFloat("u_DepthErrorConst", camera.Orthographic ? glm::max(camera.FarClip - camera.NearClip, 0.0f) / steps : 0.0f);
        }
    }

    EditorGridSettings& EditorGrid::Settings()
    {
        static EditorGridSettings settings;
        return settings;
    }

    void EditorGrid::Render(const RenderCamera& camera, bool plane2D, bool linearOutput, bool background)
    {
        const auto& settings = Settings();
        if (!settings.Visible || !EnsureShader())
            return;

        // TAA: sahne bu karede alt piksel titretilmiş projeksiyonla çizildi; ızgara da aynısıyla çizilir. Aksi halde alçak
        // açılarda yarım piksellik fark zeminde santimlerce derinlik farkı olur, bazı karelerde ızgaranın tamamı zeminin
        // arkasında kalır (ızgara bir görünüp bir kaybolur)
        glm::mat4 projection = camera.Projection;
        glm::vec2 jitter;
        if (PostProcessStack::GetCurrentJitter(jitter))
            for (int c = 0; c < 4; ++c)
            {
                projection[c][0] += jitter.x * projection[c][3];
                projection[c][1] += jitter.y * projection[c][3];
            }
        const glm::mat4 viewProj = projection * camera.View;

        GLboolean blend = glIsEnabled(GL_BLEND);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

        GLint depthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);

        // Derinlik testi örnek başına (MSAA) yapılır: nesne kenarları temiz kalır. Zeminle eşit derinlik yarışını
        // gölgelendirici çözer (ızgara kameraya doğru çok küçük bir pay kadar öne alınır)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        s_GridShader->Bind();
        SetCommonUniforms(camera, viewProj, plane2D, linearOutput, background);
        s_GridShader->SetInt("u_Composite", 0);
        // Gölgelendiricinin 1. çıkışı (composite örtme payı) sahne hedefinin 1. ekine yazmasın
        GLboolean mask1[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
        glGetBooleani_v(GL_COLOR_WRITEMASK, 1, mask1);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBindVertexArray(s_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        Renderer::GetStats().DrawCalls++;
        glColorMaski(1, mask1[0], mask1[1], mask1[2], mask1[3]);

        glDepthMask(depthMask);
        glDepthFunc(depthFunc);
        if (!blend) glDisable(GL_BLEND);
        if (!depthTest) glDisable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE);
    }

    bool EditorGrid::DeferToComposite(const RenderCamera& camera)
    {
        s_Deferred.Pending = false;
        if (!Settings().Visible || camera.Orthographic || !PostProcessStack::WantsMotionData())
            return false;
        glm::vec2 jitter(0.0f);
        if (!PostProcessStack::GetCurrentJitter(jitter))
            jitter = glm::vec2(0.0f);
        s_Deferred.Pending = true;
        s_Deferred.Camera = camera;
        s_Deferred.JitterNDC = jitter;
        return true;
    }

    // 273b: ızgara TAA'dan geçince kamera Viewport'ta yörüngede dönerken (sağ tık) önündeki fotoğrafın / nesnenin hareketiyle yeniden
    // izdüşülüyordu: çizgi o yüzeye göre kaydığı için geçmişte yanlış yerde kalıyor, %96 ağırlıkla tutulan geçmiş yeni çizgiyi
    // göstermiyordu; soluk çizgiler hareket boyunca kayboluyor, bazıları kesik görünüyordu (durunca düzeliyordu). Unity'nin Scene ızgarası
    // ve Unreal'ın editör ilkelleri gibi ızgara artık kenar yumuşatma ve ton eşlemeden sonra çizilir; kendi çizgi yumuşatması
    // (Golus "pristine grid") olduğundan TAA'ya ihtiyacı yoktur. Nesnelerin önünde / arkasında kalması sahne derinliğinden, sahnenin
    // bu karedeki titreşimiyle örnek örnek (eskiden olduğu gibi) bulunur; karışım doğrusal uzaydadır (ton eşleme kapalı aralıkta eskisiyle aynı)
    void EditorGrid::RenderDeferred(uint32_t colorTexture, uint32_t width, uint32_t height, uint32_t sceneDepth)
    {
        if (!s_Deferred.Pending)
            return;
        s_Deferred.Pending = false;
        if (!colorTexture || !sceneDepth || width == 0 || height == 0 || !EnsureShader())
            return;

        if (!s_BelowTexture || s_BelowWidth != width || s_BelowHeight != height)
        {
            if (s_BelowTexture)
                glDeleteTextures(1, &s_BelowTexture);
            glCreateTextures(GL_TEXTURE_2D, 1, &s_BelowTexture);
            glTextureStorage2D(s_BelowTexture, 1, GL_RGBA8, (GLsizei)width, (GLsizei)height);
            glTextureParameteri(s_BelowTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(s_BelowTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            s_BelowWidth = width;
            s_BelowHeight = height;
        }
        glCopyImageSubData(colorTexture, GL_TEXTURE_2D, 0, 0, 0, 0, s_BelowTexture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)width, (GLsizei)height, 1);

        // Örtme payı geçmişi (görünüm başına). Kullanılmayan görünümlerinki (kapatılan / yeniden boyutlanan Viewport) atılır
        ++s_CompositeFrame;
        for (auto it = s_Coverage.begin(); it != s_Coverage.end();)
        {
            if (it->first != colorTexture && s_CompositeFrame - it->second.LastFrame > 120)
            {
                DestroyHistory(it->second);
                it = s_Coverage.erase(it);
            }
            else
                ++it;
        }
        CoverageHistory& h = s_Coverage[colorTexture];
        h.LastFrame = s_CompositeFrame;
        if (!h.Texture[0] || h.Width != width || h.Height != height)
        {
            DestroyHistory(h);
            h.LastFrame = s_CompositeFrame;
            glCreateTextures(GL_TEXTURE_2D, 2, h.Texture);
            glCreateFramebuffers(2, h.Framebuffer);
            const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            for (int i = 0; i < 2; ++i)
            {
                glTextureStorage2D(h.Texture[i], 1, GL_R16F, (GLsizei)width, (GLsizei)height);
                glTextureParameteri(h.Texture[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTextureParameteri(h.Texture[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTextureParameteri(h.Texture[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(h.Texture[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glNamedFramebufferTexture(h.Framebuffer[i], GL_COLOR_ATTACHMENT1, h.Texture[i], 0);
                glNamedFramebufferDrawBuffers(h.Framebuffer[i], 2, buffers);
            }
            h.Width = width;
            h.Height = height;
            h.Valid = false;
        }
        // Son görüntünün dokusu her karede yeniden bağlanır: Viewport çerçeve tamponunu yeniden kurunca doku aynı adla yeniden
        // oluşturulabiliyor, eski bağ silinen dokuda kalıyor ve ızgara görünmeyen bir dokuya çiziliyordu
        glNamedFramebufferTexture(h.Framebuffer[h.Write], GL_COLOR_ATTACHMENT0, colorTexture, 0);
        const int write = h.Write, read = 1 - h.Write;
        const float one = 1.0f;
        glClearTexImage(h.Texture[write], 0, GL_RED, GL_FLOAT, &one);

        const RenderCamera& camera = s_Deferred.Camera;
        const glm::mat4 viewProj = camera.Projection * camera.View;
        // Kamera kıpırdamıyorsa daha uzun birikim (16 titreşim konumu); dönerken / yürürken kısa (kenar yeni yerine çabuk oturur)
        bool still = h.Valid;
        for (int c = 0; c < 4 && still; ++c)
            for (int r = 0; r < 4; ++r)
                if (std::abs(viewProj[c][r] - h.PrevViewProj[c][r]) > 1e-6f * std::max(1.0f, std::abs(viewProj[c][r])))
                {
                    still = false;
                    break;
                }

        GLint prevDraw = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
        GLboolean blend = glIsEnabled(GL_BLEND);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        // Kendi çerçeve tamponu: 0. ek son görüntü, 1. ek örtme payı (Viewport'un nesne kimliği eki dokunulmaz)
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, h.Framebuffer[write]);
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);

        s_GridShader->Bind();
        SetCommonUniforms(camera, viewProj, false, true, false);
        s_GridShader->SetInt("u_Composite", 1);
        s_GridShader->SetFloat2("u_JitterNDC", s_Deferred.JitterNDC);
        s_GridShader->SetFloat2("u_OutputSize", glm::vec2((float)width, (float)height));
        s_GridShader->SetMat4("u_PrevViewProjection", h.PrevViewProj);
        s_GridShader->SetInt("u_CoverageHistoryValid", h.Valid ? 1 : 0);
        s_GridShader->SetFloat("u_CoverageFeedback", still ? 0.93f : 0.85f);
        glBindTextureUnit(0, sceneDepth);
        glBindTextureUnit(1, s_BelowTexture);
        glBindTextureUnit(2, h.Texture[read]);
        s_GridShader->SetInt("u_SceneDepth", 0);
        s_GridShader->SetInt("u_Below", 1);
        s_GridShader->SetInt("u_CoverageHistory", 2);
        glBindVertexArray(s_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        Renderer::GetStats().DrawCalls++;

        h.PrevViewProj = viewProj;
        h.Valid = true;
        h.Write = read;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        glDepthMask(depthMask);
        if (blend) glEnable(GL_BLEND);
        if (depthTest) glEnable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE);
    }

    void EditorGrid::Shutdown()
    {
        s_Deferred.Pending = false;
        for (auto& [key, h] : s_Coverage)
            DestroyHistory(h);
        s_Coverage.clear();
        if (s_BelowTexture)
        {
            glDeleteTextures(1, &s_BelowTexture);
            s_BelowTexture = 0;
            s_BelowWidth = s_BelowHeight = 0;
        }
        s_GridShader.reset();
        if (s_EmptyVAO)
        {
            glDeleteVertexArrays(1, &s_EmptyVAO);
            s_EmptyVAO = 0;
        }
    }
}
