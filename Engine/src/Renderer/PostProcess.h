#pragma once

#include "Core/Base/Base.h"
#include "RHI/Framebuffer.h"
#include "RHI/Shader.h"
#include "RHI/VertexArray.h"
#include "Renderer/RenderSettings.h"
#include "Scene/Scene.h"

#include <memory>
#include <vector>

namespace Conqueror
{
    struct RenderCamera;

    // Bir görünümün (editör viewport'u, oyun paneli, runtime pencere) HDR
    // render hedefleri ve post-process zinciri. Her görünüm kendi örneğine
    // sahiptir; böylece farklı boyutlu paneller birbirinin hedeflerini her
    // karede yeniden oluşturmaz.
    class CQ_API PostProcessStack
    {
    public:
        PostProcessStack() = default;
        ~PostProcessStack();
        PostProcessStack(const PostProcessStack&) = delete;
        PostProcessStack& operator=(const PostProcessStack&) = delete;

        // HDR sahne hedefini bağlar (RGBA16F + derinlik, MSAA kalite ayarından)
        void BeginScene(uint32_t width, uint32_t height);
        // Pixel Perfect Camera (Upscale Render Texture): son geçişte en yakın komşu örnekleme, AA kapalı
        void SetPointUpscale(bool on) { m_PointUpscale = on; }
        void EndScene();

        // Tüm zinciri çalıştırıp sonucu ŞU AN bağlı framebuffer'a yazar.
        // litScene: sahnede aydınlatılan 3D içerik var (Scene::HasRenderable3D); saf 2D'de nötr ton eşleme renkleri korur
        void Render(const PostProcessSettings& settings, const RenderCamera& camera, float time, bool litScene = true);

        void ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor,
                            bool lightVisible, bool haloEnabled, std::shared_ptr<class Texture2D> haloTexture,
                            float haloStrength, bool flareEnabled, float flareStrength,
                            const std::vector<struct Scene::FlareElement>& flareElements);

        uint32_t GetSceneColorTexture() const;
        uint32_t GetSceneDepthTexture() const;
        uint32_t GetAOTexture() const { return m_AOBlur.Texture; }
        uint32_t GetBloomTexture() const { return m_BloomMips.empty() ? 0 : m_BloomMips[0].Texture; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        std::shared_ptr<Framebuffer> GetSceneFramebuffer() const { return m_SceneFramebuffer; }

        // Eski API uyumluluğu (yalnızca AA; ton eşleme yok)
        void ApplyFXAA();
        void ApplySMAA();

        // Renderer3D: şu an bu görünümün sahne hedefine çiziliyorsa Temporal AA alt piksel titreşimi (NDC)
        static bool GetCurrentJitter(glm::vec2& outNDC);
        // SSR normal/pürüzlülük tamponu (yoksa 0)
        uint32_t GetSceneNormalTexture() const;
        // SSGI albedo tamponu (yoksa 0)
        uint32_t GetSceneAlbedoTexture() const;

    private:
        struct RenderTarget
        {
            uint32_t Framebuffer = 0;
            uint32_t Texture = 0;
            uint32_t Width = 0;
            uint32_t Height = 0;
        };

        void CreateTarget(RenderTarget& rt, uint32_t width, uint32_t height, uint32_t internalFormat);
        void DestroyTarget(RenderTarget& rt);
        void EnsureTargets(uint32_t width, uint32_t height);
        void RunSSAO(const PostProcessSettings& settings, const RenderCamera& camera, float time);
        void RunBloom(const PostProcessSettings& settings);
        void RunComposite(const PostProcessSettings& settings, float time);
        void RunFinal(const PostProcessSettings& settings, uint32_t outputFramebuffer, const int outputViewport[4]);

        // Unreal render paketi (PostProcessEffects.cpp): SSR + hacimsel sis/bulut birleştirme, TAA, alan derinliği,
        // hareket bulanıklığı, otomatik pozlama. Her biri yalnızca açıkken çalışır ve hedeflerini ilk kullanımda kurar.
        void RunSceneEffects(const PostProcessSettings& settings, const RenderCamera& camera, float time);
        void RunTAA(const PostProcessSettings& settings, const RenderCamera& camera);
        void RunDepthOfField(const PostProcessSettings& settings, const RenderCamera& camera);
        void RunMotionBlur(const PostProcessSettings& settings, const RenderCamera& camera);
        void RunAutoExposure(const PostProcessSettings& settings, float deltaTime);
        void EnsureTarget(RenderTarget& rt, uint32_t width, uint32_t height, uint32_t internalFormat);
        RenderTarget& NextHDRTarget();
        glm::mat4 JitteredProjection(const RenderCamera& camera) const;
        void ReleaseEffectTargets();

        std::shared_ptr<Framebuffer> m_SceneFramebuffer;     // MSAA olabilir
        std::shared_ptr<Framebuffer> m_ResolvedFramebuffer;  // MSAA çözülmüş (yoksa null)
        RenderTarget m_AO;
        RenderTarget m_AOBlur;
        std::vector<RenderTarget> m_BloomMips;   // aşağı örnekleme zinciri
        std::vector<RenderTarget> m_BloomUp;     // yukarı örnekleme zinciri (geri besleme döngüsü olmasın)
        RenderTarget m_LDR;
        RenderTarget m_SMAAEdges;
        RenderTarget m_Temp;
        uint32_t m_BloomResult = 0;
        bool m_AOValid = false;
        bool m_LitScene = true;
        bool m_PointUpscale = false;   // Pixel Perfect Camera: düşük çözünürlüklü sahne en yakın komşu ile büyütülür
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_Samples = 1;

        // Efekt zinciri
        uint32_t m_Source = 0;          // zincirin şu anki HDR kaynağı (sahne ya da bir efekt çıktısı)
        bool m_AOApplied = false;       // AO sahne efektlerinde uygulandı (birleştirmede tekrar uygulanmaz)
        RenderTarget m_HDR[2];          // RGBA16F ara hedefler (sırayla)
        RenderTarget m_SSR, m_Fog, m_Clouds, m_DOFHalf, m_DOFBlur;
        RenderTarget m_History[2];      // TAA geçmişi
        int m_HistoryIndex = 0;
        bool m_HistoryValid = false;
        uint32_t m_FrameIndex = 0;
        glm::vec2 m_JitterNDC{ 0.0f };
        bool m_JitterActive = false;    // bu karede sahne titreşimli çizildi
        bool m_WantTAA = false;         // sonraki karede titreşim uygula
        bool m_WantNormals = false;     // sahne hedefinde SSR normal eki
        bool m_HasNormals = false;
        bool m_WantAlbedo = false;      // SSGI albedo eki (normal ekiyle birlikte)
        bool m_HasAlbedo = false;
        RenderTarget m_GI, m_GITemp, m_GIHistory[2];
        int m_GIHistoryIndex = 0;
        bool m_GIHistoryValid = false;
        glm::mat4 m_PrevViewProj{ 1.0f };
        bool m_HasPrevViewProj = false;
        float m_LastTime = -1.0f;
        glm::vec2 m_CloudWind{ 0.0f };
        float m_DeltaTime = 0.0f;
        uint32_t m_LumTexture = 0, m_LumFramebuffer = 0;
        RenderTarget m_Adapt[2];
        int m_AdaptIndex = 0;
        bool m_AdaptValid = false;
        bool m_UseAutoExposure = false;
        bool m_TAAResolved = false;
    };

    class CQ_API PostProcess
    {
    public:
        static void Init();
        static void Shutdown();

        // Varsayılan yığın (runtime oyun penceresi ve eski kod için)
        static PostProcessStack& Default();

        // ---- Eski statik API (Default() yığınına yönlendirilir)
        static void BeginScene(uint32_t width, uint32_t height);
        static void EndScene();
        static void ApplyFXAA();
        static void ApplySMAA();
        static void ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor,
                                   bool lightVisible, bool haloEnabled, std::shared_ptr<class Texture2D> haloTexture,
                                   float haloStrength, bool flareEnabled, float flareStrength,
                                   const std::vector<struct Scene::FlareElement>& flareElements);
        static std::shared_ptr<Framebuffer> GetSceneFramebuffer() { return Default().GetSceneFramebuffer(); }
        static uint32_t GetFinalTextureID() { return Default().GetSceneColorTexture(); }

        // Paylaşılan kaynaklar
        static void DrawFullscreenQuad();
        static std::shared_ptr<Shader> GetShader(const char* name);
    };
}
